#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "Chat.h"
#include "ObjectMgr.h"
#include "World.h"

#include <array>
#include <cstddef>
#include <sstream>
#include <string>

namespace
{
    constexpr uint32 NPC_TEXT_ID = 68;

    constexpr uint32 ITEM_MC_FRAGMENT = 23186;
    constexpr uint32 ITEM_BWL_FRAGMENT = 25704;

    constexpr uint32 ACTION_MAIN_MENU = 100;
    constexpr uint32 ACTION_MC_MENU = 200;
    constexpr uint32 ACTION_BWL_MENU = 300;
    constexpr uint32 ACTION_CHECK_FRAGMENT = 400;

    constexpr uint32 ACTION_MC_REWARD_BASE = 10000;
    constexpr uint32 ACTION_BWL_REWARD_BASE = 20000;

    struct RetroExchangeReward
    {
        uint32 ItemId;
        uint32 CostItemId;
        uint32 CostCount;
        char const* Label;
    };

    std::array<RetroExchangeReward, 11> const McRewards =
	{{
		{ 23705, ITEM_MC_FRAGMENT, 100, "TCG 휘장 보상" },
		{ 23709, ITEM_MC_FRAGMENT, 100, "TCG 휘장 보상" },
		{ 32588, ITEM_MC_FRAGMENT, 200, "TCG 펫 보상" },
		{ 23713, ITEM_MC_FRAGMENT, 200, "TCG 펫 보상" },
		{ 34492, ITEM_MC_FRAGMENT, 200, "TCG 펫 보상" },
		{ 34493, ITEM_MC_FRAGMENT, 200, "TCG 펫 보상" },
		{ 35223, ITEM_MC_FRAGMENT, 150, "TCG 장난감 보상" },
		{ 35227, ITEM_MC_FRAGMENT, 150, "TCG 장난감 보상" },

		{ 23720, ITEM_MC_FRAGMENT, 500, "TCG 탈것 보상" },
		{ 38576, ITEM_MC_FRAGMENT, 700, "TCG 탈것 보상" },
		{ 46778, ITEM_MC_FRAGMENT, 800, "TCG 탈것 보상" }
	}};

    std::array<RetroExchangeReward, 12> const BwlRewards =
	{{
		{ 33224, ITEM_BWL_FRAGMENT, 1000, "TCG 탈것 보상" },
		{ 33225, ITEM_BWL_FRAGMENT, 1000, "TCG 탈것 보상" },
		{ 35225, ITEM_BWL_FRAGMENT, 1000, "TCG 탈것 보상" },
		{ 35226, ITEM_BWL_FRAGMENT, 1000, "TCG 탈것 보상" },
		{ 49290, ITEM_BWL_FRAGMENT, 1200, "고급 TCG 탈것 보상" },
		{ 39286, ITEM_BWL_FRAGMENT, 500, "컬렉터즈 펫 보상" },
		{ 43599, ITEM_BWL_FRAGMENT, 1800, "블리즈컨 보상" },
		{ 49662, ITEM_BWL_FRAGMENT, 800, "계정 프로모션 보상" },

		{ 49282, ITEM_BWL_FRAGMENT, 1000, "TCG 탈것 보상" },
		{ 49285, ITEM_BWL_FRAGMENT, 1000, "TCG 탈것 보상" },
		{ 49286, ITEM_BWL_FRAGMENT, 1000, "TCG 탈것 보상" },
		{ 54069, ITEM_BWL_FRAGMENT, 1400, "고급 TCG 탈것 보상" }
	}};

    void SendRetroMessage(Player* player, std::string const& message)
    {
        if (!player || !player->GetSession())
            return;

        ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
    }

    std::string GetRewardItemName(uint32 itemId)
    {
        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);

        if (!itemTemplate)
        {
            std::ostringstream ss;
            ss << "알 수 없는 아이템(" << itemId << ")";
            return ss.str();
        }

