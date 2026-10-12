#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Player.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Chat.h"
#include "Group.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Mail.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Pet.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "MovementGenerator.h"
#include "DBCStores.h"
#include "AchievementMgr.h"
#include "World.h"
#include "Log.h"
#include "Random.h"
#include "StringFormat.h"
#include "ScriptMgr.h"
#include "Player.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "DBCStores.h"
#include "ReputationMgr.h"

#include <unordered_map>
#include <unordered_set>
#include <array>
#include <string>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <limits>

#include "wrathful_set_mgr.h"
#include "DiscordEvent.h"
#include "GameObject.h"

#include "Config.h"
/* ============================================================
 * RP 유틸 (중복 제거 / 단일 정의)
 * ============================================================ */
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

    static inline bool TrySpendOne(uint32 accountId, uint32 amount, long long& outAfter)
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

        outAfter = Get(accountId);
        return true;
    }
}

/* ============================================================
 * 공통 상수
 * ============================================================ */
constexpr uint32 kMapICC = 631;
constexpr uint32 kMapRS  = 724;

constexpr uint32 kFailMapId = 571;
constexpr float  kFailX = 5788.359f;
constexpr float  kFailY = 2070.689f;
constexpr float  kFailZ = 636.064f;
constexpr float  kFailO = 3.57269f;

constexpr float  kFailRSX = 3561.363f;
constexpr float  kFailRSY = 275.11f;
constexpr float  kFailRSZ = -115.55f;
constexpr float  kFailRSO = 3.172924f;

constexpr uint32 kRequireGearScore = 5000;
constexpr uint32 kAchCallOfTheCrusade10 = 3917;
constexpr uint32 kAchCallOfTheCrusade25 = 3916;

/* ============================================================
 * GearScore 계산
 * ============================================================ */
namespace GearScore
{
    static constexpr float A = 91.45f;
    static constexpr float B = 0.65f;
    static constexpr float SCALE = 1.8618f;

    static inline bool IsIgnoredSlot(uint8 slot)
    {
        return slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD;
    }

    static inline float SlotMod(uint8 slot, ItemTemplate const* proto)
    {
        if (!proto) return 0.f;

        switch (slot)
        {
            case EQUIPMENT_SLOT_HEAD:
            case EQUIPMENT_SLOT_CHEST:
            case EQUIPMENT_SLOT_LEGS:
                return 1.0f;

            case EQUIPMENT_SLOT_SHOULDERS:
            case EQUIPMENT_SLOT_HANDS:
            case EQUIPMENT_SLOT_WAIST:
            case EQUIPMENT_SLOT_FEET:
                return 0.75f;

            case EQUIPMENT_SLOT_WRISTS:
            case EQUIPMENT_SLOT_NECK:
            case EQUIPMENT_SLOT_BACK:
            case EQUIPMENT_SLOT_FINGER1:
            case EQUIPMENT_SLOT_FINGER2:
            case EQUIPMENT_SLOT_TRINKET1:
            case EQUIPMENT_SLOT_TRINKET2:
                return 0.5625f;

            case EQUIPMENT_SLOT_MAINHAND:
            case EQUIPMENT_SLOT_OFFHAND:
                return proto->InventoryType == INVTYPE_2HWEAPON ? 2.0f : 1.0f;

            case EQUIPMENT_SLOT_RANGED:
                return 0.3164f;
        }
        return 0.f;
    }

    static inline uint32 ItemGS(uint8 slot, Item const* item)
    {
        if (!item) return 0;
        auto proto = item->GetTemplate();
        if (!proto || IsIgnoredSlot(slot)) return 0;

        float ilvl = float(proto->ItemLevel);
        if (ilvl <= 0.f) return 0;

        float gs = ((ilvl - A) / B) * SlotMod(slot, proto) * SCALE;
        return gs > 0.f ? uint32(gs) : 0;
    }

    static inline uint32 Total(Player const* player)
    {
        uint32 total = 0;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (IsIgnoredSlot(slot)) continue;
            if (Item const* it = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                total += ItemGS(slot, it);
        }
        return total;
    }
}

/* ============================================================
 * 입장 제한 처리
 * ============================================================ */
static constexpr uint32 FACTION_ASHEN_VERDICT_1 = 1156;

static bool HasAshenVerdictNeutralOrHigher(Player* player)
{
    if (!player)
        return false;

    FactionEntry const* factionEntry = sFactionStore.LookupEntry(FACTION_ASHEN_VERDICT_1);
    if (!factionEntry)
        return false;

    FactionState const* state = player->GetReputationMgr().GetState(factionEntry);
    if (!state)
        return false;

    if (!state->Flags.HasFlag(ReputationFlags::Visible))
        return false;

    return player->GetReputationMgr().GetRank(factionEntry) >= REP_NEUTRAL;
}

static bool HasCallOfTheCrusade(Player const* p)
{
    return p && (p->HasAchieved(kAchCallOfTheCrusade10) ||
                 p->HasAchieved(kAchCallOfTheCrusade25));
}

static void KickToFailSpot(Player* p, uint32 fromMap, std::string const& msg)
{
    if (!p) return;
    ChatHandler(p->GetSession()).SendSysMessage(msg.c_str());

    if (fromMap == kMapRS)
        p->TeleportTo(kFailMapId, kFailRSX, kFailRSY, kFailRSZ, kFailRSO);
    else
        p->TeleportTo(kFailMapId, kFailX, kFailY, kFailZ, kFailO);
}

static constexpr uint32 ALLIANCE_GUILD_ID = 4;
static constexpr uint32 HORDE_GUILD_ID    = 2;
static constexpr uint32 AUTO_GUILD_INVITE_DELAY_MS = 10 * IN_MILLISECONDS;

static uint32 GetTargetGuildId(Player* player)
{
    return (player->GetTeamId() == TEAM_ALLIANCE) ? ALLIANCE_GUILD_ID : HORDE_GUILD_ID;
}


//길드 초대
static void SendGuildInvitePopup(Player* player, uint32 guildId, char const* notice)
    {
        if (!player || !player->GetSession())
            return;

        Guild* guild = sGuildMgr->GetGuildById(guildId);
        if (!guild)
            return;

        // “초대 받은 길드” 세션 상태 세팅 (길드 수락/거절 UI에서 사용)
        player->SetGuildIdInvited(guildId);

        // 길드명은 실제 DB/서버 값으로
        std::string const& gname = guild->GetName();

        // 패킷 포맷은 코어에 따라 다를 수 있음.
        // (네가 예전에 쓰던 형태가 네 코어에서 동작했던 전제가 있으니 그 형태 유지)
        WorldPacket data(SMSG_GUILD_INVITE, 8 + gname.size() + 10);
        data << player->GetName();
        data << gname;

        player->GetSession()->SendPacket(&data);

  /*      if (notice && *notice)
		{
			std::string msg = Trinity::StringFormat("첫 접속 자동길드초대입니다. 다음접속부터 자동초대 되지 않습니다.");
			ChatHandler(player->GetSession()).PSendSysMessage("%s", msg.c_str());
		} */
    }

class AutoGuildInviteEvent : public BasicEvent
{
public:
    explicit AutoGuildInviteEvent(ObjectGuid playerGuid)
        : _playerGuid(playerGuid)
    {
    }

    bool Execute(uint64 /*time*/, uint32 /*diff*/) override
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(_playerGuid);
        if (!player)
            return true;

        if (player->IsGameMaster())
            return true;

        if (player->GetGuildId() != 0)
            return true;

        uint32 gid = GetTargetGuildId(player);
        if (!gid)
            return true;

        SendGuildInvitePopup(
            player,
            gid,
            " 자동길드초대입니다."
        );

        return true;
    }

private:
    ObjectGuid _playerGuid;
};

static void ScheduleAutoGuildInvite(Player* player)
{
    if (!player || !player->GetSession())
        return;

    if (player->IsGameMaster())
        return;

    if (player->GetGuildId() != 0)
        return;

    player->m_Events.AddEvent(
        new AutoGuildInviteEvent(player->GetGUID()),
        player->m_Events.CalculateTime(Milliseconds(AUTO_GUILD_INVITE_DELAY_MS))
    );
}

/* =========================
 * 계정별 XP 모드 캐시
 * ========================= */
static std::unordered_set<uint32> s_OriginalXpAccounts;

/* ============================================================
 * PlayerScript
 * ============================================================ */
class boost_player : public PlayerScript
{
public:
    boost_player() : PlayerScript("boost_player") {}

	void OnLogin(Player* player, bool firstLogin) override
	{
		if (!player)
			return;

		uint32 charGuid = player->GetGUID().GetCounter();

		/* ============================================
		 * 1) 첫 접속 자동 길드 초대 (기존 로직 유지)
		 * ============================================ */
		if (firstLogin)
		{
			// ───── 디스코드 알림 ─────
			std::string charName = player->GetName();
			uint32 accountId = player->GetSession()->GetAccountId();

			std::string accountName = std::to_string(accountId);

			if (QueryResult r = LoginDatabase.PQuery(
					"SELECT username FROM account WHERE id={}",
					accountId))
			{
				Field* f = r->Fetch();
				accountName = f[0].GetString();
			}

			SendServerDiscordEvent(
				"🎉 신규 캐릭터 생성",
				"캐릭터: " + charName + "\n계정: " + accountName,
				"https://stimpack.pe.kr",
				16753920,
				"home"
			);

			/* ============================================
			 * 신규 캐릭터 첫 접속 Activity Log 기록
			 * ============================================ */

			std::string msg = "🆕 신규 캐릭터 생성 : " + charName;

			/* SQL escape */
			CharacterDatabase.EscapeString(msg);

			std::string q =
				"INSERT INTO appdata.activity_log "
				"(account_id, character_guid, type, message) VALUES ("
				+ std::to_string(accountId) + ", "
				+ std::to_string(charGuid) + ", "
				"'character_create', '"
				+ msg +
				"');";

			CharacterDatabase.DirectExecute(q.c_str());

/*			if (player->GetGuildId() == 0)
			{
				uint32 gid = GetTargetGuildId(player);
				SendGuildInvitePopup(
					player,
					gid,
					"첫 접속 자동길드초대입니다. 다음접속부터 자동초대 되지 않습니다."
				);
			}*/
		}

		ScheduleAutoGuildInvite(player);

		/* ============================================
		 * 2) TaxiCheater DB 플래그 적용 (기존 로직 유지)
		 * ============================================ */
		{
			QueryResult r = CharacterDatabase.PQuery(
				"SELECT taxi_cheat "
				"FROM appdata.character_flags "
				"WHERE guid = {}",
				charGuid
			);

			if (r && r->Fetch()[0].GetUInt8() == 1)
			{
				player->SetTaxiCheater(true);

				ChatHandler(player->GetSession())
					.SendSysMessage("택시 무제한 이동이 활성화되었습니다.");
			}
		}

		/* ============================================
		 * 3) Wrathful 아이템 회수 로직
		 *  - 전장이 아니면 무조건 회수
		 *  - 서버 재시작 후에도 DB 기준으로 동작
		 * ============================================ */
		// 전장이 아니면 전장 아이템은 존재하면 안 됨
        if (!player->InBattleground())
            CleanupWrathfulItemsFromDB(player);

		//개인경험치
		uint32 accountId = player->GetSession()->GetAccountId();
		QueryResult result = CharacterDatabase.PQuery(
			"SELECT xp_mode FROM appdata.account_data WHERE id={}",
			accountId
		);

		if (result && result->Fetch()[0].GetUInt32() == 1)
			s_OriginalXpAccounts.insert(accountId);
		else
			s_OriginalXpAccounts.erase(accountId);
	}

	/* XP 지급 훅 */
    void OnGiveXP(Player* player, uint32& amount, Unit* victim) override
    {
        uint32 accountId = player->GetSession()->GetAccountId();

		// 1배 계정이 아니면 그대로
		if (s_OriginalXpAccounts.find(accountId) == s_OriginalXpAccounts.end())
			return;

		// 서버가 이미 5배 → 1배로 환원
		amount /= 4;
    }

    void OnMapChanged(Player* player) override
    {
		CleanupWrathfulItemsFromDB(player);

		// 🔥 먼저 기존 기록 제거
		CharacterDatabase.PExecute("DELETE FROM character_dungeon_session WHERE character_guid={}",	player->GetGUID().GetCounter());

		if (!player->InBattleground())
		{
			CleanupWrathfulItemsFromDB(player);
		}

        if (!player || player->IsGameMaster())
            return;

		Map* map = player->GetMap();
		if (!map || !map->IsDungeon())
			return;

		uint32 instanceId = map->GetInstanceId();
        uint32 mapId = player->GetMapId();
		uint32 startTime = GameTime::GetGameTime();

		// 🔥 현재 세션 존재 여부
		QueryResult r = CharacterDatabase.PQuery("SELECT start_time FROM character_dungeon_session WHERE instance_id={}  LIMIT 1",instanceId);
		if (!r)
		{
			// 🔥 run 생성 (추가된 부분)
			CharacterDatabase.PExecute("INSERT INTO character_dungeon_run (instance_id, map_id, boss_killed, start_time, finished) VALUES ({}, {}, 0, {}, 0) ON DUPLICATE KEY UPDATE map_id=VALUES(map_id), start_time=VALUES(start_time), boss_killed=0, finished=0",
				instanceId, mapId, startTime);

			// 🔥 session 생성
			CharacterDatabase.PExecute("INSERT INTO character_dungeon_session (instance_id, map_id, start_time, character_guid) VALUES ({}, {}, {}, {})",	instanceId, mapId, startTime, player->GetGUID().GetCounter());
		}
		else
		{
			startTime = r->Fetch()[0].GetUInt32();

			// 🔥 세션 없으면 추가 (중간 합류 대응)
			CharacterDatabase.PExecute("INSERT IGNORE INTO character_dungeon_session (instance_id, map_id, start_time, character_guid) VALUES ({}, {}, {}, {})",instanceId,mapId,startTime,player->GetGUID().GetCounter());
		}

        if (mapId != kMapICC && mapId != kMapRS)
            return;

/*		if (!HasAshenVerdictNeutralOrHigher(player))
		{
			if (!HasCallOfTheCrusade(player))
			{
				KickToFailSpot(player, mapId,
					"[입장 제한] 십자군의 부름 업적이 필요합니다.");
				return;
			}

			uint32 gs = GearScore::Total(player);
			if (gs < kRequireGearScore)
			{
				KickToFailSpot(player, mapId,
					"[입장 제한] GearScore 5000 이상 필요 (현재: "
					+ std::to_string(gs) + ")");
			}
		} */

		uint32 gs = GearScore::Total(player);
		if (!HasCallOfTheCrusade(player) || gs < kRequireGearScore)
		{
			uint32 phaseMask = player->GetPhaseMask();

			phaseMask &= ~1; // 기본 1페이즈 제거
			phaseMask |= 16384;  // 평판작업모드 페이즈 추가

			player->SetPhaseMask(phaseMask, true);

			ChatHandler(player->GetSession()).SendSysMessage(
				Trinity::StringFormat(
					"|cffff0000[평판작업모드진입]|r 필요 조건: 십자군의 부름 업적 보유 및 GearScore {} 이상 (현재: {})",
					kRequireGearScore,
					gs
				).c_str()
			);

			return;
		}

		CleanupWrathfulItemsFromDB(player);
    }

