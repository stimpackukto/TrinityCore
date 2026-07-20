#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "Creature.h"
#include "Unit.h"
#include "World.h"
#include "WorldSession.h"
#include "TaskScheduler.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Map.h"
#include "Log.h"
#include "StringFormat.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

/*
 * 데스레이스 - 신기루 경주장
 *
 * 구조:
 * 1. 경주 진행자 NPC에게 참가 등록
 * 2. 첫 참가자가 등록하면 5분 대기 시작
 * 3. 5분 후 레이스 시작
 * 4. 참가자 1명 = 기록 레이스
 * 5. 참가자 2명 이상 = 경쟁 레이스
 * 6. 레이스 시작 후에만 WorldScript 감시 루프 작동
 * 7. 트랙 중앙선에서 벗어나면 3초 정지 페널티
 * 8. 레이스 중 랜덤으로 25781 오일바닥 생성
 * 9. 오일바닥에 닿으면 2초 미끄러짐 페널티
 */

struct DeathRacePathPoint
{
    float x;
    float y;
    float z;
};

enum DeathRacePhase : uint8
{
    DEATH_RACE_IDLE = 0,
    DEATH_RACE_WAITING = 1,
    DEATH_RACE_RUNNING = 2
};

enum DeathRaceGossipAction : uint32
{
    DEATH_RACE_ACTION_REGISTER = GOSSIP_ACTION_INFO_DEF + 1,
    DEATH_RACE_ACTION_CANCEL   = GOSSIP_ACTION_INFO_DEF + 2,
    DEATH_RACE_ACTION_STATUS   = GOSSIP_ACTION_INFO_DEF + 3
};

struct DeathRaceParticipant
{
    ObjectGuid playerGuid = ObjectGuid::Empty;
    ObjectGuid racerGuid = ObjectGuid::Empty;

    bool started = false;
    bool finished = false;

    uint32 nextPoint = 1;
    uint32 offtrackCount = 0;
    uint32 oilHitCount = 0;
    uint32 finishRank = 0;

    time_t startedAt = 0;
    time_t finishedAt = 0;
    time_t penaltyUntil = 0;
    time_t nextPenaltyAt = 0;
    time_t oilImmuneUntil = 0;
    bool penaltyActive = false;
};

struct DeathRaceOilSpot
{
    ObjectGuid guid = ObjectGuid::Empty;
    time_t despawnAt = 0;
};

struct DeathRaceState
{
    DeathRacePhase phase = DEATH_RACE_IDLE;

    time_t waitingStartedAt = 0;
    time_t raceStartsAt = 0;
    time_t raceEndsAt = 0;

    bool competitive = false;
    bool waitingLoopScheduled = false;
    bool monitorLoopScheduled = false;
    bool oilLoopScheduled = false;

    uint32 finishCount = 0;

    std::unordered_map<uint32, DeathRaceParticipant> participants;
    std::vector<DeathRaceOilSpot> oilSpots;
};

static DeathRaceState g_DeathRace;
static TaskScheduler g_DeathRaceScheduler;

/* =========================
 * 설정값
 * ========================= */

static constexpr uint32 DEATH_RACE_MAP_ID = 1;
static constexpr uint32 DEATH_RACE_ZONE_ID = 400;
static constexpr uint32 DEATH_RACE_AREA_ID = 2240;

static constexpr uint32 DEATH_RACE_RACER_ENTRY = 40281;
static constexpr uint32 DEATH_RACE_OIL_ENTRY = 25781;

static constexpr uint32 DEATH_RACE_WAIT_SECONDS = 300;
static constexpr uint32 DEATH_RACE_LIMIT_SECONDS = 900;

static constexpr uint32 DEATH_RACE_CHECK_INTERVAL_MS = 500;

static constexpr float DEATH_RACE_START_RADIUS = 18.0f;
static constexpr float DEATH_RACE_POINT_RADIUS = 30.0f;
static constexpr float DEATH_RACE_FINISH_RADIUS = 22.0f;

