#include "CustomRP.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <atomic>
#include <exception>
#include <string>
#include <thread>

namespace
{
    static constexpr uint32 FRIEND_FCM_CATEGORY_ID = 7;
    static constexpr char const* FRIEND_FCM_CATEGORY_CODE = "7";
    static constexpr uint32 FRIEND_LOGIN_FCM_COOLDOWN_MINUTES = 30;

    static constexpr uint32 MAIL_FCM_CATEGORY_ID = 6;
    static constexpr char const* MAIL_FCM_CATEGORY_CODE = "6";
    static constexpr uint32 MAIL_FCM_INTERVAL_MS = 60 * 1000;
    static constexpr uint32 MAIL_FCM_LIMIT = 50;
    static constexpr uint8 MAIL_CHECK_MASK_READ_SQL = 1;

    std::atomic_bool g_MailArrivalFcmRunning{ false };

    std::string SqlEscape(std::string const& value)
    {
        std::string escaped;
        escaped.reserve(value.size() + 8);

        for (char ch : value)
        {
            switch (ch)
            {
                case '\0': escaped += "\\0"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\\': escaped += "\\\\"; break;
                case '\'': escaped += "\\'"; break;
                case '"':  escaped += "\\\""; break;
                case '\x1a': escaped += "\\Z"; break;
                default: escaped += ch; break;
            }
        }

        return escaped;
    }

    void NotifyFriendAccountsBackground(uint32 loginAccountId, uint32 loginCharacterGuid, std::string characterName)
    {
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT DISTINCT owner.account "
            "FROM character_social s "
            "INNER JOIN characters owner ON owner.guid = s.guid "
            "INNER JOIN lightguardian_community.fcm_device_token d "
            "    ON d.account_id = owner.account AND d.is_active = 1 AND d.fcm_token <> '' "
            "INNER JOIN lightguardian_community.fcm_user_preference p "
            "    ON p.account_id = owner.account AND p.is_enabled = 1 AND p.category_code IN ('7', 'friend') "
            "LEFT JOIN lightguardian_community.fcm_friend_login_cooldown cd "
            "    ON cd.target_account_id = owner.account AND cd.login_character_guid = {} "
            "WHERE s.friend = {} "
            "  AND (s.flags = 0 OR (s.flags & 1) <> 0) "
            "  AND owner.account <> {} "
            "  AND (cd.last_sent_at IS NULL OR cd.last_sent_at < DATE_SUB(NOW(), INTERVAL {} MINUTE)) "
            "ORDER BY owner.account",
            loginCharacterGuid,
            loginCharacterGuid,
            loginAccountId,
            FRIEND_LOGIN_FCM_COOLDOWN_MINUTES
        );

        if (!result)
            return;

        do
        {
            uint32 targetAccountId = result->Fetch()[0].GetUInt32();

            LoginDatabase.PExecute(
                "INSERT INTO lightguardian_community.fcm_friend_login_cooldown "
                "(target_account_id, login_character_guid, login_account_id, login_character_name, last_sent_at) "
                "VALUES ({}, {}, {}, '{}', NOW()) "
                "ON DUPLICATE KEY UPDATE login_account_id = VALUES(login_account_id), "
                "login_character_name = VALUES(login_character_name), last_sent_at = NOW()",
                targetAccountId,
                loginCharacterGuid,
                loginAccountId,
                SqlEscape(characterName)
            );

            SendFcmCategory(
                FRIEND_FCM_CATEGORY_ID,
                FRIEND_FCM_CATEGORY_CODE,
                "친구 접속 알림",
                Trinity::StringFormat("{} 님이 접속했습니다.", characterName),
                targetAccountId
            );
        } while (result->NextRow());
    }

    void QueueFriendAccountsNotify(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        uint32 loginAccountId = player->GetSession()->GetAccountId();
        uint32 loginCharacterGuid = uint32(player->GetGUID().GetCounter());
        std::string characterName = player->GetName();

        std::thread([loginAccountId, loginCharacterGuid, characterName]()
        {
            NotifyFriendAccountsBackground(loginAccountId, loginCharacterGuid, characterName);
        }).detach();
    }

    void RunArrivedMailFcmBackground()
    {
        uint64 now = uint64(GameTime::GetGameTime());

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT m.id, m.receiver, c.account, c.name, IFNULL(m.subject, '') "
            "FROM mail m "
            "INNER JOIN characters c ON c.guid = m.receiver "
            "LEFT JOIN lightguardian_community.fcm_mail_notify_log l ON l.mail_id = m.id "
            "WHERE m.deliver_time <= {} "
            "  AND m.expire_time > {} "
            "  AND (m.checked & {}) = 0 "
            "  AND l.mail_id IS NULL "
            "ORDER BY m.deliver_time ASC, m.id ASC "
            "LIMIT {}",
            now,
            now,
            uint32(MAIL_CHECK_MASK_READ_SQL),
            MAIL_FCM_LIMIT
        );

        if (result)
        {
            do
            {
                Field* fields = result->Fetch();

                uint32 mailId = fields[0].GetUInt32();
                uint32 receiverGuid = fields[1].GetUInt32();
                uint32 receiverAccountId = fields[2].GetUInt32();
                std::string subject = fields[4].GetString();

                CharacterDatabase.PExecute(
                    "INSERT IGNORE INTO lightguardian_community.fcm_mail_notify_log "
                    "(mail_id, receiver_guid, receiver_account_id, sent_at) "
                    "VALUES ({}, {}, {}, NOW())",
                    mailId,
                    receiverGuid,
                    receiverAccountId
                );

                SendFcmCategory(
                    MAIL_FCM_CATEGORY_ID,
                    MAIL_FCM_CATEGORY_CODE,
                    "[우편] 새 우편 도착",
                    subject.empty()
                        ? "새 우편이 도착했습니다."
                        : Trinity::StringFormat("새 우편이 도착했습니다. 제목: {}", subject),
                    receiverAccountId
                );
            } while (result->NextRow());
        }

        g_MailArrivalFcmRunning.store(false);
    }

    void QueueArrivedMailFcmBackground()
    {
        bool expected = false;

        if (!g_MailArrivalFcmRunning.compare_exchange_strong(expected, true))
            return;

        std::thread([]()
        {
            RunArrivedMailFcmBackground();
        }).detach();
    }
}

class custom_friend_login_fcm_player_script : public PlayerScript
{
public:
    custom_friend_login_fcm_player_script() : PlayerScript("custom_friend_login_fcm_player_script") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        QueueFriendAccountsNotify(player);
    }
};

class custom_mail_arrival_fcm_worldscript : public WorldScript
{
public:
    custom_mail_arrival_fcm_worldscript() : WorldScript("custom_mail_arrival_fcm_worldscript") { }

    void OnUpdate(uint32 diff) override
    {
        if (_timer > diff)
        {
            _timer -= diff;
            return;
        }

        _timer = MAIL_FCM_INTERVAL_MS;
        QueueArrivedMailFcmBackground();
    }

private:
    uint32 _timer = MAIL_FCM_INTERVAL_MS;
};

void AddSC_custom_friend_login_fcm()
{
    new custom_friend_login_fcm_player_script();
    new custom_mail_arrival_fcm_worldscript();
}