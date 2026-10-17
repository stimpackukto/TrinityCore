#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Creature.h"
#include "Player.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"   // SendAreaTriggerMessage 용
#include "Group.h"
#include "Config.h"
#include "Map.h"
#include "GameTime.h"
#include "Log.h"
#include "World.h"
#include "Random.h"
#include "Channel.h"
#include "Item.h"
#include "StringFormat.h"    // Trinity::StringFormat
#include "Creature.h"
#include "CreatureAI.h"
#include "Chat.h"
#include "ObjectAccessor.h"
#include "MapManager.h"
#include "Mail.h"
#include "ObjectGuid.h"
#include "ScriptedCreature.h"
#include "SharedDefines.h"      // GOLD 등 통화 상수
#include "ItemTemplate.h"       // ItemTemplate 접근
#include "ObjectMgr.h"    // sObjectMgr
#include "QuestDef.h"     // Quest
#include "MotionMaster.h"
 #include "MovementGenerator.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "ObjectMgr.h" // sObjectMgr->GetItemTemplate
#include "SharedDefines.h" // ITEM_CLASS_WEAPON / ITEM_CLASS_ARMOR

#include <inttypes.h>
#include <unordered_map>
#include <array>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <string>
#include <cstdlib>
#include <iomanip>
#include <cctype>
#include <curl/curl.h>

#include "ScriptMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"
#include "ObjectGuid.h"
#include "StringFormat.h"

#include "AuctionHouseMgr.h"
#include "SocialMgr.h"

#include "GameObject.h"
#include "GameObjectAI.h"
#include "ObjectMgr.h"

#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <filesystem>

#include "Pet.h"
#include "DBCStores.h"
// [NEW] 업적 체크용
#include "AchievementMgr.h"
// [NEW] GearScore 계산용
#include <cmath>

static std::set<uint32> NorthrendDungeons =
{
    574, // Utgarde Keep
    575, // Utgarde Pinnacle
    576, // The Nexus
    578, // The Oculus
    595, // The Culling of Stratholme
    599, // Halls of Stone
    600, // Drak'Tharon Keep
    601, // Azjol-Nerub
    602, // Halls of Lightning
    604, // Gundrak
    608, // Violet Hold
    619, // Ahn'kahet

    650, // Trial of the Champion
    658, // Pit of Saron
    668,  // Halls of Reflection
	632,  // Halls of Reflection
};


[[maybe_unused]] static std::string GetTodayDateString()
{
    std::time_t now = std::time(nullptr);
    std::tm* tm = std::localtime(&now);

    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d");
    return oss.str();
}
namespace
{
    [[maybe_unused]] static constexpr bool CHECK_DATE_RANGE = true;

    // item_entry -> goal_type (화이트리스트)
    static bool GetGoalTypeFromWhitelist(uint32 itemEntry, std::string& outGoalType)
    {
        QueryResult r = CharacterDatabase.PQuery(
            "SELECT goal_type "
            "FROM appdata.weekly_delivery_allowed "
            "WHERE item_entry={} AND enabled=1 "
            "LIMIT 1",
            itemEntry
        );

        if (!r)
            return false;

        Field* f = r->Fetch();
        outGoalType = f[0].GetString();
        return !outGoalType.empty();
    }

    // goal_type -> 이번 주 활성 goal_id, min_personal_count
    static bool GetActiveGoalInfo(std::string const& goalType, uint32& outGoalId, uint32& outMinPersonal)
    {
        QueryResult r;

        r = CharacterDatabase.PQuery(
                "SELECT id, min_personal_count "
                "FROM appdata.weekly_server_goals "
                "WHERE goal_type='{}' AND status='active' "
                "ORDER BY start_date DESC "
                "LIMIT 1",
                goalType
            );

        if (!r)
            return false;

        Field* f = r->Fetch();
        outGoalId      = f[0].GetUInt32();
        outMinPersonal = f[1].GetUInt32();
        return outGoalId > 0;
    }

