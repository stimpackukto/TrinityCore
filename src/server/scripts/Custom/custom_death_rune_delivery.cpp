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
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */

// Custom Update - 2026-07-28
// Death Rune delivery GameObject for Dalaran and Trial of the Crusader
// - Dalaran announces every contribution to the entire server
// - Dalaran starts a 5 minute ritual when 100 runes are completed
// - The LRUNE addon receives start, end, and current timer state messages
// - After 5 minutes, living players within 30 yards receive the 2 hour buff
// - Spell 900204 provides its cast visual directly from Spell.dbc
// - Trial of the Crusader keeps the existing raid delivery behavior
// - Trial altar disappears while an encounter is in progress and returns afterward
// - Existing buff is refreshed by another completed delivery
// - Dalaran contribution state and contributor totals are persisted in the world database
// - Dalaran state is restored after a server restart
// - One eligible account with at least 20 contributed runes is randomly selected after each completed ritual
// - The Dalaran shard reward can be received once per account per day
// - Dalaran delivery remains locked for 2 hours after the blessing is activated
// - Buff is removed on death

#include "ScriptMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GameTime.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Item.h"
#include "Mail.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "Random.h"
#include "ScriptedGossip.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

namespace
{
    constexpr uint32 MAP_NORTHREND = 571;
    constexpr uint32 MAP_TRIAL_OF_THE_CRUSADER = 649;
    constexpr uint32 ZONE_DALARAN = 4395;

    constexpr uint32 ITEM_NECROTIC_RUNE = 22484;
    constexpr uint32 ITEM_TRANSFORMATION_SHARD_CUT = 56924;
    constexpr uint32 ITEM_TRANSFORMATION_SHARD_PIERCE = 56925;
    constexpr uint32 ITEM_TRANSFORMATION_SHARD_COLLAPSE = 56926;
    constexpr uint32 ITEM_TRANSFORMATION_SHARD_OVERWHELM = 56927;
    constexpr uint32 ITEM_TRANSFORMATION_SHARD_SALVATION = 56928;
    constexpr uint32 TRANSFORMATION_SHARD_REWARD_ITEMS[] =
    {
        ITEM_TRANSFORMATION_SHARD_CUT,
        ITEM_TRANSFORMATION_SHARD_PIERCE,
        ITEM_TRANSFORMATION_SHARD_COLLAPSE,
        ITEM_TRANSFORMATION_SHARD_OVERWHELM,
        ITEM_TRANSFORMATION_SHARD_SALVATION
    };
    constexpr uint32 SPELL_DEATH_RUNE_TOC_BUFF = 900204;
    constexpr uint32 REQUIRED_RUNE_COUNT = 100;
    constexpr uint32 MIN_REWARD_CONTRIBUTION = 20;
    constexpr uint32 TRIAL_BUFF_DURATION_MS = 30 * 60 * 1000;
    constexpr uint32 DALARAN_BUFF_DURATION_MS = 2 * 60 * 60 * 1000;
    constexpr uint32 DALARAN_RITUAL_DELAY_SEC = 5 * 60;
    constexpr uint32 DALARAN_DELIVERY_LOCK_SEC = 2 * 60 * 60;
    constexpr uint32 GO_UPDATE_INTERVAL_MS = 500;
    constexpr float BUFF_RADIUS = 50.0f;

    std::string const LEGACY_RUNE_ADDON_PREFIX = "LRUNE";
    std::string const DEATH_RUNE_TIMER_QUERY_COMMAND = "DEATHRUNE:TIMER:QUERY";

    uint64 DalaranCycleId = 1;
    uint32 DalaranDeliveredRuneCount = 0;
    bool DalaranRitualActive = false;
    time_t DalaranRitualEndTime = 0;
    time_t DalaranDeliveryUnlockTime = 0;
    bool DalaranDeliveryStateLoaded = false;

    enum DeathRuneDeliveryActions : uint32
    {
        ACTION_DELIVER_ONE = GOSSIP_ACTION_INFO_DEF + 1,
        ACTION_DELIVER_TEN,
        ACTION_DELIVER_REMAINING,
        ACTION_REFRESH
    };

    enum DeathRuneAltarType : uint8
    {
        DEATH_RUNE_ALTAR_DALARAN = 1,
        DEATH_RUNE_ALTAR_TRIAL = 2
    };

    enum DeathRuneRewardStatus : uint8
    {
        DEATH_RUNE_REWARD_NO_ELIGIBLE_CONTRIBUTOR = 0,
        DEATH_RUNE_REWARD_SENT = 1,
        DEATH_RUNE_REWARD_FAILED = 2
    };

    struct DeathRuneRewardCandidate
    {
        ObjectGuid::LowType CharacterGuid = 0;
        uint32 AccountId = 0;
        std::string CharacterName;
        uint32 DeliveredRuneCount = 0;
    };

    uint32 SelectRandomTransformationShardReward()
    {
        constexpr uint32 rewardItemCount =
            sizeof(TRANSFORMATION_SHARD_REWARD_ITEMS) /
            sizeof(TRANSFORMATION_SHARD_REWARD_ITEMS[0]);

        return TRANSFORMATION_SHARD_REWARD_ITEMS[
            urand(0, rewardItemCount - 1)];
    }

    void SaveDalaranDeliveryState();

