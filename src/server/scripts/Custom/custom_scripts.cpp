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

        if (notice && *notice)
		{
			std::string msg = Trinity::StringFormat("첫 접속 자동길드초대입니다. 다음접속부터 자동초대 되지 않습니다.");
			ChatHandler(player->GetSession()).PSendSysMessage("%s", msg.c_str());
		}
    }

//채널 초대
static void SendChannelInvitePopup(Player* player, char const* notice)
{
    if (!player || !player->GetSession())
        return;

    static std::string const channelName = "빛의수호자";

    WorldPacket data(SMSG_CHANNEL_NOTIFY, 50 + channelName.size() + player->GetName().size());

    // 현재 코어에 있는 초대 notice 상수로 교체
    // 예: CHAT_INVITE_NOTICE 또는 CHAT_PLAYER_INVITED_NOTICE
    data << uint8(CHAT_INVITE_NOTICE);
    data << channelName;
    data << player->GetName();

    player->GetSession()->SendPacket(&data);

    if (notice && *notice)
        ChatHandler(player->GetSession()).SendSysMessage(notice);
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

		if (!player->IsGameMaster())
			if (player->GetGuildId() == 0)
				{
					uint32 gid = GetTargetGuildId(player);
					SendGuildInvitePopup(
						player,
						gid,
						" 자동길드초대입니다."
					);
				}

		SendChannelInvitePopup(
    player,
    "빛의수호자 채널 초대 팝업을 보냈습니다."
);
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

        if (!player /*|| player->IsGameMaster()*/)
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
		if (player->GetLevel() >= 64 || !player->IsAlive() || mEntry->IsBattlegroundOrArena() || player->IsInCombat() || player->HasInvisibilityAura() || player->HasStealthAura() || player->InBattleground() || player->InArena() || zone == 4197)
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


//암시장
// 암시장 (RP 기반, 단일 입찰로그 테이블 관리)

// ===== Config / Consts =====
static constexpr uint32 MIN_START_RP             = 10000;   // 최초 입찰 최소 RP
static constexpr uint32 MIN_INCREMENT_RP         = 100;    // 최소 인상 RP
static constexpr uint32 EXTEND_THRESHOLD_SEC     = 300;    // 종료 5분 이내
static constexpr uint32 EXTEND_ADD_SEC           = 600;    // 10분 연장
static constexpr uint8  EXTEND_MAX_COUNT         = 3;
static constexpr uint32 AUCTION_VISIBLE          = 12;
static constexpr uint32 SPAWN_DURATION_SEC       = 5400;
static constexpr uint32 BLACKMARKET_NPC_ENTRY    = 5000064;
static constexpr uint32 BM_BID_FEE_GOLD          = 100;    // 입찰 수수료(고정 100골드)
static constexpr uint64 BM_BID_FEE_COPPER        = uint64(BM_BID_FEE_GOLD) * GOLD;
static constexpr uint32 BM_RP_REFUND_ITEM_ENTRY  = 823; // "RP 환급 토큰" 템플릿(직접 DB에 생성)

// 앱 아이템 샵 카테고리 ID (하드코딩)
static constexpr std::array<uint32, 3> BM_CATEGORY_IDS = {
    7 // 실제 category_id 에 맞게 수정
};

// ===== Globals =====
static ObjectGuid s_BlackMarketNpcGuid;
static uint32     s_BlackMarketNpcMapId = 0;

// ===== 유틸 =====
static std::string EscapeShell(std::string s)
{
    for (char& c : s)
        if (c == '\'' || c == '\"') c = ' ';
    return s;
}
static void WritePartyChatToFile(uint32, std::string const&, uint32, uint32, std::string const&) { /* no-op */ }

// ============================================================================
//                       NPC: Auctioneer (Gossip & Bidding)
// ============================================================================
class npc_black_market_auctioneer : public CreatureScript
{
public:
    npc_black_market_auctioneer() : CreatureScript("npc_black_market_auctioneer") {}

    struct npc_black_market_auctioneerAI : public ScriptedAI
    {
        using GuidKey = uint64;
        std::unordered_map<GuidKey, uint32> g_AuctionSelections;

        // ★ 추가: 액션 상수/환율 상수
        static constexpr uint32 ACTION_BID_INPUT        = 999;        // 기존 입찰 코드 입력
        static constexpr uint32 ACTION_EXCHANGE_MENU    = 900000001;  // 골드→RP 환전 메뉴
        static constexpr uint32 ACTION_EXCHANGE_INPUT   = 900000002;  // 환전 수량 입력
        static constexpr uint32 GOLD_PER_RP             = 10000;       // 1 RP 당 5000골드
        static constexpr uint32 COPPER_PER_GOLD         = 10000;      // 1골드 = 10000코퍼

        npc_black_market_auctioneerAI(Creature* c) : ScriptedAI(c) {}

        static void SendEndTimeWhisper(Player* player, time_t endTime)
        {
            if (!player) return;
            tm* ti = localtime(&endTime);
            if (!ti) return;

            std::ostringstream timeStr;
            timeStr << (ti->tm_year + 1900) << "년 "
                    << (ti->tm_mon + 1)    << "월 "
                    << ti->tm_mday        << "일 "
                    << ti->tm_hour        << "시 "
                    << ti->tm_min         << "분";

            std::ostringstream msg;
            msg << "|cff00ff00[암시장알림]|r " << timeStr.str() << " 종료 예정입니다.";
            ChatHandler(player->GetSession()).SendSysMessage(msg.str().c_str());
        }

        bool OnGossipHello(Player* player) override
        {
            if (!player)
                return false;

            ClearGossipMenuFor(player);

            if (player->GetLevel() != 80)
            {
                player->GetSession()->SendNotification("80레벨만 입찰할 수 있습니다.");
                return true;
            }

            // active 경매 리스트 (current_bid: RP 기준)
            {
                std::string sql = Trinity::StringFormat(
                    "SELECT a.auction_id, a.item_id, i.name, i.ItemLevel, "
                    "       a.current_bid, IFNULL(ci.iconname,''), a.end_time, a.bid_count "
                    "FROM blackmarket_auction a "
                    "JOIN item_template i ON a.item_id = i.entry "
                    "LEFT JOIN appdata.data_wotlk_itemdisplayinfo ci ON i.displayid = ci.id "
                    "WHERE a.auction_status='active' "
                    "ORDER BY a.auction_id ASC LIMIT {}",
                    AUCTION_VISIBLE
                );

                QueryResult result = WorldDatabase.Query(sql.c_str());
                if (!result)
                {
                    player->GetSession()->SendNotification("현재 진행 중인 경매가 없습니다.");
                }
                else
                {
                    // 첫 행 종료 예정 시간 안내
                    {
                        Field* f = result->Fetch();
                        time_t endTime = time_t(f[6].GetUInt32());
                        SendEndTimeWhisper(player, endTime);
                    }

                    // 목록
                    do
                    {
                        Field* f = result->Fetch();
                        uint32 auctionId    = f[0].GetUInt32();
                        uint32 itemID       = f[1].GetUInt32();
                        std::string name    = f[2].GetString();
                        uint32 itemLevel    = f[3].GetUInt32();
                        uint64 currentBidRP = f[4].GetUInt64();
                        std::string icon    = f[5].GetString();
                        uint32 timeLeft     = f[7].GetUInt32();

                        std::ostringstream label;
                        label << "|TInterface\\Icons\\" << icon << ":30:30:-18:0|t "
                              << "[경매ID " << auctionId << "] [아이템ID " << itemID << "]\n"
                              << name << " (Lv" << itemLevel << ")\n현재 |cffff0000"
                              << currentBidRP << "|r RP, 입찰자 " << timeLeft;

                        AddGossipItemFor(player, GOSSIP_ICON_DOT, label.str(), GOSSIP_SENDER_MAIN, auctionId);
                    } while (result->NextRow());
                }
            }

            // ★ 추가: 아이템 리스트 밑에 고정 환전 메뉴
            {
                std::ostringstream exLabel;
                exLabel << "|TInterface\\Icons\\INV_Misc_Coin_01:30:30:-18:0|t "
                        << "골드를 RP로 환전하기 (1 RP = " << GOLD_PER_RP << "골드)";
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, exLabel.str(), GOSSIP_SENDER_MAIN, ACTION_EXCHANGE_MENU);
            }

            AddGossipItemFor(player, GOSSIP_ICON_DOT, "닫기", GOSSIP_SENDER_MAIN, 0);

            player->TalkedToCreature(me->GetEntry(), me->GetGUID());
            SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            if (!player) return false;

            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            if (action == 0)
                return OnGossipHello(player);

            // ★ 추가: 골드→RP 환전 메뉴 선택
            if (action == ACTION_EXCHANGE_MENU)
            {
                std::ostringstream label;
                label << "환전할 RP 수량을 입력해주세요.\n"
                      << "1 RP당 " << GOLD_PER_RP << " 골드가 차감됩니다.\n"
                      << "(예: 10 입력 시 " << (GOLD_PER_RP * 10) << "골드 차감)";

                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, label.str(),
                                 GOSSIP_SENDER_MAIN, ACTION_EXCHANGE_INPUT, "", 0, true);
                AddGossipItemFor(player, GOSSIP_ICON_DOT, "뒤로가기", GOSSIP_SENDER_MAIN, 0);
                SendGossipMenuFor(player, 1, me->GetGUID());
                return true;
            }

            // ★ 기존: action = auctionId → 입찰
            g_AuctionSelections[player->GetGUID().GetRawValue()] = action;

            std::ostringstream label;
            label << "[ID:" << action << "] 입찰 RP를 입력해주세요.\n"
                  << "시작금액은 " << MIN_START_RP << " RP 입니다.\n"
                  << "입찰 시 수수료로 골드 100이 차감됩니다.";

            AddGossipItemFor(player, GOSSIP_ICON_DOT, label.str(), GOSSIP_SENDER_MAIN, ACTION_BID_INPUT, "", 0, true);
            AddGossipItemFor(player, GOSSIP_ICON_DOT, "뒤로가기", GOSSIP_SENDER_MAIN, 0);
            SendGossipMenuFor(player, 1, me->GetGUID());
            return true;
        }

        bool OnGossipSelectCode(Player* player, uint32 /*menuId*/, uint32 gossipListId, char const* args) override
        {
            if (!player)
                return false;

            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            // ★ 분기: 입찰 코드 입력 vs 환전 코드 입력
            if (action == ACTION_BID_INPUT)
            {
                // ───────────────────────
                // 기존 입찰 처리 로직
                // ───────────────────────
                GuidKey key = player->GetGUID().GetRawValue();
                if (!g_AuctionSelections.count(key))
                {
                    player->GetSession()->SendNotification("세션이 만료되었거나 잘못된 접근입니다.");
                    return true;
                }

                // 0) 입력 RP 파싱
                uint64 bidRP = uint64(std::max<long long>(atoll(args), 0));
                if (bidRP < MIN_START_RP)
                {
                    player->GetSession()->SendNotification("최소 10000 RP 이상만 입찰 가능합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                uint32 auctionId = g_AuctionSelections[key];
                uint32 accountId = player->GetSession()->GetAccountId();
                uint32 guidLo    = player->GetGUID().GetCounter();

                // 1) 기존 내 입찰 RP 조회
                uint32 oldBidRP = 0;
                {
                    std::string qOld = Trinity::StringFormat(
                        "SELECT bid_rp FROM appdata.blackmarket_rp_bids "
                        "WHERE auction_id={} AND account_id={}",
                        auctionId, accountId
                    );
                    if (QueryResult rOld = CharacterDatabase.Query(qOld.c_str()))
                        oldBidRP = rOld->Fetch()[0].GetUInt32();
                }

                if (bidRP <= oldBidRP)
                {
                    player->GetSession()->SendNotification("이전 입찰 RP보다 더 높게 입력해야 합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                uint32 needRP = bidRP - oldBidRP;

                // 2) 계정 RP 보유량 확인
                {
                    std::string qRP = Trinity::StringFormat(
                        "SELECT reg_point FROM appdata.account_data WHERE id={}",
                        accountId
                    );
                    if (QueryResult rRP = CharacterDatabase.Query(qRP.c_str()))
                    {
                        uint32 curRP = rRP->Fetch()[0].GetUInt32();
                        if (curRP < needRP)
                        {
                            player->GetSession()->SendNotification("보유 RP가 부족합니다.");
                            CloseGossipMenuFor(player);
                            return true;
                        }
                    }
                    else
                    {
                        player->GetSession()->SendNotification("RP 정보를 조회할 수 없습니다.");
                        CloseGossipMenuFor(player);
                        return true;
                    }
                }

                // 3) 골드 수수료(100골드) 확인
                if (player->GetMoney() < BM_BID_FEE_COPPER)
                {
                    player->GetSession()->SendNotification("입찰 수수료 100골드가 부족합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                // 4) 경매 현재 상태 읽기 (검증만)
                uint64 curBid   = 0;
                uint64 startRP  = MIN_START_RP;
                uint64 minIncRP = MIN_INCREMENT_RP;

                {
                    std::string sqlInfo = Trinity::StringFormat(
                        "SELECT current_bid, start_price, IFNULL(min_increment,0) "
                        "FROM blackmarket_auction "
                        "WHERE auction_id={} AND auction_status='active'",
                        auctionId
                    );
                    QueryResult rInfo = WorldDatabase.Query(sqlInfo.c_str());
                    if (!rInfo)
                    {
                        player->GetSession()->SendNotification("유효하지 않은 경매입니다.");
                        return true;
                    }

                    Field* fi = rInfo->Fetch();
                    curBid    = fi[0].GetUInt64();
                    startRP   = fi[1].GetUInt64();
                    minIncRP  = fi[2].GetUInt64();

                    if (startRP == 0)
                        startRP = MIN_START_RP;
                    if (minIncRP == 0)
                        minIncRP = MIN_INCREMENT_RP;
                }

                if (bidRP < startRP)
                {
                    player->GetSession()->SendNotification("시작 RP 이상만 입찰 가능합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }
                if (bidRP <= curBid)
                {
                    player->GetSession()->SendNotification("현재 입찰 RP보다 높아야 합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }
                if (bidRP < curBid + minIncRP)
                {
                    player->GetSession()->SendNotification("현재가보다 최소 100 RP 이상 입찰하셔야 합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                // 5) appdata 쪽 확정
                uint32 nowTs = uint32(GameTime::GetGameTime());

                // 5-1) RP 차감
                {
                    std::string updRP = Trinity::StringFormat(
                        "UPDATE appdata.account_data SET reg_point = reg_point - {} WHERE id={}",
                        needRP, accountId
                    );
                    CharacterDatabase.Execute(updRP.c_str());
                }

                // 5-2) 입찰 로그 upsert
                {
                    std::string upsert = Trinity::StringFormat(
                        "INSERT INTO appdata.blackmarket_rp_bids "
                        "(auction_id, account_id, character_guid, bid_rp, created_at, updated_at, status, token_item_guid) "
                        "VALUES ({}, {}, {}, {}, {}, {}, 0, 0) "
                        "ON DUPLICATE KEY UPDATE "
                        "  bid_rp=VALUES(bid_rp), "
                        "  character_guid=VALUES(character_guid), "
                        "  updated_at=VALUES(updated_at)",
                        auctionId, accountId, guidLo,
                        static_cast<uint32>(bidRP),
                        nowTs, nowTs
                    );
                    CharacterDatabase.Execute(upsert.c_str());
                }

                // 5-3) 입찰 수수료 100골드 차감
                player->ModifyMoney(-int32(BM_BID_FEE_COPPER));

                // 6) world.blackmarket_auction 갱신
                {
                    std::string sqlUpd = Trinity::StringFormat(
                        "UPDATE blackmarket_auction "
                        "SET current_bid={}, bidder_guid={}, bid_count=IFNULL(bid_count,0)+1, "
                        "    end_time=CASE WHEN (end_time-UNIX_TIMESTAMP())<={} AND IFNULL(extend_count,0)<{} "
                        "                  THEN end_time+{} ELSE end_time END, "
                        "    extend_count=CASE WHEN (end_time-UNIX_TIMESTAMP())<={} AND IFNULL(extend_count,0)<{} "
                        "                       THEN IFNULL(extend_count,0)+1 ELSE IFNULL(extend_count,0) END "
                        "WHERE auction_id={} AND auction_status='active'",
                        static_cast<uint64_t>(bidRP), guidLo,
                        EXTEND_THRESHOLD_SEC, EXTEND_MAX_COUNT, EXTEND_ADD_SEC,
                        EXTEND_THRESHOLD_SEC, EXTEND_MAX_COUNT,
                        auctionId
                    );
                    WorldDatabase.Execute(sqlUpd.c_str());
                }

                ChatHandler(player->GetSession()).SendSysMessage("입찰 수수료 100골드가 지불되었습니다.");
                std::string announce = Trinity::StringFormat("|cffABD473[암시장알림]|r {} RP 입찰!", bidRP);
                sWorld->SendServerMessage(SERVER_MSG_STRING, announce.c_str(), 0);

                CloseGossipMenuFor(player);
                return true;
            }
            else if (action == ACTION_EXCHANGE_INPUT)
            {
                // ───────────────────────
                // 골드 → RP 환전 처리
                // ───────────────────────
                uint32 accountId = player->GetSession()->GetAccountId();

                // 1) 입력값: 환전할 RP 수량
                uint64 wantRP = uint64(std::max<long long>(atoll(args), 0));
                if (wantRP == 0)
                {
                    player->GetSession()->SendNotification("1 이상의 RP를 입력해주세요.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                // 필요 골드(코퍼 기준) 계산
                uint64 needGold  = wantRP * GOLD_PER_RP;     // 골드 단위
                uint64 needCopper = needGold * COPPER_PER_GOLD;

                // ModifyMoney는 int32이므로 말도 안 되게 큰 값 방지
                if (needCopper > uint64(std::numeric_limits<int32>::max()))
                {
                    player->GetSession()->SendNotification("한 번에 너무 많은 RP를 환전하려고 합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                // 2) 골드 보유량 확인
                uint64 curCopper = player->GetMoney();
                if (curCopper < needCopper)
                {
                    player->GetSession()->SendNotification("환전에 필요한 골드가 부족합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                // 3) 골드 차감
                player->ModifyMoney(-int32(needCopper));

                // 4) 계정 RP 증가 (appdata.account_data.reg_point)
                {
                    std::string updRP = Trinity::StringFormat(
                        "UPDATE appdata.account_data SET reg_point = reg_point + {} WHERE id={}",
                        static_cast<uint32>(wantRP), accountId
                    );
                    CharacterDatabase.Execute(updRP.c_str());
                }

                // 5) 안내 메시지
                std::ostringstream msg;
                msg << "골드 " << needGold << "을(를) 사용하여 "
                    << wantRP << " RP로 환전되었습니다.";
                ChatHandler(player->GetSession()).SendSysMessage(msg.str().c_str());

                CloseGossipMenuFor(player);
                return true;
            }

            // 그 외 액션은 이 스크립트에서 처리하지 않음
            return false;
        }
    };

    CreatureAI* GetAI(Creature* c) const override { return new npc_black_market_auctioneerAI(c); }
};


// ============================================================================
//                           Scheduler: Close auctions
// ============================================================================
class BlackMarketAuctionScheduler : public WorldScript
{
public:
    BlackMarketAuctionScheduler() : WorldScript("BlackMarketAuctionScheduler") {}

	void OnStartup() override
    {
		uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
		if (realmId == 1)
			CharacterDatabase.PExecute("TRUNCATE TABLE character_dungeon_session");
    }

    void OnUpdate(uint32 /*diff*/) override
    {
        static time_t nextCheck = 0;
        time_t now = GameTime::GetGameTime();
        if (now < nextCheck)
            return;

        nextCheck = now + 5;

        QueryResult result = WorldDatabase.Query(
            "SELECT auction_id, item_id, current_bid, bidder_guid, end_time "
            "FROM blackmarket_auction "
            "WHERE auction_status='active' AND end_time <= UNIX_TIMESTAMP()"
        );
        if (!result)
            return;

        do
        {
            Field* f          = result->Fetch();
            uint32 auctionId  = f[0].GetUInt32();
            uint32 itemId     = f[1].GetUInt32();
            uint64 bidAmountRP= f[2].GetUInt64(); // 최종 낙찰 RP
            uint32 bidderLow  = f[3].GetUInt32();
            (void)f[4];

            // 상태 completed 로 변경
            {
                std::string sqlDone = Trinity::StringFormat(
                    "UPDATE blackmarket_auction SET auction_status='completed' WHERE auction_id={}",
                    auctionId
                );
                WorldDatabase.Execute(sqlDone.c_str());
            }

            if (bidderLow == 0 || bidAmountRP == 0)
                continue;

            ObjectGuid bidderGuid = ObjectGuid::Create<HighGuid::Player>(bidderLow);

            // 낙찰자 계정 ID 구하기
            uint32 accountIdWinner = 0;
            {
                std::string qAcc = Trinity::StringFormat(
                    "SELECT account FROM characters WHERE guid={}",
                    bidderGuid.GetCounter()
                );
                if (QueryResult q = CharacterDatabase.Query(qAcc.c_str()))
                    accountIdWinner = q->Fetch()[0].GetUInt32();
            }

            // 1) 낙찰 아이템 우편 발송
            {
                MailDraft draft("암시장 낙찰 아이템", "축하합니다! 암시장 경매에서 아이템을 낙찰받았습니다.");
                CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

                if (Item* item = Item::CreateItem(itemId, 1, nullptr))
                {
                    if (const ItemTemplate* proto = item->GetTemplate())
                    {
                        item->SaveToDB(trans);
                        draft.AddItem(item);

                        std::string msg = Trinity::StringFormat(
                            "|cffABD473[암시장알림]|r [{}] {} RP 낙찰.",
                            proto->Name1.c_str(), bidAmountRP
                        );
                        sWorld->SendServerMessage(SERVER_MSG_STRING, msg.c_str(), 0);
                    }
                }

                draft.SendMailTo(trans, MailReceiver(bidderGuid.GetCounter()),
                                 MailSender(MAIL_CREATURE, 24266), MAIL_CHECK_MASK_NONE);
                CharacterDatabase.CommitTransaction(trans);
            }

            // 2) 입찰 로그 상태 갱신: 낙찰자 / 패배자 분리
            {
                // 낙찰자: status = 1 (won)
                if (accountIdWinner != 0)
                {
                    std::string updWin = Trinity::StringFormat(
                        "UPDATE appdata.blackmarket_rp_bids "
                        "SET status=1, updated_at={} "
                        "WHERE auction_id={} AND account_id={}",
                        uint32(now), auctionId, accountIdWinner
                    );
                    CharacterDatabase.Execute(updWin.c_str());
                }

                // 패배자들: status=2, 환급 토큰 발송
                std::string qLosers = Trinity::StringFormat(
                    "SELECT account_id, character_guid, bid_rp "
                    "FROM appdata.blackmarket_rp_bids "
                    "WHERE auction_id={} AND account_id<>{}",
                    auctionId, accountIdWinner == 0 ? 0 : accountIdWinner
                );
                if (QueryResult rLoser = CharacterDatabase.Query(qLosers.c_str()))
                {
                    do
                    {
                        Field* fl       = rLoser->Fetch();
                        uint32 accId    = fl[0].GetUInt32();
                        uint32 chGuidLo = fl[1].GetUInt32();
                        uint32 bidRP    = fl[2].GetUInt32();

                        if (bidRP == 0)
                            continue;

                        uint64 tokenItemGuidCounter = 0;

                        // 환급 토큰 생성 + 우편 발송
                        {
                            MailDraft draft("암시장 RP 환급 토큰",
                                "이번 암시장 경매에서 낙찰받지 못했습니다.\n"
                                "동봉된 토큰을 사용하면 입찰에 사용된 RP를 돌려받을 수 있습니다.");

                            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

                            if (Item* token = Item::CreateItem(BM_RP_REFUND_ITEM_ENTRY, 1, nullptr))
                            {
                                token->SaveToDB(trans);
                                draft.AddItem(token);
                                tokenItemGuidCounter = token->GetGUID().GetCounter();
                            }

                            draft.SendMailTo(trans, MailReceiver(chGuidLo),
                                             MailSender(MAIL_CREATURE, 24266), MAIL_CHECK_MASK_NONE);
                            CharacterDatabase.CommitTransaction(trans);
                        }

                        if (tokenItemGuidCounter != 0)
                        {
                            std::string updLoser = Trinity::StringFormat(
                                "UPDATE appdata.blackmarket_rp_bids "
                                "SET status=2, token_item_guid={}, updated_at={} "
                                "WHERE auction_id={} AND account_id={}",
                                static_cast<uint64_t>(tokenItemGuidCounter),
                                uint32(now), auctionId, accId
                            );
                            CharacterDatabase.Execute(updLoser.c_str());
                        }

                    } while (rLoser->NextRow());
                }
            }

        } while (result->NextRow());
    }
};

// ============================================================================
//                             Spawner: Realm1 only
// ============================================================================
// BlackMarketSpawner (WorldScript)
// - 스폰 모드: TEST / MONTHLY / WEEKLY
// - 플레이어가 없을 때 소환이 자주 실패/즉시 소멸하는 문제를 막기 위해
//   CreateBaseMap + LoadGrid + SetActive(true) 적용
//
// [CHANGED] 핵심 패치
//  1) FindMap(mapId, 0)  -> CreateBaseMap(mapId)
//  2) map->LoadGrid(x,y) 로 그리드 강제 로드
//  3) npc->SetActive(true) 로 그리드 언로드로 인한 즉시 소멸 방지
//  4) 소환 실패/성공 로그 추가

class BlackMarketSpawner : public WorldScript
{
public:
    BlackMarketSpawner() : WorldScript("BlackMarketSpawner") {}

    // NEW: 스폰 모드 구분
    enum BMSpawnMode
    {
        BM_MODE_TEST    = 0, // 서버 기동 후 120초 뒤 테스트 스폰 (계속 반복)
        BM_MODE_MONTHLY = 1, // 한 달에 한 번 랜덤 스폰
        BM_MODE_WEEKLY  = 2,  // 일주일에 한 번 랜덤 스폰
		BM_MODE_MIXED   = 3   // 🔥 추가
    };

    // NEW: 여기서 모드 선택
    static constexpr BMSpawnMode BM_MODE = BM_MODE_MIXED;

    static constexpr uint32 BM_TEST_DELAY_SEC = 120;

    // ─────────────────────────────────────────────────────
    // 한 달에 한 번: 1~28일 사이, 20~22시 사이 랜덤
    // ─────────────────────────────────────────────────────
    static uint32 GenerateMonthlySpawnTime()
    {
        time_t now = GameTime::GetGameTime();
        tm lt = *localtime(&now);

        int year  = lt.tm_year + 1900;
        int month = lt.tm_mon + 1;

        int day   = urand(1, 28);
        int hour  = urand(20, 22);
        int min   = urand(0, 59);

        tm when {};
        when.tm_year = year - 1900;
        when.tm_mon  = month - 1;
        when.tm_mday = day;
        when.tm_hour = hour;
        when.tm_min  = min;
        when.tm_sec  = 0;

        time_t ts = mktime(&when);
        if (ts <= now)
        {
            month += 1;
            if (month > 12)
            {
                month = 1;
                year += 1;
            }

            day   = urand(1, 28);
            hour  = urand(20, 22);
            min   = urand(0, 59);

            when = {};
            when.tm_year = year - 1900;
            when.tm_mon  = month - 1;
            when.tm_mday = day;
            when.tm_hour = hour;
            when.tm_min  = min;
            when.tm_sec  = 0;
            ts = mktime(&when);
        }
        return static_cast<uint32>(ts);
    }

    // NEW: 주간 1회 스폰 스케줄 생성 (다음 주 중 하루)
	//      - "이번 주"가 아니라 반드시 "다음 주" (월~일)에서 하루 랜덤
	//      - 20~22시 사이 랜덤
	static uint32 GenerateWeeklySpawnTime()
	{
		time_t now = GameTime::GetGameTime();
		tm lt = *localtime(&now);

		// tm_wday: 0=일, 1=월, ... 6=토
		// 이번 주 월요일 00:00으로 맞춤
		int daysSinceMonday = (lt.tm_wday == 0) ? 6 : (lt.tm_wday - 1);
		lt.tm_mday -= daysSinceMonday;
		lt.tm_hour = 0;
		lt.tm_min  = 0;
		lt.tm_sec  = 0;

		// mktime으로 정규화(월/년 넘어감 자동 처리)
		time_t thisMonday = mktime(&lt);

		// 다음 주 월요일 00:00
		time_t nextMonday = thisMonday + 7 * 24 * 60 * 60;

		// 다음 주 중 하루(월~일) 랜덤
		int addDays = urand(0, 6);
		time_t baseDay = nextMonday + addDays * 24 * 60 * 60;

		tm when = *localtime(&baseDay);
		when.tm_hour = urand(20, 22);
		when.tm_min  = urand(0, 59);
		when.tm_sec  = 0;

		time_t ts = mktime(&when);

		// 혹시나(서버 타임/서머타임 등) 과거가 되면 7일 뒤로 보정
		if (ts <= now)
			ts += 7 * 24 * 60 * 60;

		return static_cast<uint32>(ts);
	}

	static uint32 GenerateMixedSpawnTime()
	{
		time_t now = GameTime::GetGameTime();
		tm lt = *localtime(&now);

		// 이번 주 월요일 00:00 기준
		int daysSinceMonday = (lt.tm_wday == 0) ? 6 : (lt.tm_wday - 1);
		lt.tm_mday -= daysSinceMonday;
		lt.tm_hour = 0;
		lt.tm_min  = 0;
		lt.tm_sec  = 0;

		time_t thisMonday = mktime(&lt);

		// 다음 주 월요일
		time_t nextMonday = thisMonday + 7 * 24 * 60 * 60;

		// 🔥 일~금 (0=일 ~ 5=금)
		int addDays = urand(0, 5);

		time_t baseDay = nextMonday + addDays * 24 * 60 * 60;

		tm when = *localtime(&baseDay);

		// 🔥 18~22시 랜덤
		when.tm_hour = urand(18, 22);
		when.tm_min  = urand(0, 59);
		when.tm_sec  = 0;

		time_t ts = mktime(&when);

		if (ts <= now)
			ts += 7 * 24 * 60 * 60;

		return static_cast<uint32>(ts);
	}

    // NEW: 공통 스케줄 생성/저장 함수
    void ScheduleNextSpawn(time_t now)
    {
        uint32 nextTs = 0;

        if (BM_MODE == BM_MODE_TEST)
        {
            nextTs = static_cast<uint32>(now) + BM_TEST_DELAY_SEC;
            TC_LOG_INFO("custom", "[BM][TEST] QuickNext: {}초 후({}) 스폰 예정", BM_TEST_DELAY_SEC, nextTs);
        }
        else if (BM_MODE == BM_MODE_MONTHLY)
        {
            nextTs = GenerateMonthlySpawnTime();
            TC_LOG_INFO("custom", "[BM] 다음 월간 스폰 스케줄 생성: next_spawn_time={}", nextTs);
        }
        else if (BM_MODE == BM_MODE_WEEKLY)
        {
            nextTs = GenerateWeeklySpawnTime();
            TC_LOG_INFO("custom", "[BM] 다음 주간 스폰 스케줄 생성: next_spawn_time={}", nextTs);
        }
		else if (BM_MODE == BM_MODE_MIXED)
		{
			nextTs = GenerateMixedSpawnTime(); // 평일 랜덤 1회
			TC_LOG_INFO("custom", "[BM] MIXED 랜덤 스케줄 생성: {}", nextTs);
		}
        else
        {
            nextTs = GenerateMonthlySpawnTime();
            TC_LOG_INFO("custom", "[BM] BM_MODE 알 수 없음, 월간 스케줄로 대체 생성: next_spawn_time={}", nextTs);
        }

        std::string upsert = Trinity::StringFormat(
            "REPLACE INTO blackmarket_schedule (id, next_spawn_time, status) VALUES (1, {}, 1)",
            nextTs);

        WorldDatabase.Execute(upsert.c_str());

        m_spawnTime   = time_t(nextTs);
        m_warningSent = false;
        m_spawned     = false;
    }

    void OnStartup() override
    {
        time_t now = GameTime::GetGameTime();

		// 🔥 TEST 모드: DB 완전 무시
		if (BM_MODE == BM_MODE_TEST)
		{
			TC_LOG_INFO("custom", "[BM][TEST] DB 스케줄 무시, 테스트 타임 적용");

			ScheduleNextSpawn(now); // 바로 TEST 스케줄 생성
			return;
		}

        // 그 이후는 SpawnNPC 호출 후 ScheduleNextSpawn(now)로 자동 순환
        QueryResult r = WorldDatabase.Query("SELECT next_spawn_time, status FROM blackmarket_schedule WHERE id=1");
        if (!r)
        {
            // DB에 아무 것도 없으면 지금 시점을 기준으로 새 스케줄 생성
            ScheduleNextSpawn(now);
            return;
        }

        Field* f = r->Fetch();
        uint32 nextSpawn = f[0].GetUInt32();
        uint32 status    = f[1].GetUInt32();

        m_spawnTime   = time_t(nextSpawn);
        m_warningSent = false;
        m_spawned     = false;

        // status==0 이거나, 이미 과거 시각이면 새로 스케줄 생성
        if (status == 0 || m_spawnTime <= now)
        {
            TC_LOG_INFO("custom", "[BM] 기존 스케줄이 비활성/과거(next_spawn_time={}, status={}), 새 스케줄 생성",
                        nextSpawn, status);
            ScheduleNextSpawn(now);
        }
    }

    void OnUpdate(uint32 /*diff*/) override
    {
        time_t now = GameTime::GetGameTime();
        if (m_spawnTime == 0)
            return;

		// =========================
		// 🔥 토요일 21시 고정 (MIXED 전용)
		// =========================
		if (BM_MODE == BM_MODE_MIXED)
		{
			static uint32 lastSaturdaySpawnDay = 0;

			tm lt = *localtime(&now);

			if (lt.tm_wday == 6 && lt.tm_hour == 21 && lt.tm_min == 0)
			{
				uint32 today = lt.tm_yday;

				if (today != lastSaturdaySpawnDay)
				{
					TC_LOG_INFO("custom", "[BM] MIXED 토요일 고정 경매 시작");

					SpawnNPC();

					lastSaturdaySpawnDay = today;
				}
			}
		}

        // 1시간 전 경고
		uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
		if (realmId == 1)
		{
			if (!m_warningSent && now >= (m_spawnTime - 3600))
			{
				(void)std::system("php /home/esotar/fcm/send_fcm_BM.php 'alert' '암거상 노사장이 1시간뒤 경매를 시작할 예정입니다.' > /dev/null 2>&1 &");
				SendServerDiscordEvent("🧿 암거상 등장", "암거상 노사장이 1시간뒤 경매를 시작할 예정입니다","https://stimpack.pe.kr/auth/register", 16759040, "event");
				m_warningSent = true;
			} 
		}

        // 스폰 시도
        if (!m_spawned && now >= m_spawnTime)
        {
            SpawnNPC();
            m_spawned = true;

            // ★ 스폰 이후 곧바로 다음 스케줄 생성/저장
            ScheduleNextSpawn(now);
        }
    }

private:
    time_t m_spawnTime = 0;
    bool   m_warningSent = false;
    bool   m_spawned = false;

    void BroadcastSpawn(Creature* npc)
    {
        sWorld->SendServerMessage(
            SERVER_MSG_STRING,
            "|cffFF0000[암시장 알림]|r 암시장 상인이 경매를 시작했습니다. 1시간동안 거래가 가능합니다.",
            0);

        if (npc)
            npc->DespawnOrUnsummon(Seconds(SPAWN_DURATION_SEC));

		uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
		if (realmId == 1)
		{
			(void)std::system("php /home/esotar/fcm/send_fcm_BM.php 'alert' '암거상 노사장이 경매를 시작했습니다.'  > /dev/null 2>&1 &");
			SendServerDiscordEvent("🧿 암거상 등장", "암거상 노사장이 경매를 시작했습니다.","https://stimpack.pe.kr/auth/register", 16759040, "event");
		}
    }

    void BroadcastSpawnGold(Creature* npc)
    {
        if (npc)
            npc->DespawnOrUnsummon(Seconds(SPAWN_DURATION_SEC));
    }

    void SpawnNPC()
    {
    /*    static const std::array<std::pair<uint32, Position>, 1> kSpawns = {{
            {1, Position{-7191.6f, -3938.15f, 8.96297f, 3.50838f}},
        }};

        auto pick = kSpawns[urand(0, kSpawns.size() - 1)];
        uint32 mapId = pick.first;
        Position pos = pick.second;

        Position pos_gold{-7196.125f, -3933.963f, 9.143f, 4.276315f}; // 노스렌드

        // [CHANGED] FindMap은 "이미 로드된 맵"에서만 잘 잡히는 경우가 있어,
        //           플레이어가 없을 때 소환 실패/즉시 소멸 이슈가 자주 발생함.
        //           베이스맵 확보 + 그리드 강제 로드로 해결.
        Map* map = sMapMgr->CreateBaseMap(mapId);
        if (!map)
        {
            TC_LOG_ERROR("custom", "[BM] CreateBaseMap 실패: mapId={}", mapId);
            return;
        }

        // [CHANGED] 좌표 그리드 강제 로드 (플레이어 없어도 소환 가능하게)
        map->LoadGrid(pos.GetPositionX(), pos.GetPositionY());
        map->LoadGrid(pos_gold.GetPositionX(), pos_gold.GetPositionY());

        Creature* npc = map->SummonCreature(BLACKMARKET_NPC_ENTRY, pos);
        if (!npc)
        {
            TC_LOG_ERROR("custom", "[BM] SummonCreature 실패: entry={}, mapId={}, pos=({}, {}, {})",
                         BLACKMARKET_NPC_ENTRY,
                         mapId,
                         pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
            return;
        }

        // [CHANGED] 그리드 언로드로 바로 사라지는 케이스 완화
        npc->setActive(true);

        if (Creature* npcgold = map->SummonCreature(5000065, pos_gold)) // 이벤트 NPC Entry
        {
            npcgold->setActive(true);
            BroadcastSpawnGold(npcgold);
        }

        // [CHANGED] 소환 성공 로그
        TC_LOG_INFO("custom", "[BM] NPC 소환 성공: guidLow={}, mapId={}, active={}",
                    npc->GetGUID().GetCounter(), mapId, npc->isActiveObject());

        s_BlackMarketNpcGuid  = npc->GetGUID();
        s_BlackMarketNpcMapId = mapId;  */

		QueryResult activeCheck = WorldDatabase.PQuery("SELECT 1 FROM blackmarket_auction WHERE auction_status='active' LIMIT 1");

		if (activeCheck)
		{
			TC_LOG_INFO("custom", "[BM] 이미 진행 중인 경매 있음 → 생성 스킵");
			return;
		}

		WorldDatabase.PExecute("UPDATE blackmarket_auction SET auction_status='completed' WHERE auction_status='active'");

        uint32 groupId = static_cast<uint32>(GameTime::GetGameTime());

		// ─────────────────────────
		// 1️⃣ 카테고리별 아이템 미리 로드
		// ─────────────────────────
		std::unordered_map<uint32, std::vector<uint32>> itemCache;

		for (uint32 catId : BM_CATEGORY_IDS)
		{
			std::string q = Trinity::StringFormat(
				"SELECT game_item_id FROM appdata.shop_items WHERE category_id = {}",
				catId
			);

			QueryResult r = WorldDatabase.Query(q.c_str());
			if (!r)
				continue;

			std::vector<uint32>& items = itemCache[catId];

			do
			{
				Field* f = r->Fetch();
				items.push_back(f[0].GetUInt32());
			}
			while (r->NextRow());
		}

        // ─────────────────────────────────────────────
        // 여기부터: AUCTION_VISIBLE 개수 채우기
        // ─────────────────────────────────────────────
        // ─────────────────────────
		// 2️⃣ 기존 루프
		// ─────────────────────────
		uint32 created   = 0;
		uint32 attempts  = 0;
		const uint32 kMaxAttempts = AUCTION_VISIBLE * 20;

		for (uint32 i = 0; i < AUCTION_VISIBLE; )
		{
			if (++attempts > kMaxAttempts)
				break;

			uint32 catId = BM_CATEGORY_IDS[urand(0, BM_CATEGORY_IDS.size() - 1)];

			auto it = itemCache.find(catId);
			if (it == itemCache.end() || it->second.empty())
				continue;

			std::vector<uint32>& items = it->second;

			// 🔥 랜덤 선택 + 즉시 제거 (중복 방지 핵심)
			uint32 idx = urand(0, items.size() - 1);
			uint32 item = items[idx];

			items[idx] = items.back();
			items.pop_back();

			time_t end  = GameTime::GetGameTime() + SPAWN_DURATION_SEC;

			uint64 minIncRP = MIN_INCREMENT_RP;

			std::string ins = Trinity::StringFormat(
				"INSERT INTO blackmarket_auction "
				"(item_id, start_price, current_bid, bidder_guid, end_time, auction_status, group_id, extend_count, min_increment, bid_count) "
				"VALUES ({}, {}, 0, 0, {}, 'active', {}, 0, {}, 0)",
				item,
				static_cast<uint64_t>(MIN_START_RP),
				static_cast<uint32_t>(end),
				groupId,
				static_cast<uint64_t>(minIncRP));

			WorldDatabase.Execute(ins.c_str());
			++created;
			++i;
		}

        if (created < AUCTION_VISIBLE)
        {
            TC_LOG_WARN("custom", "[BM] 경매 아이템을 충분히 채우지 못했습니다: {}/{} (시도횟수 {})",
                        created, AUCTION_VISIBLE, attempts);
        }
        else
        {
            TC_LOG_INFO("custom", "[BM] 경매 아이템 {}개 정상 등록 (시도횟수 {})",
                        created, attempts);
        }

        sWorld->SendServerMessage(SERVER_MSG_STRING, "|cffFF0000[암시장 알림]|r 암시장 상인이 경매를 시작했습니다. 1시간동안 거래가 가능합니다.", 0);

		uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
		if (realmId == 1)
		{
			(void)std::system("php /home/esotar/fcm/send_fcm_BM.php 'alert' '암거상 노사장이 경매를 시작했습니다.'  > /dev/null 2>&1 &");
			SendServerDiscordEvent("🧿 암거상 등장", "암거상 노사장이 경매를 시작했습니다.","https://stimpack.pe.kr",16753920,"event"); 
		}
    }
};

//rp환불아이템
class item_blackmarket_rp_refund : public ItemScript
{
public:
    item_blackmarket_rp_refund() : ItemScript("item_blackmarket_rp_refund") {}

    bool OnUse(Player* player, Item* item, SpellCastTargets const&) override
    {
        if (!player || !item)
            return false;

        uint32 accountId = player->GetSession()->GetAccountId();
        uint64 itemGuid  = item->GetGUID().GetCounter();

        // 1) 토큰이 연결된 입찰 로그 찾기
        std::string q = Trinity::StringFormat(
            "SELECT auction_id, account_id, bid_rp, status "
            "FROM appdata.blackmarket_rp_bids "
            "WHERE token_item_guid={} LIMIT 1",
            static_cast<uint64_t>(itemGuid)
        );

        uint32 auctionId = 0;
        uint32 logAccId  = 0;
        uint32 bidRP     = 0;
        uint8  status    = 0;

        if (QueryResult r = CharacterDatabase.Query(q.c_str()))
        {
            Field* f = r->Fetch();
            auctionId = f[0].GetUInt32();
            logAccId  = f[1].GetUInt32();
            bidRP     = f[2].GetUInt32();
            status    = f[3].GetUInt8();
        }
        else
        {
            player->GetSession()->SendNotification("이 토큰과 연결된 RP 입찰 기록을 찾을 수 없습니다.");
            return false;
        }

        if (logAccId != accountId)
        {
            player->GetSession()->SendNotification("이 토큰은 다른 계정의 RP 입찰과 연결되어 있습니다.");
            return false;
        }

        if (status != 2) // lost_token_sent 상태가 아닌 경우
        {
            player->GetSession()->SendNotification("이미 환급되었거나 사용할 수 없는 토큰입니다.");
            return false;
        }

        if (bidRP == 0)
        {
            player->GetSession()->SendNotification("환급할 RP가 없습니다.");
            return false;
        }

        // 2) 계정 RP에 재적립
        {
            std::string updRP = Trinity::StringFormat(
                "UPDATE appdata.account_data SET reg_point = reg_point + {} WHERE id={}",
                bidRP, accountId
            );
            CharacterDatabase.Execute(updRP.c_str());
        }

        // 3) 로그 상태 갱신 (refunded)
        {
            uint32 nowTs = uint32(GameTime::GetGameTime());
            std::string updLog = Trinity::StringFormat(
                "UPDATE appdata.blackmarket_rp_bids "
                "SET status=3, updated_at={} "
                "WHERE auction_id={} AND account_id={} AND token_item_guid={}",
                nowTs, auctionId, accountId, static_cast<uint64_t>(itemGuid)
            );
            CharacterDatabase.Execute(updLog.c_str());
        }

        // 4) 아이템 파기
        player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        player->GetSession()->SendNotification("입찰에 사용된 RP가 환급되었습니다.");

        return true;
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

			SendServerDiscordEvent(
				"🟢 로그인",
				"계정: " + username,
				"https://stimpack.pe.kr",
				3066993,
				"home"
			);
		}
    }
};

//6개월 이상 미접속 관리
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
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT c.account, c.guid, c.name, c.logout_time "
            "FROM characters.characters c "
            "LEFT JOIN appdata.character_archive_log l ON l.guid = c.guid "
            "WHERE c.logout_time > 0 "
            "AND c.logout_time <= UNIX_TIMESTAMP(DATE_SUB(NOW(), INTERVAL 6 MONTH)) "
            "AND c.online = 0 "
            "AND l.guid IS NULL"
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
            uint32 now     = uint32(time(nullptr));

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
                "(guid, account_id, name, last_logout, locked_at, active) "
                "VALUES ({}, {}, '{}', {}, {}, 1) "
                "ON DUPLICATE KEY UPDATE "
                "account_id = VALUES(account_id), "
                "name = VALUES(name), "
                "last_logout = VALUES(last_logout), "
                "locked_at = VALUES(locked_at), "
                "active = 1",
                guid,
                account,
                name,
                logout,
                now
            );

            if (!exists)
            {
                CharacterDatabase.PExecute(
                    "INSERT INTO appdata.character_archive_log "
                    "(guid, name, account_id, last_logout, archived_at) "
                    "VALUES ({}, '{}', {}, {}, {})",
                    guid,
                    name,
                    account,
                    logout,
                    now
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









/* ============================================================
 * 스크립트 등록
 * ============================================================ */
void AddSC_custom_scripts()
{
    new boost_player();
    new item_portable_bankmail_onuse();
	new item_mail();
	new item_buff();

	//암시장
	new npc_black_market_auctioneer();
    new BlackMarketAuctionScheduler();
    new BlackMarketSpawner();
	new item_blackmarket_rp_refund();

	//80레벨 부스터
	new item_level80_boost_dps();

	//로그인카운트
	new account_daily_ingame();

	//6개월이상 미접속 케릭터
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
}