	void OnCreatureKill(Player* killer, Creature* killed) override
	{
		if (!killer || !killed)
			return;

		Map* map = killer->GetMap();
		if (!map || !map->IsDungeon())
			return;

		uint32 instanceId = map->GetInstanceId();
		uint32 mapId = map->GetId();
		uint32 entry = killed->GetEntry();

		// 🔥 보스 여부 확인
		QueryResult r = CharacterDatabase.PQuery("SELECT 1 FROM character_dungeon_boss WHERE map_id={} AND boss_entry={}",mapId, entry);

		if (!r)
			return;

		// 🔥 현재 값 먼저 읽기
		QueryResult r0 = CharacterDatabase.PQuery("SELECT boss_killed FROM character_dungeon_run WHERE instance_id={}",	instanceId);

		if (!r0)
			return;

		uint32 current = r0->Fetch()[0].GetUInt32();
		uint32 next = current + 1;

		// 🔥 증가
		CharacterDatabase.PExecute("UPDATE character_dungeon_run SET boss_killed = {} WHERE instance_id={} AND finished=0",	next, instanceId);

		// 🔥 총 보스 수
		QueryResult r2 = CharacterDatabase.PQuery("SELECT COUNT(*) FROM character_dungeon_boss WHERE map_id={}",mapId);

		if (!r2)
			return;

		uint32 total = r2->Fetch()[0].GetUInt32();

		// 🔥 완료 체크 (DB 재조회 안함)
		if (next >= total)
		{
			CharacterDatabase.PExecute("UPDATE character_dungeon_run SET finished=1 WHERE instance_id={} AND finished=0",instanceId);
			killer->SummonGameObject(1900003,	killed->GetPositionX(),killed->GetPositionY(),killed->GetPositionZ(),killed->GetOrientation(),	QuaternionData(),Seconds(600));
		}
	}

	void OnLootItem(Player* looter, ObjectGuid const& lootGuid, uint32 itemId, uint32 count) override
	{
		if (!looter)
			return;

		ItemTemplate const* item = sObjectMgr->GetItemTemplate(itemId);
		if (!item)
			return;

		/* 기록할 가치 있는 클래스만 통과 */
		switch (item->Class)
		{
			case ITEM_CLASS_WEAPON:
			case ITEM_CLASS_ARMOR:
			case ITEM_CLASS_GEM:
			case ITEM_CLASS_TRADE_GOODS:
			case ITEM_CLASS_RECIPE:
				break;
			default:
				return;
		}

		/* 희귀 이상만 기록 */
		if (item->Quality < ITEM_QUALITY_EPIC)
			return;

		uint32 accountId = looter->GetSession()->GetAccountId();
		uint32 charGuid  = looter->GetGUID().GetCounter();

		std::string itemName = item->Name1;

		/* 아이템 퀄리티 이모지 */
		std::string emoji = "💎";

		if (item->Quality == ITEM_QUALITY_RARE)
			emoji = "🔷";
		else if (item->Quality == ITEM_QUALITY_EPIC)
			emoji = "🟣";
		else if (item->Quality == ITEM_QUALITY_LEGENDARY)
			emoji = "🟠";

		/* 몬스터 이름 */
		std::string creatureName = "Unknown";

		if (Creature* creature = ObjectAccessor::GetCreature(*looter, lootGuid))
			creatureName = creature->GetName();

		/* 맵 정보 */
		Map* map = looter->GetMap();
		MapEntry const* mapEntry = sMapStore.LookupEntry(map->GetId());

		std::string mapName = mapEntry ? mapEntry->MapName[0] : "Unknown";

		uint8 diff = map->GetDifficulty();
		std::string difficulty;

		if (map->IsRaid())
		{
			switch (diff)
			{
				case RAID_DIFFICULTY_10MAN_NORMAL: difficulty = "10N"; break;
				case RAID_DIFFICULTY_25MAN_NORMAL: difficulty = "25N"; break;
				case RAID_DIFFICULTY_10MAN_HEROIC: difficulty = "10H"; break;
				case RAID_DIFFICULTY_25MAN_HEROIC: difficulty = "25H"; break;
				default: difficulty = "Raid";
			}
		}
		else if (map->IsDungeon())
		{
			switch (diff)
			{
				case DUNGEON_DIFFICULTY_NORMAL: difficulty = "Normal"; break;
				case DUNGEON_DIFFICULTY_HEROIC: difficulty = "Heroic"; break;
				case DUNGEON_DIFFICULTY_EPIC: difficulty = "Epic"; break;
				default: difficulty = "Dungeon";
			}
		}
		else
		{
			difficulty = "World";
		}

		/* 메시지 생성 */
		std::string msg =
			emoji + " " + itemName +
			" 획득 (" + creatureName +
			" / " + mapName +
			" " + difficulty + ")";

		CharacterDatabase.EscapeString(msg);

		std::string q =
			"INSERT INTO appdata.activity_log "
			"(account_id, character_guid, type, message, item_entry) VALUES ("
			+ std::to_string(accountId) + ", "
			+ std::to_string(charGuid) + ", "
			"'loot', '"
			+ msg + "', "
			+ std::to_string(itemId) +
			");";

		CharacterDatabase.DirectExecute(q.c_str());
	}

	bool OnPlayerSellItem(Player* player, Creature* vendor, Item* item, uint32& count, uint32& money) override
	{
		if (!player || !item)
			return false;

		ItemTemplate const* proto = item->GetTemplate();
		if (!proto)
			return false;

		/* 루팅과 동일한 클래스 필터 */
		switch (proto->Class)
		{
			case ITEM_CLASS_WEAPON:
			case ITEM_CLASS_ARMOR:
			case ITEM_CLASS_GEM:
			case ITEM_CLASS_TRADE_GOODS:
			case ITEM_CLASS_RECIPE:
				break;
			default:
				return false;
		}

		/* 희귀 이상 */
		if (proto->Quality < ITEM_QUALITY_RARE)
			return false;

		uint32 accountId = player->GetSession()->GetAccountId();
		uint32 charGuid  = player->GetGUID().GetCounter();
		uint32 itemId    = proto->ItemId;

		std::string itemName = proto->Name1;

		std::string emoji = "💎";

		if (proto->Quality == ITEM_QUALITY_RARE)
			emoji = "🔷";
		else if (proto->Quality == ITEM_QUALITY_EPIC)
			emoji = "🟣";
		else if (proto->Quality == ITEM_QUALITY_LEGENDARY)
			emoji = "🟠";

		std::string msg =
			emoji + " " + itemName + " 판매";

		CharacterDatabase.EscapeString(msg);

		std::string q =
			"INSERT INTO appdata.activity_log "
			"(account_id, character_guid, type, message, item_entry) VALUES ("
			+ std::to_string(accountId) + ", "
			+ std::to_string(charGuid) + ", "
			"'sell', '"
			+ msg + "', "
			+ std::to_string(itemId) +
			");";

		CharacterDatabase.DirectExecute(q.c_str());

		return false;
	}
};

/* ============================================================
 * 아이템: 포터블 은행/우체통 (RP 1 소모)
 * ============================================================ */
//커스첨 은행 우체통
namespace {
    constexpr uint32 ENTRY_OPEN_BANK = 34252;
    constexpr uint32 ENTRY_OPEN_MAIL = 35351;
}

class item_portable_bankmail_onuse : public ItemScript {
public:
    item_portable_bankmail_onuse() : ItemScript("item_portable_bankmail_onuse") {}

    bool OnUse(Player* player, Item* item, SpellCastTargets const&) override
    {
        if (!player || !item) return true; // 기본 캐스트 취소

        if (player->IsInCombat()) {
            player->GetSession()->SendNotification("전투 중에는 사용할 수 없습니다.");
            return true;
        }

        // ★ RP 1 소모 체크
        uint32 accId = player->GetSession()->GetAccountId();
        long long remain = 0;
        if (!RP::TrySpendOne(accId, 1, remain)) {
            std::string msg = Trinity::StringFormat("RP 부족: 현재 {} RP (필요: 1)", remain);
            ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
            return true; // 기본 캐스트 취소
        }

        {
			long long after = RP::Get(accId) - 1;
            std::string msg = Trinity::StringFormat("RP 1 소모됨. 잔여 RP: {}", after);
            ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
        }

       // 조건 충족 → 기능 수행
        if (item->GetEntry() == ENTRY_OPEN_BANK) {
            player->GetSession()->SendShowBank(player->GetGUID());
        } else if (item->GetEntry() == ENTRY_OPEN_MAIL) {
            player->GetSession()->SendShowMailBox(player->GetGUID());
        }

        return true; // 기본 주문 캐스트 취소 (우리가 직접 처리했으므로)
    }
};

//아이템 우체통
class item_mail : public ItemScript
{
public:
    item_mail() : ItemScript("item_mail") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& targets)
    {
		player->PlayerTalkClass->ClearMenus();
		if (player->HasItemCount(item->GetEntry(), 1))
		{
			player->DestroyItemCount(item->GetEntry(), 1, true);
			player->GetSession()->SendShowMailBox(player->GetGUID());
		}
		return true;
    }
};

class item_buff : public ItemScript
{
public:
    item_buff() : ItemScript("item_buff") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& targets)
    {
		player->PlayerTalkClass->ClearMenus();
		uint32 mapid = player->GetMapId();
		uint32 zone = player->GetZoneId();
		MapEntry const* mEntry = sMapStore.LookupEntry(mapid);
		if (!player->IsAlive() || mEntry->IsBattlegroundOrArena() || player->IsInCombat() || player->HasInvisibilityAura() || player->HasStealthAura() || player->InBattleground() || player->InArena() || zone == 4197)
		{
			player->GetSession()->SendNotification("사용할수없습니다.");
			return true;
		}
		else
		{
			if (player->HasItemCount(item->GetEntry(), 1))
			{
				player->DestroyItemCount(item->GetEntry(), 1, true);
				player->CastSpell(player, 24425, true); //잔달라의 기백  v
				player->CastSpell(player, 22888, true); //용사냥꾼 재집결의 외침  v

				if (Pet* target = player->GetPet())
				{
					target->CastSpell(target, 24425, true);
					target->CastSpell(target, 22888, true);
				}
			}
			return true;
		}
    }
};

#include "ScriptMgr.h"
#include "Player.h"
#include "Item.h"
#include "Mail.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"
#include "ObjectMgr.h"
#include "Log.h"

#include <vector>
#include <unordered_map>
#include <string>

/*
 * 80레벨 DPS 부스트 아이템
 * - 직업 자동 판별 (DPS only)
 * - 장비/가방은 우편으로 분할 지급 (첨부 7개 제한 대응)
 * - 골드 4000 지급
 * - 계정당 1회
 */

enum BoostSetKey
{
    WARRIOR_DPS,
    ROGUE,
    HUNTER,
    MAGE,
    WARLOCK,
    PRIEST_SHADOW,
    DRUID_FERAL,
    SHAMAN_DPS,
    PALADIN_RETRIBUTION,
    DK_DPS
};

/* ===============================
 * DPS 세트 (무기/악세 등)
 * =============================== */
static std::unordered_map<BoostSetKey, std::vector<uint32>> SetItems =
{
    { WARRIOR_DPS,         {37852,37290,37624,37232,37847,37723} },
    { ROGUE,               {37856,37290,37624,37232,37847,37723} },
    { HUNTER,              {37191,37290,37624,37232,37847,37723} },
    { MAGE,                {37824,37290,37651,37232,37666,37734} },
    { WARLOCK,             {37824,37290,37651,37232,37666,37734} },
    { PRIEST_SHADOW,       {37824,37290,37651,37232,37666,37734} },
    { DRUID_FERAL,         {37179,38365,37290,37624,37232,37847,37723} },
    { SHAMAN_DPS,          {37693,38361,37290,37624,37232,37847,37723} },
    { PALADIN_RETRIBUTION, {37852,38368,37290,37624,37232,37847,37723} },
    { DK_DPS,              {37852,40822,37290,37624,37232,37847,37723} }
};

/* ===============================
 * DPS 방어구 세트
 * =============================== */