    void LoadDalaranDeliveryState()
    {
        DalaranCycleId = 1;
        DalaranDeliveredRuneCount = 0;
        DalaranRitualActive = false;
        DalaranRitualEndTime = 0;
        DalaranDeliveryUnlockTime = 0;

        WorldDatabase.DirectPExecute(
            "INSERT IGNORE INTO `custom_death_rune_delivery_state` "
            "(`id`, `cycle_id`, `delivered_count`, `ritual_active`, `ritual_end_time`, `delivery_unlock_time`) "
            "VALUES (1, 1, 0, 0, 0, 0)");

        QueryResult result = WorldDatabase.Query(
            "SELECT `cycle_id`, `delivered_count`, `ritual_active`, `ritual_end_time`, `delivery_unlock_time` "
            "FROM `custom_death_rune_delivery_state` "
            "WHERE `id` = 1");

        if (result)
        {
            Field* fields = result->Fetch();

            DalaranCycleId = std::max<uint64>(1, fields[0].GetUInt64());
            DalaranDeliveredRuneCount = std::min<uint32>(
                REQUIRED_RUNE_COUNT,
                fields[1].GetUInt32());
            DalaranRitualActive = fields[2].GetUInt8() != 0;
            DalaranRitualEndTime = static_cast<time_t>(fields[3].GetUInt64());
            DalaranDeliveryUnlockTime = static_cast<time_t>(fields[4].GetUInt64());
        }

        DalaranDeliveryStateLoaded = true;

        time_t const now = GameTime::GetGameTime();

        if (DalaranDeliveryUnlockTime > 0 && now >= DalaranDeliveryUnlockTime)
        {
            ++DalaranCycleId;
            DalaranDeliveredRuneCount = 0;
            DalaranRitualActive = false;
            DalaranRitualEndTime = 0;
            DalaranDeliveryUnlockTime = 0;
            SaveDalaranDeliveryState();
        }
        else if (DalaranDeliveredRuneCount >= REQUIRED_RUNE_COUNT &&
                 !DalaranRitualActive &&
                 DalaranDeliveryUnlockTime == 0)
        {
            DalaranRitualActive = true;
            DalaranRitualEndTime = now + DALARAN_RITUAL_DELAY_SEC;
            SaveDalaranDeliveryState();
        }
        else if (DalaranRitualActive && DalaranRitualEndTime == 0)
        {
            DalaranRitualEndTime = now + DALARAN_RITUAL_DELAY_SEC;
            SaveDalaranDeliveryState();
        }
    }

    void EnsureDalaranDeliveryStateLoaded()
    {
        if (!DalaranDeliveryStateLoaded)
            LoadDalaranDeliveryState();
    }

    void SaveDalaranDeliveryState()
    {
        EnsureDalaranDeliveryStateLoaded();

        WorldDatabase.DirectPExecute(
            "UPDATE `custom_death_rune_delivery_state` "
            "SET `cycle_id` = {}, "
            "`delivered_count` = {}, "
            "`ritual_active` = {}, "
            "`ritual_end_time` = {}, "
            "`delivery_unlock_time` = {}, "
            "`updated_at` = CURRENT_TIMESTAMP "
            "WHERE `id` = 1",
            DalaranCycleId,
            DalaranDeliveredRuneCount,
            DalaranRitualActive ? 1 : 0,
            static_cast<uint64>(DalaranRitualEndTime),
            static_cast<uint64>(DalaranDeliveryUnlockTime));
    }

    void RecordDeathRuneDeliveryLog(
        Player* player,
        DeathRuneAltarType altarType,
        uint64 cycleId,
        uint32 deliveryRuneCount,
        uint32 totalAfter,
        uint32 mapId,
        uint32 instanceId)
    {
        if (!player || !player->GetSession())
            return;

        std::string characterName = player->GetName();
        WorldDatabase.EscapeString(characterName);

        WorldDatabase.DirectPExecute(
            "INSERT INTO `custom_death_rune_delivery_log` "
            "(`cycle_id`, `altar_type`, `map_id`, `instance_id`, "
            "`character_guid`, `account_id`, `character_name`, "
            "`delivery_count`, `total_after`, `created_at`) "
            "VALUES ({}, {}, {}, {}, {}, {}, '{}', {}, {}, CURRENT_TIMESTAMP)",
            cycleId,
            static_cast<uint32>(altarType),
            mapId,
            instanceId,
            player->GetGUID().GetCounter(),
            player->GetSession()->GetAccountId(),
            characterName,
            deliveryRuneCount,
            totalAfter);
    }

    void RecordDalaranContribution(
        Player* player,
        uint32 deliveryRuneCount,
        uint32 mapId,
        uint32 instanceId)
    {
        if (!player || !player->GetSession())
            return;

        std::string characterName = player->GetName();
        WorldDatabase.EscapeString(characterName);

        WorldDatabase.DirectPExecute(
            "INSERT INTO `custom_death_rune_delivery_contributor` "
            "(`cycle_id`, `character_guid`, `account_id`, `character_name`, "
            "`delivered_count`, `updated_at`) "
            "VALUES ({}, {}, {}, '{}', {}, CURRENT_TIMESTAMP) "
            "ON DUPLICATE KEY UPDATE "
            "`account_id` = {}, "
            "`character_name` = '{}', "
            "`delivered_count` = `delivered_count` + {}, "
            "`updated_at` = CURRENT_TIMESTAMP",
            DalaranCycleId,
            player->GetGUID().GetCounter(),
            player->GetSession()->GetAccountId(),
            characterName,
            deliveryRuneCount,
            player->GetSession()->GetAccountId(),
            characterName,
            deliveryRuneCount);

        RecordDeathRuneDeliveryLog(
            player,
            DEATH_RUNE_ALTAR_DALARAN,
            DalaranCycleId,
            deliveryRuneCount,
            DalaranDeliveredRuneCount,
            mapId,
            instanceId);

        SaveDalaranDeliveryState();
    }