    static void AddServerProgress([[maybe_unused]] uint32 goalId, [[maybe_unused]] uint32 amount)
    {
     /*   CharacterDatabase.PExecute(
            "UPDATE appdata.weekly_server_goals "
            "SET current_count = current_count + {} "
            "WHERE id={}",
            amount,
            goalId
        ); */
    }

    static void AddPersonalContribution([[maybe_unused]] uint32 goalId, [[maybe_unused]] uint32 accountId, [[maybe_unused]] uint32 addAmount, [[maybe_unused]] uint32 minPersonal)
    {
        // 중요: weekly_goal_contributions에 UNIQUE(goal_id, account_id) 필요
    /*    CharacterDatabase.PExecute(
            "INSERT INTO appdata.weekly_goal_contributions "
            "(goal_id, account_id, personal_count, qualified, rewarded, updated_at) "
            "VALUES ({}, {}, {}, IF({} >= {}, 1, 0), 0, NOW()) "
            "ON DUPLICATE KEY UPDATE "
            "personal_count = personal_count + {}, "
            "qualified = IF(personal_count + {} >= {}, 1, qualified), "
            "updated_at = NOW()",
            goalId, accountId, addAmount, addAmount, minPersonal,
            addAmount, addAmount, minPersonal
        ); */
    }

    static void InsertDeliveryLog(uint32 goalId, std::string const& goalType, uint32 accountId, uint64 charGuid,
                                 uint32 vendorEntry, uint32 itemEntry, uint32 itemCount)
    {
        CharacterDatabase.PExecute(
            "INSERT INTO appdata.weekly_delivery_logs "
            "(goal_id, goal_type, account_id, char_guid, vendor_entry, item_entry, item_count) "
            "VALUES ({}, '{}', {}, {}, {}, {}, {})",
            goalId,
            goalType,
            accountId,
            charGuid,
            vendorEntry,
            itemEntry,
            itemCount
        );
    }

	static bool GetGoalTitle(uint32 goalId, std::string& outTitle)
	{
		std::string sql = Trinity::StringFormat(
			"SELECT title "
			"FROM appdata.weekly_server_goals "
			"WHERE id={} "
			"LIMIT 1",
			goalId
		);

		QueryResult r = CharacterDatabase.Query(sql.c_str());
		if (!r)
			return false;

		Field* f = r->Fetch();
		outTitle = f[0].GetString();
		return !outTitle.empty();
	}

    bool TryLogDaily(Player* player, Player* target, uint32 emoteId)
    {
        if (!player || !target)
            return false;

        if (player == target)
            return false;

        uint64 senderGuid = player->GetGUID().GetRawValue();
        uint64 targetGuid = target->GetGUID().GetRawValue();

        std::string checkSql = Trinity::StringFormat(
            "SELECT 1 FROM appdata.daily_emote_log "
            "WHERE sender_guid={} AND target_guid={} "
            "AND emote_id={} AND log_date=CURDATE()",
            senderGuid,
            targetGuid,
            emoteId
        );

        if (CharacterDatabase.Query(checkSql.c_str()))
            return false;

        std::string insertSql = Trinity::StringFormat(
            "INSERT IGNORE INTO appdata.daily_emote_log "
            "(sender_guid, target_guid, emote_id, log_date) "
            "VALUES ({}, {}, {}, CURDATE())",
            senderGuid,
            targetGuid,
            emoteId
        );

        CharacterDatabase.Execute(insertSql.c_str());
        return true;
    }

	std::unordered_map<std::string, std::vector<uint32>> g_TaskMap;

    inline std::string TaskDateSQL()
    {
        return "DATE(DATE_SUB(NOW(), INTERVAL 4 HOUR))";
    }

