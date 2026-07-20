#include "ScriptMgr.h"
#include "Player.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"

#include <sstream>
#include <string>

namespace CustomBattleKillLogger
{
    static constexpr uint32 WINTERGRASP_ZONE_ID = 4197;

    enum class KillType
    {
        None,
        Wintergrasp,
        Battleground
    };

    static char const* GetKillTypeDbValue(KillType type)
    {
        switch (type)
        {
            case KillType::Wintergrasp:
                return "wintergrasp";
            case KillType::Battleground:
                return "battleground";
            default:
                return "";
        }
    }

    static std::string GetWeekStartSql()
    {
        return
            "DATE_ADD("
                "DATE_SUB("
                    "DATE(NOW() - INTERVAL 4 HOUR), "
                    "INTERVAL ((WEEKDAY(NOW() - INTERVAL 4 HOUR) - 3 + 7) % 7) DAY"
                "), "
                "INTERVAL 4 HOUR"
            ")";
    }

    static uint32 GetAccountId(Player* player)
    {
        if (!player || !player->GetSession())
            return 0;

        return player->GetSession()->GetAccountId();
    }

    static uint64 GetCharacterGuid(Player* player)
    {
        if (!player)
            return 0;

        return player->GetGUID().GetCounter();
    }

    static bool IsArenaKill(Player* killer, Player* killed)
    {
        if (!killer || !killed)
            return false;

        return killer->InArena() || killed->InArena();
    }

    static bool IsWintergraspKill(Player* killer, Player* killed)
    {
        if (!killer || !killed)
            return false;

        return killer->GetZoneId() == WINTERGRASP_ZONE_ID || killed->GetZoneId() == WINTERGRASP_ZONE_ID;
    }

    static bool IsNormalBattlegroundKill(Player* killer, Player* killed)
    {
        if (!killer || !killed)
            return false;

        if (IsArenaKill(killer, killed))
            return false;

        return killer->InBattleground() || killed->InBattleground();
    }

    static uint32 GetBgTypeId(Player* killer, Player* killed)
    {
        if (killer && killer->InBattleground())
            return uint32(killer->GetBattlegroundTypeId());

        if (killed && killed->InBattleground())
            return uint32(killed->GetBattlegroundTypeId());

        return 0;
    }

    static KillType ResolveKillType(Player* killer, Player* killed)
    {
        if (!killer || !killed)
            return KillType::None;

        if (killer == killed)
            return KillType::None;

        if (GetCharacterGuid(killer) == 0 || GetCharacterGuid(killed) == 0)
            return KillType::None;

        if (GetCharacterGuid(killer) == GetCharacterGuid(killed))
            return KillType::None;

        if (IsArenaKill(killer, killed))
            return KillType::None;

        if (IsWintergraspKill(killer, killed))
            return KillType::Wintergrasp;

        if (IsNormalBattlegroundKill(killer, killed))
            return KillType::Battleground;

        return KillType::None;
    }

    static bool HasRecentCountedKill(uint64 killerGuid, uint64 victimGuid, KillType killType)
    {
        char const* killTypeText = GetKillTypeDbValue(killType);

        std::ostringstream sql;
        sql
            << "SELECT 1 "
            << "FROM appdata.pvp_kill_log "
            << "WHERE killer_guid = " << killerGuid << " "
            << "AND victim_guid = " << victimGuid << " "
            << "AND kill_type = '" << killTypeText << "' "
            << "AND is_counted = 1 "
            << "AND killed_at >= NOW() - INTERVAL 3 MINUTE "
            << "LIMIT 1";

        QueryResult result = LoginDatabase.Query(sql.str().c_str());
        return bool(result);
    }

    static uint32 GetWeeklyCountedKillsAgainstVictim(uint64 killerGuid, uint64 victimGuid, KillType killType)
    {
        char const* killTypeText = GetKillTypeDbValue(killType);
        std::string weekStartSql = GetWeekStartSql();

        std::ostringstream sql;
        sql
            << "SELECT COUNT(*) "
            << "FROM appdata.pvp_kill_log "
            << "WHERE killer_guid = " << killerGuid << " "
            << "AND victim_guid = " << victimGuid << " "
            << "AND kill_type = '" << killTypeText << "' "
            << "AND is_counted = 1 "
            << "AND week_start = " << weekStartSql;

        QueryResult result = LoginDatabase.Query(sql.str().c_str());

        if (!result)
            return 0;

        Field* fields = result->Fetch();

        if (!fields)
            return 0;

        return fields[0].GetUInt32();
    }

