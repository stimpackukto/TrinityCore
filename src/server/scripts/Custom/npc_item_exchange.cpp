// Custom/npc_item_exchange.cpp
// TrinityCore 3.3.5a
//
// 풀(pool) 방식 아이템 교환 NPC
// - appdata.item_exchange_pairs
//   * source_entry > 0, target_entry = 0 : 소스 풀
//   * source_entry = 0, target_entry > 0 : 타겟 풀
// - 플레이어 백팩(기본 16칸)만 스캔해서 "소스 풀"에 등록된 아이템만 표시
// - [CHANGED] 소스 리스트는 "플레이어가 착용/사용 가능한 아이템만" 표시
// - [CHANGED] 타겟은 소스 아이템과 속성이 동일한 것만 자동 필터링하여 표시
//   (Class / SubClass / InventoryType 동일)   <-- 3개만 검수
// - [CHANGED/FIX] 타겟 리스트는 진영(얼라/호드) 전용 아이템이면 플레이어 진영에 맞는 것만 표시
//   * AllowableRace가 아니라, item_template.Flags2(ITEM_FLAG2_FACTION_*)를 우선 반영
// - [CHANGED] "사용흔적"은 추적/마킹 없이 단순 판별
//   * playedTime == 0 : 신품
//   * playedTime >= 1 : 중고
// - 목록 라벨 앞에 아이템 아이콘 표시(암시장 스타일)
// - 타겟 선택 후 확인(Preview)로 넘어갈 때 채팅창에 "타겟 아이템 링크"를 출력
//
// RP
// - appdata.account_data.reg_point 사용
//
// 적용
// 1) AddSC_npc_item_exchange() 를 custom_scripts.cpp 등에 등록
// 2) world.creature_template.ScriptName = 'npc_item_exchange'
// 3) npcflag에 gossip(1) 포함
//
// 예시:
// UPDATE world.creature_template
// SET ScriptName='npc_item_exchange', npcflag = npcflag | 1
// WHERE entry=12345;

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "WorldSession.h"
#include "ObjectMgr.h"
#include "Item.h"
#include "DatabaseEnv.h"
#include "Chat.h"

