#include "ScriptMgr.h"
#include "Creature.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "Map.h"
#include "MapManager.h"
#include "World.h"
#include "TaskScheduler.h"
#include "DatabaseEnv.h"
#include "ObjectGuid.h"
#include "Object.h"
#include "Log.h"
#include "DBCStores.h"
#include "SpellScript.h"
#include "SpellAuras.h"
#include "WorldSession.h"
#include "Config.h"

#include "DiscordEvent.h"

#include <ctime>

/* =========================
 * 늑구 상태 구조체
 * ========================= */
struct NeukguState
{
    ObjectGuid guid = ObjectGuid::Empty;
    uint32 mapId = 0;
    time_t startedAt = 0;
    bool active = false;
	bool summons = false;
};

/* =========================
 * 홈페이지 기록 공통 상태 정리
 * ========================= */
static NeukguState g_Neukgu;

static constexpr float NEUKGU_NEAR_ALERT_RANGE = 80.0f;
static constexpr uint32 NEUKGU_NEAR_ALERT_COOLDOWN = 300;

static std::unordered_map<uint32, time_t> g_NeukguNearAlertCooldown;

static void InsertNeukguEventLog(uint32 status)
{
	uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 1);
    if (realmId != 1)
        return;

    LoginDatabase.PExecute(
        "INSERT INTO appdata.neukgu_event_log (start_time, status, capture_guid) "
        "VALUES ({}, {}, 0)",
        uint32(time(nullptr)),
        status
    );
}

static void UpdateNeukguEventStatus(uint32 captureGuid, uint32 status)
{
    uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 1);
    if (realmId != 1)
        return;

    LoginDatabase.PExecute(
        "UPDATE appdata.neukgu_event_log "
        "SET capture_guid = {}, status = {} "
        "WHERE end_time IS NULL "
        "ORDER BY id DESC "
        "LIMIT 1",
        captureGuid,
        status
    );
}

static void FinishNeukguEventLog(uint32 captureGuid, uint32 status)
{
    uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 1);
    if (realmId != 1)
        return;

    LoginDatabase.PExecute(
        "UPDATE appdata.neukgu_event_log "
        "SET capture_guid = {}, status = {}, end_time = {} "
        "WHERE end_time IS NULL "
        "ORDER BY id DESC "
        "LIMIT 1",
        captureGuid,
        status,
        uint32(time(nullptr))
    );
}

static bool IsNeukguFrameRunning()
{
    return g_Neukgu.active || g_Neukgu.summons;
}

static bool HasOpenNeukguFrame()
{
    uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 1);
    if (realmId != 1)
        return false;

    time_t now = time(nullptr);
    tm local = *localtime(&now);

    tm frameStart = local;
    frameStart.tm_hour = 9;
    frameStart.tm_min  = 18;
    frameStart.tm_sec  = 0;

    time_t frameStartTs = mktime(&frameStart);

    if (now < frameStartTs)
        frameStartTs -= 86400;

    QueryResult result = LoginDatabase.PQuery(
        "SELECT id "
        "FROM appdata.neukgu_event_log "
        "WHERE end_time IS NULL "
        "  AND start_time >= {} "
        "ORDER BY id DESC "
        "LIMIT 1",
        uint32(frameStartTs)
    );

    return result != nullptr;
}

/* =========================
 * 공통 상태 정리
 * ========================= */
static void ClearNeukguState()
{
    g_Neukgu.guid = ObjectGuid::Empty;
    g_Neukgu.mapId = 0;
    g_Neukgu.startedAt = 0;
    g_Neukgu.active = false;
	g_Neukgu.summons = false;

	g_NeukguNearAlertCooldown.clear();
}

/* =========================
 * 늑구 위치 힌트
 * ========================= */
static char const* GetFirstValidMapName(MapEntry const* entry)
{
    if (!entry)
        return "Unknown";

    for (uint32 i = 0; i < TOTAL_LOCALES; ++i)
    {
        if (entry->MapName[i] && entry->MapName[i][0] != '\0')
            return entry->MapName[i];
    }

    return "Unknown";
}

static char const* GetFirstValidAreaName(AreaTableEntry const* entry)
{
    if (!entry)
        return "Unknown";

    for (uint32 i = 0; i < TOTAL_LOCALES; ++i)
    {
        if (entry->AreaName[i] && entry->AreaName[i][0] != '\0')
            return entry->AreaName[i];
    }

    return "Unknown";
}