    void RecordTrialContribution(
        Player* player,
        uint32 deliveryRuneCount,
        uint32 totalAfter,
        uint32 mapId,
        uint32 instanceId)
    {
        RecordDeathRuneDeliveryLog(
            player,
            DEATH_RUNE_ALTAR_TRIAL,
            0,
            deliveryRuneCount,
            totalAfter,
            mapId,
            instanceId);
    }

    bool HasDalaranRewardResult(uint64 cycleId)
    {
        return WorldDatabase.PQuery(
            "SELECT 1 "
            "FROM `custom_death_rune_delivery_reward_log` "
            "WHERE `cycle_id` = {} "
            "LIMIT 1",
            cycleId) != nullptr;
    }

    bool HasAccountReceivedDalaranRewardToday(uint32 accountId)
    {
        if (accountId == 0)
            return true;

        return WorldDatabase.PQuery(
            "SELECT 1 "
            "FROM `custom_death_rune_delivery_reward_log` "
            "WHERE `account_id` = {} "
            "AND `reward_status` = {} "
            "AND DATE(`created_at`) = CURDATE() "
            "LIMIT 1",
            accountId,
            static_cast<uint32>(DEATH_RUNE_REWARD_SENT)) != nullptr;
    }

    std::vector<DeathRuneRewardCandidate> GetDalaranRewardCandidates(uint64 cycleId)
    {
        std::vector<DeathRuneRewardCandidate> candidates;

        QueryResult accountResult = WorldDatabase.PQuery(
            "SELECT `account_id`, SUM(`delivered_count`) AS `account_delivered_count` "
            "FROM `custom_death_rune_delivery_contributor` "
            "WHERE `cycle_id` = {} "
            "GROUP BY `account_id` "
            "HAVING SUM(`delivered_count`) >= {} "
            "ORDER BY `account_id`",
            cycleId,
            MIN_REWARD_CONTRIBUTION);

        if (!accountResult)
            return candidates;

        do
        {
            Field* accountFields = accountResult->Fetch();

            uint32 const accountId = accountFields[0].GetUInt32();
            uint32 const accountDeliveredRuneCount = accountFields[1].GetUInt32();

            if (HasAccountReceivedDalaranRewardToday(accountId))
                continue;

            QueryResult contributorResult = WorldDatabase.PQuery(
                "SELECT `character_guid`, `character_name` "
                "FROM `custom_death_rune_delivery_contributor` "
                "WHERE `cycle_id` = {} "
                "AND `account_id` = {} "
                "ORDER BY `delivered_count` DESC, `updated_at` DESC, `character_guid` ASC "
                "LIMIT 1",
                cycleId,
                accountId);

            if (!contributorResult)
                continue;

            Field* contributorFields = contributorResult->Fetch();
            ObjectGuid::LowType const characterGuid =
                static_cast<ObjectGuid::LowType>(contributorFields[0].GetUInt64());

            QueryResult characterResult = CharacterDatabase.PQuery(
                "SELECT `name` "
                "FROM `characters` "
                "WHERE `guid` = {} "
                "AND `account` = {} "
                "LIMIT 1",
                characterGuid,
                accountId);

            if (!characterResult)
                continue;

            DeathRuneRewardCandidate candidate;
            candidate.CharacterGuid = characterGuid;
            candidate.AccountId = accountId;
            candidate.CharacterName = characterResult->Fetch()[0].GetString();
            candidate.DeliveredRuneCount = accountDeliveredRuneCount;
            candidates.push_back(candidate);
        }
        while (accountResult->NextRow());

        return candidates;
    }

    bool SendDalaranRewardMail(
        uint64 cycleId,
        DeathRuneRewardCandidate const& candidate,
        uint32 rewardItemId,
        uint32 senderGameObjectEntry)
    {
        if (!candidate.CharacterGuid || candidate.AccountId == 0)
            return false;

        if (!sObjectMgr->GetItemTemplate(rewardItemId))
            return false;

        Item* rewardItem = Item::CreateItem(
            rewardItemId,
            1);

        if (!rewardItem)
            return false;

        CharacterDatabaseTransaction transaction =
            CharacterDatabase.BeginTransaction();

        rewardItem->SaveToDB(transaction);

        MailDraft rewardMail(
            "죽음의 룬 공물 납품 보상",
            Trinity::StringFormat(
                "죽음의 룬 공물 주기 {}에서 계정 기준 총 {}개를 납품하여 추첨 대상이 되었으며, "
                "저형의 파편 1개를 받았습니다. 죽음의 룬 공물 파편 보상은 계정당 하루 1회만 받을 수 있습니다.",
                cycleId,
                candidate.DeliveredRuneCount));

        ObjectGuid const receiverGuid =
            ObjectGuid::Create<HighGuid::Player>(candidate.CharacterGuid);

        Player* receiver =
            ObjectAccessor::FindConnectedPlayer(receiverGuid);

        rewardMail.AddItem(rewardItem);
        rewardMail.SendMailTo(
            transaction,
            MailReceiver(receiver, candidate.CharacterGuid),
            MailSender(
                MAIL_GAMEOBJECT,
                senderGameObjectEntry,
                MAIL_STATIONERY_GM),
            MAIL_CHECK_MASK_NONE);

        CharacterDatabase.CommitTransaction(transaction);
        return true;
    }

