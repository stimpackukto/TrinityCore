// Custom/discord_daily_event.cpp
// TrinityCore 3.3.5a

#include "ScriptMgr.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "Random.h"
#include "DiscordEvent.h"

#include <ctime>
#include <string>

namespace
{
    static constexpr uint32 kTickMs = 60 * 1000;
    static constexpr int32  kMinReward = 2000;

    static constexpr char const* kEventUrl = "https://stimpack.pe.kr/auth/register";
    static constexpr int kDiscordColor = 16759040;

    struct DailyRow
    {
        uint16 fireHHMM = 0;
        uint8  fired    = 0;
        uint8  posted   = 0;
        std::string code;
        uint32 rewardRP = 0;
    };

    static std::tm GetLocalTm(time_t t)
    {
        std::tm tmLocal{};
#if defined(_WIN32)
        localtime_s(&tmLocal, &t);
#else
        localtime_r(&t, &tmLocal);
#endif
        return tmLocal;
    }

    static std::string DateString(std::tm const& tmLocal)
	{
		char buf[16]{};
		std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
			tmLocal.tm_year + 1900,
			tmLocal.tm_mon + 1,
			tmLocal.tm_mday);
		return std::string(buf);
	}

    static uint16 HHMM(std::tm const& tmLocal)
    {
        return uint16(tmLocal.tm_hour * 100 + tmLocal.tm_min);
    }

    static uint16 MakeRandomFireHHMM()
    {
        uint32 offsetMin = urand(0, 780);
        uint32 totalMin  = 540 + offsetMin;

        uint32 hour = totalMin / 60;
        uint32 min  = totalMin % 60;

        return uint16(hour * 100 + min);
    }

    static std::string MakeRandomCode()
    {
        static char const* kChars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        static constexpr uint32 kLen = 6;

        std::string s;
        s.reserve(5 + kLen);
        s.append("STIM-");

        for (uint32 i = 0; i < kLen; ++i)
            s.push_back(kChars[urand(0, 31)]);

        return s;
    }

    static bool LoadTodayRow(std::string const& dateStr, DailyRow& out)
    {
        QueryResult r = CharacterDatabase.PQuery(
            "SELECT fire_hhmm, fired, posted, code, reward_rp "
            "FROM appdata.discord_daily_event "
            "WHERE event_date='{}' "
            "LIMIT 1",
            dateStr
        );

        if (!r)
            return false;

        Field* f = r->Fetch();
        out.fireHHMM = f[0].GetUInt16();
        out.fired    = f[1].GetUInt8();
        out.posted   = f[2].GetUInt8();
        out.code     = f[3].GetString();
        out.rewardRP = f[4].GetUInt32();
        return true;
    }

    // 🔥 INSERT 후 재조회 안 하는 안정 버전
    static bool EnsureTodayRow(std::string const& dateStr, DailyRow& out)
    {
        if (LoadTodayRow(dateStr, out))
            return true;

        uint16 fireHHMM = MakeRandomFireHHMM();
        std::string code = MakeRandomCode();

        CharacterDatabase.PExecute(
            "INSERT IGNORE INTO appdata.discord_daily_event "
            "(event_date, fire_hhmm, code, reward_rp, posted, fired, claims_json) "
            "VALUES ('{}', {}, '{}', {}, 0, 0, '{{\"accounts\":[]}}')",
            dateStr, uint32(fireHHMM), code, kMinReward
        );

        // 🔥 DB 재조회 없이 바로 세팅
        out.fireHHMM = fireHHMM;
        out.fired    = 0;
        out.posted   = 0;
        out.code     = code;
        out.rewardRP = kMinReward;

        TC_LOG_INFO("custom.daily_event",
            "[discord_daily_event] created daily row {} fire_hhmm={}",
            dateStr, fireHHMM);

        return true;
    }

    static void MarkFired(std::string const& dateStr)
    {
        CharacterDatabase.PExecute(
            "UPDATE appdata.discord_daily_event "
            "SET fired=1 "
            "WHERE event_date='{}' AND fired=0",
            dateStr
        );

        TC_LOG_INFO("custom.daily_event",
            "[discord_daily_event] fired=1 date={}",
            dateStr);
    }

    static void TryPostDiscord(std::string const& dateStr, DailyRow const& row)
    {
        if (row.fired == 0)
            return;

        if (row.posted != 0)
            return;

        CharacterDatabase.PExecute(
            "UPDATE appdata.discord_daily_event "
            "SET posted=1 "
            "WHERE event_date='{}' AND fired=1 AND posted=0",
            dateStr
        );

        std::string title = "오늘의 이벤트 코드";
        std::string body =
            "코드: " + row.code + "\n"
            "웹에서 입력 시 RP " + std::to_string(row.rewardRP) + " (하루 1회)";

        SendServerDiscordEvent(title, body, kEventUrl, kDiscordColor, "event");

        TC_LOG_INFO("custom.daily_event",
            "[discord_daily_event] discord posted date={} code={}",
            dateStr, row.code);
    }

    class DiscordDailyEventWorldScript final : public WorldScript
    {
    public:
        DiscordDailyEventWorldScript()
            : WorldScript("DiscordDailyEventWorldScript") { }

        void OnStartup() override
        {
            _accMs = kTickMs;
        }

        void OnUpdate(uint32 diff) override
        {
            _accMs += diff;
            if (_accMs < kTickMs)
                return;

            _accMs = 0;

            time_t now = GameTime::GetGameTime();
            std::tm tmLocal = GetLocalTm(now);

            std::string today = DateString(tmLocal);
            uint16 nowHHMM = HHMM(tmLocal);

            DailyRow row;
            if (!EnsureTodayRow(today, row))
                return;

            if (row.fired == 0 && nowHHMM >= row.fireHHMM)
            {
                MarkFired(today);
                row.fired = 1; // 🔥 DB 재조회 없이 반영
            }

            TryPostDiscord(today, row);
        }

    private:
        uint32 _accMs = 0;
    };
}

void AddSC_discord_daily_event()
{
    new DiscordDailyEventWorldScript();
}