// Custom/npc_delivery_depot.cpp
// TrinityCore 3.3.5a
//
// 납품(모든 아이템) -> 포인트 적립 -> 10,000pt = 300RP 환전
// - 백팩(기본 16칸)만 표시
// - 주간 통계: 목 04:00 ~ 다음 목 03:59:59 (서버 로컬타임 기준)
// - 진영 점유율에 따른 가변 보정: 작은쪽 +X / 큰쪽 -X (최대 ±50%)
// - 개인 누적에 따른 감쇠(스팸 방지), 반복 납품 감쇠
// - 보정 상수/공식은 C++ 고정, PHP는 DB 시각화
//
// RP
// - appdata.account_data.id = accountId
// - reg_point 사용 (없으면 INSERT)
//
// 필요 DB:
//  appdata.delivery_log
//  appdata.delivery_week_account
//  appdata.delivery_week_faction
//  appdata.delivery_week_server
//  appdata.account_data (id, reg_point)
//
// [CHANGED]
// - 메인 화면에서 "현재 총포인트(누적)"를 "이번 주 포인트" 위에 표시
//   * 총포인트 = appdata.delivery_week_account 의 points_sum 전체 합(계정 기준)

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "WorldSession.h"
#include "ObjectMgr.h"
#include "Item.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Chat.h"
#include "SharedDefines.h"

#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <cmath>

namespace
{
    // ─────────────────────────────────────────────
    // 운영값
    // ─────────────────────────────────────────────
    static constexpr uint8  BACKPACK_SLOT_START = INVENTORY_SLOT_ITEM_START; // 23
    static constexpr uint8  BACKPACK_SLOT_END   = INVENTORY_SLOT_ITEM_END;   // 39(exclusive) -> 16칸

    static constexpr int    WEEK_START_HOUR = 4; // 목 04:00 기준

    static constexpr uint32 WITHDRAW_POINTS = 10000;
    static constexpr uint32 WITHDRAW_RP     = 700;

    // 개인 감쇠: K/(K+개인주간포인트)
    static constexpr float  PERSONAL_K = 30000.0f;

    // 서버 보정(최근 2주 평균 기준)
    static constexpr float  SERVER_TARGET_WEEK_TOTAL = 2000000.0f; // TODO 서버 규모에 맞게 조절
    static constexpr float  SERVER_MULT_MIN = 0.80f;
    static constexpr float  SERVER_MULT_MAX = 1.10f;

    // 진영 보정: 점유율 격차만큼 대칭 적용 (최대 ±50%)
    static constexpr float  FACTION_MAX_DELTA = 0.50f; // 50%

    // 반복 납품 감쇠
    static constexpr float  REPEAT_DECAY_PER_DELIVERY = 0.95f;
    static constexpr uint32 REPEAT_DECAY_CAP = 50;

    // ─────────────────────────────────────────────
    // 액션
    // ─────────────────────────────────────────────
    enum Action : uint32
    {
        ACT_MAIN     = 1000,
        ACT_LIST     = 1100,
        ACT_WITHDRAW = 1200,
        ACT_CLOSE    = 1999,

        ACT_CONFIRM_BASE = 20000, // +slotIndex(0..15)
        ACT_DELIVER_BASE = 30000  // +slotIndex(0..15)
    };

    // ─────────────────────────────────────────────
    // 유틸
    // ─────────────────────────────────────────────
    static float ClampF(float v, float lo, float hi)
    {
        return std::max(lo, std::min(hi, v));
    }

    static uint32 NowTs()
    {
        return static_cast<uint32>(GameTime::GetGameTime());
    }

    static uint32 AccountId(Player* player)
    {
        return (player && player->GetSession()) ? player->GetSession()->GetAccountId() : 0;
    }

    // 0=Alliance, 1=Horde
    static uint8 FactionId(Player* player)
    {
        if (!player)
            return 0;

        // 3.3.5a: GetTeam() 사용 (ALLIANCE / HORDE)
        return (player->GetTeam() == HORDE) ? 1 : 0;
    }

