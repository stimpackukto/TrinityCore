// Custom/daily_continent_boss_quests.cpp
// - BossCodexData.h 기반: 대륙/던전/보스(encounter_index) 정보로 "대륙별 보스 풀" 구성
// - 3개 퀘스트(99999/99998/99997) 각각 독립 운영
//   - 매일 09:00~21:00 랜덤 시각에 공지 + 그 순간 4보스 확정
//   - 23:59:59까지 유효
//   - 4보스 처치 완료 시 퀘스트 자동 완료 처리
//
// [NEW] 테스트 모드:
//   - TEST_MODE = true 일 때,
//     서버 기동 후 5분 뒤(지연) 공지 + 그 순간 4보스 확정
//   - (요청 반영) 서버 재시작 시 "이미 오늘자 정보가 있으면" 갱신/리셋하지 않고 그대로 사용
//     오늘자 정보가 "없을 때만" 생성
//
// 주의: 코어 수정 없이 Custom 스크립트만 사용
//
// 필요 DB:
//  - appdata.daily_boss_quest_state
//  - appdata.daily_boss_quest_progress

#include "ScriptMgr.h"
#include "Player.h"
#include "Creature.h"
#include "ObjectMgr.h"
#include "World.h"
#include "GameTime.h"
#include "DatabaseEnv.h"
#include "Chat.h"

#include "BossCodexData.h" // <-- 사용자가 만든 헤더

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include <string>
#include <sstream>
#include <algorithm>

namespace DailyContinentBoss
{
    static constexpr uint32 QUEST_AZEROTH   = 99999;
    static constexpr uint32 QUEST_OUTLAND   = 99998;
    static constexpr uint32 QUEST_NORTHREND = 99997;

    static constexpr uint32 ANNOUNCE_START_HOUR = 9;   // 09:00
    static constexpr uint32 ANNOUNCE_END_HOUR   = 21;  // 21:00 (포함)
    static constexpr uint32 PICK_COUNT          = 4;

    // =========================
    // [NEW] TEST MODE SWITCH
    // =========================
    static constexpr bool   TEST_MODE                 = false;    // <-- 운영시 false
    static constexpr uint32 TEST_START_DELAY_SEC      = 5u * 60u; // 서버온 후 5분
    static constexpr uint32 TEST_EXPIRE_AFTER_SEC     = 0u;       // 0이면 "오늘 23:59:59", 아니면 now+N초

    enum class Region : uint8
    {
        Azeroth   = 0, // eastern + kalimdor
        Outland   = 1, // outland
        Northrend = 2, // northrend
    };

    struct QuestRuntimeState
    {
        uint32 questId = 0;
        Region region  = Region::Azeroth;

        uint32 dateYmd = 0;         // YYYYMMDD
        time_t announceTs = 0;      // epoch
        time_t expireTs   = 0;      // epoch
        bool announced    = false;

        std::array<uint32, PICK_COUNT> bosses = {0,0,0,0}; // creature entry
    };

    // -----------------------------
    // BossCodexData -> lookup maps
    // -----------------------------
    static std::unordered_map<uint32, std::string_view> gInstanceToContinentKey;
    static std::unordered_map<uint32, std::string_view> gEntryToName; // entry -> boss name(첫 매칭)
    static std::vector<uint32> gPoolAzeroth;
    static std::vector<uint32> gPoolOutland;
    static std::vector<uint32> gPoolNorthrend;

    static bool gPoolsBuilt = false;

    static Region ContinentKeyToRegion(std::string_view key)
    {
        // BossCodexData.h: "eastern", "kalimdor", "outland", "northrend"
        if (key == "outland")
            return Region::Outland;
        if (key == "northrend")
            return Region::Northrend;
        // eastern/kalimdor는 Azeroth로 묶음
        return Region::Azeroth;
    }