    void SaveDalaranRewardResult(
        uint64 cycleId,
        DeathRuneRewardCandidate const* candidate,
        uint32 rewardItemId,
        DeathRuneRewardStatus rewardStatus)
    {
        std::string characterName;
        ObjectGuid::LowType characterGuid = 0;
        uint32 accountId = 0;
        uint32 deliveredRuneCount = 0;

        if (candidate)
        {
            characterGuid = candidate->CharacterGuid;
            accountId = candidate->AccountId;
            characterName = candidate->CharacterName;
            deliveredRuneCount = candidate->DeliveredRuneCount;
            WorldDatabase.EscapeString(characterName);
        }

        WorldDatabase.DirectPExecute(
            "INSERT INTO `custom_death_rune_delivery_reward_log` "
            "(`cycle_id`, `character_guid`, `account_id`, `character_name`, "
            "`delivered_count`, `reward_item_id`, `reward_count`, "
            "`reward_status`, `created_at`) "
            "VALUES ({}, {}, {}, '{}', {}, {}, 1, {}, CURRENT_TIMESTAMP) "
            "ON DUPLICATE KEY UPDATE "
            "`account_id` = {}, "
            "`character_name` = '{}', "
            "`delivered_count` = {}, "
            "`reward_item_id` = {}, "
            "`reward_count` = 1, "
            "`reward_status` = {}, "
            "`created_at` = CURRENT_TIMESTAMP",
            cycleId,
            characterGuid,
            accountId,
            characterName,
            deliveredRuneCount,
            rewardItemId,
            static_cast<uint32>(rewardStatus),
            accountId,
            characterName,
            deliveredRuneCount,
            rewardItemId,
            static_cast<uint32>(rewardStatus));
    }

    void StartNextDalaranDeliveryCycle()
    {
        ++DalaranCycleId;
        DalaranDeliveredRuneCount = 0;
        DalaranRitualActive = false;
        DalaranRitualEndTime = 0;
        DalaranDeliveryUnlockTime = 0;
        SaveDalaranDeliveryState();
    }

    void SendSystemMessage(Player* player, std::string const& message)
    {
        if (!player || !player->GetSession())
            return;

        ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
    }

    void SendServerAnnouncement(std::string const& message)
    {
        sWorld->SendServerMessage(SERVER_MSG_STRING, message.c_str(), 0);
    }


    uint32 GetGlobalDalaranRitualRemainingSeconds()
    {
        EnsureDalaranDeliveryStateLoaded();

        if (!DalaranRitualActive || DalaranRitualEndTime == 0)
            return 0;

        time_t const now = GameTime::GetGameTime();
        if (now >= DalaranRitualEndTime)
            return 0;

        return static_cast<uint32>(DalaranRitualEndTime - now);
    }

    uint32 GetGlobalDalaranDeliveryLockRemainingSeconds()
    {
        EnsureDalaranDeliveryStateLoaded();

        if (DalaranDeliveryUnlockTime == 0)
            return 0;

        time_t const now = GameTime::GetGameTime();
        if (now >= DalaranDeliveryUnlockTime)
            return 0;

        return static_cast<uint32>(DalaranDeliveryUnlockTime - now);
    }

    void SendDeathRuneAddonMessage(Player* player, std::string const& payload)
    {
        if (!player)
            return;

        player->WhisperAddon(LEGACY_RUNE_ADDON_PREFIX + "\t" + payload, player);
    }

    void BroadcastDeathRuneAddonMessage(std::string const& payload)
    {
        for (auto const& sessionPair : sWorld->GetAllSessions())
        {
            WorldSession* session = sessionPair.second;
            if (!session)
                continue;

            Player* player = session->GetPlayer();
            if (!player)
                continue;

            SendDeathRuneAddonMessage(player, payload);
        }
    }

    void SendDalaranTimerState(Player* player)
    {
        if (!player)
            return;

        uint32 const remainingSeconds = GetGlobalDalaranRitualRemainingSeconds();
        if (DalaranRitualActive && remainingSeconds > 0)
        {
            SendDeathRuneAddonMessage(
                player,
                "DEATHRUNE:TIMER:STATE:ACTIVE:" + std::to_string(remainingSeconds));
            return;
        }

        SendDeathRuneAddonMessage(player, "DEATHRUNE:TIMER:STATE:INACTIVE:0");
    }

    bool GetDeathRuneAddonPayload(std::string const& message, std::string& payload)
    {
        std::string const prefix = LEGACY_RUNE_ADDON_PREFIX + "\t";
        if (message.compare(0, prefix.size(), prefix) != 0)
            return false;

        payload = message.substr(prefix.size());
        return !payload.empty();
    }