    // 목 04:00 기준 주 시작 timestamp
    static time_t GetWeekStartTs(time_t now)
    {
        // 04:00을 주 경계로 만들기 위해 4시간 빼서 "날짜 경계"를 잡는다
        time_t shifted = now - WEEK_START_HOUR * 3600;
        tm lt = *localtime(&shifted);

        int wday = lt.tm_wday;                 // 0=일..4=목
        int daysSinceThu = (wday - 4 + 7) % 7; // 목 기준

        lt.tm_mday -= daysSinceThu;
        lt.tm_hour  = 0;
        lt.tm_min   = 0;
        lt.tm_sec   = 0;

        time_t thu00 = mktime(&lt);
        return thu00 + WEEK_START_HOUR * 3600; // 목 04:00
    }

    static uint32 MakeWeekId(time_t weekStartTs)
    {
        tm lt = *localtime(&weekStartTs);
        uint32 y = static_cast<uint32>(lt.tm_year + 1900);
        uint32 m = static_cast<uint32>(lt.tm_mon + 1);
        uint32 d = static_cast<uint32>(lt.tm_mday);
        return y * 10000u + m * 100u + d; // YYYYMMDD(목요일 날짜)
    }

    // ─────────────────────────────────────────────
    // DB helpers (단일 인자 Query/Execute)
    // ─────────────────────────────────────────────
    static QueryResult Q(std::string const& sql)
    {
        return CharacterDatabase.Query(sql.c_str());
    }

    static void E(std::string const& sql)
    {
        CharacterDatabase.Execute(sql.c_str());
    }

    // ─────────────────────────────────────────────
    // RP: appdata.account_data.reg_point (없으면 INSERT)
    // ─────────────────────────────────────────────
    static uint64 GetRP(uint32 accountId)
    {
        std::ostringstream ss;
        ss << "SELECT reg_point FROM appdata.account_data WHERE id=" << accountId << " LIMIT 1";
        QueryResult r = Q(ss.str());
        if (!r)
            return 0;

        Field* f = r->Fetch();
        return f[0].GetUInt64();
    }

    static void AddRP(uint32 accountId, uint32 addRp)
    {
        // UPSERT
        std::ostringstream ss;
        ss << "INSERT INTO appdata.account_data (id, reg_point) "
              "VALUES (" << accountId << ", " << static_cast<uint64>(addRp) << ") "
              "ON DUPLICATE KEY UPDATE reg_point = reg_point + VALUES(reg_point)";
        E(ss.str());
    }

    // ─────────────────────────────────────────────
    // 포인트(계정/주간) 조회
    // ─────────────────────────────────────────────
    static uint64 GetWeeklyAccountPoints(uint32 weekId, uint32 accountId)
    {
        std::ostringstream ss;
        ss << "SELECT points_sum FROM appdata.delivery_week_account "
              "WHERE week_id=" << weekId << " AND account_id=" << accountId << " LIMIT 1";
        QueryResult r = Q(ss.str());
        if (!r)
            return 0;
        return r->Fetch()[0].GetUInt64();
    }

    // [ADDED] 누적 총 포인트(주차 합산)
    static uint64 GetTotalAccountPoints(uint32 accountId)
    {
        std::ostringstream ss;
        ss << "SELECT IFNULL(SUM(delivery_point),0) "
              "FROM appdata.account_data "
              "WHERE id=" << accountId;
        QueryResult r = Q(ss.str());
        if (!r)
            return 0;
        return r->Fetch()[0].GetUInt64();
    }

    static void AddWeeklyAccountPoints(uint32 weekId, uint32 accountId, uint8 faction, uint32 addPoints)
	{
		uint32 now = NowTs();
		std::ostringstream ss;
		ss << "INSERT INTO appdata.delivery_week_account "
			  "(week_id, account_id, faction, points_sum, deliveries, updated_at) VALUES ("
		   << weekId << "," << accountId << "," << static_cast<uint32>(faction) << ","
		   << static_cast<uint64>(addPoints) << ",1," << now << ") "
			  "ON DUPLICATE KEY UPDATE "
			  "points_sum=points_sum+VALUES(points_sum), "
			  "deliveries=deliveries+1, "
			  "updated_at=VALUES(updated_at)";
		E(ss.str());

		// ─────────────────────────────────────────────
		// [ADDED] 전체 누적 포인트(account_data.delivery_point) 증가
		// ─────────────────────────────────────────────
		std::ostringstream ss2;
		ss2 << "INSERT INTO appdata.account_data (id, delivery_point) VALUES ("
			<< accountId << "," << static_cast<uint64>(addPoints) << ") "
			<< "ON DUPLICATE KEY UPDATE "
			<< "delivery_point = delivery_point + VALUES(delivery_point)";
		E(ss2.str());
	}

