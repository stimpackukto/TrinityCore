/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Custom Update - 2026-07-18
 * Scourge Invasion
 *
 * This implementation is deliberately self-contained. It only uses the
 * custom 960xxx templates, custom game event, and custom spawn rows created
 * by 2026_07_18_scourge_invasion.sql. It does not bind or change the legacy
 * Scourge Invasion templates, event 17, Player.h, or ScriptMgr.h.
 */

/*
 * Custom Update - 2026-07-19
 * Added the three original Scourge Invasion rare monsters at 15, 30,
 * and 45 invader kills.
 */

/*
 * Custom Update - 2026-07-19
 * Added item 56940 as a completed-site reward for players who remain within
 * 100 yards of the shard and accumulate at least 60 seconds of combat
 * participation during that site's invasion cycle.
 */

/*
 * Custom Update - 2026-07-19
 * Added dedicated combat scripts for Lumbering Horror, Spirit of the Damned,
 * and Bone Witch. Each rare keeps its own combat pattern and is automatically
 * removed when its source shard leaves the active invasion stage.
 */

/*
 * Custom Update - 2026-07-19
 * Added the original Shadow of Doom spell pattern: Mind Flay and Fear.
 */

/*
 * Custom Update - 2026-07-19
 * Added a server-wide announcement showing the two randomly selected invasion
 * zones when a new Scourge Invasion event cycle begins.
 */

/*
 * Custom Update - 2026-07-30
 * The completion of all selected invasion zones now starts one global
 * 30-minute idle period instead of respawning each completed shard locally.
 * The next invasion time is persisted in custom_scourge_invasion_runtime,
 * survives a worldserver restart, and starts a newly randomized two-zone
 * invasion when the idle period expires.
 * During the idle period the Scourge Invasion UI displays the remaining
 * minutes and seconds through world states 9103, 9104, and 9105.
 */

