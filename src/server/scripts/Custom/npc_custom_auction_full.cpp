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
        std::unordered_map<GuidKey, uint32> g_Bid;
        std::unordered_map<GuidKey, uint32> g_Buyout;
        std::unordered_map<GuidKey, uint32> g_Deposit;

        static constexpr uint32 ACTION_SELECT_ITEM   = 1000;
        static constexpr uint32 ACTION_INPUT_BID     = 2000;
        static constexpr uint32 ACTION_INPUT_BUYOUT  = 2001;
        static constexpr uint32 ACTION_INPUT_DEPOSIT = 2002;
        static constexpr uint32 ACTION_CONFIRM       = 3000;

        static constexpr uint8 FIXED_AUCTION_HOUSE_ID = 7;

        npc_custom_auctioneerAI(Creature* c) : ScriptedAI(c) {}

        // =========================
        // 아이템 리스트
        // =========================
        bool OnGossipHello(Player* player) override
        {
            ClearGossipMenuFor(player);

            for (uint8 slot = 0; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            {
                Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (!item)
                    continue;

                if (item->GetTemplate()->Quality < ITEM_QUALITY_EPIC)
                    continue;

				if (item->GetTemplate()->Class != ITEM_CLASS_WEAPON &&
					item->GetTemplate()->Class != ITEM_CLASS_ARMOR)
					continue;

				// 🔥 귀속 아이템만 허용
				if (!item->IsSoulBound())
					continue;

				if (item->GetUInt32Value(ITEM_FIELD_DURABILITY) <
					item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY))
				{
					continue; // 🔥 사용된 장비 금지
				}

				if (item->GetPlayedTime() > 12000)
					continue;

				const ItemTemplate* proto = item->GetTemplate();

				// 🔥 shop_items 제외
				if (g_BlockItemSet.count(proto->ItemId))
					continue;

                AddGossipItemFor(player, GOSSIP_ICON_DOT,
                    item->GetTemplate()->Name1,
                    GOSSIP_SENDER_MAIN,
                    ACTION_SELECT_ITEM + slot);
            }

            SendGossipMenuFor(player, 1, me->GetGUID());
            return true;
        }

        // =========================
        // 선택
        // =========================
        bool OnGossipSelect(Player* player, uint32, uint32 gossipListId) override
        {
            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            if (action >= ACTION_SELECT_ITEM && action < ACTION_SELECT_ITEM + 100)
            {
                uint32 slot = action - ACTION_SELECT_ITEM;
                Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

                if (!item)
                    return true;

                g_SelectedItem[player->GetGUID().GetRawValue()] = item->GetGUID();

                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                    "시작가 입력",
                    GOSSIP_SENDER_MAIN,
                    ACTION_INPUT_BUYOUT,
                    "", 0, true);

                SendGossipMenuFor(player, 1, me->GetGUID());
                return true;
            }

            if (action == ACTION_CONFIRM)
            {
                RegisterAuction(player);
                CloseGossipMenuFor(player);
                return true;
            }

            return true;
        }

        // =========================
        // 코드 입력 처리
        // =========================
        bool OnGossipSelectCode(Player* player, uint32, uint32 gossipListId, const char* code) override
        {
            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            uint32 value = atoi(code);
            if (!value)
                return true;

            GuidKey key = player->GetGUID().GetRawValue();

            // 시작가
            if (action == ACTION_INPUT_BID)
            {
                g_Bid[key] = value;

                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                    "즉시구매가 입력",
                    GOSSIP_SENDER_MAIN,
                    ACTION_INPUT_BUYOUT,
                    "", 0, true);

                SendGossipMenuFor(player, 1, me->GetGUID());
                return true;
            }

            // 즉구가
            if (action == ACTION_INPUT_BUYOUT)
			{
				g_Bid[key] = value;

				uint32 bid = g_Bid[key];
				uint32 buyout = g_Buyout[key];

				ObjectGuid itemGuid = g_SelectedItem[key];
				Item* item = player->GetItemByGuid(itemGuid);
				const ItemTemplate* proto = item->GetTemplate();

				if (!item)
					return true;

				if (g_MinbiditemSet.count(proto->ItemId))
					if (bid < 20000)
					{
						ChatHandler(player->GetSession()).SendSysMessage("상급 아이템음 시작금이이 20000RP 이상 입니다.");
						return true;
					}

				// 🔥 보증금 자동 계산
				uint32 depositRP = 10000;

				// 저장
				g_Deposit[key] = depositRP;

				std::ostringstream msg;
				msg << "|cffFFD700[경매 등록 확인]|r\n\n"
					<< "시작가: " << bid << "\n"
					<< "보증금: " << depositRP << "\n\n"
					<< "판매시 수수료는 25% 입니다.\n\n"
					<< "등록하시겠습니까?";

				AddGossipItemFor(player, GOSSIP_ICON_DOT,
					msg.str(),
					GOSSIP_SENDER_MAIN,
					ACTION_CONFIRM);

				SendGossipMenuFor(player, 1, me->GetGUID());
				return true;
			}

            return true;
        }

        // =========================
        // 등록
        // =========================
        void RegisterAuction(Player* player)
        {
			uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
            GuidKey key = player->GetGUID().GetRawValue();

            if (!g_SelectedItem.count(key))
                return;

            Item* item = player->GetItemByGuid(g_SelectedItem[key]);
            if (!item)
                return;

			uint32 bidRP = g_Bid[key];
			uint32 buyoutRP = g_Buyout[key];
			uint32 depositRP = g_Deposit[key];

            uint32 accountId = player->GetSession()->GetAccountId();

            if (!HasEnoughRP(accountId, depositRP))
            {
                ChatHandler(player->GetSession()).SendSysMessage("RP 부족");
                return;
            }

			SubtractRP(accountId, depositRP);

			// 🔥 전부 변환
			uint32 bid = bidRP;
			uint32 buyout = 0;
			uint32 deposit = depositRP;

            AuctionHouseEntry const* ahEntry =
                sAuctionMgr->GetAuctionHouseEntryFromHouse(FIXED_AUCTION_HOUSE_ID);

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

            AuctionHouseObject* auctionHouse =
                sAuctionMgr->GetAuctionsMapByHouseId(FIXED_AUCTION_HOUSE_ID);

            sAuctionMgr->AddAItem(item);
            auctionHouse->AddAuction(AH);

            player->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);

            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

            item->DeleteFromInventoryDB(trans);
            item->SaveToDB(trans);
            AH->SaveToDB(trans);

			uint32 endTime = AH->expire_time;
			LogAuctionHistory(accountId, player->GetGUID().GetCounter(), AH->itemGUIDLow, AH->itemEntry, bid, buyout, deposit, "SELL", false,endTime, 1);

			if (realmId == 1)
				SendServerDiscordEvent("🧿 경매물품 등록", "새로운 경매 물품이 등록되었습니다. 지금 바로 확인하세요!","https://stimpack.pe.kr",16753920,"auction");

            player->SaveInventoryAndGoldToDB(trans);

            CharacterDatabase.CommitTransaction(trans);

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
		{ 36908, "서리 연꽃" }
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