static constexpr float DEATH_RACE_TRACK_WIDTH = 38.0f;

static constexpr uint32 DEATH_RACE_OFFTRACK_PENALTY_SECONDS = 3;
static constexpr uint32 DEATH_RACE_OFFTRACK_COOLDOWN_SECONDS = 5;

static constexpr uint32 DEATH_RACE_OIL_SPAWN_INTERVAL_SECONDS = 15;
static constexpr uint32 DEATH_RACE_OIL_DESPAWN_SECONDS = 35;
static constexpr uint32 DEATH_RACE_OIL_SLIP_SECONDS = 2;
static constexpr uint32 DEATH_RACE_OIL_IMMUNE_SECONDS = 5;
static constexpr float DEATH_RACE_OIL_TRIGGER_RADIUS = 6.0f;
static constexpr float DEATH_RACE_OIL_RANDOM_OFFSET = 10.0f;

/*
 * 보상은 나중에 확정 후 입력.
 * 0이면 보상 지급 안 함.
 */
static constexpr uint32 DEATH_RACE_REWARD_RECORD_ITEM = 0;
static constexpr uint32 DEATH_RACE_REWARD_WINNER_ITEM = 0;
static constexpr uint32 DEATH_RACE_REWARD_PARTICIPANT_ITEM = 0;

/* =========================
 * 1번 시작 / 도착, 29번 제외
 * ========================= */

static constexpr std::array<DeathRacePathPoint, 28> DEATH_RACE_PATH =
{{
    { -6199.766602f, -3902.937012f, -60.278065f }, // 01 시작 / 도착
    { -6135.674805f, -3902.735840f, -59.900177f }, // 02
    { -6105.943359f, -3895.598145f, -59.999916f }, // 03
    { -6077.071289f, -3886.705322f, -60.140190f }, // 04
    { -5868.875488f, -3788.455322f, -60.215527f }, // 05
    { -5837.540527f, -3783.285156f, -61.072567f }, // 06
    { -5772.320312f, -3785.132812f, -61.680382f }, // 07
    { -5726.913574f, -3796.029297f, -60.616371f }, // 08
    { -5687.304688f, -3823.601562f, -61.861366f }, // 09
    { -5668.572266f, -3853.188477f, -62.401962f }, // 10
    { -5652.035156f, -3900.162109f, -61.659107f }, // 11
    { -5647.029297f, -4091.823242f, -61.430988f }, // 12
    { -5673.140137f, -4141.726074f, -61.658558f }, // 13
    { -5705.886230f, -4169.383301f, -60.618759f }, // 14
    { -5760.379395f, -4187.063477f, -61.501186f }, // 15
    { -5816.841797f, -4196.554199f, -61.722107f }, // 16
    { -6003.148926f, -4199.904785f, -62.402966f }, // 17
    { -6088.107910f, -4187.052246f, -61.967556f }, // 18
    { -6182.629395f, -4191.377930f, -61.681343f }, // 19
    { -6304.366699f, -4217.515625f, -61.316181f }, // 20
    { -6346.907715f, -4211.832520f, -62.356529f }, // 21
    { -6381.010254f, -4188.122070f, -61.931759f }, // 22
    { -6406.144531f, -4162.631348f, -63.192673f }, // 23
    { -6425.964844f, -4102.097656f, -63.852737f }, // 24
    { -6428.076660f, -3973.307617f, -62.834915f }, // 25
    { -6409.350586f, -3930.547607f, -62.293865f }, // 26
    { -6381.949219f, -3905.964844f, -61.874939f }, // 27
    { -6345.900879f, -3902.066895f, -62.413193f }  // 28
}};

/* =========================
 * 공통 유틸
 * ========================= */

static float Distance2DSq(float ax, float ay, float bx, float by)
{
    float dx = ax - bx;
    float dy = ay - by;
    return dx * dx + dy * dy;
}