static std::unordered_map<BoostSetKey, std::vector<uint32>> SetArmor =
{
    { WARRIOR_DPS,   {37182,37376,37395,37363,37152,37193,37175,37150,37174} },
    { ROGUE,         {37149,37139,37165,37230,37194,37374,37183,37176,37174} },
    { HUNTER,        {37188,37373,37144,37614,37407,37155,37138,37167,37174} },
    { MAGE,          {37294,37196,37222,37153,37242,37189,37245,37218,37174} },
    { WARLOCK,       {37294,37196,37222,37153,37242,37189,37245,37218,37174} },
    { PRIEST_SHADOW, {37294,37196,37222,37153,37242,37189,37245,37218,37174} }
};

/* ===============================
 * 직업 → DPS 세트 매핑
 * =============================== */
static BoostSetKey GetDpsSetByClass(uint8 classId)
{
    switch (classId)
    {
        case CLASS_WARRIOR:      return WARRIOR_DPS;
        case CLASS_PALADIN:      return PALADIN_RETRIBUTION;
        case CLASS_HUNTER:       return HUNTER;
        case CLASS_ROGUE:        return ROGUE;
        case CLASS_PRIEST:       return PRIEST_SHADOW;
        case CLASS_DEATH_KNIGHT: return DK_DPS;
        case CLASS_SHAMAN:       return SHAMAN_DPS;
        case CLASS_MAGE:         return MAGE;
        case CLASS_WARLOCK:      return WARLOCK;
        case CLASS_DRUID:        return DRUID_FERAL;
        default:                 return WARRIOR_DPS;
    }
}

/* ===============================
 * 메일 분할 발송 (첨부 7개 제한)
 * =============================== */
static void SendItemsByMailSplit(Player* player,
                                 std::string const& subject,
                                 std::string const& body,
                                 std::vector<uint32> const& items)
{
    if (!player || items.empty())
        return;

    const uint32 MAX_ATTACH = 7;
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    uint32 idx = 0;
    uint32 mailCount = 0;

    while (idx < items.size())
    {
        MailDraft draft(subject, body);
        uint32 attach = 0;

        while (idx < items.size() && attach < MAX_ATTACH)
        {
            uint32 entry = items[idx++];

            if (Item* it = Item::CreateItem(entry, 1, player))
            {
                it->SaveToDB(trans);
                draft.AddItem(it);
                ++attach;
            }
        }

        if (attach == 0)
            break;

        draft.SendMailTo(
            trans,
            MailReceiver(player),
            MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM)
        );

        ++mailCount;
    }

    CharacterDatabase.CommitTransaction(trans);

    if (mailCount > 0)
        ChatHandler(player->GetSession())
            .PSendSysMessage("80레벨 DPS 지원 아이템이 우편으로 발송되었습니다.");
}

/* ===============================
 * 아이템 스크립트
 * =============================== */
class item_level80_boost_dps : public ItemScript
{
public:
    item_level80_boost_dps() : ItemScript("item_level80_boost_dps") {}

    bool OnUse(Player* player, Item* item, SpellCastTargets const&) override
    {
        if (!player || !item)
            return false;

        uint32 accountId = player->GetSession()->GetAccountId();

        /* 계정당 1회 체크 */
        QueryResult used = CharacterDatabase.Query(
            Trinity::StringFormat(
                "SELECT 1 FROM appdata.account_level80_boost WHERE account_id={}",
                accountId
            ).c_str()
        );

        if (used)
        {
            ChatHandler(player->GetSession())
                .SendSysMessage("이 계정은 이미 80레벨 지원을 사용했습니다.");
            return false;
        }

        /* 레벨 80 */
        if (player->GetLevel() < 80)
        {
            player->SetLevel(80);
            player->InitTalentForLevel();
            player->SetFreeTalentPoints(0);
        }

        BoostSetKey setKey = GetDpsSetByClass(player->GetClass());

        /* 메일로 보낼 아이템 목록 구성 */
        std::vector<uint32> mailItems;

        for (uint32 e : SetItems[setKey])
            mailItems.push_back(e);

        auto itArmor = SetArmor.find(setKey);
        if (itArmor != SetArmor.end())
        {
            for (uint32 e : itArmor->second)
                mailItems.push_back(e);
        }

        /* 가방 4개 (1개씩) */
        for (uint32 i = 0; i < 4; ++i)
            mailItems.push_back(51809);

        SendItemsByMailSplit(
            player,
            "80레벨 DPS 서버 지원",
            "서버 지원으로 지급되는 80레벨 DPS 장비 세트입니다.\n"
            "우편에 첨부된 아이템을 수령해 주세요.",
            mailItems
        );

        /* 골드 4000 */
        player->ModifyMoney(4000 * 10000);
		player->LearnSpell(34091, false);

        /* 사용 기록 */
        CharacterDatabase.Execute(
            Trinity::StringFormat(
                "INSERT INTO appdata.account_level80_boost (account_id, used_at) VALUES ({}, NOW())",
                accountId
            ).c_str()
        );

        /* 아이템 소모 */
        player->DestroyItemCount(item->GetEntry(), 1, true);

        return true;
    }
};

//로그인 카운트
class account_daily_ingame : public AccountScript
{
public:
    account_daily_ingame() : AccountScript("account_daily_ingame") { }

    void OnAccountLogin(uint32 accountId) override
    {
		uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
		if (realmId == 1)
		{
			if (!accountId)
				return;

			CharacterDatabase.PExecute(
				"INSERT INTO appdata.web_daily_stats (stat_date, ingame) "
				"VALUES (CURDATE(), 1) "
				"ON DUPLICATE KEY UPDATE "
				"ingame = ingame + 1"
			);

			// 계정명 조회 (Auth DB)
			std::string username = "Account " + std::to_string(accountId);

			if (QueryResult r = LoginDatabase.PQuery(
					"SELECT username FROM account WHERE id={}",
					accountId))
			{
				Field* f = r->Fetch();
				username = f[0].GetString();
			}

	/*		SendServerDiscordEvent(
				"🟢 로그인",
				"계정: " + username,
				"https://stimpack.pe.kr",
				3066993,
				"home"
			);  */
		}
    }
};

// 40레벨 미만 1개월 / 전체 레벨 6개월 이상 미접속 관리
#include "ScriptMgr.h"
#include "TaskScheduler.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "Chat.h"

class inactive_character_archive : public WorldScript
{
public:
    inactive_character_archive() : WorldScript("inactive_character_archive") { }

    TaskScheduler _scheduler;

    void OnStartup() override
    {
        ScheduleNextRun();
    }

    void OnUpdate(uint32 diff) override
    {
        _scheduler.Update(diff);
    }

    void ScheduleNextRun()
    {
        time_t now = time(nullptr);
        tm local = *localtime(&now);

        tm target = local;
        target.tm_hour = 4;
        target.tm_min  = 0;
        target.tm_sec  = 0;

        time_t targetTime = mktime(&target);

        if (targetTime <= now)
            targetTime += 86400;

        uint32 delay = uint32((targetTime - now) * 1000);

        _scheduler.Schedule(Milliseconds(delay), [this](TaskContext)
        {
            ProcessArchive();
            ScheduleNextRun();
        });
    }

    void ProcessArchive()
    {
        // 계정 LOCK 테이블을 확인할 수 없으면 미접속 정리를 중단한다.
        QueryResult accountLockTableCheck = CharacterDatabase.PQuery(
            "SELECT COUNT(*) FROM appdata.account_character_locks"
        );
        if (!accountLockTableCheck)
            return;

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT c.account, c.guid, c.name, c.logout_time, c.level "
            "FROM characters.characters c "
            "LEFT JOIN appdata.character_archive_log l ON l.guid = c.guid "
            "WHERE c.logout_time > 0 "
            "AND (c.logout_time <= UNIX_TIMESTAMP(DATE_SUB(NOW(), INTERVAL 6 MONTH)) "
            "OR (c.level < 40 AND c.logout_time <= UNIX_TIMESTAMP(DATE_SUB(NOW(), INTERVAL 1 MONTH)))) "
            "AND c.online = 0 "
            "AND l.guid IS NULL "
            "AND NOT EXISTS ( "
            "SELECT 1 FROM appdata.account_character_locks account_lock "
            "WHERE account_lock.account_id = c.account AND account_lock.is_active = 1)"
        );

        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();

            uint32 account = fields[0].GetUInt32();
            uint32 guid    = fields[1].GetUInt32();
            std::string name = fields[2].GetString();
            uint32 logout  = fields[3].GetUInt32();
            uint32 level   = fields[4].GetUInt32();
            uint32 now     = uint32(time(nullptr));
            char const* archiveType = level < 40 ? "under40_1month" : "inactive_6months";

            // 대상 조회 후 LOCK을 구매한 계정도 정리에서 제외한다.
            QueryResult accountLockCheck = CharacterDatabase.PQuery(
                "SELECT COUNT(*) FROM appdata.account_character_locks "
                "WHERE account_id = {} AND is_active = 1",
                account
            );
            if (!accountLockCheck)
                return;
            if (accountLockCheck->Fetch()[0].GetUInt64() != 0)
                continue;

            QueryResult exists = CharacterDatabase.PQuery(
                "SELECT 1 "
                "FROM appdata.inactive_character_lock "
                "WHERE guid = {} "
                "LIMIT 1",
                guid
            );

            CharacterDatabase.EscapeString(name);

            CharacterDatabase.PExecute(
                "INSERT INTO appdata.inactive_character_lock "
                "(guid, account_id, name, last_logout, locked_at, active, archive_type) "
                "SELECT {}, {}, '{}', {}, {}, 1, '{}' "
                "WHERE NOT EXISTS ( "
                "SELECT 1 FROM appdata.account_character_locks account_lock "
                "WHERE account_lock.account_id = {} AND account_lock.is_active = 1) "
                "ON DUPLICATE KEY UPDATE "
                "account_id = VALUES(account_id), "
                "name = VALUES(name), "
                "last_logout = VALUES(last_logout), "
                "locked_at = VALUES(locked_at), "
                "active = 1, "
                "archive_type = VALUES(archive_type)",
                guid,
                account,
                name,
                logout,
                now,
                archiveType,
                account
            );

            if (!exists)
            {
                CharacterDatabase.PExecute(
                    "INSERT INTO appdata.character_archive_log "
                    "(guid, name, account_id, last_logout, archived_at, archive_type) "
                    "SELECT {}, '{}', {}, {}, {}, '{}' "
                    "WHERE NOT EXISTS ( "
                    "SELECT 1 FROM appdata.account_character_locks account_lock "
                    "WHERE account_lock.account_id = {} AND account_lock.is_active = 1)",
                    guid,
                    name,
                    account,
                    logout,
                    now,
                    archiveType,
                    account
                );
            }

        } while (result->NextRow());
    }
};

class inactive_character_lock_player : public PlayerScript
{
public:
    inactive_character_lock_player() : PlayerScript("inactive_character_lock_player") { }

    bool IsLocked(Player* player)
    {
        if (!player)
            return false;

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT 1 "
            "FROM appdata.inactive_character_lock "
            "WHERE guid = {} "
            "AND active = 1 "
            "LIMIT 1",
            player->GetGUID().GetCounter()
        );

        return result != nullptr;
    }

    void ApplyLock(Player* player)
    {
        if (!player)
            return;

        if (!player->HasAura(9454))
            player->CastSpell(player, 9454, true);

        ChatHandler(player->GetSession()).PSendSysMessage("장기 미접속 캐릭터로 분류되어 현재 이용이 제한되었습니다.");
    }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!IsLocked(player))
            return;

        ApplyLock(player);
    }

    void OnMapChanged(Player* player) override
    {
        if (!IsLocked(player))
            return;

        ApplyLock(player);
    }

    void OnUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
    {
        if (!IsLocked(player))
            return;

        ApplyLock(player);
    }

    void OnChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg) override
    {
        if (!IsLocked(player))
            return;

        msg.clear();
        ChatHandler(player->GetSession()).PSendSysMessage("이 캐릭터는 장기 미접속 상태로 채팅이 제한되었습니다.");
    }

    void OnChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg, Player* /*receiver*/) override
    {
        if (!IsLocked(player))
            return;

        msg.clear();
        ChatHandler(player->GetSession()).PSendSysMessage("이 캐릭터는 장기 미접속 상태로 채팅이 제한되었습니다.");
    }

    void OnChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg, Group* /*group*/) override
    {
        if (!IsLocked(player))
            return;

        msg.clear();
        ChatHandler(player->GetSession()).PSendSysMessage("이 캐릭터는 장기 미접속 상태로 채팅이 제한되었습니다.");
    }

    void OnChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg, Guild* /*guild*/) override
    {
        if (!IsLocked(player))
            return;

        msg.clear();
        ChatHandler(player->GetSession()).PSendSysMessage("이 캐릭터는 장기 미접속 상태로 채팅이 제한되었습니다.");
    }

    void OnChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg, Channel* /*channel*/) override
    {
        if (!IsLocked(player))
            return;

        msg.clear();
        ChatHandler(player->GetSession()).PSendSysMessage("이 캐릭터는 장기 미접속 상태로 채팅이 제한되었습니다.");
    }
};

//솔로공격대
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "Group.h"
#include "GroupMgr.h"

//player->TeleportTo(532, -11107.200195f, -1842.880005f, 229.707993f, 5.365469f );
//Position const WaitingRoomPos = { -11107.200195f, -1842.880005f, 229.707993f, 5.365469f };
enum SoloRaidNpc
{
    GOSSIP_MENU_SOLO_RAID = 90001,
    SOLO_RAID_COST = 1000
};

class npc_solo_raid_creator : public CreatureScript
{
public:
    npc_solo_raid_creator() : CreatureScript("npc_solo_raid_creator") { }

    struct npc_solo_raid_creatorAI : public ScriptedAI
    {
        npc_solo_raid_creatorAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            InitGossipMenuFor(player, GOSSIP_MENU_SOLO_RAID);

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "체스방으로 이동... (1000 RP 소모-- 현재 무료)", GOSSIP_SENDER_MAIN,  GOSSIP_ACTION_INFO_DEF + 1);