#include <unordered_map>
#include <unordered_set>
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
    static constexpr uint32 PAGE_SIZE      = 10;
    static constexpr uint32 BASE_PER_ILVL  = 300;
    static constexpr uint32 MIN_RP_FLOOR   = 10000;

    static constexpr float  BIND_MULT_SOULBOUND = 1.15f;
    static constexpr float  BIND_MULT_UNBOUND   = 1.00f;

    // 신품/중고 2단계
    static constexpr float  USED_MULT_NEW  = 1.00f;
    static constexpr float  USED_MULT_USED = 1.20f; // 중고 가중치(원하는 값으로 조절)

	static constexpr uint32 MAX_ITEM_LEVEL_GAP = 45;

	// ─────────────────────────────────────────────
	// [ADD] 불일치 가중치
	// ─────────────────────────────────────────────

	// 소스 착용 불가 페널티
	static constexpr float SRC_PENALTY_ARMOR      = 1.30f;
	static constexpr float SRC_PENALTY_WEAPON_1H  = 1.35f;
	static constexpr float SRC_PENALTY_WEAPON_2H  = 1.45f;
	static constexpr float SRC_PENALTY_WEAPON_RNG = 1.35f;
	static constexpr float SRC_PENALTY_WEAPON_ETC = 1.30f;

	// 무기 종류(SubClass) 불일치
	static constexpr float WEAPON_KIND_MISMATCH_MULT = 1.25f;


    // 뽀글이 첫인사/말투 유틸
    static void PpoggeuliHello(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        ChatHandler(player->GetSession()).SendSysMessage(
            "뽀글이: 야~ 가방 열어봐! 규격만 맞으면 바꿔준다 ! "
        );
    }

    // ─────────────────────────────────────────────
    // 액션(여관주인/암시장 스타일)
    // ─────────────────────────────────────────────
    enum Action : uint32
    {
        ACT_MAIN   = 1000,
        ACT_HELP   = 1001,
        ACT_START  = 1100,

        ACT_SRC_NEXT = 1201,
        ACT_SRC_PREV = 1202,

        ACT_TGT_NEXT = 1301,
        ACT_TGT_PREV = 1302,

        ACT_PREVIEW  = 1400,
        ACT_EXECUTE  = 1401,

        ACT_RESET_TO_SRC = 1500,
        ACT_RESET_TO_TGT = 1501,

        ACT_CLOSE  = 1999,

        ACT_PICK_SOURCE_BASE = 20000, // + idx(0..PAGE_SIZE-1)
        ACT_PICK_TARGET_BASE = 30000  // + idx(0..PAGE_SIZE-1)
    };

    // ─────────────────────────────────────────────
    // 상태(플레이어별)
    // ─────────────────────────────────────────────
    struct State
    {
        uint32 srcPage = 0;
        uint32 tgtPage = 0;

        // 소스(실제 아이템)
        uint32     srcEntry = 0;
        ObjectGuid srcGuid;
        uint32     srcIlvl = 0;
        uint32     srcQuality = 0;
        bool       srcBound = false;

        // 신품/중고(playedTime 기반)
        bool       srcUsed = false;

        // 타겟(엔트리)
        uint32      tgtEntry = 0;
        uint32      tgtIlvl = 0;
        uint32      tgtQuality = 0;
        std::string tgtSlotType;

		bool srcUsable = true; // [ADD] 소스 착용 가능 여부
    };

    static std::unordered_map<uint32, State> g_state;

    static uint32 PlayerKey(Player* player)
    {
        return player ? player->GetGUID().GetCounter() : 0;
    }

    static State& ST(Player* player)
    {
        return g_state[PlayerKey(player)];
    }

    static void ResetAll(Player* player)
    {
        ST(player) = State{};
    }

    static void ResetPickTargetOnly(Player* player)
    {
        auto& s = ST(player);
        s.tgtPage = 0;
        s.tgtEntry = 0;
        s.tgtIlvl = 0;
        s.tgtQuality = 0;
        s.tgtSlotType.clear();
    }

    static void ResetToSource(Player* player)
    {
        auto& s = ST(player);
        s.srcPage = 0;
        s.tgtPage = 0;

        s.srcEntry = 0;
        s.srcGuid.Clear();
        s.srcIlvl = 0;
        s.srcQuality = 0;
        s.srcBound = false;

        s.srcUsed = false;

        ResetPickTargetOnly(player);
    }

    // ─────────────────────────────────────────────
    // 아이템 정보 유틸
    // ─────────────────────────────────────────────
    static std::string QualityName(uint32 q)
    {
        switch (q)
        {
            case 2: return "G";
            case 3: return "B";
            case 4: return "P";
            case 5: return "O";
            default: return "기타";
        }
    }

    // SlotTypeFor는 "토큰(영문)"으로 반환 (비용 계산과 맞추기)
    static std::string SlotTypeFor(ItemTemplate const* proto)
    {
        if (!proto)
            return "ARMOR";

        switch (proto->InventoryType)
        {
            case INVTYPE_TRINKET: return "TRINKET";
            case INVTYPE_FINGER:  return "RING";
            case INVTYPE_NECK:    return "NECK";
            case INVTYPE_CLOAK:   return "CLOAK";
            default: break;
        }

        if (proto->Class == ITEM_CLASS_WEAPON)
            return "WEAPON";

        return "ARMOR";
    }

    // UI 표기용 한글 라벨
    static std::string SlotTypeLabel(std::string const& token)
    {
        if (token == "WEAPON")  return "무기";
        if (token == "TRINKET") return "장신구";
        if (token == "RING")    return "반지";
        if (token == "NECK")    return "목걸이";
        if (token == "CLOAK")   return "망토";
        return "방어구";
    }

    static std::string UsedLabel(bool used)
    {
        return used ? "중고" : "신품";
    }

    static std::string ItemName(uint32 entry)
    {
        if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry))
            return proto->Name1;

        std::ostringstream ss;
        ss << "item#" << entry;
        return ss.str();
    }

    // ─────────────────────────────────────────────
    // [FIX] 진영 판별 (타겟 전용 필터)
    // - 1순위: item_template.Flags2 (ITEM_FLAG2_FACTION_HORDE/ALLIANCE)
    // - 2순위: AllowableRace(종족 제한)로 보조 판별
    // ─────────────────────────────────────────────
    enum class FactionLock : uint8
    {
        BOTH = 0,
        ALLIANCE = 1,
        HORDE = 2
    };

    // WotLK 종족 비트 (TrinityCore ItemTemplate.AllowableRace는 RaceMask 비트)
    static constexpr uint32 RACE_MASK_ALLIANCE =
        (1u /*Human*/ | 4u /*Dwarf*/ | 8u /*NightElf*/ | 64u /*Gnome*/ | 1024u /*Draenei*/);

    static constexpr uint32 RACE_MASK_HORDE =
        (2u /*Orc*/ | 16u /*Undead*/ | 32u /*Tauren*/ | 128u /*Troll*/ | 512u /*BloodElf*/);

    static FactionLock FactionFromFlags2(uint32 flags2)
    {
        bool h = (flags2 & ITEM_FLAG2_FACTION_HORDE) != 0;
        bool a = (flags2 & ITEM_FLAG2_FACTION_ALLIANCE) != 0;

        if (h && !a) return FactionLock::HORDE;
        if (a && !h) return FactionLock::ALLIANCE;
        return FactionLock::BOTH;
    }

    static FactionLock FactionFromAllowableRace(uint32 raceMask)
    {
        if (raceMask == 0)
            return FactionLock::BOTH;

        bool hasA = (raceMask & RACE_MASK_ALLIANCE) != 0;
        bool hasH = (raceMask & RACE_MASK_HORDE) != 0;

        if (hasA && !hasH) return FactionLock::ALLIANCE;
        if (hasH && !hasA) return FactionLock::HORDE;
        return FactionLock::BOTH;
    }

    static FactionLock PlayerFaction(Player* player)
    {
        if (!player)
            return FactionLock::BOTH;

        return player->GetTeam() == ALLIANCE ? FactionLock::ALLIANCE : FactionLock::HORDE;
    }

    static bool IsTargetFactionOkForPlayer(Player* player, ItemTemplate const* tgt)
    {
        if (!player || !tgt)
            return false;

        // 1순위: Flags2 진영 플래그
        {
            FactionLock lock = FactionFromFlags2(tgt->Flags2);
            if (lock != FactionLock::BOTH)
                return lock == PlayerFaction(player);
        }

        // 2순위: AllowableRace 보조 판별(없거나 혼합이면 BOTH)
        {
            FactionLock lock = FactionFromAllowableRace(tgt->AllowableRace);
            if (lock == FactionLock::BOTH)
                return true;

            return lock == PlayerFaction(player);
        }
    }

    // ─────────────────────────────────────────────
    // 채팅용 아이템 링크 생성 + 전송
    // ─────────────────────────────────────────────
    static std::string MakeItemChatLink(uint32 entry)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
        std::string name = proto ? proto->Name1 : "Unknown Item";

        std::ostringstream ss;
        ss << "|cffffffff|Hitem:" << entry << ":0:0:0:0:0:0:0|h[" << name << "]|h|r";
        return ss.str();
    }

    static void SendTargetLinkToChat(Player* player, uint32 targetEntry)
    {
        if (!player || !player->GetSession() || !targetEntry)
            return;

        std::ostringstream msg;
        msg << "뽀글이: 오케이! 이걸로 간다?  " << MakeItemChatLink(targetEntry);

        ChatHandler(player->GetSession()).SendSysMessage(msg.str().c_str());
    }

    // ─────────────────────────────────────────────
    // 아이템 아이콘(displayid -> iconname) 캐시 + 조회
    // - appdata.data_wotlk_itemdisplayinfo(id=displayid, iconname)
    // ─────────────────────────────────────────────
    static std::unordered_map<uint32, std::string> g_displayIconCache;

    static std::string GetIconByDisplayId(uint32 displayId)
    {
        if (!displayId)
            return "INV_Misc_QuestionMark";

        auto it = g_displayIconCache.find(displayId);
        if (it != g_displayIconCache.end())
            return it->second;

        std::ostringstream ss;
        ss << "SELECT IFNULL(iconname,'') "
              "FROM appdata.data_wotlk_itemdisplayinfo "
              "WHERE id=" << displayId << " LIMIT 1";

        QueryResult r = CharacterDatabase.Query(ss.str().c_str());

        std::string icon = "INV_Misc_QuestionMark";
        if (r)
        {
            std::string v = r->Fetch()[0].GetString();
            if (!v.empty())
                icon = v;
        }

        g_displayIconCache.emplace(displayId, icon);
        return icon;
    }

    static std::string GetIconByEntry(uint32 entry)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
        if (!proto)
            return "INV_Misc_QuestionMark";

        return GetIconByDisplayId(proto->DisplayInfoID);
    }

    static std::string IconPrefix(uint32 entry)
    {
        std::string icon = GetIconByEntry(entry);
        std::ostringstream ss;
        ss << "|TInterface\\Icons\\" << icon << ":30:30:-3:0|t";
        return ss.str();
    }

    // ─────────────────────────────────────────────
    // 풀 로딩
    // ─────────────────────────────────────────────
    struct Pools
    {
        std::unordered_set<uint32> sources;
        std::unordered_set<uint32> targets;
    };

    static Pools LoadPools()
    {
        Pools p;

        QueryResult r = CharacterDatabase.Query(
            "SELECT source_entry, target_entry "
            "FROM appdata.item_exchange_pairs "
            "WHERE enabled=1"
        );

        if (!r)
            return p;

        do
        {
            Field* f = r->Fetch();
            uint32 s = f[0].GetUInt32();
            uint32 t = f[1].GetUInt32();

            if (s > 0 && t == 0)
                p.sources.insert(s);
            else if (s == 0 && t > 0)
                p.targets.insert(t);

        } while (r->NextRow());

        return p;
    }

    // ─────────────────────────────────────────────
    // 백팩(기본 16칸)에서 소스 후보 수집
    // ─────────────────────────────────────────────
    struct SourceFound
    {
        Item* item = nullptr;
        uint32 entry = 0;
        uint32 ilvl = 0;
        uint32 q = 0;
        bool bound = false;
        bool used = false; // playedTime(0=신품, 1+=중고)
    };

    // [CHANGED] "진짜 착용 가능" 판정 보강:
    // - CanUseItem()은 요구조건 위주라 사슬/판금 숙련까지 완전히 걸러주지 못할 수 있음
    // - CanEquipItem()로 어떤 장비 슬롯이라도 (교체 포함) 장착 가능한지 추가 검사
    static bool CanEquipSomewhere(Player* player, Item* it)
    {
        if (!player || !it)
            return false;

        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            uint16 dest = 0;
            // swap=true : 이미 슬롯이 차 있어도 "교체해서 착용 가능"이면 통과
            if (player->CanEquipItem(slot, dest, it, true) == EQUIP_ERR_OK)
                return true;
        }
        return false;
    }

	#include <memory> // 상단 include 추가

	// [ADD] 타겟(엔트리)용: 임시 Item 생성해서 장착 가능 검사
	static bool CanEquipTargetEntrySomewhere(Player* player, uint32 entry)
	{
		if (!player || entry == 0)
			return false;

		ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
		if (!proto)
			return false;

		// 요구조건(레벨/직업/종족/스킬 등)
		if (player->CanUseItem(proto) != EQUIP_ERR_OK)
			return false;

		std::unique_ptr<Item> tmp(Item::CreateItem(entry, 1, player));
		if (!tmp)
			return false;

		// ✅ 여기서만 Item* 버전을 호출
		return CanEquipSomewhere(player, tmp.get());
	}

	// Armor SubClass (WotLK)
	static constexpr uint32 ARMOR_CLOTH   = 1;
	static constexpr uint32 ARMOR_LEATHER = 2;
	static constexpr uint32 ARMOR_MAIL    = 3;
	static constexpr uint32 ARMOR_PLATE   = 4;
	static constexpr uint32 ARMOR_SHIELD  = 6; // Shield

	static bool IsArmorSubClassAllowedByClass(Player* player, ItemTemplate const* proto)
	{
		if (!player || !proto)
			return false;

		if (proto->Class != ITEM_CLASS_ARMOR)
			return true; // Armor가 아니면 여기서 판정하지 않음

		uint32 sc = proto->SubClass;
		uint8 cls = player->GetClass();

		// 공통: 망토는 대부분 Class=ARMOR지만 SubClass가 0(기타)로 잡히는 케이스가 있어.
		// 너는 InventoryType으로 이미 "CLOAK"을 따로 취급하고 있으니,
		// 여기서는 SubClass 0은 통과시켜도 무방(망토/목걸이 등 혼동 방지용).
		if (sc == 0)
			return true;

		switch (cls)
		{
			// 천
			case CLASS_PRIEST:
			case CLASS_MAGE:
			case CLASS_WARLOCK:
				return sc == ARMOR_CLOTH;

			// 가죽
			case CLASS_ROGUE:
			case CLASS_DRUID:
				return sc == ARMOR_CLOTH || sc == ARMOR_LEATHER; // 저렙/일부 템 고려해 cloth 포함

			// 사슬
			case CLASS_HUNTER:
			case CLASS_SHAMAN:
				return sc == ARMOR_CLOTH || sc == ARMOR_LEATHER || sc == ARMOR_MAIL;

			// 판금
			case CLASS_WARRIOR:
			case CLASS_PALADIN:
			case CLASS_DEATH_KNIGHT:
				return sc == ARMOR_CLOTH || sc == ARMOR_LEATHER || sc == ARMOR_MAIL || sc == ARMOR_PLATE || sc == ARMOR_SHIELD;

			default:
				break;
		}

		// 알 수 없는 클래스면 너무 막지 말고 통과
		return true;
	}

    // "플레이어가 착용/사용 가능한 아이템" 필터
    static bool IsUsableSourceForPlayer(Player* player, Item* it)
    {
        if (!player || !it)
            return false;

        ItemTemplate const* proto = it->GetTemplate();
        if (!proto)
            return false;

        // 장비(무기/방어구)만 소스로 인정
        if (proto->Class != ITEM_CLASS_ARMOR && proto->Class != ITEM_CLASS_WEAPON)
            return false;

        // 요구 조건(레벨/직업제한/종족/스킬 등)
        if (player->CanUseItem(proto) != EQUIP_ERR_OK)
            return false;

        // [CHANGED] 숙련/장착 가능 여부(사슬/판금 같은 케이스)까지 확인
        if (!CanEquipSomewhere(player, it))
            return false; 

        return true;
    }

    // pools에 등록된 아이템 + 플레이어 착용 가능만 리스트업
    static void CollectSourcesFromBackpack16(Player* player, Pools const& pools, std::vector<SourceFound>& out)
    {
        out.clear();
        if (!player)
            return;

        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        {
            Item* it = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!it)
                continue;

            uint32 e = it->GetEntry();
            if (pools.sources.find(e) == pools.sources.end())
                continue;

            // 내가 착용/사용할 수 있는 소스만
            if (!IsUsableSourceForPlayer(player, it))
                continue;

            ItemTemplate const* proto = it->GetTemplate();
            uint32 ilvl = proto ? proto->ItemLevel : 0;
            uint32 q    = proto ? proto->Quality   : 0;

            // 신품/중고: playedTime 기준(0=신품, >=1=중고)
            bool used = (it->GetPlayedTime() > 600);

            out.push_back({ it, e, ilvl, q, it->IsSoulBound(), used });
        }

        std::sort(out.begin(), out.end(), [](SourceFound const& a, SourceFound const& b)
        {
            if (a.ilvl != b.ilvl) return a.ilvl > b.ilvl;
            if (a.q != b.q)       return a.q > b.q;
            if (a.used != b.used) return a.used < b.used; // 신품 우선(원하면 반대로)
            return a.entry < b.entry;
        });
    }

    static Item* FindBackpackItemByGuid(Player* player, ObjectGuid guid)
    {
        if (!player || !guid)
            return nullptr;

        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        {
            Item* it = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (it && it->GetGUID() == guid)
                return it;
        }
        return nullptr;
    }

    // ─────────────────────────────────────────────
    // 타겟 풀에서 소스 속성과 동일한 아이템만 필터링
    // ─────────────────────────────────────────────
    struct TargetFound
    {
        uint32 entry = 0;
        uint32 ilvl = 0;
        uint32 q = 0;
        std::string slotType;
    };

    // 3가지만 검수: Class / SubClass / InventoryType
    static bool IsMatchTargetBySourceProto(ItemTemplate const* src, ItemTemplate const* tgt)
	{
		if (!src || !tgt)
			return false;

		// 클래스는 반드시 동일
		if (src->Class != tgt->Class)
			return false;

		// 방어구: 슬롯(InventoryType) 맞추기 (SubClass 미검수 = 장비 크로스)
		if (src->Class == ITEM_CLASS_ARMOR)
			return src->InventoryType == tgt->InventoryType;

		// 무기: 크로스 허용 (InventoryType도 안 막음)
		if (src->Class == ITEM_CLASS_WEAPON)
			return true;

		return false;
	}

    // player를 받아서 "진영 전용 타겟 필터"까지 적용
    static void CollectTargetsByMatching(Player* player, Pools const& pools, ItemTemplate const* srcProto, std::vector<TargetFound>& out)
    {
        out.clear();
        if (!srcProto)
            return;

        for (uint32 e : pools.targets)
        {
            ItemTemplate const* tgt = sObjectMgr->GetItemTemplate(e);
            if (!tgt)
                continue;

            if (!IsMatchTargetBySourceProto(srcProto, tgt))
                continue;

			if (tgt->ItemLevel > srcProto->ItemLevel + MAX_ITEM_LEVEL_GAP)
				continue;

            // 진영 전용 아이템이면 플레이어 진영에 맞는 것만 노출 (Flags2 우선)
            if (!IsTargetFactionOkForPlayer(player, tgt))
                continue;

		/*	if (!IsArmorSubClassAllowedByClass(player, tgt))
				continue; */

			if (!CanEquipTargetEntrySomewhere(player, e))
    continue;

            out.push_back({ e, tgt->ItemLevel, tgt->Quality, SlotTypeFor(tgt) });
        }

        std::sort(out.begin(), out.end(), [](TargetFound const& a, TargetFound const& b)
        {
            if (a.ilvl != b.ilvl) return a.ilvl > b.ilvl;
            if (a.q != b.q)       return a.q > b.q;
            return a.entry < b.entry;
        });
    }

	static bool IsUsableEntryForPlayer(Player* player, ItemTemplate const* proto)
	{
		if (!player || !proto)
			return false;

		if (player->CanUseItem(proto) != EQUIP_ERR_OK)
			return false;

		for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
		{
			uint16 dest = 0;
			if (player->CanEquipNewItem(slot, dest, proto->ItemId, true) == EQUIP_ERR_OK)
				return true;
		}
		return false;
	}

	static float SourceUnusablePenalty(ItemTemplate const* proto)
	{
		if (!proto)
			return 1.0f;

		if (proto->Class == ITEM_CLASS_ARMOR)
			return SRC_PENALTY_ARMOR;

		if (proto->Class == ITEM_CLASS_WEAPON)
		{
			switch (proto->InventoryType)
			{
				case INVTYPE_WEAPON:
				case INVTYPE_WEAPONMAINHAND:
				case INVTYPE_WEAPONOFFHAND:
					return SRC_PENALTY_WEAPON_1H;

				case INVTYPE_2HWEAPON:
					return SRC_PENALTY_WEAPON_2H;

				case INVTYPE_RANGED:
				case INVTYPE_RANGEDRIGHT:
					return SRC_PENALTY_WEAPON_RNG;

				default:
					return SRC_PENALTY_WEAPON_ETC;
			}
		}
		return 1.0f;
	}

	static bool IsWeaponKindMismatch(uint32 srcEntry, uint32 tgtEntry)
	{
		ItemTemplate const* src = sObjectMgr->GetItemTemplate(srcEntry);
		ItemTemplate const* tgt = sObjectMgr->GetItemTemplate(tgtEntry);
		if (!src || !tgt)
			return false;

		if (src->Class != ITEM_CLASS_WEAPON || tgt->Class != ITEM_CLASS_WEAPON)
			return false;

		return src->SubClass != tgt->SubClass;
	}

    // ─────────────────────────────────────────────
    // RP: appdata.account_data.reg_point
    // ─────────────────────────────────────────────
    static int64 GetRP(Player* player)
    {
        if (!player || !player->GetSession())
            return 0;

        uint32 accountId = player->GetSession()->GetAccountId();

        std::ostringstream ss;
        ss << "SELECT reg_point FROM appdata.account_data WHERE id=" << accountId << " LIMIT 1";

        QueryResult r = CharacterDatabase.Query(ss.str().c_str());
        if (!r)
            return 0;

        return r->Fetch()[0].GetInt64();
    }

    static bool SpendRP(Player* player, int64 cost)
    {
        if (!player || !player->GetSession() || cost <= 0)
            return false;

        uint32 accountId = player->GetSession()->GetAccountId();

        std::ostringstream ss;
        ss << "UPDATE appdata.account_data "
              "SET reg_point = reg_point - " << cost << " "
              "WHERE id=" << accountId << " AND reg_point >= " << cost;

        CharacterDatabase.Execute(ss.str().c_str());
        return GetRP(player) >= 0;
    }

    static void RefundRP(Player* player, int64 amount)
    {
        if (!player || !player->GetSession() || amount <= 0)
            return;

        uint32 accountId = player->GetSession()->GetAccountId();

        std::ostringstream ss;
        ss << "UPDATE appdata.account_data "
              "SET reg_point = reg_point + " << amount << " "
              "WHERE id=" << accountId;

        CharacterDatabase.Execute(ss.str().c_str());
    }

    // ─────────────────────────────────────────────
    // 비용 계산
    // ─────────────────────────────────────────────
    static float SlotMult(std::string const& slotType)
    {
        if (slotType == "WEAPON")  return 1.50f;
        if (slotType == "TRINKET") return 1.40f;
        if (slotType == "RING")    return 1.30f;
        if (slotType == "NECK")    return 1.30f;
        if (slotType == "CLOAK")   return 1.15f;
        return 1.00f;
    }

    static float QualityMult(uint32 fromQ, uint32 toQ)
    {
        if (fromQ == toQ)
            return 1.0f;

        if (toQ >= 4)
        {
            if (fromQ == 3) return 2.5f;
            if (fromQ == 2) return 3.0f;
            if (fromQ <= 1) return 4.0f;
        }

        return 1.2f;
    }

    static float UsedMult(bool used)
    {
        return used ? USED_MULT_USED : USED_MULT_NEW;
    }

    struct Breakdown
    {
        int64 base = 0;
        float q = 1.0f;
        float slot = 1.0f;
        float bind = 1.0f;
        float used = 1.0f;
        int64 total = 0;
		float src  = 1.0f; // 소스 착용불가 배수
		float kind = 1.0f; // 무기 종류 불일치 배수
    };

    static Breakdown ComputeCost(State const& s)
    {
        Breakdown b;

        int64 diff = int64(s.tgtIlvl) - int64(s.srcIlvl);
        if (diff < 0) diff = 0;

        int64 raw = diff * int64(BASE_PER_ILVL);
        b.base = std::max<int64>(raw, int64(MIN_RP_FLOOR));

        b.q    = QualityMult(s.srcQuality, s.tgtQuality);
        b.slot = SlotMult(s.tgtSlotType);
        b.bind = s.srcBound ? BIND_MULT_SOULBOUND : BIND_MULT_UNBOUND;
        b.used = UsedMult(s.srcUsed);

        // [ADD #7] 소스 착용 불가 페널티 (장비/무기 세분화)
		ItemTemplate const* srcProto = sObjectMgr->GetItemTemplate(s.srcEntry);
		b.src = s.srcUsable ? 1.0f : SourceUnusablePenalty(srcProto);

		// [ADD #7] 무기 종류(SubClass) 불일치 페널티
		b.kind = IsWeaponKindMismatch(s.srcEntry, s.tgtEntry) ? WEAPON_KIND_MISMATCH_MULT : 1.0f;

		double total = double(b.base) * double(b.q) * double(b.slot) * double(b.bind) * double(b.used) * double(b.src) * double(b.kind);
        b.total = (int64)std::llround(total);
        if (b.total < 0) b.total = 0;

        return b;
    }

    // ─────────────────────────────────────────────
    // UI (뽀글이 말투)
    // ─────────────────────────────────────────────
    static void ShowMain(Player* player, Creature* me)
    {
        ClearGossipMenuFor(player);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 교환 시작! (백팩 16칸 뒤지기)", GOSSIP_SENDER_MAIN, ACT_START);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 규칙 좀 읽자 !! (안 보면 손해)",  GOSSIP_SENDER_MAIN, ACT_HELP);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 나갈래? (닫기)",                 GOSSIP_SENDER_MAIN, ACT_CLOSE);

        SendGossipMenuFor(player, 1, me->GetGUID());
    }

    static void ShowHelp(Player* player, Creature* me)
    {
        ClearGossipMenuFor(player);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 소스는 '니가 착용/사용 가능한 것'만 보여준다.", GOSSIP_SENDER_MAIN, ACT_HELP);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: RP = max((목표iLv-재료iLv)*100, 하한) * 등급배수 * 슬롯배수 * 귀속배수 * 착용불가배수 * (무기일경우 무기종류배수) * (중고배수)", GOSSIP_SENDER_MAIN, ACT_HELP);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 오케이, 다시 메인!", GOSSIP_SENDER_MAIN, ACT_MAIN);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 닫기",               GOSSIP_SENDER_MAIN, ACT_CLOSE);

        SendGossipMenuFor(player, 1, me->GetGUID());
    }

    static void ShowSourcePick(Player* player, Creature* me, Pools const& pools)
    {
        auto& s = ST(player);

        ClearGossipMenuFor(player);

        if (pools.sources.empty())
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 소스 풀이 비었네?", GOSSIP_SENDER_MAIN, ACT_MAIN);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 닫기", GOSSIP_SENDER_MAIN, ACT_CLOSE);
            SendGossipMenuFor(player, 1, me->GetGUID());
            return;
        }

        std::vector<SourceFound> found;
        CollectSourcesFromBackpack16(player, pools, found);

        if (found.empty())
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 백팩(16칸)에 교환 재료가 없네.", GOSSIP_SENDER_MAIN, ACT_MAIN);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 닫기", GOSSIP_SENDER_MAIN, ACT_CLOSE);
            SendGossipMenuFor(player, 1, me->GetGUID());
            return;
        }

        uint32 from = s.srcPage * PAGE_SIZE;
        uint32 to   = std::min<uint32>(from + PAGE_SIZE, (uint32)found.size());

        for (uint32 i = from; i < to; ++i)
        {
            std::ostringstream label;
            label << IconPrefix(found[i].entry)
                  << ItemName(found[i].entry)
                  << "(" << found[i].ilvl
                  << " / " << QualityName(found[i].q)
                  << " / " << (found[i].bound ? "귀속" : "미귀속")
                  << " / " << UsedLabel(found[i].used)
                  << ")";

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, label.str(), GOSSIP_SENDER_MAIN, ACT_PICK_SOURCE_BASE + (i - from));
        }

        if (to < found.size())
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 다음 페이지 >", GOSSIP_SENDER_MAIN, ACT_SRC_NEXT);
        if (s.srcPage > 0)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: < 이전 페이지", GOSSIP_SENDER_MAIN, ACT_SRC_PREV);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 메인으로", GOSSIP_SENDER_MAIN, ACT_MAIN);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 닫기",     GOSSIP_SENDER_MAIN, ACT_CLOSE);

        SendGossipMenuFor(player, 1, me->GetGUID());
    }

    static void ShowTargetPick(Player* player, Creature* me, Pools const& pools)
    {
        auto& s = ST(player);

        ClearGossipMenuFor(player);

        if (!s.srcGuid || s.srcEntry == 0)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 재료부터 골라. 순서 지켜!!", GOSSIP_SENDER_MAIN, ACT_RESET_TO_SRC);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 닫기", GOSSIP_SENDER_MAIN, ACT_CLOSE);
            SendGossipMenuFor(player, 1, me->GetGUID());
            return;
        }

        ItemTemplate const* srcProto = sObjectMgr->GetItemTemplate(s.srcEntry);
        if (!srcProto)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 소스 아이템 정보를 못 찾겠는데? 다시 골라!", GOSSIP_SENDER_MAIN, ACT_RESET_TO_SRC);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 닫기", GOSSIP_SENDER_MAIN, ACT_CLOSE);
            SendGossipMenuFor(player, 1, me->GetGUID());
            return;
        }

        std::vector<TargetFound> tgts;
        CollectTargetsByMatching(player, pools, srcProto, tgts);

        if (tgts.empty())
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 맞는 타겟이 0개다… ", GOSSIP_SENDER_MAIN, ACT_RESET_TO_SRC);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 닫기", GOSSIP_SENDER_MAIN, ACT_CLOSE);
            SendGossipMenuFor(player, 1, me->GetGUID());
            return;
        }

        uint32 from = s.tgtPage * PAGE_SIZE;
        uint32 to   = std::min<uint32>(from + PAGE_SIZE, (uint32)tgts.size());

        for (uint32 i = from; i < to; ++i)
        {
            std::ostringstream label;
            label << IconPrefix(tgts[i].entry)
                  << ItemName(tgts[i].entry)
                  << "(" << tgts[i].ilvl
                  << " / " << QualityName(tgts[i].q)
                  << " / " << SlotTypeLabel(tgts[i].slotType) << ")";

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, label.str(), GOSSIP_SENDER_MAIN, ACT_PICK_TARGET_BASE + (i - from));
        }

        if (to < tgts.size())
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 다음 페이지 >", GOSSIP_SENDER_MAIN, ACT_TGT_NEXT);
        if (s.tgtPage > 0)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: < 이전 페이지", GOSSIP_SENDER_MAIN, ACT_TGT_PREV);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 재료 다시 고르기", GOSSIP_SENDER_MAIN, ACT_RESET_TO_SRC);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 메인으로",         GOSSIP_SENDER_MAIN, ACT_MAIN);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 닫기",             GOSSIP_SENDER_MAIN, ACT_CLOSE);

        SendGossipMenuFor(player, 1, me->GetGUID());
    }

    static void ShowPreview(Player* player, Creature* me)
    {
        auto& s = ST(player);

        ClearGossipMenuFor(player);

        if (!s.srcGuid || !s.srcEntry || !s.tgtEntry)
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 아직 덜 골랐어! 소스/타겟 다시 골라!", GOSSIP_SENDER_MAIN, ACT_RESET_TO_SRC);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 닫기", GOSSIP_SENDER_MAIN, ACT_CLOSE);
            SendGossipMenuFor(player, 1, me->GetGUID());
            return;
        }

        Breakdown bd = ComputeCost(s);
        int64 have = GetRP(player);

        {
            std::ostringstream ss;
            ss << IconPrefix(s.srcEntry)
               << "S: " << ItemName(s.srcEntry)
               << " (" << s.srcIlvl << " / " << QualityName(s.srcQuality)
				<< " / 착용불가 x" << bd.src
				<< " / 무기종류 x" << bd.kind
               << " / " << (s.srcBound ? "귀속" : "미귀속")
               << " / " << UsedLabel(s.srcUsed)
               << ")";
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, ss.str(), GOSSIP_SENDER_MAIN, ACT_PREVIEW);
        }
        {
            std::ostringstream ss;
            ss << IconPrefix(s.tgtEntry)
               << "T: " << ItemName(s.tgtEntry)
               << " (" << s.tgtIlvl << " / " << QualityName(s.tgtQuality) << " / " << SlotTypeLabel(s.tgtSlotType) << ")";
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, ss.str(), GOSSIP_SENDER_MAIN, ACT_PREVIEW);
        }
        {
            std::ostringstream ss;
            ss << "뽀글이 계산서) 기본RP " << bd.base
               << " | 등급 x" << bd.q
               << " | 슬롯 x" << bd.slot
               << " | 귀속 x" << bd.bind
			   << " | 재료 x" << bd.src
			   << " | 무기종류 x" << bd.kind
               << " | " << (s.srcUsed ? "중고" : "신품") << " x" << bd.used;
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, ss.str(), GOSSIP_SENDER_MAIN, ACT_PREVIEW);
        }
        {
            std::ostringstream ss;
            ss << "필요RP: " << bd.total << " / 보유RP: " << have;
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, ss.str(), GOSSIP_SENDER_MAIN, ACT_PREVIEW);
        }

        if (have >= bd.total && bd.total > 0)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 교환 실행! (진짜 간다?)", GOSSIP_SENDER_MAIN, ACT_EXECUTE);
        else
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: RP 부족이다… 더 벌어와 !!!!", GOSSIP_SENDER_MAIN, ACT_PREVIEW);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 타겟 다시 고르기", GOSSIP_SENDER_MAIN, ACT_RESET_TO_TGT);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 재료 다시 고르기", GOSSIP_SENDER_MAIN, ACT_RESET_TO_SRC);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 닫기",             GOSSIP_SENDER_MAIN, ACT_CLOSE);

        SendGossipMenuFor(player, 1, me->GetGUID());
    }

    static void ExecuteExchange(Player* player, Creature* me)
    {
        auto& s = ST(player);

        Item* src = FindBackpackItemByGuid(player, s.srcGuid);
        if (!src || src->GetEntry() != s.srcEntry)
        {
            ClearGossipMenuFor(player);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 재료가 없어졌는데? 백팩(16칸)에서 다시 골라!", GOSSIP_SENDER_MAIN, ACT_RESET_TO_SRC);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 닫기", GOSSIP_SENDER_MAIN, ACT_CLOSE);
            SendGossipMenuFor(player, 1, me->GetGUID());
            return;
        }

        // 실행 직전, 혹시 값이 바뀌었으면 다시 반영(판별만)
        s.srcUsed = (src->GetPlayedTime() > 600);

        Breakdown bd = ComputeCost(s);
        if (bd.total <= 0)
        {
            ShowPreview(player, me);
            return;
        }

        ItemPosCountVec dest;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, s.tgtEntry, 1) != EQUIP_ERR_OK)
        {
            ClearGossipMenuFor(player);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 인벤 칸부터 만들고 와. 빈칸 없으면 교환 못 해!", GOSSIP_SENDER_MAIN, ACT_CLOSE);
            SendGossipMenuFor(player, 1, me->GetGUID());
            return;
        }

        int64 before = GetRP(player);
        if (before < bd.total)
        {
            ShowPreview(player, me);
            return;
        }

        if (!SpendRP(player, bd.total))
        {
            ShowPreview(player, me);
            return;
        }

        player->DestroyItem(src->GetBagSlot(), src->GetSlot(), true);

        Item* newItem = player->StoreNewItem(dest, s.tgtEntry, true);
        if (newItem)
        {
            newItem->SetBinding(true);
            player->SendNewItem(newItem, 1, true, false);
        }
        else
        {
            RefundRP(player, bd.total);
        }

        ClearGossipMenuFor(player);
        {
            std::ostringstream ss;
            ss << "뽀글이: 끝~! RP " << bd.total << " 먹었다 😎";
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, ss.str(), GOSSIP_SENDER_MAIN, ACT_MAIN);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 한 판 더? (다시 교환)", GOSSIP_SENDER_MAIN, ACT_START);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "뽀글이: 닫기",                  GOSSIP_SENDER_MAIN, ACT_CLOSE);
        SendGossipMenuFor(player, 1, me->GetGUID());

        ResetToSource(player);
    }
} // namespace