static float DistanceToSegment2D(float px, float py, DeathRacePathPoint const& a, DeathRacePathPoint const& b)
{
    float vx = b.x - a.x;
    float vy = b.y - a.y;

    float wx = px - a.x;
    float wy = py - a.y;

    float lenSq = vx * vx + vy * vy;

    if (lenSq <= 0.0001f)
        return std::sqrt(Distance2DSq(px, py, a.x, a.y));

    float t = (wx * vx + wy * vy) / lenSq;

    if (t < 0.0f)
        t = 0.0f;
    else if (t > 1.0f)
        t = 1.0f;

    float closestX = a.x + t * vx;
    float closestY = a.y + t * vy;

    return std::sqrt(Distance2DSq(px, py, closestX, closestY));
}

static float GetDistanceToRaceLine(Unit const* unit)
{
    if (!unit)
        return std::numeric_limits<float>::max();

    float px = unit->GetPositionX();
    float py = unit->GetPositionY();

    float best = std::numeric_limits<float>::max();

    for (uint32 i = 0; i < DEATH_RACE_PATH.size(); ++i)
    {
        DeathRacePathPoint const& a = DEATH_RACE_PATH[i];
        DeathRacePathPoint const& b = DEATH_RACE_PATH[(i + 1) % DEATH_RACE_PATH.size()];

        float dist = DistanceToSegment2D(px, py, a, b);

        if (dist < best)
            best = dist;
    }

    return best;
}

static bool IsNearPathPoint(Unit const* unit, uint32 index, float radius)
{
    if (!unit)
        return false;

    if (index >= DEATH_RACE_PATH.size())
        return false;

    DeathRacePathPoint const& p = DEATH_RACE_PATH[index];

    return Distance2DSq(
        unit->GetPositionX(),
        unit->GetPositionY(),
        p.x,
        p.y
    ) <= radius * radius;
}

static Player* FindOnlinePlayer(uint32 playerLowGuid)
{
    for (auto const& sessionPair : sWorld->GetAllSessions())
    {
        WorldSession* session = sessionPair.second;
        if (!session)
            continue;

        Player* player = session->GetPlayer();
        if (!player)
            continue;

        if (player->GetGUID().GetCounter() == playerLowGuid)
            return player;
    }

    return nullptr;
}

static Player* GetAnyOnlineParticipant()
{
    for (auto const& pair : g_DeathRace.participants)
    {
        Player* player = FindOnlinePlayer(pair.first);
        if (player && player->IsInWorld() && player->GetMapId() == DEATH_RACE_MAP_ID)
            return player;
    }

    return nullptr;
}

static bool IsDeathRaceRacer(Creature const* creature)
{
    if (!creature)
        return false;

    return creature->GetEntry() == DEATH_RACE_RACER_ENTRY;
}

static Creature* GetPlayerControlledRacer(Player* player)
{
    if (!player)
        return nullptr;

    ObjectGuid charmGuid = player->GetGuidValue(UNIT_FIELD_CHARM);
    if (charmGuid.IsEmpty())
        return nullptr;

    Unit* charm = ObjectAccessor::GetUnit(*player, charmGuid);
    if (!charm)
        return nullptr;

    Creature* creature = charm->ToCreature();
    if (!creature)
        return nullptr;

    if (!IsDeathRaceRacer(creature))
        return nullptr;

    return creature;
}

static void ResetDeathRace()
{
    g_DeathRace.phase = DEATH_RACE_IDLE;
    g_DeathRace.waitingStartedAt = 0;
    g_DeathRace.raceStartsAt = 0;
    g_DeathRace.raceEndsAt = 0;
    g_DeathRace.competitive = false;
    g_DeathRace.waitingLoopScheduled = false;
    g_DeathRace.monitorLoopScheduled = false;
    g_DeathRace.oilLoopScheduled = false;
    g_DeathRace.finishCount = 0;
    g_DeathRace.participants.clear();
    g_DeathRace.oilSpots.clear();
}

static void BroadcastDeathRace(std::string const& msg)
{
    sWorld->SendServerMessage(SERVER_MSG_STRING, msg.c_str());
}

