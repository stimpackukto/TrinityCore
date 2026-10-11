#include "wrathful_set_mgr.h"

#include "DatabaseEnv.h"
#include "Item.h"
#include "Log.h"

/* =====================================================
 * 전장 임시 장비 지급 기록
 * - 지급된 아이템 GUID만 DB에 기록
 * - 회수는 DB를 단일 기준으로 사용
 * ===================================================== */
void RegisterWrathfulItem(Player* player, uint32 entry, ObjectGuid itemGuid)
{
    if (!player || !itemGuid.IsItem())
        return;

    // 지급 직후 전장 퇴장/로그아웃이 발생해도 회수 기록이 누락되지 않게 동기 저장.
    CharacterDatabase.DirectPExecute(
        "INSERT INTO appdata.wrathful_given_items "
        "(char_guid, item_guid, item_entry) VALUES ({}, {}, {})",
        player->GetGUID().GetCounter(),
        itemGuid.GetRawValue(),
        entry);
}

/* =====================================================
 * 전장 이탈 / 로그아웃 시 지급 장비 회수
 * - DB에 기록된 정확한 아이템 GUID와 entry만 삭제
 * - 플레이어가 원래 보유한 같은 종류의 아이템은 보존
 * - 로그아웃 시 전장 참가 상태여도 회수
 * ===================================================== */
void RemoveWrathfulItems(Player* player)
{
    if (!player)
        return;

    uint32 charGuid = player->GetGUID().GetCounter();
    QueryResult result = CharacterDatabase.PQuery(
        "SELECT item_guid, item_entry "
        "FROM appdata.wrathful_given_items WHERE char_guid={}",
        charGuid);

    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        uint64 rawItemGuid = fields[0].GetUInt64();
        uint32 expectedEntry = fields[1].GetUInt32();

        // Existing records store the full 64-bit GUID.
        // Create<HighGuid::Item>() expects only the 32-bit counter, not the raw GUID.
        ObjectGuid itemGuid = ObjectGuid::Create<HighGuid::Item>(uint32(rawItemGuid));

        if (Item* item = player->GetItemByGuid(itemGuid))
        {
            // Never destroy an unrelated item if a saved record is inconsistent.
            if (item->GetGUID().GetRawValue() != rawItemGuid || item->GetEntry() != expectedEntry)
            {
                TC_LOG_ERROR("scripts", "Wrathful rental: mismatched item for character {} / item {} / entry {}; preserving item",
                    charGuid, rawItemGuid, expectedEntry);
                continue;
            }

            player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);

            // Keep the record for a later retry if the item could not be removed.
            if (player->GetItemByGuid(itemGuid))
            {
                TC_LOG_ERROR("scripts", "Wrathful rental: failed to remove item {} from character {}",
                    rawItemGuid, charGuid);
                continue;
            }
        }

        // DestroyItem removes the in-memory item before SaveToDB persists it.
        // Keep the rental record until the item_instance DB row is really gone:
        // a crash between removal and character save must not restore untracked gear.
        QueryResult stillExists = CharacterDatabase.PQuery(
            "SELECT 1 FROM item_instance WHERE guid={} LIMIT 1",
            itemGuid.GetCounter());

        if (stillExists)
            continue;

        CharacterDatabase.DirectPExecute(
            "DELETE FROM appdata.wrathful_given_items "
            "WHERE char_guid={} AND item_guid={} AND item_entry={}",
            charGuid, rawItemGuid, expectedEntry);

    } while (result->NextRow());
}

/* =====================================================
 * 로그인 / 전장 퇴장 / 맵 이동 시 남은 대여 장비 정리
 * ===================================================== */
void CleanupWrathfulItemsFromDB(Player* player)
{
    if (!player || player->InBattleground())
        return;

    RemoveWrathfulItems(player);
}
