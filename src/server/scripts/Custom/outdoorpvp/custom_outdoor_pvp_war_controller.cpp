#include "CustomOutdoorPvpWarController.h"

#include "Chat.h"
#include "Config.h"
#include "Containers.h"
#include "CustomRP.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "StringFormat.h"
#include "Util.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    static constexpr uint32 MINUTE_SECONDS = 60;
    static constexpr uint32 HOUR_SECONDS = 60 * MINUTE_SECONDS;

    // 1 = 하루 1회, 7 = 일주일 1회
    static constexpr uint32 OUTDOOR_WAR_PERIOD_DAYS = 1;

    // 테스트 모드: 서버 시작 후 2분 뒤 야외전장 시작
    static constexpr bool OUTDOOR_WAR_TEST_MODE = false;
    static constexpr uint32 OUTDOOR_WAR_TEST_START_DELAY_SECONDS = 120;

    // 야외전장 시작 시간대: 오후 6시 ~ 오후 9시 사이
    static constexpr uint32 OUTDOOR_WAR_START_HOUR_MIN = 18;
    static constexpr uint32 OUTDOOR_WAR_START_HOUR_MAX = 21;

    // 전쟁 최대 지속시간
    static constexpr uint32 OUTDOOR_WAR_DURATION_SECONDS = 2 * HOUR_SECONDS;

    // 지옥불 반도 승리 조건: 3개 깃발 점령 후 10분 유지
    static constexpr uint32 HELLFIRE_HOLD_SECONDS = 10 * MINUTE_SECONDS;

    // 실리더스 승리 조건: 먼저 200점
    static constexpr uint32 SILITHUS_WIN_SCORE = 200;

    // 나그란드 승리 조건: 경비병 1명 이상 생존 상태로 5분 유지
    static constexpr uint32 NAGRAND_HOLD_SECONDS = 5 * MINUTE_SECONDS;

    // 컨트롤러 갱신 주기
    static constexpr uint32 OUTDOOR_WAR_UPDATE_MS = 5000;

    // 보상 설정
    static constexpr uint32 REWARD_REG_POINT = 10000;
    static constexpr uint32 REWARD_ITEM_ENTRY = 49426;
    static constexpr uint32 REWARD_ITEM_COUNT = 20;

    // 불굴의 의지 설정
    static constexpr uint32 OUTDOOR_WAR_TENACITY_SPELL = 58549;
    static constexpr uint32 OUTDOOR_WAR_TENACITY_MAX_STACK = 5;

    // 포털 phase
    static constexpr uint32 OUTDOOR_WAR_PORTAL_PHASE_SHOW = 1;
    static constexpr uint32 OUTDOOR_WAR_PORTAL_PHASE_HIDE = 2;

    static constexpr int32 TEAM_ID_NEUTRAL = -1;

    static constexpr char const* OUTDOOR_WAR_CONTROLLER_PVP_TYPE_REALM1 = "outdoor_war_controller";
    static constexpr char const* OUTDOOR_WAR_CONTROLLER_PVP_TYPE_REALM2 = "outdoor_war_controller2";
    static constexpr char const* OUTDOOR_WAR_CONTROLLER_STATE_KEY = "state";

    // 지옥불 반도 보상 인정 지역
    // 3483 = 지옥불 반도 전체 zone
    // 3669 / 3670 / 3671 = 점령 거점 주변 area
    static constexpr std::array<uint32, 4> HELLFIRE_REWARD_AREA_IDS =
    {{
        3483,
        3669,
        3670,
        3671
    }};

	// 나그란드 할라아 보상 인정 지역
	// 3628 = 할라아 area
	static constexpr uint32 NAGRAND_HALAA_REWARD_AREA_ID = 3628;

    enum class OutdoorWarType : uint8
    {
        None = 0,
        Hellfire = 1,
        Silithus = 2,
        Zangarmarsh = 3,
        Nagrand = 4,
        Terokkar = 5,
        EasternPlaguelands = 6
    };

    struct OutdoorWarDefinition
    {
        OutdoorWarType Type;
        char const* Name;
        uint32 MapId;
        uint32 ZoneId;
        bool Enabled;
    };

	static constexpr std::array<uint32, 5> REWARD_EXECUTION_FRAGMENT_ITEMS =
	{{
		56924,
		56925,
		56926,
		56927,
		56928
	}};

    // 테스트용: 나그란드만 활성화
    static constexpr std::array<OutdoorWarDefinition, 6> OutdoorWarDefinitions =
    {{
        { OutdoorWarType::Hellfire, "지옥불 반도", 530, 3483, true },
        { OutdoorWarType::Silithus, "실리더스", 1, 1377, true },
        { OutdoorWarType::Zangarmarsh, "장가르 습지대", 530, 3521, false },
        { OutdoorWarType::Nagrand, "나그란드", 530, 3518, true },
        { OutdoorWarType::Terokkar, "테로카르 숲", 530, 3519, false },
        { OutdoorWarType::EasternPlaguelands, "동부 역병지대", 0, 139, false }
    }};

    struct OutdoorWarPortalDefinition
    {
        OutdoorWarType Type;
        uint32 Entry;
        uint32 SpawnGuid;
    };

    static constexpr std::array<OutdoorWarPortalDefinition, 8> OutdoorWarPortalDefinitions =
    {{
        { OutdoorWarType::Silithus, 244608, 167284 },
        { OutdoorWarType::Silithus, 244608, 167285 },

        { OutdoorWarType::Hellfire, 244609, 167286 },
        { OutdoorWarType::Hellfire, 244609, 167287 },

        { OutdoorWarType::Zangarmarsh, 244610, 167288 },
        { OutdoorWarType::Zangarmarsh, 244610, 167289 },

        { OutdoorWarType::Nagrand, 244611, 167290 },
        { OutdoorWarType::Nagrand, 244611, 167291 }
    }};

    struct ParticipantInfo
    {
        uint64 Guid = 0;
        uint32 AccountId = 0;
        int32 TeamId = TEAM_ID_NEUTRAL;
        uint32 ActiveSeconds = 0;
        uint64 LastSeenAt = 0;
        bool Rewarded = false;
    };

    uint32 GetRealmId()
    {
        return sConfigMgr->GetIntDefault("RealmID", 0);
    }

    char const* GetOutdoorWarControllerPvpType()
    {
        if (GetRealmId() == 2)
            return OUTDOOR_WAR_CONTROLLER_PVP_TYPE_REALM2;

        return OUTDOOR_WAR_CONTROLLER_PVP_TYPE_REALM1;
    }

    bool JsonReadInteger(std::string const& json, std::string const& key, long long& value)
    {
        std::string pattern = Trinity::StringFormat("\"{}\"", key);

        size_t pos = json.find(pattern);
        if (pos == std::string::npos)
            return false;

        pos = json.find(':', pos);
        if (pos == std::string::npos)
            return false;

        ++pos;

        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
            ++pos;

        bool negative = false;

        if (pos < json.size() && json[pos] == '-')
        {
            negative = true;
            ++pos;
        }

        if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos])))
            return false;

        long long number = 0;

        while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos])))
        {
            number = (number * 10) + (json[pos] - '0');
            ++pos;
        }

        value = negative ? -number : number;
        return true;
    }

    bool JsonReadBool(std::string const& json, std::string const& key, bool& value)
    {
        long long number = 0;

        if (!JsonReadInteger(json, key, number))
            return false;

        value = number != 0;
        return true;
    }

    bool JsonReadInt32(std::string const& json, std::string const& key, int32& value)
    {
        long long number = 0;

        if (!JsonReadInteger(json, key, number))
            return false;

        value = int32(number);
        return true;
    }

    bool JsonReadUInt32(std::string const& json, std::string const& key, uint32& value)
    {
        long long number = 0;

        if (!JsonReadInteger(json, key, number))
            return false;

        if (number < 0)
            return false;

        value = uint32(number);
        return true;
    }

    bool JsonReadUInt64(std::string const& json, std::string const& key, uint64& value)
    {
        long long number = 0;

        if (!JsonReadInteger(json, key, number))
            return false;

        if (number < 0)
            return false;

        value = uint64(number);
        return true;
    }

    bool IsValidTeam(int32 teamId)
    {
        return teamId == int32(TEAM_ALLIANCE) || teamId == int32(TEAM_HORDE);
    }

    char const* GetTeamName(int32 teamId)
    {
        if (teamId == int32(TEAM_ALLIANCE))
            return "얼라이언스";

        if (teamId == int32(TEAM_HORDE))
            return "호드";

        return "중립";
    }

    bool IsHellfireRewardAreaId(uint32 areaOrZoneId)
    {
        for (uint32 rewardAreaId : HELLFIRE_REWARD_AREA_IDS)
        {
            if (areaOrZoneId == rewardAreaId)
                return true;
        }

        return false;
    }

    OutdoorWarDefinition const* GetWarDefinition(OutdoorWarType type)
    {
        for (OutdoorWarDefinition const& definition : OutdoorWarDefinitions)
        {
            if (definition.Type == type)
                return &definition;
        }

        return nullptr;
    }

    class OutdoorPvpWarMgr
    {
    public:
        static OutdoorPvpWarMgr& Instance()
        {
            static OutdoorPvpWarMgr instance;
            return instance;
        }

        void Initialize()
        {
            SetOutdoorWarPortalPhase(OutdoorWarType::None);

            if (LoadControllerState())
                return;

            ScheduleNextStart(GetNow());
        }

        void Update(uint32 diff)
        {
            m_updateTimer += diff;

            if (m_updateTimer < OUTDOOR_WAR_UPDATE_MS)
                return;

            uint32 tickSeconds = std::max<uint32>(1, m_updateTimer / 1000);
            m_updateTimer = 0;

            uint64 now = GetNow();

            if (m_activeType == OutdoorWarType::None)
            {
                if (m_nextStartAt == 0)
                {
                    ScheduleNextStart(now);
                    return;
                }

                if (now >= m_nextStartAt)
                    StartScheduledWar(now);

                return;
            }

            UpdateActiveWar(now, tickSeconds);
        }

        void OnPlayerLogin(Player* player)
        {
            SendActiveWarLoginNotice(player);
            TrackPlayerIfInsideWarZone(player, 0);
            RefreshWarBalancePlayer(player);
			EnforceNagrandHalaaNoFlying(player);
        }

        void OnPlayerLogout(Player* player)
        {
            RemoveWarBalancePlayer(player);
        }

        void OnPlayerZoneUpdate(Player* player, uint32 newZone, uint32 newArea)
		{
			if (!player)
				return;

			if (m_activeType == OutdoorWarType::Nagrand)
				EnforceNagrandHalaaNoFlying(player);

			if (m_activeType == OutdoorWarType::Hellfire && (IsHellfireRewardAreaId(newZone) || IsHellfireRewardAreaId(newArea)))
				TrackPlayerIfInsideWarZone(player, 0);

			RefreshWarBalancePlayer(player);
		}

        void OnHellfireFlagOwnerChanged(uint8 pointIndex, TeamId ownerTeam)
        {
            if (m_activeType != OutdoorWarType::Hellfire)
                return;

            if (pointIndex >= m_hellfireOwners.size())
                return;

            int32 teamId = int32(ownerTeam);

            if (!IsValidTeam(teamId))
                teamId = TEAM_ID_NEUTRAL;

            m_hellfireOwners[pointIndex] = teamId;

            if (!AreAllHellfireFlagsOwnedBySameTeam())
            {
                m_hellfireHoldTeam = TEAM_ID_NEUTRAL;
                m_hellfireHoldElapsedSeconds = 0;
                SaveControllerState();
                return;
            }

            int32 owner = m_hellfireOwners[0];

            if (m_hellfireHoldTeam != owner)
            {
                m_hellfireHoldTeam = owner;
                m_hellfireHoldElapsedSeconds = 0;

                Broadcast(Trinity::StringFormat(
                    "[야외 PvP] {} 진영이 지옥불 반도 깃발 3곳을 모두 점령했습니다. 10분 유지 시 승리합니다.",
                    GetTeamName(owner)
                ));
            }

            SaveControllerState();
        }

        void OnSilithusTurnIn(Player* player, uint32 count)
        {
            if (!player)
                return;

            if (m_activeType != OutdoorWarType::Silithus)
                return;

            if (count == 0)
                return;

            RegisterParticipant(player, 0, GetNow());

            int32 teamId = int32(player->GetTeamId());

            if (teamId == int32(TEAM_ALLIANCE))
                m_scoreAlliance += count;
            else if (teamId == int32(TEAM_HORDE))
                m_scoreHorde += count;
            else
                return;

            if (m_scoreAlliance >= SILITHUS_WIN_SCORE)
            {
                EndWar(int32(TEAM_ALLIANCE), "얼라이언스가 실리더스 납품 목표를 달성했습니다.");
                return;
            }

            if (m_scoreHorde >= SILITHUS_WIN_SCORE)
            {
                EndWar(int32(TEAM_HORDE), "호드가 실리더스 납품 목표를 달성했습니다.");
                return;
            }

            SaveControllerState();
        }

        void OnNagrandStateChanged(uint32 ownerTeam, uint32 guardsAlive)
        {
            if (m_activeType != OutdoorWarType::Nagrand)
                return;

            int32 teamId = TEAM_ID_NEUTRAL;

            if (ownerTeam == ALLIANCE)
                teamId = int32(TEAM_ALLIANCE);
            else if (ownerTeam == HORDE)
                teamId = int32(TEAM_HORDE);

            if (!IsValidTeam(teamId) || guardsAlive == 0)
            {
                bool changed = false;

                if (m_nagrandHoldTeam != TEAM_ID_NEUTRAL)
                {
                    m_nagrandHoldTeam = TEAM_ID_NEUTRAL;
                    changed = true;
                }

                if (m_nagrandHoldElapsedSeconds != 0)
                {
                    m_nagrandHoldElapsedSeconds = 0;
                    changed = true;
                }

                if (m_nagrandGuardsAlive != guardsAlive)
                {
                    m_nagrandGuardsAlive = guardsAlive;
                    changed = true;
                }

                m_nagrandRewardTargetsLocked = false;
                m_nagrandRewardTargetTeam = TEAM_ID_NEUTRAL;
                m_participants.clear();

                if (changed)
                    SaveControllerState();

                return;
            }

            bool changed = false;

            if (m_nagrandGuardsAlive != guardsAlive)
            {
                m_nagrandGuardsAlive = guardsAlive;
                changed = true;
            }

            if (m_nagrandHoldTeam != teamId)
            {
                m_nagrandHoldTeam = teamId;
                m_nagrandHoldElapsedSeconds = 0;
                m_nagrandRewardTargetsLocked = false;
                m_nagrandRewardTargetTeam = TEAM_ID_NEUTRAL;
                m_participants.clear();
                changed = true;

                Broadcast(Trinity::StringFormat(
                    "[야외 PvP] {} 진영이 할라아를 점령했습니다. 경비병을 지키며 5분 유지하면 승리합니다.",
                    GetTeamName(teamId)
                ));
            }

            if (changed)
                SaveControllerState();
        }

		void OnZangarmarshVictory(uint32 winnerTeam)
		{
			if (m_activeType != OutdoorWarType::Zangarmarsh)
				return;

			int32 teamId = TEAM_ID_NEUTRAL;

			if (winnerTeam == ALLIANCE)
				teamId = int32(TEAM_ALLIANCE);
			else if (winnerTeam == HORDE)
				teamId = int32(TEAM_HORDE);

			if (!IsValidTeam(teamId))
				return;

			EndWar(teamId, Trinity::StringFormat(
				"{}가 쌍둥이 첨탑 무덤을 5분 동안 방어했습니다.",
				GetTeamName(teamId)
			));
}

        bool ConsumeHellfireNeutralResetRequest()
        {
            if (!m_hellfireNeutralResetRequested)
                return false;

            m_hellfireNeutralResetRequested = false;
            return true;
        }

        bool ConsumeSilithusScoreResetRequest()
        {
            if (!m_silithusScoreResetRequested)
                return false;

            m_silithusScoreResetRequested = false;
            return true;
        }

        bool ConsumeNagrandNeutralResetRequest()
        {
            if (!m_nagrandNeutralResetRequested)
                return false;

            m_nagrandNeutralResetRequested = false;
            return true;
        }

        bool IsHellfireWarActive() const
        {
            return m_activeType == OutdoorWarType::Hellfire;
        }

        bool IsSilithusWarActive() const
        {
            return m_activeType == OutdoorWarType::Silithus;
        }

        bool IsNagrandWarActive() const
        {
            return m_activeType == OutdoorWarType::Nagrand;
        }

		bool IsZangarmarshWarActive() const
		{
			return m_activeType == OutdoorWarType::Zangarmarsh;
		}

        bool IsPortalEntryActive(uint32 entry) const
        {
            if (m_activeType == OutdoorWarType::None)
                return false;

            for (OutdoorWarPortalDefinition const& portal : OutdoorWarPortalDefinitions)
            {
                if (portal.Entry != entry)
                    continue;

                if (portal.Type != m_activeType)
                    continue;

                OutdoorWarDefinition const* definition = GetWarDefinition(portal.Type);
                return definition && definition->Enabled;
            }

            return false;
        }

		void EnforceNagrandHalaaNoFlying(Player* player)
		{
			if (!player)
				return;

			if (m_activeType != OutdoorWarType::Nagrand)
				return;

			if (!player->IsInWorld())
				return;

			if (!IsPlayerInNagrandHalaaArea(player))
				return;

			if (player->IsInFlight())
				return;

			player->RemoveAurasByType(SPELL_AURA_FLY);
			player->RemoveAurasByType(SPELL_AURA_MOD_INCREASE_FLIGHT_SPEED);
			player->RemoveAurasByType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED);
		}

    private:
        uint64 GetNow() const
        {
            return uint64(std::time(nullptr));
        }

        void UpdateActiveWar(uint64 now, uint32 tickSeconds)
        {
            if (m_elapsedSeconds < OUTDOOR_WAR_DURATION_SECONDS)
                m_elapsedSeconds = std::min<uint32>(OUTDOOR_WAR_DURATION_SECONDS, m_elapsedSeconds + tickSeconds);

            if (m_activeType == OutdoorWarType::Hellfire)
            {
                TickHellfireParticipants(now, tickSeconds);
                UpdateHellfireHoldTimer(tickSeconds);
                CheckHellfireVictory();
            }
            else if (m_activeType == OutdoorWarType::Nagrand)
            {
				EnforceNagrandHalaaNoFlyingForAllPlayers();
                UpdateNagrandHoldTimer(tickSeconds);
                CheckNagrandVictory();
            }

            uint32 remainingSeconds = OUTDOOR_WAR_DURATION_SECONDS > m_elapsedSeconds
                ? OUTDOOR_WAR_DURATION_SECONDS - m_elapsedSeconds
                : 0;

            m_endsAt = now + remainingSeconds;

            SaveControllerState();

            if (m_elapsedSeconds >= OUTDOOR_WAR_DURATION_SECONDS)
                EndWar(TEAM_ID_NEUTRAL, "제한 시간이 경과하여 전쟁이 종료되었습니다.");
        }

        bool LoadControllerState()
        {
            std::string sql = Trinity::StringFormat(
                "SELECT state_value "
                "FROM appdata.outdoorpvp_state "
                "WHERE pvp_type = '{}' AND state_key = '{}' "
                "LIMIT 1",
                GetOutdoorWarControllerPvpType(),
                OUTDOOR_WAR_CONTROLLER_STATE_KEY
            );

            QueryResult result = LoginDatabase.Query(sql.c_str());

            if (!result)
                return false;

            Field* fields = result->Fetch();
            std::string stateJson = fields[0].GetString();

            if (stateJson.empty())
                return false;

            return RestoreControllerState(stateJson, GetNow());
        }

        bool RestoreControllerState(std::string const& stateJson, uint64 now)
        {
            bool isActive = false;
            JsonReadBool(stateJson, "is_active", isActive);

            if (isActive)
                return RestoreActiveControllerState(stateJson, now);

            return RestoreScheduledControllerState(stateJson, now);
        }

        bool RestoreActiveControllerState(std::string const& stateJson, uint64 now)
        {
            int32 activeTypeValue = 0;

            if (!JsonReadInt32(stateJson, "active_type", activeTypeValue))
                return false;

            OutdoorWarType restoredType = OutdoorWarType(uint8(activeTypeValue));
            OutdoorWarDefinition const* definition = GetWarDefinition(restoredType);

            if (!definition || !definition->Enabled)
                return false;

            ResetRuntimeWarData();

            uint32 elapsedSeconds = 0;
            JsonReadUInt32(stateJson, "elapsed_seconds", elapsedSeconds);

            if (elapsedSeconds >= OUTDOOR_WAR_DURATION_SECONDS)
            {
                FinishWarOnly();
                ScheduleNextStart(now, true);
                return true;
            }

            m_activeType = restoredType;
            m_nextType = OutdoorWarType::None;
            m_nextStartAt = 0;

            if (m_activeType == OutdoorWarType::Nagrand)
            {
                JsonReadInt32(stateJson, "nagrand_hold_team", m_nagrandHoldTeam);
                JsonReadUInt32(stateJson, "nagrand_hold_elapsed_seconds", m_nagrandHoldElapsedSeconds);
                JsonReadUInt32(stateJson, "nagrand_guards_alive", m_nagrandGuardsAlive);
            }

            m_elapsedSeconds = elapsedSeconds;
            m_startedAt = now > m_elapsedSeconds ? now - m_elapsedSeconds : now;
            m_endsAt = now + (OUTDOOR_WAR_DURATION_SECONDS - m_elapsedSeconds);

            SetOutdoorWarPortalPhase(m_activeType);

            TC_LOG_INFO(
                "custom",
                "[야외 PvP] 진행 중 이벤트 복원: type={}, elapsed={}, remaining={}",
                uint32(m_activeType),
                m_elapsedSeconds,
                OUTDOOR_WAR_DURATION_SECONDS - m_elapsedSeconds
            );

            SaveControllerState();
            return true;
        }

        bool RestoreScheduledControllerState(std::string const& stateJson, uint64 now)
        {
            int32 nextTypeValue = 0;
            uint64 nextStartAt = 0;

            if (!JsonReadInt32(stateJson, "next_type", nextTypeValue))
                return false;

            if (!JsonReadUInt64(stateJson, "next_start_at", nextStartAt))
                return false;

            OutdoorWarType restoredNextType = OutdoorWarType(uint8(nextTypeValue));
            OutdoorWarDefinition const* definition = GetWarDefinition(restoredNextType);

            if (!definition || !definition->Enabled)
                return false;

            FinishWarOnly();

            m_nextType = restoredNextType;
            m_nextStartAt = nextStartAt;

            SetOutdoorWarPortalPhase(OutdoorWarType::None);

            if (m_nextStartAt <= now)
            {
                if (m_nextStartAt + OUTDOOR_WAR_DURATION_SECONDS > now)
                {
                    TC_LOG_INFO(
                        "custom",
                        "[야외 PvP] 예약 시간이 서버 중지 중 경과하여 즉시 시작: type={}, nextStartAt={}, now={}",
                        uint32(m_nextType),
                        m_nextStartAt,
                        now
                    );

                    StartScheduledWar(now);
                    return true;
                }

                TC_LOG_INFO(
                    "custom",
                    "[야외 PvP] 저장된 예약 시간이 완전히 지나 새 예약 생성: type={}, nextStartAt={}, now={}",
                    uint32(m_nextType),
                    m_nextStartAt,
                    now
                );

                ScheduleNextStart(now);
                return true;
            }

            TC_LOG_INFO(
                "custom",
                "[야외 PvP] 다음 이벤트 예약 복원: type={}, nextStartAt={}",
                uint32(m_nextType),
                m_nextStartAt
            );

            SaveControllerState();
            return true;
        }

        std::string BuildControllerStateJson() const
        {
            std::ostringstream json;

            uint32 isActive = m_activeType != OutdoorWarType::None ? 1 : 0;
            uint32 remainingSeconds = OUTDOOR_WAR_DURATION_SECONDS > m_elapsedSeconds
                ? OUTDOOR_WAR_DURATION_SECONDS - m_elapsedSeconds
                : 0;

            // 컨트롤러 state에는 컨트롤 정보만 저장한다.
            // 전장별 실제 점수/거점/승리 상태는 각 전장 C++의 state를 사용한다.
            json << "{";
            json << "\"version\":1";
            json << ",\"state_saved_at\":" << GetNow();
            json << ",\"is_active\":" << isActive;
            json << ",\"active_type\":" << uint32(m_activeType);
            json << ",\"next_type\":" << uint32(m_nextType);
            json << ",\"next_start_at\":" << m_nextStartAt;
            json << ",\"started_at\":" << m_startedAt;
            json << ",\"elapsed_seconds\":" << m_elapsedSeconds;
            json << ",\"remaining_seconds\":" << remainingSeconds;
            json << ",\"nagrand_hold_team\":" << m_nagrandHoldTeam;
            json << ",\"nagrand_hold_elapsed_seconds\":" << m_nagrandHoldElapsedSeconds;
            json << ",\"nagrand_guards_alive\":" << m_nagrandGuardsAlive;
            json << "}";

            return json.str();
        }

        void SaveControllerState() const
        {
            std::string stateJson = BuildControllerStateJson();

            std::string sql = Trinity::StringFormat(
                "INSERT INTO appdata.outdoorpvp_state "
                "(pvp_type, state_key, state_value) "
                "VALUES ('{}', '{}', '{}') "
                "ON DUPLICATE KEY UPDATE state_value = VALUES(state_value)",
                GetOutdoorWarControllerPvpType(),
                OUTDOOR_WAR_CONTROLLER_STATE_KEY,
                stateJson
            );

            LoginDatabase.Execute(sql.c_str());
        }

        OutdoorWarType SelectRandomEnabledWarType()
        {
            std::vector<OutdoorWarType> candidates;

            for (OutdoorWarDefinition const& definition : OutdoorWarDefinitions)
            {
                if (definition.Enabled)
                    candidates.push_back(definition.Type);
            }

            if (candidates.empty())
                return OutdoorWarType::None;

            return candidates[urand(0, uint32(candidates.size() - 1))];
        }

        void StartScheduledWar(uint64 now)
        {
            OutdoorWarType selectedType = m_nextType;

            if (selectedType == OutdoorWarType::None)
                selectedType = SelectRandomEnabledWarType();

            if (selectedType == OutdoorWarType::None)
            {
                ScheduleNextStart(now);
                return;
            }

            StartWar(selectedType, now);
        }

        void StartWar(OutdoorWarType type, uint64 now)
        {
            OutdoorWarDefinition const* definition = GetWarDefinition(type);

            if (!definition || !definition->Enabled)
                return;

            ResetRuntimeWarData();

            m_activeType = type;
            m_nextType = OutdoorWarType::None;
            m_nextStartAt = 0;
            m_startedAt = now;
            m_endsAt = now + OUTDOOR_WAR_DURATION_SECONDS;
            m_elapsedSeconds = 0;

            SetOutdoorWarPortalPhase(type);

            if (type == OutdoorWarType::Hellfire)
                m_hellfireNeutralResetRequested = true;
            else if (type == OutdoorWarType::Silithus)
                m_silithusScoreResetRequested = true;
            else if (type == OutdoorWarType::Nagrand)
			{
                m_nagrandNeutralResetRequested = true;
				TeleportNagrandHalaaPlayersToFactionStart();
			}

            Broadcast(Trinity::StringFormat(
                "[야외 PvP] {} 전장이 시작됩니다. 제한 시간은 2시간입니다.",
                definition->Name
            ));

            SendFcmCategory(
				3,
				"outdoor_pvp",
				"[야외 PvP] 야외전장 시작",
				Trinity::StringFormat("{} 전장이 시작되었습니다. 제한 시간은 2시간입니다.", definition->Name),
				0
			);

            if (type == OutdoorWarType::Hellfire)
            {
                Broadcast("[야외 PvP] 지옥불 반도 승리 조건: 깃발 3곳을 모두 점령하고 10분 동안 유지해야 합니다.");
            }
            else if (type == OutdoorWarType::Silithus)
            {
                Broadcast(Trinity::StringFormat(
                    "[야외 PvP] 실리더스 승리 조건: 실리시스트를 먼저 {}점까지 납품해야 합니다.",
                    SILITHUS_WIN_SCORE
                ));
            }
            else if (type == OutdoorWarType::Nagrand)
            {
                Broadcast("[야외 PvP] 나그란드 할라아가 중립 상태로 시작됩니다. 먼저 도착한 진영이 할라아를 선점할 수 있습니다.");
                Broadcast("[야외 PvP] 나그란드 승리 조건: 점령 진영이 경비병 1명 이상 생존 상태로 5분 유지하면 승리합니다.");
            }

            BuildInitialWarBalanceParticipants();

            SaveControllerState();
        }

        void EndWar(int32 winnerTeam, std::string const& reason)
        {
            OutdoorWarDefinition const* definition = GetWarDefinition(m_activeType);

            if (!definition)
            {
                FinishWarOnly();
                ScheduleNextStart(GetNow(), true);
                return;
            }

            Broadcast(Trinity::StringFormat(
                "[야외 PvP] {} 전장이 종료됩니다.",
                definition->Name
            ));

            Broadcast(Trinity::StringFormat("[야외 PvP] {}", reason));

            if (IsValidTeam(winnerTeam))
            {
                uint32 rewardedCount = RewardParticipants(winnerTeam);

                Broadcast(Trinity::StringFormat(
                    "[야외 PvP] {} 전쟁 승리 진영: {}. 보상 지급 인원: {}명",
                    definition->Name,
                    GetTeamName(winnerTeam),
                    rewardedCount
                ));
            }
            else
            {
                Broadcast(Trinity::StringFormat(
                    "[야외 PvP] {} 전쟁은 승리 조건 미달성으로 종료되었습니다. 보상은 지급되지 않습니다.",
                    definition->Name
                ));
            }

            FinishWarOnly();
            ScheduleNextStart(GetNow(), true);
        }

        void ResetRuntimeWarData()
        {
            m_scoreAlliance = 0;
            m_scoreHorde = 0;

            m_hellfireOwners.fill(TEAM_ID_NEUTRAL);
            m_hellfireHoldTeam = TEAM_ID_NEUTRAL;
            m_hellfireHoldElapsedSeconds = 0;

            m_hellfireNeutralResetRequested = false;
            m_silithusScoreResetRequested = false;
            m_nagrandNeutralResetRequested = false;

            m_nagrandHoldTeam = TEAM_ID_NEUTRAL;
            m_nagrandHoldElapsedSeconds = 0;
            m_nagrandGuardsAlive = 0;
            m_nagrandRewardTargetsLocked = false;
            m_nagrandRewardTargetTeam = TEAM_ID_NEUTRAL;

            m_balanceParticipants.clear();
            m_balanceBuffTeam = TEAM_ID_NEUTRAL;
            m_balanceBuffStacks = 0;

            m_participants.clear();
        }

        void FinishWarOnly()
        {
            ClearWarBalanceBuffForAllPlayers();

            SetOutdoorWarPortalPhase(OutdoorWarType::None);

            m_activeType = OutdoorWarType::None;
            m_nextType = OutdoorWarType::None;
            m_nextStartAt = 0;
            m_startedAt = 0;
            m_endsAt = 0;
            m_elapsedSeconds = 0;

            ResetRuntimeWarData();
        }

        void ScheduleNextStart(uint64 now, bool forceNextDay = false)
        {
            FinishWarOnly();

            m_nextType = SelectRandomEnabledWarType();

            if (m_nextType == OutdoorWarType::None)
            {
                m_nextStartAt = 0;

                TC_LOG_WARN(
                    "custom",
                    "[야외 PvP] 활성화된 야외전장이 없어 다음 전쟁을 예약하지 못했습니다."
                );

                SaveControllerState();
                return;
            }

            if (OUTDOOR_WAR_TEST_MODE)
            {
                m_nextStartAt = now + OUTDOOR_WAR_TEST_START_DELAY_SECONDS;

                TC_LOG_INFO(
                    "custom",
                    "[야외 PvP][TEST] {}초 후 야외전장 시작 예정: type={}, nextStartAt={}",
                    OUTDOOR_WAR_TEST_START_DELAY_SECONDS,
                    uint32(m_nextType),
                    m_nextStartAt
                );

                SaveControllerState();
                return;
            }

            time_t nowTime = time_t(now);
            tm when = *localtime(&nowTime);

            if (forceNextDay)
                when.tm_mday += int(OUTDOOR_WAR_PERIOD_DAYS);

            when.tm_hour = int(urand(OUTDOOR_WAR_START_HOUR_MIN, OUTDOOR_WAR_START_HOUR_MAX));
            when.tm_min = int(urand(0, 59));
            when.tm_sec = 0;

            time_t nextStart = mktime(&when);

            if (nextStart <= nowTime)
            {
                when = *localtime(&nowTime);
                when.tm_mday += int(OUTDOOR_WAR_PERIOD_DAYS);
                when.tm_hour = int(urand(OUTDOOR_WAR_START_HOUR_MIN, OUTDOOR_WAR_START_HOUR_MAX));
                when.tm_min = int(urand(0, 59));
                when.tm_sec = 0;

                nextStart = mktime(&when);
            }

            m_nextStartAt = uint64(nextStart);

            TC_LOG_INFO(
                "custom",
                "[야외 PvP] 다음 야외전장 예약 저장: type={}, nextStartAt={}",
                uint32(m_nextType),
                m_nextStartAt
            );

            SaveControllerState();
        }

        void SendActiveWarLoginNotice(Player* player) const
        {
            if (!player)
                return;

            if (m_activeType == OutdoorWarType::None)
                return;

            WorldSession* session = player->GetSession();
            if (!session)
                return;

            OutdoorWarDefinition const* definition = GetWarDefinition(m_activeType);
            if (!definition || !definition->Enabled)
                return;

            uint32 remainingSeconds = OUTDOOR_WAR_DURATION_SECONDS > m_elapsedSeconds
                ? OUTDOOR_WAR_DURATION_SECONDS - m_elapsedSeconds
                : 0;

            uint32 remainingMinutes = std::max<uint32>(1, (remainingSeconds + 59) / 60);

            std::string message = Trinity::StringFormat(
                "|cffFF8000[야외 PvP]|r 현재 |cff00ff00{}|r 전장이 진행 중입니다. 남은 시간: 약 {}분. 달라란 전장 포털을 이용할 수 있습니다.",
                definition->Name,
                remainingMinutes
            );

            ChatHandler(session).SendSysMessage(message.c_str());
        }

        bool IsPlayerInCurrentWarBalanceArea(Player* player) const
        {
            if (!player)
                return false;

            if (m_activeType == OutdoorWarType::None)
                return false;

            if (!player->IsInWorld())
                return false;

            if (player->IsGameMaster())
                return false;

            if (!IsValidTeam(int32(player->GetTeamId())))
                return false;

            OutdoorWarDefinition const* definition = GetWarDefinition(m_activeType);

            if (!definition || !definition->Enabled)
                return false;

            if (player->GetMapId() != definition->MapId)
                return false;

            if (m_activeType == OutdoorWarType::Hellfire)
                return IsPlayerInHellfireRewardArea(player);

            if (m_activeType == OutdoorWarType::Nagrand)
                return player->GetAreaId() == NAGRAND_HALAA_REWARD_AREA_ID;

            if (player->GetZoneId() == definition->ZoneId)
                return true;

            if (player->GetAreaId() == definition->ZoneId)
                return true;

            return false;
        }

        void BuildInitialWarBalanceParticipants()
        {
            m_balanceParticipants.clear();
            m_balanceBuffTeam = TEAM_ID_NEUTRAL;
            m_balanceBuffStacks = 0;

            HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();

            for (auto const& pair : players)
                RefreshWarBalancePlayer(pair.second, false);

            RecalculateWarBalanceBuff();
        }

        void RefreshWarBalancePlayer(Player* player, bool recalculate = true)
        {
            if (!player)
                return;

            uint64 guid = player->GetGUID().GetCounter();
            bool changed = false;

            if (IsPlayerInCurrentWarBalanceArea(player))
            {
                int32 teamId = int32(player->GetTeamId());
                auto itr = m_balanceParticipants.find(guid);

                if (itr == m_balanceParticipants.end() || itr->second != teamId)
                {
                    m_balanceParticipants[guid] = teamId;
                    changed = true;
                }
            }
            else
            {
                auto itr = m_balanceParticipants.find(guid);

                if (itr != m_balanceParticipants.end())
                {
                    m_balanceParticipants.erase(itr);
                    changed = true;
                }

                RemoveWarBalanceBuff(player);
            }

            if (recalculate && changed)
                RecalculateWarBalanceBuff();
            else if (recalculate)
                ApplyWarBalanceBuffToPlayer(player);
        }

        void RemoveWarBalancePlayer(Player* player)
        {
            if (!player)
                return;

            uint64 guid = player->GetGUID().GetCounter();
            bool changed = m_balanceParticipants.erase(guid) > 0;

            RemoveWarBalanceBuff(player);

            if (changed)
                RecalculateWarBalanceBuff();
        }

        uint32 GetWarBalanceTenacityStack(uint32 highCount, uint32 lowCount) const
        {
            if (lowCount == 0)
                return 0;

            if (highCount <= lowCount)
                return 0;

            uint32 ratio = (highCount * 100) / lowCount;

            if (ratio < 150)
                return 0;

            if (ratio < 200)
                return 1;

            if (ratio < 250)
                return 2;

            if (ratio < 300)
                return 3;

            if (ratio < 400)
                return 4;

            return OUTDOOR_WAR_TENACITY_MAX_STACK;
        }

        void RecalculateWarBalanceBuff()
        {
            if (m_activeType == OutdoorWarType::None)
            {
                m_balanceBuffTeam = TEAM_ID_NEUTRAL;
                m_balanceBuffStacks = 0;
                ClearWarBalanceBuffForAllPlayers();
                return;
            }

            uint32 allianceCount = 0;
            uint32 hordeCount = 0;

            for (auto const& pair : m_balanceParticipants)
            {
                if (pair.second == int32(TEAM_ALLIANCE))
                    ++allianceCount;
                else if (pair.second == int32(TEAM_HORDE))
                    ++hordeCount;
            }

            int32 newBuffTeam = TEAM_ID_NEUTRAL;
            uint32 newBuffStacks = 0;

            if (allianceCount > hordeCount && hordeCount > 0)
            {
                newBuffTeam = int32(TEAM_HORDE);
                newBuffStacks = GetWarBalanceTenacityStack(allianceCount, hordeCount);
            }
            else if (hordeCount > allianceCount && allianceCount > 0)
            {
                newBuffTeam = int32(TEAM_ALLIANCE);
                newBuffStacks = GetWarBalanceTenacityStack(hordeCount, allianceCount);
            }

            if (newBuffStacks == 0)
                newBuffTeam = TEAM_ID_NEUTRAL;

            m_balanceBuffTeam = newBuffTeam;
            m_balanceBuffStacks = newBuffStacks;

            ApplyWarBalanceBuffToParticipants();
        }

        void ApplyWarBalanceBuffToParticipants()
        {
            HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();

            for (auto const& pair : players)
                ApplyWarBalanceBuffToPlayer(pair.second);
        }

        void ApplyWarBalanceBuffToPlayer(Player* player)
        {
            if (!player)
                return;

            uint64 guid = player->GetGUID().GetCounter();
            auto itr = m_balanceParticipants.find(guid);

            if (itr == m_balanceParticipants.end())
            {
                RemoveWarBalanceBuff(player);
                return;
            }

            if (m_balanceBuffTeam == TEAM_ID_NEUTRAL || m_balanceBuffStacks == 0)
            {
                RemoveWarBalanceBuff(player);
                return;
            }

            if (itr->second != m_balanceBuffTeam)
            {
                RemoveWarBalanceBuff(player);
                return;
            }

            ApplyWarBalanceBuff(player, m_balanceBuffStacks);
        }

        void ApplyWarBalanceBuff(Player* player, uint32 stacks)
        {
            if (!player || !player->IsInWorld())
                return;

            RemoveWarBalanceBuff(player);

            if (stacks == 0)
                return;

            if (stacks > OUTDOOR_WAR_TENACITY_MAX_STACK)
                stacks = OUTDOOR_WAR_TENACITY_MAX_STACK;

            player->CastSpell(player, OUTDOOR_WAR_TENACITY_SPELL, true);

            if (Aura* aura = player->GetAura(OUTDOOR_WAR_TENACITY_SPELL))
                aura->SetStackAmount(uint8(stacks));
        }

        void RemoveWarBalanceBuff(Player* player)
        {
            if (!player)
                return;

            player->RemoveAurasDueToSpell(OUTDOOR_WAR_TENACITY_SPELL);
        }

        void ClearWarBalanceBuffForAllPlayers()
        {
            HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();

            for (auto const& pair : players)
                RemoveWarBalanceBuff(pair.second);
        }

        void TickHellfireParticipants(uint64 now, uint32 tickSeconds)
        {
            if (m_activeType != OutdoorWarType::Hellfire)
                return;

            HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();

            for (auto const& pair : players)
            {
                Player* player = pair.second;

                if (!player)
                    continue;

                TrackPlayerIfInsideWarZone(player, tickSeconds, now);
            }
        }

        void TrackPlayerIfInsideWarZone(Player* player, uint32 addSeconds)
        {
            TrackPlayerIfInsideWarZone(player, addSeconds, GetNow());
        }

        void TrackPlayerIfInsideWarZone(Player* player, uint32 addSeconds, uint64 now)
        {
            if (!player)
                return;

            if (m_activeType != OutdoorWarType::Hellfire)
                return;

            if (!IsPlayerInHellfireRewardArea(player))
                return;

            RegisterParticipant(player, addSeconds, now);
        }

        void RegisterParticipant(Player* player, uint32 addSeconds, uint64 now)
        {
            if (!player)
                return;

            if (m_activeType == OutdoorWarType::None)
                return;

            if (player->IsGameMaster())
                return;

            uint64 guid = player->GetGUID().GetCounter();
            ParticipantInfo& info = m_participants[guid];

            if (info.Guid == 0)
            {
                info.Guid = guid;
                info.AccountId = player->GetSession() ? player->GetSession()->GetAccountId() : 0;
                info.TeamId = int32(player->GetTeamId());
                info.ActiveSeconds = 0;
                info.LastSeenAt = now;
                info.Rewarded = false;
            }

            if (addSeconds > 0)
                info.ActiveSeconds += addSeconds;

            info.TeamId = int32(player->GetTeamId());
            info.LastSeenAt = now;
        }

        bool IsPlayerInHellfireRewardArea(Player* player) const
        {
            if (!player)
                return false;

            if (player->GetMapId() != 530)
                return false;

            if (IsHellfireRewardAreaId(player->GetZoneId()))
                return true;

            if (IsHellfireRewardAreaId(player->GetAreaId()))
                return true;

            return false;
        }

        bool AreAllHellfireFlagsOwnedBySameTeam() const
        {
            int32 owner = m_hellfireOwners[0];

            if (!IsValidTeam(owner))
                return false;

            return m_hellfireOwners[1] == owner && m_hellfireOwners[2] == owner;
        }

        void UpdateHellfireHoldTimer(uint32 tickSeconds)
        {
            if (m_activeType != OutdoorWarType::Hellfire)
                return;

            if (!IsValidTeam(m_hellfireHoldTeam))
                return;

            if (m_hellfireHoldElapsedSeconds < HELLFIRE_HOLD_SECONDS)
                m_hellfireHoldElapsedSeconds = std::min<uint32>(HELLFIRE_HOLD_SECONDS, m_hellfireHoldElapsedSeconds + tickSeconds);
        }

        void CheckHellfireVictory()
        {
            if (m_activeType != OutdoorWarType::Hellfire)
                return;

            if (!IsValidTeam(m_hellfireHoldTeam))
                return;

            if (m_hellfireHoldElapsedSeconds < HELLFIRE_HOLD_SECONDS)
                return;

            EndWar(m_hellfireHoldTeam, Trinity::StringFormat(
                "{}가 지옥불 반도 깃발 3곳을 10분 동안 유지했습니다.",
                GetTeamName(m_hellfireHoldTeam)
            ));
        }

		uint32 CountNagrandHalaaPlayersByTeam(int32 teamId) const
		{
			if (!IsValidTeam(teamId))
				return 0;

			uint32 count = 0;

			HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();

			for (auto const& pair : players)
			{
				Player* player = pair.second;

				if (!player)
					continue;

				if (!player->IsInWorld())
					continue;

				if (player->IsGameMaster())
					continue;

				if (int32(player->GetTeamId()) != teamId)
					continue;

				if (!IsPlayerInNagrandHalaaArea(player))
					continue;

				++count;
			}

			return count;
		}

        void UpdateNagrandHoldTimer(uint32 tickSeconds)
        {
            if (m_activeType != OutdoorWarType::Nagrand)
                return;

            if (!IsValidTeam(m_nagrandHoldTeam))
                return;

            if (m_nagrandGuardsAlive == 0)
                return;

			// 점령 진영 유저가 할라아 3628 지역에 없으면 5분 유지 카운트만 초기화한다.
			// 캡쳐바는 OutdoorPvPNA.cpp에서 그대로 정상 동작하게 둔다.
			if (CountNagrandHalaaPlayersByTeam(m_nagrandHoldTeam) == 0)
			{
				if (m_nagrandHoldElapsedSeconds != 0)
				{
					m_nagrandHoldElapsedSeconds = 0;
					SaveControllerState();
				}

				return;
			}

            if (!m_nagrandRewardTargetsLocked || m_nagrandRewardTargetTeam != m_nagrandHoldTeam)
                RegisterNagrandHalaaParticipants(m_nagrandHoldTeam);

            if (m_nagrandHoldElapsedSeconds < NAGRAND_HOLD_SECONDS)
                m_nagrandHoldElapsedSeconds = std::min<uint32>(NAGRAND_HOLD_SECONDS, m_nagrandHoldElapsedSeconds + tickSeconds);
        }

		void EnforceNagrandHalaaNoFlyingForAllPlayers()
		{
			if (m_activeType != OutdoorWarType::Nagrand)
				return;

			HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();

			for (auto const& pair : players)
			{
				Player* player = pair.second;

				if (!player)
					continue;

				EnforceNagrandHalaaNoFlying(player);
			}
		}

        void CheckNagrandVictory()
        {
            if (m_activeType != OutdoorWarType::Nagrand)
                return;

            if (!IsValidTeam(m_nagrandHoldTeam))
                return;

            if (m_nagrandGuardsAlive == 0)
                return;

            if (m_nagrandHoldElapsedSeconds < NAGRAND_HOLD_SECONDS)
                return;

            EndWar(m_nagrandHoldTeam, Trinity::StringFormat(
                "{}가 경비병을 지키며 할라아를 5분 동안 유지했습니다.",
                GetTeamName(m_nagrandHoldTeam)
            ));
        }

        uint32 RewardParticipants(int32 winnerTeam)
        {
            if (m_activeType == OutdoorWarType::Hellfire)
                return RewardHellfireParticipants(winnerTeam);

            if (m_activeType == OutdoorWarType::Silithus)
                return RewardSilithusParticipants(winnerTeam);

			if (m_activeType == OutdoorWarType::Nagrand)
				return RewardNagrandParticipants(winnerTeam);

            return 0;
        }

        uint32 RewardHellfireParticipants(int32 winnerTeam)
        {
            TickHellfireParticipants(GetNow(), 0);

            uint32 rewardedCount = 0;

            for (auto& pair : m_participants)
            {
                ParticipantInfo& info = pair.second;

                if (info.Rewarded)
                    continue;

                if (info.AccountId == 0)
                    continue;

                Player* player = FindOnlinePlayerByCounter(info.Guid);

                if (!player)
                    continue;

                if (player->IsGameMaster())
                    continue;

                if (!IsPlayerInHellfireRewardArea(player))
                    continue;

                if (int32(player->GetTeamId()) != winnerTeam)
                    continue;

                GiveReward(info);
                info.Rewarded = true;
                ++rewardedCount;
            }

            return rewardedCount;
        }

        uint32 RewardSilithusParticipants(int32 winnerTeam)
        {
            uint32 rewardedCount = 0;

            for (auto& pair : m_participants)
            {
                ParticipantInfo& info = pair.second;

                if (info.Rewarded)
                    continue;

                if (info.AccountId == 0)
                    continue;

                if (info.TeamId != winnerTeam)
                    continue;

                GiveReward(info);
                info.Rewarded = true;
                ++rewardedCount;
            }

            return rewardedCount;
        }

		bool IsPlayerInNagrandHalaaArea(Player* player) const
		{
			if (!player)
				return false;

			if (player->GetMapId() != 530)
				return false;

			if (player->GetAreaId() == NAGRAND_HALAA_REWARD_AREA_ID)
				return true;

			return false;
		}

		void RegisterNagrandHalaaParticipants(int32 teamId)
        {
            m_participants.clear();

            m_nagrandRewardTargetsLocked = true;
            m_nagrandRewardTargetTeam = teamId;

            if (!IsValidTeam(teamId))
                return;

            uint64 now = GetNow();

            HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();

            for (auto const& pair : players)
            {
                Player* player = pair.second;

                if (!player)
                    continue;

                if (!player->IsInWorld())
                    continue;

                if (player->IsGameMaster())
                    continue;

                if (int32(player->GetTeamId()) != teamId)
                    continue;

                if (!IsPlayerInNagrandHalaaArea(player))
                    continue;

                RegisterParticipant(player, 0, now);
            }
        }

		void TeleportNagrandHalaaPlayersToFactionStart()
		{
			HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();

			for (auto const& pair : players)
			{
				Player* player = pair.second;

				if (!player)
					continue;

				if (!player->IsInWorld())
					continue;

				if (player->GetMapId() != 530)
					continue;

				if (player->GetAreaId() != NAGRAND_HALAA_REWARD_AREA_ID)
					continue;

				if (player->GetTeamId() == TEAM_HORDE)
				{
					player->TeleportTo(
						530,
						-1208.005493f,
						7408.603027f,
						28.196314f,
						4.355800f
					);

					continue;
				}

				if (player->GetTeamId() == TEAM_ALLIANCE)
				{
					player->TeleportTo(
						530,
						-508.327393f,
						7308.223633f,
						1.653407f,
						3.519342f
					);

					continue;
				}
			}
		}

        uint32 RewardNagrandParticipants(int32 winnerTeam)
		{
			uint32 rewardedCount = 0;

			for (auto& pair : m_participants)
			{
				ParticipantInfo& info = pair.second;

				if (info.Rewarded)
					continue;

				if (info.AccountId == 0)
					continue;

				if (info.TeamId != winnerTeam)
					continue;

				GiveReward(info);
				info.Rewarded = true;
				++rewardedCount;
			}

			m_participants.clear();
			m_nagrandRewardTargetsLocked = false;
			m_nagrandRewardTargetTeam = TEAM_ID_NEUTRAL;

			return rewardedCount;
		}

        void GiveReward(ParticipantInfo const& info)
        {
            if (REWARD_REG_POINT > 0)
            {
                std::string sql = Trinity::StringFormat(
                    "UPDATE appdata.account_data "
                    "SET reg_point = reg_point + {} "
                    "WHERE id = {}",
                    REWARD_REG_POINT,
                    info.AccountId
                );

                LoginDatabase.Execute(sql.c_str());
            }

            if (REWARD_ITEM_ENTRY == 0 || REWARD_ITEM_COUNT == 0)
                return;

            Player* player = FindOnlinePlayerByCounter(info.Guid);

            if (!player)
                return;

            player->AddItem(REWARD_ITEM_ENTRY, REWARD_ITEM_COUNT);

			uint32 fragmentItem = REWARD_EXECUTION_FRAGMENT_ITEMS[urand(0, uint32(REWARD_EXECUTION_FRAGMENT_ITEMS.size() - 1))];
			player->AddItem(fragmentItem, 1);
        }

        Player* FindOnlinePlayerByCounter(uint64 guidCounter) const
        {
            HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();

            for (auto const& pair : players)
            {
                Player* player = pair.second;

                if (!player)
                    continue;

                if (player->GetGUID().GetCounter() == guidCounter)
                    return player;
            }

            return nullptr;
        }

        void SetOutdoorWarPortalPhase(OutdoorWarType activeType)
        {
            for (OutdoorWarPortalDefinition const& portal : OutdoorWarPortalDefinitions)
            {
                SetOutdoorWarPortalSpawnPhase(
                    portal.SpawnGuid,
                    portal.Entry,
                    OUTDOOR_WAR_PORTAL_PHASE_HIDE,
                    true
                );
            }

            if (activeType == OutdoorWarType::None)
                return;

            OutdoorWarDefinition const* activeDefinition = GetWarDefinition(activeType);
            if (!activeDefinition || !activeDefinition->Enabled)
                return;

            for (OutdoorWarPortalDefinition const& portal : OutdoorWarPortalDefinitions)
            {
                if (portal.Type != activeType)
                    continue;

                SetOutdoorWarPortalSpawnPhase(
                    portal.SpawnGuid,
                    portal.Entry,
                    OUTDOOR_WAR_PORTAL_PHASE_SHOW,
                    true
                );
            }
        }

        void SetOutdoorWarPortalSpawnPhase(uint32 spawnGuid, uint32 entry, uint32 phase, bool force = false)
        {
            uint32& currentPhase = m_portalCurrentPhases[spawnGuid];

            if (!force && currentPhase == phase)
                return;

            currentPhase = phase;

            GameObjectData const* data = sObjectMgr->GetGameObjectData(spawnGuid);
            if (!data)
                return;

            const_cast<GameObjectData*>(data)->phaseMask = phase;

            WorldDatabase.PExecute(
                "UPDATE gameobject "
                "SET phaseMask = {} "
                "WHERE guid = {} "
                "AND id = {}",
                phase,
                spawnGuid,
                entry
            );

            sMapMgr->DoForAllMapsWithMapId(
                data->mapId,
                [spawnGuid, entry, phase](Map* map) -> void
                {
                    if (!map)
                        return;

                    auto bounds = Trinity::Containers::MapEqualRange(
                        map->GetGameObjectBySpawnIdStore(),
                        spawnGuid
                    );

                    for (auto const& pair : bounds)
                    {
                        GameObject* go = pair.second;
                        if (!go)
                            continue;

                        if (go->GetSpawnId() != spawnGuid)
                            continue;

                        if (go->GetEntry() != entry)
                            continue;

                        go->SetPhaseMask(phase, true);
                        go->UpdateObjectVisibility();
                    }
                }
            );
        }

        void Broadcast(std::string const& message) const
        {
            sWorld->SendServerMessage(SERVER_MSG_STRING, message.c_str());
        }

    private:
        OutdoorWarType m_activeType = OutdoorWarType::None;
        OutdoorWarType m_nextType = OutdoorWarType::None;

        uint64 m_startedAt = 0;
        uint64 m_endsAt = 0;
        uint64 m_nextStartAt = 0;

        uint32 m_elapsedSeconds = 0;

        uint32 m_scoreAlliance = 0;
        uint32 m_scoreHorde = 0;

        std::array<int32, 3> m_hellfireOwners =
        {{
            TEAM_ID_NEUTRAL,
            TEAM_ID_NEUTRAL,
            TEAM_ID_NEUTRAL
        }};

        int32 m_hellfireHoldTeam = TEAM_ID_NEUTRAL;
        uint32 m_hellfireHoldElapsedSeconds = 0;

        bool m_hellfireNeutralResetRequested = false;
        bool m_silithusScoreResetRequested = false;
        bool m_nagrandNeutralResetRequested = false;

        int32 m_nagrandHoldTeam = TEAM_ID_NEUTRAL;
        uint32 m_nagrandHoldElapsedSeconds = 0;
        uint32 m_nagrandGuardsAlive = 0;
        bool m_nagrandRewardTargetsLocked = false;
        int32 m_nagrandRewardTargetTeam = TEAM_ID_NEUTRAL;

        int32 m_balanceBuffTeam = TEAM_ID_NEUTRAL;
        uint32 m_balanceBuffStacks = 0;
        std::unordered_map<uint64, int32> m_balanceParticipants;

        uint32 m_updateTimer = 0;

        std::unordered_map<uint64, ParticipantInfo> m_participants;
        std::unordered_map<uint32, uint32> m_portalCurrentPhases;
    };

    class custom_outdoor_pvp_war_worldscript : public WorldScript
    {
    public:
        custom_outdoor_pvp_war_worldscript() : WorldScript("custom_outdoor_pvp_war_worldscript") { }

        void OnStartup() override
        {
            OutdoorPvpWarMgr::Instance().Initialize();
        }

        void OnUpdate(uint32 diff) override
        {
            OutdoorPvpWarMgr::Instance().Update(diff);
        }
    };

    class custom_outdoor_pvp_war_playerscript : public PlayerScript
    {
    public:
        custom_outdoor_pvp_war_playerscript() : PlayerScript("custom_outdoor_pvp_war_playerscript") { }

        void OnLogin(Player* player, bool /*firstLogin*/) override
        {
            OutdoorPvpWarMgr::Instance().OnPlayerLogin(player);
        }

        void OnLogout(Player* player) override
        {
            OutdoorPvpWarMgr::Instance().OnPlayerLogout(player);
        }

        void OnUpdateZone(Player* player, uint32 newZone, uint32 newArea) override
        {
            OutdoorPvpWarMgr::Instance().OnPlayerZoneUpdate(player, newZone, newArea);
        }

		void OnSpellCast(Player* player, Spell* spell, bool skipCheck) override
		{
			(void)spell;
			(void)skipCheck;

			OutdoorPvpWarMgr::Instance().EnforceNagrandHalaaNoFlying(player);
		}
    };
}