/* =========================
 * 페널티 / 오일
 * ========================= */

static void StopPenaltyIfNeeded(DeathRaceParticipant& state, Creature* racer)
{
    if (!racer)
        return;

    if (!state.penaltyActive)
        return;

    if (time(nullptr) < state.penaltyUntil)
        return;

    racer->SetControlled(false, UNIT_STATE_ROOT);
    state.penaltyActive = false;
}

static void ApplyStopPenalty(Player* player, Creature* racer, DeathRaceParticipant& state, uint32 seconds, char const* message)
{
    if (!player || !racer)
        return;

    time_t now = time(nullptr);

    state.penaltyUntil = now + seconds;
    state.penaltyActive = true;

    racer->StopMoving();
    racer->SetControlled(true, UNIT_STATE_ROOT);

    player->GetSession()->SendNotification("%s", message);
}

static void ApplyOfftrackPenalty(Player* player, Creature* racer, DeathRaceParticipant& state)
{
    if (!player || !racer)
        return;

    time_t now = time(nullptr);

    if (now < state.nextPenaltyAt)
        return;

    state.offtrackCount++;
    state.nextPenaltyAt = now + DEATH_RACE_OFFTRACK_COOLDOWN_SECONDS;

    ApplyStopPenalty(player, racer, state, DEATH_RACE_OFFTRACK_PENALTY_SECONDS, "트랙을 벗어났습니다. 3초간 정지됩니다.");
}

static void ApplyOilSlip(Player* player, Creature* racer, DeathRaceParticipant& state)
{
    if (!player || !racer)
        return;

    time_t now = time(nullptr);

    if (now < state.oilImmuneUntil)
        return;

    state.oilHitCount++;
    state.oilImmuneUntil = now + DEATH_RACE_OIL_IMMUNE_SECONDS;

    ApplyStopPenalty(player, racer, state, DEATH_RACE_OIL_SLIP_SECONDS, "오일바닥에 미끄러졌습니다. 2초간 조작할 수 없습니다.");
}

static void CleanupOilSpots(Player* anyPlayer)
{
    if (!anyPlayer)
        return;

    time_t now = time(nullptr);

    g_DeathRace.oilSpots.erase(
        std::remove_if(
            g_DeathRace.oilSpots.begin(),
            g_DeathRace.oilSpots.end(),
            [anyPlayer, now](DeathRaceOilSpot const& spot)
            {
                if (spot.guid.IsEmpty())
                    return true;

                if (now >= spot.despawnAt)
                    return true;

                Creature* oil = anyPlayer->GetMap()->GetCreature(spot.guid);
                return oil == nullptr;
            }
        ),
        g_DeathRace.oilSpots.end()
    );
}

static void SpawnRandomOilSpot()
{
    if (g_DeathRace.phase != DEATH_RACE_RUNNING)
        return;

    Player* player = GetAnyOnlineParticipant();
    if (!player)
        return;

    if (!player->IsInWorld() || player->GetMapId() != DEATH_RACE_MAP_ID)
        return;

    uint32 index = urand(1, DEATH_RACE_PATH.size() - 1);
    DeathRacePathPoint const& p = DEATH_RACE_PATH[index];

    float offsetX = float(irand(-int32(DEATH_RACE_OIL_RANDOM_OFFSET), int32(DEATH_RACE_OIL_RANDOM_OFFSET)));
    float offsetY = float(irand(-int32(DEATH_RACE_OIL_RANDOM_OFFSET), int32(DEATH_RACE_OIL_RANDOM_OFFSET)));

    Creature* oil = player->SummonCreature(
		DEATH_RACE_OIL_ENTRY,
		p.x + offsetX,
		p.y + offsetY,
		p.z,
		frand(0.0f, 6.28f),
		TEMPSUMMON_TIMED_DESPAWN,
		Milliseconds(DEATH_RACE_OIL_DESPAWN_SECONDS * 1000)
	);

    if (!oil)
        return;

    DeathRaceOilSpot spot;
    spot.guid = oil->GetGUID();
    spot.despawnAt = time(nullptr) + DEATH_RACE_OIL_DESPAWN_SECONDS;

    g_DeathRace.oilSpots.push_back(spot);
}