    bool UpdateTask(uint32 accountId, uint32 taskId, uint32 add)
	{
		if (QueryResult res = CharacterDatabase.PQuery("SELECT l.completed FROM appdata.daily_task_log l WHERE l.account_id = {} AND l.task_id = {} AND l.task_date = {}",	accountId, taskId, TaskDateSQL()))
            if (Field* fields = res->Fetch())
				if (fields[0].GetUInt8() == 1)
					return false; 

		std::string sql1 = Trinity::StringFormat(
			"INSERT INTO appdata.daily_task_log "
			"(account_id, task_id, task_date, progress, completed, rewarded) "
			"VALUES ({}, {}, {}, {}, 0, 0) "
			"ON DUPLICATE KEY UPDATE "
			"progress = progress + {}",
			accountId, taskId, TaskDateSQL(), add, add
		);
		CharacterDatabase.Execute(sql1.c_str());

		std::string sql2 = Trinity::StringFormat(
			"UPDATE appdata.daily_task_log l "
			"JOIN appdata.daily_tasks t ON l.task_id = t.task_id "
			"SET l.completed = 1 "
			"WHERE l.account_id = {} "
			"AND l.task_id = {} "
			"AND l.task_date = {} "
			"AND l.progress >= t.target_count",
			accountId, taskId, TaskDateSQL()
		);
		CharacterDatabase.Execute(sql2.c_str());

		return true;
	}

    bool AddDailyProgress(Player* player, const std::string& type, uint32 add)
	{
		if (!player)
			return false;

		auto it = g_TaskMap.find(type);
		if (it == g_TaskMap.end())
			return false;

		uint32 accId = player->GetSession()->GetAccountId();

		bool updated = false;

		for (uint32 taskId : it->second)
		{
			if (UpdateTask(accId, taskId, add))
				updated = true;
		}

		return updated;
	}
}

static std::unordered_map<uint32, uint32> g_KillStreak; // charGuid -> 연속킬 수
static std::unordered_map<uint32, uint32> g_EmoteCount; // charGuid -> 사용 횟수
static std::unordered_map<uint32, uint32> g_NpcTalkCount; // charGuid -> 대화 횟수
static std::unordered_map<uint32, time_t> g_FriendPartyStart; // charGuid -> 시작시간
static std::unordered_set<uint32> g_FriendPartyDone;          // 이미 완료한 캐릭
static std::unordered_map<uint32, time_t> g_LoginStart; // charGuid -> 로그인 시작 시간
static std::unordered_set<uint32> g_Online1hDone;       // 이미 완료한 캐릭
static std::unordered_map<uint32, uint32> g_ProfessionCastCount; // charGuid -> 시도 횟수
static std::unordered_map<uint32, uint32> g_PvPKillCount; // charGuid -> PvP 킬 수
static std::unordered_map<uint32, uint32> g_DailyQuestCompleteCount; // charGuid -> 완료 수
static std::unordered_map<uint32, uint32> g_NormalQuestCompleteCount; // charGuid -> 완료 수
static std::unordered_map<uint32, uint32> g_GatherCount; // charGuid -> 채집 횟수
static std::unordered_set<uint32> g_BgQueuedOnce;
static std::unordered_set<uint32> g_LfgQueued;

class DeliveryVendorScript : public PlayerScript
{
public:
    DeliveryVendorScript() : PlayerScript("DeliveryVendorScript") { }

    bool OnPlayerSellItem(Player* player, Creature* vendor, Item* item, uint32& count, [[maybe_unused]] uint32& money) override
    {
        if (!player || !vendor || !item || count == 0)
            return false;

        // 1) 여관주인 아니면: 일반 판매
        if (!vendor->IsInnkeeper())
            return false;

        // 2) 화이트리스트 아이템인지 확인
        std::string goalType;
        if (!GetGoalTypeFromWhitelist(item->GetEntry(), goalType))
            return false; // 화이트리스트 아니면 일반 판매

        // 3) 이번 주 active 목표 찾기
        uint32 goalId = 0;
        uint32 minPersonal = 0;
        if (!GetActiveGoalInfo(goalType, goalId, minPersonal))
            return false; // 목표가 없으면 그냥 일반 판매

        uint32 accountId = player->GetSession()->GetAccountId();

        // 4) 서버/개인 누적
        AddServerProgress(goalId, count);
        AddPersonalContribution(goalId, accountId, count, minPersonal);

        // 5) 로그(선택)
        InsertDeliveryLog(goalId, goalType, accountId, player->GetGUID().GetRawValue(),
                          vendor->GetEntry(), item->GetEntry(), count);

		std::string goalTitle;
		if (!GetGoalTitle(goalId, goalTitle))
			goalTitle = goalType; // 혹시 실패하면 fallback

		std::string msg = Trinity::StringFormat(
			"납품 처리: {}개가 집계되었습니다. [{}]",
			count,
			goalTitle
		);
		ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());

