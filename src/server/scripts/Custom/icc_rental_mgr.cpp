// Custom/icc_rental_npc.cpp
// ICC 렌탈 NPC - 온라인 상태에서만 시간 흐르는 렌탈 시스템 (메모리 누적 방식)

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "Chat.h"
#include "Item.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"

#include <unordered_map>
#include <vector>
#include <string>

/* =============================
 * 설정
 * ============================= */
static constexpr uint32 RENTAL_RP_COST   = 35000;
static constexpr uint32 RENTAL_SECONDS   = 2 * 60 * 60; // 테스트용
static constexpr uint32 RENTAL_TICK_MS   = 30 * 1000;

/* =============================
 * RP 유틸
 * ============================= */
namespace RP
{
    static inline void EnsureRow(uint32 accountId)
    {
        CharacterDatabase.PExecute(
            "INSERT INTO appdata.account_data (id, reg_point) VALUES ({},0) "
            "ON DUPLICATE KEY UPDATE reg_point=reg_point",
            accountId
        );
    }

    static inline bool TrySpend(uint32 accountId, uint32 amount)
    {
        EnsureRow(accountId);

        QueryResult r = CharacterDatabase.PQuery(
            "SELECT reg_point FROM appdata.account_data WHERE id={}",
            accountId
        );
        if (!r || r->Fetch()[0].GetInt64() < amount)
            return false;

        CharacterDatabase.PExecute(
            "UPDATE appdata.account_data SET reg_point = reg_point - {} "
            "WHERE id={} AND reg_point >= {}",
            amount, accountId, amount
        );
        return true;
    }

    static inline long long Get(uint32 accountId)
    {
        EnsureRow(accountId);
        if (QueryResult r = CharacterDatabase.PQuery(
            "SELECT reg_point FROM appdata.account_data WHERE id={}",
            accountId))
        {
            return r->Fetch()[0].GetInt64();
        }
        return 0;
    }
}

/* =============================
 * Shop 구조
 * ============================= */
struct ShopRow
{
    uint32 shopId = 0;
    uint32 itemEntry = 0;
    uint32 itemLevel = 0;
    std::string desc;
};

static uint32 DB_GetItemLevel(uint32 entry)
{
    if (QueryResult r = WorldDatabase.PQuery(
        "SELECT ItemLevel FROM item_template WHERE entry={}",
        entry))
    {
        return r->Fetch()[0].GetUInt32();
    }
    return 0;
}

static bool DB_LoadShop(uint32 shopId, ShopRow& out)
{
    QueryResult r = CharacterDatabase.PQuery(
        "SELECT id, game_item_id, description "
        "FROM appdata.shop_items WHERE id={} AND category_id=8",
        shopId
    );
    if (!r)
        return false;

    Field* f = r->Fetch();
    out.shopId    = f[0].GetUInt32();
    out.itemEntry = f[1].GetUInt32();
    out.desc      = f[2].GetString();
    out.itemLevel = DB_GetItemLevel(out.itemEntry);
    return true;
}

static std::vector<ShopRow> DB_SearchItems(std::string const& keyword)
{
    std::vector<ShopRow> rows;

    QueryResult r = CharacterDatabase.PQuery(
        "SELECT id, game_item_id, description "
        "FROM appdata.shop_items "
        "WHERE category_id=8 AND description LIKE '%{}%' "
        "ORDER BY id DESC LIMIT 20",
        keyword
    );
    if (!r)
        return rows;

    do
    {
        Field* f = r->Fetch();
        ShopRow row;
        row.shopId    = f[0].GetUInt32();
        row.itemEntry = f[1].GetUInt32();
        row.desc      = f[2].GetString();
        row.itemLevel = DB_GetItemLevel(row.itemEntry);
        rows.push_back(row);
    } while (r->NextRow());

    return rows;
}

/* =============================
 * 검색 캐시
 * ============================= */
static std::unordered_map<uint32, std::vector<uint32>> g_SearchCache;

/* =============================
 * 렌탈 메모리 (온라인 누적)
 * ============================= */
struct RentalInfo
{
    uint32 charGuid;
    uint32 itemEntry;

    uint32 remainSeconds; // DB 기준 남은 시간
    uint32 usedOnline;    // 🔥 온라인 누적 시간

    bool   warned10min;
};

static std::unordered_map<uint64, RentalInfo> g_RentalMap;
static uint32 g_TickMs = 0;

/* =============================
 * WorldScript
 * ============================= */