static void CheckOilHit(Player* player, Creature* racer, DeathRaceParticipant& state)
{
    if (!player || !racer)
        return;

    CleanupOilSpots(player);

    for (DeathRaceOilSpot const& spot : g_DeathRace.oilSpots)
    {
        Creature* oil = racer->GetMap()->GetCreature(spot.guid);
        if (!oil)
            continue;

        if (!oil->IsInWorld())
            continue;

        if (racer->GetDistance(oil) <= DEATH_RACE_OIL_TRIGGER_RADIUS)
        {
            ApplyOilSlip(player, racer, state);
            return;
        }
    }
}

/* =========================
 * 레이스 진행
 * ========================= */

static void FinishParticipant(Player* player, Creature* racer, DeathRaceParticipant& state)
{
    if (!player)
        return;

    state.finished = true;
    state.finishedAt = time(nullptr);

    if (racer && state.penaltyActive)
    {
        racer->SetControlled(false, UNIT_STATE_ROOT);
        state.penaltyActive = false;
    }

    g_DeathRace.finishCount++;
    state.finishRank = g_DeathRace.finishCount;

    uint32 clearTime = 0;
    if (state.startedAt > 0 && state.finishedAt >= state.startedAt)
        clearTime = uint32(state.finishedAt - state.startedAt);

    if (!g_DeathRace.competitive)
    {
        if (DEATH_RACE_REWARD_RECORD_ITEM != 0)
            player->AddItem(DEATH_RACE_REWARD_RECORD_ITEM, 1);

        std::string msg = Trinity::StringFormat(
            "|cff00ff00[데스레이스 기록]|r {} 님이 신기루 경주장 한 바퀴를 완주했습니다! 기록: {}초 / 이탈: {}회 / 오일: {}회",
            player->GetName(),
            clearTime,
            state.offtrackCount,
            state.oilHitCount
        );

        BroadcastDeathRace(msg);
        player->GetSession()->SendNotification("데스레이스 기록 레이스 완주!");
        return;
    }

    if (state.finishRank == 1)
    {
        if (DEATH_RACE_REWARD_WINNER_ITEM != 0)
            player->AddItem(DEATH_RACE_REWARD_WINNER_ITEM, 1);
    }
    else
    {
        if (DEATH_RACE_REWARD_PARTICIPANT_ITEM != 0)
            player->AddItem(DEATH_RACE_REWARD_PARTICIPANT_ITEM, 1);
    }

    std::string msg = Trinity::StringFormat(
        "|cff00ff00[데스레이스]|r {} 님이 {}등으로 완주했습니다! 기록: {}초 / 이탈: {}회 / 오일: {}회",
        player->GetName(),
        state.finishRank,
        clearTime,
        state.offtrackCount,
        state.oilHitCount
    );

    BroadcastDeathRace(msg);
    player->GetSession()->SendNotification("데스레이스 완주!");
}

static bool AreAllParticipantsFinished()
{
    if (g_DeathRace.participants.empty())
        return true;

    for (auto const& pair : g_DeathRace.participants)
    {
        if (!pair.second.finished)
            return false;
    }

    return true;
}

static void EndRaceIfNeeded()
{
    if (g_DeathRace.phase != DEATH_RACE_RUNNING)
        return;

    time_t now = time(nullptr);

    if (AreAllParticipantsFinished())
    {
        BroadcastDeathRace("|cff00ff00[데스레이스]|r 모든 참가자가 완주했습니다. 레이스가 종료됩니다.");
        ResetDeathRace();
        return;
    }

    if (now >= g_DeathRace.raceEndsAt)
    {
        BroadcastDeathRace("|cffff0000[데스레이스]|r 제한 시간이 종료되어 레이스가 종료됩니다.");
        ResetDeathRace();
        return;
    }
}