    static uint64 GetWeeklyFactionPoints(uint32 weekId, uint8 faction)
    {
        std::ostringstream ss;
        ss << "SELECT points_sum FROM appdata.delivery_week_faction "
              "WHERE week_id=" << weekId << " AND faction=" << static_cast<uint32>(faction) << " LIMIT 1";
        QueryResult r = Q(ss.str());
        if (!r)
            return 0;
        return r->Fetch()[0].GetUInt64();
    }

    static void AddWeeklyFactionPoints(uint32 weekId, uint8 faction, uint32 addPoints)
    {
        uint32 now = NowTs();
        std::ostringstream ss;
        ss << "INSERT INTO appdata.delivery_week_faction "
              "(week_id, faction, points_sum, deliveries, updated_at) VALUES ("
           << weekId << "," << static_cast<uint32>(faction) << "," << static_cast<uint64>(addPoints) << ",1," << now << ") "
              "ON DUPLICATE KEY UPDATE "
              "points_sum=points_sum+VALUES(points_sum), "
              "deliveries=deliveries+1, "
              "updated_at=VALUES(updated_at)";
        E(ss.str());
    }

    static uint64 GetWeeklyServerPoints(uint32 weekId)
    {
        std::ostringstream ss;
        ss << "SELECT points_sum FROM appdata.delivery_week_server WHERE week_id=" << weekId << " LIMIT 1";
        QueryResult r = Q(ss.str());
        if (!r)
            return 0;
        return r->Fetch()[0].GetUInt64();
    }

    static void AddWeeklyServerPoints(uint32 weekId, uint32 addPoints)
    {
        uint32 now = NowTs();
        std::ostringstream ss;
        ss << "INSERT INTO appdata.delivery_week_server "
              "(week_id, points_sum, deliveries, updated_at) VALUES ("
           << weekId << "," << static_cast<uint64>(addPoints) << ",1," << now << ") "
              "ON DUPLICATE KEY UPDATE "
              "points_sum=points_sum+VALUES(points_sum), "
              "deliveries=deliveries+1, "
              "updated_at=VALUES(updated_at)";
        E(ss.str());
    }

    // ─────────────────────────────────────────────
    // 보정 계산
    // ─────────────────────────────────────────────
    static float CalcPersonalMult(uint64 personalWeeklyPoints)
    {
        float m = static_cast<float>(personalWeeklyPoints);
        float mult = PERSONAL_K / (PERSONAL_K + m);
        return ClampF(mult, 0.30f, 1.00f);
    }

    static float CalcFactionMult(uint32 weekId, uint8 myFaction)
    {
        uint64 a = GetWeeklyFactionPoints(weekId, 0);
        uint64 h = GetWeeklyFactionPoints(weekId, 1);
        double total = static_cast<double>(a) + static_cast<double>(h);

        if (total <= 1.0)
            return 1.0f;

        // 점유율(호드)
        double pH = static_cast<double>(h) / total; // 0..1
        double diff = std::fabs(pH - 0.5) * 2.0;    // 0..1 (중립=0, 완전쏠림=1)
        float delta = static_cast<float>(diff) * FACTION_MAX_DELTA; // 0..0.5

        // 작은쪽 +delta, 큰쪽 -delta
        bool hIsBig = (pH > 0.5);
        float multH = hIsBig ? (1.0f - delta) : (1.0f + delta);
        float multA = hIsBig ? (1.0f + delta) : (1.0f - delta);

        // 안전 클램프
        multH = ClampF(multH, 0.50f, 1.50f);
        multA = ClampF(multA, 0.50f, 1.50f);

        return (myFaction == 1) ? multH : multA;
    }