class icc_rental_world : public WorldScript
{
public:
    icc_rental_world() : WorldScript("icc_rental_world") {}

    void OnUpdate(uint32 diff) override
    {
        g_TickMs += diff;
        if (g_TickMs < RENTAL_TICK_MS)
            return;
        g_TickMs = 0;

        uint32 delta = RENTAL_TICK_MS / 1000;

        for (auto it = g_RentalMap.begin(); it != g_RentalMap.end(); )
        {
            RentalInfo& info = it->second;

            Player* plr = ObjectAccessor::FindPlayer(
                ObjectGuid::Create<HighGuid::Player>(info.charGuid)
            );

            if (!plr)
            {
                ++it;
                continue;
            }

            info.usedOnline += delta;

            uint32 remain = (info.usedOnline >= info.remainSeconds)
                ? 0
                : info.remainSeconds - info.usedOnline;

            if (!info.warned10min && remain <= 600)
            {
                ChatHandler(plr->GetSession())
                    .SendSysMessage("렌탈 아이템이 곧 삭제됩니다.");
                info.warned10min = true;
            }

            if (remain == 0)
            {
                ObjectGuid itemGuid = ObjectGuid::Create<HighGuid::Item>(it->first);
                if (Item* item = plr->GetItemByGuid(itemGuid))
                {
                    plr->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
                    ChatHandler(plr->GetSession())
                        .SendSysMessage("렌탈 아이템이 삭제되었습니다.");
                }

                CharacterDatabase.PExecute(
                    "DELETE FROM appdata.rental_items WHERE item_guid={}",
                    it->first
                );

                it = g_RentalMap.erase(it);
                continue;
            }

            ++it;
        }
    }
};

/* =============================
 * PlayerScript
 * ============================= */
class icc_rental_player : public PlayerScript
{
public:
    icc_rental_player() : PlayerScript("icc_rental_player") {}

    // 로그인: DB 값 그대로 메모리에 적재
    void OnLogin(Player* player, bool) override
	{
		if (!player)
			return;

		uint32 charGuid = player->GetGUID().GetCounter();

		bool hasRental = false;
		uint32 minRemain = 0;

		QueryResult r = CharacterDatabase.PQuery(
			"SELECT item_guid, item_entry, remain_seconds, warned_10min "
			"FROM appdata.rental_items WHERE char_guid={}",
			charGuid
		);
		if (!r)
			return;

		do
		{
			Field* f = r->Fetch();

			RentalInfo info;
			info.charGuid      = charGuid;
			info.itemEntry     = f[1].GetUInt32();
			info.remainSeconds = f[2].GetUInt32();
			info.usedOnline    = 0;
			info.warned10min   = f[3].GetUInt8() != 0;

			if (info.remainSeconds == 0)
			{
				CharacterDatabase.PExecute(
					"DELETE FROM appdata.rental_items WHERE item_guid={}",
					f[0].GetUInt64()
				);
				continue;
			}

			// 🔔 로그인 알림용
			uint32 remain = info.remainSeconds;
			g_RentalMap[f[0].GetUInt64()] = info;

			minRemain = remain;
		} while (r->NextRow());

		// 🔔 로그인 메시지
		uint32 min = minRemain / 60;
		uint32 sec = minRemain % 60;

		std::string msg = Trinity::StringFormat("현재 렌탈 아이템 남은 시간: {}분 {}초", min, sec);
        ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
	}

    // 로그아웃: 누적 시간 DB 반영 후 메모리 제거
    void OnLogout(Player* player) override
    {
        if (!player)
            return;

        uint32 charGuid = player->GetGUID().GetCounter();

        for (auto it = g_RentalMap.begin(); it != g_RentalMap.end(); )
        {
            RentalInfo& info = it->second;
            if (info.charGuid != charGuid)
            {
                ++it;
                continue;
            }

            uint32 remain = (info.usedOnline >= info.remainSeconds)
                ? 0
                : info.remainSeconds - info.usedOnline;

            CharacterDatabase.PExecute(
                "UPDATE appdata.rental_items "
                "SET remain_seconds={} "
                "WHERE item_guid={}",
                remain, it->first
            );

            it = g_RentalMap.erase(it);
        }
    }
};

/* =============================
 * NPC
 * ============================= */
class npc_icc_rental : public CreatureScript
{
public:
    npc_icc_rental() : CreatureScript("npc_icc_rental") {}

    struct npc_icc_rentalAI : public ScriptedAI
    {
        npc_icc_rentalAI(Creature* c) : ScriptedAI(c) {}