static void SendNeukguHint()
{
    if (!g_Neukgu.active)
        return;

	uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 1);
    if (realmId != 1)
        return;

    QueryResult result = WorldDatabase.PQuery(
        "SELECT map, position_x, position_y "
        "FROM world.creature "
        "WHERE guid = {}",
        g_Neukgu.guid.GetCounter()
    );

    if (!result)
        return;

    Field* f = result->Fetch();

    uint32 mapId = f[0].GetUInt32();
    float x = f[1].GetFloat();
    float y = f[2].GetFloat();

    uint32 phaseMask = PHASEMASK_NORMAL;

    uint32 zoneId = sMapMgr->GetZoneId(phaseMask, mapId, x, y, 0.0f);
    uint32 areaId = sMapMgr->GetAreaId(phaseMask, mapId, x, y, 0.0f);

    MapEntry const* mapEntry = sMapStore.LookupEntry(mapId);
    AreaTableEntry const* zoneEntry = sAreaTableStore.LookupEntry(zoneId);
    AreaTableEntry const* areaEntry = sAreaTableStore.LookupEntry(areaId);

    char const* mapName  = GetFirstValidMapName(mapEntry);
    char const* zoneName = GetFirstValidAreaName(zoneEntry);
    char const* areaName = GetFirstValidAreaName(areaEntry);

    std::string msg = Trinity::StringFormat(
        "늑구의 흔적이 발견되었습니다. {} / {} / {} 주변을 수색해 보세요!",
        mapName, zoneName, areaName
    );

    sWorld->SendServerMessage(SERVER_MSG_STRING, msg.c_str());
	SendServerDiscordEvent("방황하는 늑구", msg,"https://stimpack.pe.kr/auth/register", 16759040, "event");
}

/* =========================
 * 늑구 선택
 * ========================= */
static bool PickRandomWolf()
{
    static uint32 const wolfEntries[] =
    {
        2960, 2924, 2681, 1817, 525, 704, 705
    };

    uint32 pickedEntry = wolfEntries[urand(0, std::size(wolfEntries) - 1)];

    QueryResult result = WorldDatabase.PQuery(
        "SELECT guid, map "
        "FROM world.creature "
        "WHERE id = {} "
        "ORDER BY RAND() LIMIT 1",
        pickedEntry
    );

    if (!result)
        return false;

    Field* f = result->Fetch();

    uint32 guidLow = f[0].GetUInt32();
    uint32 mapId   = f[1].GetUInt32();

    g_Neukgu.guid = ObjectGuid::Create<HighGuid::Unit>(0, guidLow);
    g_Neukgu.mapId = mapId;
    g_Neukgu.startedAt = time(nullptr);
    g_Neukgu.active = true;
	g_Neukgu.summons = true;

	uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 1);
    if (realmId != 1)
		TC_LOG_INFO("server", "[늑구] 선정 entry={} guidLow={} mapId={}",	pickedEntry, guidLow, mapId);

    return true;
}

/* =========================
 * 늑구 지정
 * ========================= */
static void AssignNeukgu(bool announce = true)
{
	uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 1);
	if (realmId != 1)
		return;

    if (IsNeukguFrameRunning())
        return;

    if (!PickRandomWolf())
        return;

    if (!announce)
        return;

    sWorld->SendServerMessage(SERVER_MSG_STRING, "사건 발생! 늑구가 우리를 빠져나갔습니다!");
    SendNeukguHint();

    SendServerDiscordEvent(
        "방황하는 늑구",
        "사건 발생! 늑구가 우리를 빠져나갔습니다!",
        "https://stimpack.pe.kr/auth/register",
        16759040,
        "event"
    );
}

/* =========================
 * 오늘 생성할지 랜덤 판정
 * ========================= */
static bool ShouldSpawnToday()
{
    return urand(0, 1) == 1;
}

/* =========================
 * 월드 이벤트
 * ========================= */
class neukgu_event : public WorldScript
{
public:
    neukgu_event() : WorldScript("neukgu_event") { }

    TaskScheduler _scheduler;

    void StartHintLoop()
	{
		_scheduler.Schedule(Hours(1), [this](TaskContext context)
		{
			if (IsNeukguFrameRunning())
				SendNeukguHint();

			context.Repeat(Hours(1));
		});
}