static void UpdateParticipant(Player* player, DeathRaceParticipant& state)
{
    if (!player || !player->IsInWorld())
        return;

    if (state.finished)
        return;

    if (player->GetMapId() != DEATH_RACE_MAP_ID)
        return;

    if (player->GetZoneId() != DEATH_RACE_ZONE_ID)
        return;

    Creature* racer = GetPlayerControlledRacer(player);
    if (!racer)
        return;

    if (!racer->IsInWorld() || !racer->IsAlive())
        return;

    if (racer->GetMapId() != DEATH_RACE_MAP_ID)
        return;

    if (!state.started)
    {
        if (!IsNearPathPoint(racer, 0, DEATH_RACE_START_RADIUS))
            return;

        state.started = true;
        state.racerGuid = racer->GetGUID();
        state.nextPoint = 1;
        state.startedAt = time(nullptr);

        player->GetSession()->SendNotification("데스레이스 출발! 중앙 트랙을 따라 한 바퀴 완주하십시오.");
        return;
    }

    StopPenaltyIfNeeded(state, racer);

    if (time(nullptr) < state.penaltyUntil)
    {
        racer->StopMoving();
        return;
    }

    float lineDistance = GetDistanceToRaceLine(racer);
    if (lineDistance > DEATH_RACE_TRACK_WIDTH)
    {
        ApplyOfftrackPenalty(player, racer, state);
        return;
    }

    CheckOilHit(player, racer, state);

    if (time(nullptr) < state.penaltyUntil)
    {
        racer->StopMoving();
        return;
    }

    if (state.nextPoint < DEATH_RACE_PATH.size())
    {
        if (IsNearPathPoint(racer, state.nextPoint, DEATH_RACE_POINT_RADIUS))
        {
            state.nextPoint++;

            if (state.nextPoint == 5 || state.nextPoint == 10 || state.nextPoint == 15 || state.nextPoint == 20 || state.nextPoint == 25)
                player->GetSession()->SendNotification("좋습니다. 계속 트랙을 따라가십시오.");
        }

        return;
    }

    if (IsNearPathPoint(racer, 0, DEATH_RACE_FINISH_RADIUS))
        FinishParticipant(player, racer, state);
}

static void MonitorRaceTick()
{
    if (g_DeathRace.phase != DEATH_RACE_RUNNING)
        return;

    for (auto& pair : g_DeathRace.participants)
    {
        Player* player = FindOnlinePlayer(pair.first);
        if (!player)
            continue;

        UpdateParticipant(player, pair.second);
    }

    EndRaceIfNeeded();
}

static void ScheduleMonitorLoop()
{
    if (g_DeathRace.monitorLoopScheduled)
        return;

    g_DeathRace.monitorLoopScheduled = true;

    g_DeathRaceScheduler.Schedule(Milliseconds(DEATH_RACE_CHECK_INTERVAL_MS), [](TaskContext context)
    {
        if (g_DeathRace.phase != DEATH_RACE_RUNNING)
        {
            g_DeathRace.monitorLoopScheduled = false;
            return;
        }

        MonitorRaceTick();
        context.Repeat(Milliseconds(DEATH_RACE_CHECK_INTERVAL_MS));
    });
}

static void ScheduleOilLoop()
{
    if (g_DeathRace.oilLoopScheduled)
        return;

    g_DeathRace.oilLoopScheduled = true;

    g_DeathRaceScheduler.Schedule(Seconds(DEATH_RACE_OIL_SPAWN_INTERVAL_SECONDS), [](TaskContext context)
    {
        if (g_DeathRace.phase != DEATH_RACE_RUNNING)
        {
            g_DeathRace.oilLoopScheduled = false;
            return;
        }

        SpawnRandomOilSpot();
        context.Repeat(Seconds(DEATH_RACE_OIL_SPAWN_INTERVAL_SECONDS));
    });
}