    bool TryHandleDeathRuneTimerAddonPacket(WorldSession* session, WorldPacket& packet)
    {
        if (!session || packet.GetOpcode() != CMSG_MESSAGECHAT)
            return false;

        Player* player = session->GetPlayer();
        if (!player)
            return false;

        WorldPacket data(packet);
        data.rpos(0);

        uint32 chatType = 0;
        uint32 language = 0;
        data >> chatType;
        data >> language;

        if (language != LANG_ADDON || chatType != CHAT_MSG_WHISPER)
            return false;

        std::string targetOrChannel;
        data >> targetOrChannel;

        if (!targetOrChannel.empty() && targetOrChannel != player->GetName())
            return false;

        std::string message;
        data >> message;

        std::string payload;
        if (!GetDeathRuneAddonPayload(message, payload))
            return false;

        if (payload != DEATH_RUNE_TIMER_QUERY_COMMAND)
            return false;

        SendDalaranTimerState(player);
        return true;
    }

    bool IsDeliveryBuffConfigured()
    {
        return sSpellMgr->GetSpellInfo(SPELL_DEATH_RUNE_TOC_BUFF) != nullptr;
    }

    void RemoveDeliveryBuff(Player* player)
    {
        if (!player)
            return;

        player->RemoveAurasDueToSpell(SPELL_DEATH_RUNE_TOC_BUFF);
    }
}

struct go_death_rune_delivery_toc : public GameObjectAI
{
    explicit go_death_rune_delivery_toc(GameObject* gameObject)
        : GameObjectAI(gameObject),
          _deliveredRuneCount(0),
          _updateTimer(GO_UPDATE_INTERVAL_MS),
          _hiddenForEncounter(false),
          _normalPhaseMask(gameObject->GetPhaseMask())
    {
        EnsureDalaranDeliveryStateLoaded();
        UpdateEncounterVisibility(true);
    }

    bool OnGossipHello(Player* player) override
    {
        if (!CanUseAltar(player, true))
            return true;

        BuildDeliveryMenu(player);
        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        if (!player)
            return true;

        uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

        ClearGossipMenuFor(player);

        if (!CanUseAltar(player, true))
        {
            CloseGossipMenuFor(player);
            return true;
        }

        switch (action)
        {
            case ACTION_DELIVER_ONE:
                DeliverRunes(player, 1);
                break;

            case ACTION_DELIVER_TEN:
                DeliverRunes(player, 10);
                break;

            case ACTION_DELIVER_REMAINING:
                DeliverRunes(player, GetRemainingRuneCount());
                break;

            case ACTION_REFRESH:
                break;

            default:
                CloseGossipMenuFor(player);
                return true;
        }

        if (CanUseAltar(player, false))
            BuildDeliveryMenu(player);
        else
            CloseGossipMenuFor(player);

        return true;
    }

    void UpdateAI(uint32 diff) override
    {
        if (_updateTimer > diff)
        {
            _updateTimer -= diff;
            return;
        }

        _updateTimer = GO_UPDATE_INTERVAL_MS;

        UpdateEncounterVisibility(false);
        UpdateDalaranRitual();
        UpdateDalaranDeliveryLock();
        RemoveBuffFromDeadPlayers();
    }

private:
    bool IsDalaranAltar() const
    {
        if (!me)
            return false;

        return me->GetMapId() == MAP_NORTHREND && me->GetZoneId() == ZONE_DALARAN;
    }

    bool IsTrialAltar() const
    {
        return me && me->GetMapId() == MAP_TRIAL_OF_THE_CRUSADER;
    }

    bool CanUseAltar(Player* player, bool sendReason) const
    {
        if (!player || !me)
            return false;

        bool const isDalaranAltar = IsDalaranAltar();
        bool const isTrialAltar = IsTrialAltar();

        if (!isDalaranAltar && !isTrialAltar)
        {
            if (sendReason)
                SendSystemMessage(player, "이 위치에서는 죽음의 룬 제단을 사용할 수 없습니다.");

            return false;
        }

        if (player->GetMapId() != me->GetMapId())
            return false;

        if (!player->IsAlive())
        {
            if (sendReason)
                SendSystemMessage(player, "죽은 상태에서는 죽음의 룬을 납품할 수 없습니다.");

            return false;
        }

        if (!IsDeliveryBuffConfigured())
        {
            if (sendReason)
                SendSystemMessage(player, "죽음의 룬 버프 주문 900204를 찾을 수 없습니다.");

            return false;
        }

        if (isDalaranAltar)
        {
            if (DalaranRitualActive)
            {
                if (sendReason)
                    SendSystemMessage(player, Trinity::StringFormat(
                        "달라란 공물 의식이 진행 중입니다. 버프 발동까지 약 {}초 남았습니다.",
                        GetGlobalDalaranRitualRemainingSeconds()));

                return false;
            }

            uint32 const lockRemainingSeconds =
                GetGlobalDalaranDeliveryLockRemainingSeconds();

            if (lockRemainingSeconds > 0)
            {
                if (sendReason)
                    SendSystemMessage(player, Trinity::StringFormat(
                        "죽음의 룬 축복이 발동된 상태입니다. 다음 공물 납품까지 약 {}분 남았습니다.",
                        (lockRemainingSeconds + 59) / 60));

                return false;
            }

            return true;
        }

        Group* group = player->GetGroup();
        if (!group || !group->isRaidGroup())
        {
            if (sendReason)
                SendSystemMessage(player, "공격대를 구성한 상태에서만 죽음의 룬을 납품할 수 있습니다.");

            return false;
        }

        if (_hiddenForEncounter || IsEncounterInProgress())
        {
            if (sendReason)
                SendSystemMessage(player, "전투가 진행 중이므로 죽음의 룬 제단을 사용할 수 없습니다.");

            return false;
        }

        return true;
    }