    static void BuildPoolsOnce()
    {
        if (gPoolsBuilt)
            return;

        gPoolsBuilt = true;

        // instance_id -> continent_key
        for (auto const& inst : BossCodexData::kInstances)
            gInstanceToContinentKey[inst.instance_id] = inst.continent_key;

        auto pushUnique = [](std::vector<uint32>& vec, uint32 entry)
        {
            if (!entry)
                return;
            // entry 중복 제거
            if (std::find(vec.begin(), vec.end(), entry) == vec.end())
                vec.push_back(entry);
        };

        // bosses를 순회해서 entry 풀 구성
        for (auto const& b : BossCodexData::kBosses)
        {
            if (b.entry == 0u)
                continue; // entry 없는 건 제외

            auto it = gInstanceToContinentKey.find(b.instance_id);
            if (it == gInstanceToContinentKey.end())
                continue;

            Region r = ContinentKeyToRegion(it->second);

            // 이름 맵 (첫 매칭만)
            if (gEntryToName.find(b.entry) == gEntryToName.end())
                gEntryToName[b.entry] = b.name;

            switch (r)
            {
                case Region::Azeroth:   pushUnique(gPoolAzeroth, b.entry); break;
                case Region::Outland:   pushUnique(gPoolOutland, b.entry); break;
                case Region::Northrend: pushUnique(gPoolNorthrend, b.entry); break;
            }
        }
    }