static void StartDeathRaceNow()
{
    if (g_DeathRace.phase != DEATH_RACE_WAITING)
        return;

    if (g_DeathRace.participants.empty())
    {
        BroadcastDeathRace("|cffff0000[데스레이스]|r 참가자가 없어 레이스가 취소되었습니다.");
        ResetDeathRace();
        return;
    }

    g_DeathRace.phase = DEATH_RACE_RUNNING;
    g_DeathRace.competitive = g_DeathRace.participants.size() >= 2;
    g_DeathRace.raceEndsAt = time(nullptr) + DEATH_RACE_LIMIT_SECONDS;
    g_DeathRace.finishCount = 0;
    g_DeathRace.monitorLoopScheduled = false;
    g_DeathRace.oilLoopScheduled = false;

    std::string msg = Trinity::StringFormat(
        "|cff00ff00[데스레이스]|r 레이스가 시작되었습니다! 참가자: {}명 / 방식: {}",
        uint32(g_DeathRace.participants.size()),
        g_DeathRace.competitive ? "경쟁 레이스" : "기록 레이스"
    );

    BroadcastDeathRace(msg);

    ScheduleMonitorLoop();
    ScheduleOilLoop();
}

static void ScheduleWaitingLoop()
{
    if (g_DeathRace.waitingLoopScheduled)
        return;

    g_DeathRace.waitingLoopScheduled = true;

    g_DeathRaceScheduler.Schedule(Seconds(10), [](TaskContext context)
    {
        if (g_DeathRace.phase != DEATH_RACE_WAITING)
        {
            g_DeathRace.waitingLoopScheduled = false;
            return;
        }

        time_t now = time(nullptr);

        if (now >= g_DeathRace.raceStartsAt)
        {
            g_DeathRace.waitingLoopScheduled = false;
            StartDeathRaceNow();
            return;
        }

        uint32 remain = uint32(g_DeathRace.raceStartsAt - now);

        if (remain == 300 || remain == 180 || remain == 120 || remain == 60 || remain == 30 || remain == 10)
        {
            std::string msg = Trinity::StringFormat(
                "|cff00ff00[데스레이스]|r {}초 후 레이스가 시작됩니다. 현재 참가자: {}명",
                remain,
                uint32(g_DeathRace.participants.size())
            );

            BroadcastDeathRace(msg);
        }

        context.Repeat(Seconds(10));
    });
}

static void BeginWaitingIfNeeded()
{
    if (g_DeathRace.phase != DEATH_RACE_IDLE)
        return;

    time_t now = time(nullptr);

    g_DeathRace.phase = DEATH_RACE_WAITING;
    g_DeathRace.waitingStartedAt = now;
    g_DeathRace.raceStartsAt = now + DEATH_RACE_WAIT_SECONDS;
    g_DeathRace.raceEndsAt = 0;
    g_DeathRace.competitive = false;
    g_DeathRace.finishCount = 0;
    g_DeathRace.waitingLoopScheduled = false;
    g_DeathRace.monitorLoopScheduled = false;
    g_DeathRace.oilLoopScheduled = false;
    g_DeathRace.oilSpots.clear();

    BroadcastDeathRace("|cff00ff00[데스레이스]|r 참가 접수가 시작되었습니다. 5분 후 레이스가 시작됩니다.");

    ScheduleWaitingLoop();
}

/* =========================
 * 경주 진행자 NPC
 * ========================= */

class npc_death_race_master : public CreatureScript
{
public:
    npc_death_race_master() : CreatureScript("npc_death_race_master") { }

    struct npc_death_race_masterAI : public ScriptedAI
    {
        npc_death_race_masterAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            if (g_DeathRace.phase == DEATH_RACE_IDLE || g_DeathRace.phase == DEATH_RACE_WAITING)
                AddGossipItemFor(player, GOSSIP_ICON_DOT, "데스레이스 참가 등록", GOSSIP_SENDER_MAIN, DEATH_RACE_ACTION_REGISTER);

            if (g_DeathRace.phase == DEATH_RACE_WAITING)
                AddGossipItemFor(player, GOSSIP_ICON_DOT, "데스레이스 참가 취소", GOSSIP_SENDER_MAIN, DEATH_RACE_ACTION_CANCEL);