// ─────────────────────────────────────────────
// Script
// ─────────────────────────────────────────────
class npc_item_exchange : public CreatureScript
{
public:
    npc_item_exchange() : CreatureScript("npc_item_exchange") { }

    struct npc_item_exchangeAI : public ScriptedAI
    {
        npc_item_exchangeAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            if (!player)
                return false;

            PpoggeuliHello(player);

            ResetAll(player);
            ShowMain(player, me);
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            if (!player)
                return false;

            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            auto& s = ST(player);
            Pools pools = LoadPools();

            switch (action)
            {
                case ACT_CLOSE:
                    CloseGossipMenuFor(player);
                    return true;

                case ACT_MAIN:
                    ResetAll(player);
                    ShowMain(player, me);
                    return true;

                case ACT_HELP:
                    ShowHelp(player, me);
                    return true;

                case ACT_START:
                    ResetToSource(player);
                    ShowSourcePick(player, me, pools);
                    return true;

                case ACT_SRC_NEXT:
                    s.srcPage++;
                    ShowSourcePick(player, me, pools);
                    return true;

                case ACT_SRC_PREV:
                    if (s.srcPage > 0) s.srcPage--;
                    ShowSourcePick(player, me, pools);
                    return true;

                case ACT_TGT_NEXT:
                    s.tgtPage++;
                    ShowTargetPick(player, me, pools);
                    return true;

                case ACT_TGT_PREV:
                    if (s.tgtPage > 0) s.tgtPage--;
                    ShowTargetPick(player, me, pools);
                    return true;

                case ACT_RESET_TO_SRC:
                    ResetToSource(player);
                    ShowSourcePick(player, me, pools);
                    return true;

                case ACT_RESET_TO_TGT:
                    ResetPickTargetOnly(player);
                    ShowTargetPick(player, me, pools);
                    return true;

                case ACT_PREVIEW:
                    ShowPreview(player, me);
                    return true;

                case ACT_EXECUTE:
                    ExecuteExchange(player, me);
                    return true;

                default:
                    break;
            }