    bool IsEncounterInProgress() const
    {
        if (!IsTrialAltar())
            return false;

        if (InstanceScript* instance = me->GetInstanceScript())
            return instance->IsEncounterInProgress();

        return false;
    }

    uint32 GetDeliveredRuneCount() const
    {
        return IsDalaranAltar() ? DalaranDeliveredRuneCount : _deliveredRuneCount;
    }

    uint32 GetRemainingRuneCount() const
    {
        uint32 const deliveredRuneCount = GetDeliveredRuneCount();

        if (deliveredRuneCount >= REQUIRED_RUNE_COUNT)
            return 0;

        return REQUIRED_RUNE_COUNT - deliveredRuneCount;
    }

    void BuildDeliveryMenu(Player* player)
    {
        if (!player || !me)
            return;

        ClearGossipMenuFor(player);

        uint32 const ownedRuneCount = player->GetItemCount(ITEM_NECROTIC_RUNE, false);
        uint32 const deliveredRuneCount = GetDeliveredRuneCount();
        uint32 const remainingRuneCount = GetRemainingRuneCount();

        SendSystemMessage(player, Trinity::StringFormat(
            "죽음의 룬 납품 현황: {}/{}개, 남은 수량: {}개, 보유 수량: {}개",
            deliveredRuneCount,
            REQUIRED_RUNE_COUNT,
            remainingRuneCount,
            ownedRuneCount));

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "죽음의 룬 1개 납품", GOSSIP_SENDER_MAIN, ACTION_DELIVER_ONE);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "죽음의 룬 10개 납품", GOSSIP_SENDER_MAIN, ACTION_DELIVER_TEN);
        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            Trinity::StringFormat("목표까지 필요한 죽음의 룬 {}개 납품", remainingRuneCount),
            GOSSIP_SENDER_MAIN,
            ACTION_DELIVER_REMAINING
        );
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "납품 현황 새로고침", GOSSIP_SENDER_MAIN, ACTION_REFRESH);

        SendGossipMenuFor(player, 1, me->GetGUID());
    }

    void DeliverRunes(Player* player, uint32 requestedRuneCount)
    {
        if (!player || requestedRuneCount == 0)
            return;

        uint32 const remainingRuneCount = GetRemainingRuneCount();
        uint32 const ownedRuneCount = player->GetItemCount(ITEM_NECROTIC_RUNE, false);
        uint32 const deliveryRuneCount = std::min(requestedRuneCount, std::min(remainingRuneCount, ownedRuneCount));

        if (deliveryRuneCount == 0)
        {
            SendSystemMessage(player, "납품할 수 있는 죽음의 룬이 없습니다.");
            return;
        }

        if (IsDalaranAltar())
        {
            DeliverDalaranRunes(player, deliveryRuneCount);
            return;
        }

        DeliverTrialRunes(player, deliveryRuneCount, remainingRuneCount);
    }

    void DeliverDalaranRunes(Player* player, uint32 deliveryRuneCount)
    {
        if (!player ||
            DalaranRitualActive ||
            GetGlobalDalaranDeliveryLockRemainingSeconds() > 0)
        {
            return;
        }

        player->DestroyItemCount(ITEM_NECROTIC_RUNE, deliveryRuneCount, true);
        DalaranDeliveredRuneCount += deliveryRuneCount;

        if (DalaranDeliveredRuneCount >= REQUIRED_RUNE_COUNT)
        {
            DalaranDeliveredRuneCount = REQUIRED_RUNE_COUNT;
            DalaranRitualActive = true;
            DalaranRitualEndTime = GameTime::GetGameTime() + DALARAN_RITUAL_DELAY_SEC;
        }

        RecordDalaranContribution(
            player,
            deliveryRuneCount,
            me->GetMapId(),
            me->GetInstanceId());

        SendServerAnnouncement(Trinity::StringFormat(
            "|cffC41F3B[죽음의 룬 공물]|r {}님이 달라란 제단에 죽음의 룬 {}개를 바쳤습니다. 현재 {}/{}개입니다.",
            player->GetName(),
            deliveryRuneCount,
            DalaranDeliveredRuneCount,
            REQUIRED_RUNE_COUNT));

        if (!DalaranRitualActive)
            return;

        SendServerAnnouncement(
            "|cffC41F3B[죽음의 룬 공물]|r 달라란 제단의 공물 100개가 완성되었습니다. "
            "5분 뒤 제단 30야드 안의 살아있는 용사들에게 2시간 동안 죽음의 룬 축복이 적용됩니다. "
            "계정 기준 20개 이상 납품했으며 오늘 공물 파편 보상을 받지 않은 계정 중 1명을 추첨하여 "
            "저형의 파편 1개를 우편으로 지급합니다.");

        BroadcastDeathRuneAddonMessage(
            "DEATHRUNE:TIMER:START:" + std::to_string(DALARAN_RITUAL_DELAY_SEC));
    }

    void DeliverTrialRunes(Player* player, uint32 deliveryRuneCount, uint32 remainingRuneCount)
    {
        if (!player)
            return;

        Group* group = player->GetGroup();
        if (!group || !group->isRaidGroup())
            return;

        bool const completesDelivery = deliveryRuneCount >= remainingRuneCount;
        if (completesDelivery && CountEligibleRaidMembers(group) == 0)
        {
            SendSystemMessage(player, "30야드 안에 버프를 받을 수 있는 살아있는 공격대원이 없습니다.");
            return;
        }

        player->DestroyItemCount(ITEM_NECROTIC_RUNE, deliveryRuneCount, true);
        _deliveredRuneCount += deliveryRuneCount;

        RecordTrialContribution(
            player,
            deliveryRuneCount,
            _deliveredRuneCount,
            me->GetMapId(),
            me->GetInstanceId());

        SendRaidProgressMessage(group, Trinity::StringFormat(
            "{}님이 죽음의 룬 {}개를 납품했습니다. 현재 {}/{}개입니다.",
            player->GetName(),
            deliveryRuneCount,
            _deliveredRuneCount,
            REQUIRED_RUNE_COUNT));

        if (_deliveredRuneCount < REQUIRED_RUNE_COUNT)
            return;

        _deliveredRuneCount = 0;

        uint32 const buffedPlayerCount = ApplyBuffToEligibleRaidMembers(group);

        SendRaidProgressMessage(group, Trinity::StringFormat(
            "죽음의 룬 납품이 완료되어 제단 30야드 안의 공격대원 {}명에게 30분 버프가 적용되었습니다.",
            buffedPlayerCount));
    }

    void UpdateDalaranRitual()
    {
        if (!IsDalaranAltar() || !DalaranRitualActive)
            return;

        time_t const now = GameTime::GetGameTime();
        if (DalaranRitualEndTime == 0 || now < DalaranRitualEndTime)
            return;

        uint64 const completedCycleId = DalaranCycleId;

        DalaranRitualActive = false;
        DalaranRitualEndTime = 0;
        DalaranDeliveryUnlockTime = now + DALARAN_DELIVERY_LOCK_SEC;
        SaveDalaranDeliveryState();

        uint32 const buffedPlayerCount = ApplyBuffToNearbyDalaranPlayers();

        SendServerAnnouncement(Trinity::StringFormat(
            "|cffC41F3B[죽음의 룬 공물]|r 달라란 제단의 공물 의식이 발동했습니다. "
            "제단 30야드 안의 용사 {}명에게 2시간 동안 죽음의 룬 축복이 적용되었습니다. "
            "축복이 유지되는 2시간 동안 달라란 제단에는 추가 공물을 납품할 수 없습니다.",
            buffedPlayerCount));

        if (!HasDalaranRewardResult(completedCycleId))
        {
            std::vector<DeathRuneRewardCandidate> const rewardCandidates =
                GetDalaranRewardCandidates(completedCycleId);

            if (rewardCandidates.empty())
            {
                SaveDalaranRewardResult(
                    completedCycleId,
                    nullptr,
                    0,
                    DEATH_RUNE_REWARD_NO_ELIGIBLE_CONTRIBUTOR);

                SendServerAnnouncement(
                    "|cffC41F3B[죽음의 룬 공물]|r 계정 기준 20개 이상 납품했으며 "
                    "오늘 공물 파편 보상을 받지 않은 대상이 없어 저형의 파편 추첨이 진행되지 않았습니다.");
            }
            else
            {
                DeathRuneRewardCandidate const& rewardCandidate =
                    rewardCandidates[urand(0, static_cast<uint32>(rewardCandidates.size() - 1))];

                uint32 const rewardItemId =
                    SelectRandomTransformationShardReward();

                if (SendDalaranRewardMail(
                        completedCycleId,
                        rewardCandidate,
                        rewardItemId,
                        me->GetEntry()))
                {
                    SaveDalaranRewardResult(
                        completedCycleId,
                        &rewardCandidate,
                        rewardItemId,
                        DEATH_RUNE_REWARD_SENT);

                    SendServerAnnouncement(Trinity::StringFormat(
                        "|cffC41F3B[죽음의 룬 공물]|r 계정 기준 {}개를 납품한 {}님이 공물 추첨에 선정되어 "
                        "저형의 파편 1개를 우편으로 받았습니다.",
                        rewardCandidate.DeliveredRuneCount,
                        rewardCandidate.CharacterName));
                }
                else
                {
                    SaveDalaranRewardResult(
                        completedCycleId,
                        &rewardCandidate,
                        rewardItemId,
                        DEATH_RUNE_REWARD_FAILED);

                    SendServerAnnouncement(
                        "|cffC41F3B[죽음의 룬 공물]|r 저형의 파편 우편 발송에 실패했습니다. "
                        "관리자 확인이 필요합니다.");
                }
            }
        }

        BroadcastDeathRuneAddonMessage("DEATHRUNE:TIMER:END");
    }

    void UpdateDalaranDeliveryLock()
    {
        if (!IsDalaranAltar() || DalaranDeliveryUnlockTime == 0)
            return;

        if (GameTime::GetGameTime() < DalaranDeliveryUnlockTime)
            return;

        StartNextDalaranDeliveryCycle();

        SendServerAnnouncement(
            "|cffC41F3B[죽음의 룬 공물]|r 죽음의 룬 축복 유지 시간이 종료되어 "
            "달라란 제단에 다시 공물을 납품할 수 있습니다.");
    }

    uint32 ApplyBuffToNearbyDalaranPlayers()
    {
        if (!me || !me->GetMap())
            return 0;

        uint32 buffedPlayerCount = 0;
        Map::PlayerList const& playerList = me->GetMap()->GetPlayers();

        for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
        {
            Player* player = itr->GetSource();
            if (!IsEligibleNearbyPlayer(player))
                continue;

            if (ApplyDeliveryBuff(player, DALARAN_BUFF_DURATION_MS))
                ++buffedPlayerCount;
        }

        return buffedPlayerCount;
    }

    uint32 CountEligibleRaidMembers(Group* group) const
    {
        if (!group || !me || !me->GetMap())
            return 0;

        uint32 eligibleCount = 0;
        Map::PlayerList const& playerList = me->GetMap()->GetPlayers();

        for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
        {
            Player* member = itr->GetSource();
            if (!IsEligibleRaidMember(member, group))
                continue;

            ++eligibleCount;
        }

        return eligibleCount;
    }

    uint32 ApplyBuffToEligibleRaidMembers(Group* group)
    {
        if (!group || !me || !me->GetMap())
            return 0;

        uint32 buffedPlayerCount = 0;
        Map::PlayerList const& playerList = me->GetMap()->GetPlayers();

        for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
        {
            Player* member = itr->GetSource();
            if (!IsEligibleRaidMember(member, group))
                continue;

            if (ApplyDeliveryBuff(member, TRIAL_BUFF_DURATION_MS))
                ++buffedPlayerCount;
        }

        return buffedPlayerCount;
    }

    bool ApplyDeliveryBuff(Player* player, uint32 durationMs) const
    {
        if (!player)
            return false;

        player->CastSpell(player, SPELL_DEATH_RUNE_TOC_BUFF, false);

        Aura* aura = player->GetAura(SPELL_DEATH_RUNE_TOC_BUFF);
        if (!aura)
            return false;

        aura->SetMaxDuration(durationMs);
        aura->SetDuration(durationMs);

        if (durationMs == DALARAN_BUFF_DURATION_MS)
            SendSystemMessage(player, "죽음의 룬 공물 버프가 2시간 동안 적용되었습니다. 죽으면 사라집니다.");
        else
            SendSystemMessage(player, "죽음의 룬 공물 버프가 30분 동안 적용되었습니다. 죽으면 사라집니다.");
        return true;
    }

    bool IsEligibleNearbyPlayer(Player* player) const
    {
        if (!player || !me)
            return false;

        if (!player->IsAlive())
            return false;

        if (player->GetDistance(me) > BUFF_RADIUS)
            return false;

        return true;
    }

    bool IsEligibleRaidMember(Player* member, Group* group) const
    {
        if (!member || !group || !me)
            return false;

        if (member->GetGroup() != group)
            return false;

        return IsEligibleNearbyPlayer(member);
    }

    void SendRaidProgressMessage(Group* group, std::string const& message) const
    {
        if (!group || !me || !me->GetMap())
            return;

        Map::PlayerList const& playerList = me->GetMap()->GetPlayers();

        for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
        {
            Player* member = itr->GetSource();
            if (!member || member->GetGroup() != group)
                continue;

            SendSystemMessage(member, message);
        }
    }

    void UpdateEncounterVisibility(bool force)
    {
        if (!me)
            return;

        bool const shouldHide = IsTrialAltar() && IsEncounterInProgress();

        if (!force && shouldHide == _hiddenForEncounter)
            return;

        _hiddenForEncounter = shouldHide;
        me->SetPhaseMask(shouldHide ? 0 : _normalPhaseMask, true);
    }

    void RemoveBuffFromDeadPlayers() const
    {
        if (!me || !me->GetMap())
            return;

        Map::PlayerList const& playerList = me->GetMap()->GetPlayers();

        for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
        {
            Player* player = itr->GetSource();
            if (!player || player->IsAlive())
                continue;

            RemoveDeliveryBuff(player);
        }
    }

    uint32 _deliveredRuneCount;
    uint32 _updateTimer;
    bool _hiddenForEncounter;
    uint32 _normalPhaseMask;
};

