#include "CustomArenaReward.h"

#include "Battleground.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"
#include "fmt/format.h"

#include <ctime>
#include <string>
#include <unordered_set>
#include <vector>

namespace CustomArenaReward
{
    // 실제 아이템 ID로 교체
    static constexpr uint32 ITEM_ARENA_TOKEN        = 3438; // 투기장 증표
    static constexpr uint32 ITEM_SEASON8_BATTLE_BOX = 23656; // 시즌 8 전투상자   랜덤 탈것임 교체요망
    static constexpr uint32 ITEM_SEASON8_HIGH_BOX   = 22582; // 시즌 8 고급 전투상자   랜덤 탈것임 교체요망

    // 우편 발송자: 우체국장
    static constexpr uint32 MAIL_SENDER_ENTRY = 34337;

    struct RewardData
    {
        uint32 joinRp;
        uint32 winRp;
        uint32 firstWinRp;

        uint32 joinToken;
        uint32 winToken;
        uint32 firstWinToken;

        uint32 firstWinBoxItem;
        uint32 firstWinBoxCount;
    };

    struct MailRewardItem
    {
        uint32 itemEntry;
        uint32 count;
    };

    static std::unordered_set<std::string> DailyFirstWinCache;
    static std::string CurrentDateText;

    static std::string GetTodayText()
    {
        std::time_t now = std::time(nullptr);
        std::tm localTime = *std::localtime(&now);

        char dateText[16];
        std::strftime(dateText, sizeof(dateText), "%Y%m%d", &localTime);

        return std::string(dateText);
    }

    static void RefreshDailyCacheIfNeeded()
    {
        std::string today = GetTodayText();

        if (CurrentDateText == today)
            return;

        CurrentDateText = today;
        DailyFirstWinCache.clear();
    }

    static std::string GetDailyFirstWinKey(uint32 accountId, uint8 arenaType)
    {
        RefreshDailyCacheIfNeeded();

        return Trinity::StringFormat("{}:{}:{}", accountId, uint32(arenaType), CurrentDateText);
    }

    static RewardData GetRewardData(uint8 arenaType)
    {
        switch (arenaType)
        {
            // 1:1 연습 투기장
            // RP를 지급하지 않으려면 앞의 3개 값을 0, 0, 0으로 되돌리면 된다.
            case 5:
                return RewardData
                {
                    0,50, 100,
                    1, 2, 5,
                    0, 0
                };

            // 2:2 메인 투기장
            case 2:
                return RewardData
                {
                    500, 1500, 8000,
                    2, 5, 0,
                    ITEM_SEASON8_BATTLE_BOX, 2
                };

            // 3:3 최고 보상
            case 3:
                return RewardData
                {
                    800, 2500, 12000,
                    3, 8, 0,
                    ITEM_SEASON8_HIGH_BOX, 1
                };

            default:
                return RewardData
                {
                    0, 0, 0,
                    0, 0, 0,
                    0, 0
                };
        }
    }

    static void Send(Player* player, std::string const& message)
    {
        if (!player || !player->GetSession())
            return;

        ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
    }

    static void AddRP(Player* player, uint32 amount)
    {
        if (!player || amount == 0 || !player->GetSession())
            return;

        uint32 accountId = player->GetSession()->GetAccountId();

        if (accountId == 0)
            return;

        LoginDatabase.PExecute(
            "INSERT INTO appdata.account_data (id, reg_point) "
            "VALUES ({}, {}) "
            "ON DUPLICATE KEY UPDATE reg_point = reg_point + {}",
            accountId,
            amount,
            amount
        );
    }

    static bool SendRewardMail(Player* player, std::string const& subject, std::string const& body, std::vector<MailRewardItem> const& items)
    {
        if (!player)
            return false;

        bool hasItem = false;

        for (MailRewardItem const& rewardItem : items)
        {
            if (rewardItem.itemEntry != 0 && rewardItem.count != 0)
            {
                hasItem = true;
                break;
            }
        }

        if (!hasItem)
            return false;

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        MailSender sender(MAIL_CREATURE, MAIL_SENDER_ENTRY);
        MailDraft draft(subject, body);

        bool attached = false;

        for (MailRewardItem const& rewardItem : items)
        {
            if (rewardItem.itemEntry == 0 || rewardItem.count == 0)
                continue;

            Item* item = Item::CreateItem(rewardItem.itemEntry, rewardItem.count, player);

            if (!item)
                continue;

            item->SaveToDB(trans);
            draft.AddItem(item);
            attached = true;
        }

        if (!attached)
            return false;

        draft.SendMailTo(
            trans,
            MailReceiver(player, player->GetGUID().GetCounter()),
            sender
        );

        CharacterDatabase.CommitTransaction(trans);

        if (player->GetSession())
            player->SendNewMail();

        return true;
    }