    // -----------------------------
    // time helpers
    // -----------------------------
    static uint32 MakeDateYmd(time_t t)
    {
        tm lt{};
        localtime_r(&t, &lt);
        return uint32((lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday);
    }

    static time_t MakeTodayAt(time_t now, uint32 hour, uint32 min, uint32 sec)
    {
        tm lt{};
        localtime_r(&now, &lt);
        lt.tm_hour = int(hour);
        lt.tm_min  = int(min);
        lt.tm_sec  = int(sec);
        return mktime(&lt);
    }

    static time_t RandomAnnounceTs(time_t now)
    {
        // [NEW] 테스트 모드면: "서버온 후 5분" 고정
        if (TEST_MODE)
            return now + TEST_START_DELAY_SEC;

        // 09:00:00 ~ 21:59:59 랜덤(분/초 포함)
        time_t start = MakeTodayAt(now, ANNOUNCE_START_HOUR, 0, 0);
        time_t end   = MakeTodayAt(now, ANNOUNCE_END_HOUR, 59, 59);

        if (end <= start)
            return start;

        uint32 span = uint32(end - start);
        return start + urand(0u, span);
    }

    static time_t TodayExpireTs(time_t now)
    {
        // [NEW] 테스트 모드에서 원하면 now+N초로 만료도 짧게 할 수 있게
        if (TEST_MODE && TEST_EXPIRE_AFTER_SEC > 0u)
            return now + TEST_EXPIRE_AFTER_SEC;

        return MakeTodayAt(now, 23, 59, 59);
    }

    // -----------------------------
    // DB helpers
    // -----------------------------
    static bool StateRowExists(uint32 dateYmd, uint32 questId) // [CHG]
    {
        QueryResult r = CharacterDatabase.PQuery(
            "SELECT 1 FROM appdata.daily_boss_quest_state WHERE date_ymd = {} AND quest_id = {} LIMIT 1",
            dateYmd, questId);
        return !!r;
    }

    static void EnsureStateRow(uint32 dateYmd, uint32 questId, time_t announceTs, time_t expireTs)
    {
        // [CHG] 테스트 모드: "이미 오늘자 정보가 있으면" 절대 갱신/리셋하지 않고 그대로 사용
        if (TEST_MODE)
        {
            if (StateRowExists(dateYmd, questId))
                return; // <-- 기존 정보 유지

            // 없을 때만 생성
            CharacterDatabase.PExecute(
                "INSERT INTO appdata.daily_boss_quest_state "
                "(date_ymd, quest_id, announce_ts, expire_ts, announced, boss1, boss2, boss3, boss4) "
                "VALUES ({}, {}, {}, {}, 0, 0, 0, 0, 0)",
                dateYmd, questId, uint32(announceTs), uint32(expireTs));
            return;
        }

        // 운영 모드: 기존 스케줄 유지(중복이면 보존)
        CharacterDatabase.PExecute(
            "INSERT INTO appdata.daily_boss_quest_state "
            "(date_ymd, quest_id, announce_ts, expire_ts, announced, boss1, boss2, boss3, boss4) "
            "VALUES ({}, {}, {}, {}, 0, 0, 0, 0, 0) "
            "ON DUPLICATE KEY UPDATE "
            "announce_ts = announce_ts, expire_ts = expire_ts",
            dateYmd, questId, uint32(announceTs), uint32(expireTs));
    }

    static bool LoadStateRow(uint32 dateYmd, uint32 questId, QuestRuntimeState& out)
    {
        QueryResult r = CharacterDatabase.PQuery(
            "SELECT announce_ts, expire_ts, announced, boss1, boss2, boss3, boss4 "
            "FROM appdata.daily_boss_quest_state "
            "WHERE date_ymd = {} AND quest_id = {}",
            dateYmd, questId);

        if (!r)
            return false;

        Field* f = r->Fetch();
        out.announceTs = time_t(f[0].GetUInt32());
        out.expireTs   = time_t(f[1].GetUInt32());
        out.announced  = (f[2].GetUInt8() != 0);

        out.bosses[0] = f[3].GetUInt32();
        out.bosses[1] = f[4].GetUInt32();
        out.bosses[2] = f[5].GetUInt32();
        out.bosses[3] = f[6].GetUInt32();
        return true;
    }

    static void SaveAnnouncedBosses(uint32 dateYmd, uint32 questId, std::array<uint32,4> const& bosses)
    {
        CharacterDatabase.PExecute(
            "UPDATE appdata.daily_boss_quest_state "
            "SET announced = 1, boss1 = {}, boss2 = {}, boss3 = {}, boss4 = {} "
            "WHERE date_ymd = {} AND quest_id = {}",
            bosses[0], bosses[1], bosses[2], bosses[3], dateYmd, questId);
    }

    static void UpsertProgress(uint32 dateYmd, uint32 guid, uint32 questId, uint32 bossEntry, time_t now)
{
    CharacterDatabase.PExecute(
        "INSERT INTO appdata.daily_boss_quest_progress "
        "(date_ymd, guid, quest_id, boss_entry, done, done_ts) "
        "VALUES ({}, {}, {}, {}, 1, {}) "
        "ON DUPLICATE KEY UPDATE done = 1, done_ts = GREATEST(done_ts, VALUES(done_ts))",
        dateYmd, guid, questId, bossEntry, uint32(now));
}

static uint32 CountDone(uint32 dateYmd, uint32 guid, uint32 questId)
{
    QueryResult r = CharacterDatabase.PQuery(
        "SELECT COUNT(*) FROM appdata.daily_boss_quest_progress "
        "WHERE date_ymd = {} AND guid = {} AND quest_id = {} AND done = 1",
        dateYmd, guid, questId);

    if (!r)
        return 0;

    return r->Fetch()[0].GetUInt32();
}

    // -----------------------------
    // pick helpers
    // -----------------------------
    static std::vector<uint32> const& GetPool(Region r)
    {
        switch (r)
        {
            case Region::Outland:   return gPoolOutland;
            case Region::Northrend: return gPoolNorthrend;
            default:                return gPoolAzeroth;
        }
    }

    static std::array<uint32,4> PickRandom4(Region r)
    {
        std::vector<uint32> const& pool = GetPool(r);
        std::array<uint32,4> res = {0,0,0,0};

        if (pool.size() <= 4)
        {
            for (uint32 i = 0; i < 4 && i < pool.size(); ++i)
                res[i] = pool[i];
            return res;
        }

        std::unordered_set<uint32> picked;
        for (uint32 i = 0; i < 4; ++i)
        {
            for (uint32 tries = 0; tries < 1000; ++tries)
            {
                uint32 e = pool[urand(0u, uint32(pool.size() - 1))];
                if (e != 0u && picked.insert(e).second)
                {
                    res[i] = e;
                    break;
                }
            }
        }
        return res;
    }

    static std::string BossName(uint32 entry)
    {
        auto it = gEntryToName.find(entry);
        if (it != gEntryToName.end())
            return std::string(it->second);

        if (CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(entry))
            return std::string(ct->Name);

        return std::string("Entry ") + std::to_string(entry);
    }

    static std::string BossListString(std::array<uint32,4> const& bosses)
    {
        std::ostringstream oss;
        for (uint32 i = 0; i < bosses.size(); ++i)
        {
            if (i) oss << ", ";
            oss << BossName(bosses[i]);
        }
        return oss.str();
    }

    static void Announce(uint32 questId, std::array<uint32,4> const& bosses)
    {
        std::string title;
        if (questId == QUEST_AZEROTH)   title = "아제로스";
        if (questId == QUEST_OUTLAND)   title = "아웃랜드";
        if (questId == QUEST_NORTHREND) title = "노스랜드";

        std::ostringstream oss;
        oss << "[일일 보스 퀘스트] 오늘의 " << title << " 타겟(퀘스트 " << questId << "): "
            << BossListString(bosses) << "  (23:59까지 유효)";

        sWorld->SendServerMessage(SERVER_MSG_STRING, oss.str().c_str());
    }

    // -----------------------------
    // runtime manager
    // -----------------------------
    class Manager
    {
    public:
        void Init()
        {
            BuildPoolsOnce();
            ReloadToday(true);
        }

        void Update()
        {
            time_t now = GameTime::GetGameTime();

            uint32 today = MakeDateYmd(now);
            if (today != _dateYmd)
            {
                ReloadToday(false);
                return;
            }

            for (auto& st : _states)
            {
                if (st.dateYmd != today)
                    continue;

                if (!st.announced && now >= st.announceTs && now <= st.expireTs)
                {
                    st.bosses = PickRandom4(st.region);
                    st.announced = true;

                    SaveAnnouncedBosses(st.dateYmd, st.questId, st.bosses);
                    Announce(st.questId, st.bosses);
                }
            }
        }

        void OnBossKilled(Player* killer, Creature* victim)
        {
            if (!killer || !victim)
                return;

            time_t now = GameTime::GetGameTime();
            uint32 today = MakeDateYmd(now);

            if (today != _dateYmd)
                return;

            uint32 entry = victim->GetEntry();
            if (!entry)
                return;

            for (auto& st : _states)
            {
                if (!st.announced)
                    continue;

                if (now < st.announceTs || now > st.expireTs)
                    continue;

                bool isTarget = false;
                for (uint32 e : st.bosses)
                {
                    if (e == entry)
                    {
                        isTarget = true;
                        break;
                    }
                }

                if (!isTarget)
                    continue;

                uint32 guidLow = killer->GetGUID().GetCounter(); // ★ 32비트 guidLow로 통일

				// 1) 즉시 반영(동기) INSERT/UPSERT
				std::string up = Trinity::StringFormat(
					"INSERT INTO appdata.daily_boss_quest_progress "
					"(date_ymd, guid, quest_id, boss_entry, done, done_ts) "
					"VALUES ({}, {}, {}, {}, 1, {}) "
					"ON DUPLICATE KEY UPDATE done=1, done_ts=GREATEST(done_ts, {})",
					st.dateYmd, guidLow, st.questId, entry, uint32(now), uint32(now));

				CharacterDatabase.Execute(up.c_str());

				// 2) 즉시 조회(동기) COUNT
				std::string q = Trinity::StringFormat(
					"SELECT COUNT(*) "
					"FROM appdata.daily_boss_quest_progress "
					"WHERE date_ymd={} AND guid={} AND quest_id={} AND done=1",
					st.dateYmd, guidLow, st.questId);

				QueryResult r = CharacterDatabase.Query(q.c_str());
				uint32 done = 0;
				if (r)
					done = r->Fetch()[0].GetUInt32();

/*				std::string msg = Trinity::StringFormat("|cffABD473[done]|r [{}] .", done);
				sWorld->SendServerMessage(SERVER_MSG_STRING, msg.c_str(), 0);*/

				// 3) 4마리면 완료 처리 호출
				if (done >= 4)
					CompleteQuestIfPossible(killer, st.questId);
            }
        }

    private:
        void ReloadToday(bool /*onStartup*/)
        {
            time_t now = GameTime::GetGameTime();
            _dateYmd = MakeDateYmd(now);

            EnsureOne(_dateYmd, QUEST_AZEROTH,   Region::Azeroth,   now);
            EnsureOne(_dateYmd, QUEST_OUTLAND,   Region::Outland,   now);
            EnsureOne(_dateYmd, QUEST_NORTHREND, Region::Northrend, now);

            LoadOne(_dateYmd, QUEST_AZEROTH,   Region::Azeroth);
            LoadOne(_dateYmd, QUEST_OUTLAND,   Region::Outland);
            LoadOne(_dateYmd, QUEST_NORTHREND, Region::Northrend);
        }

        void EnsureOne(uint32 dateYmd, uint32 questId, Region /*region*/, time_t now)
        {
            time_t announce = RandomAnnounceTs(now);
            time_t expire   = TodayExpireTs(now);
            EnsureStateRow(dateYmd, questId, announce, expire);
        }

        void LoadOne(uint32 dateYmd, uint32 questId, Region region)
        {
            QuestRuntimeState st;
            st.questId = questId;
            st.region  = region;
            st.dateYmd = dateYmd;

            if (!LoadStateRow(dateYmd, questId, st))
            {
                time_t now = GameTime::GetGameTime();
                st.announceTs = RandomAnnounceTs(now);
                st.expireTs   = TodayExpireTs(now);
                st.announced  = false;
                st.bosses     = {0,0,0,0};
            }

            auto put = [&](uint32 idx)
            {
                if (_states.size() <= idx)
                    _states.resize(idx + 1);
                _states[idx] = st;
            };

            if (questId == QUEST_AZEROTH)   put(0);
            if (questId == QUEST_OUTLAND)   put(1);
            if (questId == QUEST_NORTHREND) put(2);
        }

        void CompleteQuestIfPossible(Player* player, uint32 questId)
        {
            if (!player)
                return;

            Quest const* q = sObjectMgr->GetQuestTemplate(questId);
            if (!q)
                return;

            if (player->GetQuestRewardStatus(questId))
                return;

            player->KilledMonsterCredit(questId, ObjectGuid::Empty);

            ChatHandler(player->GetSession()).SendSysMessage("[일일 보스 퀘스트] 퀘스트완료되었습니다");
        }

    private:
        uint32 _dateYmd = 0;
        std::vector<QuestRuntimeState> _states; // [0]=azeroth, [1]=outland, [2]=northrend
    };

    static Manager& GetMgr()
    {
        static Manager mgr;
        return mgr;
    }

    class DailyBossQuestWorldScript : public WorldScript
    {
    public:
        DailyBossQuestWorldScript() : WorldScript("DailyBossQuestWorldScript") {}

        void OnStartup() override
        {
            GetMgr().Init();

            if (TEST_MODE)
            {
                std::ostringstream oss;
                oss << "[일일 보스 퀘스트][TEST] 서버 기동 후 "
                    << (TEST_START_DELAY_SEC / 60) << "분 뒤 오늘의 타겟이 공지됩니다.";
                sWorld->SendServerMessage(SERVER_MSG_STRING, oss.str().c_str());
            }
        }

        void OnUpdate(uint32 /*diff*/) override
        {
            static time_t last = 0;
            time_t now = GameTime::GetGameTime();
            if (now == last)
                return;

            if ((now - last) < 5)
                return;

            last = now;
            GetMgr().Update();
        }
    };

    class DailyBossQuestPlayerScript : public PlayerScript
    {
    public:
        DailyBossQuestPlayerScript() : PlayerScript("DailyBossQuestPlayerScript") {}

        void OnCreatureKill(Player* killer, Creature* killed) override
        {
            GetMgr().OnBossKilled(killer, killed);
        }
    };
}

void AddSC_daily_continent_boss_quests()
{
  //  new DailyContinentBoss::DailyBossQuestWorldScript();
 //   new DailyContinentBoss::DailyBossQuestPlayerScript();
}