/*
 * Custom Update - 2026-09-01
 * Per-zone independent 30-minute invasion cycles
 *
 * - Each selected invasion zone completes independently.
 * - Completing one zone does not stop or reset the other selected zone.
 * - Each completed zone receives its own 30-minute maintenance timer.
 * - custom_scourge_invasion_state.cooldown_until stores each zone's
 *   persistent restart deadline.
 * - The timer survives a worldserver restart.
 * - When a local 30-minute timer expires, only that zone is reset.
 * - Existing minions are removed and new minion spawning is blocked
 *   throughout that zone's maintenance period.
 * - Idle HUD is shown only inside the selected zone currently waiting
 *   for its own restart.
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"

#include "DatabaseEnv.h"
#include "GameEventMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GameObjectData.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "TemporarySummon.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
    enum ScourgeInvasionData : uint32
    {
        DATA_INVADER_KILLED                    = 1,
        DATA_BOSS_DEFEATED                     = 2,
        DATA_SITE_COMPLETED                    = 3,
        DATA_SOURCE_SHARD_GUID                 = 4,
        DATA_SHADOW_SUMMONED                   = 5,
        DATA_ENGINEER_REPAIR_CHANNEL           = 6,
        DATA_SHADOW_DAMAGED_SHARD_GUID         = 7,
        DATA_INTERMEDIATE_SOURCE_SHARD_GUID    = 8
    };

    enum ScourgeInvasionStage : uint8
    {
        STAGE_ACTIVE   = 0,
        STAGE_DAMAGED  = 1,
        STAGE_COMPLETE = 2
    };

    enum ScourgeInvasionNpcs : uint32
    {
        NPC_LUMBERING_HORROR        = 14697,
        NPC_SPIRIT_OF_THE_DAMNED    = 16379,
        NPC_BONE_WITCH              = 16380,

        NPC_NECROTIC_SHARD          = 960000,
        NPC_DAMAGED_NECROTIC_SHARD  = 960001,
        NPC_CULTIST_ENGINEER        = 960002,
        NPC_SHADOW_OF_DOOM          = 960003,

        NPC_GHOUL_BERSERKER         = 960010,
        NPC_SPECTRAL_SOLDIER        = 960011,
        NPC_SKELETAL_SHOCKTROOPER   = 960012,
        NPC_MINION_SPAWNER          = 960020
    };

    enum ScourgeInvasionGameObjects : uint32
    {
        GO_NECROPOLIS                  = 960100,
        GO_SUMMON_CIRCLE               = 960101,
        GO_INVASION_PORTAL_ALLIANCE_I  = 244616,
        GO_INVASION_PORTAL_ALLIANCE_II = 244617,
        GO_INVASION_PORTAL_HORDE_I     = 244614,
        GO_INVASION_PORTAL_HORDE_II    = 244615
    };

    enum ScourgeInvasionPortalGuids : uint32
    {
        GUID_INVASION_PORTAL_ALLIANCE_I  = 167721,
        GUID_INVASION_PORTAL_ALLIANCE_II = 167722,
        GUID_INVASION_PORTAL_HORDE_I     = 167719,
        GUID_INVASION_PORTAL_HORDE_II    = 167720
    };

    struct ScourgeInvasionPortalSpawn
    {
        uint32 Guid;
        uint32 Entry;
    };

    std::array<ScourgeInvasionPortalSpawn, 4> const DalaranInvasionPortalSpawns =
    {{
        { uint32(GUID_INVASION_PORTAL_ALLIANCE_I),  uint32(GO_INVASION_PORTAL_ALLIANCE_I) },
        { uint32(GUID_INVASION_PORTAL_ALLIANCE_II), uint32(GO_INVASION_PORTAL_ALLIANCE_II) },
        { uint32(GUID_INVASION_PORTAL_HORDE_I),     uint32(GO_INVASION_PORTAL_HORDE_I) },
        { uint32(GUID_INVASION_PORTAL_HORDE_II),    uint32(GO_INVASION_PORTAL_HORDE_II) }
    }};

    uint32 s_DalaranInvasionPortalPhase = 0;

    enum ScourgeInvasionItems : uint32
    {
        ITEM_NECROTIC_RUNE          = 22484,
        ITEM_INVASION_CYCLE_REWARD  = 56940
    };

    enum ScourgeInvasionSpells : uint32
    {
        SPELL_REPAIR_BEAM_VISUAL          = 1120,
        SPELL_SOUL_REVIVAL                = 28681,
        SPELL_BUTTRESS_CHANNEL            = 28078,

        SPELL_LUMBERING_CLEAVE            = 15496,
        SPELL_LUMBERING_DISEASE_CLOUD     = 16577,
        SPELL_LUMBERING_KNOCK_AWAY        = 10101,
        SPELL_LUMBERING_ENRAGE            = 8599,

        SPELL_SPIRIT_RIBBON_OF_SOULS      = 22743,
        SPELL_SPIRIT_PSYCHIC_SCREAM       = 13704,
        SPELL_SPIRIT_DRAIN_LIFE           = 17238,

        SPELL_BONE_WITCH_ARCANE_BOLT      = 20829,
        SPELL_BONE_WITCH_BONE_SHARDS_PROC = 32900,
        SPELL_BONE_WITCH_FROST_NOVA       = 15531,

        SPELL_SHADOW_OF_DOOM_FEAR          = 12542,
        SPELL_SHADOW_OF_DOOM_MIND_FLAY     = 16568
    };

    // 9100~9102: active-site HUD
    // 9103~9105: current selected-zone idle countdown HUD
    enum ScourgeInvasionHudWorldStates : uint32
    {
        WORLD_STATE_SCOURGE_INVASION_HUD_SHOW          = 9100,
        WORLD_STATE_SCOURGE_INVASION_HUD_KILL_COUNT    = 9101,
        WORLD_STATE_SCOURGE_INVASION_HUD_SHARD_HEALTH  = 9102,

        WORLD_STATE_SCOURGE_INVASION_IDLE_SHOW          = 9103,
        WORLD_STATE_SCOURGE_INVASION_IDLE_MINUTES       = 9104,
        WORLD_STATE_SCOURGE_INVASION_IDLE_SECONDS       = 9105
    };

    constexpr uint32 KILLS_TO_DAMAGE_SHARD = 60;
    constexpr uint32 ENGINEER_RUNE_COST = 8;
    constexpr uint8 ENGINEER_COUNT = 4;
    constexpr uint8 ACTIVE_INVASION_ZONE_COUNT = 2;

    // One engineer repairs in ten minutes. Two, three, and four active
    // channels complete in 7:30, 5:00, and 2:30 respectively.
    constexpr uint32 ENGINEER_REPAIR_WINDOW_SECONDS = (10 * 60) / ENGINEER_COUNT;
    constexpr uint32 REPAIR_RATE_UNITS_PER_SECOND = 12;
    constexpr uint32 REPAIR_PROGRESS_SAVE_INTERVAL_SECONDS = 5;

    // Each completed invasion zone waits this long independently.
    constexpr uint32 INVASION_IDLE_SECONDS = 30 * 60;

    constexpr uint32 SHARD_KILL_RADIUS = 110;
    constexpr uint32 SPAWNER_ACTIVE_RADIUS = 120;
    constexpr uint32 SPAWNER_SHARD_RADIUS = 100;
    constexpr uint32 MAX_MINIONS_PER_SPAWNER = 2;
    constexpr uint32 MINION_SPAWN_INTERVAL = 8 * IN_MILLISECONDS;
    constexpr uint32 MINION_DESPAWN_DURATION = 2 * 60 * IN_MILLISECONDS;
    constexpr uint32 SHADOW_DESPAWN_DURATION = 10 * 60 * IN_MILLISECONDS;
    constexpr uint32 SAVE_EVERY_KILLS = 5;
    constexpr uint32 MINIMUM_COMBAT_PARTICIPATION_SECONDS = 60;
    constexpr float HUD_DISPLAY_RADIUS = 120.0f;
    constexpr float CYCLE_REWARD_RADIUS = 100.0f;
    constexpr float INVASION_PORTAL_TRIGGER_RADIUS = 2.5f;
    constexpr uint32 INVASION_PORTAL_CHECK_INTERVAL = 250;
    constexpr float INVASION_PORTAL_DESTINATION_OFFSET = 35.0f;
    constexpr float HALF_PI = 1.57079632679f;
    constexpr float TWO_PI = 6.28318530718f;

    struct ZoneWorldState
    {
        uint32 ZoneId;
        uint32 WorldStateId;
        char const* ZoneName;
    };

    struct ScourgeInvasionPortalDestination
    {
        uint32 MapId = 0;
        float PositionX = 0.0f;
        float PositionY = 0.0f;
        float PositionZ = 0.0f;
        float Orientation = 0.0f;
        char const* ZoneName = nullptr;
    };

    std::array<ZoneWorldState, 6> const ScourgeInvasionZones =
    {{
        { 618, 2259, "여명의 설원" },
        { 16,  2260, "아즈샤라" },
        { 4,   2261, "저주받은 땅" },
        { 46,  2262, "불타는 평원" },
        { 440, 2263, "타나리스" },
        { 139, 2264, "동부 역병지대" }
    }};

    std::array<uint32, 3> const InvasionMinionEntries =
    {{
        NPC_GHOUL_BERSERKER,
        NPC_SPECTRAL_SOLDIER,
        NPC_SKELETAL_SHOCKTROOPER
    }};

    struct IntermediateBossSpawn
    {
        uint16 RequiredKills;
        uint32 Entry;
    };

    std::array<IntermediateBossSpawn, 3> const IntermediateBossSpawns =
    {{
        { 15, NPC_LUMBERING_HORROR },
        { 30, NPC_SPIRIT_OF_THE_DAMNED },
        { 45, NPC_BONE_WITCH }
    }};

    struct ShardProgress
    {
        uint8 Stage = STAGE_ACTIVE;
        uint16 KillCount = 0;
        uint8 BossCount = 0;
        time_t CooldownUntil = 0;
    };

    bool IsScourgeInvasionMinion(uint32 entry)
    {
        return std::find(
            InvasionMinionEntries.begin(),
            InvasionMinionEntries.end(),
            entry) != InvasionMinionEntries.end();
    }

    bool GetScourgeInvasionPortalInfo(
        uint32 entry,
        TeamId& teamId,
        uint8& portalSlot)
    {
        switch (entry)
        {
            case GO_INVASION_PORTAL_ALLIANCE_I:
                teamId = TEAM_ALLIANCE;
                portalSlot = 0;
                return true;

            case GO_INVASION_PORTAL_ALLIANCE_II:
                teamId = TEAM_ALLIANCE;
                portalSlot = 1;
                return true;

            case GO_INVASION_PORTAL_HORDE_I:
                teamId = TEAM_HORDE;
                portalSlot = 0;
                return true;

            case GO_INVASION_PORTAL_HORDE_II:
                teamId = TEAM_HORDE;
                portalSlot = 1;
                return true;

            default:
                return false;
        }
    }

    void SnapToGround(Creature* creature)
    {
        if (!creature)
            return;

        Map* map = creature->FindMap();
        if (!map)
            return;

        float const groundZ = map->GetHeight(
            creature->GetPositionX(),
            creature->GetPositionY(),
            MAX_HEIGHT,
            true,
            MAX_FALL_DISTANCE);

        if (groundZ > INVALID_HEIGHT)
        {
            map->CreatureRelocation(
                creature,
                creature->GetPositionX(),
                creature->GetPositionY(),
                groundZ,
                creature->GetOrientation(),
                false);

            creature->SetHomePosition(
                creature->GetPositionX(),
                creature->GetPositionY(),
                groundZ,
                creature->GetOrientation());
        }
    }

    class ScourgeInvasionMgr
    {
    public:
        static ScourgeInvasionMgr& Instance()
        {
            static ScourgeInvasionMgr instance;
            return instance;
        }

        void Initialize()
        {
            if (_initialized)
                return;

            LoadConfiguration();
            LoadRuntimeEventState();
            LoadZoneStates();
            LoadSelectedZones();
            LoadZoneIdleTimes();
            CorrectStaticSpawnHeights();
            SyncEventState();

            SetDalaranPortalPhase(
                IsEventActive() && HasAnyActiveSelectedZone(),
                true);

            _initialized = true;
        }

        void Update(uint32 diff)
        {
            if (!_initialized)
                Initialize();

            if (_idleHudUpdateTimer > diff)
                _idleHudUpdateTimer -= diff;
            else
            {
                _idleHudUpdateTimer = IN_MILLISECONDS;
                UpdateIdleHudForAllPlayers();
            }

            if (_syncTimer > diff)
            {
                _syncTimer -= diff;
                return;
            }

            _syncTimer = 10 * IN_MILLISECONDS;
            SyncEventState();
        }

        bool IsEventActive() const
        {
            return _eventId != 0 &&
                sGameEventMgr->IsActiveEvent(_eventId);
        }

        bool IsZoneSelected(uint32 zoneId) const
        {
            return _selectedZoneIds.find(zoneId) !=
                _selectedZoneIds.end();
        }

        bool IsZoneIdleState(uint32 zoneId) const
        {
            if (!IsEventActive() ||
                !IsZoneSelected(zoneId) ||
                IsZoneUnderAttack(zoneId))
            {
                return false;
            }

            auto const itr = _zoneNextInvasionTime.find(zoneId);
            if (itr == _zoneNextInvasionTime.end())
                return false;

            return std::time(nullptr) < itr->second;
        }

        uint32 GetZoneNextInvasionRemainingSeconds(
            uint32 zoneId) const
        {
            if (!IsZoneIdleState(zoneId))
                return 0;

            auto const itr =
                _zoneNextInvasionTime.find(zoneId);

            if (itr == _zoneNextInvasionTime.end())
                return 0;

            time_t const now = std::time(nullptr);

            if (now >= itr->second)
                return 0;

            return uint32(itr->second - now);
        }

        uint32 GetEventCycle() const
        {
            return _eventCycle;
        }

        void RestartZoneAfterIdle(uint32 zoneId)
        {
            if (!IsEventActive() ||
                !IsZoneSelected(zoneId))
            {
                return;
            }

            _zoneNextInvasionTime.erase(zoneId);

            /*
             * 해당 지역의 완료된 수정 상태만 초기화한다.
             * 다른 선택 지역의 진행 상태는 절대로 건드리지 않는다.
             */
            WorldDatabase.PExecute(
                "UPDATE `custom_scourge_invasion_state` AS `state` "
                "INNER JOIN `custom_scourge_invasion_spawn` AS `spawn` "
                "ON `spawn`.`guid` = `state`.`shard_guid` "
                "SET "
                "`state`.`stage` = {}, "
                "`state`.`kill_count` = 0, "
                "`state`.`boss_count` = 0, "
                "`state`.`cooldown_until` = NULL "
                "WHERE `spawn`.`spawn_role` = 0 "
                "AND `spawn`.`zone_id` = {} "
                "AND `state`.`stage` = {}",
                uint32(STAGE_ACTIVE),
                zoneId,
                uint32(STAGE_COMPLETE));

            bool const wasUnderAttack =
                IsZoneUnderAttack(zoneId);

            if (!wasUnderAttack)
            {
                SetZoneUnderAttack(zoneId, true);
                BroadcastWorldStates();
                SetDalaranPortalPhase(true);

                if (char const* zoneName =
                        GetZoneName(zoneId))
                {
                    std::string message =
                        "[스컬지 침공] ";

                    message += zoneName;
                    message +=
                        " 지역의 정비가 끝나 침공이 다시 시작되었습니다.";

                    sWorld->SendServerMessage(
                        SERVER_MSG_STRING,
                        message.c_str());
                }
            }
        }

        bool GetPortalDestination(
            uint8 portalSlot,
            TeamId portalTeam,
            ScourgeInvasionPortalDestination& destination) const
        {
            if (!IsEventActive() ||
                portalSlot >= ACTIVE_INVASION_ZONE_COUNT)
            {
                return false;
            }

            ZoneWorldState const* selectedZone = nullptr;
            uint8 selectedSlot = 0;

            for (ZoneWorldState const& zoneState :
                ScourgeInvasionZones)
            {
                if (!IsZoneSelected(zoneState.ZoneId))
                    continue;

                if (selectedSlot == portalSlot)
                {
                    selectedZone = &zoneState;
                    break;
                }

                ++selectedSlot;
            }

            if (!selectedZone ||
                !IsZoneUnderAttack(selectedZone->ZoneId))
            {
                return false;
            }

            QueryResult result =
                WorldDatabase.PQuery(
                    "SELECT `map_id`, `position_x`, "
                    "`position_y`, `position_z`, `orientation` "
                    "FROM `custom_scourge_invasion_site` "
                    "WHERE `zone_id` = {} "
                    "ORDER BY `site_id` LIMIT 1",
                    selectedZone->ZoneId);

            if (!result)
                return false;

            Field* fields = result->Fetch();

            uint32 const mapId =
                fields[0].GetUInt32();

            float const shardX =
                fields[1].GetFloat();

            float const shardY =
                fields[2].GetFloat();

            float const shardZ =
                fields[3].GetFloat();

            float const shardOrientation =
                fields[4].GetFloat();

            float const destinationAngle =
                shardOrientation +
                (portalTeam == TEAM_ALLIANCE
                    ? HALF_PI
                    : -HALF_PI);

            float const destinationX =
                shardX +
                std::cos(destinationAngle) *
                INVASION_PORTAL_DESTINATION_OFFSET;

            float const destinationY =
                shardY +
                std::sin(destinationAngle) *
                INVASION_PORTAL_DESTINATION_OFFSET;

            float destinationZ = shardZ;

            if (Map* map =
                    sMapMgr->CreateBaseMap(mapId))
            {
                float const groundZ =
                    map->GetHeight(
                        destinationX,
                        destinationY,
                        MAX_HEIGHT,
                        true,
                        MAX_FALL_DISTANCE);

                if (groundZ > INVALID_HEIGHT)
                    destinationZ = groundZ;
            }

            destination.MapId = mapId;
            destination.PositionX = destinationX;
            destination.PositionY = destinationY;
            destination.PositionZ = destinationZ;

            destination.Orientation =
                std::atan2(
                    shardY - destinationY,
                    shardX - destinationX);

            destination.ZoneName =
                selectedZone->ZoneName;

            return true;
        }

        void SetDalaranPortalPhase(
            bool active,
            bool force = false) const
        {
            uint32 const phaseMask =
                active ? 1 : 2;

            if (!force &&
                s_DalaranInvasionPortalPhase ==
                    phaseMask)
            {
                return;
            }

            s_DalaranInvasionPortalPhase =
                phaseMask;

            for (ScourgeInvasionPortalSpawn const&
                portalSpawn :
                DalaranInvasionPortalSpawns)
            {
                GameObjectData const* data =
                    sObjectMgr->GetGameObjectData(
                        portalSpawn.Guid);

                if (!data)
                {
                    TC_LOG_ERROR(
                        "custom",
                        "[Scourge Invasion] 달라란 침공 포탈 GameObjectData 없음: guid={}, entry={}",
                        portalSpawn.Guid,
                        portalSpawn.Entry);

                    continue;
                }

                const_cast<GameObjectData*>(data)
                    ->phaseMask = phaseMask;

                uint32 const portalGuid =
                    portalSpawn.Guid;

                uint32 const portalEntry =
                    portalSpawn.Entry;

                sMapMgr->DoForAllMapsWithMapId(
                    data->mapId,
                    [phaseMask,
                     portalGuid,
                     portalEntry](Map* map) -> void
                    {
                        if (!map)
                            return;

                        auto bounds =
                            Trinity::Containers::MapEqualRange(
                                map->GetGameObjectBySpawnIdStore(),
                                portalGuid);

                        for (auto const& pair :
                            bounds)
                        {
                            GameObject* portal =
                                pair.second;

                            if (!portal)
                                continue;

                            if (portal->GetSpawnId() !=
                                portalGuid)
                            {
                                continue;
                            }

                            if (portal->GetEntry() !=
                                portalEntry)
                            {
                                continue;
                            }

                            portal->SetPhaseMask(
                                phaseMask,
                                true);

                            portal
                                ->UpdateObjectVisibility();
                        }
                    });
            }

            TC_LOG_INFO(
                "custom",
                "[Scourge Invasion] 달라란 침공 포탈 페이즈 변경: phase={}",
                phaseMask);
        }

        ShardProgress LoadShardProgress(
            uint32 shardGuid) const
        {
            ShardProgress progress;

            QueryResult result =
                WorldDatabase.PQuery(
                    "SELECT `stage`, `kill_count`, "
                    "`boss_count`, "
                    "IFNULL(UNIX_TIMESTAMP(`cooldown_until`), 0) "
                    "FROM `custom_scourge_invasion_state` "
                    "WHERE `shard_guid` = {}",
                    shardGuid);

            if (!result)
                return progress;

            Field* fields = result->Fetch();

            progress.Stage =
                fields[0].GetUInt8();

            progress.KillCount =
                fields[1].GetUInt16();

            progress.BossCount =
                fields[2].GetUInt8();

            progress.CooldownUntil =
                time_t(fields[3].GetUInt64());

            return progress;
        }

        void SaveShardProgress(
            uint32 shardGuid,
            ShardProgress const& progress) const
        {
            uint64 const cooldownUntil =
                progress.CooldownUntil > 0
                    ? uint64(progress.CooldownUntil)
                    : 0;

            WorldDatabase.PExecute(
                "INSERT INTO `custom_scourge_invasion_state` "
                "(`shard_guid`, `stage`, `kill_count`, "
                "`boss_count`, `cooldown_until`) "
                "VALUES ({}, {}, {}, {}, "
                "IF({} = 0, NULL, FROM_UNIXTIME({}))) "
                "ON DUPLICATE KEY UPDATE "
                "`stage` = VALUES(`stage`), "
                "`kill_count` = VALUES(`kill_count`), "
                "`boss_count` = VALUES(`boss_count`), "
                "`cooldown_until` = VALUES(`cooldown_until`)",
                shardGuid,
                uint32(progress.Stage),
                uint32(progress.KillCount),
                uint32(progress.BossCount),
                cooldownUntil,
                cooldownUntil);
        }

        void SaveShardRepairDeadline(
            uint32 shardGuid,
            time_t cooldownUntil) const
        {
            uint64 const deadline =
                cooldownUntil > 0
                    ? uint64(cooldownUntil)
                    : 0;

            WorldDatabase.PExecute(
                "UPDATE `custom_scourge_invasion_state` "
                "SET `cooldown_until` = "
                "IF({} = 0, NULL, FROM_UNIXTIME({})) "
                "WHERE `shard_guid` = {} "
                "AND `stage` = {}",
                deadline,
                deadline,
                shardGuid,
                uint32(STAGE_DAMAGED));
        }

        void SendWorldStates(Player* player) const
        {
            if (!player)
                return;

            bool const eventActive =
                IsEventActive();

            for (ZoneWorldState const& zoneState :
                ScourgeInvasionZones)
            {
                bool const underAttack =
                    eventActive &&
                    IsZoneUnderAttack(
                        zoneState.ZoneId);

                player->SendUpdateWorldState(
                    zoneState.WorldStateId,
                    underAttack ? 1 : 0);
            }

            if (IsZoneIdleState(
                    player->GetZoneId()))
            {
                SendIdleHud(player);
            }
            else
            {
                ClearIdleHud(player);
                ClearInvasionHud(player);
            }
        }

        void UpdateShardHud(
            Creature* sourceShard,
            ShardProgress const& progress) const
        {
            if (!sourceShard)
                return;

            Map* map =
                sourceShard->FindMap();

            if (!map)
                return;

            bool const shouldShow =
                IsEventActive() &&
                IsZoneSelected(
                    sourceShard->GetZoneId()) &&
                IsZoneUnderAttack(
                    sourceShard->GetZoneId()) &&
                (progress.Stage == STAGE_ACTIVE ||
                 progress.Stage == STAGE_DAMAGED);

            Map::PlayerList const& players =
                map->GetPlayers();

            for (Map::PlayerList::const_iterator itr =
                    players.begin();
                 itr != players.end();
                 ++itr)
            {
                Player* player =
                    itr->GetSource();

                if (!player)
                    continue;

                if (shouldShow &&
                    player->IsWithinDistInMap(
                        sourceShard,
                        HUD_DISPLAY_RADIUS))
                {
                    SendInvasionHud(
                        player,
                        sourceShard,
                        progress);
                }
                else
                {
                    ClearInvasionHudForSource(
                        player,
                        sourceShard->GetGUID());
                }
            }
        }

        void UpdateMapHud(Map* map) const
        {
            if (!map)
                return;

            Map::PlayerList const& players =
                map->GetPlayers();

            for (Map::PlayerList::const_iterator itr =
                    players.begin();
                 itr != players.end();
                 ++itr)
            {
                Player* player =
                    itr->GetSource();

                if (!player)
                    continue;

                if (IsZoneIdleState(
                        player->GetZoneId()))
                {
                    SendIdleHud(player);
                    continue;
                }

                ClearIdleHud(player);

                auto const sourceItr =
                    _hudSourceByPlayer.find(
                        player->GetGUID()
                            .GetCounter());

                if (sourceItr ==
                    _hudSourceByPlayer.end())
                {
                    continue;
                }

                Creature* const sourceShard =
                    ObjectAccessor::GetCreature(
                        *player,
                        sourceItr->second);

                if (!IsEventActive() ||
                    !sourceShard ||
                    !IsZoneSelected(
                        sourceShard->GetZoneId()) ||
                    !IsZoneUnderAttack(
                        sourceShard->GetZoneId()) ||
                    sourceShard->GetMapId() !=
                        player->GetMapId() ||
                    !player->IsWithinDistInMap(
                        sourceShard,
                        HUD_DISPLAY_RADIUS))
                {
                    ClearInvasionHud(player);
                }
            }
        }

        void ForgetInvasionHud(
            Player* player)
        {
            if (!player)
                return;

            uint32 const playerGuid =
                player->GetGUID().GetCounter();

            _hudVisiblePlayers.erase(
                playerGuid);

            _hudSourceByPlayer.erase(
                playerGuid);

            _idleHudVisiblePlayers.erase(
                playerGuid);
        }

        void ClearInvasionHud(
            Player* player) const
        {
            if (!player)
                return;

            uint32 const playerGuid =
                player->GetGUID().GetCounter();

            _hudSourceByPlayer.erase(
                playerGuid);

            if (_hudVisiblePlayers.erase(
                    playerGuid))
            {
                player->SendUpdateWorldState(
                    WORLD_STATE_SCOURGE_INVASION_HUD_SHOW,
                    0);
            }
        }

        void SetZoneUnderAttack(
            uint32 zoneId,
            bool underAttack)
        {
            _zoneStates[zoneId] =
                underAttack;

            WorldDatabase.PExecute(
                "INSERT INTO "
                "`custom_scourge_invasion_zone_state` "
                "(`zone_id`, `world_state_id`, `is_active`) "
                "VALUES ({}, {}, {}) "
                "ON DUPLICATE KEY UPDATE "
                "`is_active` = VALUES(`is_active`)",
                zoneId,
                GetWorldStateForZone(zoneId),
                underAttack ? 1 : 0);
        }

        void UpdateZoneAfterShardCompletion(
            uint32 zoneId,
            time_t cooldownUntil)
        {
            if (!IsZoneSelected(zoneId))
                return;

            if (cooldownUntil > 0)
            {
                auto itr =
                    _zoneNextInvasionTime.find(
                        zoneId);

                if (itr ==
                        _zoneNextInvasionTime.end() ||
                    itr->second < cooldownUntil)
                {
                    _zoneNextInvasionTime[
                        zoneId] = cooldownUntil;
                }
            }

            QueryResult result =
                WorldDatabase.PQuery(
                    "SELECT COUNT(*) "
                    "FROM `custom_scourge_invasion_spawn` "
                    "AS `spawn` "
                    "LEFT JOIN "
                    "`custom_scourge_invasion_state` "
                    "AS `state` "
                    "ON `state`.`shard_guid` = "
                    "`spawn`.`guid` "
                    "WHERE `spawn`.`spawn_role` = 0 "
                    "AND `spawn`.`zone_id` = {} "
                    "AND (`state`.`stage` IS NULL "
                    "OR `state`.`stage` <> {})",
                    zoneId,
                    uint32(STAGE_COMPLETE));

            bool const hasRemainingShard =
                result &&
                result->Fetch()[0]
                    .GetUInt64() > 0;

            if (!hasRemainingShard)
            {
                SetZoneUnderAttack(
                    zoneId,
                    false);

                BroadcastWorldStates();

                SetDalaranPortalPhase(
                    HasAnyActiveSelectedZone());

                if (char const* zoneName =
                        GetZoneName(zoneId))
                {
                    std::string message =
                        "[스컬지 침공] ";

                    message += zoneName;
                    message +=
                        " 지역의 침공이 종료되었습니다. "
                        "30분 후 해당 지역에서 다시 시작됩니다.";

                    sWorld->SendServerMessage(
                        SERVER_MSG_STRING,
                        message.c_str());
                }
            }
        }

        void UpdateCombatParticipation(
            Creature* sourceShard)
        {
            if (!sourceShard ||
                !IsEventActive() ||
                !IsZoneSelected(
                    sourceShard->GetZoneId()) ||
                IsZoneIdleState(
                    sourceShard->GetZoneId()))
            {
                return;
            }

            Map* map =
                sourceShard->FindMap();

            if (!map)
                return;

            uint32 const shardGuid =
                sourceShard->GetGUID()
                    .GetCounter();

            std::unordered_map<uint32, uint32>&
                participants =
                    _combatParticipationSeconds[
                        shardGuid];

            Map::PlayerList const& players =
                map->GetPlayers();

            for (Map::PlayerList::const_iterator itr =
                    players.begin();
                 itr != players.end();
                 ++itr)
            {
                Player* player =
                    itr->GetSource();

                if (!player ||
                    !player->IsInCombat() ||
                    !player->IsWithinDistInMap(
                        sourceShard,
                        CYCLE_REWARD_RADIUS))
                {
                    continue;
                }

                uint32& participationSeconds =
                    participants[
                        player->GetGUID()
                            .GetCounter()];

                if (participationSeconds <
                    MINIMUM_COMBAT_PARTICIPATION_SECONDS)
                {
                    ++participationSeconds;
                }
            }
        }

        void GrantCycleReward(
            Creature* sourceShard)
        {
            if (!sourceShard)
                return;

            uint32 const shardGuid =
                sourceShard->GetGUID()
                    .GetCounter();

            auto const participantsItr =
                _combatParticipationSeconds.find(
                    shardGuid);

            if (participantsItr ==
                _combatParticipationSeconds.end())
            {
                return;
            }

            Map* map =
                sourceShard->FindMap();

            if (!map)
            {
                _combatParticipationSeconds.erase(
                    participantsItr);

                return;
            }

            Map::PlayerList const& players =
                map->GetPlayers();

            for (Map::PlayerList::const_iterator itr =
                    players.begin();
                 itr != players.end();
                 ++itr)
            {
                Player* player =
                    itr->GetSource();

                if (!player ||
                    !player->IsWithinDistInMap(
                        sourceShard,
                        CYCLE_REWARD_RADIUS))
                {
                    continue;
                }

                auto const playerItr =
                    participantsItr->second.find(
                        player->GetGUID()
                            .GetCounter());

                if (playerItr ==
                        participantsItr->second.end() ||
                    playerItr->second <
                        MINIMUM_COMBAT_PARTICIPATION_SECONDS)
                {
                    continue;
                }

                player->AddItem(
                    ITEM_INVASION_CYCLE_REWARD,
                    1);
            }

            _combatParticipationSeconds.erase(
                participantsItr);
        }

        void ClearCombatParticipation(
            uint32 shardGuid)
        {
            _combatParticipationSeconds.erase(
                shardGuid);
        }

    private:
        void LoadConfiguration()
        {
            QueryResult result =
                WorldDatabase.Query(
                    "SELECT `event_id` "
                    "FROM `custom_scourge_invasion_config` "
                    "WHERE `id` = 1");

            _eventId =
                result
                    ? result->Fetch()[0]
                        .GetUInt8()
                    : 0;
        }

        void LoadRuntimeEventState()
        {
            QueryResult result =
                WorldDatabase.Query(
                    "SELECT `event_active` "
                    "FROM `custom_scourge_invasion_runtime` "
                    "WHERE `id` = 1");

            if (!result)
            {
                _eventWasActive = false;
                return;
            }

            Field* fields =
                result->Fetch();

            _eventWasActive =
                fields[0].GetBool();
        }

        void LoadZoneStates()
        {
            _zoneStates.clear();

            QueryResult result =
                WorldDatabase.Query(
                    "SELECT `zone_id`, `is_active` "
                    "FROM "
                    "`custom_scourge_invasion_zone_state`");

            if (result)
            {
                do
                {
                    Field* fields =
                        result->Fetch();

                    _zoneStates[
                        fields[0].GetUInt32()] =
                            fields[1].GetBool();
                }
                while (result->NextRow());
            }

            for (ZoneWorldState const& zoneState :
                ScourgeInvasionZones)
            {
                if (_zoneStates.find(
                        zoneState.ZoneId) ==
                    _zoneStates.end())
                {
                    _zoneStates.emplace(
                        zoneState.ZoneId,
                        false);
                }
            }
        }

        void LoadSelectedZones()
        {
            _selectedZoneIds.clear();

            for (ZoneWorldState const& zoneState :
                ScourgeInvasionZones)
            {
                if (IsZoneUnderAttack(
                        zoneState.ZoneId))
                {
                    _selectedZoneIds.insert(
                        zoneState.ZoneId);
                }
            }

            /*
             * 서버 재시작 시 한 지역이 이미 완료되어
             * is_active = 0 이더라도 state 테이블에서
             * 원래 선택되었던 지역을 복원한다.
             */
            if (!_eventWasActive)
                return;

            QueryResult result =
                WorldDatabase.Query(
                    "SELECT DISTINCT "
                    "`spawn`.`zone_id` "
                    "FROM "
                    "`custom_scourge_invasion_spawn` "
                    "AS `spawn` "
                    "INNER JOIN "
                    "`custom_scourge_invasion_state` "
                    "AS `state` "
                    "ON `state`.`shard_guid` = "
                    "`spawn`.`guid` "
                    "WHERE `spawn`.`spawn_role` = 0");

            if (!result)
                return;

            do
            {
                uint32 const zoneId =
                    result->Fetch()[0]
                        .GetUInt32();

                for (ZoneWorldState const&
                    zoneState :
                    ScourgeInvasionZones)
                {
                    if (zoneState.ZoneId ==
                        zoneId)
                    {
                        _selectedZoneIds.insert(
                            zoneId);

                        break;
                    }
                }
            }
            while (result->NextRow());
        }

        void LoadZoneIdleTimes()
        {
            _zoneNextInvasionTime.clear();

            if (!_eventWasActive)
                return;

            QueryResult result =
                WorldDatabase.Query(
                    "SELECT "
                    "`spawn`.`zone_id`, "
                    "MAX(IFNULL("
                    "UNIX_TIMESTAMP("
                    "`state`.`cooldown_until`), 0)) "
                    "FROM "
                    "`custom_scourge_invasion_spawn` "
                    "AS `spawn` "
                    "INNER JOIN "
                    "`custom_scourge_invasion_state` "
                    "AS `state` "
                    "ON `state`.`shard_guid` = "
                    "`spawn`.`guid` "
                    "WHERE `spawn`.`spawn_role` = 0 "
                    "AND `state`.`stage` = 2 "
                    "GROUP BY `spawn`.`zone_id`");

            if (!result)
                return;

            time_t const now =
                std::time(nullptr);

            do
            {
                Field* fields =
                    result->Fetch();

                uint32 const zoneId =
                    fields[0].GetUInt32();

                time_t cooldownUntil =
                    time_t(
                        fields[1].GetUInt64());

                if (!IsZoneSelected(zoneId))
                    continue;

                /*
                 * 이전 전역 30분 방식과 호환.
                 * 기존 완료 데이터는 cooldown_until이
                 * NULL이므로 해당 지역에 새 30분 시간을
                 * 설정한다.
                 */
                if (!cooldownUntil)
                {
                    cooldownUntil =
                        now +
                        time_t(
                            INVASION_IDLE_SECONDS);

                    WorldDatabase.PExecute(
                        "UPDATE "
                        "`custom_scourge_invasion_state` "
                        "AS `state` "
                        "INNER JOIN "
                        "`custom_scourge_invasion_spawn` "
                        "AS `spawn` "
                        "ON `spawn`.`guid` = "
                        "`state`.`shard_guid` "
                        "SET "
                        "`state`.`cooldown_until` = "
                        "FROM_UNIXTIME({}) "
                        "WHERE "
                        "`spawn`.`spawn_role` = 0 "
                        "AND `spawn`.`zone_id` = {} "
                        "AND `state`.`stage` = {}",
                        uint64(cooldownUntil),
                        zoneId,
                        uint32(STAGE_COMPLETE));
                }

                _zoneNextInvasionTime[
                    zoneId] = cooldownUntil;
            }
            while (result->NextRow());
        }

        void CorrectStaticSpawnHeights()
        {
            if (!_eventId)
                return;

            QueryResult result =
                WorldDatabase.Query(
                    "SELECT "
                    "`spawn`.`guid`, "
                    "`spawn`.`site_id`, "
                    "`spawn`.`spawn_role`, "
                    "`creature`.`map`, "
                    "`creature`.`position_x`, "
                    "`creature`.`position_y`, "
                    "`creature`.`position_z`, "
                    "`creature`.`orientation` "
                    "FROM "
                    "`custom_scourge_invasion_spawn` "
                    "AS `spawn` "
                    "INNER JOIN `creature` "
                    "AS `creature` "
                    "ON `creature`.`guid` = "
                    "`spawn`.`guid`");

            if (!result)
                return;

            do
            {
                Field* fields =
                    result->Fetch();

                uint32 const spawnGuid =
                    fields[0].GetUInt32();

                uint32 const siteId =
                    fields[1].GetUInt32();

                uint8 const spawnRole =
                    fields[2].GetUInt8();

                uint32 const mapId =
                    fields[3].GetUInt32();

                float const x =
                    fields[4].GetFloat();

                float const y =
                    fields[5].GetFloat();

                float const currentZ =
                    fields[6].GetFloat();

                float const orientation =
                    fields[7].GetFloat();

                Map* map =
                    sMapMgr->CreateBaseMap(
                        mapId);

                if (!map)
                    continue;

                float const groundZ =
                    map->GetHeight(
                        x,
                        y,
                        MAX_HEIGHT,
                        true,
                        MAX_FALL_DISTANCE);

                if (groundZ <= INVALID_HEIGHT ||
                    std::fabs(
                        currentZ - groundZ) <
                        0.01f)
                {
                    continue;
                }

                CreatureData& creatureData =
                    sObjectMgr
                        ->NewOrExistCreatureData(
                            spawnGuid);

                creatureData.spawnPoint.Relocate(
                    x,
                    y,
                    groundZ,
                    orientation);

                WorldDatabase.PExecute(
                    "UPDATE `creature` "
                    "SET `position_z` = {} "
                    "WHERE `guid` = {}",
                    groundZ,
                    spawnGuid);

                if (spawnRole == 0)
                {
                    WorldDatabase.PExecute(
                        "UPDATE "
                        "`custom_scourge_invasion_site` "
                        "SET `position_z` = {} "
                        "WHERE `site_id` = {}",
                        groundZ,
                        siteId);
                }
            }
            while (result->NextRow());
        }

        void SyncEventState()
        {
            bool const gameEventActive =
                IsEventActive();

            time_t const now =
                std::time(nullptr);

            if (!gameEventActive)
            {
                if (_eventWasActive ||
                    !_selectedZoneIds.empty() ||
                    !_zoneNextInvasionTime.empty())
                {
                    StopEventCycle();
                }

                return;
            }

            if (!_eventWasActive)
            {
                _eventWasActive = true;
                StartNewEventCycle();
                return;
            }

            if (!_selectedZoneIds.empty() &&
                _selectedZoneIds.size() !=
                    ACTIVE_INVASION_ZONE_COUNT)
            {
                StartNewEventCycle();
                return;
            }

            if (_selectedZoneIds.empty())
            {
                StartNewEventCycle();
                return;
            }

            std::array<uint32, 6>
                expiredZoneIds =
                {{
                    0, 0, 0, 0, 0, 0
                }};

            uint8 expiredZoneCount = 0;

            for (auto const& pair :
                _zoneNextInvasionTime)
            {
                if (now >= pair.second &&
                    expiredZoneCount <
                        expiredZoneIds.size())
                {
                    expiredZoneIds[
                        expiredZoneCount++] =
                            pair.first;
                }
            }

            for (uint8 index = 0;
                 index < expiredZoneCount;
                 ++index)
            {
                RestartZoneAfterIdle(
                    expiredZoneIds[index]);
            }
        }

        void StartNewEventCycle()
        {
            WorldDatabase.Execute(
                "DELETE FROM "
                "`custom_scourge_invasion_state`");

            _combatParticipationSeconds.clear();
            _selectedZoneIds.clear();
            _zoneNextInvasionTime.clear();

            ++_eventCycle;

            for (ZoneWorldState const& zoneState :
                ScourgeInvasionZones)
            {
                SetZoneUnderAttack(
                    zoneState.ZoneId,
                    false);
            }

            std::array<uint8, 6> zoneIndexes =
            {{
                0, 1, 2, 3, 4, 5
            }};

            std::array<
                char const*,
                ACTIVE_INVASION_ZONE_COUNT>
                selectedZoneNames =
            {{
                nullptr,
                nullptr
            }};

            for (uint8 index = 0;
                 index <
                    ACTIVE_INVASION_ZONE_COUNT;
                 ++index)
            {
                uint8 const randomIndex =
                    uint8(
                        urand(
                            index,
                            uint32(
                                zoneIndexes.size() -
                                1)));

                std::swap(
                    zoneIndexes[index],
                    zoneIndexes[randomIndex]);
            }

            for (uint8 index = 0;
                 index <
                    ACTIVE_INVASION_ZONE_COUNT;
                 ++index)
            {
                ZoneWorldState const&
                    selectedZone =
                        ScourgeInvasionZones[
                            zoneIndexes[index]];

                _selectedZoneIds.insert(
                    selectedZone.ZoneId);

                SetZoneUnderAttack(
                    selectedZone.ZoneId,
                    true);

                selectedZoneNames[index] =
                    selectedZone.ZoneName;
            }

            _eventWasActive = true;

            SaveRuntimeEventState();
            BroadcastWorldStates();
            SetDalaranPortalPhase(true);

            if (selectedZoneNames[0] &&
                selectedZoneNames[1])
            {
                std::string message =
                    "[스컬지 침공] 침공이 시작되었습니다. "
                    "침공 지역: ";

                message +=
                    selectedZoneNames[0];

                message += ", ";

                message +=
                    selectedZoneNames[1];

                sWorld->SendServerMessage(
                    SERVER_MSG_STRING,
                    message.c_str());
            }
        }

        void StopEventCycle()
        {
            for (ZoneWorldState const& zoneState :
                ScourgeInvasionZones)
            {
                SetZoneUnderAttack(
                    zoneState.ZoneId,
                    false);
            }

            _combatParticipationSeconds.clear();
            _selectedZoneIds.clear();
            _zoneNextInvasionTime.clear();
            _eventWasActive = false;

            SaveRuntimeEventState();
            BroadcastWorldStates();
            SetDalaranPortalPhase(false);
        }

        void SaveRuntimeEventState()
        {
            /*
             * next_invasion_time 컬럼은 기존 DB 호환을 위해
             * 유지하지만 지역별 사이클에서는 사용하지 않는다.
             */
            WorldDatabase.PExecute(
                "INSERT INTO "
                "`custom_scourge_invasion_runtime` "
                "(`id`, `event_active`, "
                "`next_invasion_time`) "
                "VALUES (1, {}, 0) "
                "ON DUPLICATE KEY UPDATE "
                "`event_active` = "
                "VALUES(`event_active`), "
                "`next_invasion_time` = 0",
                _eventWasActive ? 1 : 0);
        }

        void BroadcastWorldStates() const
        {
            for (auto const& sessionPair :
                sWorld->GetAllSessions())
            {
                WorldSession* session =
                    sessionPair.second;

                if (!session)
                    continue;

                Player* player =
                    session->GetPlayer();

                if (!player)
                    continue;

                SendWorldStates(player);
            }
        }

        void UpdateIdleHudForAllPlayers() const
        {
            for (auto const& sessionPair :
                sWorld->GetAllSessions())
            {
                WorldSession* session =
                    sessionPair.second;

                if (!session ||
                    !session->GetPlayer())
                {
                    continue;
                }

                Player* player =
                    session->GetPlayer();

                if (IsZoneIdleState(
                        player->GetZoneId()))
                {
                    SendIdleHud(player);
                }
                else
                {
                    ClearIdleHud(player);
                }
            }
        }

        bool HasAnyActiveSelectedZone() const
        {
            for (uint32 const zoneId :
                _selectedZoneIds)
            {
                if (IsZoneUnderAttack(zoneId))
                    return true;
            }

            return false;
        }

        char const* GetZoneName(
            uint32 zoneId) const
        {
            for (ZoneWorldState const& zoneState :
                ScourgeInvasionZones)
            {
                if (zoneState.ZoneId ==
                    zoneId)
                {
                    return zoneState.ZoneName;
                }
            }

            return nullptr;
        }

        bool IsZoneUnderAttack(
            uint32 zoneId) const
        {
            auto const itr =
                _zoneStates.find(zoneId);

            return itr !=
                    _zoneStates.end() &&
                itr->second;
        }

        uint32 GetShardHealthPercent(
            ShardProgress const& progress) const
        {
            if (progress.Stage ==
                STAGE_ACTIVE)
            {
                uint32 const killCount =
                    std::min<uint32>(
                        KILLS_TO_DAMAGE_SHARD,
                        progress.KillCount);

                if (killCount >=
                    KILLS_TO_DAMAGE_SHARD)
                {
                    return 0;
                }

                return
                    (KILLS_TO_DAMAGE_SHARD -
                        killCount) *
                    100 /
                    KILLS_TO_DAMAGE_SHARD;
            }

            if (progress.Stage !=
                    STAGE_DAMAGED ||
                !progress.CooldownUntil)
            {
                return 0;
            }

            time_t const repairStartedAt =
                progress.CooldownUntil -
                ENGINEER_REPAIR_WINDOW_SECONDS;

            time_t const now =
                std::time(nullptr);

            if (now <= repairStartedAt)
                return 0;

            if (now >=
                progress.CooldownUntil)
            {
                return 100;
            }

            return uint32(
                (uint64(
                    now - repairStartedAt) *
                    100) /
                ENGINEER_REPAIR_WINDOW_SECONDS);
        }

        void SendInvasionHud(
            Player* player,
            Creature* sourceShard,
            ShardProgress const& progress) const
        {
            if (!player ||
                !sourceShard)
            {
                return;
            }

            ClearIdleHud(player);

            uint32 const playerGuid =
                player->GetGUID()
                    .GetCounter();

            uint32 const killCount =
                std::min<uint32>(
                    KILLS_TO_DAMAGE_SHARD,
                    progress.KillCount);

            player->SendUpdateWorldState(
                WORLD_STATE_SCOURGE_INVASION_HUD_KILL_COUNT,
                killCount);

            player->SendUpdateWorldState(
                WORLD_STATE_SCOURGE_INVASION_HUD_SHARD_HEALTH,
                GetShardHealthPercent(progress));

            _hudSourceByPlayer[
                playerGuid] =
                    sourceShard->GetGUID();

            if (_hudVisiblePlayers.insert(
                    playerGuid).second)
            {
                player->SendUpdateWorldState(
                    WORLD_STATE_SCOURGE_INVASION_HUD_SHOW,
                    1);
            }
        }

        void SendIdleHud(
            Player* player) const
        {
            if (!player ||
                !IsZoneIdleState(
                    player->GetZoneId()))
            {
                return;
            }

            ClearInvasionHud(player);

            uint32 const remainingSeconds =
                GetZoneNextInvasionRemainingSeconds(
                    player->GetZoneId());

            uint32 const minutes =
                remainingSeconds / 60;

            uint32 const seconds =
                remainingSeconds % 60;

            uint32 const playerGuid =
                player->GetGUID()
                    .GetCounter();

            player->SendUpdateWorldState(
                WORLD_STATE_SCOURGE_INVASION_IDLE_MINUTES,
                minutes);

            player->SendUpdateWorldState(
                WORLD_STATE_SCOURGE_INVASION_IDLE_SECONDS,
                seconds);

            if (_idleHudVisiblePlayers.insert(
                    playerGuid).second)
            {
                player->SendUpdateWorldState(
                    WORLD_STATE_SCOURGE_INVASION_IDLE_SHOW,
                    1);
            }
        }

        void ClearIdleHud(
            Player* player) const
        {
            if (!player)
                return;

            uint32 const playerGuid =
                player->GetGUID()
                    .GetCounter();

            if (_idleHudVisiblePlayers.erase(
                    playerGuid))
            {
                player->SendUpdateWorldState(
                    WORLD_STATE_SCOURGE_INVASION_IDLE_SHOW,
                    0);
            }
        }

        void ClearInvasionHudForSource(
            Player* player,
            ObjectGuid const& sourceGuid) const
        {
            if (!player)
                return;

            auto const sourceItr =
                _hudSourceByPlayer.find(
                    player->GetGUID()
                        .GetCounter());

            if (sourceItr !=
                    _hudSourceByPlayer.end() &&
                sourceItr->second ==
                    sourceGuid)
            {
                ClearInvasionHud(player);
            }
        }

        uint32 GetWorldStateForZone(
            uint32 zoneId) const
        {
            for (ZoneWorldState const& zoneState :
                ScourgeInvasionZones)
            {
                if (zoneState.ZoneId ==
                    zoneId)
                {
                    return
                        zoneState.WorldStateId;
                }
            }

            return 0;
        }

        bool _initialized = false;
        bool _eventWasActive = false;
        uint8 _eventId = 0;
        uint32 _eventCycle = 0;
        uint32 _syncTimer = 0;
        uint32 _idleHudUpdateTimer = 0;

        std::unordered_map<
            uint32,
            bool>
            _zoneStates;

        std::unordered_set<uint32>
            _selectedZoneIds;

        std::unordered_map<
            uint32,
            time_t>
            _zoneNextInvasionTime;

        std::unordered_map<
            uint32,
            std::unordered_map<
                uint32,
                uint32>>
            _combatParticipationSeconds;

        mutable std::unordered_set<uint32>
            _hudVisiblePlayers;

        mutable std::unordered_map<
            uint32,
            ObjectGuid>
            _hudSourceByPlayer;

        mutable std::unordered_set<uint32>
            _idleHudVisiblePlayers;
    };