        bool OnGossipHello(Player* player) override
        {
            ClearGossipMenuFor(player);

            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                "렌탈할 아이템 이름 입력",
                GOSSIP_SENDER_MAIN, 101, "", 0, true);

            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                "이용 방법 안내",
                GOSSIP_SENDER_MAIN, 50);

            SendGossipMenuFor(player, 1, me->GetGUID());
            return true;
        }

        bool OnGossipSelectCode(Player* player, uint32, uint32 action, const char* code) override
        {
            if (player->PlayerTalkClass->GetGossipOptionAction(action) != 101)
                return true;

            std::string keyword = code ? code : "";
            if (keyword.size() < 2)
            {
                ChatHandler(player->GetSession())
                    .SendSysMessage("2글자 이상 입력하세요.");
                CloseGossipMenuFor(player);
                return true;
            }

            auto rows = DB_SearchItems(keyword);
            uint32 lowGuid = player->GetGUID().GetCounter();
            g_SearchCache[lowGuid].clear();
            ClearGossipMenuFor(player);

            for (uint32 i = 0; i < rows.size(); ++i)
            {
                g_SearchCache[lowGuid].push_back(rows[i].shopId);
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
                    rows[i].desc + " (iLv " + std::to_string(rows[i].itemLevel) + ")",
                    GOSSIP_SENDER_MAIN, 200000 + i);
            }

            SendGossipMenuFor(player, 1, me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32, uint32 gossipListId) override
        {
            uint32 action  = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            uint32 lowGuid = player->GetGUID().GetCounter();

			if (action == 50)
            {
                ClearGossipMenuFor(player);

                AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                    "ICC 렌탈 시스템 안내\n"
                    "1. 아이템 이름 검색 후 렌탈\n"
                    "2. 비용: RP 35,000\n"
                    "3. 사용 시간: 2시간\n"
                    "4. 만료 시 자동 삭제\n"
                    "5. 렌탈은 1개만 가능",
                    GOSSIP_SENDER_MAIN, 1);

                SendGossipMenuFor(player, 1, me->GetGUID());
                return true;
            }

            if (action < 200000)
                return true;

            QueryResult chk = CharacterDatabase.PQuery(
                "SELECT COUNT(*) FROM appdata.rental_items WHERE char_guid={}",
                lowGuid
            );
            if (chk && chk->Fetch()[0].GetUInt32() > 0)
            {
                ChatHandler(player->GetSession())
                    .SendSysMessage("이미 렌탈 중인 아이템이 있습니다.");
                CloseGossipMenuFor(player);
                return true;
            }

            uint32 idx    = action - 200000;
            uint32 shopId = g_SearchCache[lowGuid][idx];

            ShopRow row;
            if (!DB_LoadShop(shopId, row))
                return true;

            uint32 accountId = player->GetSession()->GetAccountId();
            if (!RP::TrySpend(accountId, RENTAL_RP_COST))
            {
				long long after = RP::Get(accountId) ;
				std::string msg = Trinity::StringFormat("RP 부족 (현재 RP: {})", after);
				ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
                CloseGossipMenuFor(player);
                return true;
            }

            ItemPosCountVec dest;
            if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, row.itemEntry, 1) != EQUIP_ERR_OK)
            {
                ChatHandler(player->GetSession())
                    .SendSysMessage("가방 공간이 부족합니다.");
                CloseGossipMenuFor(player);
                return true;
            }

            Item* item = player->StoreNewItem(dest, row.itemEntry, true);
            player->SendNewItem(item, 1, true, false);

            uint64 itemGuidRaw = item->GetGUID().GetRawValue();

            CharacterDatabase.PExecute(
                "INSERT INTO appdata.rental_items "
                "(char_guid,item_guid,item_entry,remain_seconds,warned_10min) "
                "VALUES ({},{},{},{},0)",
                lowGuid, itemGuidRaw, row.itemEntry, RENTAL_SECONDS
            );

            g_RentalMap[itemGuidRaw] = {
                lowGuid, row.itemEntry, RENTAL_SECONDS, 0, false
            };

			long long after = RP::Get(accountId)  - 35000;
			std::string msg = Trinity::StringFormat("렌탈아이템이 지급되었습니다. (현재 RP: {})", after);
			ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());

            CloseGossipMenuFor(player);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_icc_rentalAI(creature);
    }
};

/* =============================
 * Script 등록
 * ============================= */
void AddSC_icc_rental_mgr()
{
    new npc_icc_rental();
    new icc_rental_world();
    new icc_rental_player();
}