            SendGossipMenuFor(player, GOSSIP_MENU_SOLO_RAID, me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            if (action == GOSSIP_ACTION_INFO_DEF + 1)
            {
         /*       uint32 accountId = player->GetSession()->GetAccountId();

                long long currentRP = RP::Get(accountId);

                if (currentRP < SOLO_RAID_COST)
                {
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "RP가 부족합니다. 필요 RP: %u (현재: %lld)",
                        SOLO_RAID_COST, currentRP);
                    CloseGossipMenuFor(player);
                    return true;
                }

                long long afterRP = 0;
                if (!RP::TrySpendOne(accountId, SOLO_RAID_COST, afterRP))
                {
                    ChatHandler(player->GetSession()).SendSysMessage("RP 차감 중 오류가 발생했습니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                Group* group = new Group;
                group->Create(player);
                group->ConvertToRaid();

				long long after = RP::Get(accountId) - SOLO_RAID_COST;
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "1000 RP가 차감되었습니다. 남은 RP: %lld", after); */

				// 이동
				 player->TeleportTo(532, -11107.200195f, -1842.880005f, 229.707993f, 5.365469f );
            }

            CloseGossipMenuFor(player);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_solo_raid_creatorAI(creature);
    }
};

class ItemDestroyLogScript : public ItemScript
{
public:
	ItemDestroyLogScript() : ItemScript("ItemDestroyLogScript") { }

	bool OnRemove(Player* player, Item* item) override
	{
		if (!player || !item)
			return true;

		ItemTemplate const* proto = item->GetTemplate();
		if (!proto)
			return true;

		switch (proto->Class)
		{
			case ITEM_CLASS_WEAPON:
			case ITEM_CLASS_ARMOR:
			case ITEM_CLASS_GEM:
			case ITEM_CLASS_TRADE_GOODS:
			case ITEM_CLASS_RECIPE:
				break;
			default:
				return true;
		}

		if (proto->Quality < ITEM_QUALITY_RARE)
			return true;

		uint32 accountId = player->GetSession()->GetAccountId();
		uint32 charGuid  = player->GetGUID().GetCounter();
		uint32 itemId    = proto->ItemId;

		std::string itemName = proto->Name1;

		std::string emoji = "💎";

		if (proto->Quality == ITEM_QUALITY_RARE)
			emoji = "🔷";
		else if (proto->Quality == ITEM_QUALITY_EPIC)
			emoji = "🟣";
		else if (proto->Quality == ITEM_QUALITY_LEGENDARY)
			emoji = "🟠";

		std::string msg =
			emoji + " " + itemName + " 파괴";

		CharacterDatabase.EscapeString(msg);

		std::string q =
			"INSERT INTO appdata.activity_log "
			"(account_id, character_guid, type, message, item_entry) VALUES ("
			+ std::to_string(accountId) + ", "
			+ std::to_string(charGuid) + ", "
			"'destroy', '"
			+ msg + "', "
			+ std::to_string(itemId) +
			");";

		CharacterDatabase.DirectExecute(q.c_str());

		return true;
	}
};

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Player.h"
#include "Chat.h"

class go_time_attack_end : public GameObjectScript
{
	public:
		go_time_attack_end() : GameObjectScript("go_time_attack_end") { }

		struct go_time_attack_endAI : public GameObjectAI
		{
			go_time_attack_endAI(GameObject* go) : GameObjectAI(go) { }

			bool OnGossipHello(Player* player) override
			{
				if (!player)
					return false;

				Map* map = player->GetMap();
				if (!map || !map->IsDungeon())
					return false;

				uint32 instanceId = map->GetInstanceId();

				QueryResult r = CharacterDatabase.PQuery("SELECT start_time, finished FROM character_dungeon_run WHERE instance_id={}", instanceId);

				if (!r)
					return false;

				Field* f = r->Fetch();

				uint32 startTime = f[0].GetUInt32();
				uint32 endTime = GameTime::GetGameTime();
				uint32 diff = endTime - startTime;

				CharacterDatabase.PExecute( "UPDATE character_dungeon_run SET finished=1 WHERE instance_id={}", instanceId);

				QueryResult members = CharacterDatabase.PQuery("SELECT character_guid FROM character_dungeon_session WHERE instance_id={}", instanceId);

				if (members)
				{
					do
					{
						Field* f = members->Fetch();
						uint32 guid = f[0].GetUInt32();

						// account_id 조회
						QueryResult acc = CharacterDatabase.PQuery("SELECT account FROM characters WHERE guid={}",guid);

						if (!acc)
							continue;

						uint32 accountId = acc->Fetch()[0].GetUInt32();
						MapEntry const* mapEntry = sMapStore.LookupEntry(map->GetId());
						std::string mapName = "Unknown";

						if (mapEntry)
						{
							uint8 locale = player->GetSession()->GetSessionDbcLocale();

							if (mapEntry->MapName[locale] && mapEntry->MapName[locale][0] != '\0')
								mapName = mapEntry->MapName[locale];
							else
								mapName = mapEntry->MapName[0]; // fallback
						}

						time_t now = GameTime::GetGameTime();
						tm t = *localtime(&now);

						// 새벽 4시 이전이면 하루 빼기
						if (t.tm_hour < 4)
							now -= 86400;

						t = *localtime(&now);

						// 오늘 04:00으로 고정
						t.tm_hour = 4;
						t.tm_min = 0;
						t.tm_sec = 0;

						uint32 challengeDate = uint32(mktime(&t));

					//	CharacterDatabase.PExecute("INSERT INTO appdata.dungeon_rank (map_id, map_name, clear_time, account_id, character_guid, start_time) VALUES ({}, '{}', {}, {}, {}, {})", map->GetId(), mapName, diff, accountId, guid, startTime);
						CharacterDatabase.PExecute(
							"INSERT INTO appdata.dungeon_rank "
							"(map_id, map_name, clear_time, account_id, character_guid, start_time, challenge_date) "
							"VALUES ({}, '{}', {}, {}, {}, {}, {}) "
							"ON DUPLICATE KEY UPDATE "
							"clear_time = IF(VALUES(clear_time) < clear_time, VALUES(clear_time), clear_time), "
							"start_time = IF(VALUES(clear_time) < clear_time, VALUES(start_time), start_time)",
							map->GetId(),
							mapName,
							diff,
							accountId,
							guid,
							startTime,
							challengeDate
						);

						uint32 min = diff / 60;
						uint32 sec = diff % 60;

						std::string msg = "타임어택 완료 - " + mapName +
										  " / 기록: " + std::to_string(min) + "분 " + std::to_string(sec) + "초";

						CharacterDatabase.PExecute(
							"INSERT INTO appdata.activity_log (account_id, character_guid, type, message, created_at, item_entry) "
							"VALUES ({}, {}, '{}', '{}', NOW(), 0)",
							accountId,
							guid,
							"dungeon_clear",
							msg
						);

					} while (members->NextRow());
				}

				ChatHandler(player->GetSession()).SendSysMessage("타임어택 종료!");

				me->DespawnOrUnsummon();

				return true;
			}
    };

	GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_time_attack_endAI(go);
    }
};



//로또
#include "ScriptMgr.h"
#include "World.h"
#include "DatabaseEnv.h"

class LottoDrawScript : public WorldScript
{
public:
    LottoDrawScript() : WorldScript("LottoDrawScript") {}

    uint32 _timer = 0;
    time_t _lastTick = 0;

    std::vector<int> _nums;
    int _bonus = 0;
    int _step = 0;
    bool _initialized = false;

    uint32 _roundId = 0;

	// 🔥 여기 (클래스 안)
	static std::unordered_set<uint64> _participants;
	static bool _eventActive;

    void OnUpdate(uint32 diff) override
    {
        _timer += diff;
        if (_timer < 1000)
            return;

        _timer = 0;

        time_t now = time(nullptr);
        tm local = *localtime(&now);

        // 🔥 토요일 21시만 동작
        if (local.tm_wday != 6 || local.tm_hour != 21)
	//	if (local.tm_wday != 0 || local.tm_hour != 14)
        {
            _initialized = false;
            _step = 0;
            return;
        }

		// 🔥 DB 체크 (open 회차 존재 여부)
		QueryResult r = CharacterDatabase.PQuery(
			"SELECT id FROM appdata.lotto_round WHERE status='open' ORDER BY id DESC LIMIT 1"
		);

		if (!r)
			return;

        // 🔥 최초 1회 초기화
        if (!_initialized)
        {
            _initialized = true;

			Field* f = r->Fetch();
			_roundId = f[0].GetUInt32();

            _step = 0;

            _nums.clear();

            // 🔥 랜덤 6개 생성
            while (_nums.size() < 6)
            {
                int n = urand(1, 45);
                if (std::find(_nums.begin(), _nums.end(), n) == _nums.end())
                    _nums.push_back(n);
            }

            std::sort(_nums.begin(), _nums.end());
            _bonus = urand(1, 45);

            sWorld->SendServerMessage(SERVER_MSG_STRING, "|cffFFD700[로또]|r 추첨이 시작됩니다!");

			// 🔥 참여자 기록 시작
			_participants.clear();

			SessionMap const& sessions = sWorld->GetAllSessions();
			for (auto const& itr : sessions)
			{
				if (Player* player = itr.second->GetPlayer())
					_participants.insert(player->GetGUID().GetRawValue());
			}

			_eventActive = true;
        }

        // 🔥 5분 간격
        if (_lastTick != 0 && (now - _lastTick) < 300)
            return;

        _lastTick = now;

        std::ostringstream oss;

        // 🔥 숫자 순차 공개 + DB append
        if (_step < 6)
        {
            int current = _nums[_step];

            oss << "|cffFFD700[로또]|r "
                << (_step + 1)
                << "번째 번호: |cff00FF00"
                << current << "|r";

            sWorld->SendServerMessage(SERVER_MSG_STRING, oss.str().c_str());

            // 🔥 기존 draw_numbers 가져오기
            QueryResult r = CharacterDatabase.PQuery("SELECT draw_numbers FROM appdata.lotto_round WHERE id={}",_roundId);

            std::string currentNumbers = "";
            if (r)
                currentNumbers = r->Fetch()[0].GetString();

            // 🔥 append
            std::string newNumbers;
            if (currentNumbers.empty())
                newNumbers = std::to_string(current);
            else
                newNumbers = currentNumbers + "," + std::to_string(current);

            CharacterDatabase.PExecute("UPDATE appdata.lotto_round SET draw_numbers='{}' WHERE id={}", newNumbers, _roundId);

            _step++;
        }
        // 🔥 보너스 + 완료 처리
        else if (_step == 6)
        {
            oss << "|cffFFD700[로또]|r 보너스 번호: |cff00FFFF"
                << _bonus << "|r";

            sWorld->SendServerMessage(SERVER_MSG_STRING, oss.str().c_str());

            CharacterDatabase.PExecute("UPDATE appdata.lotto_round SET bonus={}, status='drawn' WHERE id={}",_bonus, _roundId);

            _step++;

			// 🔥 참여자 보상 지급
			for (uint64 guid : _participants)
			{
				Player* player = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(guid));

				if (!player)
					continue;

				uint32 itemId = 4418; // 🔥 보상 아이템

				MailDraft draft("로또 참여 보상", "끝까지 참여 감사합니다!");
				CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

				if (Item* reward = Item::CreateItem(itemId, 1, nullptr))
				{
					reward->SaveToDB(trans);
					draft.AddItem(reward);
				}

				draft.SendMailTo(
					trans,
					MailReceiver(player),
					MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM)
				);

				CharacterDatabase.CommitTransaction(trans);
			}

			// 🔥 초기화
			_participants.clear();
			_eventActive = false;

        }
    }
};

// 🔥 여기 (클래스 밖)
std::unordered_set<uint64> LottoDrawScript::_participants;
bool LottoDrawScript::_eventActive = false;

class LottoPlayerScript : public PlayerScript
{
public:
    LottoPlayerScript() : PlayerScript("LottoPlayerScript") {}

    void OnLogout(Player* player) override
    {
        if (!player)
            return;

        if (LottoDrawScript::_eventActive)
            LottoDrawScript::_participants.erase(player->GetGUID().GetRawValue());
    }
};

//랜덤박스
#include "ScriptMgr.h"
#include "Player.h"
#include "Item.h"
#include "Mail.h"
#include "DatabaseEnv.h"

class item_random_box : public ItemScript
{
public:
    item_random_box() : ItemScript("item_random_box") {}

    bool OnUse(Player* player, Item* item, SpellCastTargets const&) override
    {
        if (!player || !item)
            return false;

        uint32 accountId = player->GetSession()->GetAccountId();
        uint32 charGuid  = player->GetGUID().GetCounter();

        // 🔥 랜덤 아이템 선택
        QueryResult result = CharacterDatabase.Query(
            "SELECT game_item_id FROM appdata.shop_items WHERE category_id = 5 ORDER BY RAND() LIMIT 1"
        );

        if (!result)
        {
            ChatHandler(player->GetSession()).SendSysMessage("랜덤박스 아이템이 없습니다.");
            return true;
        }

        Field* f = result->Fetch();
        uint32 itemId = f[0].GetUInt32();

        // 🔥 우편 지급 시작
        MailDraft draft("랜덤박스 보상", "축하합니다! 랜덤 아이템이 지급되었습니다.");
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        Item* reward = Item::CreateItem(itemId, 1, nullptr);

        if (!reward)
        {
            ChatHandler(player->GetSession()).SendSysMessage("아이템 생성 실패");
            return true; // ❗ 소비 안함
        }

        const ItemTemplate* proto = reward->GetTemplate();
        if (!proto)
        {
            ChatHandler(player->GetSession()).SendSysMessage("아이템 템플릿 오류");
            return true;
        }

        reward->SaveToDB(trans);
        draft.AddItem(reward);

        // 🔥 서버 알림
        std::string msg = Trinity::StringFormat(
            "|cffFFD700[랜덤박스]|r [{}] 획득!",
            proto->Name1.c_str()
        );
        sWorld->SendServerMessage(SERVER_MSG_STRING, msg.c_str(), 0);

        // 🔥 activity_log
        std::string logMsg =
            "🎁 랜덤박스 → " + std::string(proto->Name1) + " 획득";

        CharacterDatabase.EscapeString(logMsg);

        std::string q =
            "INSERT INTO appdata.activity_log "
            "(account_id, character_guid, type, message, item_entry) VALUES ("
            + std::to_string(accountId) + ", "
            + std::to_string(charGuid) + ", "
            "'random_box', '"
            + logMsg + "', "
            + std::to_string(itemId) +
            ");";

        CharacterDatabase.DirectExecute(q.c_str());

		draft.SendMailTo(
            trans,
            MailReceiver(player),
            MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM)
        );