    static float CalcServerMult(uint32 thisWeekId)
    {
        // 최근 2주 평균(week_id < thisWeekId)
        std::ostringstream ss;
        ss << "SELECT points_sum FROM appdata.delivery_week_server "
              "WHERE week_id < " << thisWeekId << " ORDER BY week_id DESC LIMIT 2";
        QueryResult r = Q(ss.str());
        if (!r)
            return 1.0f;

        double sum = 0.0;
        uint32 n = 0;
        do
        {
            Field* f = r->Fetch();
            sum += static_cast<double>(f[0].GetUInt64());
            n++;
        } while (r->NextRow() && n < 2);

        if (n == 0)
            return 1.0f;

        double avg = sum / static_cast<double>(n);
        if (avg <= 1.0)
            return 1.0f;

        float mult = static_cast<float>(SERVER_TARGET_WEEK_TOTAL / avg);
        return ClampF(mult, SERVER_MULT_MIN, SERVER_MULT_MAX);
    }

    static float CalcRepeatMult(uint32 weekId, uint32 accountId, uint32 itemEntry)
    {
        std::ostringstream ss;
        ss << "SELECT COUNT(*) FROM appdata.delivery_log "
              "WHERE week_id=" << weekId << " AND account_id=" << accountId << " AND item_entry=" << itemEntry;
        QueryResult r = Q(ss.str());

        uint32 cnt = 0;
        if (r)
            cnt = r->Fetch()[0].GetUInt32();

        uint32 exp = std::min(cnt, REPEAT_DECAY_CAP);
        float mult = std::pow(REPEAT_DECAY_PER_DELIVERY, static_cast<float>(exp));
        return ClampF(mult, 0.35f, 1.00f);
    }

    // ─────────────────────────────────────────────
    // 아이템 기반 점수
    // ─────────────────────────────────────────────
    static float GetQualityMult(uint32 quality)
    {
        switch (quality)
        {
            case ITEM_QUALITY_UNCOMMON: return 0.30f; // green
            case ITEM_QUALITY_RARE:     return 0.60f; // blue
            case ITEM_QUALITY_EPIC:     return 1.00f; // purple
            default:
                if (quality <= ITEM_QUALITY_NORMAL) // white 이하
                    return 0.10f;
                return 0.20f;
        }
    }

    static float GetClassMult(ItemTemplate const* proto)
	{
		if (!proto)
			return 1.0f;

		// 1) 슬롯 보정(액세서리 가산) : "추가"로 곱해지게
		float slotMult = 1.0f;
		switch (proto->InventoryType)
		{
			case INVTYPE_TRINKET: slotMult = 1.15f; break;
			case INVTYPE_FINGER:  slotMult = 1.10f; break;
			case INVTYPE_NECK:    slotMult = 1.08f; break;
			case INVTYPE_CLOAK:   slotMult = 1.05f; break;
			default: break;
		}

		// 2) 클래스 보정(기본 뼈대)
		float classMult = 1.0f;
		switch (proto->Class)
		{
			case ITEM_CLASS_WEAPON:        classMult = 1.60f; break;
			case ITEM_CLASS_ARMOR:         classMult = 1.30f; break;
			case ITEM_CLASS_CONSUMABLE:    classMult = 0.60f; break;
			case ITEM_CLASS_TRADE_GOODS:   classMult = 0.70f; break;
			case ITEM_CLASS_GEM:           classMult = 0.85f; break;
			case ITEM_CLASS_RECIPE:        classMult = 0.50f; break;
			case ITEM_CLASS_MISCELLANEOUS: classMult = 0.20f; break;
			default:                       classMult = 0.80f; break;
		}

		// 3) 무기/방어구만 등급(품질) 추가 보정 (과하지 않게)
		float qualityExtra = 1.0f;
		if (proto->Class == ITEM_CLASS_WEAPON)
		{
			switch (proto->Quality)
			{
				case ITEM_QUALITY_UNCOMMON: qualityExtra = 1.20f; break; // 초록
				case ITEM_QUALITY_RARE:     qualityExtra = 1.45f; break; // 파랑
				case ITEM_QUALITY_EPIC:     qualityExtra = 1.60f; break; // 보라
				default: break;
			}
		}
		else if (proto->Class == ITEM_CLASS_ARMOR)
		{
			switch (proto->Quality)
			{
				case ITEM_QUALITY_UNCOMMON: qualityExtra = 1.10f; break;
				case ITEM_QUALITY_RARE:     qualityExtra = 1.30f; break;
				case ITEM_QUALITY_EPIC:     qualityExtra = 1.60f; break;
				default: break;
			}
		}

		return slotMult * classMult * qualityExtra;
	}