		AddDailyProgress(player, "delivery", 1);

        return true;
    }

	void OnBattlegroundEnd(Player* player, Battleground* bg, bool won) override
    {
        if (!player || !bg)
            return;

        uint32 accountId = player->GetSession()->GetAccountId();
		AddDailyProgress(player, "bg", 1);

        // 🔹 참여 1회 (항상 증가)
        {
            uint32 goalId = 0;
            uint32 minPersonal = 0;

            if (GetActiveGoalInfo("bg_complete", goalId, minPersonal))
            {
                AddServerProgress(goalId, 1);
                AddPersonalContribution(goalId, accountId, 1, minPersonal);
            }
        }

        // 🔹 승리한 경우만
        if (won)
        {
			AddDailyProgress(player, "bg_win", 1);
            uint32 goalId = 0;
            uint32 minPersonal = 0;

            if (GetActiveGoalInfo("bg_win", goalId, minPersonal))
            {
                AddServerProgress(goalId, 1);
                AddPersonalContribution(goalId, accountId, 1, minPersonal);
            }
        }
    }

	void OnCreatureKill(Player* player, Creature* creature) override
	{
		if (!player || !creature)
			return;

		uint32 accountId = player->GetSession()->GetAccountId();
		uint32 charGuid = player->GetGUID().GetCounter();

		AddDailyProgress(player, "kill", 1);

		if (creature->isElite())
			AddDailyProgress(player, "elite_kill", 1);

		g_KillStreak[charGuid]++;
		if (g_KillStreak[charGuid] == 30)
			AddDailyProgress(player, "no_death_kill", 1);

		Map* map = player->GetMap();
		if (!creature->IsDungeonBoss())
			return;

		uint32 mapId = map->GetId();
		bool isHeroic = map->IsHeroic();

		if (!creature->GetMap()->IsDungeon() && creature->GetCreatureTemplate()->rank == CREATURE_ELITE_WORLDBOSS)
			AddDailyProgress(player, "world_boss", 1);

		if (creature->GetMap()->IsRaid() && creature->IsDungeonBoss())
			AddDailyProgress(player, "raid_boss", 1);

		bool isNorthrend = NorthrendDungeons.count(mapId);

		std::string goalType;

		if (isNorthrend && !isHeroic)
			goalType = "northrend_dungeon_boss";
		else if (isNorthrend && isHeroic)
			goalType = "northrend_dungeon_hboss";
		else if (!isNorthrend && !isHeroic)
			goalType = "dungeon_other_normal";
		else if (!isNorthrend && isHeroic)
			goalType = "dungeon_other_heroic";
		else
			return;

		uint32 goalId = 0;
		uint32 minPersonal = 0;

		if (!GetActiveGoalInfo(goalType, goalId, minPersonal))
			return;

		AddServerProgress(goalId, 1);
		AddPersonalContribution(goalId, accountId, 1, minPersonal);
	}

	void OnAchievementEarned(Player* player, uint32 achievementId) override
	{
		if (!player)
			return;

		AchievementEntry const* achievement = sAchievementStore.LookupEntry(achievementId);
		if (!achievement)
			return;

		uint32 accountId = player->GetSession()->GetAccountId();

		AddDailyProgress(player, "achievements", 1);

		// ------------------------
		// 1️⃣ 통합 업적 (항상 증가)
		// ------------------------
		{
			uint32 goalId = 0;
			uint32 minPersonal = 0;

			if (GetActiveGoalInfo("achievements", goalId, minPersonal))
			{
				AddServerProgress(goalId, 1);
				AddPersonalContribution(goalId, accountId, 1, minPersonal);
			}
		}

		// ------------------------
		// 2️⃣ 오리지널
		// ------------------------
		switch (achievement->Category)
		{
			case 14808:
			case 14821:
			case 14861:
			case 14864:
			{
				uint32 goalId = 0;
				uint32 minPersonal = 0;

				if (GetActiveGoalInfo("classic_achievements", goalId, minPersonal))
				{
					AddServerProgress(goalId, 1);
					AddPersonalContribution(goalId, accountId, 1, minPersonal);
				}
				break;
			}

			// ------------------------
			// 3️⃣ 불타는 성전
			// ------------------------
			case 14805:
			case 14822:
			case 14862:
			case 14865:
			{
				uint32 goalId = 0;
				uint32 minPersonal = 0;

				if (GetActiveGoalInfo("tbc_achievements", goalId, minPersonal))
				{
					AddServerProgress(goalId, 1);
					AddPersonalContribution(goalId, accountId, 1, minPersonal);
				}
				break;
			}

			// ------------------------
			// 3️⃣ 리치왕의 분노 
			// ------------------------
			case 14807:
			case 96:
			case 201:
			{
				uint32 goalId = 0;
				uint32 minPersonal = 0;

				if (GetActiveGoalInfo("wotlk_achievements", goalId, minPersonal))
				{
					AddServerProgress(goalId, 1);
					AddPersonalContribution(goalId, accountId, 1, minPersonal);
				}
				break;
			}

			default:
				break;
		}
	}

    void OnTextEmote(Player* player, uint32 textEmote, uint32 /*emoteNum*/, ObjectGuid guid) override
	{
		if (!player)
			return;

		// 대상 플레이어 찾기
		Player* target = ObjectAccessor::FindPlayer(guid);
		if (!target)
			return;

	/*	std::string msgt = Trinity::StringFormat(
			"[Weekly-Test] EMOTE via OnChat textEmote={}",
			textEmote
		);

		ChatHandler(player->GetSession()).SendSysMessage(msgt.c_str()); */

		if (TryLogDaily(player, target, textEmote))
			AddDailyProgress(player, "emote_use", 1);

		std::string goalType;
		switch (textEmote)
		{
			case 17: goalType = "greetings"; break; // /인사
			case 34: goalType = "dance";     break; // /춤
			case 58: goalType = "kiss";      break; // /키스
			case 56: goalType = "hug";       break; // /포옹
			case 5: goalType = "clap";      break; // /박수
			case 78: goalType = "salute";      break; // 경례
			default:
				return;
		}

		uint32 accountId   = player->GetSession()->GetAccountId();
		uint32 goalId      = 0;
		uint32 minPersonal = 0;

		if (!GetActiveGoalInfo(goalType, goalId, minPersonal))
			return;

		if (TryLogDaily(player, target, textEmote))
		{
			AddServerProgress(goalId, 1);
			AddPersonalContribution(goalId, accountId, 1, minPersonal);
		}
	}

	void OnLogin(Player* player, bool /*firstLogin*/) override
	{
		if (!player)
			return;

		uint32 charGuid = player->GetGUID().GetCounter();
		g_LoginStart[charGuid] = time(nullptr);
		g_Online1hDone.erase(charGuid);

		// 여관(휴식 지역) 여부 확인
		if (player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))
			AddDailyProgress(player, "rest_area", 1);
	}

	// 로그아웃 시 초기화
	void OnLogout(Player* player) override
	{
		if (!player)
			return;

		uint32 charGuid = player->GetGUID().GetCounter();
		g_LoginStart.erase(charGuid);
		g_Online1hDone.erase(charGuid);
	}

	void OnSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
	{
		if (!player || !spell)
			return;

		SpellInfo const* info = spell->GetSpellInfo();
		if (!info)
			return;

		for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
		{
			uint32 effect = info->_effects[i].Effect; // 🔥 수정

			// 🔹 타인 부활
			if (effect == SPELL_EFFECT_RESURRECT)
			{
				AddDailyProgress(player, "resurrect", 1);
				return;
			}

			// 🔹 제작
			if (effect == SPELL_EFFECT_CREATE_ITEM)
			{
				AddDailyProgress(player, "profession_craft", 1);
				return;
			}

			// 🔥 무두질 (추가)
			if (effect == SPELL_EFFECT_SKINNING)
			{
				AddDailyProgress(player, "profession_gather", 1);
				return;
			}
		}
	}

	void OnPVPKill(Player* killer, Player* victim) override
	{
		if (!killer || !victim)
			return;

		if (killer == victim)
			return;

		AddDailyProgress(killer, "world_pvp", 1);
	}

	void OnLootItem(Player* looter, [[maybe_unused]] ObjectGuid const& lootGuid, uint32 itemId, [[maybe_unused]] uint32 count) override
	{
		if (!looter)
			return;

		bool isGather = false;

		/* 약초 */
		if (sItemStore.LookupEntry(itemId))
		{
			ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);

			if (proto)
			{
				if (proto->Class == ITEM_CLASS_TRADE_GOODS)
				{
					switch (proto->SubClass)
					{
						case ITEM_SUBCLASS_HERB:
						case ITEM_SUBCLASS_METAL_STONE:
							isGather = true;
							break;
					}
				}
			}
		}

		if (isGather)
			AddDailyProgress(looter, "profession_gather", 1); 
	}

	void OnSendMail(Player* sender, ObjectGuid /*receiverGuid*/, uint32 receiverAccountId, uint32 /*money*/, uint32 /*cod*/, std::vector<Item*> const& /*items*/) override
    {
        if (!sender)
            return;

		// ✅ 같은 계정이면 카운트하지 않음
		if (sender->GetSession()->GetAccountId() == receiverAccountId)
			return;

        // 여기서 호출
        AddDailyProgress(sender, "mail_send", 1);
    }
};