    static bool TryGiveDailyFirstWin(Player* player, uint8 arenaType)
    {
        if (!player || !player->GetSession())
            return false;

        uint32 accountId = player->GetSession()->GetAccountId();

        if (accountId == 0)
            return false;

        std::string key = GetDailyFirstWinKey(accountId, arenaType);

        if (DailyFirstWinCache.find(key) != DailyFirstWinCache.end())
            return false;

        DailyFirstWinCache.insert(key);
        return true;
    }

    static bool HasRewardData(RewardData const& reward)
    {
        return reward.joinRp > 0
            || reward.winRp > 0
            || reward.firstWinRp > 0
            || reward.joinToken > 0
            || reward.winToken > 0
            || reward.firstWinToken > 0
            || reward.firstWinBoxItem > 0;
    }

    static void RewardPlayer(Player* player, uint8 arenaType, bool win)
    {
        if (!player || !player->GetSession())
            return;

        RewardData reward = GetRewardData(arenaType);

        if (!HasRewardData(reward))
            return;

        // 참여 보상
        AddRP(player, reward.joinRp);

        SendRewardMail(
            player,
            "투기장 참여 보상",
            Trinity::StringFormat(
                "투기장 참여 보상입니다.$B$BRP {}점이 지급되었습니다.$B첨부된 아이템을 확인해주세요.",
                reward.joinRp
            ),
            {
                { ITEM_ARENA_TOKEN, reward.joinToken }
            }
        );

        if (!win)
        {
            Send(player, Trinity::StringFormat("투기장 참여 보상이 지급되었습니다. RP +{}", reward.joinRp));
            return;
        }

        // 승리 보상
        AddRP(player, reward.winRp);

        SendRewardMail(
            player,
            "투기장 승리 보상",
            Trinity::StringFormat(
                "투기장 승리 보상입니다.$B$BRP {}점이 지급되었습니다.$B첨부된 아이템을 확인해주세요.",
                reward.winRp
            ),
            {
                { ITEM_ARENA_TOKEN, reward.winToken }
            }
        );

        Send(player, Trinity::StringFormat("투기장 승리 보상이 지급되었습니다. RP +{}", reward.winRp));

        // 하루 첫 승 보상
        if (!TryGiveDailyFirstWin(player, arenaType))
            return;

        AddRP(player, reward.firstWinRp);

        SendRewardMail(
            player,
            "투기장 하루 첫 승 보상",
            Trinity::StringFormat(
                "투기장 하루 첫 승 보상입니다.$B$BRP {}점이 지급되었습니다.$B첨부된 아이템을 확인해주세요.",
                reward.firstWinRp
            ),
            {
                { ITEM_ARENA_TOKEN, reward.firstWinToken },
                { reward.firstWinBoxItem, reward.firstWinBoxCount }
            }
        );

        Send(player, Trinity::StringFormat("투기장 하루 첫 승 보상이 지급되었습니다. RP +{}", reward.firstWinRp));
    }

        void RewardArenaEnd(Battleground* bg, uint32 winner)
    {
        if (!bg)
            return;

        if (!bg->isArena())
            return;

        uint8 arenaType = bg->GetArenaType();

        if (arenaType != 5 && arenaType != 2 && arenaType != 3)
            return;

        if (winner != ALLIANCE && winner != HORDE)
            return;

        Battleground::BattlegroundPlayerMap const& players = bg->GetPlayers();

        for (auto const& pair : players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(pair.first);

            if (!player || !player->GetSession())
                continue;

            uint32 playerTeam = bg->GetPlayerTeam(pair.first);

            if (playerTeam != ALLIANCE && playerTeam != HORDE)
                continue;

            bool win = playerTeam == winner;

            RewardPlayer(player, arenaType, win);
        }
    }
}