    static uint32 CalcBasePoints(ItemTemplate const* proto, uint32 qty)
    {
        if (!proto || qty == 0)
            return 0;

        bool isEquip =
            (proto->Class == ITEM_CLASS_WEAPON) ||
            (proto->Class == ITEM_CLASS_ARMOR)  ||
            (proto->InventoryType == INVTYPE_TRINKET) ||
            (proto->InventoryType == INVTYPE_FINGER)  ||
            (proto->InventoryType == INVTYPE_NECK)    ||
            (proto->InventoryType == INVTYPE_CLOAK);

        if (isEquip)
            return static_cast<uint32>(proto->ItemLevel) * qty;

        // 소모/재료 등은 판매가 기반(실버 단위)
        uint64 vendor = static_cast<uint64>(proto->SellPrice);
        uint64 base = (vendor / 100ull) * static_cast<uint64>(qty);
        if (base > 0xFFFFFFFFull) base = 0xFFFFFFFFull;
        return static_cast<uint32>(base);
    }

    static uint32 ApplyAll(
        uint32 basePoints,
        float classMult,
        float qualityMult,
        float personalMult,
        float factionMult,
        float serverMult,
        float repeatMult)
    {
        double v = static_cast<double>(basePoints);
        v *= static_cast<double>(classMult);
        v *= static_cast<double>(qualityMult);
        v *= static_cast<double>(personalMult);
        v *= static_cast<double>(factionMult);
        v *= static_cast<double>(serverMult);
        v *= static_cast<double>(repeatMult);

        if (v < 0.0) v = 0.0;
        if (v > 4294967295.0) v = 4294967295.0;
        return static_cast<uint32>(v + 0.5);
    }

    static std::string MakeMultJson(
        uint32 weekId,
        uint32 basePoints,
        uint32 finalPoints,
        float classMult,
        float qualityMult,
        float personalMult,
        float factionMult,
        float serverMult,
        float repeatMult)
    {
        // 단순 JSON 문자열(시각화용)
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(4);
        ss << "{"
           << "\"week_id\":" << weekId << ","
           << "\"base\":" << basePoints << ","
           << "\"final\":" << finalPoints << ","
           << "\"mult\":{"
           << "\"class\":" << classMult << ","
           << "\"quality\":" << qualityMult << ","
           << "\"personal\":" << personalMult << ","
           << "\"faction\":" << factionMult << ","
           << "\"server\":" << serverMult << ","
           << "\"repeat\":" << repeatMult
           << "}"
           << "}";
        return ss.str();
    }