        CharacterDatabase.CommitTransaction(trans);

        // 🔥 성공 시에만 소비
        player->DestroyItemCount(item->GetEntry(), 1, true);

        ChatHandler(player->GetSession()).SendSysMessage("랜덤박스 보상이 우편으로 지급되었습니다. 우편확인이 않될시 /reload 해보세요");

        return true;
    }
};

class item_mount_random_box : public ItemScript
{
public:
    item_mount_random_box() : ItemScript("item_mount_random_box") {}

    bool OnUse(Player* player, Item* item, SpellCastTargets const&) override
    {
        if (!player || !item)
            return false;

        uint32 accountId = player->GetSession()->GetAccountId();
        uint32 charGuid  = player->GetGUID().GetCounter();

        // 🔥 랜덤 아이템 선택
        QueryResult result = CharacterDatabase.Query(
            "SELECT game_item_id FROM appdata.shop_items WHERE category_id = 1 ORDER BY RAND() LIMIT 1"
        );

        if (!result)
        {
            ChatHandler(player->GetSession()).SendSysMessage("랜덤박스 아이템이 없습니다.");
            return true;
        }

        Field* f = result->Fetch();
        uint32 itemId = f[0].GetUInt32();

        // 🔥 우편 지급 시작
        MailDraft draft("랜덤박스 보상", "축하합니다! 랜덤 아이템이 지급되었습니다.");
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        Item* reward = Item::CreateItem(itemId, 1, nullptr);

        if (!reward)
        {
            ChatHandler(player->GetSession()).SendSysMessage("아이템 생성 실패");
            return true; // ❗ 소비 안함
        }

        const ItemTemplate* proto = reward->GetTemplate();
        if (!proto)
        {
            ChatHandler(player->GetSession()).SendSysMessage("아이템 템플릿 오류");
            return true;
        }

        reward->SaveToDB(trans);
        draft.AddItem(reward);

        // 🔥 서버 알림
        std::string msg = Trinity::StringFormat(
            "|cffFFD700[랜덤박스]|r [{}] 획득!",
            proto->Name1.c_str()
        );
        sWorld->SendServerMessage(SERVER_MSG_STRING, msg.c_str(), 0);

        // 🔥 activity_log
        std::string logMsg =
            "🎁 랜덤박스 → " + std::string(proto->Name1) + " 획득";

        CharacterDatabase.EscapeString(logMsg);

        std::string q =
            "INSERT INTO appdata.activity_log "
            "(account_id, character_guid, type, message, item_entry) VALUES ("
            + std::to_string(accountId) + ", "
            + std::to_string(charGuid) + ", "
            "'random_box', '"
            + logMsg + "', "
            + std::to_string(itemId) +
            ");";

        CharacterDatabase.DirectExecute(q.c_str());

		draft.SendMailTo(
            trans,
            MailReceiver(player),
            MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM)
        );

        CharacterDatabase.CommitTransaction(trans);

        // 🔥 성공 시에만 소비
        player->DestroyItemCount(item->GetEntry(), 1, true);

        ChatHandler(player->GetSession()).SendSysMessage("랜덤박스 보상이 우편으로 지급되었습니다. 우편확인이 않될시 /reload 해보세요");

        return true;
    }
};


#include "ScriptMgr.h"
#include "Player.h"
#include "WorldSession.h"

#include <curl/curl.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

// 🔥 CURL 응답 저장
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output)
{
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

// 🔥 PHP 공지 가져오기
std::string GetNoticeFromPHP()
{
    CURL* curl = curl_easy_init();
    std::string result;

    if (!curl)
        return "";

    curl_easy_setopt(curl, CURLOPT_URL, "https://stimpack.pe.kr/api/notice.php");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    // 🔥 리다이렉트 허용
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return "";

    return result;
}

// 🔥 SHA256 생성
std::string GetHash(const std::string& input)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)input.c_str(), input.size(), hash);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];

    return ss.str();
}

// 🔥 로그인 시 공지 전송
class custom_server_notice : public PlayerScript
{
public:
    custom_server_notice() : PlayerScript("custom_server_notice") {}

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player)
            return;

        std::string msg = GetNoticeFromPHP();

        if (msg.empty())
            return;

        std::string hash = GetHash(msg);
        uint32 guid = player->GetGUID().GetCounter();
		uint32 accountId = player->GetSession()->GetAccountId();

        // 🔥 기존 공지 확인
        QueryResult result = CharacterDatabase.PQuery(
			"SELECT notice_hash FROM appdata.account_notice_log WHERE account_id = {}",
			accountId
		);

		if (result)
		{
			Field* fields = result->Fetch();
			std::string last = fields[0].GetString();

			if (last == hash)
				return; // 이미 본 공지 → 스킵
		}

        // 🔥 색상 적용 + 가독성 강화
		msg = "|cff66ff66" + msg + "|r";

        WorldSession* session = player->GetSession();

        WorldPacket data(SMSG_NOTIFICATION, msg.size() + 1);
        data << msg;
        session->SendPacket(&data);

        // 🔥 저장 (UPSERT)
        CharacterDatabase.PExecute(
			"INSERT INTO appdata.account_notice_log (account_id, notice_hash) VALUES ({}, '{}') "
			"ON DUPLICATE KEY UPDATE notice_hash = VALUES(notice_hash), seen_at = NOW()",
			accountId, hash
		);
    }
};

///지엠채팅
// chat_gm_only.cpp
// ─────────────────────────────────────────────
// 기능
// 1) "gm <내용>" → GM NPC 귓말
// 2) 기존 채팅은 완전 차단이 아니라 "무력화"
// 3) 오류 메시지 제거
// 4) DB 저장
// ─────────────────────────────────────────────

#include "ScriptMgr.h"
#include "Player.h"
#include "Chat.h"

#include <thread>
#include <sstream>
#include <cstdlib>
#include <algorithm>

namespace
{
    constexpr bool kEnabled = true;

    inline bool StartsWith(const std::string& s, const std::string& pfx)
    {
        return s.size() >= pfx.size() && std::equal(pfx.begin(), pfx.end(), s.begin());
    }

    inline std::string ShellEscapeSingleQuoted(const std::string& raw)
    {
        std::string out;
        out.reserve(raw.size() + 16);

        for (char c : raw)
            out += (c == '\'' ? "'\"'\"'" : std::string(1, c));

        return "'" + out + "'";
    }

    inline void AsyncLogGM(uint32 accountId, const std::string& playerName, const std::string& content)
    {
        std::ostringstream cmd;
        cmd << "/usr/bin/php /home/esotar/fcm/gm_log.php"
            << " --account=" << ShellEscapeSingleQuoted(std::to_string(accountId))
            << " --name=" << ShellEscapeSingleQuoted(playerName)
            << " --msg=" << ShellEscapeSingleQuoted(content)
            << " >/dev/null 2>&1";

        std::thread([c = cmd.str()]()
        {
            std::system(c.c_str());
        }).detach();
    }
}

class ChatGMOnly : public PlayerScript
{
public:
    ChatGMOnly() : PlayerScript("ChatGMOnly") {}

    /// 🔥 단일 훅만 사용 (중복 방지 핵심)
    void OnChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg) override
	{
		if (!kEnabled || !player)
			return;

		if (msg.empty())
			return;

		if (!StartsWith(msg, "gm "))
			return;

		std::string content = msg.substr(3);
		if (content.empty())
			return;

		/// 🔥 주변 전파되는 채팅만 차단
		if (type == CHAT_MSG_SAY || type == CHAT_MSG_YELL)
		{
			msg = "";  // ← 공백 말고 완전 빈값
		}


        /// 🔥 DB 저장
        if (player->GetSession())
            AsyncLogGM(player->GetSession()->GetAccountId(), player->GetName(), content);
    }
};


#include "ScriptMgr.h"
#include "Player.h"
#include "Item.h"
#include "Spell.h"

class item_icc_faction_buff : public ItemScript
{
public:
    item_icc_faction_buff() : ItemScript("item_icc_faction_buff") { }

	bool OnUse(Player* player, Item* item, SpellCastTargets const&) override
    {
        if (!player)
            return false;

		// 겨울손아귀 호수 / 일반 전장 사용 불가
        // 겨울손아귀 호수 zoneId = 4197
        if (player->GetZoneId() == 4197 || (player->InBattleground() && !player->InArena()))
        {
            ChatHandler(player->GetSession()).SendSysMessage("이 아이템은 겨울손아귀 호수와 전장에서는 사용할 수 없습니다.");
            return true;
        }

        uint32 spellId = 0;

        if (player->GetTeamId() == TEAM_HORDE)
            spellId = 73822;
        else
            spellId = 73828;

        player->CastSpell(player, spellId, true);
		player->CastSpell(player, 16634, true);

		if (Pet* pet = player->GetPet())
        {
            pet->CastSpell(pet, spellId, true);
            pet->CastSpell(pet, 16634, true);
        }

		player->DestroyItemCount(item->GetEntry(), 1, true);
        return true;
    }
};

#include "ScriptMgr.h"
#include "Player.h"
#include "WorldSession.h"
#include "Channel.h"
#include "DatabaseEnv.h"

class faction_channel_chat_log : public PlayerScript
{
public:
    faction_channel_chat_log() : PlayerScript("faction_channel_chat_log") { }

    void OnChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg, Channel* channel) override
    {
        if (!player || !channel)
            return;

        if (msg.empty())
            return;

        std::string channelName = channel->GetName();

        // 기록할 공식 진영 채널명
        if (channelName != "빛의수호자")
            return;

        uint32 team = player->GetTeam(); // 호드 67 / 얼라이언스 469
        uint32 accountId = player->GetSession()->GetAccountId();
        uint64 playerGuid = player->GetGUID().GetCounter();
        std::string playerName = player->GetName();
        uint32 now = uint32(time(nullptr));

        LoginDatabase.EscapeString(playerName);
        LoginDatabase.EscapeString(channelName);
        LoginDatabase.EscapeString(msg);

        LoginDatabase.PExecute(
            "INSERT INTO appdata.faction_channel_chat_log "
            "(team, account_id, player_guid, player_name, channel_name, message, created_at) "
            "VALUES ({}, {}, {}, '{}', '{}', '{}', {})",
            team,
            accountId,
            playerGuid,
            playerName,
            channelName,
            msg,
            now
        );

		LoginDatabase.PExecute(
			"DELETE FROM appdata.faction_channel_chat_log "
			"WHERE team = {} "
			"AND channel_name = '{}' "
			"AND id NOT IN ( "
				"SELECT id FROM ( "
					"SELECT id "
					"FROM appdata.faction_channel_chat_log "
					"WHERE team = {} "
					"AND channel_name = '{}' "
					"ORDER BY id DESC "
					"LIMIT 5 "
				") AS recent_rows "
			")",
			team,
			channelName,
			team,
			channelName
		);
    }
};

#include "ScriptMgr.h"
#include "Player.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"

class guild_login_chat_insert : public PlayerScript
{
public:
    guild_login_chat_insert() : PlayerScript("guild_login_chat_insert") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
		uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
		if (realmId != 1)
			return;

        if (!player)
            return;

        if (!player->GetSession())
            return;

        uint32 guildId = player->GetGuildId();

        if (!guildId)
            return;

        uint32 accountId = player->GetSession()->GetAccountId();
        uint64 characterGuid = player->GetGUID().GetCounter();

        std::string characterName = player->GetName();
        std::string message = "로그인하였습니다.";

        LoginDatabase.EscapeString(characterName);
        LoginDatabase.EscapeString(message);

        LoginDatabase.PExecute(
            "INSERT INTO appdata.guild_tech_chat "
            "(guild_id, account_id, character_guid, character_name, message, created_at, deleted_at) "
            "VALUES ({}, {}, {}, '{}', '{}', NOW(), NULL)",
            guildId,
            accountId,
            characterGuid,
            characterName,
            message
        );
    }

	void OnLogout(Player* player) override
	{
		uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
		if (realmId != 1)
			return;

		if (!player)
			return;

		if (!player->GetSession())
			return;

		uint32 guildId = player->GetGuildId();

		if (!guildId)
			return;

		uint32 accountId = player->GetSession()->GetAccountId();
		uint64 characterGuid = player->GetGUID().GetCounter();

		std::string characterName = player->GetName();
		std::string message = "로그아웃하였습니다.";

		LoginDatabase.EscapeString(characterName);
		LoginDatabase.EscapeString(message);

		LoginDatabase.PExecute(
			"INSERT INTO appdata.guild_tech_chat "
			"(guild_id, account_id, character_guid, character_name, message, created_at, deleted_at) "
			"VALUES ({}, {}, {}, '{}', '{}', NOW(), NULL)",
			guildId,
			accountId,
			characterGuid,
			characterName,
			message
		);
	}
};