    void StartExpireLoop()
    {
        _scheduler.Schedule(Seconds(10), [this](TaskContext context)
        {
            if (IsNeukguFrameRunning())
            {
                uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 1);
                time_t now = time(nullptr);

                if (realmId != 1)
                {
                    if (g_Neukgu.startedAt > 0 && now >= (g_Neukgu.startedAt + 300))
                    {
                        sWorld->SendServerMessage(SERVER_MSG_STRING, "테스트 시간이 종료되어 늑구 이벤트가 종료되었습니다.");
                        ClearNeukguState();
                    }
                }
                else
                {
                    tm local = *localtime(&now);

                    if (local.tm_hour == 0 && local.tm_min >= 44)
                    {
                        sWorld->SendServerMessage(SERVER_MSG_STRING, "비보가 전해졌습니다. 늑구를 제시간에 구하지 못했습니다.");
						SendServerDiscordEvent("방황하는 늑구", "비보가 전해졌습니다. 늑구를 제시간에 구하지 못했습니다.","https://stimpack.pe.kr/auth/register", 16759040, "event");
						FinishNeukguEventLog(0, 2);
                        ClearNeukguState();
                    }
                }
            }

            context.Repeat(Seconds(10));
        });
    }

	void StartNearbyAlertLoop()
	{
		_scheduler.Schedule(Seconds(10), [this](TaskContext context)
		{
			if (!g_Neukgu.active || g_Neukgu.guid.IsEmpty())
			{
				context.Repeat(Seconds(10));
				return;
			}

			time_t now = time(nullptr);

			for (auto const& sessionPair : sWorld->GetAllSessions())
			{
				WorldSession* session = sessionPair.second;
				if (!session)
					continue;

				Player* player = session->GetPlayer();
				if (!player || !player->IsInWorld())
					continue;

				if (player->GetMapId() != g_Neukgu.mapId)
					continue;

				Creature* neukgu = player->GetMap()->GetCreature(g_Neukgu.guid);
				if (!neukgu || !neukgu->IsAlive())
					continue;

				if (!player->IsWithinDist(neukgu, NEUKGU_NEAR_ALERT_RANGE, false))
					continue;

				uint32 playerGuid = player->GetGUID().GetCounter();

				auto itr = g_NeukguNearAlertCooldown.find(playerGuid);
				if (itr != g_NeukguNearAlertCooldown.end())
				{
					if (now < itr->second + NEUKGU_NEAR_ALERT_COOLDOWN)
						continue;
				}

				g_NeukguNearAlertCooldown[playerGuid] = now;

				std::string msg = Trinity::StringFormat(
					"근처에서 늑구의 흔적이 느껴집니다. 거리: 약 {} 야드",
					uint32(player->GetDistance(neukgu))
				);

				player->GetSession()->SendNotification(msg.c_str());
			}

			context.Repeat(Seconds(10));
		});
	}

    time_t GetNextSpawnCheckTime() const
    {
        time_t now = time(nullptr);
        tm local = *localtime(&now);

        tm target = local;
        target.tm_hour = 9;
        target.tm_min = 18;
        target.tm_sec = 0;

        time_t targetTime = mktime(&target);

        if (targetTime <= now)
        {
            target.tm_mday += 1;
            target.tm_hour = 9;
            target.tm_min = 18;
            target.tm_sec = 0;
            targetTime = mktime(&target);
        }

        return targetTime;
    }

    void ScheduleProductionSpawnCheck()
    {
        time_t now = time(nullptr);
        time_t nextCheck = GetNextSpawnCheckTime();

        uint32 delayMs = uint32(std::max<int64>(1000, int64(nextCheck - now) * 1000));

        tm local = *localtime(&nextCheck);

        _scheduler.Schedule(Milliseconds(delayMs), [this](TaskContext /*context*/)
        {
            if (!IsNeukguFrameRunning())
                if (ShouldSpawnToday())
				{
					InsertNeukguEventLog(1);
					AssignNeukgu();
				}

            ScheduleProductionSpawnCheck();
        });
    }

    void ScheduleTestSpawnCheck()
    {
        _scheduler.Schedule(Seconds(60), [this](TaskContext /*context*/)
        {
            if (!IsNeukguFrameRunning())
			{
                AssignNeukgu();

				uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 1);
				if (realmId != 1)
					InsertNeukguEventLog(1);
			}
        });
    }

    void OnStartup() override
	{
		StartHintLoop();
		StartExpireLoop();
		StartNearbyAlertLoop();

		uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 1);
		if (realmId != 1)
		{
			ScheduleTestSpawnCheck();
			return;
		}

		if (HasOpenNeukguFrame())
			AssignNeukgu(false);

		ScheduleProductionSpawnCheck();
	}

    void OnUpdate(uint32 diff) override
    {
        _scheduler.Update(diff);
    }
};