class WeeklyAuctionGoalScript : public AuctionHouseScript
{
public:
    WeeklyAuctionGoalScript() : AuctionHouseScript("WeeklyAuctionGoalScript") { }

    void OnAuctionAdd([[maybe_unused]] AuctionHouseObject* ah, AuctionEntry* entry) override
    {
        if (!entry)
            return;

        if (Player* seller = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(entry->owner)))
        {
			AddDailyProgress(seller, "auction_use", 1);
            std::string goalType = "auction_list"; // DB와 동일하게

            uint32 goalId = 0;
            uint32 minPersonal = 0;

            if (!GetActiveGoalInfo(goalType, goalId, minPersonal))
                return;

            uint32 accountId = seller->GetSession()->GetAccountId();

            AddServerProgress(goalId, 1);
            AddPersonalContribution(goalId, accountId, 1, minPersonal);

            std::string goalTitle;
            if (!GetGoalTitle(goalId, goalTitle))
                goalTitle = goalType;

            std::string msg = Trinity::StringFormat(
                "경매 등록 1회 집계되었습니다. [{}]",
                goalTitle
            );

            ChatHandler(seller->GetSession()).SendSysMessage(msg.c_str());
        }
    }

   void OnAuctionWon(Player* player, [[maybe_unused]] uint32 itemEntry, [[maybe_unused]] uint32 itemCount) override
    {
        uint32 goalId = 0;
        uint32 minPersonal = 0;
		 uint32 accountId = player->GetSession()->GetAccountId();

        if (!GetActiveGoalInfo("auction_buy", goalId, minPersonal))
            return;

        AddServerProgress(goalId, 1);
		AddDailyProgress(player, "auction_buy", 1);
        AddPersonalContribution(goalId, accountId, 1, minPersonal);
    }
};