class guild_join_chat_insert : public GuildScript
{
public:
    guild_join_chat_insert() : GuildScript("guild_join_chat_insert") { }

    void OnAddMember(Guild* guild, Player* player, uint8& /*plRank*/) override
    {
        if (!guild)
            return;

        if (!player)
            return;

        if (!player->GetSession())
            return;

        uint32 guildId = guild->GetId();
        uint32 accountId = player->GetSession()->GetAccountId();
        uint64 characterGuid = player->GetGUID().GetCounter();

        std::string characterName = player->GetName();
        std::string message = "길드에 가입하였습니다.";

        LoginDatabase.EscapeString(characterName);
        LoginDatabase.EscapeString(message);

        LoginDatabase.PExecute(
            "INSERT INTO appdata.guild_tech_chat "
            "(guild_id, account_id, character_guid, character_name, message, created_at, deleted_at) "
            "VALUES ({}, {}, {}, '{}', '{}', NOW(), NULL)",
            guildId,
            accountId,
            characterGuid,
            characterName,
            message
        );
    }
};


namespace
{
    static constexpr uint32 OLD_BATTLE_MARK_ITEM_ENTRY = 3438;
}

class item_old_battle_mark_deposit : public ItemScript
{
public:
    item_old_battle_mark_deposit() : ItemScript("item_old_battle_mark_deposit") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        if (!player || !item)
            return true;

        if (item->GetEntry() != OLD_BATTLE_MARK_ITEM_ENTRY)
            return true;

        WorldSession* session = player->GetSession();
        if (!session)
            return true;

        uint32 accountId = session->GetAccountId();

        if (accountId == 0)
            return true;

        std::string query = fmt::format(
			"INSERT INTO appdata.account_data "
			"    (id, old_battle_mark) "
			"VALUES "
			"    ({}, 1) "
			"ON DUPLICATE KEY UPDATE "
			"    old_battle_mark = old_battle_mark + 1",
			accountId
		);

		LoginDatabase.DirectExecute(query.c_str());

		uint32 count = 1;
		player->DestroyItemCount(item, count, true);

		ChatHandler(session).SendSysMessage("낡은 전장의 훈장 1개가 홈페이지 전장 상점에 적립되었습니다.");

		return true;

        return true;
    }
};

#include "ScriptMgr.h"
#include "Player.h"
#include "Item.h"
#include "Chat.h"
#include "WorldSession.h"
#include "Map.h"
#include "SpellAuras.h"
#include "SpellAuraDefines.h"
#include "fmt/format.h"

#include <ctime>
#include <unordered_map>

namespace BattleShopRewardItems
{
    static constexpr uint32 ITEM_BATTLE_GUARD_CHARM       = 23245; // 전장의 수호 부적
    static constexpr uint32 ITEM_SMALL_ARENA_POINT_TICKET = 35554; // 소량 아레나 점수 교환권
    static constexpr uint32 ITEM_HEROIC_RESET_TICKET      = 35552; // 영웅 던전 초기화권

    static constexpr uint32 ARENA_POINT_REWARD = 50;

    // 전장의 수호 부적 효과용 기존 주문
    // Dispersion 계열: 받는 피해 90% 감소 성격.
    // 지속시간은 스크립트에서 5초로 줄임.
    static constexpr uint32 SPELL_BATTLE_GUARD_AURA = 29476;

    static constexpr uint32 BATTLE_GUARD_DURATION_MS = 5 * IN_MILLISECONDS;
    static constexpr uint32 BATTLE_GUARD_COOLDOWN_SEC = 10 * MINUTE;

    static constexpr uint32 ZONE_WINTERGRASP = 4197;

    static std::unordered_map<uint64, time_t> BattleGuardCooldowns;

    bool IsInAllowedBattleArea(Player* player)
    {
        if (!player)
            return false;

        if (player->InArena())
            return false;

        if (player->InBattleground())
            return true;

        if (player->GetMapId() == 571 && player->GetZoneId() == ZONE_WINTERGRASP)
            return true;

        return false;
    }

    void Send(Player* player, std::string const& message)
    {
        if (!player || !player->GetSession())
            return;

        ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
    }

    void ConsumeOne(Player* player, Item* item)
    {
        if (!player || !item)
            return;

        uint32 count = 1;
        player->DestroyItemCount(item, count, true);
    }

    void RemoveEmergencyControlAuras(Player* player)
    {
        if (!player)
            return;

        player->RemoveAurasByType(SPELL_AURA_MOD_STUN);
        player->RemoveAurasByType(SPELL_AURA_MOD_FEAR);
        player->RemoveAurasByType(SPELL_AURA_MOD_ROOT);
        player->RemoveAurasByType(SPELL_AURA_MOD_CONFUSE);
        player->RemoveAurasByType(SPELL_AURA_MOD_DECREASE_SPEED);
    }
}

class item_battle_guard_charm : public ItemScript
{
public:
    item_battle_guard_charm() : ItemScript("item_battle_guard_charm") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        using namespace BattleShopRewardItems;

        if (!player || !item)
            return true;

        if (item->GetEntry() != ITEM_BATTLE_GUARD_CHARM)
            return true;

        if (player->GetHealthPct() > 40.0f)
        {
            Send(player, "전장의 수호 부적은 생명력이 40% 이하일 때만 사용할 수 있습니다.");
            return true;
        }

        uint64 guidLow = player->GetGUID().GetCounter();
        time_t now = std::time(nullptr);

        auto itr = BattleGuardCooldowns.find(guidLow);
        if (itr != BattleGuardCooldowns.end() && itr->second > now)
        {
            uint32 remain = uint32(itr->second - now);
            uint32 minute = (remain + 59) / 60;

            Send(player, fmt::format("전장의 수호 부적은 아직 사용할 수 없습니다. 남은 시간: 약 {}분", minute));
            return true;
        }

        RemoveEmergencyControlAuras(player);

        Aura* aura = player->AddAura(SPELL_BATTLE_GUARD_AURA, player);
        if (!aura)
        {
            Send(player, "전장의 수호 부적 효과 적용에 실패했습니다.");
            return true;
        }

        aura->SetMaxDuration(BATTLE_GUARD_DURATION_MS);
        aura->SetDuration(BATTLE_GUARD_DURATION_MS);

        BattleGuardCooldowns[guidLow] = now + BATTLE_GUARD_COOLDOWN_SEC;

        ConsumeOne(player, item);

        Send(player, "전장의 수호 부적을 사용했습니다. 5초 동안 받는 피해가 크게 감소합니다.");

        return true;
    }
};

class item_small_arena_point_ticket : public ItemScript
{
public:
    item_small_arena_point_ticket() : ItemScript("item_small_arena_point_ticket") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        using namespace BattleShopRewardItems;

        if (!player || !item)
            return true;

        if (item->GetEntry() != ITEM_SMALL_ARENA_POINT_TICKET)
            return true;

        player->ModifyArenaPoints(int32(ARENA_POINT_REWARD));

        ConsumeOne(player, item);

        Send(player, fmt::format("아레나 점수 {}점을 획득했습니다.", ARENA_POINT_REWARD));

        return true;
    }
};

class item_heroic_dungeon_reset_ticket : public ItemScript
{
public:
    item_heroic_dungeon_reset_ticket() : ItemScript("item_heroic_dungeon_reset_ticket") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        using namespace BattleShopRewardItems;

        if (!player || !item)
            return true;

        if (item->GetEntry() != ITEM_HEROIC_RESET_TICKET)
            return true;

        if (player->IsInCombat())
        {
            Send(player, "전투 중에는 영웅 던전 초기화권을 사용할 수 없습니다.");
            return true;
        }

        Map* map = player->GetMap();
        if (map && map->IsDungeon())
        {
            Send(player, "인스턴스 내부에서는 영웅 던전 초기화권을 사용할 수 없습니다.");
            return true;
        }

        if (player->InBattleground() || player->InArena())
        {
            Send(player, "전장 또는 투기장 안에서는 영웅 던전 초기화권을 사용할 수 없습니다.");
            return true;
        }

        static constexpr uint8 INSTANCE_RESET_ALL_METHOD = 0;

        player->ResetInstances(INSTANCE_RESET_ALL_METHOD, false);

        ConsumeOne(player, item);

        Send(player, "영웅 던전 귀속이 초기화되었습니다. 레이드 귀속은 초기화되지 않습니다.");

        return true;
    }
};

//투기장 보상상자
#include "ScriptMgr.h"
#include "Player.h"
#include "Item.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"

namespace Season8BattleBoxes
{
    // 상자 아이템
    static constexpr uint32 ITEM_SEASON8_BATTLE_BOX      = 23656; // 시즌 8 전투상자
    static constexpr uint32 ITEM_SEASON8_HIGH_BATTLE_BOX = 22582; // 시즌 8 고급 전투상자

    // 보상 아이템
    static constexpr uint32 ITEM_ARENA_TOKEN             = 3438; // 투기장 증표
    static constexpr uint32 ITEM_BATTLE_HEALING_POTION   = 17348; // 전장 특급 치유 물약
    static constexpr uint32 ITEM_BATTLE_MANA_POTION      = 17351; // 전장 특급 마나 물약
    static constexpr uint32 ITEM_BATTLE_GUARD_CHARM      = 23245; // 전장의 수호 부적
    static constexpr uint32 ITEM_ARENA_POINT_TICKET      = 35554; // 전장의 아레나 증서
    static constexpr uint32 ITEM_HEROIC_RESET_TICKET     = 35552; // 영웅 던전 재입장 허가서

    static constexpr uint32 NORMAL_BOX_BASE_TOKEN = 3;
    static constexpr uint32 HIGH_BOX_BASE_TOKEN   = 8;

    static void Send(Player* player, std::string const& message)
    {
        if (!player || !player->GetSession())
            return;

        ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
    }

    static void AddRP(Player* player, uint32 amount)
    {
        if (!player || !player->GetSession() || amount == 0)
            return;

        uint32 accountId = player->GetSession()->GetAccountId();

        if (accountId == 0)
            return;

        LoginDatabase.PExecute(
            "UPDATE appdata.account_data "
            "SET reg_point = reg_point + {} "
            "WHERE id = {}",
            amount,
            accountId
        );
    }

    static void AddHonor(Player* player, uint32 amount)
    {
        if (!player || amount == 0)
            return;

        player->ModifyHonorPoints(int32(amount));
    }

    static void AddItem(Player* player, uint32 itemEntry, uint32 count)
    {
        if (!player || itemEntry == 0 || count == 0)
            return;

        player->AddItem(itemEntry, count);
    }

    static void ConsumeBox(Player* player, Item* item)
    {
        if (!player || !item)
            return;

        uint32 count = 1;
        player->DestroyItemCount(item, count, true);
    }

    static void GiveNormalBoxReward(Player* player)
    {
        AddItem(player, ITEM_ARENA_TOKEN, NORMAL_BOX_BASE_TOKEN);

        uint32 roll = urand(1, 100);

        if (roll <= 30)
        {
            AddItem(player, ITEM_ARENA_TOKEN, 2);
            Send(player, "시즌 8 전투상자에서 투기장 증표 2개를 추가로 획득했습니다.");
        }
        else if (roll <= 45)
        {
            AddItem(player, ITEM_BATTLE_HEALING_POTION, 5);
            Send(player, "시즌 8 전투상자에서 전장 특급 치유 물약 5개를 획득했습니다.");
        }
        else if (roll <= 60)
        {
            AddItem(player, ITEM_BATTLE_MANA_POTION, 5);
            Send(player, "시즌 8 전투상자에서 전장 특급 마나 물약 5개를 획득했습니다.");
        }
        else if (roll <= 70)
        {
            AddItem(player, ITEM_BATTLE_GUARD_CHARM, 1);
            Send(player, "시즌 8 전투상자에서 전장의 수호 부적 1개를 획득했습니다.");
        }
        else if (roll <= 80)
        {
            AddItem(player, ITEM_ARENA_POINT_TICKET, 1);
            Send(player, "시즌 8 전투상자에서 전장의 아레나 증서 1개를 획득했습니다.");
        }
        else if (roll <= 85)
        {
            AddItem(player, ITEM_HEROIC_RESET_TICKET, 1);
            Send(player, "시즌 8 전투상자에서 영웅 던전 재입장 허가서 1개를 획득했습니다.");
        }
        else if (roll <= 95)
        {
            AddRP(player, 2000);
            Send(player, "시즌 8 전투상자에서 RP 2,000을 획득했습니다.");
        }
        else
        {
            AddHonor(player, 1000);
            Send(player, "시즌 8 전투상자에서 명예 점수 1,000점을 획득했습니다.");
        }

        Send(player, "시즌 8 전투상자 기본 보상으로 투기장 증표 3개를 획득했습니다.");
    }

    static void GiveHighBoxReward(Player* player)
    {
        AddItem(player, ITEM_ARENA_TOKEN, HIGH_BOX_BASE_TOKEN);

        uint32 roll = urand(1, 100);

        if (roll <= 30)
        {
            AddItem(player, ITEM_ARENA_TOKEN, 5);
            Send(player, "시즌 8 고급 전투상자에서 투기장 증표 5개를 추가로 획득했습니다.");
        }
        else if (roll <= 45)
        {
            AddItem(player, ITEM_BATTLE_GUARD_CHARM, 2);
            Send(player, "시즌 8 고급 전투상자에서 전장의 수호 부적 2개를 획득했습니다.");
        }
        else if (roll <= 60)
        {
            AddItem(player, ITEM_ARENA_POINT_TICKET, 2);
            Send(player, "시즌 8 고급 전투상자에서 전장의 아레나 증서 2개를 획득했습니다.");
        }
        else if (roll <= 70)
        {
            AddItem(player, ITEM_HEROIC_RESET_TICKET, 1);
            Send(player, "시즌 8 고급 전투상자에서 영웅 던전 재입장 허가서 1개를 획득했습니다.");
        }
        else if (roll <= 80)
        {
            AddRP(player, 5000);
            Send(player, "시즌 8 고급 전투상자에서 RP 5,000을 획득했습니다.");
        }
        else if (roll <= 90)
        {
            AddHonor(player, 3000);
            Send(player, "시즌 8 고급 전투상자에서 명예 점수 3,000점을 획득했습니다.");
        }
        else if (roll <= 97)
        {
            AddItem(player, ITEM_ARENA_TOKEN, 10);
            Send(player, "시즌 8 고급 전투상자에서 투기장 증표 10개를 추가로 획득했습니다.");
        }
        else
        {
            AddItem(player, ITEM_ARENA_TOKEN, 15);
            Send(player, "시즌 8 고급 전투상자에서 투기장 증표 15개를 추가로 획득했습니다.");
        }

        Send(player, "시즌 8 고급 전투상자 기본 보상으로 투기장 증표 8개를 획득했습니다.");
    }
}

