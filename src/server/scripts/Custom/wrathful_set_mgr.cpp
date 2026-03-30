#include "wrathful_set_mgr.h"

#include "Item.h"
#include "DatabaseEnv.h"
#include "ObjectMgr.h"

#include <unordered_map>

/* =====================================================
 * 전역 메모리
 * ===================================================== */
// charGuid → 지급된 아이템 목록
static std::unordered_map<uint32, std::vector<WrathfulItemMem>> g_WrathfulItemMap;

/* =====================================================
 * DB 헬퍼
 * ===================================================== */
static void DB_Insert(uint32 charGuid, WrathfulItemMem const& info)
{
    std::string sql = Trinity::StringFormat(
        "INSERT INTO appdata.wrathful_given_items "
        "(char_guid, item_guid, item_entry) "
        "VALUES ({}, {}, {})",
        charGuid,
        info.itemGuid.GetRawValue(),
        info.entry
    );
    CharacterDatabase.Execute(sql.c_str());
}

static void DB_Delete(uint32 charGuid, ObjectGuid itemGuid)
{
    std::string sql = Trinity::StringFormat(
        "DELETE FROM appdata.wrathful_given_items "
        "WHERE char_guid={} AND item_guid={}",
        charGuid,
        itemGuid.GetRawValue()
    );
    CharacterDatabase.Execute(sql.c_str());
}

/* =====================================================
 * 지급 등록 (NPC에서 호출)
 * ===================================================== */
void RegisterWrathfulItem(Player* player, uint32 entry, ObjectGuid itemGuid)
{
    if (!player)
        return;

    uint32 charGuid = player->GetGUID().GetCounter();

    WrathfulItemMem info;
    info.entry    = entry;
    info.itemGuid = itemGuid;

    g_WrathfulItemMap[charGuid].push_back(info);
    DB_Insert(charGuid, info);
}

/* =====================================================
 * 회수 (로그아웃 / 전장 이탈)
 * ===================================================== */
void RemoveWrathfulItems(Player* player)
{
    if (!player)
        return;

    uint32 charGuid = player->GetGUID().GetCounter();

    auto it = g_WrathfulItemMap.find(charGuid);
    if (it == g_WrathfulItemMap.end())
        return;

    for (auto const& info : it->second)
    {
        if (Item* item = player->GetItemByGuid(info.itemGuid))
            player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);

        DB_Delete(charGuid, info.itemGuid);
    }

    g_WrathfulItemMap.erase(it);
}

/* =====================================================
 * 로그인 시 DB 기준 정리 (서버 재시작 대비)
 * ===================================================== */
void CleanupWrathfulItemsFromDB(Player* player)
{
    if (!player || player->InBattleground())
        return;

    uint32 charGuid = player->GetGUID().GetCounter();

    QueryResult r = CharacterDatabase.PQuery(
        "SELECT item_guid FROM appdata.wrathful_given_items WHERE char_guid={}",
        charGuid
    );

    if (!r)
        return;

    do
    {
        uint64 raw = r->Fetch()[0].GetUInt64();
        ObjectGuid itemGuid = ObjectGuid::Create<HighGuid::Item>(raw);

        if (Item* item = player->GetItemByGuid(itemGuid))
            player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);

    } while (r->NextRow());

    CharacterDatabase.Execute(
        Trinity::StringFormat(
            "DELETE FROM appdata.wrathful_given_items WHERE char_guid={}",
            charGuid
        ).c_str()
    );
}