#include "ScriptMgr.h"
#include "World.h"
#include "DatabaseEnv.h"
#include "Timer.h"

class weekly_reset_script : public WorldScript
{
public:
    weekly_reset_script() : WorldScript("weekly_reset_script") { }

	void OnStartup() override
    {
        g_TaskMap.clear();

        QueryResult r = CharacterDatabase.Query(
            "SELECT task_id, task_type "
            "FROM appdata.daily_tasks "
            "WHERE active = 1"
        );

        if (!r)
            return;

        do
        {
            Field* f = r->Fetch();
            uint32 taskId = f[0].GetUInt32();
            std::string type = f[1].GetString();
            g_TaskMap[type].push_back(taskId);
        }
        while (r->NextRow());
    }

    void OnUpdate(uint32 diff) override
    {
        static uint32 checkTimer = 0;
        checkTimer += diff;

        // 1분마다 체크
        if (checkTimer < 60000)
            return;

        checkTimer = 0;

        time_t now = time(nullptr);
        tm localTm;
        localtime_r(&now, &localTm);

        // 목요일 (0=일, 4=목)
        if (localTm.tm_wday == 4 &&
            localTm.tm_hour == 4 &&
            localTm.tm_min == 0)
        {
            ResetWeeklyData();
        }

		SessionMap const& sessions = sWorld->GetAllSessions();
        for (auto const& itr : sessions)
        {
            Player* player = itr.second->GetPlayer();
            if (!player)
                continue;

			uint32 charGuid = player->GetGUID().GetCounter();

			bool inLfg = player->isUsingLfg(); // 코어에 존재함

			if (inLfg && !g_LfgQueued.count(charGuid))
			{
				AddDailyProgress(player, "lfg_enter", 1);
				g_LfgQueued.insert(charGuid);
			}

			// 플레이어 사망 시 초기화
			// 🔽 사망 상태 감지 → 연속킬 초기화
			if (!player->IsAlive())
				g_KillStreak[charGuid] = 0;

			// 이미 완료했으면 체크 안함
			if (g_FriendPartyDone.count(charGuid))
				return;

			Group* group = player->GetGroup();
			if (!group)
			{
				g_FriendPartyStart.erase(charGuid);
				return;
			}

			bool hasFriendInGroup = false;

			for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
			{
				Player* member = itr->GetSource();
				if (!member || member == player)
					continue;
				
				AddDailyProgress(player, "party_play", 1);

				if (player->GetSocial()->HasFriend(member->GetGUID()))
				{
					hasFriendInGroup = true;
					break;
				}
			}

			if (!hasFriendInGroup)
			{
				g_FriendPartyStart.erase(charGuid);
				return;
			}

			time_t now = time(nullptr);

			if (!g_FriendPartyStart.count(charGuid))
				g_FriendPartyStart[charGuid] = now;

			if (now - g_FriendPartyStart[charGuid] >= 1800) // 30분
			{
				AddDailyProgress(player, "party_friend", 1);
				g_FriendPartyDone.insert(charGuid);
				g_FriendPartyStart.erase(charGuid);
			}
		}
    }

private:

    void ResetWeeklyData()
    {
        static bool alreadyReset = false;

        if (alreadyReset)
            return;

        alreadyReset = true;

        // 서버 목표 초기화
        CharacterDatabase.Execute(
            "UPDATE appdata.weekly_server_goals "
            "SET current_count = 0, completed = 0"
        );

        // 개인 기여도 초기화
        CharacterDatabase.Execute(
            "DELETE FROM appdata.weekly_goal_contributions"
        );

        // 다음 분부터 다시 허용
        // 1분 후 자동 해제
        std::thread([](){
            std::this_thread::sleep_for(std::chrono::minutes(1));
            alreadyReset = false;
        }).detach();
    }
};


void AddSC_weekly_delivery_vendor()
{
    new DeliveryVendorScript();

	new WeeklyAuctionGoalScript();

	new weekly_reset_script();

}

/*
SELECT guid, account, name, level, logout_time,
       FROM_UNIXTIME(logout_time) AS last_logout
FROM characters.characters
WHERE logout_time > 0
  AND logout_time < UNIX_TIMESTAMP(DATE_SUB(NOW(), INTERVAL 6 MONTH))
ORDER BY logout_time ASC;
*/