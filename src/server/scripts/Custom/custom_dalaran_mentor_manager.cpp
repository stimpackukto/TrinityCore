// custom_dalaran_mentor_manager.cpp

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"
#include "Chat.h"

namespace
{
    enum DalaranMentorManagerActions
    {
        ACTION_SHOW_APPLICATIONS     = GOSSIP_ACTION_INFO_DEF + 1,
        ACTION_BACK_MAIN             = GOSSIP_ACTION_INFO_DEF + 2,
        ACTION_CLOSE                 = GOSSIP_ACTION_INFO_DEF + 3,

        ACTION_APP_DETAIL_BASE       = GOSSIP_ACTION_INFO_DEF + 1000,
        ACTION_APP_ACCEPT_BASE       = GOSSIP_ACTION_INFO_DEF + 100000
    };

    static constexpr uint32 MAX_ACTIVE_MENTEES_PER_MENTOR = 3;

    static uint32 GetPlayerFaction(Player* player)
    {
        if (!player)
            return 2;

        return uint32(player->GetTeamId());
    }

    static char const* GetFactionName(uint32 faction)
    {
        switch (faction)
        {
            case 0:
                return "얼라이언스";
            case 1:
                return "호드";
            default:
                return "알 수 없음";
        }
    }

    static std::string GetHelpTypeLabel(std::string const& helpType)
    {
        if (helpType == "level")
            return "레벨업";

        if (helpType == "heroic")
            return "영웅 던전";

        if (helpType == "raid")
            return "얼성/루비 준비";

        if (helpType == "battle")
            return "전장/겨손";

        if (helpType == "profession")
            return "전문기술";

        if (helpType == "general")
            return "일반 도움";

        return helpType;
    }

    static std::string CutText(std::string text, std::size_t maxLength)
    {
        if (text.length() <= maxLength)
            return text;

        return text.substr(0, maxLength) + "...";
    }

    static void SendPlayerMessage(Player* player, std::string const& message)
    {
        if (!player)
            return;

        ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
    }

    static bool HasActiveMentor(Player* player, uint32 menteeAccountId)
    {
        if (!player || menteeAccountId == 0)
            return false;

        QueryResult result = LoginDatabase.PQuery(
            "SELECT id "
            "FROM appdata.mentor_links "
            "WHERE mentee_account_id = {} "
            "AND status = 1 "
            "LIMIT 1",
            menteeAccountId
        );

        return result != nullptr;
    }

    static uint32 GetActiveMenteeCount(Player* player)
    {
        if (!player)
            return 0;

        uint32 mentorAccountId = player->GetSession()->GetAccountId();

        QueryResult result = LoginDatabase.PQuery(
            "SELECT COUNT(*) "
            "FROM appdata.mentor_links "
            "WHERE mentor_account_id = {} "
            "AND status = 1",
            mentorAccountId
        );

        if (!result)
            return 0;

        Field* fields = result->Fetch();
        return fields[0].GetUInt32();
    }