class item_season8_battle_box : public ItemScript
{
public:
    item_season8_battle_box() : ItemScript("item_season8_battle_box") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        if (!player || !item)
            return true;

        if (item->GetEntry() != Season8BattleBoxes::ITEM_SEASON8_BATTLE_BOX)
            return true;

        Season8BattleBoxes::GiveNormalBoxReward(player);
        Season8BattleBoxes::ConsumeBox(player, item);

        return true;
    }
};

class item_season8_high_battle_box : public ItemScript
{
public:
    item_season8_high_battle_box() : ItemScript("item_season8_high_battle_box") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        if (!player || !item)
            return true;

        if (item->GetEntry() != Season8BattleBoxes::ITEM_SEASON8_HIGH_BATTLE_BOX)
            return true;

        Season8BattleBoxes::GiveHighBoxReward(player);
        Season8BattleBoxes::ConsumeBox(player, item);

        return true;
    }
};


//전문기술확장
namespace
{
    static constexpr uint32 ITEM_PROFESSION_BONUS_TOKEN = 1136;
}

class item_profession_bonus_token : public ItemScript
{
public:
    item_profession_bonus_token() : ItemScript("item_profession_bonus_token") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        if (!player || !item)
            return true;

        if (item->GetEntry() != ITEM_PROFESSION_BONUS_TOKEN)
            return true;

        uint32 guid = player->GetGUID().GetCounter();

        QueryResult checkResult = CharacterDatabase.PQuery(
            "SELECT enabled FROM character_profession_bonus WHERE guid = {} LIMIT 1",
            guid
        );

        if (checkResult)
        {
            Field* fields = checkResult->Fetch();
            uint8 enabled = fields[0].GetUInt8();

            if (enabled == 1)
            {
                ChatHandler(player->GetSession()).PSendSysMessage("이미 전문기술 확장권을 사용한 캐릭터입니다.");
                return true;
            }
        }

		CharacterDatabase.PExecute(
            "INSERT INTO character_profession_bonus "
            "(guid, enabled, note) "
            "VALUES ({}, 1, '전문기술 확장권 사용') "
            "ON DUPLICATE KEY UPDATE "
            "enabled = 1, "
            "note = '전문기술 확장권 사용'",
            guid
        );

        player->SetFreePrimaryProfessions(player->GetFreePrimaryProfessionPoints() + 1);

		player->DestroyItemCount(item->GetEntry(), 1, true);

        ChatHandler(player->GetSession()).PSendSysMessage("전문기술 가능 개수가 1 증가했습니다.");
        return true;
    }
};


//잿빛선고단 평판확고
#include "ScriptMgr.h"
#include "Player.h"
#include "Item.h"
#include "Chat.h"
#include "DBCStores.h"
#include "ReputationMgr.h"

static constexpr uint32 ITEM_ASHEN_VERDICT_EXALTED = 1138;
static constexpr uint32 FACTION_ASHEN_VERDICT = 1156;
static constexpr int32 REPUTATION_EXALTED_MAX = 42999;

class item_ashen_verdict_exalted : public ItemScript
{
public:
    item_ashen_verdict_exalted() : ItemScript("item_ashen_verdict_exalted") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        if (!player || !item)
            return false;

        FactionEntry const* factionEntry = sFactionStore.LookupEntry(FACTION_ASHEN_VERDICT);
        if (!factionEntry)
        {
            ChatHandler(player->GetSession()).SendSysMessage("잿빛 선고단 평판 정보를 찾을 수 없습니다.");
            return true;
        }

        int32 currentStanding = player->GetReputationMgr().GetReputation(FACTION_ASHEN_VERDICT);

        if (currentStanding >= REPUTATION_EXALTED_MAX)
        {
            ChatHandler(player->GetSession()).SendSysMessage("이미 잿빛 선고단 평판이 확고한 동맹입니다.");
            return true;
        }

        player->GetReputationMgr().SetReputation(factionEntry, REPUTATION_EXALTED_MAX);

        player->DestroyItemCount(item->GetEntry(), 1, true);

        ChatHandler(player->GetSession()).SendSysMessage("잿빛 선고단 평판이 확고한 동맹이 되었습니다.");

        return true;
    }
};

// custom_booty_bay_transport.cpp

#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "Creature.h"
#include "Player.h"
#include "Unit.h"

namespace
{
    enum BootyBayTransportData : uint32
    {
        ACTION_TO_BOOTY_BAY  = GOSSIP_ACTION_INFO_DEF + 1,
        ACTION_TO_GADGETZAN  = GOSSIP_ACTION_INFO_DEF + 2,
        ACTION_TO_FERALAS    = GOSSIP_ACTION_INFO_DEF + 3,

        MIN_TRANSPORT_LEVEL  = 35,
        MAX_TRANSPORT_LEVEL  = 60
    };

    struct TransportDestination
    {
        uint32 map;
        float x;
        float y;
        float z;
        float o;
    };

    // 무법항 도착지
    static constexpr TransportDestination DEST_BOOTY_BAY =
    {
        0,
        -14296.80f,
        531.30f,
        8.80f,
        3.80f
    };

    // 가젯잔 도착지
    static constexpr TransportDestination DEST_GADGETZAN =
    {
        1,
        -7169.20f,
        -3839.70f,
        8.70f,
        0.75f
    };

    // 페랄라스 - 호드: 모자케 야영지 근처
    static constexpr TransportDestination DEST_FERALAS_HORDE =
    {
        1,
        -4414.516f,
        193.8892f,
        25.660864f,
        1.416485f
    };

    // 페랄라스 - 얼라이언스: 페더문 요새 근처
    static constexpr TransportDestination DEST_FERALAS_ALLIANCE =
    {
        1,
        -4370.10f,
        3288.30f,
        13.90f,
        4.65f
    };

    enum TransportNpcPlace : uint8
	{
		PLACE_BOOTY_BAY = 0,
		PLACE_GADGETZAN = 1,
		PLACE_FERALAS_HORDE = 2,
		PLACE_FERALAS_ALLIANCE = 3,
		PLACE_UNKNOWN = 4
	};

	enum TransportDisplayId : uint32
	{
		DISPLAY_TRANSPORT_GOBLIN = 11376, // 무법항 / 가젯잔
		DISPLAY_TRANSPORT_TAUREN = 7248, // 모자케
		DISPLAY_TRANSPORT_HUMAN  = 7022  // 페더문
	};

    static TransportNpcPlace GetTransportNpcPlace(Creature* creature)
	{
		if (!creature)
			return PLACE_UNKNOWN;

		uint32 mapId = creature->GetMapId();
		float x = creature->GetPositionX();
		float y = creature->GetPositionY();

		// 무법항 근처
		if (mapId == 0)
		{
			if (x < -14000.0f && y > 300.0f && y < 900.0f)
				return PLACE_BOOTY_BAY;
		}

		// 칼림도어
		if (mapId == 1)
		{
			// 가젯잔 근처
			if (x < -6900.0f && x > -7500.0f && y < -3500.0f && y > -4200.0f)
				return PLACE_GADGETZAN;

			// 모자케 야영지 근처
			if (x < -4100.0f && x > -4700.0f && y > -100.0f && y < 600.0f)
				return PLACE_FERALAS_HORDE;

			// 페더문 요새 근처
			if (x < -4100.0f && x > -4700.0f && y > 2800.0f && y < 3600.0f)
				return PLACE_FERALAS_ALLIANCE;
		}

		return PLACE_UNKNOWN;
	}

	static uint32 GetTransportDisplayIdByPlace(TransportNpcPlace place)
	{
		switch (place)
		{
			case PLACE_BOOTY_BAY:
			case PLACE_GADGETZAN:
				return DISPLAY_TRANSPORT_GOBLIN;

			case PLACE_FERALAS_HORDE:
				return DISPLAY_TRANSPORT_TAUREN;

			case PLACE_FERALAS_ALLIANCE:
				return DISPLAY_TRANSPORT_HUMAN;

			default:
				return DISPLAY_TRANSPORT_GOBLIN;
		}
	}

	static void ApplyTransportDisplay(Creature* creature)
	{
		if (!creature)
			return;

		TransportNpcPlace place = GetTransportNpcPlace(creature);
		uint32 displayId = GetTransportDisplayIdByPlace(place);

		creature->SetDisplayId(displayId);
		creature->SetNativeDisplayId(displayId);
	}

    static bool CanUseBootyBayTransport(Player* player)
    {
        if (!player)
            return false;

        if (!player->IsAlive())
        {
            player->GetSession()->SendAreaTriggerMessage("죽은 상태에서는 이용할 수 없습니다.");
            return false;
        }

        if (player->IsInCombat())
        {
            player->GetSession()->SendAreaTriggerMessage("전투 중에는 이용할 수 없습니다.");
            return false;
        }

        uint8 level = player->GetLevel();

        if (level < MIN_TRANSPORT_LEVEL || level > MAX_TRANSPORT_LEVEL)
        {
            player->GetSession()->SendAreaTriggerMessage("이 수송은 35~50레벨만 이용할 수 있습니다.");
            return false;
        }

        return true;
    }

    static void SendTransport(Player* player, TransportDestination const& dest)
    {
        if (!player)
            return;

        player->TeleportTo(dest.map, dest.x, dest.y, dest.z, dest.o);
    }

    static void SendFeralas(Player* player)
    {
        if (!player)
            return;

        if (player->GetTeamId() == TEAM_ALLIANCE)
            SendTransport(player, DEST_FERALAS_ALLIANCE);
        else
            SendTransport(player, DEST_FERALAS_HORDE);
    }
}

struct npc_booty_bay_transport : public ScriptedAI
{
    npc_booty_bay_transport(Creature* creature) : ScriptedAI(creature) { }

	void Reset() override
	{
		ApplyTransportDisplay(me);
	}

    bool OnGossipHello(Player* player) override
    {
        ClearGossipMenuFor(player);

        TransportNpcPlace place = GetTransportNpcPlace(me);

        switch (place)
        {
            case PLACE_BOOTY_BAY:
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "가젯잔으로 이동한다", GOSSIP_SENDER_MAIN, ACTION_TO_GADGETZAN);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "페랄라스로 이동한다", GOSSIP_SENDER_MAIN, ACTION_TO_FERALAS);
                break;

            case PLACE_GADGETZAN:
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "무법항으로 이동한다", GOSSIP_SENDER_MAIN, ACTION_TO_BOOTY_BAY);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "페랄라스로 이동한다", GOSSIP_SENDER_MAIN, ACTION_TO_FERALAS);
                break;

            case PLACE_FERALAS_HORDE:
			case PLACE_FERALAS_ALLIANCE:
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "무법항으로 이동한다", GOSSIP_SENDER_MAIN, ACTION_TO_BOOTY_BAY);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "가젯잔으로 이동한다", GOSSIP_SENDER_MAIN, ACTION_TO_GADGETZAN);
                break;

            default:
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "무법항으로 이동한다", GOSSIP_SENDER_MAIN, ACTION_TO_BOOTY_BAY);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "가젯잔으로 이동한다", GOSSIP_SENDER_MAIN, ACTION_TO_GADGETZAN);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "페랄라스로 이동한다", GOSSIP_SENDER_MAIN, ACTION_TO_FERALAS);
                break;
        }

        SendGossipMenuFor(player, 1, me->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

        CloseGossipMenuFor(player);

        if (!CanUseBootyBayTransport(player))
            return true;

        switch (action)
        {
            case ACTION_TO_BOOTY_BAY:
                SendTransport(player, DEST_BOOTY_BAY);
                break;

            case ACTION_TO_GADGETZAN:
                SendTransport(player, DEST_GADGETZAN);
                break;

            case ACTION_TO_FERALAS:
                SendFeralas(player);
                break;

            default:
                break;
        }

        return true;
    }
};


//레벨 골드 보상
#include "ScriptMgr.h"
#include "Player.h"
#include "Mail.h"
#include "DatabaseEnv.h"

#include <array>
#include <string>

namespace
{
    constexpr uint32 GOLD_TO_COPPER = 10000;

    struct LevelGoldReward
    {
        uint8 Level;
        uint32 Gold;
    };

    constexpr std::array<LevelGoldReward, 8> LEVEL_GOLD_REWARDS =
    {{
        {10, 500},
        {20, 700},
        {30, 800},
        {40, 1000},
        {50, 1200},
        {60, 1800},
        {65, 1500},
        {70, 1500}
    }};

    void SendGoldRewardMail(Player* player, uint8 level, uint32 gold)
    {
        if (!player || gold == 0)
            return;

        std::string subject = "레벨 달성 지원금";
        std::string body =
            "축하합니다!\n\n"
            + std::to_string(level)
            + "레벨 달성을 기념하여 서버 정착 지원금 "
            + std::to_string(gold)
            + "골드를 지급합니다.\n\n"
            "탈것, 숙련, 수리비, 소모품 준비에 사용해 주세요.";

        uint32 money = gold * GOLD_TO_COPPER;

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        MailDraft draft(subject, body);
        draft.AddMoney(money);

        draft.SendMailTo(
            trans,
            MailReceiver(player, player->GetGUID().GetCounter()),
            MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM)
        );