        return itemTemplate->Name1;
    }

    std::string GetItemQualityColor(uint32 quality)
    {
        switch (quality)
        {
            case 0: return "|cff9d9d9d";
            case 1: return "|cffffffff";
            case 2: return "|cff1eff00";
            case 3: return "|cff0070dd";
            case 4: return "|cffa335ee";
            case 5: return "|cffff8000";
            case 6: return "|cffe6cc80";
            case 7: return "|cffe6cc80";
            default: return "|cffffffff";
        }
    }

    std::string BuildItemLink(uint32 itemId)
    {
        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);

        if (!itemTemplate)
            return "알 수 없는 아이템";

        std::ostringstream ss;

        ss << GetItemQualityColor(itemTemplate->Quality)
           << "|Hitem:"
           << itemId
           << ":0:0:0:0:0:0:0:0|h["
           << itemTemplate->Name1
           << "]|h|r";

        return ss.str();
    }

    void SendRetroWorldMessage(std::string const& message)
    {
        sWorld->SendGlobalText(message.c_str(), nullptr);
    }

    std::string BuildRewardLabel(RetroExchangeReward const& reward)
    {
        std::ostringstream ss;

        ss << GetRewardItemName(reward.ItemId)
           << " | 필요 조각 "
           << reward.CostCount
           << "개";

        return ss.str();
    }

    void ShowMainMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "화심 보상 교환", GOSSIP_SENDER_MAIN, ACTION_MC_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "검둥 보상 교환", GOSSIP_SENDER_MAIN, ACTION_BWL_MENU);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "내 조각 개수 확인", GOSSIP_SENDER_MAIN, ACTION_CHECK_FRAGMENT);

        SendGossipMenuFor(player, NPC_TEXT_ID, creature->GetGUID());
    }

    void ShowMcMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        for (std::size_t i = 0; i < McRewards.size(); ++i)
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_MONEY_BAG,
                BuildRewardLabel(McRewards[i]),
                GOSSIP_SENDER_MAIN,
                ACTION_MC_REWARD_BASE + uint32(i)
            );
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뒤로가기", GOSSIP_SENDER_MAIN, ACTION_MAIN_MENU);

        SendGossipMenuFor(player, NPC_TEXT_ID, creature->GetGUID());
    }

    void ShowBwlMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        for (std::size_t i = 0; i < BwlRewards.size(); ++i)
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_MONEY_BAG,
                BuildRewardLabel(BwlRewards[i]),
                GOSSIP_SENDER_MAIN,
                ACTION_BWL_REWARD_BASE + uint32(i)
            );
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뒤로가기", GOSSIP_SENDER_MAIN, ACTION_MAIN_MENU);

        SendGossipMenuFor(player, NPC_TEXT_ID, creature->GetGUID());
    }

    void ShowFragmentCount(Player* player, Creature* creature)
    {
        uint32 mcCount = player->GetItemCount(ITEM_MC_FRAGMENT, true);
        uint32 bwlCount = player->GetItemCount(ITEM_BWL_FRAGMENT, true);

        std::ostringstream ss;

        ss << "|cff33ff99[레트로 수집]|r 보유 조각 - "
           << "화심의 잔불 조각: "
           << mcCount
           << "개 / 검은날개 비늘 조각: "
           << bwlCount
           << "개";

        SendRetroMessage(player, ss.str());
        ShowMainMenu(player, creature);
    }

    void ExchangeReward(Player* player, Creature* creature, RetroExchangeReward const& reward)
    {
        if (!player)
            return;

        if (!player->HasItemCount(reward.CostItemId, reward.CostCount, true))
        {
            std::ostringstream ss;

            ss << "|cffff5555[레트로 수집]|r 조각이 부족합니다. 필요 조각: "
               << reward.CostCount
               << "개";

            SendRetroMessage(player, ss.str());
            ShowMainMenu(player, creature);
            return;
        }

        if (!player->AddItem(reward.ItemId, 1))
        {
            SendRetroMessage(player, "|cffff5555[레트로 수집]|r 가방 공간이 부족하거나 아이템을 지급할 수 없습니다.");
            ShowMainMenu(player, creature);
            return;
        }

        player->DestroyItemCount(reward.CostItemId, reward.CostCount, true);

        std::ostringstream ss;

        ss << "|cff33ff99[레트로 수집]|r "
           << BuildItemLink(reward.ItemId)
           << " 아이템을 교환했습니다.";

        SendRetroWorldMessage(ss.str());
        ShowMainMenu(player, creature);
    }
}

class npc_retro_collection_exchange : public CreatureScript
{
public:
    npc_retro_collection_exchange() : CreatureScript("npc_retro_collection_exchange") { }

    struct npc_retro_collection_exchangeAI : public ScriptedAI
    {
        explicit npc_retro_collection_exchangeAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            ShowMainMenu(player, me);
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

            ClearGossipMenuFor(player);

            if (action == ACTION_MAIN_MENU)
            {
                ShowMainMenu(player, me);
                return true;
            }

            if (action == ACTION_MC_MENU)
            {
                ShowMcMenu(player, me);
                return true;
            }

            if (action == ACTION_BWL_MENU)
            {
                ShowBwlMenu(player, me);
                return true;
            }

            if (action == ACTION_CHECK_FRAGMENT)
            {
                ShowFragmentCount(player, me);
                return true;
            }

            if (action >= ACTION_MC_REWARD_BASE && action < ACTION_MC_REWARD_BASE + McRewards.size())
            {
                uint32 index = action - ACTION_MC_REWARD_BASE;
                ExchangeReward(player, me, McRewards[index]);
                return true;
            }

            if (action >= ACTION_BWL_REWARD_BASE && action < ACTION_BWL_REWARD_BASE + BwlRewards.size())
            {
                uint32 index = action - ACTION_BWL_REWARD_BASE;
                ExchangeReward(player, me, BwlRewards[index]);
                return true;
            }

            ShowMainMenu(player, me);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_retro_collection_exchangeAI(creature);
    }
};

void AddSC_npc_retro_collection_exchange()
{
    new npc_retro_collection_exchange();
}