void OutdoorPvpWar_OnHellfireFlagOwnerChanged(uint8 pointIndex, TeamId ownerTeam)
{
    OutdoorPvpWarMgr::Instance().OnHellfireFlagOwnerChanged(pointIndex, ownerTeam);
}

void OutdoorPvpWar_OnSilithusTurnIn(Player* player, uint32 count)
{
    OutdoorPvpWarMgr::Instance().OnSilithusTurnIn(player, count);
}

void OutdoorPvpWar_OnNagrandStateChanged(uint32 ownerTeam, uint32 guardsAlive)
{
    OutdoorPvpWarMgr::Instance().OnNagrandStateChanged(ownerTeam, guardsAlive);
}

bool OutdoorPvpWar_ConsumeHellfireNeutralResetRequest()
{
    return OutdoorPvpWarMgr::Instance().ConsumeHellfireNeutralResetRequest();
}

bool OutdoorPvpWar_ConsumeSilithusScoreResetRequest()
{
    return OutdoorPvpWarMgr::Instance().ConsumeSilithusScoreResetRequest();
}

bool OutdoorPvpWar_ConsumeNagrandNeutralResetRequest()
{
    return OutdoorPvpWarMgr::Instance().ConsumeNagrandNeutralResetRequest();
}

bool OutdoorPvpWar_IsHellfireWarActive()
{
    return OutdoorPvpWarMgr::Instance().IsHellfireWarActive();
}

bool OutdoorPvpWar_IsSilithusWarActive()
{
    return OutdoorPvpWarMgr::Instance().IsSilithusWarActive();
}

bool OutdoorPvpWar_IsNagrandWarActive()
{
    return OutdoorPvpWarMgr::Instance().IsNagrandWarActive();
}

bool OutdoorPvpWar_IsPortalEntryActive(uint32 entry)
{
    return OutdoorPvpWarMgr::Instance().IsPortalEntryActive(entry);
}

bool OutdoorPvpWar_IsZangarmarshWarActive()
{
    return OutdoorPvpWarMgr::Instance().IsZangarmarshWarActive();
}

void OutdoorPvpWar_OnZangarmarshVictory(uint32 winnerTeam)
{
    OutdoorPvpWarMgr::Instance().OnZangarmarshVictory(winnerTeam);
}

void AddSC_custom_outdoor_pvp_war_controller()
{
    new custom_outdoor_pvp_war_worldscript();
    new custom_outdoor_pvp_war_playerscript();
}