            AddGossipItemFor(player, GOSSIP_ICON_DOT, "데스레이스 상태 확인", GOSSIP_SENDER_MAIN, DEATH_RACE_ACTION_STATUS);

            SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            if (!player)
                return true;

            uint32 playerGuid = player->GetGUID().GetCounter();

            if (action == DEATH_RACE_ACTION_REGISTER)
            {
                if (g_DeathRace.phase == DEATH_RACE_RUNNING)
                {
                    player->GetSession()->SendNotification("이미 레이스가 진행 중입니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                BeginWaitingIfNeeded();

                if (g_DeathRace.participants.find(playerGuid) != g_DeathRace.participants.end())
                {
                    player->GetSession()->SendNotification("이미 데스레이스에 등록되어 있습니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                DeathRaceParticipant participant;
                participant.playerGuid = player->GetGUID();

                g_DeathRace.participants[playerGuid] = participant;

                std::string msg = Trinity::StringFormat(
                    "|cff00ff00[데스레이스]|r {} 님이 참가 등록했습니다. 현재 참가자: {}명",
                    player->GetName(),
                    uint32(g_DeathRace.participants.size())
                );

                BroadcastDeathRace(msg);

                player->GetSession()->SendNotification("데스레이스 참가 등록 완료. 시작 전까지 장난감 자동차를 준비하십시오.");
                CloseGossipMenuFor(player);
                return true;
            }

            if (action == DEATH_RACE_ACTION_CANCEL)
            {
                if (g_DeathRace.phase != DEATH_RACE_WAITING)
                {
                    player->GetSession()->SendNotification("지금은 참가 취소를 할 수 없습니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                auto itr = g_DeathRace.participants.find(playerGuid);
                if (itr == g_DeathRace.participants.end())
                {
                    player->GetSession()->SendNotification("등록된 참가자가 아닙니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                g_DeathRace.participants.erase(itr);

                player->GetSession()->SendNotification("데스레이스 참가를 취소했습니다.");

                if (g_DeathRace.participants.empty())
                {
                    BroadcastDeathRace("|cffff0000[데스레이스]|r 참가자가 없어 접수가 취소되었습니다.");
                    ResetDeathRace();
                }

                CloseGossipMenuFor(player);
                return true;
            }

            if (action == DEATH_RACE_ACTION_STATUS)
            {
                if (g_DeathRace.phase == DEATH_RACE_IDLE)
                {
                    player->GetSession()->SendNotification("현재 데스레이스 접수는 진행 중이 아닙니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                if (g_DeathRace.phase == DEATH_RACE_WAITING)
                {
                    uint32 remain = 0;
                    time_t now = time(nullptr);

                    if (g_DeathRace.raceStartsAt > now)
                        remain = uint32(g_DeathRace.raceStartsAt - now);

                    std::string msg = Trinity::StringFormat(
                        "데스레이스 시작까지 {}초 남았습니다. 참가자: {}명",
                        remain,
                        uint32(g_DeathRace.participants.size())
                    );

                    player->GetSession()->SendNotification("%s", msg.c_str());
                    CloseGossipMenuFor(player);
                    return true;
                }

                if (g_DeathRace.phase == DEATH_RACE_RUNNING)
                {
                    player->GetSession()->SendNotification("데스레이스가 진행 중입니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }
            }

            CloseGossipMenuFor(player);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_death_race_masterAI(creature);
    }
};

/* =========================
 * 월드 스크립트
 * ========================= */

class death_race_event : public WorldScript
{
public:
    death_race_event() : WorldScript("death_race_event") { }

    void OnUpdate(uint32 diff) override
    {
        if (g_DeathRace.phase == DEATH_RACE_IDLE)
            return;

        g_DeathRaceScheduler.Update(diff);
    }
};

/* =========================
 * 등록
 * ========================= */

void AddSC_death_race_event()
{
    new death_race_event();
    new npc_death_race_master();
}