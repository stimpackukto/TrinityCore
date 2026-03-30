// Custom/icc_rental_mgr.h
// ICC(얼음왕관 성채) 아이템 렌탈 매니저
// - "로그인 상태에서만" 시간 감소
// - 로그아웃 시 시간 멈춤(남은 시간 저장)
// - 로그인 시 남은 시간 <= 0 이면(인던/전투 중이어도) 즉시 회수
// - 10분 전 경고(remain_seconds <= 600)
// - 동시 렌탈 1개 정책(캐릭터당 1개만 active)

#pragma once

#include "ObjectGuid.h"
#include <unordered_map>
#include <string>

class Player;

namespace ICCRental
{
    static constexpr uint32 RENT_SECONDS        = 7200;   // 2시간
    static constexpr uint32 WARN_SECONDS        = 600;    // 10분
    static constexpr uint32 TICK_UPDATE_SECONDS = 30;     // 월드 주기 검사(초)
    static constexpr uint32 PRICE_RP            = 35000;  // 3만5천 RP

    struct RentalState
    {
        uint32     charGuid      = 0;     // player lowguid(counter)
        ObjectGuid playerGuid    = ObjectGuid::Empty;
        uint32     itemEntry     = 0;
        ObjectGuid itemGuid      = ObjectGuid::Empty;

        int32      remainSeconds = 0;     // 온라인에서만 감소
        uint8      warned10min   = 0;     // 0/1
        uint32     lastUpdateTs  = 0;     // 마지막 차감 기준(UNIX)
        uint32     nextDbSaveTs  = 0;     // DB 저장 빈도 제어(UNIX)
    };

    // ====== DB / 상태 ======
    bool HasActiveRental(uint32 charGuid);
    bool LoadActiveRental(Player* player);          // 로그인 시: DB -> 메모리, 만료면 즉시 회수
    void SaveOnLogout(Player* player);              // 로그아웃 시: 남은 시간 저장(멈춤)
    void UpdateAll(uint32 diffMs);                  // 월드 주기 루프(온라인 시간 감소 / 만료 처리)

    // ====== 지급 / 회수 ======
    bool StartRental(Player* player, uint32 itemEntry, std::string const& itemNameForMsg);
    void ForceRemove(Player* player, RentalState const& st, bool silent);

    // ====== NPC 검색 지원 ======
    struct SearchHit
    {
        uint32 entry = 0;
        std::string name;
    };

    void CacheSearchResult(uint32 charGuid, std::vector<SearchHit> hits);
    std::vector<SearchHit> const* GetCachedSearch(uint32 charGuid);
    void ClearCachedSearch(uint32 charGuid);

    void SetPendingPick(uint32 charGuid, uint32 entry, std::string const& name);
    bool GetPendingPick(uint32 charGuid, uint32& outEntry, std::string& outName);
    void ClearPendingPick(uint32 charGuid);
} // namespace ICCRental
