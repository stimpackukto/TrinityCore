#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "GossipDef.h"

#include "Player.h"
#include "Creature.h"
#include "Item.h"

#include "AuctionHouseMgr.h"
#include "ObjectMgr.h"

#include "WorldSession.h"
#include "Chat.h"
#include "GameTime.h"

#include "DatabaseEnv.h"
#include "CustomRP.h"

#include "DiscordEvent.h"

#include "Config.h"

static std::unordered_set<uint32> g_BlockItemSet;
static std::unordered_set<uint32> g_MinbiditemSet;
static std::unordered_map<uint32, uint32> g_autobidauctionitemCategoryMap;

namespace AutoRareMaterialAuction
{
	static void RegisterRareMaterialAuction();
}

class npc_custom_auctioneer : public CreatureScript
{
public:
    npc_custom_auctioneer() : CreatureScript("npc_custom_auctioneer") {}

    struct npc_custom_auctioneerAI : public ScriptedAI
    {
        using GuidKey = uint64;

        std::unordered_map<GuidKey, ObjectGuid> g_SelectedItem;
        std::unordered_map<GuidKey, std::unordered_map<uint32, ObjectGuid>> g_ItemList;
        std::unordered_map<GuidKey, uint32> g_Bid;
        std::unordered_map<GuidKey, uint32> g_Deposit;

        static constexpr uint32 ACTION_REFRESH_LIST      = 900;
        static constexpr uint32 ACTION_SELECT_ITEM       = 1000;
        static constexpr uint32 ACTION_SELECT_ITEM_END   = 1100;
        static constexpr uint32 ACTION_INPUT_BID         = 2000;
        static constexpr uint32 ACTION_CONFIRM           = 3000;

        static constexpr uint8 FIXED_AUCTION_HOUSE_ID = 7;

        npc_custom_auctioneerAI(Creature* c) : ScriptedAI(c) {}

        bool IsKnownAction(uint32 action) const
        {
            if (action == ACTION_REFRESH_LIST)
                return true;

            if (action == ACTION_INPUT_BID)
                return true;

            if (action == ACTION_CONFIRM)
                return true;

            if (action >= ACTION_SELECT_ITEM && action < ACTION_SELECT_ITEM_END)
                return true;

            return false;
        }

        uint32 GetSelectedGossipAction(Player* player, uint32 gossipListId) const
        {
            if (!player)
                return 0;

            if (IsKnownAction(gossipListId))
                return gossipListId;

            return player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
        }

        bool IsBaseInventoryItem(Item* item) const
        {
            if (!item)
                return false;

            if (item->GetBagSlot() != INVENTORY_SLOT_BAG_0)
                return false;

            uint8 slot = item->GetSlot();

            if (slot < INVENTORY_SLOT_ITEM_START)
                return false;

            if (slot >= INVENTORY_SLOT_ITEM_END)
                return false;

            return true;
        }

        bool IsAuctionableInventoryItem(Item* item) const
        {
            if (!item)
                return false;

            if (!IsBaseInventoryItem(item))
                return false;

            const ItemTemplate* proto = item->GetTemplate();
            if (!proto)
                return false;

            if (proto->Quality < ITEM_QUALITY_EPIC)
                return false;

            if (proto->Class != ITEM_CLASS_WEAPON &&
                proto->Class != ITEM_CLASS_ARMOR)
                return false;

            if (!item->IsSoulBound())
                return false;

            if (item->GetUInt32Value(ITEM_FIELD_DURABILITY) <
                item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY))
            {
                return false;
            }

            if (item->GetPlayedTime() > 12000)
                return false;

            if (g_BlockItemSet.count(proto->ItemId))
                return false;

            return true;
        }

        void ClearPlayerAuctionState(Player* player)
        {
            if (!player)
                return;

            GuidKey key = player->GetGUID().GetRawValue();

            g_SelectedItem.erase(key);
            g_ItemList.erase(key);
            g_Bid.erase(key);
            g_Deposit.erase(key);
        }

