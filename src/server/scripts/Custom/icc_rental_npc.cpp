// Custom/icc_rental_npc.cpp (또는 네 파일)
// npc_icc_rental : CodeBox 검색 → 2차 메뉴 → 클릭 시 RP 차감 + 아이템 지급

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "Chat.h"
#include "Item.h"
#include "DatabaseEnv.h"
#include "WorldSession.h" // ✅ GetAccountId 때문에 필요

#include <unordered_map>
#include <vector>
#include <string>

/* =============================
 * 설정값
 * ============================= */
static constexpr uint32 RENTAL_RP_COST = 35000;

/* =============================
 * RP 유틸 (네가 쓰던 방식에 맞춰서)
 * TrySpend(accountId, amount) 형태로 이미 있으면 그걸 그대로 써도 됨
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

    static inline long long Get(uint32 accountId)
    {
        EnsureRow(accountId);
        if (QueryResult r = CharacterDatabase.PQuery(
            "SELECT IFNULL(reg_point,0) FROM appdata.account_data WHERE id={}",
            accountId))
        {
            return r->Fetch()[0].GetInt64();
        }
        return 0;
    }

    static inline bool TrySpend(uint32 accountId, uint32 amount)
    {
        EnsureRow(accountId);

        long long before = Get(accountId);
        if (before < amount)
            return false;

        CharacterDatabase.PExecute(
            "UPDATE appdata.account_data "
            "SET reg_point = reg_point - {} "
            "WHERE id={} AND reg_point >= {}",
            amount, accountId, amount
        );

        return true;
    }
}

/* =============================
 * Shop 검색 결과 구조 (예시)
 *  - 네 DB 함수/구조가 이미 있으면 그대로 연결
 * ============================= */
struct ShopRow
{
    uint32 shopId = 0;
    uint32 itemEntry = 0;
    std::string desc;
};

// ✅ 네가 구현해둔 걸 쓰면 됨 (시그니처만 맞추면 됨)
static bool DB_LoadShop(uint32 shopId, ShopRow& outRow)
{
    QueryResult r = CharacterDatabase.PQuery(
        "SELECT id, game_item_id, description "
        "FROM appdata.shop_items "
        "WHERE id={} AND category_id=8",
        shopId
    );
    if (!r)
        return false;

    Field* f = r->Fetch();
    outRow.shopId    = f[0].GetUInt32();
    outRow.itemEntry = f[1].GetUInt32();
    outRow.desc      = f[2].GetString();
    return true;
}

static std::vector<ShopRow> DB_SearchItems(std::string const& keyword)
{
    std::vector<ShopRow> rows;

    // 간단 LIKE 검색 (필요하면 escape 처리)
    QueryResult r = CharacterDatabase.PQuery(
        "SELECT id, game_item_id, description "
        "FROM appdata.shop_items "
        "WHERE category_id=8 AND description LIKE '%{}%' "
        "ORDER BY id DESC "
        "LIMIT 20",
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
        rows.push_back(row);
    } while (r->NextRow());

    return rows;
}

/* =============================
 * 플레이어별 검색 캐시
 * lowGuid -> [shopId, shopId, ...]
 * ============================= */
static std::unordered_map<uint32, std::vector<uint32>> g_SearchCache;

/* =====================================================
 * NPC
 * ===================================================== */
class npc_icc_rental : public CreatureScript
{
public:
    npc_icc_rental() : CreatureScript("npc_icc_rental") {}

    struct npc_icc_rentalAI : public ScriptedAI
    {
        npc_icc_rentalAI(Creature* c) : ScriptedAI(c) {}