#define sScourgeInvasion ScourgeInvasionMgr::Instance()

    class scourge_invasion_world final :
        public WorldScript
    {
    public:
        scourge_invasion_world()
            : WorldScript(
                "scourge_invasion_world")
        {
        }

        void OnStartup() override
        {
            sScourgeInvasion.Initialize();
        }

        void OnUpdate(
            uint32 diff) override
        {
            sScourgeInvasion.Update(diff);
        }
    };

    class scourge_invasion_player final :
        public PlayerScript
    {
    public:
        scourge_invasion_player()
            : PlayerScript(
                "scourge_invasion_player")
        {
        }

        void OnCreatureKill(
            Player* killer,
            Creature* killed) override
        {
            if (!killer ||
                !killed ||
                !sScourgeInvasion
                    .IsEventActive())
            {
                return;
            }

            if (!IsScourgeInvasionMinion(
                    killed->GetEntry()))
            {
                return;
            }

            Creature* shard =
                killed->FindNearestCreature(
                    NPC_NECROTIC_SHARD,
                    SHARD_KILL_RADIUS,
                    true);

            if (!shard ||
                !shard->IsVisible())
            {
                return;
            }

            if (sScourgeInvasion.IsZoneIdleState(
                    shard->GetZoneId()))
            {
                return;
            }

            shard->AI()->SetData(
                DATA_INVADER_KILLED,
                1);
        }

        void OnLogin(
            Player* player,
            bool /*firstLogin*/) override
        {
            sScourgeInvasion
                .SendWorldStates(player);
        }

        void OnUpdateZone(
            Player* player,
            uint32 /*newZone*/,
            uint32 /*newArea*/) override
        {
            sScourgeInvasion
                .SendWorldStates(player);
        }

        void OnMapChanged(
            Player* player) override
        {
            sScourgeInvasion
                .SendWorldStates(player);
        }

        void OnLogout(
            Player* player) override
        {
            sScourgeInvasion
                .ForgetInvasionHud(player);
        }
    };

    class go_scourge_invasion_portal final :
        public GameObjectScript
    {
    public:
        go_scourge_invasion_portal()
            : GameObjectScript(
                "go_scourge_invasion_portal")
        {
        }

        struct go_scourge_invasion_portalAI final :
            public GameObjectAI
        {
            explicit
            go_scourge_invasion_portalAI(
                GameObject* gameObject)
                : GameObjectAI(gameObject)
            {
            }

            void UpdateAI(
                uint32 diff) override
            {
                if (_checkTimer > diff)
                {
                    _checkTimer -= diff;
                    return;
                }

                _checkTimer =
                    INVASION_PORTAL_CHECK_INTERVAL;

                if (!sScourgeInvasion
                        .IsEventActive())
                {
                    return;
                }

                TeamId portalTeam =
                    TEAM_ALLIANCE;

                uint8 portalSlot = 0;

                if (!GetScourgeInvasionPortalInfo(
                        me->GetEntry(),
                        portalTeam,
                        portalSlot))
                {
                    return;
                }

                ScourgeInvasionPortalDestination
                    destination;

                if (!sScourgeInvasion
                        .GetPortalDestination(
                            portalSlot,
                            portalTeam,
                            destination))
                {
                    return;
                }

                Map* map =
                    me->FindMap();

                if (!map)
                    return;

                Map::PlayerList const& players =
                    map->GetPlayers();

                for (Map::PlayerList::const_iterator itr =
                        players.begin();
                     itr != players.end();
                     ++itr)
                {
                    Player* player =
                        itr->GetSource();

                    if (!player ||
                        player->GetTeamId() !=
                            portalTeam ||
                        player->IsInCombat() ||
                        player->IsBeingTeleported())
                    {
                        continue;
                    }

                    if (!player
                            ->IsWithinDistInMap(
                                me,
                                INVASION_PORTAL_TRIGGER_RADIUS))
                    {
                        continue;
                    }

                    player->TeleportTo(
                        destination.MapId,
                        destination.PositionX,
                        destination.PositionY,
                        destination.PositionZ,
                        destination.Orientation);

                    return;
                }
            }

        private:
            uint32 _checkTimer = 0;
        };

        GameObjectAI* GetAI(
            GameObject* gameObject) const override
        {
            return new
                go_scourge_invasion_portalAI(
                    gameObject);
        }
    };

    class scourge_invasion_map final :
        public WorldMapScript
    {
    public:
        scourge_invasion_map(
            char const* scriptName,
            uint32 mapId)
            : WorldMapScript(
                scriptName,
                mapId)
        {
        }

        void OnUpdate(
            Map* map,
            uint32 diff) override
        {
            if (_hudUpdateTimer > diff)
            {
                _hudUpdateTimer -= diff;
                return;
            }

            _hudUpdateTimer =
                IN_MILLISECONDS;

            sScourgeInvasion
                .UpdateMapHud(map);
        }

    private:
        uint32 _hudUpdateTimer = 0;
    };

    class npc_scourge_invasion_shard final :
        public CreatureScript
    {
    public:
        npc_scourge_invasion_shard()
            : CreatureScript(
                "npc_scourge_invasion_shard")
        {
        }

        struct npc_scourge_invasion_shardAI final :
            public ScriptedAI
        {
            explicit npc_scourge_invasion_shardAI(
                Creature* creature)
                : ScriptedAI(creature)
            {
            }

            void Reset() override
            {
                SnapToGround(me);

                _shardGuid =
                    me->GetGUID()
                        .GetCounter();

                _progress =
                    sScourgeInvasion
                        .LoadShardProgress(
                            _shardGuid);

                _lastSavedKillCount =
                    _progress.KillCount;

                _updateTimer =
                    IN_MILLISECONDS;

                _lastRepairUpdate = 0;
                _lastRepairProgressSave = 0;
                _repairDelayRemainder = 0;
                _repairProgressDirty = false;

                _damagedShardGuid =
                    ObjectGuid::Empty;

                _summonCircleGuid =
                    ObjectGuid::Empty;

                _necropolisGuid =
                    ObjectGuid::Empty;

                _eventCycle =
                    sScourgeInvasion
                        .GetEventCycle();

                FindExistingVisuals();
                ApplyProgressState();

                sScourgeInvasion
                    .UpdateShardHud(
                        me,
                        _progress);
            }

            void SetData(
                uint32 type,
                uint32 data) override
            {
                if (type ==
                    DATA_INVADER_KILLED)
                {
                    if (!sScourgeInvasion
                            .IsEventActive() ||
                        !sScourgeInvasion
                            .IsZoneSelected(
                                me->GetZoneId()) ||
                        sScourgeInvasion
                            .IsZoneIdleState(
                                me->GetZoneId()) ||
                        _progress.Stage !=
                            STAGE_ACTIVE ||
                        !data)
                    {
                        return;
                    }

                    uint16 const
                        previousKillCount =
                            _progress.KillCount;

                    uint32 const
                        updatedKillCount =
                            std::min<uint32>(
                                KILLS_TO_DAMAGE_SHARD,
                                uint32(
                                    _progress
                                        .KillCount) +
                                    data);

                    _progress.KillCount =
                        uint16(
                            updatedKillCount);

                    SpawnIntermediateBosses(
                        previousKillCount,
                        _progress.KillCount);

                    if (_progress.KillCount >=
                        KILLS_TO_DAMAGE_SHARD)
                    {
                        _progress.Stage =
                            STAGE_DAMAGED;

                        _progress.BossCount = 0;
                        _progress.CooldownUntil = 0;

                        ResetRepairTracking();
                        SaveProgress();
                        HideShard();
                        EnsureSiteVisuals();
                        EnsureDamagedShard();
                    }
                    else if (
                        _progress.KillCount >=
                        _lastSavedKillCount +
                            SAVE_EVERY_KILLS)
                    {
                        SaveProgress();
                    }

                    sScourgeInvasion
                        .UpdateShardHud(
                            me,
                            _progress);
                }
                else if (
                    type ==
                    DATA_SITE_COMPLETED)
                {
                    _progress =
                        sScourgeInvasion
                            .LoadShardProgress(
                                _shardGuid);

                    _lastSavedKillCount =
                        _progress.KillCount;

                    ResetRepairTracking();
                    HideShard();
                    RemoveSiteVisuals();

                    sScourgeInvasion
                        .UpdateShardHud(
                            me,
                            _progress);
                }
            }

            void UpdateAI(
                uint32 diff) override
            {
                if (_updateTimer > diff)
                {
                    _updateTimer -= diff;
                    return;
                }

                _updateTimer =
                    IN_MILLISECONDS;

                uint32 const
                    currentEventCycle =
                        sScourgeInvasion
                            .GetEventCycle();

                if (_eventCycle !=
                    currentEventCycle)
                {
                    _eventCycle =
                        currentEventCycle;

                    DeactivateDamagedShard();
                    RemoveSiteVisuals();

                    _progress =
                        ShardProgress();

                    _lastSavedKillCount = 0;

                    ResetRepairTracking();
                }

                if (!sScourgeInvasion
                        .IsEventActive() ||
                    !sScourgeInvasion
                        .IsZoneSelected(
                            me->GetZoneId()))
                {
                    DeactivateSite();

                    sScourgeInvasion
                        .UpdateShardHud(
                            me,
                            _progress);

                    return;
                }

                if (_progress.Stage ==
                    STAGE_COMPLETE)
                {
                    DeactivateSite();

                    time_t const now =
                        std::time(nullptr);

                    /*
                     * 이전 전역 버전에서 완료된
                     * cooldown_until = NULL 데이터 호환.
                     */
                    if (!_progress.CooldownUntil)
                    {
                        _progress.CooldownUntil =
                            now +
                            time_t(
                                INVASION_IDLE_SECONDS);

                        SaveProgress();

                        sScourgeInvasion
                            .UpdateZoneAfterShardCompletion(
                                me->GetZoneId(),
                                _progress.CooldownUntil);
                    }

                    if (now <
                        _progress.CooldownUntil)
                    {
                        sScourgeInvasion
                            .UpdateShardHud(
                                me,
                                _progress);

                        return;
                    }

                    _progress =
                        ShardProgress();

                    _lastSavedKillCount = 0;

                    ResetRepairTracking();
                    SaveProgress();

                    sScourgeInvasion
                        .RestartZoneAfterIdle(
                            me->GetZoneId());
                }

                if (sScourgeInvasion
                        .IsZoneIdleState(
                            me->GetZoneId()))
                {
                    DeactivateSite();

                    sScourgeInvasion
                        .UpdateShardHud(
                            me,
                            _progress);

                    return;
                }

                if (_progress.Stage ==
                    STAGE_ACTIVE)
                {
                    DeactivateDamagedShard();
                    ShowShard();
                    EnsureSiteVisuals();
                }
                else if (
                    _progress.Stage ==
                    STAGE_DAMAGED)
                {
                    HideShard();
                    EnsureSiteVisuals();
                    EnsureDamagedShard();

                    time_t const now =
                        std::time(nullptr);

                    UpdateRepairProgress(
                        now,
                        GetRepairingEngineerCount());

                    SaveRepairProgressIfNeeded(
                        now);

                    if (_progress.CooldownUntil &&
                        now >=
                            _progress.CooldownUntil)
                    {
                        DeactivateDamagedShard();

                        _progress =
                            ShardProgress();

                        _lastSavedKillCount = 0;

                        ResetRepairTracking();
                        SaveProgress();
                        ShowShard();
                        EnsureSiteVisuals();
                    }
                }

                sScourgeInvasion
                    .UpdateCombatParticipation(
                        me);

                sScourgeInvasion
                    .UpdateShardHud(
                        me,
                        _progress);
            }

        private:
            void SpawnIntermediateBosses(
                uint16 previousKillCount,
                uint16 currentKillCount)
            {
                for (IntermediateBossSpawn const&
                    bossSpawn :
                    IntermediateBossSpawns)
                {
                    if (previousKillCount >=
                            bossSpawn.RequiredKills ||
                        currentKillCount <
                            bossSpawn.RequiredKills)
                    {
                        continue;
                    }

                    Position const position =
                        me->GetNearPosition(
                            frand(
                                12.0f,
                                20.0f),
                            frand(
                                0.0f,
                                TWO_PI));

                    if (Creature* boss =
                            me->SummonCreature(
                                bossSpawn.Entry,
                                position,
                                TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT,
                                Milliseconds(
                                    15 *
                                    60 *
                                    IN_MILLISECONDS)))
                    {
                        boss->AI()->SetGUID(
                            me->GetGUID(),
                            DATA_INTERMEDIATE_SOURCE_SHARD_GUID);

                        if (Player* player =
                                me->SelectNearestPlayer(
                                    SPAWNER_ACTIVE_RADIUS))
                        {
                            boss->AI()
                                ->AttackStart(
                                    player);
                        }
                    }
                }
            }

            void ApplyProgressState()
            {
                if (!sScourgeInvasion
                        .IsEventActive() ||
                    !sScourgeInvasion
                        .IsZoneSelected(
                            me->GetZoneId()))
                {
                    DeactivateSite();
                    return;
                }

                if (_progress.Stage ==
                    STAGE_ACTIVE)
                {
                    DeactivateDamagedShard();
                    ShowShard();
                    EnsureSiteVisuals();
                    return;
                }

                if (_progress.Stage ==
                    STAGE_DAMAGED)
                {
                    HideShard();
                    EnsureSiteVisuals();
                    EnsureDamagedShard();
                    return;
                }

                DeactivateSite();
            }

            void SaveProgress()
            {
                _lastSavedKillCount =
                    _progress.KillCount;

                sScourgeInvasion
                    .SaveShardProgress(
                        _shardGuid,
                        _progress);
            }

            void ResetRepairTracking()
            {
                _lastRepairUpdate = 0;
                _lastRepairProgressSave = 0;
                _repairDelayRemainder = 0;
                _repairProgressDirty = false;
            }

            uint8
            GetRepairingEngineerCount() const
            {
                if (_damagedShardGuid.IsEmpty())
                    return 0;

                Creature* damagedShard =
                    ObjectAccessor::GetCreature(
                        *me,
                        _damagedShardGuid);

                if (!damagedShard)
                    return 0;

                return uint8(
                    std::min<uint32>(
                        ENGINEER_COUNT,
                        damagedShard->AI()
                            ->GetData(
                                DATA_ENGINEER_REPAIR_CHANNEL)));
            }

            void SaveRepairProgressIfNeeded(
                time_t now)
            {
                if (!_repairProgressDirty)
                    return;

                if (!_lastRepairProgressSave ||
                    now >=
                        _lastRepairProgressSave +
                        time_t(
                            REPAIR_PROGRESS_SAVE_INTERVAL_SECONDS))
                {
                    sScourgeInvasion
                        .SaveShardRepairDeadline(
                            _shardGuid,
                            _progress.CooldownUntil);

                    _lastRepairProgressSave =
                        now;

                    _repairProgressDirty =
                        false;
                }
            }

            void UpdateRepairProgress(
                time_t now,
                uint8 repairingEngineers)
            {
                repairingEngineers =
                    uint8(
                        std::min<uint32>(
                            ENGINEER_COUNT,
                            repairingEngineers));

                if (!_progress.CooldownUntil)
                {
                    if (!repairingEngineers)
                        return;

                    _progress.CooldownUntil =
                        now +
                        time_t(
                            ENGINEER_REPAIR_WINDOW_SECONDS);

                    _lastRepairUpdate = now;
                    _lastRepairProgressSave = now;
                    _repairDelayRemainder = 0;
                    _repairProgressDirty = false;

                    SaveProgress();
                    return;
                }

                if (!_lastRepairUpdate)
                {
                    _lastRepairUpdate = now;
                    return;
                }

                if (now <= _lastRepairUpdate)
                    return;

                uint32 const elapsedSeconds =
                    uint32(
                        now -
                        _lastRepairUpdate);

                _lastRepairUpdate = now;

                uint32 const repairRateUnits =
                    repairingEngineers
                        ? REPAIR_RATE_UNITS_PER_SECOND /
                            (ENGINEER_COUNT -
                             repairingEngineers +
                             1)
                        : 0;

                uint32 const
                    repairDelayUnitsPerSecond =
                        REPAIR_RATE_UNITS_PER_SECOND -
                        repairRateUnits;

                if (!repairDelayUnitsPerSecond)
                    return;

                uint64 const repairDelayUnits =
                    uint64(
                        _repairDelayRemainder) +
                    uint64(elapsedSeconds) *
                        repairDelayUnitsPerSecond;

                uint32 const
                    additionalDelaySeconds =
                        uint32(
                            repairDelayUnits /
                            REPAIR_RATE_UNITS_PER_SECOND);

                _repairDelayRemainder =
                    uint8(
                        repairDelayUnits %
                        REPAIR_RATE_UNITS_PER_SECOND);

                if (additionalDelaySeconds)
                {
                    _progress.CooldownUntil +=
                        additionalDelaySeconds;

                    _repairProgressDirty =
                        true;
                }
            }

            void FindExistingVisuals()
            {
                if (GameObject* circle =
                        me->FindNearestGameObject(
                            GO_SUMMON_CIRCLE,
                            8.0f))
                {
                    _summonCircleGuid =
                        circle->GetGUID();
                }

                if (GameObject* necropolis =
                        me->FindNearestGameObject(
                            GO_NECROPOLIS,
                            120.0f))
                {
                    _necropolisGuid =
                        necropolis->GetGUID();
                }

                if (Creature* damagedShard =
                        me->FindNearestCreature(
                            NPC_DAMAGED_NECROTIC_SHARD,
                            3.0f,
                            true))
                {
                    _damagedShardGuid =
                        damagedShard->GetGUID();
                }
            }

            void ShowShard()
            {
                me->SetVisible(true);
                me->SetReactState(
                    REACT_PASSIVE);

                me->SetFlag(
                    UNIT_FIELD_FLAGS,
                    UNIT_FLAG_NON_ATTACKABLE |
                    UNIT_FLAG_UNINTERACTIBLE);
            }

            void HideShard()
            {
                me->SetVisible(false);
            }

            void EnsureSiteVisuals()
            {
                if (_summonCircleGuid.IsEmpty() ||
                    !ObjectAccessor::GetGameObject(
                        *me,
                        _summonCircleGuid))
                {
                    if (GameObject* circle =
                            me->SummonGameObject(
                                GO_SUMMON_CIRCLE,
                                me->GetPosition(),
                                QuaternionData(),
                                Seconds(0)))
                    {
                        _summonCircleGuid =
                            circle->GetGUID();
                    }
                }

                if (_necropolisGuid.IsEmpty() ||
                    !ObjectAccessor::GetGameObject(
                        *me,
                        _necropolisGuid))
                {
                    Position const
                        necropolisPosition(
                            me->GetPositionX(),
                            me->GetPositionY(),
                            me->GetPositionZ() +
                                75.0f,
                            me->GetOrientation());

                    if (GameObject* necropolis =
                            me->SummonGameObject(
                                GO_NECROPOLIS,
                                necropolisPosition,
                                QuaternionData(),
                                Seconds(0)))
                    {
                        _necropolisGuid =
                            necropolis->GetGUID();
                    }
                }
            }

            void RemoveSiteVisuals()
            {
                if (!_summonCircleGuid.IsEmpty())
                {
                    if (GameObject* circle =
                            ObjectAccessor::GetGameObject(
                                *me,
                                _summonCircleGuid))
                    {
                        circle->DespawnOrUnsummon();
                    }
                }

                if (!_necropolisGuid.IsEmpty())
                {
                    if (GameObject* necropolis =
                            ObjectAccessor::GetGameObject(
                                *me,
                                _necropolisGuid))
                    {
                        necropolis
                            ->DespawnOrUnsummon();
                    }
                }

                _summonCircleGuid =
                    ObjectGuid::Empty;

                _necropolisGuid =
                    ObjectGuid::Empty;
            }

            void DeactivateDamagedShard()
            {
                if (_damagedShardGuid.IsEmpty())
                    return;

                if (Creature* damagedShard =
                        ObjectAccessor::GetCreature(
                            *me,
                            _damagedShardGuid))
                {
                    damagedShard->AI()->SetGUID(
                        ObjectGuid::Empty,
                        DATA_SOURCE_SHARD_GUID);
                }

                _damagedShardGuid =
                    ObjectGuid::Empty;
            }

            void DeactivateSite()
            {
                DeactivateDamagedShard();
                RemoveSiteVisuals();
                HideShard();
            }

            void EnsureDamagedShard()
            {
                if (!_damagedShardGuid.IsEmpty())
                {
                    if (ObjectAccessor::GetCreature(
                            *me,
                            _damagedShardGuid))
                    {
                        return;
                    }

                    _damagedShardGuid =
                        ObjectGuid::Empty;
                }

                if (Creature* damagedShard =
                        me->FindNearestCreature(
                            NPC_DAMAGED_NECROTIC_SHARD,
                            2.0f,
                            true))
                {
                    _damagedShardGuid =
                        damagedShard->GetGUID();

                    damagedShard->AI()->SetGUID(
                        me->GetGUID(),
                        DATA_SOURCE_SHARD_GUID);

                    return;
                }

                if (Creature* damagedShard =
                        me->SummonCreature(
                            NPC_DAMAGED_NECROTIC_SHARD,
                            me->GetPosition(),
                            TEMPSUMMON_MANUAL_DESPAWN))
                {
                    _damagedShardGuid =
                        damagedShard->GetGUID();

                    damagedShard->AI()->SetGUID(
                        me->GetGUID(),
                        DATA_SOURCE_SHARD_GUID);
                }
            }

            uint32 _shardGuid = 0;
            uint16 _lastSavedKillCount = 0;
            uint32 _updateTimer = 0;
            uint32 _eventCycle = 0;

            time_t _lastRepairUpdate = 0;
            time_t _lastRepairProgressSave = 0;

            uint8 _repairDelayRemainder = 0;
            bool _repairProgressDirty = false;

            ObjectGuid _damagedShardGuid;
            ObjectGuid _summonCircleGuid;
            ObjectGuid _necropolisGuid;

            ShardProgress _progress;
        };

        CreatureAI* GetAI(
            Creature* creature) const override
        {
            return new
                npc_scourge_invasion_shardAI(
                    creature);
        }
    };

    class npc_scourge_invasion_damaged_shard final :
        public CreatureScript
    {
    public:
        npc_scourge_invasion_damaged_shard()
            : CreatureScript(
                "npc_scourge_invasion_damaged_shard")
        {
        }

        struct npc_scourge_invasion_damaged_shardAI final :
            public ScriptedAI
        {
            explicit
            npc_scourge_invasion_damaged_shardAI(
                Creature* creature)
                : ScriptedAI(creature),
                  _engineerSummons(me),
                  _shadowSummons(me)
            {
            }

            void Reset() override
            {
                _sourceShardGuid =
                    ObjectGuid::Empty;

                _engineersSpawned = false;
                _repairingEngineers = 0;

                _startupTimer =
                    IN_MILLISECONDS;

                _engineerSummons.DespawnAll();
                _shadowSummons.DespawnAll();

                me->SetReactState(
                    REACT_PASSIVE);

                me->SetFlag(
                    UNIT_FIELD_FLAGS,
                    UNIT_FLAG_NON_ATTACKABLE |
                    UNIT_FLAG_UNINTERACTIBLE);

                me->SetVisible(false);
            }

            void SetGUID(
                ObjectGuid const& guid,
                int32 type) override
            {
                if (type ==
                    DATA_SHADOW_SUMMONED)
                {
                    if (Creature* shadow =
                            ObjectAccessor::GetCreature(
                                *me,
                                guid))
                    {
                        _shadowSummons.Summon(
                            shadow);
                    }

                    return;
                }

                if (type !=
                    DATA_SOURCE_SHARD_GUID)
                {
                    return;
                }

                if (guid.IsEmpty())
                {
                    Deactivate();
                    return;
                }

                _sourceShardGuid = guid;
                _engineersSpawned = false;
                _repairingEngineers = 0;

                _startupTimer =
                    IN_MILLISECONDS;

                me->SetVisible(true);
            }

            void JustSummoned(
                Creature* summon) override
            {
                if (summon->GetEntry() ==
                    NPC_CULTIST_ENGINEER)
                {
                    _engineerSummons.Summon(
                        summon);
                }
            }

            uint32 GetData(
                uint32 type) const override
            {
                if (type ==
                    DATA_ENGINEER_REPAIR_CHANNEL)
                {
                    return
                        _repairingEngineers;
                }

                return 0;
            }

            void SetData(
                uint32 type,
                uint32 data) override
            {
                if (type ==
                    DATA_ENGINEER_REPAIR_CHANNEL)
                {
                    if (data)
                    {
                        _repairingEngineers =
                            uint8(
                                std::min<uint32>(
                                    ENGINEER_COUNT,
                                    uint32(
                                        _repairingEngineers) +
                                        1));
                    }
                    else if (
                        _repairingEngineers)
                    {
                        --_repairingEngineers;
                    }

                    return;
                }

                if (type !=
                        DATA_BOSS_DEFEATED ||
                    !data ||
                    _sourceShardGuid.IsEmpty())
                {
                    return;
                }

                uint32 const
                    sourceShardGuid =
                        _sourceShardGuid
                            .GetCounter();

                ShardProgress progress =
                    sScourgeInvasion
                        .LoadShardProgress(
                            sourceShardGuid);

                if (progress.Stage !=
                    STAGE_DAMAGED)
                {
                    return;
                }

                progress.BossCount =
                    uint8(
                        std::min<uint32>(
                            ENGINEER_COUNT,
                            uint32(
                                progress.BossCount) +
                                data));

                sScourgeInvasion
                    .SaveShardProgress(
                        sourceShardGuid,
                        progress);

                if (progress.BossCount >=
                    ENGINEER_COUNT)
                {
                    CompleteSite(progress);
                }
            }

            void UpdateAI(
                uint32 diff) override
            {
                if (_startupTimer > diff)
                {
                    _startupTimer -= diff;
                    return;
                }

                _startupTimer =
                    IN_MILLISECONDS;

                if (!sScourgeInvasion
                        .IsEventActive() ||
                    _sourceShardGuid.IsEmpty())
                {
                    Deactivate();
                    return;
                }

                Creature* sourceShard =
                    ObjectAccessor::GetCreature(
                        *me,
                        _sourceShardGuid);

                if (!sourceShard ||
                    !sScourgeInvasion
                        .IsZoneSelected(
                            sourceShard->GetZoneId()) ||
                    sScourgeInvasion
                        .IsZoneIdleState(
                            sourceShard->GetZoneId()))
                {
                    Deactivate();
                    return;
                }

                ShardProgress const progress =
                    sScourgeInvasion
                        .LoadShardProgress(
                            _sourceShardGuid
                                .GetCounter());

                if (progress.Stage !=
                    STAGE_DAMAGED)
                {
                    Deactivate();
                    return;
                }

                if (!_engineersSpawned)
                {
                    SpawnRemainingEngineers(
                        progress.BossCount);
                }
            }

        private:
            void SpawnRemainingEngineers(
                uint8 defeatedBosses)
            {
                uint8 const engineerCount =
                    defeatedBosses >=
                            ENGINEER_COUNT
                        ? 0
                        : ENGINEER_COUNT -
                            defeatedBosses;

                for (uint8 index = 0;
                     index < engineerCount;
                     ++index)
                {
                    float const angle =
                        TWO_PI *
                        float(index) /
                        float(ENGINEER_COUNT);

                    Position const position =
                        me->GetNearPosition(
                            6.0f,
                            angle);

                    me->SummonCreature(
                        NPC_CULTIST_ENGINEER,
                        position,
                        TEMPSUMMON_MANUAL_DESPAWN);
                }

                _engineersSpawned = true;
            }

            void CompleteSite(
                ShardProgress& progress)
            {
                progress.Stage =
                    STAGE_COMPLETE;

                progress.KillCount =
                    KILLS_TO_DAMAGE_SHARD;

                progress.BossCount =
                    ENGINEER_COUNT;

                /*
                 * 해당 지역만 독립적으로
                 * 현재 시각부터 30분 정비에 들어간다.
                 */
                progress.CooldownUntil =
                    std::time(nullptr) +
                    time_t(
                        INVASION_IDLE_SECONDS);

                sScourgeInvasion
                    .SaveShardProgress(
                        _sourceShardGuid
                            .GetCounter(),
                        progress);

                if (Creature* sourceShard =
                        ObjectAccessor::GetCreature(
                            *me,
                            _sourceShardGuid))
                {
                    sScourgeInvasion
                        .GrantCycleReward(
                            sourceShard);

                    sourceShard->AI()->SetData(
                        DATA_SITE_COMPLETED,
                        1);
                }

                GrantSoulRevival();

                sScourgeInvasion
                    .UpdateZoneAfterShardCompletion(
                        me->GetZoneId(),
                        progress.CooldownUntil);

                Deactivate();
            }

            void Deactivate()
            {
                _sourceShardGuid =
                    ObjectGuid::Empty;

                _engineersSpawned = false;
                _repairingEngineers = 0;

                _engineerSummons.DespawnAll();
                _shadowSummons.DespawnAll();

                me->SetVisible(false);
                me->DespawnOrUnsummon();
            }

            void GrantSoulRevival()
            {
                Map* map =
                    me->FindMap();

                if (!map)
                    return;

                Map::PlayerList const& players =
                    map->GetPlayers();

                for (Map::PlayerList::const_iterator itr =
                        players.begin();
                     itr != players.end();
                     ++itr)
                {
                    Player* player =
                        itr->GetSource();

                    if (player &&
                        player->IsWithinDistInMap(
                            me,
                            100.0f))
                    {
                        me->CastSpell(
                            player,
                            SPELL_SOUL_REVIVAL,
                            true);
                    }
                }
            }

            ObjectGuid _sourceShardGuid;

            SummonList _engineerSummons;
            SummonList _shadowSummons;

            bool _engineersSpawned = false;
            uint8 _repairingEngineers = 0;
            uint32 _startupTimer = 0;
        };

        CreatureAI* GetAI(
            Creature* creature) const override
        {
            return new
                npc_scourge_invasion_damaged_shardAI(
                    creature);
        }
    };

    class npc_scourge_invasion_engineer final :
        public CreatureScript
    {
    public:
        npc_scourge_invasion_engineer()
            : CreatureScript(
                "npc_scourge_invasion_engineer")
        {
        }

        struct npc_scourge_invasion_engineerAI final :
            public ScriptedAI
        {
            explicit
            npc_scourge_invasion_engineerAI(
                Creature* creature)
                : ScriptedAI(creature)
            {
            }

            void Reset() override
            {
                StopRepairChannel();

                _disrupted = false;

                _checkTimer =
                    IN_MILLISECONDS;

                _channelStartTimer =
                    IN_MILLISECONDS;

                _channelStarted = false;

                me->SetReactState(
                    REACT_PASSIVE);
            }

            void OnDespawn() override
            {
                StopRepairChannel();
            }

            void UpdateAI(
                uint32 diff) override
            {
                if (_checkTimer > diff)
                    _checkTimer -= diff;
                else
                {
                    _checkTimer =
                        IN_MILLISECONDS;

                    if (!sScourgeInvasion
                            .IsEventActive() ||
                        !sScourgeInvasion
                            .IsZoneSelected(
                                me->GetZoneId()) ||
                        sScourgeInvasion
                            .IsZoneIdleState(
                                me->GetZoneId()) ||
                        !FindDamagedShard())
                    {
                        StopRepairChannel();
                        me->DespawnOrUnsummon();
                        return;
                    }
                }

                if (_disrupted ||
                    _channelStarted)
                {
                    return;
                }

                if (_channelStartTimer > diff)
                {
                    _channelStartTimer -= diff;
                    return;
                }

                if (Creature* damagedShard =
                        FindDamagedShard())
                {
                    StartRepairChannel(
                        damagedShard);
                }
                else
                {
                    _channelStartTimer =
                        IN_MILLISECONDS;
                }
            }

            bool OnGossipHello(
                Player* player) override
            {
                if (!player ||
                    _disrupted ||
                    !sScourgeInvasion
                        .IsEventActive() ||
                    !sScourgeInvasion
                        .IsZoneSelected(
                            me->GetZoneId()) ||
                    sScourgeInvasion
                        .IsZoneIdleState(
                            me->GetZoneId()) ||
                    !FindDamagedShard())
                {
                    return false;
                }

                ClearGossipMenuFor(player);

                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    "죽음의 룬 8개를 사용해 광신자를 방해한다.",
                    GOSSIP_SENDER_MAIN,
                    GOSSIP_ACTION_INFO_DEF +
                        1);

                SendGossipMenuFor(
                    player,
                    1,
                    me->GetGUID());

                return true;
            }

            bool OnGossipSelect(
                Player* player,
                uint32 /*menuId*/,
                uint32 gossipListId) override
            {
                if (!player)
                    return true;

                uint32 const action =
                    player->PlayerTalkClass
                        ->GetGossipOptionAction(
                            gossipListId);

                ClearGossipMenuFor(player);
                CloseGossipMenuFor(player);

                if (action !=
                        GOSSIP_ACTION_INFO_DEF +
                            1 ||
                    _disrupted ||
                    !sScourgeInvasion
                        .IsEventActive() ||
                    !sScourgeInvasion
                        .IsZoneSelected(
                            me->GetZoneId()) ||
                    sScourgeInvasion
                        .IsZoneIdleState(
                            me->GetZoneId()))
                {
                    return true;
                }

                if (!FindDamagedShard() ||
                    !player->HasItemCount(
                        ITEM_NECROTIC_RUNE,
                        ENGINEER_RUNE_COST))
                {
                    return true;
                }

                player->DestroyItemCount(
                    ITEM_NECROTIC_RUNE,
                    ENGINEER_RUNE_COST,
                    true);

                _disrupted = true;

                StopRepairChannel();

                if (Creature* shadow =
                        me->SummonCreature(
                            NPC_SHADOW_OF_DOOM,
                            me->GetPosition(),
                            TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT,
                            Milliseconds(
                                SHADOW_DESPAWN_DURATION)))
                {
                    if (Creature* damagedShard =
                            FindDamagedShard())
                    {
                        shadow->AI()->SetGUID(
                            damagedShard->GetGUID(),
                            DATA_SHADOW_DAMAGED_SHARD_GUID);

                        damagedShard->AI()->SetGUID(
                            shadow->GetGUID(),
                            DATA_SHADOW_SUMMONED);
                    }
                }

                me->DespawnOrUnsummon();

                return true;
            }

        private:
            void StartRepairChannel(
                Creature* damagedShard)
            {
                if (!damagedShard)
                    return;

                me->CastSpell(
                    me,
                    SPELL_BUTTRESS_CHANNEL,
                    true);

                me->SetTarget(
                    damagedShard->GetGUID());

                me->SetChannelObjectGuid(
                    damagedShard->GetGUID());

                me->SetChannelSpellId(
                    SPELL_REPAIR_BEAM_VISUAL);

                _channelStarted = true;
                _repairing = true;

                damagedShard->AI()->SetData(
                    DATA_ENGINEER_REPAIR_CHANNEL,
                    1);
            }

            void StopRepairChannel()
            {
                if (_repairing)
                {
                    if (Creature* damagedShard =
                            FindDamagedShard())
                    {
                        damagedShard->AI()->SetData(
                            DATA_ENGINEER_REPAIR_CHANNEL,
                            0);
                    }
                }

                _repairing = false;

                me->SetChannelObjectGuid(
                    ObjectGuid::Empty);

                me->SetChannelSpellId(0);
            }

            Creature*
            FindDamagedShard() const
            {
                Creature* damagedShard =
                    me->FindNearestCreature(
                        NPC_DAMAGED_NECROTIC_SHARD,
                        65.0f,
                        true);

                return
                    damagedShard &&
                    damagedShard->IsVisible()
                        ? damagedShard
                        : nullptr;
            }

            bool _disrupted = false;
            uint32 _checkTimer = 0;
            uint32 _channelStartTimer = 0;
            bool _channelStarted = false;
            bool _repairing = false;
        };

        CreatureAI* GetAI(
            Creature* creature) const override
        {
            return new
                npc_scourge_invasion_engineerAI(
                    creature);
        }
    };

    class npc_scourge_invasion_minion_spawner final :
        public CreatureScript
    {
    public:
        npc_scourge_invasion_minion_spawner()
            : CreatureScript(
                "npc_scourge_invasion_minion_spawner")
        {
        }

        struct npc_scourge_invasion_minion_spawnerAI final :
            public ScriptedAI
        {
            explicit
            npc_scourge_invasion_minion_spawnerAI(
                Creature* creature)
                : ScriptedAI(creature),
                  _summons(me)
            {
            }

            void Reset() override
            {
                SnapToGround(me);

                _spawnTimer =
                    urand(
                        3 * IN_MILLISECONDS,
                        6 * IN_MILLISECONDS);

                _summons.DespawnAll();

                me->SetReactState(
                    REACT_PASSIVE);

                me->SetFlag(
                    UNIT_FIELD_FLAGS,
                    UNIT_FLAG_NON_ATTACKABLE |
                    UNIT_FLAG_UNINTERACTIBLE);

                me->SetVisible(false);
            }

            void JustSummoned(
                Creature* summon) override
            {
                _summons.Summon(summon);

                if (Player* player =
                        me->SelectNearestPlayer(
                            45.0f))
                {
                    summon->AI()->AttackStart(
                        player);
                }
            }

            void SummonedCreatureDies(
                Creature* summon,
                Unit* /*killer*/) override
            {
                _summons.Despawn(summon);
            }

            void SummonedCreatureDespawn(
                Creature* summon) override
            {
                _summons.Despawn(summon);
            }

            void UpdateAI(
                uint32 diff) override
            {
                /*
                 * 지역 정비 상태를 직접 검사한다.
                 * 이것이 이번 수정에서 중요한 부분이다.
                 *
                 * 한 지역이 완료되면 다른 지역과 관계없이
                 * 해당 지역 스포너가 즉시 모든 몹을 제거하고
                 * 30분 동안 신규 생성을 완전히 차단한다.
                 */
                if (!sScourgeInvasion
                        .IsEventActive() ||
                    !sScourgeInvasion
                        .IsZoneSelected(
                            me->GetZoneId()) ||
                    sScourgeInvasion
                        .IsZoneIdleState(
                            me->GetZoneId()) ||
                    !FindActiveShard())
                {
                    _summons.DespawnAll();
                    return;
                }

                if (_spawnTimer > diff)
                {
                    _spawnTimer -= diff;
                    return;
                }

                _spawnTimer =
                    MINION_SPAWN_INTERVAL;

                if (!me->SelectNearestPlayer(
                        SPAWNER_ACTIVE_RADIUS) ||
                    _summons.size() >=
                        MAX_MINIONS_PER_SPAWNER)
                {
                    return;
                }

                Position const position =
                    me->GetNearPosition(
                        frand(
                            8.0f,
                            18.0f),
                        frand(
                            0.0f,
                            TWO_PI));

                uint32 const entry =
                    InvasionMinionEntries[
                        urand(
                            0,
                            uint32(
                                InvasionMinionEntries
                                    .size() -
                                1))];

                me->SummonCreature(
                    entry,
                    position,
                    TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT,
                    Milliseconds(
                        MINION_DESPAWN_DURATION));
            }

        private:
            Creature*
            FindActiveShard() const
            {
                if (Creature* damagedShard =
                        me->FindNearestCreature(
                            NPC_DAMAGED_NECROTIC_SHARD,
                            SPAWNER_SHARD_RADIUS,
                            true))
                {
                    if (damagedShard->IsVisible())
                        return damagedShard;
                }

                Creature* shard =
                    me->FindNearestCreature(
                        NPC_NECROTIC_SHARD,
                        SPAWNER_SHARD_RADIUS,
                        true);

                return
                    shard &&
                    shard->IsVisible()
                        ? shard
                        : nullptr;
            }

            SummonList _summons;
            uint32 _spawnTimer = 0;
        };

        CreatureAI* GetAI(
            Creature* creature) const override
        {
            return new
                npc_scourge_invasion_minion_spawnerAI(
                    creature);
        }
    };

    struct scourge_invasion_intermediate_bossAI :
        public ScriptedAI
    {
        explicit
        scourge_invasion_intermediate_bossAI(
            Creature* creature)
            : ScriptedAI(creature)
        {
        }

        void SetGUID(
            ObjectGuid const& guid,
            int32 type) override
        {
            if (type ==
                DATA_INTERMEDIATE_SOURCE_SHARD_GUID)
            {
                _sourceShardGuid = guid;
            }
        }

    protected:
        void ResetInvasionBoss()
        {
            _eventCheckTimer =
                IN_MILLISECONDS;

            me->SetReactState(
                REACT_AGGRESSIVE);
        }

        bool UpdateInvasionBoss(
            uint32 diff)
        {
            if (_eventCheckTimer > diff)
            {
                _eventCheckTimer -= diff;
            }
            else
            {
                _eventCheckTimer =
                    IN_MILLISECONDS;

                if (!sScourgeInvasion
                        .IsEventActive() ||
                    !sScourgeInvasion
                        .IsZoneSelected(
                            me->GetZoneId()) ||
                    sScourgeInvasion
                        .IsZoneIdleState(
                            me->GetZoneId()))
                {
                    me->DespawnOrUnsummon();
                    return false;
                }

                Creature* sourceShard =
                    GetSourceShard();

                if (!sourceShard ||
                    !sourceShard->IsVisible() ||
                    !me->IsWithinDistInMap(
                        sourceShard,
                        160.0f))
                {
                    me->DespawnOrUnsummon();
                    return false;
                }
            }

            if (!UpdateVictim())
            {
                if (Player* player =
                        me->SelectNearestPlayer(
                            60.0f))
                {
                    AttackStart(player);
                }

                return false;
            }

            return true;
        }

        Unit* SelectRandomPlayerTarget(
            float range)
        {
            if (Unit* target =
                    SelectTarget(
                        SelectTargetMethod::Random,
                        0,
                        range,
                        true))
            {
                return target;
            }

            return me->GetVictim();
        }

    private:
        Creature* GetSourceShard()
        {
            if (!_sourceShardGuid.IsEmpty())
            {
                if (Creature* sourceShard =
                        ObjectAccessor::GetCreature(
                            *me,
                            _sourceShardGuid))
                {
                    return sourceShard;
                }
            }

            if (Creature* sourceShard =
                    me->FindNearestCreature(
                        NPC_NECROTIC_SHARD,
                        160.0f,
                        true))
            {
                _sourceShardGuid =
                    sourceShard->GetGUID();

                return sourceShard;
            }

            return nullptr;
        }

        ObjectGuid _sourceShardGuid;
        uint32 _eventCheckTimer = 0;
    };

    class npc_scourge_invasion_lumbering_horror final :
        public CreatureScript
    {
    public:
        npc_scourge_invasion_lumbering_horror()
            : CreatureScript(
                "npc_scourge_invasion_lumbering_horror")
        {
        }

        struct npc_scourge_invasion_lumbering_horrorAI final :
            public scourge_invasion_intermediate_bossAI
        {
            explicit
            npc_scourge_invasion_lumbering_horrorAI(
                Creature* creature)
                : scourge_invasion_intermediate_bossAI(
                    creature)
            {
            }

            void Reset() override
            {
                ResetInvasionBoss();

                _cleaveTimer =
                    urand(
                        4 * IN_MILLISECONDS,
                        7 * IN_MILLISECONDS);

                _knockAwayTimer =
                    urand(
                        10 * IN_MILLISECONDS,
                        14 * IN_MILLISECONDS);

                _enraged = false;

                if (!me->HasAura(
                        SPELL_LUMBERING_DISEASE_CLOUD))
                {
                    DoCast(
                        me,
                        SPELL_LUMBERING_DISEASE_CLOUD,
                        true);
                }
            }

            void UpdateAI(
                uint32 diff) override
            {
                if (!UpdateInvasionBoss(diff))
                    return;

                if (!_enraged &&
                    HealthBelowPct(30))
                {
                    DoCast(
                        me,
                        SPELL_LUMBERING_ENRAGE,
                        true);

                    _enraged = true;
                }

                if (me->HasUnitState(
                        UNIT_STATE_CASTING))
                {
                    return;
                }

                if (_cleaveTimer <= diff)
                {
                    DoCastVictim(
                        SPELL_LUMBERING_CLEAVE);

                    _cleaveTimer =
                        urand(
                            6 * IN_MILLISECONDS,
                            9 * IN_MILLISECONDS);
                }
                else
                {
                    _cleaveTimer -= diff;
                }

                if (_knockAwayTimer <= diff)
                {
                    DoCastVictim(
                        SPELL_LUMBERING_KNOCK_AWAY);

                    _knockAwayTimer =
                        urand(
                            14 * IN_MILLISECONDS,
                            18 * IN_MILLISECONDS);
                }
                else
                {
                    _knockAwayTimer -= diff;
                }

                DoMeleeAttackIfReady();
            }

        private:
            uint32 _cleaveTimer = 0;
            uint32 _knockAwayTimer = 0;
            bool _enraged = false;
        };

        CreatureAI* GetAI(
            Creature* creature) const override
        {
            return new
                npc_scourge_invasion_lumbering_horrorAI(
                    creature);
        }
    };

    class npc_scourge_invasion_spirit_of_the_damned final :
        public CreatureScript
    {
    public:
        npc_scourge_invasion_spirit_of_the_damned()
            : CreatureScript(
                "npc_scourge_invasion_spirit_of_the_damned")
        {
        }

        struct npc_scourge_invasion_spirit_of_the_damnedAI final :
            public scourge_invasion_intermediate_bossAI
        {
            explicit
            npc_scourge_invasion_spirit_of_the_damnedAI(
                Creature* creature)
                : scourge_invasion_intermediate_bossAI(
                    creature)
            {
            }

            void Reset() override
            {
                ResetInvasionBoss();

                _ribbonTimer =
                    urand(
                        2 * IN_MILLISECONDS,
                        4 * IN_MILLISECONDS);

                _drainLifeTimer =
                    urand(
                        9 * IN_MILLISECONDS,
                        13 * IN_MILLISECONDS);

                _psychicScreamTimer =
                    urand(
                        15 * IN_MILLISECONDS,
                        20 * IN_MILLISECONDS);
            }

            void UpdateAI(
                uint32 diff) override
            {
                if (!UpdateInvasionBoss(diff))
                    return;

                if (me->HasUnitState(
                        UNIT_STATE_CASTING))
                {
                    return;
                }

                if (_ribbonTimer <= diff)
                {
                    if (Unit* target =
                            SelectRandomPlayerTarget(
                                40.0f))
                    {
                        DoCast(
                            target,
                            SPELL_SPIRIT_RIBBON_OF_SOULS);
                    }

                    _ribbonTimer =
                        urand(
                            4 * IN_MILLISECONDS,
                            6 * IN_MILLISECONDS);
                }
                else
                {
                    _ribbonTimer -= diff;
                }

                if (_drainLifeTimer <= diff)
                {
                    if (Unit* target =
                            SelectRandomPlayerTarget(
                                30.0f))
                    {
                        DoCast(
                            target,
                            SPELL_SPIRIT_DRAIN_LIFE);
                    }

                    _drainLifeTimer =
                        urand(
                            13 * IN_MILLISECONDS,
                            17 * IN_MILLISECONDS);
                }
                else
                {
                    _drainLifeTimer -= diff;
                }

                if (_psychicScreamTimer <= diff)
                {
                    DoCast(
                        me,
                        SPELL_SPIRIT_PSYCHIC_SCREAM);

                    _psychicScreamTimer =
                        urand(
                            20 * IN_MILLISECONDS,
                            25 * IN_MILLISECONDS);
                }
                else
                {
                    _psychicScreamTimer -= diff;
                }

                DoMeleeAttackIfReady();
            }

        private:
            uint32 _ribbonTimer = 0;
            uint32 _drainLifeTimer = 0;
            uint32 _psychicScreamTimer = 0;
        };

        CreatureAI* GetAI(
            Creature* creature) const override
        {
            return new
                npc_scourge_invasion_spirit_of_the_damnedAI(
                    creature);
        }
    };

    class npc_scourge_invasion_bone_witch final :
        public CreatureScript
    {
    public:
        npc_scourge_invasion_bone_witch()
            : CreatureScript(
                "npc_scourge_invasion_bone_witch")
        {
        }

        struct npc_scourge_invasion_bone_witchAI final :
            public scourge_invasion_intermediate_bossAI
        {
            explicit
            npc_scourge_invasion_bone_witchAI(
                Creature* creature)
                : scourge_invasion_intermediate_bossAI(
                    creature)
            {
            }

            void Reset() override
            {
                ResetInvasionBoss();

                _arcaneBoltTimer =
                    urand(
                        IN_MILLISECONDS,
                        2 * IN_MILLISECONDS);

                _frostNovaTimer =
                    urand(
                        9 * IN_MILLISECONDS,
                        13 * IN_MILLISECONDS);

                if (!me->HasAura(
                        SPELL_BONE_WITCH_BONE_SHARDS_PROC))
                {
                    DoCast(
                        me,
                        SPELL_BONE_WITCH_BONE_SHARDS_PROC,
                        true);
                }
            }

            void UpdateAI(
                uint32 diff) override
            {
                if (!UpdateInvasionBoss(diff))
                    return;

                if (me->HasUnitState(
                        UNIT_STATE_CASTING))
                {
                    return;
                }

                if (_arcaneBoltTimer <= diff)
                {
                    if (Unit* target =
                            SelectRandomPlayerTarget(
                                40.0f))
                    {
                        DoCast(
                            target,
                            SPELL_BONE_WITCH_ARCANE_BOLT);
                    }

                    _arcaneBoltTimer =
                        urand(
                            2 * IN_MILLISECONDS,
                            4 * IN_MILLISECONDS);
                }
                else
                {
                    _arcaneBoltTimer -= diff;
                }

                if (_frostNovaTimer <= diff)
                {
                    if (me->GetVictim() &&
                        me->IsWithinDistInMap(
                            me->GetVictim(),
                            10.0f))
                    {
                        DoCast(
                            me,
                            SPELL_BONE_WITCH_FROST_NOVA);
                    }

                    _frostNovaTimer =
                        urand(
                            13 * IN_MILLISECONDS,
                            17 * IN_MILLISECONDS);
                }
                else
                {
                    _frostNovaTimer -= diff;
                }

                DoMeleeAttackIfReady();
            }

        private:
            uint32 _arcaneBoltTimer = 0;
            uint32 _frostNovaTimer = 0;
        };

        CreatureAI* GetAI(
            Creature* creature) const override
        {
            return new
                npc_scourge_invasion_bone_witchAI(
                    creature);
        }
    };

    class npc_scourge_invasion_combatant final :
        public CreatureScript
    {
    public:
        npc_scourge_invasion_combatant()
            : CreatureScript(
                "npc_scourge_invasion_combatant")
        {
        }

        struct npc_scourge_invasion_combatantAI final :
            public ScriptedAI
        {
            explicit
            npc_scourge_invasion_combatantAI(
                Creature* creature)
                : ScriptedAI(creature)
            {
            }

            void Reset() override
            {
                _mindFlayTimer =
                    urand(
                        2 * IN_MILLISECONDS,
                        4 * IN_MILLISECONDS);

                _fearTimer =
                    urand(
                        8 * IN_MILLISECONDS,
                        12 * IN_MILLISECONDS);
            }

            void SetGUID(
                ObjectGuid const& guid,
                int32 type) override
            {
                if (type ==
                    DATA_SHADOW_DAMAGED_SHARD_GUID)
                {
                    _damagedShardGuid = guid;
                }
            }

            void JustDied(
                Unit* /*killer*/) override
            {
                if (!sScourgeInvasion
                        .IsEventActive() ||
                    me->GetEntry() !=
                        NPC_SHADOW_OF_DOOM)
                {
                    return;
                }

                if (sScourgeInvasion
                        .IsZoneIdleState(
                            me->GetZoneId()))
                {
                    return;
                }

                Creature* damagedShard =
                    !_damagedShardGuid.IsEmpty()
                        ? ObjectAccessor::GetCreature(
                            *me,
                            _damagedShardGuid)
                        : me->FindNearestCreature(
                            NPC_DAMAGED_NECROTIC_SHARD,
                            80.0f,
                            true);

                if (damagedShard)
                {
                    damagedShard->AI()->SetData(
                        DATA_BOSS_DEFEATED,
                        1);
                }
            }

            void UpdateAI(
                uint32 diff) override
            {
                if (!sScourgeInvasion
                        .IsEventActive() ||
                    !sScourgeInvasion
                        .IsZoneSelected(
                            me->GetZoneId()) ||
                    sScourgeInvasion
                        .IsZoneIdleState(
                            me->GetZoneId()))
                {
                    me->DespawnOrUnsummon();
                    return;
                }

                if (!UpdateVictim())
                {
                    if (Player* player =
                            me->SelectNearestPlayer(
                                45.0f))
                    {
                        AttackStart(player);
                    }

                    return;
                }

                if (me->GetEntry() !=
                    NPC_SHADOW_OF_DOOM)
                {
                    DoMeleeAttackIfReady();
                    return;
                }

                if (me->HasUnitState(
                        UNIT_STATE_CASTING))
                {
                    return;
                }

                if (_fearTimer <= diff)
                {
                    if (Unit* target =
                            SelectTarget(
                                SelectTargetMethod::Random,
                                0,
                                20.0f,
                                true))
                    {
                        DoCast(
                            target,
                            SPELL_SHADOW_OF_DOOM_FEAR);
                    }
                    else
                    {
                        DoCastVictim(
                            SPELL_SHADOW_OF_DOOM_FEAR);
                    }

                    _fearTimer =
                        urand(
                            18 * IN_MILLISECONDS,
                            24 * IN_MILLISECONDS);

                    return;
                }

                _fearTimer -= diff;

                if (_mindFlayTimer <= diff)
                {
                    if (Unit* target =
                            SelectTarget(
                                SelectTargetMethod::Random,
                                0,
                                30.0f,
                                true))
                    {
                        DoCast(
                            target,
                            SPELL_SHADOW_OF_DOOM_MIND_FLAY);
                    }
                    else
                    {
                        DoCastVictim(
                            SPELL_SHADOW_OF_DOOM_MIND_FLAY);
                    }

                    _mindFlayTimer =
                        urand(
                            8 * IN_MILLISECONDS,
                            12 * IN_MILLISECONDS);

                    return;
                }

                _mindFlayTimer -= diff;

                DoMeleeAttackIfReady();
            }

        private:
            ObjectGuid _damagedShardGuid;

            uint32 _mindFlayTimer = 0;
            uint32 _fearTimer = 0;
        };

        CreatureAI* GetAI(
            Creature* creature) const override
        {
            return new
                npc_scourge_invasion_combatantAI(
                    creature);
        }
    };
}

void AddSC_custom_scourge_invasion()
{
    new scourge_invasion_world();
    new scourge_invasion_player();
    new go_scourge_invasion_portal();

    new scourge_invasion_map(
        "scourge_invasion_eastern_kingdoms",
        0);

    new scourge_invasion_map(
        "scourge_invasion_kalimdor",
        1);

    new npc_scourge_invasion_shard();
    new npc_scourge_invasion_damaged_shard();
    new npc_scourge_invasion_engineer();
    new npc_scourge_invasion_minion_spawner();

    new npc_scourge_invasion_lumbering_horror();
    new npc_scourge_invasion_spirit_of_the_damned();
    new npc_scourge_invasion_bone_witch();

    new npc_scourge_invasion_combatant();
}