        void BuildItemListMenu(Player* player)
        {
            if (!player)
                return;

            ClearGossipMenuFor(player);

            GuidKey key = player->GetGUID().GetRawValue();

            g_SelectedItem.erase(key);
            g_ItemList[key].clear();
            g_Bid.erase(key);
            g_Deposit.erase(key);

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "등록할 아이템을 선택하세요.",
                GOSSIP_SENDER_MAIN,
                ACTION_REFRESH_LIST
            );

            bool hasItem = false;
            uint32 listIndex = 0;

            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            {
                Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (!item)
                    continue;

                if (!IsAuctionableInventoryItem(item))
                    continue;

                const ItemTemplate* proto = item->GetTemplate();
                if (!proto)
                    continue;

                uint32 action = ACTION_SELECT_ITEM + listIndex;

                if (action >= ACTION_SELECT_ITEM_END)
                    break;

                g_ItemList[key][action] = item->GetGUID();

                std::ostringstream label;
                label << proto->Name1;

                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_DOT,
                    label.str(),
                    GOSSIP_SENDER_MAIN,
                    action
                );

                hasItem = true;
                ++listIndex;
            }

            if (!hasItem)
            {
                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    "등록 가능한 기본가방 아이템이 없습니다.",
                    GOSSIP_SENDER_MAIN,
                    ACTION_REFRESH_LIST
                );
            }

            SendGossipMenuFor(player, 1, me->GetGUID());
        }

        bool OnGossipHello(Player* player) override
        {
            if (!player)
                return true;

            BuildItemListMenu(player);
            return true;
        }

        bool OnGossipSelect(Player* player, uint32, uint32 gossipListId) override
        {
            if (!player)
                return true;

            uint32 action = GetSelectedGossipAction(player, gossipListId);
            ClearGossipMenuFor(player);

            GuidKey key = player->GetGUID().GetRawValue();

            if (action == ACTION_REFRESH_LIST)
            {
                BuildItemListMenu(player);
                return true;
            }

            if (action >= ACTION_SELECT_ITEM && action < ACTION_SELECT_ITEM_END)
            {
                auto playerListItr = g_ItemList.find(key);

                if (playerListItr == g_ItemList.end())
                {
                    ChatHandler(player->GetSession()).SendSysMessage("아이템 목록이 만료되었습니다. 다시 말을 걸어주세요.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                auto itemItr = playerListItr->second.find(action);

                if (itemItr == playerListItr->second.end())
                {
                    ChatHandler(player->GetSession()).SendSysMessage("선택한 아이템을 찾을 수 없습니다. 다시 말을 걸어주세요.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                Item* item = player->GetItemByGuid(itemItr->second);

                if (!item)
                {
                    ChatHandler(player->GetSession()).SendSysMessage("선택한 아이템이 기본가방에 없습니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                if (!IsAuctionableInventoryItem(item))
                {
                    ChatHandler(player->GetSession()).SendSysMessage("이 아이템은 더 이상 등록 조건을 만족하지 않습니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                const ItemTemplate* proto = item->GetTemplate();

                if (!proto)
                {
                    CloseGossipMenuFor(player);
                    return true;
                }

                g_SelectedItem[key] = item->GetGUID();
                g_Bid.erase(key);
                g_Deposit.erase(key);

                std::ostringstream label;
                label << proto->Name1 << " 시작가 입력";

                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_MONEY_BAG,
                    label.str(),
                    GOSSIP_SENDER_MAIN,
                    ACTION_INPUT_BID,
                    "",
                    0,
                    true
                );

                SendGossipMenuFor(player, 1, me->GetGUID());
                return true;
            }

            if (action == ACTION_CONFIRM)
            {
                RegisterAuction(player);
                CloseGossipMenuFor(player);
                return true;
            }

            CloseGossipMenuFor(player);
            return true;
        }

        bool OnGossipSelectCode(Player* player, uint32, uint32 gossipListId, const char* code) override
        {
            if (!player)
                return true;

            uint32 action = GetSelectedGossipAction(player, gossipListId);
            ClearGossipMenuFor(player);

            if (!code)
            {
                CloseGossipMenuFor(player);
                return true;
            }

            uint32 value = uint32(atoi(code));

            if (!value)
            {
                ChatHandler(player->GetSession()).SendSysMessage("시작가를 올바르게 입력해주세요.");
                CloseGossipMenuFor(player);
                return true;
            }

            GuidKey key = player->GetGUID().GetRawValue();

            if (action == ACTION_INPUT_BID)
            {
                if (!g_SelectedItem.count(key))
                {
                    ChatHandler(player->GetSession()).SendSysMessage("선택된 아이템이 없습니다. 다시 말을 걸어주세요.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                Item* item = player->GetItemByGuid(g_SelectedItem[key]);

                if (!item)
                {
                    ChatHandler(player->GetSession()).SendSysMessage("선택한 아이템이 기본가방에 없습니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                if (!IsAuctionableInventoryItem(item))
                {
                    ChatHandler(player->GetSession()).SendSysMessage("이 아이템은 더 이상 등록 조건을 만족하지 않습니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                const ItemTemplate* proto = item->GetTemplate();

                if (!proto)
                {
                    CloseGossipMenuFor(player);
                    return true;
                }

                if (g_MinbiditemSet.count(proto->ItemId))
                {
                    if (value < 20000)
                    {
                        ChatHandler(player->GetSession()).SendSysMessage("상급 아이템은 시작금액이 20000RP 이상입니다.");
                        CloseGossipMenuFor(player);
                        return true;
                    }
                }

                uint32 depositRP = 10000;

                g_Bid[key] = value;
                g_Deposit[key] = depositRP;

                std::ostringstream msg;
                msg << "|cffFFD700[경매 등록 확인]|r\n\n"
                    << "아이템: " << proto->Name1 << "\n"
                    << "시작가: " << value << " RP\n"
                    << "보증금: " << depositRP << " RP\n\n"
                    << "즉시구매가는 사용하지 않습니다.\n"
                    << "판매시 수수료는 25% 입니다.\n\n"
                    << "등록하시겠습니까?";

                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_DOT,
                    msg.str(),
                    GOSSIP_SENDER_MAIN,
                    ACTION_CONFIRM
                );

                SendGossipMenuFor(player, 1, me->GetGUID());
                return true;
            }

            CloseGossipMenuFor(player);
            return true;
        }

        void RegisterAuction(Player* player)
        {
            if (!player)
                return;

            uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
            GuidKey key = player->GetGUID().GetRawValue();

            if (!g_SelectedItem.count(key))
            {
                ChatHandler(player->GetSession()).SendSysMessage("선택된 아이템이 없습니다.");
                return;
            }

            if (!g_Bid.count(key))
            {
                ChatHandler(player->GetSession()).SendSysMessage("시작가가 입력되지 않았습니다.");
                return;
            }

            if (!g_Deposit.count(key))
            {
                ChatHandler(player->GetSession()).SendSysMessage("보증금 정보가 없습니다.");
                return;
            }

            Item* item = player->GetItemByGuid(g_SelectedItem[key]);

            if (!item)
            {
                ChatHandler(player->GetSession()).SendSysMessage("선택한 아이템이 기본가방에 없습니다.");
                ClearPlayerAuctionState(player);
                return;
            }

            if (!IsAuctionableInventoryItem(item))
            {
                ChatHandler(player->GetSession()).SendSysMessage("이 아이템은 더 이상 등록 조건을 만족하지 않습니다.");
                ClearPlayerAuctionState(player);
                return;
            }

            uint32 bidRP = g_Bid[key];
            uint32 depositRP = g_Deposit[key];

            uint32 accountId = player->GetSession()->GetAccountId();

            if (!HasEnoughRP(accountId, depositRP))
            {
                ChatHandler(player->GetSession()).SendSysMessage("RP 부족");
                return;
            }

            AuctionHouseEntry const* ahEntry =
                sAuctionMgr->GetAuctionHouseEntryFromHouse(FIXED_AUCTION_HOUSE_ID);

            if (!ahEntry)
            {
                ChatHandler(player->GetSession()).SendSysMessage("경매장 정보를 찾을 수 없습니다.");
                return;
            }

            AuctionHouseObject* auctionHouse =
                sAuctionMgr->GetAuctionsMapByHouseId(FIXED_AUCTION_HOUSE_ID);

            if (!auctionHouse)
            {
                ChatHandler(player->GetSession()).SendSysMessage("경매장 객체를 찾을 수 없습니다.");
                return;
            }

            SubtractRP(accountId, depositRP);

            uint32 bid = bidRP;
            uint32 buyout = 0;
            uint32 deposit = depositRP;

            AuctionEntry* AH = new AuctionEntry();

            AH->Id = sObjectMgr->GenerateAuctionID();
            AH->houseId = FIXED_AUCTION_HOUSE_ID;
            AH->itemGUIDLow = item->GetGUID().GetCounter();
            AH->itemEntry = item->GetEntry();
            AH->itemCount = item->GetCount();
            AH->owner = player->GetGUID().GetCounter();
            AH->startbid = bid;
            AH->bidder = 0;
            AH->bid = 0;
            AH->buyout = buyout;
            AH->expire_time = time(nullptr) + 12 * HOUR;

            if (realmId != 1)
                AH->expire_time = time(nullptr) + 2 * MINUTE;

            AH->deposit = deposit;
            AH->auctionHouseEntry = ahEntry;

            sAuctionMgr->AddAItem(item);
            auctionHouse->AddAuction(AH);

            player->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);

            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

            item->DeleteFromInventoryDB(trans);
            item->SaveToDB(trans);
            AH->SaveToDB(trans);

            uint32 endTime = AH->expire_time;

            LogAuctionHistory(
                accountId,
                player->GetGUID().GetCounter(),
                AH->itemGUIDLow,
                AH->itemEntry,
                bid,
                buyout,
                deposit,
                "SELL",
                false,
                endTime,
                1
            );

            if (realmId == 1)
            {
                SendServerDiscordEvent(
                    "🧿 경매물품 등록",
                    "새로운 경매 물품이 등록되었습니다. 지금 바로 확인하세요!",
                    "https://stimpack.pe.kr",
                    16753920,
                    "auction"
                );
            }

            player->SaveInventoryAndGoldToDB(trans);

            CharacterDatabase.CommitTransaction(trans);

            ClearPlayerAuctionState(player);

            ChatHandler(player->GetSession()).SendSysMessage("경매 등록 완료");
        }
    };

    CreatureAI* GetAI(Creature* c) const override
    {
        return new npc_custom_auctioneerAI(c);
    }
};

namespace AutoCustomAuction
{

	static constexpr uint32 AUTO_AUCTION_OWNER_GUID = 944;
	static constexpr uint8 FIXED_AUCTION_HOUSE_ID = 7;
	//시작 금액
	static uint32 GetBotStartBid(uint32 categoryId)
	{
		if (categoryId == 5)
			return urand(2000, 5000);

		if (categoryId == 6)
			return urand(5000, 10000);

		return urand(5000, 10000);
	}

	static uint32 GetBotStartBidout(uint32 categoryId)
	{
		if (categoryId == 5)
			return 10000;

		if (categoryId == 6)
			return 15000;

		return 15000;
	}

	//판매아이템 픽
   struct AutoAuctionPickResult
	{
		uint32 ItemEntry = 0;
		uint32 CategoryId = 0;
	};

	static AutoAuctionPickResult PickRandomAvailableAutoAuctionItem()
	{
		std::unordered_set<uint32> activeEntries;

		QueryResult result = CharacterDatabase.PQuery(
			"SELECT ii.itemEntry "
			"FROM characters.auctionhouse ah "
			"JOIN characters.item_instance ii ON ii.guid = ah.itemguid");

		if (result)
		{
			do
			{
				Field* f = result->Fetch();
				uint32 itemEntry = f[0].GetUInt32();

				if (!itemEntry)
					continue;

				if (!g_autobidauctionitemCategoryMap.count(itemEntry))
					continue;

				activeEntries.insert(itemEntry);

			} while (result->NextRow());
		}

		if (activeEntries.size() >= 6)
			return {};

		std::vector<uint32> candidates;
		candidates.reserve(g_autobidauctionitemCategoryMap.size());

		for (auto const& pair : g_autobidauctionitemCategoryMap)
		{
			uint32 itemEntry = pair.first;

			if (activeEntries.count(itemEntry))
				continue;

			candidates.push_back(itemEntry);
		}

		if (candidates.empty())
			return {};

		uint32 pickedItemEntry = candidates[urand(0, uint32(candidates.size() - 1))];

		AutoAuctionPickResult pick;
		pick.ItemEntry = pickedItemEntry;
		pick.CategoryId = g_autobidauctionitemCategoryMap[pickedItemEntry];
		return pick;
	}

    static void RegisterBotAuction()
    {
        uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);

		if (realmId != 1)
			return;

        AutoAuctionPickResult pick = PickRandomAvailableAutoAuctionItem();
		if (!pick.ItemEntry)
			return;

		uint32 itemEntry = pick.ItemEntry;
		uint32 categoryId = pick.CategoryId;

        Item* item = Item::CreateItem(itemEntry, 1);
        if (!item)
            return;

		AuctionHouseEntry const* ahEntry =
                sAuctionMgr->GetAuctionHouseEntryFromHouse(FIXED_AUCTION_HOUSE_ID);

		AuctionHouseObject* auctionHouse =
                sAuctionMgr->GetAuctionsMapByHouseId(FIXED_AUCTION_HOUSE_ID);

        AuctionEntry* AH = new AuctionEntry();

        AH->Id = sObjectMgr->GenerateAuctionID();
        AH->houseId = FIXED_AUCTION_HOUSE_ID;
        AH->itemGUIDLow = item->GetGUID().GetCounter();
        AH->itemEntry = item->GetEntry();
        AH->itemCount = item->GetCount();
        AH->owner = AUTO_AUCTION_OWNER_GUID;
        AH->startbid = GetBotStartBid(categoryId);
        AH->bid = 0;
		AH->bidder = 0;
        AH->buyout = GetBotStartBidout(categoryId);
        AH->expire_time = time(nullptr) + 12 * HOUR;
        AH->deposit = 0;
        AH->auctionHouseEntry = ahEntry;

        sAuctionMgr->AddAItem(item);
        auctionHouse->AddAuction(AH);

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        item->SaveToDB(trans);
        AH->SaveToDB(trans);

        CharacterDatabase.CommitTransaction(trans);

        if (realmId == 1)
            SendServerDiscordEvent("🧿 자동 경매 등록", "자동 경매 물품이 등록되었습니다. 지금 바로 확인하세요!", "https://stimpack.pe.kr", 16753920, "auction");

		uint32 endTime = AH->expire_time;
		LogAuctionHistory(2, 944, AH->itemGUIDLow, AH->itemEntry, 0, AH->buyout , 0, "SELL", false, endTime, 1);
    }
}

class AuctionBlockItemLoader : public WorldScript
{
public:
	AuctionBlockItemLoader() : WorldScript("AuctionBlockItemLoader"), _timer(0) , m_RareMaterialAuctionTimer (0) {}

    void OnStartup() override
    {
        g_BlockItemSet.clear();
        g_MinbiditemSet.clear();
		g_autobidauctionitemCategoryMap.clear();

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT game_item_id FROM appdata.shop_items WHERE category_id IN (8,9)");

        if (result)
        {
            do
            {
                Field* f = result->Fetch();
                uint32 entry = f[0].GetUInt32();
                g_BlockItemSet.insert(entry);
            } while (result->NextRow());
        }

        QueryResult result_bid = CharacterDatabase.PQuery(
            "SELECT game_item_id FROM appdata.shop_items WHERE category_id IN (7)");

        if (result_bid)
        {
            do
            {
                Field* f = result_bid->Fetch();
                uint32 entry = f[0].GetUInt32();
                g_MinbiditemSet.insert(entry);
            } while (result_bid->NextRow());
        }

		 QueryResult result_auction = CharacterDatabase.PQuery(
			"SELECT game_item_id, category_id FROM appdata.shop_items WHERE category_id IN (5,6)");

		if (result_auction)
		{
			do
			{
				Field* f = result_auction->Fetch();
				uint32 entry = f[0].GetUInt32();
				uint32 categoryId = f[1].GetUInt32();

				g_autobidauctionitemCategoryMap[entry] = categoryId;

			} while (result_auction->NextRow());
		}
    }

	void OnUpdate(uint32 diff) override
    {
        if (_timer <= diff)
        {
            AutoCustomAuction::RegisterBotAuction();
            _timer = 6 * HOUR * IN_MILLISECONDS;
        }
        else
            _timer -= diff;

		if (m_RareMaterialAuctionTimer <= diff)
		{
			AutoRareMaterialAuction::RegisterRareMaterialAuction();
			m_RareMaterialAuctionTimer = HOUR * IN_MILLISECONDS;
		}
		else
		{
			m_RareMaterialAuctionTimer -= diff;
		}
    }

private:
    uint32 _timer;
	uint32 m_RareMaterialAuctionTimer;
};


//재료 봇
namespace AutoRareMaterialAuction
{
	static constexpr uint32 RARE_AUCTION_OWNER_GUID = 944;
	static constexpr uint8 RARE_AUCTION_HOUSE_ID = 7;

	static constexpr uint32 RARE_PRICE_PER_ITEM = 200;
	static constexpr uint32 RARE_MAX_ACTIVE_PER_ITEM = 3;
	static constexpr uint32 RARE_MIN_STACK_COUNT = 1;
	static constexpr uint32 RARE_MAX_STACK_COUNT = 20;
	static constexpr uint32 RARE_AUCTION_DURATION = 12 * HOUR;

	struct RareMaterialItem
	{
		uint32 ItemEntry = 0;
		char const* Name = "";
	};

	struct RareMaterialPickResult
	{
		uint32 ItemEntry = 0;
		uint32 ActiveCount = 0;
		char const* Name = "";
	};

	static std::vector<RareMaterialItem> const RareMaterialItems =
	{
		{ 49908, "태고의 사로나이트" },
		{ 47556, "성전사의 보주" },
		{ 45087, "룬문자 보주" },
		{ 43102, "얼어붙은 보주" },
		{ 37663, "티탄강철 주괴" },
		{ 44128, "북극 가죽" },
		{ 34057, "심연의 결정" },
		{ 36908, "서리 연꽃" },
		{ 43016, "달라란 요리상" },
		{ 41596, "달라란 보석세공사의 징표" }
	}; 

	static RareMaterialPickResult PickRandomAvailableRareMaterial()
	{
		std::unordered_map<uint32, uint32> activeCountMap;

		QueryResult result = CharacterDatabase.PQuery(
			"SELECT ii.itemEntry, COUNT(*) "
			"FROM characters.auctionhouse ah "
			"JOIN characters.item_instance ii ON ii.guid = ah.itemguid "
			"WHERE ii.itemEntry IN (49908, 47556, 45087, 43102, 37663, 44128, 34057, 36908) "
			"GROUP BY ii.itemEntry");

		if (result)
		{
			do
			{
				Field* f = result->Fetch();

				uint32 itemEntry = f[0].GetUInt32();
				uint32 activeCount = f[1].GetUInt32();

				if (!itemEntry)
					continue;

				activeCountMap[itemEntry] = activeCount;

			} while (result->NextRow());
		}

		std::vector<RareMaterialPickResult> candidates;
		candidates.reserve(RareMaterialItems.size());

		for (RareMaterialItem const& item : RareMaterialItems)
		{
			uint32 activeCount = 0;

			auto itr = activeCountMap.find(item.ItemEntry);
			if (itr != activeCountMap.end())
				activeCount = itr->second;

			if (activeCount >= RARE_MAX_ACTIVE_PER_ITEM)
				continue;

			RareMaterialPickResult pick;
			pick.ItemEntry = item.ItemEntry;
			pick.ActiveCount = activeCount;
			pick.Name = item.Name;

			candidates.push_back(pick);
		}

		if (candidates.empty())
			return {};

		return candidates[urand(0, uint32(candidates.size() - 1))];
	}

	static void RegisterRareMaterialAuction()
	{
		uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);

		if (realmId != 1)
			return; 

		RareMaterialPickResult pick = PickRandomAvailableRareMaterial();
		if (!pick.ItemEntry)
			return;

		uint32 itemEntry = pick.ItemEntry;
		uint32 itemCount = urand(RARE_MIN_STACK_COUNT, RARE_MAX_STACK_COUNT);
		uint32 totalPrice = itemCount * RARE_PRICE_PER_ITEM;

		Item* item = Item::CreateItem(itemEntry, itemCount);
		if (!item)
			return;

		AuctionHouseEntry const* ahEntry =
			sAuctionMgr->GetAuctionHouseEntryFromHouse(RARE_AUCTION_HOUSE_ID);

		if (!ahEntry)
		{
			delete item;
			return;
		}

		AuctionHouseObject* auctionHouse =
			sAuctionMgr->GetAuctionsMapByHouseId(RARE_AUCTION_HOUSE_ID);

		if (!auctionHouse)
		{
			delete item;
			return;
		}

		AuctionEntry* AH = new AuctionEntry();

		AH->Id = sObjectMgr->GenerateAuctionID();
		AH->houseId = RARE_AUCTION_HOUSE_ID;
		AH->itemGUIDLow = item->GetGUID().GetCounter();
		AH->itemEntry = item->GetEntry();
		AH->itemCount = item->GetCount();
		AH->owner = RARE_AUCTION_OWNER_GUID;
		AH->startbid = totalPrice;
		AH->bid = 0;
		AH->bidder = 0;
		AH->buyout = totalPrice;
		AH->expire_time = time(nullptr) + RARE_AUCTION_DURATION;
		AH->deposit = 0;
		AH->auctionHouseEntry = ahEntry;

		sAuctionMgr->AddAItem(item);
		auctionHouse->AddAuction(AH);

		CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

		item->SaveToDB(trans);
		AH->SaveToDB(trans);

		CharacterDatabase.CommitTransaction(trans);

/*if (realmId != 1)
			return; 

		std::string desc =
			std::string(pick.Name) +
			" x" + std::to_string(itemCount) +
			" 묶음이 경매장에 등록되었습니다. 개당 " +
			std::to_string(RARE_PRICE_PER_ITEM) +
			"RP, 총 " +
			std::to_string(totalPrice) +
			"RP입니다.";

		SendServerDiscordEvent(
			"🧱 희귀 재료 자동 경매 등록",
			desc,
			"https://stimpack.pe.kr",
			16753920,
			"auction"
		);

		uint32 endTime = AH->expire_time;

		LogAuctionHistory(
			2,
			RARE_AUCTION_OWNER_GUID,
			AH->itemGUIDLow,
			AH->itemEntry,
			0,
			AH->buyout,
			0,
			"SELL",
			false,
			endTime,
			itemCount
		);*/
	}
}

void AddSC_npc_custom_auctioneer()
{
    new npc_custom_auctioneer();
	new AuctionBlockItemLoader();
}