static constexpr uint32 SPELL_NEUKGU_CAPTURE = 31368;
static constexpr uint32 NEUKGU_CAPTURE_RATE  = 4000;

class spell_neukgu_capture : public SpellScriptLoader
{
public:
    spell_neukgu_capture() : SpellScriptLoader("spell_neukgu_capture") { }

    class spell_neukgu_capture_AuraScript : public AuraScript
    {
        PrepareAuraScript(spell_neukgu_capture_AuraScript);

        void OnApply(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
        {
            Unit* caster = GetCaster();
            Unit* target = GetTarget();
            if (!caster || !target)
                return;

            Player* player = caster->ToPlayer();
            Creature* creature = target->ToCreature();
            if (!player || !creature)
                return;
        }

        void OnRemove(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
        {
            Unit* caster = GetCaster();
            Unit* target = GetTarget();
            if (!caster || !target)
                return;

            Player* player = caster->ToPlayer();
            Creature* creature = target->ToCreature();
            if (!player || !creature)
                return;

            uint32 targetSpawnId = creature->GetSpawnId();
            uint32 neukguSpawnId = g_Neukgu.guid.GetCounter();
            AuraRemoveMode removeMode = GetTargetApplication()->GetRemoveMode();

            if (removeMode != AURA_REMOVE_BY_EXPIRE)
                return;

            if (!IsNeukguFrameRunning())
            {
                player->GetSession()->SendNotification("지금 늑구 집에 잘있습니다.");
                return;
            }

            if (targetSpawnId != neukguSpawnId)
            {
                player->GetSession()->SendNotification("이건 늑구가 아닙니다.");
                return;
            }

            uint32 roll = urand(1, 10000);
            bool success = (roll <= NEUKGU_CAPTURE_RATE);

            if (success)
            {
                std::string msg = Trinity::StringFormat("수색 성공! {} 님이 늑구를 포획했습니다!", player->GetName());
				SendServerDiscordEvent("방황하는 늑구", msg,"https://stimpack.pe.kr/auth/register", 16759040, "event");

                player->GetSession()->SendNotification("늑구 포획 성공!");
                sWorld->SendServerMessage(SERVER_MSG_STRING, msg.c_str());

				player->AddItem(24515, 1);
				creature->DespawnOrUnsummon(1s);

				UpdateNeukguEventStatus(player->GetGUID().GetCounter(), 3);

				g_Neukgu.guid = ObjectGuid::Empty;
				g_Neukgu.mapId = 0;
				g_Neukgu.active = false;
				g_Neukgu.summons = true;
				return;
            }

            std::string failMsg = Trinity::StringFormat("늑구가 {} 님의 그물을 뿌리치고 도망쳤습니다!",player->GetName());

            player->GetSession()->SendNotification("포획 실패! 늑구가 도망쳤습니다.");
            sWorld->SendServerMessage(SERVER_MSG_STRING, failMsg.c_str());

            g_Neukgu.guid = ObjectGuid::Empty;
            g_Neukgu.mapId = 0;
            g_Neukgu.active = false;
			g_Neukgu.summons = false;

            AssignNeukgu();

            if (IsNeukguFrameRunning())
                SendNeukguHint();
        }

        void Register() override
        {
            OnEffectApply += AuraEffectApplyFn(
                spell_neukgu_capture_AuraScript::OnApply,
                EFFECT_0,
                SPELL_AURA_MOD_ROOT,
                AURA_EFFECT_HANDLE_REAL);
            OnEffectApply += AuraEffectApplyFn(
                spell_neukgu_capture_AuraScript::OnApply,
                EFFECT_1,
                SPELL_AURA_MOD_ROOT,
                AURA_EFFECT_HANDLE_REAL);
            OnEffectApply += AuraEffectApplyFn(
                spell_neukgu_capture_AuraScript::OnApply,
                EFFECT_2,
                SPELL_AURA_MOD_ROOT,
                AURA_EFFECT_HANDLE_REAL);

            AfterEffectRemove += AuraEffectRemoveFn(
                spell_neukgu_capture_AuraScript::OnRemove,
                EFFECT_0,
                SPELL_AURA_MOD_ROOT,
                AURA_EFFECT_HANDLE_REAL);
            AfterEffectRemove += AuraEffectRemoveFn(
                spell_neukgu_capture_AuraScript::OnRemove,
                EFFECT_1,
                SPELL_AURA_MOD_ROOT,
                AURA_EFFECT_HANDLE_REAL);
            AfterEffectRemove += AuraEffectRemoveFn(
                spell_neukgu_capture_AuraScript::OnRemove,
                EFFECT_2,
                SPELL_AURA_MOD_ROOT,
                AURA_EFFECT_HANDLE_REAL);
        }
    };

    AuraScript* GetAuraScript() const override
    {
        return new spell_neukgu_capture_AuraScript();
    }
};


#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"

enum NeukguTurnin
{
    ITEM_NEUKGU_TRACE          = 24515,
    REWARD_RP_AMOUNT           = 10000,
    GOSSIP_MENU_NEUKGU_TURNIN  = 900001,
    GOSSIP_OPTION_NEUKGU       = 0
};

class npc_neukgu_return_owner : public CreatureScript
{
public:
    npc_neukgu_return_owner() : CreatureScript("npc_neukgu_return_owner") { }