        CharacterDatabase.CommitTransaction(trans);

        player->GetSession()->SendAreaTriggerMessage("레벨 달성 지원금이 우편으로 지급되었습니다.");
    }

    void CheckLevelGoldReward(Player* player, uint8 oldLevel, uint8 currentLevel)
    {
        if (!player)
            return;

        if (currentLevel <= oldLevel)
            return;

        for (LevelGoldReward const& reward : LEVEL_GOLD_REWARDS)
        {
            if (oldLevel < reward.Level && currentLevel >= reward.Level)
                SendGoldRewardMail(player, reward.Level, reward.Gold);
        }
    }
}

class custom_level_gold_reward : public PlayerScript
{
public:
    custom_level_gold_reward() : PlayerScript("custom_level_gold_reward") { }

    void OnLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (!player)
            return;

        CheckLevelGoldReward(player, oldLevel, player->GetLevel());
    }
};


//채널초대
static constexpr uint32 LIGHT_GUARDIAN_INVITE_DELAY_MS = 5000;
static std::string const LIGHT_GUARDIAN_CHANNEL_NAME = "빛의수호자";

static bool IsAlreadyInLightGuardianChannel(Player* player)
{
    if (!player || !player->GetSession())
        return false;

    Channel* channel = ChannelMgr::GetChannelForPlayerByNamePart(LIGHT_GUARDIAN_CHANNEL_NAME, player);
    if (!channel)
        return false;

    return channel->GetName(player->GetSession()->GetSessionDbcLocale()) == LIGHT_GUARDIAN_CHANNEL_NAME;
}

static void SendLightGuardianInvitePopup(Player* player)
{
    if (!player || !player->GetSession())
        return;

    // 이미 빛의수호자 채널에 들어가 있으면 초대창 보내지 않음
    if (IsAlreadyInLightGuardianChannel(player))
        return;

    WorldPacket data(SMSG_CHANNEL_NOTIFY, 80);

    data << uint8(CHAT_INVITE_NOTICE);
    data << LIGHT_GUARDIAN_CHANNEL_NAME;
    data << player->GetGUID();

    player->GetSession()->SendPacket(&data);
}

class LightGuardianInviteEvent : public BasicEvent
{
public:
    explicit LightGuardianInviteEvent(ObjectGuid playerGuid)
        : _playerGuid(playerGuid)
    {
    }

    bool Execute(uint64 /*time*/, uint32 /*diff*/) override
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(_playerGuid);
        if (!player)
            return true;

        SendLightGuardianInvitePopup(player);
        return true;
    }

private:
    ObjectGuid _playerGuid;
};

class light_guardian_channel_invite_player : public PlayerScript
{
public:
    light_guardian_channel_invite_player()
        : PlayerScript("light_guardian_channel_invite_player")
    {
    }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player)
            return;

        player->m_Events.AddEvent(
            new LightGuardianInviteEvent(player->GetGUID()),
            player->m_Events.CalculateTime(Milliseconds(LIGHT_GUARDIAN_INVITE_DELAY_MS))
        );
    }
};

//감사장 랜덤추가
namespace
{
    constexpr uint32 ITEM_THANK_YOU_LETTER = 45924;

    // 영웅 던전만 적용
    constexpr bool ONLY_HEROIC_DUNGEON = true;

    bool IsThankYouWeekend()
    {
        std::time_t now = std::time(nullptr);

        std::tm localTime;
#ifdef _WIN32
        localtime_s(&localTime, &now);
#else
        localtime_r(&now, &localTime);
#endif

        // 일요일 0, 금요일 5, 토요일 6
        return localTime.tm_wday == 0 || localTime.tm_wday == 5 || localTime.tm_wday == 6;
    }

    bool IsThankYouDungeon(Player* player)
    {
        if (!player)
            return false;

        Map* map = player->GetMap();
        if (!map)
            return false;

        if (!map->IsDungeon())
            return false;

        if (map->IsRaid())
            return false;

        if (ONLY_HEROIC_DUNGEON && player->GetDungeonDifficulty() != DUNGEON_DIFFICULTY_HEROIC)
            return false;

        return true;
    }

    uint32 GetSameInstanceGroupMemberCount(Player* player)
    {
        if (!player)
            return 0;

        Map* map = player->GetMap();
        if (!map)
            return 0;

        Group* group = player->GetGroup();
        if (!group)
            return 1;

        uint32 count = 0;

        Map::PlayerList const& players = map->GetPlayers();
        for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        {
            Player* member = itr->GetSource();
            if (!member)
                continue;

            if (member->GetGroup() != group)
                continue;

            ++count;
        }

        return count;
    }

    uint32 GetThankYouFinalCount(Player* player, uint32 lootedCount)
    {
        uint32 finalCount = lootedCount;

        uint32 memberCount = GetSameInstanceGroupMemberCount(player);

        if (memberCount >= 5)
            finalCount = urand(1, 5);
        else
            finalCount = 1;

        if (IsThankYouWeekend())
            finalCount *= 2;

        return finalCount;
    }
}




struct ThankYouOutdoorPvpBuffInfo
{
    uint32 AllianceBuff;
    uint32 HordeBuff;
};

static constexpr uint32 THANK_YOU_OUTDOOR_PVP_BUFF_REQUIRED_COUNT = 3;
static constexpr uint32 THANK_YOU_OUTDOOR_PVP_BUFF_MULTIPLIER = 2;

static constexpr std::array<ThankYouOutdoorPvpBuffInfo, 6> THANK_YOU_OUTDOOR_PVP_BUFFS =
{{
    // 지옥불 반도
    { 32071, 32049 },

    // 실리더스
    { 29534, 29534 },

    // 나그란드
    { 33795, 33795 },

    // 장가르 습지대
    { 0, 0 },

    // 테로카르 숲
    { 0, 0 },

    // 동부 역병지대
    { 0, 0 } 
}};

static uint32 GetThankYouOutdoorPvpBuffCount(Player* player)
{
    if (!player)
        return 0;

    uint32 buffCount = 0;

    for (ThankYouOutdoorPvpBuffInfo const& outdoorBuff : THANK_YOU_OUTDOOR_PVP_BUFFS)
    {
        if (outdoorBuff.AllianceBuff == 0 && outdoorBuff.HordeBuff == 0)
            continue;

        bool hasBuff = false;

        if (player->GetTeam() == ALLIANCE)
        {
            if (outdoorBuff.AllianceBuff != 0 && player->HasAura(outdoorBuff.AllianceBuff))
                hasBuff = true;
        }
        else if (player->GetTeam() == HORDE)
        {
            if (outdoorBuff.HordeBuff != 0 && player->HasAura(outdoorBuff.HordeBuff))
                hasBuff = true;
        }

        if (hasBuff)
            ++buffCount;
    }

    return buffCount;
}

class custom_thank_you_letter_loot : public PlayerScript
{
public:
    custom_thank_you_letter_loot() : PlayerScript("custom_thank_you_letter_loot") { }

    void OnLootItem(Player* looter, ObjectGuid const& /*lootGuid*/, uint32 itemId, uint32 count) override
    {
        if (!looter)
            return;

        if (itemId != ITEM_THANK_YOU_LETTER)
            return;

        if (!IsThankYouDungeon(looter))
            return;

        if (count == 0)
            return;

        uint32 finalCount = GetThankYouFinalCount(looter, count);

		uint32 outdoorPvpBuffCount = GetThankYouOutdoorPvpBuffCount(looter);
        bool outdoorPvpBuffBonus = outdoorPvpBuffCount >= THANK_YOU_OUTDOOR_PVP_BUFF_REQUIRED_COUNT;

        if (outdoorPvpBuffBonus)
            finalCount *= THANK_YOU_OUTDOOR_PVP_BUFF_MULTIPLIER;

        // 이미 기본 루팅으로 받은 수량보다 최종 수량이 작거나 같으면 추가 지급 없음
        if (finalCount <= count)
            return;

        uint32 bonusCount = finalCount - count;

        if (!looter->AddItem(ITEM_THANK_YOU_LETTER, bonusCount))
        {
            ChatHandler(looter->GetSession()).SendSysMessage("|cffff0000[감사장 보너스]|r 가방 공간이 부족하여 추가 감사장을 받을 수 없습니다.");
            return;
        }
    }
};

#include "ScriptMgr.h"
#include "Chat.h"
#include "Item.h"
#include "Map.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "Unit.h"

namespace CustomTrialOfTheCrusaderSilenceScroll
{
    static constexpr uint32 MAP_TRIAL_OF_THE_CRUSADER = 649;
    static constexpr uint32 SPELL_SCROLL_SILENCE_AURA = 65542;
    static constexpr uint32 MAX_TRY_PLAYER_COUNT = 7;
    static constexpr int32 SILENCE_DURATION_MS = 6000;
    static constexpr float MAX_USE_DISTANCE = 40.0f;

    static uint32 GetCurrentRaidMemberCount(Player* player)
    {
        if (!player)
            return 0;

        Group* group = player->GetGroup();

        if (!group)
            return 1;

        return group->GetMembersCount();
    }
}

class item_trial_of_the_crusader_silence_scroll : public ItemScript
{
public:
    item_trial_of_the_crusader_silence_scroll() : ItemScript("item_trial_of_the_crusader_silence_scroll") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& targets) override
    {
        using namespace CustomTrialOfTheCrusaderSilenceScroll;

        if (!player || !item)
            return true;

        if (player->GetMapId() != MAP_TRIAL_OF_THE_CRUSADER)
        {
            ChatHandler(player->GetSession()).SendSysMessage("이 두루마리는 십자군의 시험장에서만 사용할 수 있습니다.");
            return true;
        }

        if (!player->IsInCombat())
        {
            ChatHandler(player->GetSession()).SendSysMessage("전투 중에만 사용할 수 있습니다.");
            return true;
        }

        uint32 tryPlayerCount = GetCurrentRaidMemberCount(player);

        if (tryPlayerCount > MAX_TRY_PLAYER_COUNT)
        {
            ChatHandler(player->GetSession()).SendSysMessage("트라이 인원이 7명을 초과하면 사용할 수 없습니다.");
            return true;
        }

        Unit* target = targets.GetUnitTarget();

        if (!target)
            target = player->GetSelectedUnit();

        if (!target)
        {
            ChatHandler(player->GetSession()).SendSysMessage("대상을 선택해야 합니다.");
            return true;
        }

        if (!target->ToCreature())
        {
            ChatHandler(player->GetSession()).SendSysMessage("NPC 대상에게만 사용할 수 있습니다.");
            return true;
        }

        if (!target->IsAlive())
        {
            ChatHandler(player->GetSession()).SendSysMessage("죽은 대상에게는 사용할 수 없습니다.");
            return true;
        }

        if (!target->IsInCombat())
        {
            ChatHandler(player->GetSession()).SendSysMessage("전투 중인 대상에게만 사용할 수 있습니다.");
            return true;
        }

        if (!player->IsWithinDistInMap(target, MAX_USE_DISTANCE))
        {
            ChatHandler(player->GetSession()).SendSysMessage("대상이 너무 멀리 있습니다.");
            return true;
        }

        target->InterruptNonMeleeSpells(false);

        Aura* aura = target->AddAura(SPELL_SCROLL_SILENCE_AURA, target);

        if (!aura)
        {
            ChatHandler(player->GetSession()).SendSysMessage("대상에게 침묵 효과를 적용하지 못했습니다.");
            return true;
        }

        aura->SetMaxDuration(SILENCE_DURATION_MS);
        aura->SetDuration(SILENCE_DURATION_MS);

        uint32 destroyCount = 1;
        player->DestroyItemCount(item, destroyCount, true);

        return true;
    }
};







/* ============================================================
 * 스크립트 등록
 * ============================================================ */
void AddSC_custom_scripts()
{
    new boost_player();
    new item_portable_bankmail_onuse();
	new item_mail();
	new item_buff();

	//80레벨 부스터
	new item_level80_boost_dps();

	//로그인카운트
	new account_daily_ingame();

	// 40레벨 미만 1개월 / 전체 레벨 6개월 이상 미접속 캐릭터
    new inactive_character_archive();
    new inactive_character_lock_player();

	//솔로공격대
	new npc_solo_raid_creator();

	//아이템파괴 로그
	new ItemDestroyLogScript();

	//인던 트라이
	new go_time_attack_end();

	//로또
	new LottoDrawScript();
	new LottoPlayerScript();

	//랜덤 박스
	new item_random_box();
	new item_mount_random_box();

	//접속시 뉴스
	new custom_server_notice();

	//gm채팅
	new ChatGMOnly();

	//icc 버프
	new item_icc_faction_buff();

	//팀 공식체널
	new faction_channel_chat_log();
	new guild_login_chat_insert();
	new guild_join_chat_insert();

	//낡은 훈장
	new item_old_battle_mark_deposit();
	new item_battle_guard_charm();
    new item_small_arena_point_ticket();
    new item_heroic_dungeon_reset_ticket();

	//투기장 보상상자
	new item_season8_battle_box();
    new item_season8_high_battle_box();

	//전문기술확장
	new item_profession_bonus_token();

	//잿빛선고단 평팜
	new item_ashen_verdict_exalted();

	//레벨업 텔레보터
	RegisterCreatureAI(npc_booty_bay_transport);

	//레벨업보상골드
	new custom_level_gold_reward();

	//채널초대
	new light_guardian_channel_invite_player();

	//감사장추가지급
	new custom_thank_you_letter_loot();

	//침묵시전
	new item_trial_of_the_crusader_silence_scroll();

}