    // ─────────────────────────────────────────────
    // UI
    // ─────────────────────────────────────────────
    static void ShowMain(Player* player, Creature* me)
    {
        uint32 accountId = AccountId(player);
        uint8  faction   = FactionId(player);

        uint32 now = NowTs();
        time_t weekStart = GetWeekStartTs(static_cast<time_t>(now));
        uint32 weekId = MakeWeekId(weekStart);

        // [ADDED] 총포인트(누적)
        uint64 totalPts = GetTotalAccountPoints(accountId);

        uint64 myPts = GetWeeklyAccountPoints(weekId, accountId);
        uint64 aPts  = GetWeeklyFactionPoints(weekId, 0);
        uint64 hPts  = GetWeeklyFactionPoints(weekId, 1);

        double total = static_cast<double>(aPts) + static_cast<double>(hPts);
        double pH = (total > 1.0) ? static_cast<double>(hPts) / total : 0.5;

        float personalMult = CalcPersonalMult(myPts);
        float factionMult  = CalcFactionMult(weekId, faction);
        float serverMult   = CalcServerMult(weekId);

        uint64 rp = GetRP(accountId);

        ClearGossipMenuFor(player);

        {
            std::ostringstream ss;
            ss.setf(std::ios::fixed);
            ss.precision(2);
            ss << "납품소(주간)\n"
               << "현재 총포인트: " << totalPts << "\n"          // ✅ 총포인트를 위에
               << "이번 주 포인트: " << myPts << "\n"
               << "RP: " << rp << "\n"
               << "진영 점유율(호드): " << (pH * 100.0) << "%\n"
               << "개인 x" << personalMult << " / 진영 x" << factionMult << " / 서버 x" << serverMult << "\n"
               << "환전: " << WITHDRAW_POINTS << "pt -> " << WITHDRAW_RP << "RP";
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, ss.str(), GOSSIP_SENDER_MAIN, ACT_LIST);
        }

        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "10,000pt -> 700RP 환전(1회)", GOSSIP_SENDER_MAIN, ACT_WITHDRAW);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "닫기", GOSSIP_SENDER_MAIN, ACT_CLOSE);

        SendGossipMenuFor(player, 1, me->GetGUID());
    }

    static void ShowList(Player* player, Creature* me)
    {
        uint32 accountId = AccountId(player);
        uint8  faction   = FactionId(player);

        uint32 now = NowTs();
        time_t weekStart = GetWeekStartTs(static_cast<time_t>(now));
        uint32 weekId = MakeWeekId(weekStart);

        uint64 myPts = GetWeeklyAccountPoints(weekId, accountId);

        float personalMult = CalcPersonalMult(myPts);
        float factionMult  = CalcFactionMult(weekId, faction);
        float serverMult   = CalcServerMult(weekId);

        ClearGossipMenuFor(player);

        {
            std::ostringstream head;
            head.setf(std::ios::fixed);
            head.precision(2);
            head << "백팩(16칸) 납품 목록\n"
                 << "개인 x" << personalMult << " / 진영 x" << factionMult << " / 서버 x" << serverMult;
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, head.str(), GOSSIP_SENDER_MAIN, ACT_LIST);
        }

        bool any = false;

        for (uint8 slot = BACKPACK_SLOT_START; slot < BACKPACK_SLOT_END; ++slot)
        {
            Item* it = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!it)
                continue;

            ItemTemplate const* proto = it->GetTemplate();
            if (!proto)
                continue;

			// ─────────────────────────────────────────────
			// [ADDED] 특정 아이템 제외 (45229)
			// ─────────────────────────────────────────────
			if (proto->ItemId == 45229)
				continue;

            uint32 qty = it->GetCount();
            uint32 base = CalcBasePoints(proto, qty);

            float classMult   = GetClassMult(proto);
            float qualityMult = GetQualityMult(proto->Quality);
            float repeatMult  = CalcRepeatMult(weekId, accountId, proto->ItemId);

            uint32 finalPts = ApplyAll(base, classMult, qualityMult, personalMult, factionMult, serverMult, repeatMult);

            uint8 slotIndex = slot - BACKPACK_SLOT_START;

            std::ostringstream line;
            line << proto->Name1
                 << " x" << qty
                 << " (기본 " << base << " -> +" << finalPts << "pt)";

            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, line.str(), GOSSIP_SENDER_MAIN, ACT_CONFIRM_BASE + slotIndex);
            any = true;
        }

        if (!any)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "백팩에 납품할 아이템이 없습니다.", GOSSIP_SENDER_MAIN, ACT_LIST);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "메인으로", GOSSIP_SENDER_MAIN, ACT_MAIN);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "닫기",     GOSSIP_SENDER_MAIN, ACT_CLOSE);

        SendGossipMenuFor(player, 1, me->GetGUID());
    }

    static void ShowConfirm(Player* player, Creature* me, uint8 slotIndex)
    {
        uint8 slot = BACKPACK_SLOT_START + slotIndex;
        Item* it = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!it)
        {
            ShowList(player, me);
            return;
        }

        uint32 accountId = AccountId(player);
        uint8  faction   = FactionId(player);

        uint32 now = NowTs();
        time_t weekStart = GetWeekStartTs(static_cast<time_t>(now));
        uint32 weekId = MakeWeekId(weekStart);

        ItemTemplate const* proto = it->GetTemplate();
        uint32 qty = it->GetCount();

        uint64 myPts = GetWeeklyAccountPoints(weekId, accountId);

        float personalMult = CalcPersonalMult(myPts);
        float factionMult  = CalcFactionMult(weekId, faction);
        float serverMult   = CalcServerMult(weekId);

        uint32 base = CalcBasePoints(proto, qty);
        float classMult   = GetClassMult(proto);
        float qualityMult = GetQualityMult(proto->Quality);
        float repeatMult  = CalcRepeatMult(weekId, accountId, proto->ItemId);

        uint32 finalPts = ApplyAll(base, classMult, qualityMult, personalMult, factionMult, serverMult, repeatMult);

        ClearGossipMenuFor(player);

        {
            std::ostringstream ss;
            ss.setf(std::ios::fixed);
            ss.precision(2);
            ss << "납품 확인\n"
               << proto->Name1 << " x" << qty << "\n"
               << "기본 " << base << "\n"
               << "종류 x" << classMult << ", 품질 x" << qualityMult << "\n"
               << "개인 x" << personalMult << ", 진영 x" << factionMult << ", 서버 x" << serverMult << ", 반복 x" << repeatMult << "\n"
               << "예상 적립: " << finalPts << "pt";
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, ss.str(), GOSSIP_SENDER_MAIN, ACT_CONFIRM_BASE + slotIndex);
        }

        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "이 아이템을 납품한다", GOSSIP_SENDER_MAIN, ACT_DELIVER_BASE + slotIndex);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "목록으로", GOSSIP_SENDER_MAIN, ACT_LIST);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "닫기",     GOSSIP_SENDER_MAIN, ACT_CLOSE);

        SendGossipMenuFor(player, 1, me->GetGUID());
    }

    static void DoDeliver(Player* player, Creature* me, uint8 slotIndex)
    {
        uint8 slot = BACKPACK_SLOT_START + slotIndex;
        Item* it = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!it)
        {
            ChatHandler(player->GetSession()).SendSysMessage("아이템이 없습니다.");
            ShowList(player, me);
            return;
        }

        uint32 accountId = AccountId(player);
        uint8  faction   = FactionId(player);

        uint32 now = NowTs();
        time_t weekStart = GetWeekStartTs(static_cast<time_t>(now));
        uint32 weekId = MakeWeekId(weekStart);

        ItemTemplate const* proto = it->GetTemplate();
        if (!proto)
        {
            ChatHandler(player->GetSession()).SendSysMessage("템플릿을 찾을 수 없습니다.");
            ShowList(player, me);
            return;
        }

        uint32 qty = it->GetCount();

        uint64 myPts = GetWeeklyAccountPoints(weekId, accountId);

        float classMult    = GetClassMult(proto);
        float qualityMult  = GetQualityMult(proto->Quality);
        float personalMult = CalcPersonalMult(myPts);
        float factionMult  = CalcFactionMult(weekId, faction);
        float serverMult   = CalcServerMult(weekId);
        float repeatMult   = CalcRepeatMult(weekId, accountId, proto->ItemId);

        uint32 base = CalcBasePoints(proto, qty);
        uint32 finalPts = ApplyAll(base, classMult, qualityMult, personalMult, factionMult, serverMult, repeatMult);

        if (finalPts == 0)
        {
            ChatHandler(player->GetSession()).SendSysMessage("이 아이템은 납품 가치가 없습니다.");
            ShowList(player, me);
            return;
        }

        // 삭제(스택 전체)
        player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);

        // mult_json
        std::string multJson = MakeMultJson(weekId, base, finalPts, classMult, qualityMult, personalMult, factionMult, serverMult, repeatMult);
        CharacterDatabase.EscapeString(multJson);

        uint32 charGuid = static_cast<uint32>(player->GetGUID().GetCounter());

        std::ostringstream ins;
        ins << "INSERT INTO appdata.delivery_log "
               "(ts, week_id, account_id, char_guid, faction, item_entry, item_class, item_subclass, item_quality, item_level, inventory_type, vendor_copper, qty, base_points, final_points, mult_json) "
               "VALUES ("
            << now << ","
            << weekId << ","
            << accountId << ","
            << charGuid << ","
            << static_cast<uint32>(faction) << ","
            << proto->ItemId << ","
            << static_cast<uint32>(proto->Class) << ","
            << static_cast<uint32>(proto->SubClass) << ","
            << static_cast<uint32>(proto->Quality) << ","
            << static_cast<uint32>(proto->ItemLevel) << ","
            << static_cast<uint32>(proto->InventoryType) << ","
            << static_cast<uint32>(proto->SellPrice) << ","
            << qty << ","
            << base << ","
            << finalPts << ",'"
            << multJson << "')";

        E(ins.str());

        AddWeeklyAccountPoints(weekId, accountId, faction, finalPts);
        AddWeeklyFactionPoints(weekId, faction, finalPts);
        AddWeeklyServerPoints(weekId, finalPts);

        std::ostringstream ss;
        ss << "납품 완료: " << finalPts << "pt 적립 (" << proto->Name1 << " x" << qty << ")";
        ChatHandler(player->GetSession()).PSendSysMessage("%s", ss.str().c_str());

        ShowList(player, me);
    }

    static void DoWithdraw(Player* player, Creature* me)
	{
		uint32 accountId = AccountId(player);

		uint32 now = NowTs();

		// ─────────────────────────────────────────────
		// 1️⃣ 전체 누적 포인트 조회
		// ─────────────────────────────────────────────
		uint64 totalPts = 0;
		{
			std::ostringstream ss;
			ss << "SELECT delivery_point FROM appdata.account_data WHERE id=" << accountId << " LIMIT 1";
			QueryResult r = Q(ss.str());
			if (r)
				totalPts = r->Fetch()[0].GetUInt64();
		}

		if (totalPts < WITHDRAW_POINTS)
		{
			std::ostringstream ss;
			ss << "포인트가 부족합니다. (필요: " << WITHDRAW_POINTS << "pt, 현재: " << totalPts << "pt)";
			ChatHandler(player->GetSession()).PSendSysMessage("%s", ss.str().c_str());
			ShowMain(player, me);
			return;
		}

		// ─────────────────────────────────────────────
		// 2️⃣ delivery_point 차감 (동시성 방지)
		// ─────────────────────────────────────────────
		std::ostringstream up;
		up << "UPDATE appdata.account_data "
			  "SET delivery_point = delivery_point - " << WITHDRAW_POINTS << " "
			  "WHERE id=" << accountId << " AND delivery_point >= " << WITHDRAW_POINTS;
		E(up.str());

		// ─────────────────────────────────────────────
		// 3️⃣ RP 지급
		// ─────────────────────────────────────────────
		AddRP(accountId, WITHDRAW_RP);

		std::ostringstream ss;
		ss << "환전 완료: " << WITHDRAW_RP << "RP 지급 (" << WITHDRAW_POINTS << "pt 차감)";
		ChatHandler(player->GetSession()).PSendSysMessage("%s", ss.str().c_str());

		ShowMain(player, me);
	}
}

