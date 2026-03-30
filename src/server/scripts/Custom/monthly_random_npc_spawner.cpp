// Custom/monthly_random_npc_spawner.cpp
// 매달 4번 랜덤 시각에 이벤트 발생 -> 두 NPC 중 하나만 랜덤 소환
// - 같은날 중복 금지
// - 같은 주(월 기준 주차) 중복 금지
// - 랜덤 시간대: 17:00~23:59
// - 유지시간: 3시간
// - 월별 스케줄은 DB(appdata.monthly_random_spawns)에 저장 -> 재시작에도 유지
// - 좌표/맵/엔트리는 TODO 자리만 마련 (프리티우먼님이 추후 입력)

#include "ScriptMgr.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Random.h"
#include "SharedDefines.h"
#include "Utilities/Util.h"
#include "WorldSession.h"   // SendAreaTriggerMessage 용
#include "World.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
    // =========================
    // [NEW] 모드 선택
    // =========================
    enum class SpawnMode : uint8
    {
        Monthly = 0, // 월 4회 스케줄
        Test    = 1  // 서버 ON 후 60초 뒤 1회 스폰
    };

    // [NEW] 여기서 선택
    static constexpr SpawnMode MODE = SpawnMode::Monthly;

    // [NEW] 테스트 모드: 서버 기동 후 몇 초 뒤 스폰?
    static constexpr uint32 TEST_SPAWN_DELAY_SEC = 60;

    // =========================
    // 설정 값들
    // =========================
    static constexpr uint32 NPC_ENTRY_A = 190010;
    static constexpr uint32 NPC_ENTRY_B = 190011;

    static constexpr uint32 SPAWN_MAP_ID = 571;
    static constexpr uint32 DESPAWN_SECONDS = 3 * 60 * 60; // 3시간

    static constexpr int32 RAND_HOUR_FROM = 17;
    static constexpr int32 RAND_HOUR_TO   = 23;

    static std::vector<Position> g_SpawnPositions = {
        { 5806.313f, 504.565f, 657.576f, 5.413046f },
    };

    static constexpr char const* TABLE_NAME = "appdata.monthly_random_spawns";
    static constexpr uint32 UPDATE_INTERVAL_MS = 60 * IN_MILLISECONDS;

    struct SlotRow
    {
        uint8  slot = 0;
        uint64 ts = 0;
        bool   executed = false;
    };

    // ─────────────────────────────
    // 시간 유틸
    // ─────────────────────────────
    static int32 MakeMonthKey(std::tm const& t)
    {
        int32 year  = (t.tm_year + 1900);
        int32 month = (t.tm_mon + 1);
        return year * 100 + month;
    }

    static std::pair<time_t, time_t> GetMonthRangeLocal(time_t now)
    {
        std::tm lt{};
        localtime_r(&now, &lt);

        std::tm start = lt;
        start.tm_mday = 1;
        start.tm_hour = 0;
        start.tm_min  = 0;
        start.tm_sec  = 0;

        time_t monthStart = mktime(&start);

        std::tm next = start;
        next.tm_mon += 1;
        time_t nextMonthStart = mktime(&next);

        return { monthStart, nextMonthStart };
    }

    static int32 DaysInMonthLocal(time_t monthStart)
    {
        std::tm t{};
        localtime_r(&monthStart, &t);

        std::tm next = t;
        next.tm_mon += 1;
        time_t nextStart = mktime(&next);

        int64 sec = int64(nextStart) - int64(monthStart);
        int32 days = int32(sec / (24 * 60 * 60));
        return std::max<int32>(days, 28);
    }

    static int32 FirstDayWeekdayMon0(time_t monthStart)
    {
        std::tm t{};
        localtime_r(&monthStart, &t);
        return (t.tm_wday + 6) % 7;
    }

    static int32 WeekIndexInMonth(int32 dayOfMonth, int32 firstDayWeekday_Mon0)
    {
        return ((dayOfMonth - 1 + firstDayWeekday_Mon0) / 7) + 1;
    }

    static time_t Clamp(time_t v, time_t lo, time_t hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    static uint64 MakeRandomTsInWindow(time_t monthStart, int32 dayOfMonth)
    {
        std::tm base{};
        localtime_r(&monthStart, &base);

        base.tm_mday = dayOfMonth;
        base.tm_hour = 0;
        base.tm_min  = 0;
        base.tm_sec  = 0;

        time_t dayStart = mktime(&base);

        int32 hour = urand(RAND_HOUR_FROM, RAND_HOUR_TO);
        int32 min  = urand(0, 59);
        int32 sec  = urand(0, 59);

        time_t ts = dayStart + (hour * 3600) + (min * 60) + sec;
        return static_cast<uint64>(ts);
    }

    class MonthlyRandomNpcSpawner
    {
    public:
        void OnStartup()
        {
            if (g_SpawnPositions.empty())
            {
                TC_LOG_ERROR("scripts", "[MonthlySpawner] SpawnPositions is empty. Nothing will spawn.");
                return;
            }

            time_t now = static_cast<time_t>(GameTime::GetGameTime());

            // [NEW] TEST 모드: 서버 기동 후 60초 뒤 1회 스폰만
            if (MODE == SpawnMode::Test)
            {
                _testSpawnAt = static_cast<uint64>(now + TEST_SPAWN_DELAY_SEC);
                _testSpawned = false;
                TC_LOG_INFO("scripts", "[MonthlySpawner][TEST] Will spawn once at +{}s (ts={})",
                    TEST_SPAWN_DELAY_SEC, _testSpawnAt);
                return;
            }

            // 월간 모드
            LoadOrCreateMonthSchedule();
            ComputeNextDue();
        }

        void Update(uint32 diff)
        {
            if (g_SpawnPositions.empty())
                return;

            _elapsedMs += diff;
            if (_elapsedMs < UPDATE_INTERVAL_MS)
                return;
            _elapsedMs = 0;

            time_t now = static_cast<time_t>(GameTime::GetGameTime());

            // [NEW] TEST 모드: 1회 스폰만 하고 종료
            if (MODE == SpawnMode::Test)
            {
                if (!_testSpawned && _testSpawnAt != 0 && static_cast<uint64>(now) >= _testSpawnAt)
                {
                    bool ok = SpawnOne();
                    _testSpawned = true;

                    TC_LOG_INFO("scripts", "[MonthlySpawner][TEST] Spawn executed ok={}", ok ? 1 : 0);
                }
                return;
            }

            // 월간 모드
            if (IsNewMonth(now))
            {
                LoadOrCreateMonthSchedule();
                ComputeNextDue();
            }

            if (_nextDueTs == 0)
                return;

            if (static_cast<uint64>(now) >= _nextDueTs)
            {
                ExecuteDueSpawns(now);
                ComputeNextDue();
            }
        }

    private:
        bool IsNewMonth(time_t now)
        {
            std::tm lt{};
            localtime_r(&now, &lt);
            int32 mk = MakeMonthKey(lt);
            return mk != _currentMonthKey;
        }

        void LoadOrCreateMonthSchedule()
        {
            time_t now = static_cast<time_t>(GameTime::GetGameTime());

            std::tm lt{};
            localtime_r(&now, &lt);
            _currentMonthKey = MakeMonthKey(lt);

            std::vector<SlotRow> rows = LoadRows(_currentMonthKey);

            if (rows.size() < 4)
            {
                CreateMonthRows(_currentMonthKey, now);
                rows = LoadRows(_currentMonthKey);
            }

            _slots = rows;

            TC_LOG_INFO("scripts", "[MonthlySpawner] Loaded schedule: month_key={} rows={}", _currentMonthKey, _slots.size());
        }

        std::vector<SlotRow> LoadRows(int32 monthKey)
        {
            std::vector<SlotRow> out;

            std::string q = Trinity::StringFormat(
                "SELECT slot, spawn_ts, executed "
                "FROM {} "
                "WHERE month_key = {} "
                "ORDER BY slot ASC",
                TABLE_NAME, monthKey
            );

            QueryResult res = WorldDatabase.Query(q.c_str());
            if (!res)
                return out;

            do
            {
                Field* f = res->Fetch();
                SlotRow r;

                r.slot = f[0].GetUInt8();
                r.ts   = f[1].GetUInt64();
                r.executed = (f[2].GetUInt8() != 0);

                out.push_back(r);
            }
            while (res->NextRow());

            return out;
        }

        void CreateMonthRows(int32 monthKey, time_t now)
        {
            auto [monthStart, nextMonthStart] = GetMonthRangeLocal(now);
            int32 daysInMonth = DaysInMonthLocal(monthStart);
            int32 firstWMon0  = FirstDayWeekdayMon0(monthStart);

            time_t minTs = std::max(monthStart, now + 60);
            time_t maxTs = nextMonthStart - 1;

            auto existing = LoadRows(monthKey);

            std::set<uint8> usedSlots;
            std::unordered_set<int32> usedDays;
            std::unordered_set<int32> usedWeeks;

            for (auto const& r : existing)
            {
                usedSlots.insert(r.slot);

                time_t t = static_cast<time_t>(r.ts);
                std::tm lt{};
                localtime_r(&t, &lt);

                int32 day = lt.tm_mday;
                usedDays.insert(day);

                int32 weekIdx = WeekIndexInMonth(day, firstWMon0);
                usedWeeks.insert(weekIdx);
            }

            for (uint8 slot = 1; slot <= 4; ++slot)
            {
                if (usedSlots.count(slot))
                    continue;

                uint64 pickedTs = 0;

                for (int tries = 0; tries < 2000; ++tries)
                {
                    int32 day = urand(1, uint32(daysInMonth));
                    if (usedDays.count(day))
                        continue;

                    int32 weekIdx = WeekIndexInMonth(day, firstWMon0);
                    if (usedWeeks.count(weekIdx))
                        continue;

                    uint64 ts = MakeRandomTsInWindow(monthStart, day);
                    time_t cand = static_cast<time_t>(ts);

                    if (cand < minTs)
                        continue;
                    if (cand >= nextMonthStart)
                        continue;

                    pickedTs = static_cast<uint64>(Clamp(cand, minTs, maxTs));

                    usedDays.insert(day);
                    usedWeeks.insert(weekIdx);
                    break;
                }

                if (pickedTs == 0)
                {
                    for (int32 day = 1; day <= daysInMonth; ++day)
                    {
                        if (usedDays.count(day))
                            continue;

                        int32 weekIdx = WeekIndexInMonth(day, firstWMon0);
                        if (usedWeeks.count(weekIdx))
                            continue;

                        uint64 ts = MakeRandomTsInWindow(monthStart, day);
                        time_t cand = static_cast<time_t>(ts);

                        if (cand < minTs) continue;
                        if (cand >= nextMonthStart) continue;

                        pickedTs = static_cast<uint64>(Clamp(cand, minTs, maxTs));
                        usedDays.insert(day);
                        usedWeeks.insert(weekIdx);
                        break;
                    }
                }

                if (pickedTs == 0)
                    pickedTs = static_cast<uint64>(now + 300);

                std::string ins = Trinity::StringFormat(
                    "INSERT IGNORE INTO {} (month_key, slot, spawn_ts, executed) "
                    "VALUES ({}, {}, {}, 0)",
                    TABLE_NAME, monthKey, slot, pickedTs
                );
                WorldDatabase.Execute(ins.c_str());
            }

            TC_LOG_INFO("scripts", "[MonthlySpawner] Created/filled schedule for month_key={} (no same day/week, 17~23)", monthKey);
        }

        void ComputeNextDue()
        {
            _nextDueTs = 0;

            for (auto const& r : _slots)
            {
                if (r.executed)
                    continue;

                if (_nextDueTs == 0 || r.ts < _nextDueTs)
                    _nextDueTs = r.ts;
            }
        }

        void ExecuteDueSpawns(time_t now)
        {
            auto rows = LoadRows(_currentMonthKey);

            for (auto const& r : rows)
            {
                if (r.executed)
                    continue;

                if (r.ts > static_cast<uint64>(now))
                    continue;

                bool ok = SpawnOne();

                std::string up = Trinity::StringFormat(
                    "UPDATE {} "
                    "SET executed = 1, last_entry = {}, last_map = {} "
                    "WHERE month_key = {} AND slot = {}",
                    TABLE_NAME, ok ? _lastSpawnEntry : 0, SPAWN_MAP_ID, _currentMonthKey, r.slot
                );
                WorldDatabase.Execute(up.c_str());

                TC_LOG_INFO("scripts", "[MonthlySpawner] Executed slot={} ts={} spawn_ok={}", r.slot, r.ts, ok ? 1 : 0);
            }

            _slots = LoadRows(_currentMonthKey);
        }

        bool SpawnOne()
        {
            if (g_SpawnPositions.empty())
                return false;

            uint32 entry = (urand(0, 1) == 0) ? NPC_ENTRY_A : NPC_ENTRY_B;
            _lastSpawnEntry = entry;

            Position pos = g_SpawnPositions[urand(0, static_cast<uint32>(g_SpawnPositions.size() - 1))];

            Map* map = sMapMgr->FindMap(SPAWN_MAP_ID, 0);
            if (!map)
            {
                TC_LOG_ERROR("scripts", "[MonthlySpawner] Map not loaded or not found: {} (instance 0)", SPAWN_MAP_ID);
                return false;
            }

            Creature* npc = map->SummonCreature(entry, pos);
			sWorld->SendServerMessage(SERVER_MSG_STRING,"|cffFF0000[장인 방문]|r 장비의 형상을 재연마하고, 숨겨진 외형을 일깨우는 비전 장인!", 0);

            if (!npc)
            {
                TC_LOG_ERROR("scripts", "[MonthlySpawner] SummonCreature failed. entry={}", entry);
                return false;
            }

            npc->DespawnOrUnsummon(Milliseconds(int64(DESPAWN_SECONDS) * IN_MILLISECONDS));

            TC_LOG_INFO("scripts", "[MonthlySpawner] Spawned entry={} map={} x={} y={} z={} (despawn {} sec)",
                entry, SPAWN_MAP_ID, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), DESPAWN_SECONDS);

            return true;
        }

    private:
        uint32 _elapsedMs = 0;

        // 월간 모드용
        int32 _currentMonthKey = 0;
        uint64 _nextDueTs = 0;
        std::vector<SlotRow> _slots;

        uint32 _lastSpawnEntry = 0;

        // [NEW] 테스트 모드용
        uint64 _testSpawnAt = 0;
        bool   _testSpawned = false;
    };

    static MonthlyRandomNpcSpawner g_Spawner;
}

class monthly_random_npc_spawner_world : public WorldScript
{
public:
    monthly_random_npc_spawner_world() : WorldScript("monthly_random_npc_spawner_world") { }

    void OnStartup() override
    {
        g_Spawner.OnStartup();
    }

    void OnUpdate(uint32 diff) override
    {
        g_Spawner.Update(diff);
    }
};

void AddSC_monthly_random_npc_spawner()
{
    new monthly_random_npc_spawner_world();
}
