/*
 * TrinityCore 3.3.5a - Custom Treasure Hunt Event (Phase 1)
 *
 * Phase 1 scope:
 * - Server-timed Ruby Sanctum event (Map 724)
 * - One shared instance, up to 40 participants
 * - Solo-only entry
 * - Free-For-All PvP
 * - Initial 30-second hidden/protected movement period
 * - Death -> ghost at death position -> 20-second resurrection wave
 * - Resurrection occurs at the ghost's current position
 * - Event end returns connected participants to their saved position
 * - LRSC addon protocol for state and join
 * - Instance-only collectible treasure, death-drop chests, event-end RP settlement
 *
 * NOT included in Phase 1:
 * - Meeting Stone 2-player matching
 * - Dynamic cover and tactical items
 *
 * Required core patches are included separately in this package.
 */

#include "Chat.h"
#include "custom_scourge_dungeon_mode.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Group.h"
#include "InstanceSaveMgr.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    std::string const TREASURE_ADDON_PREFIX = "LRSC";
    std::string const TREASURE_PAYLOAD_PREFIX = "SCOURGE_MODE:TREASURE:";
    std::string const TREASURE_GET_COMMAND = "SCOURGE_MODE:TREASURE:GET";
    std::string const TREASURE_JOIN_COMMAND = "SCOURGE_MODE:TREASURE:JOIN";

    static constexpr uint32 MAP_RUBY_SANCTUM = 724;
    static constexpr Difficulty TREASURE_DIFFICULTY = RAID_DIFFICULTY_10MAN_NORMAL;
    static constexpr uint32 TREASURE_PHASE_MASK = 0x40000000;
    static constexpr uint32 TREASURE_UPDATE_INTERVAL_MS = 250;
    static constexpr uint32 TREASURE_DISGUISE_DURATION_SECONDS = 30;
    static constexpr uint32 TREASURE_DISGUISE_COOLDOWN_SECONDS = 90;
    static constexpr uint32 TREASURE_CHEST_ENTRY = 900410;
    static constexpr uint32 TREASURE_SPARKLE_ENTRY = 900411;
    static constexpr uint32 TREASURE_CHEST_LIFETIME_SECONDS = 300;
    static constexpr uint32 TREASURE_CHEST_OPEN_SECONDS = 30;
    static constexpr uint32 TREASURE_ACTIVE_SPOT_LIMIT = 20;
    static constexpr uint32 TREASURE_TRIUMPH_EMBLEM_ITEM_ID = 47241;
    static constexpr uint32 TREASURE_BASE_REWARD_RP = 1000;
    static constexpr uint32 TREASURE_BASE_REWARD_EMBLEMS = 3;

    enum TreasureEventState : uint8
    {
        TREASURE_EVENT_IDLE = 0,
        TREASURE_EVENT_ACTIVE = 1
    };

    enum TreasureParticipantDbState : uint8
    {
        TREASURE_PARTICIPANT_ACTIVE = 1,
        TREASURE_PARTICIPANT_COMPLETED = 2,
        TREASURE_PARTICIPANT_RECOVERED = 3,
        TREASURE_PARTICIPANT_LEFT = 4
    };

    struct TreasureConfig
    {
        bool enabled = true;
        uint32 intervalMinutes = 180;
        uint32 firstDelaySeconds = 60;
        uint32 durationMinutes = 20;
        uint32 joinCloseBeforeEndMinutes = 5;
        uint32 maxPlayers = 40;
        uint32 entryHiddenSeconds = 30;
        uint32 resurrectionWaveSeconds = 20;
        uint32 respawnProtectionSeconds = 2;
    };

    struct TreasureChestConfig
    {
        bool enabled = false; // Requires sql/custom/treasure_hunt_chests_335.sql
        uint32 respawnSeconds = 20;
        uint32 maxActiveChests = 12;
        uint32 minTreasure = 1;
        uint32 maxTreasure = 3;
        uint32 rpPerTreasure = 500;
    };

    struct TreasureChestState
    {
        ObjectGuid guid = ObjectGuid::Empty;
        uint32 treasure = 0;
        time_t expiresAt = 0;
        bool revealed = false;
        bool dropped = false;
        ObjectGuid openingPlayerGuid = ObjectGuid::Empty;
        time_t openingUntil = 0;
        uint32 openingPreviousEmote = 0;
        float openingX = 0.0f;
        float openingY = 0.0f;
        float openingZ = 0.0f;
    };

    struct TreasurePlayerState
    {
        ObjectGuid guid = ObjectGuid::Empty;

        uint32 returnMapId = 0;
        uint16 returnZoneId = 0;
        float returnX = 0.0f;
        float returnY = 0.0f;
        float returnZ = 0.0f;
        float returnO = 0.0f;

        uint32 originalPhaseMask = 1;
        Difficulty originalRaidDifficulty = RAID_DIFFICULTY_10MAN_NORMAL;
        uint8 originalPvpFlags = 0;
        bool originalInFFAPvPArea = false;
        bool originalInNoPvPArea = false;

        bool entryHidden = false;
        time_t entryHiddenUntil = 0;

        bool respawnProtected = false;
        time_t respawnProtectionUntil = 0;

        bool addedNonAttackable = false;
        bool addedImmuneToPc = false;
        bool addedPacified = false;

        bool authorizedEntryTransfer = false;
        uint32 treasureHeld = 0;

        // Disguise: appearance only (no buffs, immunity, or stealth).
        uint8 disguiseId = 0;
        uint32 disguiseModelId = 0;
        uint32 disguiseOriginalDisplayId = 0;
        time_t disguiseUntil = 0;
        time_t disguiseCooldownUntil[6] = {}; // Index 1..5: independent cooldown per disguise.
        float disguiseStartX = 0.0f;
        float disguiseStartY = 0.0f;
        float disguiseStartZ = 0.0f;
    };

    uint32 GetCharacterGuid(Player const* player)
    {
        if (!player)
            return 0;

        return uint32(player->GetGUID().GetCounter());
    }

    void SendTreasureSystemMessage(Player* player, std::string const& text)
    {
        if (!player || !player->GetSession())
            return;

        ChatHandler(player->GetSession()).SendSysMessage(text.c_str());
    }

    void SendTreasureAddonPayload(Player* player, std::string const& payload)
    {
        if (!player || payload.empty())
            return;

        player->WhisperAddon(
            TREASURE_ADDON_PREFIX + "\t" + payload,
            player
        );
    }

    bool GetTreasurePayload(std::string const& message, std::string& payload)
    {
        payload.clear();

        if (message.compare(0, TREASURE_PAYLOAD_PREFIX.size(), TREASURE_PAYLOAD_PREFIX) == 0)
        {
            payload = message;
            return true;
        }

        if (message.compare(0, TREASURE_ADDON_PREFIX.size(), TREASURE_ADDON_PREFIX) != 0)
            return false;

        if (message.size() <= TREASURE_ADDON_PREFIX.size())
            return false;

        char separator = message[TREASURE_ADDON_PREFIX.size()];
        if (separator != '\t' && separator != ' ' && separator != ':')
            return false;

        std::string innerPayload = message.substr(TREASURE_ADDON_PREFIX.size() + 1);
        if (innerPayload.compare(0, TREASURE_PAYLOAD_PREFIX.size(), TREASURE_PAYLOAD_PREFIX) != 0)
            return false;

        payload = innerPayload;
        return true;
    }

    class TreasureHuntManager
    {
    public:
        static TreasureHuntManager& Instance()
        {
            static TreasureHuntManager instance;
            return instance;
        }

        void Update(uint32 diff)
        {
            if (!_initialized)
                Initialize();

            _updateAccumulator += diff;
            if (_updateAccumulator < TREASURE_UPDATE_INTERVAL_MS)
                return;

            _updateAccumulator = 0;

            time_t now = GameTime::GetGameTime();

            if (_eventState == TREASURE_EVENT_IDLE)
            {
                if (_config.enabled && _nextStartTime && now >= _nextStartTime)
                    StartEvent();

                return;
            }

            if (_eventState != TREASURE_EVENT_ACTIVE)
                return;

            UpdateParticipants(now);
            UpdateChestOpenings(now);

            if (_treasureConfig.enabled && now >= _nextTreasureSpawnTime)
            {
                SpawnPeriodicTreasure(now);
                _nextTreasureSpawnTime = now + _treasureConfig.respawnSeconds;
            }

            if (_nextResurrectionWave && now >= _nextResurrectionWave)
            {
                RunResurrectionWave(now);
                _nextResurrectionWave = now + std::max<uint32>(1, _config.resurrectionWaveSeconds);
            }

            if (_eventEndTime && now >= _eventEndTime)
                EndEvent();
        }

        bool IsActive() const
        {
            return _eventState == TREASURE_EVENT_ACTIVE;
        }

        bool IsJoinable() const
        {
            if (!IsActive())
                return false;

            time_t now = GameTime::GetGameTime();
            return !_joinCloseTime || now < _joinCloseTime;
        }

        bool IsParticipant(Player const* player) const
        {
            uint32 guidLow = GetCharacterGuid(player);
            return guidLow && _players.find(guidLow) != _players.end();
        }

        uint32 GetRemainingSeconds() const
        {
            if (!IsActive() || !_eventEndTime)
                return 0;

            time_t now = GameTime::GetGameTime();
            if (now >= _eventEndTime)
                return 0;

            return uint32(_eventEndTime - now);
        }

        uint32 GetEntryHiddenRemaining(Player const* player) const
        {
            uint32 guidLow = GetCharacterGuid(player);
            auto itr = _players.find(guidLow);
            if (itr == _players.end() || !itr->second.entryHidden)
                return 0;

            time_t now = GameTime::GetGameTime();
            if (now >= itr->second.entryHiddenUntil)
                return 0;

            return uint32(itr->second.entryHiddenUntil - now);
        }

        // Report the NEXT shared resurrection wave, not a fresh 20-second timer.
        // A player who dies midway through a wave must see the actual remaining time.
        void SendResurrectionCountdown(Player* player) const
        {
            if (!player || !IsActive() || !IsParticipant(player) ||
                player->IsAlive() || !_nextResurrectionWave)
                return;

            time_t now = GameTime::GetGameTime();
            uint32 seconds = now < _nextResurrectionWave
                ? uint32(_nextResurrectionWave - now) : 0;
            SendTreasureAddonPayload(player,
                "SCOURGE_MODE:TREASURE:RESPAWN:START:" + std::to_string(seconds));
        }

        uint8 GetUnlockedDisguiseMask(Player* player) const
        {
            // Flower (slot 1) is permanently free. Other slots are per-character unlocks.
            uint8 mask = 1;
            if (!player || !player->GetSession())
                return mask;

            uint32 characterGuid = GetCharacterGuid(player);
            if (!characterGuid)
                return mask;

            if (QueryResult result = CharacterDatabase.PQuery(
                    "SELECT disguise_id FROM appdata.treasure_disguise_character_unlocks "
                    "WHERE character_guid = {}", characterGuid))
            {
                do
                {
                    uint8 id = result->Fetch()[0].GetUInt8();
                    if (id >= 2 && id <= 5)
                        mask |= uint8(1 << (id - 1));
                } while (result->NextRow());
            }
            return mask;
        }

        void SendDisguiseState(Player* player)
        {
            if (!player)
                return;

            uint32 cooldowns[6] = {};
            uint32 duration = 0;
            uint32 activeId = 0;
            time_t now = GameTime::GetGameTime();
            auto itr = _players.find(GetCharacterGuid(player));
            if (itr != _players.end())
            {
                TreasurePlayerState const& state = itr->second;
                for (uint8 id = 1; id <= 5; ++id)
                {
                    if (state.disguiseCooldownUntil[id] > now)
                        cooldowns[id] = uint32(state.disguiseCooldownUntil[id] - now);
                }

                if (state.disguiseId && state.disguiseUntil > now)
                {
                    activeId = state.disguiseId;
                    duration = uint32(state.disguiseUntil - now);
                }
            }

            std::ostringstream response;
            response << "SCOURGE_MODE:TREASURE:DISGUISE:STATE:"
                << uint32(GetUnlockedDisguiseMask(player)) << ":"
                << duration << ":" << activeId;
            for (uint8 id = 1; id <= 5; ++id)
                response << ":" << cooldowns[id];
            SendTreasureAddonPayload(player, response.str());
        }

        void ClearDisguise(Player* player, TreasurePlayerState& state)
        {
            if (!state.disguiseId)
                return;

            // Never replace another spell's morph if it changed the model later.
            if (player)
            {
                if (player->GetDisplayId() == state.disguiseModelId &&
                    state.disguiseOriginalDisplayId)
                    player->SetDisplayId(state.disguiseOriginalDisplayId);
            }

            state.disguiseId = 0;
            state.disguiseModelId = 0;
            state.disguiseOriginalDisplayId = 0;
            state.disguiseUntil = 0;
        }

        void InterruptDisguise(Player* player)
        {
            if (!player)
                return;

            auto itr = _players.find(GetCharacterGuid(player));
            if (itr == _players.end() || !itr->second.disguiseId)
                return;

            ClearDisguise(player, itr->second);
            SendDisguiseState(player);
        }

        void UseDisguise(Player* player, uint8 id)
        {
            if (!player || !player->GetSession())
                return;

            auto fail = [this, player](std::string const& reason)
            {
                SendTreasureSystemMessage(player, reason);
                SendTreasureAddonPayload(player, "SCOURGE_MODE:TREASURE:DISGUISE:FAIL:" + reason);
                SendDisguiseState(player);
            };

            if (id < 1 || id > 5)
            {
                fail("알 수 없는 위장 종류입니다.");
                return;
            }

            auto itr = _players.find(GetCharacterGuid(player));
            if (!IsActive() || itr == _players.end() ||
                player->GetMapId() != MAP_RUBY_SANCTUM ||
                player->GetInstanceId() != _instanceId)
            {
                fail("보물찾기 참가 중에만 위장할 수 있습니다.");
                return;
            }

            TreasurePlayerState& state = itr->second;
            if (state.disguiseId == id)
            {
                ClearDisguise(player, state);
                SendDisguiseState(player);
                return;
            }

            time_t now = GameTime::GetGameTime();
            if (state.disguiseId)
            {
                fail("현재 변신을 먼저 해제하세요.");
                return;
            }
            if (state.entryHidden || state.respawnProtected)
            {
                fail("입장 보호 또는 부활 보호 중에는 위장을 사용할 수 없습니다.");
                return;
            }
            if (!player->IsAlive() || player->IsInCombat() || player->IsMounted())
            {
                fail("전투 중, 사망 또는 탈것 탑승 상태에서는 위장할 수 없습니다.");
                return;
            }
            if (state.disguiseCooldownUntil[id] > now)
            {
                fail("선택한 위장의 재사용 대기시간이 남아 있습니다.");
                return;
            }
            if (!(GetUnlockedDisguiseMask(player) & uint8(1 << (id - 1))))
            {
                fail("RP 상점에서 아직 해금하지 않은 위장입니다.");
                return;
            }

            // These IDs MUST be valid 3.3.5a CreatureDisplayInfo IDs.
            // A gameobject display ID is not a usable player appearance.
            QueryResult modelRow = CharacterDatabase.PQuery(
                "SELECT display_id FROM appdata.treasure_disguise_models WHERE disguise_id = {}",
                uint32(id));
            uint32 displayId = modelRow ? modelRow->Fetch()[0].GetUInt32() : 0;
            if (!displayId || !sCreatureDisplayInfoStore.LookupEntry(displayId))
            {
                fail("위장 외형이 아직 설정되지 않았습니다. 관리자의 모델 ID 확인이 필요합니다.");
                return;
            }

            state.disguiseId = id;
            state.disguiseModelId = displayId;
            state.disguiseOriginalDisplayId = player->GetDisplayId();
            state.disguiseStartX = player->GetPositionX();
            state.disguiseStartY = player->GetPositionY();
            state.disguiseStartZ = player->GetPositionZ();
            state.disguiseUntil = now + TREASURE_DISGUISE_DURATION_SECONDS;
            state.disguiseCooldownUntil[id] = now + TREASURE_DISGUISE_COOLDOWN_SECONDS;

            player->SetDisplayId(displayId);
            SendDisguiseState(player);
        }

        void SendState(Player* player)
        {
            if (!_initialized)
                Initialize();

            if (!player)
                return;

            bool joined = IsParticipant(player);

            std::ostringstream payload;
            payload
                << "SCOURGE_MODE:TREASURE:STATE:"
                << (IsActive() ? 1 : 0) << ":"
                << (IsJoinable() ? 1 : 0) << ":"
                << _players.size() << ":"
                << _config.maxPlayers << ":"
                << GetRemainingSeconds() << ":"
                << (joined ? 1 : 0) << ":"
                << GetEntryHiddenRemaining(player);

            SendTreasureAddonPayload(player, payload.str());
            if (joined && !player->IsAlive())
                SendResurrectionCountdown(player);
            SendDisguiseState(player);
            SendTreasureBalance(player);
        }

        // Remove only the old treasure-hunt instance bind belonging to this character.
        // A normal Ruby Sanctum lockout is never removed.
        void CleanupExpiredTreasureBind(Player* player)
        {
            if (!player)
                return;

            uint32 guidLow = GetCharacterGuid(player);
            if (!guidLow)
                return;

            InstancePlayerBind* bind = player->GetBoundInstance(
                MAP_RUBY_SANCTUM, TREASURE_DIFFICULTY, true
            );
            if (!bind || !bind->save)
                return;

            uint32 boundInstanceId = bind->save->GetInstanceId();

            // Still inside the bound instance: keep the binding until departure.
            if (player->GetMapId() == MAP_RUBY_SANCTUM &&
                player->GetInstanceId() == boundInstanceId)
                return;

            // Keep an active event's shared instance binding intact.
            if (_eventState == TREASURE_EVENT_ACTIVE &&
                _instanceId == boundInstanceId &&
                _players.find(guidLow) != _players.end())
                return;

            QueryResult previousEvent = CharacterDatabase.PQuery(
                "SELECT 1 FROM `custom_treasure_hunt_participant` p "
                "INNER JOIN `custom_treasure_hunt_event` e ON e.`event_id` = p.`event_id` "
                "WHERE p.`guid` = {} AND e.`instance_id` = {} AND e.`map_id` = {} "
                "LIMIT 1",
                guidLow, boundInstanceId, MAP_RUBY_SANCTUM
            );
            if (!previousEvent)
                return;

            player->UnbindInstance(MAP_RUBY_SANCTUM, TREASURE_DIFFICULTY);
            TC_LOG_INFO(
                "custom.treasure",
                "Removed stale treasure-hunt bind: player={}, guid={}, instance={}",
                player->GetName(), guidLow, boundInstanceId
            );
        }

        bool Join(Player* player)
        {
            if (!_initialized)
                Initialize();

            if (!player || !player->GetSession())
                return false;

            if (!CustomScourgeDungeonMode::IsEnabled(player))
            {
                SendJoinFail(player, "침공룬을 활성화해야 루비 성소 보물찾기에 참여할 수 있습니다.");
                return false;
            }

            if (!IsActive())
            {
                SendJoinFail(player, "현재 진행 중인 보물찾기 이벤트가 없습니다.");
                return false;
            }

            if (!IsJoinable())
            {
                SendJoinFail(player, "보물찾기 이벤트의 신규 입장이 마감되었습니다.");
                return false;
            }

            if (!player->IsAlive())
            {
                SendJoinFail(player, "살아 있는 상태에서만 참여할 수 있습니다.");
                return false;
            }

            uint32 guidLow = GetCharacterGuid(player);
            if (!guidLow)
                return false;

            if (_players.find(guidLow) != _players.end())
            {
                SendJoinFail(player, "이미 보물찾기 이벤트에 참여 중입니다.");
                return false;
            }

            if (_players.size() >= _config.maxPlayers)
            {
                SendJoinFail(player, "보물찾기 이벤트의 최대 참가 인원에 도달했습니다.");
                return false;
            }

            if (!_instanceSave || !_instanceId)
            {
                SendJoinFail(player, "보물찾기 이벤트 인스턴스가 준비되지 않았습니다.");
                return false;
            }

            // Past treasure-hunt bindings cannot block the next event.
            CleanupExpiredTreasureBind(player);

            // Do not destroy a player's normal Ruby Sanctum lockout.
            // Phase 1 refuses entry if this difficulty already has a different bind.
            if (InstancePlayerBind* existingBind = player->GetBoundInstance(
                    MAP_RUBY_SANCTUM,
                    TREASURE_DIFFICULTY,
                    true))
            {
                if (existingBind->save && existingBind->save->GetInstanceId() != _instanceId)
                {
                    SendJoinFail(
                        player,
                        "현재 난이도의 루비 성소 귀속이 있어 보물찾기에 참여할 수 없습니다. 기존 귀속을 삭제하지 않도록 안전하게 차단했습니다."
                    );
                    return false;
                }
            }

            AreaTriggerTeleport const* entrance = sObjectMgr->GetMapEntranceTrigger(MAP_RUBY_SANCTUM);
            if (!entrance)
            {
                SendJoinFail(player, "루비 성소 입장 좌표를 찾을 수 없습니다.");
                TC_LOG_ERROR("custom.treasure", "Ruby Sanctum entrance trigger not found: map={}", MAP_RUBY_SANCTUM);
                return false;
            }

            TreasurePlayerState state;
            state.guid = player->GetGUID();
            state.returnMapId = player->GetMapId();
            state.returnZoneId = uint16(player->GetZoneId());
            state.returnX = player->GetPositionX();
            state.returnY = player->GetPositionY();
            state.returnZ = player->GetPositionZ();
            state.returnO = player->GetOrientation();
            state.originalPhaseMask = player->GetPhaseMask();
            state.originalRaidDifficulty = player->GetRaidDifficulty();
            state.originalPvpFlags = uint8(player->GetPvpFlags());
            state.originalInFFAPvPArea = player->pvpInfo.IsInFFAPvPArea;
            state.originalInNoPvPArea = player->pvpInfo.IsInNoPvPArea;
            state.authorizedEntryTransfer = true;

            // Entry is ALWAYS solo. Leave only this player's current party or
            // raid before the event instance is bound and the player teleports.
            // Other members of a larger group keep their existing group.
            if (player->GetGroup())
            {
                player->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);
                if (player->GetGroup())
                {
                    SendJoinFail(player, "파티를 해제할 수 없어 보물찾기 입장이 취소되었습니다.");
                    return false;
                }

                SendTreasureSystemMessage(player,
                    "보물찾기 입장을 위해 기존 파티에서 탈퇴했습니다. 입장 후 최대 2인 파티를 만들 수 있습니다.");
            }

            // Do not leave a pending outside-party invitation active on entry.
            if (player->GetGroupInvite())
                player->UninviteFromGroup();

            player->SetRaidDifficulty(TREASURE_DIFFICULTY);

            if (!player->BindToInstance(_instanceSave, false))
            {
                player->SetRaidDifficulty(state.originalRaidDifficulty);
                SendJoinFail(player, "보물찾기 인스턴스 귀속에 실패했습니다.");
                return false;
            }

            player->m_InstanceValid = true;
            player->UpdateHomebindTime(0);
            player->SetPhaseMask(TREASURE_PHASE_MASK, true);

            ApplyFreeForAll(player);
            ApplyEntryHiddenProtection(player, state);

            _players.emplace(guidLow, state);
            SaveParticipantJoin(_players.at(guidLow));
            if (_treasureConfig.enabled)
            {
                CharacterDatabase.PExecute(
                    "INSERT INTO custom_treasure_hunt_treasure_balance "
                    "(event_id, guid, account_id) VALUES ({}, {}, {}) "
                    "ON DUPLICATE KEY UPDATE account_id = VALUES(account_id)",
                    _eventId, guidLow, player->GetSession()->GetAccountId()
                );
            }
            UpdateEventParticipantCount();

            bool teleported = player->TeleportTo(
                MAP_RUBY_SANCTUM,
                entrance->target_X,
                entrance->target_Y,
                entrance->target_Z,
                entrance->target_Orientation
            );

            if (!teleported)
            {
                auto itr = _players.find(guidLow);
                if (itr != _players.end())
                {
                    TreasurePlayerState failedState = itr->second;
                    _players.erase(itr);
                    RestorePlayerState(player, failedState, true);
                }

                MarkParticipantState(guidLow, TREASURE_PARTICIPANT_LEFT);
                UpdateEventParticipantCount();
                SendJoinFail(player, "루비 성소 보물찾기 던전으로 이동하지 못했습니다.");
                return false;
            }

            std::ostringstream message;
            message
                << "보물찾기 이벤트에 입장합니다. "
                << _config.entryHiddenSeconds
                << "초 동안 다른 참가자에게 보이지 않으며 전투와 보물 획득이 제한됩니다.";
            SendTreasureSystemMessage(player, message.str());

            SendTreasureAddonPayload(player, "SCOURGE_MODE:TREASURE:JOIN:OK");
            SendState(player);

            TC_LOG_INFO(
                "custom.treasure",
                "Treasure participant joined: event={}, player={}, guid={}, instance={}",
                _eventId,
                player->GetName(),
                guidLow,
                _instanceId
            );

            return true;
        }

        void HandleMapChanged(Player* player)
        {
            if (!player)
                return;

            uint32 guidLow = GetCharacterGuid(player);
            auto itr = _players.find(guidLow);
            if (itr == _players.end())
                return;

            TreasurePlayerState& state = itr->second;

            if (state.authorizedEntryTransfer)
            {
                if (player->GetMapId() == MAP_RUBY_SANCTUM &&
                    player->GetInstanceId() == _instanceId)
                {
                    state.authorizedEntryTransfer = false;

                    // Start the full hidden timer from confirmed Ruby Sanctum arrival,
                    // not from the moment TeleportTo was requested.
                    ApplyEntryHiddenProtection(player, state);
                    EnsureParticipantRuntimeState(player);
                    if (_treasureConfig.enabled)
                    {
                        // Seed five discoverable sparkle sites when the first participant arrives.
                        for (uint32 i = 0; i < TREASURE_ACTIVE_SPOT_LIMIT; ++i)
                            SpawnChestNear(player, urand(_treasureConfig.minTreasure, _treasureConfig.maxTreasure));
                    }
                    SendState(player);
                    return;
                }

                // Still in transfer; leave the marker until the final map change arrives.
                return;
            }

            if (player->GetMapId() == MAP_RUBY_SANCTUM &&
                player->GetInstanceId() == _instanceId)
            {
                EnsureParticipantRuntimeState(player);
                return;
            }

            // Hearthstone, portal, summon, or another external transfer.
            // Phase 1 treats this as leaving the event and keeps the player at the new destination.
            ForfeitTreasure(player);
            TreasurePlayerState stateCopy = state;
            _players.erase(itr);
            RestorePlayerState(player, stateCopy, true);
            MarkParticipantState(guidLow, TREASURE_PARTICIPANT_LEFT);
            UpdateEventParticipantCount();

            SendTreasureSystemMessage(player, "보물찾기 이벤트 지역을 벗어나 참가 상태가 해제되었습니다.");
            SendState(player);
        }

        void HandlePlayerRepop(Player* player)
        {
            if (!player || !IsParticipant(player))
                return;

            // BuildPlayerRepop already created the ghost at the death position.
            // Never move the ghost to a fixed graveyard.
            uint32 guidLow = GetCharacterGuid(player);
            auto itr = _players.find(guidLow);
            if (itr == _players.end())
                return;

            TreasurePlayerState& state = itr->second;
            time_t now = GameTime::GetGameTime();
            ClearDisguise(player, state);

            if (state.entryHidden && now < state.entryHiddenUntil)
                player->SetVisible(false);
            else
                player->SetVisible(true);

            player->SetPhaseMask(TREASURE_PHASE_MASK, true);
            player->m_InstanceValid = true;
            player->UpdateHomebindTime(0);
            player->CombatStopWithPets();
            player->SetSelection(ObjectGuid::Empty);
            ApplyFreeForAll(player);

            SendTreasureSystemMessage(
                player,
                "유령 상태로 안전한 위치를 찾으세요. 다음 부활 파동 때 현재 유령 위치에서 자동 부활합니다."
            );
            SendResurrectionCountdown(player);
        }

        void HandleLogout(Player* player)
        {
            if (!player)
                return;

            uint32 guidLow = GetCharacterGuid(player);
            auto itr = _players.find(guidLow);
            if (itr == _players.end())
                return;

            // Keep the DB participant row ACTIVE. On the next login it is used
            // to return the character to the exact pre-event position.
            ForfeitTreasure(player);
            TreasurePlayerState stateCopy = itr->second;
            _players.erase(itr);

            RestorePlayerState(player, stateCopy, true);
            UpdateEventParticipantCount();

            TC_LOG_INFO(
                "custom.treasure",
                "Treasure participant logged out; recovery row preserved: event={}, player={}, guid={}",
                _eventId,
                player->GetName(),
                guidLow
            );
        }

        void RecoverStaleParticipantOnLogin(Player* player)
        {
            if (!player)
                return;

            uint32 guidLow = GetCharacterGuid(player);
            if (!guidLow)
                return;

            QueryResult result = CharacterDatabase.PQuery(
                "SELECT p.`event_id`, p.`return_map`, p.`return_zone`, p.`return_x`, p.`return_y`, p.`return_z`, p.`return_o`, "
                "p.`original_phase_mask`, p.`original_raid_difficulty`, p.`original_pvp_flags`, "
                "p.`original_in_ffa`, p.`original_in_no_pvp`, p.`added_non_attackable`, p.`added_immune_to_pc`, p.`added_pacified`, "
                "COALESCE(e.`instance_id`, 0) "
                "FROM `custom_treasure_hunt_participant` p "
                "LEFT JOIN `custom_treasure_hunt_event` e ON e.`event_id` = p.`event_id` "
                "WHERE p.`guid` = {} AND p.`state` = {} "
                "ORDER BY p.`event_id` DESC LIMIT 1",
                guidLow,
                uint32(TREASURE_PARTICIPANT_ACTIVE)
            );

            if (!result)
                return;

            Field* fields = result->Fetch();

            uint64 staleEventId = fields[0].GetUInt64();

            TreasurePlayerState state;
            state.guid = player->GetGUID();
            state.returnMapId = fields[1].GetUInt32();
            state.returnZoneId = fields[2].GetUInt16();
            state.returnX = fields[3].GetFloat();
            state.returnY = fields[4].GetFloat();
            state.returnZ = fields[5].GetFloat();
            state.returnO = fields[6].GetFloat();
            state.originalPhaseMask = fields[7].GetUInt32();
            state.originalRaidDifficulty = Difficulty(fields[8].GetUInt8());
            state.originalPvpFlags = fields[9].GetUInt8();
            state.originalInFFAPvPArea = fields[10].GetUInt8() != 0;
            state.originalInNoPvPArea = fields[11].GetUInt8() != 0;
            state.addedNonAttackable = fields[12].GetUInt8() != 0;
            state.addedImmuneToPc = fields[13].GetUInt8() != 0;
            state.addedPacified = fields[14].GetUInt8() != 0;
            uint32 staleInstanceId = fields[15].GetUInt32();

            // A worldserver crash can leave the temporary event character bind in char_instance.
            // Remove only the bind that belongs to the stale treasure event.
            if (staleInstanceId)
            {
                InstancePlayerBind* bind = player->GetBoundInstance(
                    MAP_RUBY_SANCTUM,
                    TREASURE_DIFFICULTY,
                    true
                );

                if (bind && bind->save && bind->save->GetInstanceId() == staleInstanceId)
                    player->UnbindInstance(MAP_RUBY_SANCTUM, TREASURE_DIFFICULTY);
            }

            RestorePlayerState(player, state, false);

            bool teleported = player->TeleportTo(
                state.returnMapId,
                state.returnX,
                state.returnY,
                state.returnZ,
                state.returnO
            );

            CharacterDatabase.PExecute(
                "UPDATE `custom_treasure_hunt_participant` "
                "SET `state` = {}, `left_at` = NOW() "
                "WHERE `event_id` = {} AND `guid` = {}",
                uint32(TREASURE_PARTICIPANT_RECOVERED),
                staleEventId,
                guidLow
            );

            if (teleported)
            {
                SendTreasureSystemMessage(
                    player,
                    "이전 보물찾기 참가 상태를 정리하고 이벤트 참여 전 위치로 복귀했습니다."
                );
            }
            else
            {
                TC_LOG_ERROR(
                    "custom.treasure",
                    "Failed stale participant return teleport: player={}, guid={}, map={}",
                    player->GetName(),
                    guidLow,
                    state.returnMapId
                );
            }
        }

        void SendTreasureBalance(Player* player)
        {
            if (!player)
                return;

            uint32 held = 0;
            auto itr = _players.find(GetCharacterGuid(player));
            if (itr != _players.end())
                held = itr->second.treasureHeld;

            SendTreasureAddonPayload(player,
                "SCOURGE_MODE:TREASURE:LOOT:STATE:" + std::to_string(held));
        }

        void DropHeldTreasure(Player* player)
        {
            if (!player || !IsActive() || !_treasureConfig.enabled)
                return;

            auto itr = _players.find(GetCharacterGuid(player));
            if (itr == _players.end() || !itr->second.treasureHeld)
                return;

            uint32 dropped = itr->second.treasureHeld;
            GameObject* drop = SpawnChestNear(player, dropped, true);
            if (!drop)
            {
                TC_LOG_ERROR("custom.treasure",
                    "Death-drop chest creation failed for event={} player={}; rewards retained for retry.",
                    _eventId, GetCharacterGuid(player));
                return;
            }

            itr->second.treasureHeld = 0;
            CharacterDatabase.PExecute(
                "UPDATE custom_treasure_hunt_treasure_balance "
                "SET held_treasure = 0 WHERE event_id = {} AND guid = {} AND settled = 0",
                _eventId, GetCharacterGuid(player)
            );

            SendTreasureBalance(player);
            SendTreasureSystemMessage(player,
                "사망하여 보물 " + std::to_string(dropped) + "개를 떨어뜨렸습니다.");
        }

        bool CanUseTreasureObject(Player* player, GameObject* go)
        {
            if (!player || !go || !IsActive() || !_treasureConfig.enabled ||
                !IsParticipant(player) || player->GetMapId() != MAP_RUBY_SANCTUM ||
                player->GetInstanceId() != _instanceId)
                return false;

            auto itr = _players.find(GetCharacterGuid(player));
            if (itr == _players.end())
                return false;

            TreasurePlayerState const& state = itr->second;
            if (!player->IsAlive() || state.entryHidden || state.respawnProtected)
            {
                SendTreasureSystemMessage(player, "입장 보호 또는 부활 대기 중에는 보물을 열 수 없습니다.");
                return false;
            }
            return player->IsWithinDistInMap(go, 6.0f);
        }

        bool RevealChest(Player* player, GameObject* sparkle)
        {
            if (!sparkle || sparkle->GetEntry() != TREASURE_SPARKLE_ENTRY ||
                !CanUseTreasureObject(player, sparkle))
                return false;

            uint32 key = uint32(sparkle->GetGUID().GetCounter());
            auto itr = _treasureChests.find(key);
            if (itr == _treasureChests.end() || itr->second.guid != sparkle->GetGUID() ||
                itr->second.revealed || GameTime::GetGameTime() >= itr->second.expiresAt)
                return false;

            GameObject* go = player->SummonGameObject(
                TREASURE_CHEST_ENTRY, sparkle->GetPosition(), QuaternionData(),
                Seconds(TREASURE_CHEST_LIFETIME_SECONDS)
            );
            if (!go)
                return false;

            go->SetPhaseMask(TREASURE_PHASE_MASK, true);
            TreasureChestState chest = itr->second;
            chest.guid = go->GetGUID();
            chest.revealed = true;
            chest.expiresAt = GameTime::GetGameTime() + TREASURE_CHEST_LIFETIME_SECONDS;
            _treasureChests.erase(itr);
            _treasureChests[uint32(chest.guid.GetCounter())] = chest;
            sparkle->DespawnOrUnsummon();
            // The first click reveals the chest and immediately starts the existing 30-second opening.
            if (!ClaimChest(player, go))
                SendTreasureSystemMessage(player, "보물상자를 발견했습니다. 상자를 클릭해 개봉하세요.");
            return true;
        }

        bool ClaimChest(Player* player, GameObject* go)
        {
            if (!go || go->GetEntry() != TREASURE_CHEST_ENTRY ||
                !CanUseTreasureObject(player, go))
                return false;

            uint32 key = uint32(go->GetGUID().GetCounter());
            auto itr = _treasureChests.find(key);
            if (itr == _treasureChests.end() || itr->second.guid != go->GetGUID() ||
                !itr->second.revealed || GameTime::GetGameTime() >= itr->second.expiresAt)
                return false;

            TreasureChestState& chest = itr->second;
            if (!chest.openingPlayerGuid.IsEmpty())
            {
                SendTreasureSystemMessage(player,
                    chest.openingPlayerGuid == player->GetGUID()
                    ? "이미 보물상자를 개봉하는 중입니다."
                    : "다른 참가자가 이 보물상자를 개봉하고 있습니다.");
                return false;
            }

            chest.openingPlayerGuid = player->GetGUID();
            chest.openingUntil = GameTime::GetGameTime() + TREASURE_CHEST_OPEN_SECONDS;
            chest.openingPreviousEmote = uint32(player->GetEmoteState());
            player->SetEmoteState(EMOTE_STATE_LOOT);
            chest.openingX = player->GetPositionX();
            chest.openingY = player->GetPositionY();
            chest.openingZ = player->GetPositionZ();
            SendTreasureSystemMessage(player,
                "보물상자 개봉 시작! 30초 동안 자리를 지키세요. 이동하거나 사망하면 취소됩니다.");
            SendTreasureAddonPayload(player, "SCOURGE_MODE:TREASURE:CHEST:OPEN:START:30");
            return true;
        }

        void CancelChestOpening(Player* player)
        {
            if (!player)
                return;
            for (auto& entry : _treasureChests)
            {
                TreasureChestState& chest = entry.second;
                if (chest.openingPlayerGuid != player->GetGUID())
                    continue;
                if (player->GetEmoteState() == EMOTE_STATE_LOOT)
                    player->SetEmoteState(Emote(chest.openingPreviousEmote));
                chest.openingPlayerGuid = ObjectGuid::Empty;
                chest.openingUntil = 0;
                SendTreasureAddonPayload(player, "SCOURGE_MODE:TREASURE:CHEST:OPEN:CANCEL");
                SendTreasureSystemMessage(player, "보물상자 개봉이 취소되었습니다.");
            }
        }

        void CompleteChestOpening(uint32 key)
        {
            auto itr = _treasureChests.find(key);
            if (itr == _treasureChests.end())
                return;

            TreasureChestState const& chest = itr->second;
            if (!chest.revealed || chest.openingPlayerGuid.IsEmpty() ||
                GameTime::GetGameTime() < chest.openingUntil)
                return;

            Player* player = ObjectAccessor::FindConnectedPlayer(chest.openingPlayerGuid);
            if (!player)
                return;

            GameObject* go = ObjectAccessor::GetGameObject(*player, chest.guid);
            if (!go || !CanUseTreasureObject(player, go))
                return;

            auto stateItr = _players.find(GetCharacterGuid(player));
            if (stateItr == _players.end())
                return;

            uint32 amount = chest.treasure;
            bool dropped = chest.dropped;
            if (player->GetEmoteState() == EMOTE_STATE_LOOT)
                player->SetEmoteState(Emote(chest.openingPreviousEmote));

            _treasureChests.erase(itr); // Consume before awarding, preventing duplicate claims.
            stateItr->second.treasureHeld += amount;
            CharacterDatabase.PExecute(
                "UPDATE custom_treasure_hunt_treasure_balance "
                "SET held_treasure = {} WHERE event_id = {} AND guid = {} AND settled = 0",
                stateItr->second.treasureHeld, _eventId, GetCharacterGuid(player)
            );
            go->DespawnOrUnsummon();
            SendTreasureAddonPayload(player, "SCOURGE_MODE:TREASURE:CHEST:OPEN:DONE");
            SendTreasureBalance(player);
            SendTreasureSystemMessage(player,
                "보물 " + std::to_string(amount) + "개 임시 보유! 현재 보유: " +
                std::to_string(stateItr->second.treasureHeld) +
                "개. RP와 토큰은 이벤트 종료 후 지급됩니다.");

            std::string announcement = "|cffffcc00[보물찾기]|r " + player->GetName() +
                " 님이 보물상자를 개봉했습니다!";
            sWorld->SendServerMessage(SERVER_MSG_STRING, announcement.c_str(), 0);
            if (!dropped)
                SpawnPeriodicTreasure(GameTime::GetGameTime());
        }

        void UpdateChestOpenings(time_t now)
        {
            if (!_treasureConfig.enabled)
                return;

            std::vector<uint32> completed;
            for (auto itr = _treasureChests.begin(); itr != _treasureChests.end();)
            {
                TreasureChestState& chest = itr->second;
                if (now >= chest.expiresAt)
                {
                    if (!chest.openingPlayerGuid.IsEmpty())
                        if (Player* player = ObjectAccessor::FindConnectedPlayer(chest.openingPlayerGuid))
                        {
                            if (player->GetEmoteState() == EMOTE_STATE_LOOT)
                                player->SetEmoteState(Emote(chest.openingPreviousEmote));
                            SendTreasureAddonPayload(player, "SCOURGE_MODE:TREASURE:CHEST:OPEN:CANCEL");
                        }
                    itr = _treasureChests.erase(itr);
                    continue;
                }

                if (!chest.openingPlayerGuid.IsEmpty())
                {
                    Player* player = ObjectAccessor::FindConnectedPlayer(chest.openingPlayerGuid);
                    GameObject* go = player ? ObjectAccessor::GetGameObject(*player, chest.guid) : nullptr;
                    float dx = player ? player->GetPositionX() - chest.openingX : 0.0f;
                    float dy = player ? player->GetPositionY() - chest.openingY : 0.0f;
                    float dz = player ? player->GetPositionZ() - chest.openingZ : 0.0f;
                    bool moved = dx * dx + dy * dy + dz * dz > 0.04f;

                    if (!go || !player->IsAlive() || !IsParticipant(player) ||
                        player->GetMapId() != MAP_RUBY_SANCTUM ||
                        player->GetInstanceId() != _instanceId ||
                        !player->IsWithinDistInMap(go, 6.0f) || moved)
                    {
                        if (player)
                        {
                            if (player->GetEmoteState() == EMOTE_STATE_LOOT)
                                player->SetEmoteState(Emote(chest.openingPreviousEmote));
                            SendTreasureAddonPayload(player, "SCOURGE_MODE:TREASURE:CHEST:OPEN:CANCEL");
                            SendTreasureSystemMessage(player, "보물상자 개봉이 취소되었습니다.");
                        }
                        chest.openingPlayerGuid = ObjectGuid::Empty;
                        chest.openingUntil = 0;
                    }
                    else if (now >= chest.openingUntil)
                        completed.push_back(itr->first);
                }
                ++itr;
            }
            for (uint32 key : completed)
                CompleteChestOpening(key);
        }

        void HandleAddonMessage(Player* player, std::string const& message)
        {
            if (!player)
                return;

            std::string payload;
            if (!GetTreasurePayload(message, payload))
                return;

            if (payload == TREASURE_GET_COMMAND)
            {
                SendState(player);
                return;
            }

            if (payload == TREASURE_JOIN_COMMAND)
            {
                Join(player);
                return;
            }

            if (payload == "SCOURGE_MODE:TREASURE:DISGUISE:GET")
            {
                SendDisguiseState(player);
                return;
            }
            if (payload == "SCOURGE_MODE:TREASURE:DISGUISE:CLEAR")
            {
                auto itr = _players.find(GetCharacterGuid(player));
                if (itr != _players.end())
                    ClearDisguise(player, itr->second);
                SendDisguiseState(player);
                return;
            }

            std::string const usePrefix = "SCOURGE_MODE:TREASURE:DISGUISE:USE:";
            if (payload.compare(0, usePrefix.size(), usePrefix) == 0)
            {
                std::string idText = payload.substr(usePrefix.size());
                if (idText.size() == 1 && idText[0] >= '1' && idText[0] <= '5')
                    UseDisguise(player, uint8(idText[0] - '0'));
                return;
            }
        }

    private:
        TreasureHuntManager() = default;

        void Initialize()
        {
            _initialized = true;
            LoadConfig();
            LoadTreasureConfig();

            // Any event left ACTIVE in DB belongs to a previous worldserver process.
            CharacterDatabase.PExecute(
                "UPDATE `custom_treasure_hunt_event` "
                "SET `state` = 3, `ended_at` = NOW() "
                "WHERE `state` = 1"
            );

            time_t now = GameTime::GetGameTime();
            _nextStartTime = now + std::max<uint32>(1, _config.firstDelaySeconds);

            TC_LOG_INFO(
                "custom.treasure",
                "Treasure Hunt manager initialized: enabled={}, firstDelay={}s, interval={}m, duration={}m, maxPlayers={}",
                _config.enabled ? 1 : 0,
                _config.firstDelaySeconds,
                _config.intervalMinutes,
                _config.durationMinutes,
                _config.maxPlayers
            );
        }

        void LoadConfig()
        {
            QueryResult result = CharacterDatabase.Query(
                "SELECT `enabled`, `interval_minutes`, `first_delay_seconds`, `duration_minutes`, "
                "`join_close_before_end_minutes`, `max_players`, `entry_hidden_seconds`, "
                "`resurrection_wave_seconds`, `respawn_protection_seconds` "
                "FROM `custom_treasure_hunt_config` WHERE `id` = 1 LIMIT 1"
            );

            if (!result)
            {
                TC_LOG_ERROR(
                    "custom.treasure",
                    "custom_treasure_hunt_config row id=1 not found; compiled defaults will be used."
                );
                return;
            }

            Field* fields = result->Fetch();
            _config.enabled = fields[0].GetUInt8() != 0;
            _config.intervalMinutes = std::max<uint32>(1, fields[1].GetUInt32());
            _config.firstDelaySeconds = std::max<uint32>(1, fields[2].GetUInt32());
            _config.durationMinutes = std::max<uint32>(1, fields[3].GetUInt32());
            _config.joinCloseBeforeEndMinutes = fields[4].GetUInt32();
            _config.maxPlayers = fields[5].GetUInt32();
            _config.maxPlayers = std::max<uint32>(1, std::min<uint32>(_config.maxPlayers, 40));
            _config.entryHiddenSeconds = std::max<uint32>(1, fields[6].GetUInt32());
            _config.resurrectionWaveSeconds = std::max<uint32>(1, fields[7].GetUInt32());
            _config.respawnProtectionSeconds = fields[8].GetUInt32();

            if (_config.joinCloseBeforeEndMinutes >= _config.durationMinutes)
                _config.joinCloseBeforeEndMinutes = 0;
        }

        void LoadTreasureConfig()
        {
            _treasureConfig.enabled = false;
            QueryResult result = CharacterDatabase.Query(
                "SELECT enabled, respawn_seconds, max_active_chests, min_treasure, "
                "max_treasure, rp_per_treasure "
                "FROM custom_treasure_hunt_treasure_config WHERE id = 1 LIMIT 1"
            );
            if (!result)
            {
                TC_LOG_ERROR("custom.treasure",
                    "Treasure chest config not found. Apply sql/custom/treasure_hunt_chests_335.sql.");
                return;
            }

            Field* fields = result->Fetch();
            _treasureConfig.enabled = fields[0].GetUInt8() != 0;
            _treasureConfig.respawnSeconds = std::max<uint32>(5, fields[1].GetUInt32());
            _treasureConfig.maxActiveChests = std::max<uint32>(1, std::min<uint32>(fields[2].GetUInt32(), 80));
            _treasureConfig.minTreasure = std::max<uint32>(1, fields[3].GetUInt32());
            _treasureConfig.maxTreasure = std::max<uint32>(_treasureConfig.minTreasure, fields[4].GetUInt32());
            _treasureConfig.maxTreasure = std::min<uint32>(_treasureConfig.maxTreasure, 100);
            _treasureConfig.minTreasure = std::min<uint32>(_treasureConfig.minTreasure, 100);
            _treasureConfig.rpPerTreasure = std::min<uint32>(fields[5].GetUInt32(), 1000);

            if (_treasureConfig.enabled &&
                (!sObjectMgr->GetGameObjectTemplate(TREASURE_CHEST_ENTRY) ||
                 !sObjectMgr->GetGameObjectTemplate(TREASURE_SPARKLE_ENTRY)))
            {
                _treasureConfig.enabled = false;
                TC_LOG_ERROR("custom.treasure",
                    "Treasure chest GO 900410 or sparkle GO 900411 missing in world DB; apply treasure hunt sparkle SQL.");
            }
        }

        void LoadTreasureMapSpawnPositions()
        {
            _treasureMapSpawnPositions.clear();
            if (!_treasureConfig.enabled)
                return;

            // Existing Ruby Sanctum 10-player creature locations form a map-wide
            // spawn reference pool unrelated to any participant's position.
            QueryResult result = WorldDatabase.Query(
                "SELECT position_x, position_y, position_z FROM creature "
                "WHERE map = 724 AND (spawnMask & 1) <> 0"
            );
            if (!result)
            {
                TC_LOG_ERROR("custom.treasure",
                    "No Ruby Sanctum creature spawn coordinates found. Map-wide treasure spawning is unavailable.");
                return;
            }

            do
            {
                Field* fields = result->Fetch();
                Position point(fields[0].GetFloat(), fields[1].GetFloat(), fields[2].GetFloat(), 0.0f);
                if (!Trinity::IsValidMapCoord(point.GetPositionX(), point.GetPositionY(), point.GetPositionZ()))
                    continue;

                // Count nearby creature packs as one region to spread treasure across the map.
                bool nearbyPointExists = false;
                for (Position const& existing : _treasureMapSpawnPositions)
                {
                    float dx = existing.GetPositionX() - point.GetPositionX();
                    float dy = existing.GetPositionY() - point.GetPositionY();
                    float dz = existing.GetPositionZ() - point.GetPositionZ();
                    if (dx * dx + dy * dy < 400.0f && std::fabs(dz) < 5.0f)
                    {
                        nearbyPointExists = true;
                        break;
                    }
                }
                if (!nearbyPointExists)
                    _treasureMapSpawnPositions.push_back(point);
            } while (result->NextRow());

            TC_LOG_INFO("custom.treasure",
                "Loaded {} Ruby Sanctum map-wide treasure spawn regions.",
                _treasureMapSpawnPositions.size());
        }

        bool SelectTreasureMapPosition(Player* mapContext, Position& position)
        {
            if (!mapContext || _treasureMapSpawnPositions.empty())
                return false;

            for (uint32 attempt = 0; attempt < 60; ++attempt)
            {
                Position const& seed = _treasureMapSpawnPositions[
                    urand(0, uint32(_treasureMapSpawnPositions.size() - 1))];

                // Use the instance's terrain, never the player's location, for placement.
                Position candidate = mapContext->GetRandomPoint(seed, 8.0f);
                if (!Trinity::IsValidMapCoord(candidate.GetPositionX(), candidate.GetPositionY(), candidate.GetPositionZ()))
                    continue;

                // Avoid dropping markers onto a different vertical level.
                if (std::fabs(candidate.GetPositionZ() - seed.GetPositionZ()) > 3.0f)
                    continue;

                bool tooClose = false;
                for (auto const& entry : _treasureChests)
                {
                    TreasureChestState const& active = entry.second;
                    if (active.dropped || active.expiresAt <= GameTime::GetGameTime())
                        continue;

                    if (GameObject* existing = ObjectAccessor::GetGameObject(*mapContext, active.guid))
                    {
                        float dx = existing->GetPositionX() - candidate.GetPositionX();
                        float dy = existing->GetPositionY() - candidate.GetPositionY();
                        if (dx * dx + dy * dy < 144.0f)
                        {
                            tooClose = true;
                            break;
                        }
                    }
                }
                if (tooClose)
                    continue;

                position = candidate;
                return true;
            }
            return false;
        }

        GameObject* SpawnChestNear(Player* anchor, uint32 amount, bool dropped = false)
        {
            if (!anchor || !_treasureConfig.enabled || !_eventId || (!amount && !dropped) ||
                anchor->GetMapId() != MAP_RUBY_SANCTUM ||
                anchor->GetInstanceId() != _instanceId)
                return nullptr;

            time_t now = GameTime::GetGameTime();
            // Death drops bypass the five active discoverable locations.
            uint32 activeRegularSites = 0;
            for (auto const& entry : _treasureChests)
                if (!entry.second.dropped && entry.second.expiresAt > now)
                    ++activeRegularSites;

            if (!dropped &&
                activeRegularSites >= std::min<uint32>(_treasureConfig.maxActiveChests, TREASURE_ACTIVE_SPOT_LIMIT))
                return nullptr;

            Position position = anchor->GetPosition();
            // Death drops still appear at the death position, as before.
            if (!dropped && !SelectTreasureMapPosition(anchor, position))
                return nullptr;

            uint32 gameObjectEntry = dropped ? TREASURE_CHEST_ENTRY : TREASURE_SPARKLE_ENTRY;
            GameObject* go = anchor->SummonGameObject(
                gameObjectEntry, position, QuaternionData(),
                Seconds(TREASURE_CHEST_LIFETIME_SECONDS)
            );
            if (!go)
                return nullptr;

            go->SetPhaseMask(TREASURE_PHASE_MASK, true);
            TreasureChestState chest;
            chest.guid = go->GetGUID();
            chest.treasure = amount;
            chest.expiresAt = now + TREASURE_CHEST_LIFETIME_SECONDS;
            chest.revealed = dropped;
            chest.dropped = dropped;
            _treasureChests[uint32(chest.guid.GetCounter())] = chest;
            return go;
        }

        void SpawnPeriodicTreasure(time_t /*now*/)
        {
            // SpawnChestNear prunes expired summons BEFORE enforcing the active cap.
            std::vector<Player*> candidates;
            for (auto const& entry : _players)
            {
                if (Player* player = ObjectAccessor::FindConnectedPlayer(entry.second.guid))
                {
                    if (player->IsAlive() && !entry.second.authorizedEntryTransfer &&
                        player->GetMapId() == MAP_RUBY_SANCTUM &&
                        player->GetInstanceId() == _instanceId)
                        candidates.push_back(player);
                }
            }

            if (candidates.empty())
                return;

            Player* anchor = candidates[urand(0, uint32(candidates.size() - 1))];
            SpawnChestNear(anchor, urand(_treasureConfig.minTreasure, _treasureConfig.maxTreasure));
        }

        void ForfeitTreasure(Player* player)
        {
            if (!player || !_treasureConfig.enabled)
                return;

            auto itr = _players.find(GetCharacterGuid(player));
            if (itr != _players.end())
                itr->second.treasureHeld = 0;

            CharacterDatabase.PExecute(
                "UPDATE custom_treasure_hunt_treasure_balance "
                "SET held_treasure = 0, settled = 1 "
                "WHERE event_id = {} AND guid = {} AND settled = 0",
                _eventId, GetCharacterGuid(player)
            );
        }

        void SettleTreasure(Player* player)
        {
            if (!player || !_treasureConfig.enabled || !player->GetSession())
                return;

            uint32 guid = GetCharacterGuid(player);
            uint32 account = player->GetSession()->GetAccountId();
            auto itr = _players.find(guid);
            if (itr == _players.end())
                return;

            // Check the existing settlement guard BEFORE granting a currency item.
            // Players who left or logged out have already been marked settled.
            if (!CharacterDatabase.PQuery(
                "SELECT 1 FROM custom_treasure_hunt_treasure_balance "
                "WHERE event_id = {} AND guid = {} AND account_id = {} AND settled = 0 LIMIT 1",
                _eventId, guid, account))
                return;

            uint32 held = itr->second.treasureHeld;
            uint32 rpReward = TREASURE_BASE_REWARD_RP + held * _treasureConfig.rpPerTreasure;
            uint32 emblemCount = TREASURE_BASE_REWARD_EMBLEMS + held;

            if (!sObjectMgr->GetItemTemplate(TREASURE_TRIUMPH_EMBLEM_ITEM_ID))
            {
                TC_LOG_ERROR("custom.treasure",
                    "Emblem of Triumph item {} not found; cannot settle event={} player={}.",
                    TREASURE_TRIUMPH_EMBLEM_ITEM_ID, _eventId, guid);
                return;
            }

            // Emblems of Triumph (47241) are Wrath currency items. Give them to the
            // player's currency inventory only when the event actually ends.
            ItemPosCountVec destination;
            if (player->CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT,
                destination, TREASURE_TRIUMPH_EMBLEM_ITEM_ID, emblemCount) != EQUIP_ERR_OK)
            {
                TC_LOG_ERROR("custom.treasure",
                    "Cannot store {} Emblems of Triumph for event={} player={}.",
                    emblemCount, _eventId, guid);
                SendTreasureSystemMessage(player,
                    "승전의 문장 보관 한도가 부족하여 정산하지 못했습니다. 관리자에게 문의하세요.");
                return;
            }

            Item* emblems = player->StoreNewItem(
                destination, TREASURE_TRIUMPH_EMBLEM_ITEM_ID, true);
            if (!emblems)
            {
                TC_LOG_ERROR("custom.treasure",
                    "Could not create Emblems of Triumph for event={} player={}.", _eventId, guid);
                return;
            }

            // Save the currency item and RP/token changes in the same transaction.
            // Refresh held_treasure from the player's final runtime state, so a
            // pending asynchronous chest update cannot change the final reward.
            CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();
            player->SaveInventoryAndGoldToDB(transaction);
            transaction->PAppend(
                "UPDATE custom_treasure_hunt_treasure_balance SET held_treasure = {} "
                "WHERE event_id = {} AND guid = {} AND settled = 0",
                held, _eventId, guid
            );
            transaction->PAppend(
                "INSERT INTO appdata.account_data (id, reg_point) VALUES ({}, 0) "
                "ON DUPLICATE KEY UPDATE reg_point = reg_point", account
            );
            transaction->PAppend(
                "INSERT INTO appdata.treasure_token_balance (account_id) VALUES ({}) "
                "ON DUPLICATE KEY UPDATE account_id = account_id", account
            );
            transaction->PAppend(
                "UPDATE appdata.account_data AS a "
                "INNER JOIN custom_treasure_hunt_treasure_balance AS b "
                "ON b.account_id = a.id "
                "INNER JOIN appdata.treasure_token_balance AS t ON t.account_id = b.account_id "
                "SET a.reg_point = a.reg_point + {} + b.held_treasure * {}, "
                "t.balance = t.balance + b.held_treasure, "
                "t.total_earned = t.total_earned + b.held_treasure, "
                "b.awarded_rp = {} + b.held_treasure * {}, b.settled = 1 "
                "WHERE b.event_id = {} AND b.guid = {} AND b.settled = 0",
                TREASURE_BASE_REWARD_RP, _treasureConfig.rpPerTreasure,
                TREASURE_BASE_REWARD_RP, _treasureConfig.rpPerTreasure, _eventId, guid
            );
            CharacterDatabase.CommitTransaction(transaction);
            player->SendNewItem(emblems, emblemCount, true, false);
            SendTreasureSystemMessage(player,
                "보물찾기 정산: 보물 " + std::to_string(held) + "개 / RP +" +
                std::to_string(rpReward) + " / 승전의 문장 +" +
                std::to_string(emblemCount) + "개 / 보물 토큰 +" +
                std::to_string(held) + "개.");
        }

        bool StartEvent()
        {
            if (_eventState != TREASURE_EVENT_IDLE)
                return false;

            LoadConfig();
            LoadTreasureConfig();
            if (!_config.enabled)
            {
                _nextStartTime = GameTime::GetGameTime() + 60;
                return false;
            }

            AreaTriggerTeleport const* entrance = sObjectMgr->GetMapEntranceTrigger(MAP_RUBY_SANCTUM);
            if (!entrance)
            {
                TC_LOG_ERROR("custom.treasure", "Cannot start event; Ruby Sanctum entrance trigger missing.");
                _nextStartTime = GameTime::GetGameTime() + 60;
                return false;
            }

            uint32 instanceId = sMapMgr->GenerateInstanceId();
            if (!instanceId)
            {
                TC_LOG_ERROR("custom.treasure", "Cannot start event; GenerateInstanceId returned 0.");
                _nextStartTime = GameTime::GetGameTime() + 60;
                return false;
            }

            InstanceSave* save = sInstanceSaveMgr->AddInstanceSave(
                MAP_RUBY_SANCTUM,
                instanceId,
                TREASURE_DIFFICULTY,
                0,
                true
            );

            if (!save)
            {
                sMapMgr->FreeInstanceId(instanceId);
                TC_LOG_ERROR(
                    "custom.treasure",
                    "Cannot start event; AddInstanceSave failed: map={}, instance={}",
                    MAP_RUBY_SANCTUM,
                    instanceId
                );
                _nextStartTime = GameTime::GetGameTime() + 60;
                return false;
            }

            time_t now = GameTime::GetGameTime();

            _eventId = uint64(now);
            _instanceId = instanceId;
            _instanceSave = save;
            _eventState = TREASURE_EVENT_ACTIVE;
            _eventStartTime = now;
            _eventEndTime = now + time_t(_config.durationMinutes * 60);
            _joinCloseTime = _config.joinCloseBeforeEndMinutes
                ? _eventEndTime - time_t(_config.joinCloseBeforeEndMinutes * 60)
                : _eventEndTime;
            _nextResurrectionWave = now + std::max<uint32>(1, _config.resurrectionWaveSeconds);
            _nextStartTime = 0;
            _players.clear();
            _treasureChests.clear();
            LoadTreasureMapSpawnPositions();
            _nextTreasureSpawnTime = now + 10;

            CharacterDatabase.PExecute(
                "INSERT INTO `custom_treasure_hunt_event` "
                "(`event_id`, `instance_id`, `map_id`, `state`, `started_at`, `join_closed_at`, `participant_count`) "
                "VALUES ({}, {}, {}, 1, FROM_UNIXTIME({}), FROM_UNIXTIME({}), 0)",
                _eventId,
                _instanceId,
                MAP_RUBY_SANCTUM,
                uint64(_eventStartTime),
                uint64(_joinCloseTime)
            );

            std::ostringstream announce;
            announce
                << "|cffffcc00[보물찾기]|r 루비 성소 보물찾기 이벤트가 시작되었습니다. "
                << "침공룬 메뉴에서 참여할 수 있습니다. 최대 "
                << _config.maxPlayers
                << "명, 진행 시간 "
                << _config.durationMinutes
                << "분입니다.";

            sWorld->SendServerMessage(SERVER_MSG_STRING, announce.str().c_str(), 0);

            TC_LOG_INFO(
                "custom.treasure",
                "Treasure Hunt started: event={}, map={}, instance={}, end={}, joinClose={}",
                _eventId,
                MAP_RUBY_SANCTUM,
                _instanceId,
                uint64(_eventEndTime),
                uint64(_joinCloseTime)
            );

            return true;
        }

        void EndEvent()
        {
            if (_eventState != TREASURE_EVENT_ACTIVE)
                return;

            std::vector<uint32> participantGuids;
            participantGuids.reserve(_players.size());

            for (auto const& entry : _players)
                participantGuids.push_back(entry.first);

            for (uint32 guidLow : participantGuids)
            {
                auto itr = _players.find(guidLow);
                if (itr == _players.end())
                    continue;

                TreasurePlayerState state = itr->second;

                if (Player* player = ObjectAccessor::FindConnectedPlayer(state.guid))
                {
                    if (!player->IsAlive())
                    {
                        if (!player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) && player->getDeathState() == CORPSE)
                            player->BuildPlayerRepop();

                        if (player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                        {
                            player->ResurrectPlayer(1.0f, false);
                            player->SpawnCorpseBones();
                        }
                    }

                    CancelChestOpening(player);
                    SendTreasureAddonPayload(player, "SCOURGE_MODE:TREASURE:RESPAWN:END");
                    SettleTreasure(player);
                    RestorePlayerState(player, state, true);

                    bool teleported = player->TeleportTo(
                        state.returnMapId,
                        state.returnX,
                        state.returnY,
                        state.returnZ,
                        state.returnO
                    );

                    if (!teleported)
                    {
                        TC_LOG_ERROR(
                            "custom.treasure",
                            "Failed event-end return teleport: event={}, player={}, guid={}, map={}",
                            _eventId,
                            player->GetName(),
                            guidLow,
                            state.returnMapId
                        );
                    }
                    else
                    {
                        SendTreasureSystemMessage(player, "보물찾기 이벤트가 종료되어 참여 전 위치로 복귀합니다.");
                    }
                }

                MarkParticipantState(guidLow, TREASURE_PARTICIPANT_COMPLETED);
                _players.erase(guidLow);
            }

            CharacterDatabase.PExecute(
                "UPDATE `custom_treasure_hunt_event` "
                "SET `state` = 2, `ended_at` = NOW(), `participant_count` = {} "
                "WHERE `event_id` = {}",
                participantGuids.size(),
                _eventId
            );

            sWorld->SendServerMessage(
                SERVER_MSG_STRING,
                "|cffffcc00[보물찾기]|r 루비 성소 보물찾기 이벤트가 종료되었습니다.",
                0
            );

            TC_LOG_INFO(
                "custom.treasure",
                "Treasure Hunt ended: event={}, instance={}, participants={}",
                _eventId,
                _instanceId,
                participantGuids.size()
            );

            time_t now = GameTime::GetGameTime();
            // Repeat based on the previous start time: a 180-minute interval
            // and 20-minute duration means the next event starts 3 hours later.
            time_t nextStartTime = std::max(now + time_t(1),
                _eventStartTime + time_t(_config.intervalMinutes) * 60);

            _eventState = TREASURE_EVENT_IDLE;
            _treasureChests.clear();
            _nextTreasureSpawnTime = 0;
            _eventId = 0;
            _instanceId = 0;
            _instanceSave = nullptr;
            _eventStartTime = 0;
            _eventEndTime = 0;
            _joinCloseTime = 0;
            _nextResurrectionWave = 0;
            _nextStartTime = nextStartTime;
        }

        void UpdateParticipants(time_t now)
        {
            for (auto& entry : _players)
            {
                TreasurePlayerState& state = entry.second;
                Player* player = ObjectAccessor::FindConnectedPlayer(state.guid);
                if (!player)
                    continue;

                if (state.authorizedEntryTransfer)
                    continue;

                if (player->GetMapId() != MAP_RUBY_SANCTUM || player->GetInstanceId() != _instanceId)
                    continue;

                if (state.disguiseId)
                {
                    float dx = player->GetPositionX() - state.disguiseStartX;
                    float dy = player->GetPositionY() - state.disguiseStartY;
                    float dz = player->GetPositionZ() - state.disguiseStartZ;
                    bool moved = dx * dx + dy * dy + dz * dz > 0.04f;
                    if (now >= state.disguiseUntil || moved ||
                        player->IsInCombat() || !player->IsAlive() ||
                        player->GetDisplayId() != state.disguiseModelId)
                    {
                        ClearDisguise(player, state);
                        SendDisguiseState(player);
                    }
                }

                EnsureParticipantRuntimeState(player);

                if (!player->IsAlive() && state.treasureHeld)
                    DropHeldTreasure(player);

                if (!player->IsAlive() &&
                    !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) &&
                    player->getDeathState() == CORPSE)
                {
                    // Automatic spirit release so the player can immediately move as a ghost.
                    player->BuildPlayerRepop();
                }

                if (state.entryHidden && now >= state.entryHiddenUntil)
                {
                    state.entryHidden = false;
                    player->SetVisible(true);
                    player->UpdateObjectVisibility(true);
                    RemoveProtectionFlags(player, state);

                    SendTreasureSystemMessage(
                        player,
                        "30초 입장 은신이 종료되었습니다. 이제 다른 참가자와 전투할 수 있습니다."
                    );
                    SendState(player);
                }

                if (state.respawnProtected && now >= state.respawnProtectionUntil)
                {
                    state.respawnProtected = false;
                    RemoveProtectionFlags(player, state);
                }
            }
        }

        void RunResurrectionWave(time_t now)
        {
            for (auto& entry : _players)
            {
                TreasurePlayerState& state = entry.second;
                Player* player = ObjectAccessor::FindConnectedPlayer(state.guid);
                if (!player)
                    continue;

                if (state.authorizedEntryTransfer)
                    continue;

                if (player->GetMapId() != MAP_RUBY_SANCTUM || player->GetInstanceId() != _instanceId)
                    continue;

                if (player->IsAlive())
                    continue;

                if (!player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) && player->getDeathState() == CORPSE)
                    player->BuildPlayerRepop();

                if (!player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                    continue;

                // No teleport here. ResurrectPlayer revives the character exactly
                // where the ghost currently stands.
                player->ResurrectPlayer(1.0f, false);
                player->SpawnCorpseBones();
                player->m_InstanceValid = true;
                player->UpdateHomebindTime(0);
                player->SetPhaseMask(TREASURE_PHASE_MASK, true);
                ApplyFreeForAll(player);

                if (state.entryHidden && now < state.entryHiddenUntil)
                {
                    // Environmental death during the first 30 seconds must not
                    // reveal the player before the original entry timer expires.
                    player->SetVisible(false);
                    state.respawnProtected = false;
                    state.respawnProtectionUntil = 0;
                    ApplyProtectionFlags(player, state);
                }
                else if (_config.respawnProtectionSeconds)
                {
                    player->SetVisible(true);
                    player->UpdateObjectVisibility(true);
                    state.respawnProtected = true;
                    state.respawnProtectionUntil = now + _config.respawnProtectionSeconds;
                    ApplyProtectionFlags(player, state);
                }
                else
                {
                    player->SetVisible(true);
                    player->UpdateObjectVisibility(true);
                }

                SendTreasureAddonPayload(player, "SCOURGE_MODE:TREASURE:RESPAWN:END");
                SendTreasureSystemMessage(
                    player,
                    "부활 파동으로 현재 유령 위치에서 부활했습니다."
                );
            }
        }

        void EnsureParticipantRuntimeState(Player* player)
        {
            if (!player)
                return;

            player->m_InstanceValid = true;
            player->UpdateHomebindTime(0);
            player->SetPhaseMask(TREASURE_PHASE_MASK, true);
            ApplyFreeForAll(player);
        }

        void ApplyFreeForAll(Player* player)
        {
            if (!player)
                return;

            player->pvpInfo.IsInNoPvPArea = false;
            player->pvpInfo.IsInFFAPvPArea = true;
            player->UpdatePvP(true, true);
            player->UpdatePvPState(true);
        }

        void ApplyEntryHiddenProtection(Player* player, TreasurePlayerState& state)
        {
            if (!player)
                return;

            state.entryHidden = true;
            state.entryHiddenUntil = GameTime::GetGameTime() + std::max<uint32>(1, _config.entryHiddenSeconds);

            // Same visibility concept used by the sabotage spectator/visibility work:
            // the player still sees self, but ordinary other players do not.
            player->SetVisible(false);
            ApplyProtectionFlags(player, state);
        }

        void ApplyProtectionFlags(Player* player, TreasurePlayerState& state)
        {
            if (!player)
                return;

            if (!player->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
            {
                player->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                state.addedNonAttackable = true;
            }

            if (!player->HasUnitFlag(UNIT_FLAG_IMMUNE_TO_PC))
            {
                player->SetUnitFlag(UNIT_FLAG_IMMUNE_TO_PC);
                state.addedImmuneToPc = true;
            }

            if (!player->HasUnitFlag(UNIT_FLAG_PACIFIED))
            {
                player->SetUnitFlag(UNIT_FLAG_PACIFIED);
                state.addedPacified = true;
            }
        }

        void RemoveProtectionFlags(Player* player, TreasurePlayerState& state)
        {
            if (!player)
                return;

            if (state.addedNonAttackable)
            {
                player->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                state.addedNonAttackable = false;
            }

            if (state.addedImmuneToPc)
            {
                player->RemoveUnitFlag(UNIT_FLAG_IMMUNE_TO_PC);
                state.addedImmuneToPc = false;
            }

            if (state.addedPacified)
            {
                player->RemoveUnitFlag(UNIT_FLAG_PACIFIED);
                state.addedPacified = false;
            }
        }

        void RestorePlayerState(Player* player, TreasurePlayerState& state, bool unbindEventInstance)
        {
            if (!player)
                return;

            ClearDisguise(player, state);
            player->SetVisible(true);
            player->UpdateObjectVisibility(true);
            RemoveProtectionFlags(player, state);

            player->pvpInfo.IsInFFAPvPArea = state.originalInFFAPvPArea;
            player->pvpInfo.IsInNoPvPArea = state.originalInNoPvPArea;
            player->ReplaceAllPvpFlags(UnitPVPStateFlags(state.originalPvpFlags));
            player->UpdatePvPState(true);

            player->SetPhaseMask(state.originalPhaseMask ? state.originalPhaseMask : 1, true);
            player->SetRaidDifficulty(state.originalRaidDifficulty);
            player->UpdateHomebindTime(0);

            if (unbindEventInstance)
            {
                InstancePlayerBind* bind = player->GetBoundInstance(
                    MAP_RUBY_SANCTUM,
                    TREASURE_DIFFICULTY,
                    true
                );

                if (bind && bind->save && bind->save->GetInstanceId() == _instanceId)
                    player->UnbindInstance(MAP_RUBY_SANCTUM, TREASURE_DIFFICULTY);
            }
        }

        void SaveParticipantJoin(TreasurePlayerState const& state)
        {
            CharacterDatabase.PExecute(
                "INSERT INTO `custom_treasure_hunt_participant` "
                "(`event_id`, `guid`, `return_map`, `return_zone`, `return_x`, `return_y`, `return_z`, `return_o`, "
                "`original_phase_mask`, `original_raid_difficulty`, `original_pvp_flags`, `original_in_ffa`, `original_in_no_pvp`, "
                "`added_non_attackable`, `added_immune_to_pc`, `added_pacified`, `state`, `joined_at`) "
                "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, NOW()) "
                "ON DUPLICATE KEY UPDATE "
                "`return_map` = VALUES(`return_map`), `return_zone` = VALUES(`return_zone`), "
                "`return_x` = VALUES(`return_x`), `return_y` = VALUES(`return_y`), `return_z` = VALUES(`return_z`), `return_o` = VALUES(`return_o`), "
                "`original_phase_mask` = VALUES(`original_phase_mask`), "
                "`original_raid_difficulty` = VALUES(`original_raid_difficulty`), "
                "`original_pvp_flags` = VALUES(`original_pvp_flags`), "
                "`original_in_ffa` = VALUES(`original_in_ffa`), `original_in_no_pvp` = VALUES(`original_in_no_pvp`), "
                "`added_non_attackable` = VALUES(`added_non_attackable`), "
                "`added_immune_to_pc` = VALUES(`added_immune_to_pc`), `added_pacified` = VALUES(`added_pacified`), "
                "`state` = VALUES(`state`), `joined_at` = NOW(), `left_at` = NULL",
                _eventId,
                uint32(state.guid.GetCounter()),
                state.returnMapId,
                uint32(state.returnZoneId),
                state.returnX,
                state.returnY,
                state.returnZ,
                state.returnO,
                state.originalPhaseMask,
                uint32(state.originalRaidDifficulty),
                uint32(state.originalPvpFlags),
                state.originalInFFAPvPArea ? 1 : 0,
                state.originalInNoPvPArea ? 1 : 0,
                state.addedNonAttackable ? 1 : 0,
                state.addedImmuneToPc ? 1 : 0,
                state.addedPacified ? 1 : 0,
                uint32(TREASURE_PARTICIPANT_ACTIVE)
            );
        }

        void MarkParticipantState(uint32 guidLow, TreasureParticipantDbState state)
        {
            if (!guidLow || !_eventId)
                return;

            CharacterDatabase.PExecute(
                "UPDATE `custom_treasure_hunt_participant` "
                "SET `state` = {}, `left_at` = NOW() "
                "WHERE `event_id` = {} AND `guid` = {}",
                uint32(state),
                _eventId,
                guidLow
            );
        }

        void UpdateEventParticipantCount()
        {
            if (!_eventId)
                return;

            CharacterDatabase.PExecute(
                "UPDATE `custom_treasure_hunt_event` "
                "SET `participant_count` = {} "
                "WHERE `event_id` = {}",
                _players.size(),
                _eventId
            );
        }

        void SendJoinFail(Player* player, std::string const& reason)
        {
            SendTreasureSystemMessage(player, reason);
            SendTreasureAddonPayload(
                player,
                "SCOURGE_MODE:TREASURE:JOIN:FAIL:" + reason
            );
            SendState(player);
        }

        bool _initialized = false;
        uint32 _updateAccumulator = 0;

        TreasureConfig _config;
        TreasureEventState _eventState = TREASURE_EVENT_IDLE;

        uint64 _eventId = 0;
        uint32 _instanceId = 0;
        InstanceSave* _instanceSave = nullptr;

        time_t _eventStartTime = 0;
        time_t _eventEndTime = 0;
        time_t _joinCloseTime = 0;
        time_t _nextStartTime = 0;
        time_t _nextResurrectionWave = 0;
        time_t _nextTreasureSpawnTime = 0;

        TreasureChestConfig _treasureConfig;
        std::vector<Position> _treasureMapSpawnPositions;
        std::unordered_map<uint32, TreasureChestState> _treasureChests;
        std::unordered_map<uint32, TreasurePlayerState> _players;
    };

    class custom_treasure_hunt_world : public WorldScript
    {
    public:
        custom_treasure_hunt_world()
            : WorldScript("custom_treasure_hunt_world")
        {
        }

        void OnUpdate(uint32 diff) override
        {
            TreasureHuntManager::Instance().Update(diff);
        }
    };

    class custom_treasure_hunt_group : public GroupScript
    {
    public:
        custom_treasure_hunt_group() : GroupScript("custom_treasure_hunt_group") { }

        void OnInviteMember(Group* group, ObjectGuid guid) override
        {
            if (!group || !TreasureHuntManager::Instance().IsActive() ||
                group->GetLeaderGUID().IsEmpty())
                return; // AddLeaderInvite initializes the leader after its self-invite.

            Player* invitee = ObjectAccessor::FindConnectedPlayer(guid);
            Player* leader = ObjectAccessor::FindConnectedPlayer(group->GetLeaderGUID());
            if (!invitee)
                return;

            bool inviteeInEvent = TreasureHuntManager::Instance().IsParticipant(invitee);
            bool leaderInEvent = TreasureHuntManager::Instance().IsParticipant(leader);
            bool allianceInEvent = inviteeInEvent || leaderInEvent;

            for (Group::MemberSlot const& member : group->GetMemberSlots())
                if (Player* player = ObjectAccessor::FindConnectedPlayer(member.guid))
                    if (TreasureHuntManager::Instance().IsParticipant(player))
                        allianceInEvent = true;

            if (!allianceInEvent)
                return; // Never restrict ordinary groups outside the event.

            bool allowed = inviteeInEvent && leaderInEvent && !group->isRaidGroup() &&
                group->GetMembersCount() < 2 &&
                invitee->GetMapId() == MAP_RUBY_SANCTUM &&
                leader->GetMapId() == MAP_RUBY_SANCTUM &&
                invitee->GetInstanceId() == leader->GetInstanceId();

            for (Group::MemberSlot const& member : group->GetMemberSlots())
            {
                Player* player = ObjectAccessor::FindConnectedPlayer(member.guid);
                if (!player || !TreasureHuntManager::Instance().IsParticipant(player) ||
                    player->GetMapId() != MAP_RUBY_SANCTUM ||
                    player->GetInstanceId() != invitee->GetInstanceId())
                    allowed = false;
            }

            if (!allowed)
            {
                // Reject the invitation before it can become a third-member alliance.
                group->RemoveInvite(invitee);
                SendTreasureSystemMessage(invitee,
                    "보물찾기에서는 같은 이벤트에 입장한 참가자끼리만 최대 2인 파티를 만들 수 있습니다.");
            }
        }

        void OnAddMember(Group* group, ObjectGuid guid) override
        {
            if (!group || !TreasureHuntManager::Instance().IsActive())
                return;

            // TrinityCore rechecks standard raid requirements when a group is
            // created inside Ruby Sanctum. Keep event participants in their
            // temporary 40-player instance instead of starting a homebind timer.
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
            {
                if (TreasureHuntManager::Instance().IsParticipant(player) &&
                    player->GetMapId() == MAP_RUBY_SANCTUM)
                {
                    player->m_InstanceValid = true;
                    player->UpdateHomebindTime(0);
                }
            }
        }
    };

    class custom_treasure_hunt_player : public PlayerScript
    {
    public:
        custom_treasure_hunt_player()
            : PlayerScript("custom_treasure_hunt_player")
        {
        }

        void OnChat(
            Player* player,
            uint32 type,
            uint32 lang,
            std::string& msg,
            Player* receiver
        ) override
        {
            if (!player || type != CHAT_MSG_WHISPER || lang != LANG_ADDON)
                return;

            if (receiver && receiver != player)
                return;

            TreasureHuntManager::Instance().HandleAddonMessage(player, msg);
        }

        void OnLogin(Player* player, bool /*firstLogin*/) override
        {
            if (!player)
                return;

            TreasureHuntManager::Instance().RecoverStaleParticipantOnLogin(player);
            TreasureHuntManager::Instance().CleanupExpiredTreasureBind(player);
            TreasureHuntManager::Instance().SendState(player);
        }

        void OnMapChanged(Player* player) override
        {
            TreasureHuntManager::Instance().CancelChestOpening(player);
            TreasureHuntManager::Instance().HandleMapChanged(player);
            TreasureHuntManager::Instance().CleanupExpiredTreasureBind(player);
        }

        void OnUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
        {
            if (!player || !TreasureHuntManager::Instance().IsParticipant(player))
                return;

            player->m_InstanceValid = true;
            player->UpdateHomebindTime(0);
            player->pvpInfo.IsInNoPvPArea = false;
            player->pvpInfo.IsInFFAPvPArea = true;
            player->UpdatePvPState(true);
        }

        void OnPVPKill(Player* /*killer*/, Player* killed) override
        {
            TreasureHuntManager::Instance().DropHeldTreasure(killed);
        }

        void OnPlayerRepop(Player* player) override
        {
            TreasureHuntManager::Instance().HandlePlayerRepop(player);
        }

        void OnLogout(Player* player) override
        {
            TreasureHuntManager::Instance().CancelChestOpening(player);
            TreasureHuntManager::Instance().HandleLogout(player);
        }

        void OnSpellCast(Player* player, Spell* /*spell*/, bool /*skipCheck*/) override
        {
            // Disguise is visual only; any player-cast spell reveals the player.
            TreasureHuntManager::Instance().InterruptDisguise(player);
            TreasureHuntManager::Instance().CancelChestOpening(player);
        }
    };

    class go_custom_treasure_hunt_chest : public GameObjectScript
    {
    public:
        go_custom_treasure_hunt_chest()
            : GameObjectScript("go_custom_treasure_hunt_chest") { }

        struct AI : public GameObjectAI
        {
            explicit AI(GameObject* go) : GameObjectAI(go) { }

            bool OnGossipHello(Player* player) override
            {
                TreasureHuntManager::Instance().ClaimChest(player, me);
                return true;
            }
        };

        GameObjectAI* GetAI(GameObject* go) const override
        {
            return new AI(go);
        }
    };

    class go_custom_treasure_hunt_sparkle : public GameObjectScript
    {
    public:
        go_custom_treasure_hunt_sparkle()
            : GameObjectScript("go_custom_treasure_hunt_sparkle") { }

        struct AI : public GameObjectAI
        {
            explicit AI(GameObject* go) : GameObjectAI(go) { }

            bool OnGossipHello(Player* player) override
            {
                TreasureHuntManager::Instance().RevealChest(player, me);
                return true;
            }
        };

        GameObjectAI* GetAI(GameObject* go) const override
        {
            return new AI(go);
        }
    };

    class custom_treasure_hunt_damage : public UnitScript
    {
    public:
        custom_treasure_hunt_damage() : UnitScript("custom_treasure_hunt_damage") { }

        void OnDamage(Unit* attacker, Unit* victim, uint32& /*damage*/) override
        {
            if (attacker)
                TreasureHuntManager::Instance().InterruptDisguise(attacker->ToPlayer());
            if (victim)
                TreasureHuntManager::Instance().InterruptDisguise(victim->ToPlayer());
        }
    };
}

void AddSC_custom_treasure_hunt()
{
    new custom_treasure_hunt_world();
    new custom_treasure_hunt_group();
    new custom_treasure_hunt_player();
    new go_custom_treasure_hunt_chest();
    new go_custom_treasure_hunt_sparkle();
    new custom_treasure_hunt_damage();
}