class player_death_rune_delivery_toc : public PlayerScript
{
public:
    player_death_rune_delivery_toc() : PlayerScript("player_death_rune_delivery_toc") { }

    void OnPVPKill(Player* /*killer*/, Player* killed) override
    {
        RemoveDeliveryBuff(killed);
    }

    void OnPlayerKilledByCreature(Creature* /*killer*/, Player* killed) override
    {
        RemoveDeliveryBuff(killed);
    }

    void OnPlayerRepop(Player* player) override
    {
        RemoveDeliveryBuff(player);
    }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player)
            return;

        if (!player->IsAlive())
            RemoveDeliveryBuff(player);

        SendDalaranTimerState(player);
    }
};

class death_rune_delivery_world_script : public WorldScript
{
public:
    death_rune_delivery_world_script()
        : WorldScript("death_rune_delivery_world_script")
    {
    }

    void OnStartup() override
    {
        LoadDalaranDeliveryState();
    }
};

class death_rune_delivery_server_script : public ServerScript
{
public:
    death_rune_delivery_server_script()
        : ServerScript("death_rune_delivery_server_script")
    {
    }

    void OnPacketReceive(WorldSession* session, WorldPacket& packet) override
    {
        TryHandleDeathRuneTimerAddonPacket(session, packet);
    }
};

void AddSC_custom_death_rune_delivery()
{
    RegisterGameObjectAI(go_death_rune_delivery_toc);
    new player_death_rune_delivery_toc();
    new death_rune_delivery_world_script();
    new death_rune_delivery_server_script();
}