        // 1) NPC 클릭 시 바로 CodeBox 띄움
        bool OnGossipHello(Player* player) override
        {
            ClearGossipMenuFor(player);

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "렌탈할 아이템 이름 입력",
                GOSSIP_SENDER_MAIN,
                101,
                "",
                0,
                true // CodeBox
            );

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "닫기", GOSSIP_SENDER_MAIN, 1);
            SendGossipMenuFor(player, 1, me->GetGUID());
            return true;
        }

        // 2) 2차 메뉴(검색 결과) 클릭 → RP 검사/차감/지급
        bool OnGossipSelect(Player* player, uint32 /*sender*/, uint32 gossipListId) override
        {
            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            uint32 lowGuid = player->GetGUID().GetCounter();

            if (action >= 200000 && action < 210000)
            {
                uint32 idx = action - 200000;

                auto it = g_SearchCache.find(lowGuid);
                if (it == g_SearchCache.end() || idx >= it->second.size())
                {
                    CloseGossipMenuFor(player);
                    ChatHandler(player->GetSession()).SendSysMessage("검색 목록이 만료되었습니다. 다시 검색해주세요.");
                    return true;
                }

                uint32 shopId = it->second[idx];

                ShopRow row;
                if (!DB_LoadShop(shopId, row))
                {
                    CloseGossipMenuFor(player);
                    ChatHandler(player->GetSession()).SendSysMessage("아이템 정보를 불러오지 못했습니다.");
                    return true;
                }

                // RP 검사/차감
                uint32 accountId = player->GetSession()->GetAccountId();
                if (!RP::TrySpend(accountId, RENTAL_RP_COST))
                {
                    CloseGossipMenuFor(player);
                    ChatHandler(player->GetSession()).SendSysMessage("❌ RP가 부족합니다.");
                    return true;
                }

                // 아이템 지급
                ItemPosCountVec dest;
                if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, row.itemEntry, 1) != EQUIP_ERR_OK)
                {
                    CloseGossipMenuFor(player);
                    ChatHandler(player->GetSession()).SendSysMessage("가방 공간이 부족합니다.");
                    return true;
                }

                Item* item = player->StoreNewItem(dest, row.itemEntry, true);
                if (item)
                    player->SendNewItem(item, 1, true, false);

                ChatHandler(player->GetSession()).PSendSysMessage("✅ 렌탈 지급 완료: {}", row.desc);

                CloseGossipMenuFor(player);
                return true;
            }

            CloseGossipMenuFor(player);
            return true;
        }

        // 3) CodeBox 입력 처리 → 검색 → 2차 메뉴 다시 열기
        bool OnGossipSelectCode(Player* player, uint32 /*sender*/, uint32 action, const char* code) override
        {
            // ✅ 핵심: 여기 action이 101이 아닐 수 있음(= gossipListId로 들어오는 코어가 있음)
            // 그래서 "진짜 action"으로 변환해서 비교해야 함
            uint32 realAction = player->PlayerTalkClass->GetGossipOptionAction(action);

            if (realAction != 101)
                return true;

            ClearGossipMenuFor(player);

            // 디버그 (원하면 빼)
            // ChatHandler(player->GetSession()).PSendSysMessage("DEBUG: OnGossipSelectCode action={}, realAction={}, code={}", action, realAction, (code?code:""));

            std::string keyword = code ? code : "";
            if (keyword.size() < 2)
            {
                ChatHandler(player->GetSession()).SendSysMessage("2글자 이상 입력하세요.");
                CloseGossipMenuFor(player);
                return true;
            }

            auto rows = DB_SearchItems(keyword);

            uint32 lowGuid = player->GetGUID().GetCounter();
            g_SearchCache[lowGuid].clear();

            if (rows.empty())
            {
                ChatHandler(player->GetSession()).PSendSysMessage("검색 결과가 없습니다: {}", keyword);
                CloseGossipMenuFor(player);
                return true;
            }

            // 2차 메뉴 구성
            for (uint32 i = 0; i < (uint32)rows.size(); ++i)
            {
                g_SearchCache[lowGuid].push_back(rows[i].shopId);

                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_INTERACT_1,
                    rows[i].desc,
                    GOSSIP_SENDER_MAIN,
                    200000 + i
                );
            }

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "닫기", GOSSIP_SENDER_MAIN, 1);

            // ✅ 여기서 반드시 다시 메뉴를 열어줘야 "멈춤"이 사라짐
            SendGossipMenuFor(player, 1, me->GetGUID());
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_icc_rentalAI(creature);
    }
};

void AddSC_icc_rental_npc()
{
    
}