            if (action >= ACT_PICK_SOURCE_BASE && action < ACT_PICK_SOURCE_BASE + 200)
            {
                uint32 pick = action - ACT_PICK_SOURCE_BASE;

                std::vector<SourceFound> found;
                CollectSourcesFromBackpack16(player, pools, found);

                uint32 from = s.srcPage * PAGE_SIZE;
                uint32 real = from + pick;

                if (real >= found.size())
                {
                    ShowSourcePick(player, me, pools);
                    return true;
                }

                s.srcEntry   = found[real].entry;
                s.srcGuid    = found[real].item->GetGUID();
                s.srcIlvl    = found[real].ilvl;
                s.srcQuality = found[real].q;
                s.srcBound   = found[real].bound;
                s.srcUsed    = found[real].used;

				// [ADD #6] 소스가 내 직업 기준으로 착용 가능한지 저장
				ItemTemplate const* srcProto = sObjectMgr->GetItemTemplate(s.srcEntry);
				s.srcUsable = IsUsableEntryForPlayer(player, srcProto);

                s.tgtPage = 0;
                ResetPickTargetOnly(player);

                ShowTargetPick(player, me, pools);
                return true;
            }

            if (action >= ACT_PICK_TARGET_BASE && action < ACT_PICK_TARGET_BASE + 200)
            {
                uint32 pick = action - ACT_PICK_TARGET_BASE;

                ItemTemplate const* srcProto = sObjectMgr->GetItemTemplate(s.srcEntry);
                std::vector<TargetFound> tgts;
                CollectTargetsByMatching(player, pools, srcProto, tgts);

                uint32 from = s.tgtPage * PAGE_SIZE;
                uint32 real = from + pick;

                if (real >= tgts.size())
                {
                    ShowTargetPick(player, me, pools);
                    return true;
                }

                s.tgtEntry    = tgts[real].entry;
                s.tgtIlvl     = tgts[real].ilvl;
                s.tgtQuality  = tgts[real].q;
                s.tgtSlotType = tgts[real].slotType;

                SendTargetLinkToChat(player, s.tgtEntry);

                ShowPreview(player, me);
                return true;
            }

            CloseGossipMenuFor(player);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_item_exchangeAI(creature);
    }
};

void AddSC_npc_item_exchange()
{
    new npc_item_exchange();
}