    static void SendMainMenu(Player* player, Creature* creature)
    {
        if (!player || !creature)
            return;

        ClearGossipMenuFor(player);

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "멘토 신청자 목록을 봅니다.",
            GOSSIP_SENDER_MAIN,
            ACTION_SHOW_APPLICATIONS
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "닫기",
            GOSSIP_SENDER_MAIN,
            ACTION_CLOSE
        );

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    static void SendApplicationList(Player* player, Creature* creature)
    {
        if (!player || !creature)
            return;

        ClearGossipMenuFor(player);

        uint32 faction = GetPlayerFaction(player);

        QueryResult result = LoginDatabase.PQuery(
            "SELECT id, mentee_character_name, help_type, intro_message, created_at "
            "FROM appdata.mentor_applications "
            "WHERE status = 0 "
            "AND faction = {} "
            "ORDER BY created_at ASC "
            "LIMIT 20",
            faction
        );

        if (!result)
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                std::string("[") + GetFactionName(faction) + "] 멘토 신청자가 없습니다.",
                GOSSIP_SENDER_MAIN,
                ACTION_SHOW_APPLICATIONS
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "뒤로가기",
                GOSSIP_SENDER_MAIN,
                ACTION_BACK_MAIN
            );

            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
            return;
        }

        do
        {
            Field* fields = result->Fetch();

            uint32 applicationId = fields[0].GetUInt32();
            std::string characterName = fields[1].GetString();
            std::string helpType = fields[2].GetString();
            std::string introMessage = fields[3].GetString();

            std::string menuText =
                "[" + GetHelpTypeLabel(helpType) + "] " +
                characterName + " - " + CutText(introMessage, 34);

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                menuText,
                GOSSIP_SENDER_MAIN,
                ACTION_APP_DETAIL_BASE + applicationId
            );
        }
        while (result->NextRow());

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "뒤로가기",
            GOSSIP_SENDER_MAIN,
            ACTION_BACK_MAIN
        );

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    static void SendApplicationDetail(Player* player, Creature* creature, uint32 applicationId)
    {
        if (!player || !creature || applicationId == 0)
            return;

        ClearGossipMenuFor(player);

        uint32 faction = GetPlayerFaction(player);

        QueryResult result = LoginDatabase.PQuery(
            "SELECT id, mentee_account_id, mentee_character_guid, mentee_character_name, faction, help_type, intro_message, created_at "
            "FROM appdata.mentor_applications "
            "WHERE id = {} "
            "AND status = 0 "
            "LIMIT 1",
            applicationId
        );

        if (!result)
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "멘토 신청 정보를 찾을 수 없습니다.",
                GOSSIP_SENDER_MAIN,
                ACTION_SHOW_APPLICATIONS
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "신청자 목록으로 돌아가기",
                GOSSIP_SENDER_MAIN,
                ACTION_SHOW_APPLICATIONS
            );

            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
            return;
        }

        Field* fields = result->Fetch();

        uint32 menteeAccountId = fields[1].GetUInt32();
        uint64 menteeCharacterGuid = fields[2].GetUInt64();
        std::string menteeCharacterName = fields[3].GetString();
        uint32 applicationFaction = fields[4].GetUInt32();
        std::string helpType = fields[5].GetString();
        std::string introMessage = fields[6].GetString();
        std::string createdAt = fields[7].GetString();

        if (applicationFaction != faction)
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "다른 진영의 멘토 신청은 수락할 수 없습니다.",
                GOSSIP_SENDER_MAIN,
                ACTION_SHOW_APPLICATIONS
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "신청자 목록으로 돌아가기",
                GOSSIP_SENDER_MAIN,
                ACTION_SHOW_APPLICATIONS
            );

            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
            return;
        }

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "===== 멘토 신청 정보 =====",
            GOSSIP_SENDER_MAIN,
            ACTION_APP_DETAIL_BASE + applicationId
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "[신청자] " + menteeCharacterName,
            GOSSIP_SENDER_MAIN,
            ACTION_APP_DETAIL_BASE + applicationId
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            std::string("[진영] ") + GetFactionName(applicationFaction),
            GOSSIP_SENDER_MAIN,
            ACTION_APP_DETAIL_BASE + applicationId
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "[도움 분야] " + GetHelpTypeLabel(helpType),
            GOSSIP_SENDER_MAIN,
            ACTION_APP_DETAIL_BASE + applicationId
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "[인사글] " + CutText(introMessage, 70),
            GOSSIP_SENDER_MAIN,
            ACTION_APP_DETAIL_BASE + applicationId
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "[신청 시간] " + createdAt,
            GOSSIP_SENDER_MAIN,
            ACTION_APP_DETAIL_BASE + applicationId
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "이 신청자를 멘티로 수락합니다.",
            GOSSIP_SENDER_MAIN,
            ACTION_APP_ACCEPT_BASE + applicationId
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "신청자 목록으로 돌아가기",
            GOSSIP_SENDER_MAIN,
            ACTION_SHOW_APPLICATIONS
        );

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    static void AcceptApplication(Player* player, Creature* creature, uint32 applicationId)
    {
        if (!player || !creature || applicationId == 0)
            return;

        uint32 mentorAccountId = player->GetSession()->GetAccountId();
        uint64 mentorCharacterGuid = player->GetGUID().GetCounter();
        uint32 mentorFaction = GetPlayerFaction(player);

        QueryResult result = LoginDatabase.PQuery(
            "SELECT id, mentee_account_id, mentee_character_guid, mentee_character_name, faction, status "
            "FROM appdata.mentor_applications "
            "WHERE id = {} "
            "LIMIT 1",
            applicationId
        );

        if (!result)
        {
            SendPlayerMessage(player, "멘토 신청 정보를 찾을 수 없습니다.");
            CloseGossipMenuFor(player);
            return;
        }

        Field* fields = result->Fetch();

        uint32 menteeAccountId = fields[1].GetUInt32();
        uint64 menteeCharacterGuid = fields[2].GetUInt64();
        std::string menteeCharacterName = fields[3].GetString();
        uint32 applicationFaction = fields[4].GetUInt32();
        uint32 status = fields[5].GetUInt32();

        if (status != 0)
        {
            SendPlayerMessage(player, "이미 처리된 멘토 신청입니다.");
            CloseGossipMenuFor(player);
            return;
        }

        if (mentorAccountId == menteeAccountId)
        {
            SendPlayerMessage(player, "자기 자신의 멘토 신청은 수락할 수 없습니다.");
            CloseGossipMenuFor(player);
            return;
        }

        if (applicationFaction != mentorFaction)
        {
            SendPlayerMessage(player, "다른 진영의 멘토 신청은 수락할 수 없습니다.");
            CloseGossipMenuFor(player);
            return;
        }

        if (HasActiveMentor(player, menteeAccountId))
        {
            LoginDatabase.PExecute(
                "UPDATE appdata.mentor_applications "
                "SET status = 3, updated_at = NOW() "
                "WHERE id = {} "
                "AND status = 0",
                applicationId
            );

            SendPlayerMessage(player, "해당 멘티는 이미 활성화된 멘토 관계가 있습니다.");
            CloseGossipMenuFor(player);
            return;
        }

        uint32 activeMenteeCount = GetActiveMenteeCount(player);

        if (activeMenteeCount >= MAX_ACTIVE_MENTEES_PER_MENTOR)
        {
            SendPlayerMessage(
                player,
                "활성 멘티는 최대 " + std::to_string(MAX_ACTIVE_MENTEES_PER_MENTOR) + "명까지만 받을 수 있습니다."
            );

            CloseGossipMenuFor(player);
            return;
        }

        LoginDatabase.PExecute(
            "INSERT INTO appdata.mentor_links "
            "(mentor_account_id, mentee_account_id, status, source, note, created_at, accepted_at) "
            "VALUES ({}, {}, 1, '{}', '{}', NOW(), NOW())",
            mentorAccountId,
            menteeAccountId,
            "dalaran_manager",
            "mentor_application_accept"
        );

        LoginDatabase.PExecute(
            "UPDATE appdata.mentor_applications "
            "SET status = 1, "
            "accepted_by_account_id = {}, "
            "accepted_by_character_guid = {}, "
            "accepted_at = NOW(), "
            "updated_at = NOW() "
            "WHERE id = {} "
            "AND status = 0",
            mentorAccountId,
            mentorCharacterGuid,
            applicationId
        );

        SendPlayerMessage(
            player,
            menteeCharacterName + " 님을 멘티로 수락했습니다. 이제 멘토-멘티 성장 동행이 시작됩니다."
        );

        CloseGossipMenuFor(player);
    }
}

