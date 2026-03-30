#pragma once

#include "Player.h"
#include "ObjectGuid.h"
#include <vector>

/* =====================================================
 * Wrathful 지급 아이템 메모리 구조
 * ===================================================== */
struct WrathfulItemMem
{
    uint32     entry;
    ObjectGuid itemGuid;
};

/* =====================================================
 * 외부 공개 함수
 * ===================================================== */

// 지급 기록 (NPC에서 호출)
void RegisterWrathfulItem(Player* player, uint32 entry, ObjectGuid itemGuid);

// 전장 이탈 / 로그아웃 시 회수
void RemoveWrathfulItems(Player* player);

// 서버 재시작 대비: 로그인 시 DB 기준 정리
void CleanupWrathfulItemsFromDB(Player* player);