// ─────────────────────────────────────────────
// Script (npc_item_exchange 스타일)
// ─────────────────────────────────────────────
class npc_delivery_depot : public CreatureScript
{
public:
    npc_delivery_depot() : CreatureScript("npc_delivery_depot") { }

    struct npc_delivery_depotAI : public ScriptedAI
    {
        npc_delivery_depotAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            if (!player)
                return false;

            ShowMain(player, me);
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            if (!player)
                return false;

            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            switch (action)
            {
                case ACT_CLOSE:
                    CloseGossipMenuFor(player);
                    return true;

                case ACT_MAIN:
                    ShowMain(player, me);
                    return true;

                case ACT_LIST:
                    ShowList(player, me);
                    return true;

                case ACT_WITHDRAW:
					CloseGossipMenuFor(player);
                    DoWithdraw(player, me);
                    return true;

                default:
                    break;
            }

            // confirm
            if (action >= ACT_CONFIRM_BASE && action < ACT_CONFIRM_BASE + 16)
            {
                uint8 slotIndex = static_cast<uint8>(action - ACT_CONFIRM_BASE);
                ShowConfirm(player, me, slotIndex);
                return true;
            }

            // deliver
            if (action >= ACT_DELIVER_BASE && action < ACT_DELIVER_BASE + 16)
            {
                uint8 slotIndex = static_cast<uint8>(action - ACT_DELIVER_BASE);
                DoDeliver(player, me, slotIndex);
                return true;
            }

            ShowMain(player, me);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_delivery_depotAI(creature);
    }
};

void AddSC_npc_delivery_depot()
{
    new npc_delivery_depot();
}