    static bool IsCountableKill(Player* killer, Player* killed, KillType killType, std::string& abuseReason)
    {
        abuseReason.clear();

        if (!killer || !killed || killType == KillType::None)
        {
            abuseReason = "invalid";
            return false;
        }

        uint64 killerGuid = GetCharacterGuid(killer);
        uint64 victimGuid = GetCharacterGuid(killed);

        uint32 killerAccount = GetAccountId(killer);
        uint32 victimAccount = GetAccountId(killed);

        if (killerGuid == 0 || victimGuid == 0 || killerGuid == victimGuid)
        {
            abuseReason = "invalid_guid";
            return false;
        }

        if (killerAccount != 0 && victimAccount != 0 && killerAccount == victimAccount)
        {
            abuseReason = "same_account";
            return false;
        }

        if (HasRecentCountedKill(killerGuid, victimGuid, killType))
        {
            abuseReason = "repeat_cooldown";
            return false;
        }

        uint32 weeklyVictimKills = GetWeeklyCountedKillsAgainstVictim(killerGuid, victimGuid, killType);

        if (weeklyVictimKills >= 5)
        {
            abuseReason = "weekly_victim_cap";
            return false;
        }

        return true;
    }

    static void InsertKillLog(Player* killer, Player* killed, KillType killType, bool isCounted, std::string const& abuseReason)
    {
        if (!killer || !killed || killType == KillType::None)
            return;

        uint64 killerGuid = GetCharacterGuid(killer);
        uint64 victimGuid = GetCharacterGuid(killed);

        uint32 killerAccount = GetAccountId(killer);
        uint32 victimAccount = GetAccountId(killed);

        uint32 bgTypeId = GetBgTypeId(killer, killed);
        uint32 mapId = killer->GetMapId();
        uint32 zoneId = killer->GetZoneId();
        uint32 areaId = killer->GetAreaId();

        char const* killTypeText = GetKillTypeDbValue(killType);
        std::string weekStartSql = GetWeekStartSql();

        std::ostringstream sql;

        sql
            << "INSERT INTO appdata.pvp_kill_log "
            << "("
                << "killer_guid, "
                << "killer_account, "
                << "victim_guid, "
                << "victim_account, "
                << "kill_type, "
                << "bg_type_id, "
                << "map_id, "
                << "zone_id, "
                << "area_id, "
                << "killed_at, "
                << "week_start, "
                << "is_counted, "
                << "abuse_reason"
            << ") VALUES ("
                << killerGuid << ", "
                << killerAccount << ", "
                << victimGuid << ", "
                << victimAccount << ", "
                << "'" << killTypeText << "', "
                << bgTypeId << ", "
                << mapId << ", "
                << zoneId << ", "
                << areaId << ", "
                << "NOW(), "
                << weekStartSql << ", "
                << (isCounted ? 1 : 0) << ", "
                << "'" << abuseReason << "'"
            << ")";

        LoginDatabase.Execute(sql.str().c_str());
    }

    static void UpsertWeeklyKill(Player* killer, KillType killType)
    {
        if (!killer || killType == KillType::None)
            return;

        uint64 killerGuid = GetCharacterGuid(killer);
        uint32 killerAccount = GetAccountId(killer);

        if (killerGuid == 0)
            return;

        char const* killTypeText = GetKillTypeDbValue(killType);
        std::string weekStartSql = GetWeekStartSql();

        std::ostringstream sql;

        sql
            << "INSERT INTO appdata.pvp_kill_weekly "
            << "("
                << "week_start, "
                << "kill_type, "
                << "character_guid, "
                << "account_id, "
                << "kills, "
                << "updated_at"
            << ") VALUES ("
                << weekStartSql << ", "
                << "'" << killTypeText << "', "
                << killerGuid << ", "
                << killerAccount << ", "
                << "1, "
                << "NOW()"
            << ") "
            << "ON DUPLICATE KEY UPDATE "
                << "kills = kills + 1, "
                << "account_id = VALUES(account_id), "
                << "updated_at = NOW()";

        LoginDatabase.Execute(sql.str().c_str());
    }

    static void LogBattleKill(Player* killer, Player* killed)
    {
        KillType killType = ResolveKillType(killer, killed);

        if (killType == KillType::None)
            return;

        std::string abuseReason;
        bool isCounted = IsCountableKill(killer, killed, killType, abuseReason);

        InsertKillLog(killer, killed, killType, isCounted, abuseReason);

        if (!isCounted)
            return;

        UpsertWeeklyKill(killer, killType);
    }
}

class custom_battle_kill_logger : public PlayerScript
{
public:
    custom_battle_kill_logger() : PlayerScript("custom_battle_kill_logger") { }

    void OnPVPKill(Player* killer, Player* killed) override
    {
        CustomBattleKillLogger::LogBattleKill(killer, killed);
    }
};

void AddSC_custom_battle_kill_logger()
{
    new custom_battle_kill_logger();
}