    struct npc_neukgu_return_ownerAI : public ScriptedAI
    {
        npc_neukgu_return_ownerAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
			if (player->HasItemCount(ITEM_NEUKGU_TRACE, 1))
				AddGossipItemFor(player, GOSSIP_ICON_DOT, "늑구를 돌려 드리러 왔습니다.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);

            SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            if (action != GOSSIP_ACTION_INFO_DEF)
            {
                CloseGossipMenuFor(player);
                return true;
            }

            if (!player->HasItemCount(ITEM_NEUKGU_TRACE, 1, false))
            {
                player->GetSession()->SendNotification("늑구 흔적이 없습니다.");
                CloseGossipMenuFor(player);
                return true;
            }

            uint32 accountId = player->GetSession()->GetAccountId();
            if (!accountId)
            {
                player->GetSession()->SendNotification("계정 정보를 확인할 수 없습니다.");
                CloseGossipMenuFor(player);
                return true;
            }

            player->DestroyItemCount(ITEM_NEUKGU_TRACE, 1, true);

            LoginDatabase.PExecute(
				"INSERT INTO appdata.account_data (id, reg_point) "
				"VALUES ({}, {}) "
				"ON DUPLICATE KEY UPDATE reg_point = reg_point + {}",
				accountId,
				REWARD_RP_AMOUNT,
				REWARD_RP_AMOUNT
			);

			static constexpr uint32 NEUKGU_LUCKY_ITEM = 23162;
			static constexpr uint32 NEUKGU_LUCKY_ROLL_MAX = 10000;
			static constexpr uint32 NEUKGU_LUCKY_ROLL_CHANCE = 100; // 100 / 10000 = 1%

			if (urand(1, NEUKGU_LUCKY_ROLL_MAX) <= NEUKGU_LUCKY_ROLL_CHANCE)
			{
				player->AddItem(NEUKGU_LUCKY_ITEM, 1);

				std::string msg = Trinity::StringFormat("늑구 구출 보상! {} 님이 [광활한 여행자의 가방] 획득!", player->GetName());
				SendServerDiscordEvent("방황하는 늑구", msg,"https://stimpack.pe.kr/auth/register", 16759040, "event");

				std::string msg1 = Trinity::StringFormat("|cffFFD700[늑구 구출 보상]|r [광활한 여행자의 가방] 획득!");
				sWorld->SendServerMessage(SERVER_MSG_STRING, msg1.c_str(), 0);
			}

			FinishNeukguEventLog(player->GetGUID().GetCounter(), 4);
			ClearNeukguState();

			player->GetSession()->SendNotification("늑구 흔적을 반납했습니다. RP 10000점이 적립되었습니다.");
			CloseGossipMenuFor(player);
			return true;
        }

		void UpdateAI(uint32 diff) override
		{
            if (g_Neukgu.summons)
                me->SetPhaseMask(1, true);
            else
                me->SetPhaseMask(0, true);
		}
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_neukgu_return_ownerAI(creature);
    }
};


/* =========================
 * 등록
 * ========================= */
void AddSC_neukgu_event()
{
    new neukgu_event();
    new spell_neukgu_capture();
	new npc_neukgu_return_owner();
}