class npc_dalaran_mentor_manager : public CreatureScript
{
public:
    npc_dalaran_mentor_manager() : CreatureScript("npc_dalaran_mentor_manager") { }

    struct npc_dalaran_mentor_managerAI : public ScriptedAI
    {
        npc_dalaran_mentor_managerAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            SendMainMenu(player, me);
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            if (action == ACTION_SHOW_APPLICATIONS)
            {
                SendApplicationList(player, me);
                return true;
            }

            if (action == ACTION_BACK_MAIN)
            {
                SendMainMenu(player, me);
                return true;
            }

            if (action == ACTION_CLOSE)
            {
                CloseGossipMenuFor(player);
                return true;
            }

            if (action >= ACTION_APP_ACCEPT_BASE)
            {
                uint32 applicationId = action - ACTION_APP_ACCEPT_BASE;
                AcceptApplication(player, me, applicationId);
                return true;
            }

            if (action >= ACTION_APP_DETAIL_BASE)
            {
                uint32 applicationId = action - ACTION_APP_DETAIL_BASE;
                SendApplicationDetail(player, me, applicationId);
                return true;
            }

            CloseGossipMenuFor(player);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_dalaran_mentor_managerAI(creature);
    }
};

void AddSC_custom_dalaran_mentor_manager()
{
    new npc_dalaran_mentor_manager();
}