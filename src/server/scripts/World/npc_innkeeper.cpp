/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* ScriptData
SDName: Npc_Innkeeper
SDAuthor: WarHead
SD%Complete: 99%
SDComment: Complete
SDCategory: NPCs
EndScriptData */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "GameEventMgr.h"
#include "Player.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"
#include "Chat.h"

#include <string>

enum Spells
{
    SPELL_TRICK_OR_TREATED      = 24755,
    SPELL_TREAT                 = 24715
};

enum Npc
{
    NPC_GOSSIP_MENU = 9733,
    NPC_GOSSIP_MENU_EVENT = 342,
};

enum InnkeeperMentorActions
{
    GOSSIP_ACTION_MENTOR_APPLY = GOSSIP_ACTION_INFO_DEF + 100,
    GOSSIP_ACTION_SECOND_HOMEBIND_SAVE = GOSSIP_ACTION_INFO_DEF + 101
};

namespace
{
    static constexpr uint8 SECOND_HOMEBIND_SLOT = 2;

    static uint32 GetMentorFaction(Player* player)
    {
        if (!player)
            return 2;

        return uint32(player->GetTeamId());
    }

    static bool HasActiveMentorLink(Player* player)
    {
        if (!player)
            return false;

        uint32 accountId = player->GetSession()->GetAccountId();

        QueryResult result = LoginDatabase.PQuery(
            "SELECT id "
            "FROM appdata.mentor_links "
            "WHERE mentee_account_id = {} "
            "AND status = 1 "
            "LIMIT 1",
            accountId
        );

        return result != nullptr;
    }

    static bool HasPendingMentorApplication(Player* player)
    {
        if (!player)
            return false;

        uint32 accountId = player->GetSession()->GetAccountId();

        QueryResult result = LoginDatabase.PQuery(
            "SELECT id "
            "FROM appdata.mentor_applications "
            "WHERE mentee_account_id = {} "
            "AND status = 0 "
            "LIMIT 1",
            accountId
        );

        return result != nullptr;
    }

    static void SaveMentorApplication(Player* player, char const* code)
    {
        if (!player)
            return;

        std::string intro = code ? code : "";
        intro.erase(0, intro.find_first_not_of(" \t\r\n"));
        intro.erase(intro.find_last_not_of(" \t\r\n") + 1);

        if (intro.empty())
        {
            ChatHandler(player->GetSession()).SendSysMessage("멘토 신청 인사글을 입력해야 합니다.");
            return;
        }

        if (intro.length() > 255)
            intro.resize(255);

        if (HasActiveMentorLink(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage("이미 활성화된 멘토 관계가 있습니다.");
            return;
        }

        if (HasPendingMentorApplication(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage("이미 멘토 신청이 등록되어 있습니다.");
            return;
        }

        uint32 accountId = player->GetSession()->GetAccountId();
        uint64 characterGuid = player->GetGUID().GetCounter();
        std::string characterName = player->GetName();
        uint32 faction = GetMentorFaction(player);

        LoginDatabase.EscapeString(characterName);
        LoginDatabase.EscapeString(intro);

        LoginDatabase.PExecute(
            "INSERT INTO appdata.mentor_applications "
            "(mentee_account_id, mentee_character_guid, mentee_character_name, faction, help_type, intro_message, status) "
            "VALUES ({}, {}, '{}', {}, '{}', '{}', 0)",
            accountId,
            characterGuid,
            characterName,
            faction,
            "general",
            intro
        );

        ChatHandler(player->GetSession()).SendSysMessage("멘토 신청이 등록되었습니다. 달라란 멘토 관리인을 통해 멘토가 수락할 수 있습니다.");
    }

    static void SaveSecondHomebind(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        uint32 guid = static_cast<uint32>(player->GetGUID().GetCounter());

        CharacterDatabase.PExecute(
            "REPLACE INTO characters.custom_homebind_extra "
            "(guid, slot, map_id, zone_id, area_id, position_x, position_y, position_z, position_o) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {})",
            guid,
            uint32(SECOND_HOMEBIND_SLOT),
            player->GetMapId(),
            player->GetZoneId(),
            player->GetAreaId(),
            player->GetPositionX(),
            player->GetPositionY(),
            player->GetPositionZ(),
            player->GetOrientation()
        );

        ChatHandler(player->GetSession()).SendSysMessage("두 번째 귀환지가 현재 여관 위치로 저장되었습니다.");
    }
}

class npc_innkeeper : public CreatureScript
{
public:
    npc_innkeeper() : CreatureScript("npc_innkeeper") { }

    struct npc_innkeeperAI : public ScriptedAI
    {
        npc_innkeeperAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            InitGossipMenuFor(player, NPC_GOSSIP_MENU);

            if (IsHolidayActive(HOLIDAY_HALLOWS_END) && !player->HasAura(SPELL_TRICK_OR_TREATED))
                AddGossipItemFor(player, NPC_GOSSIP_MENU_EVENT, 0, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

            if (me->IsQuestGiver())
                player->PrepareQuestMenu(me->GetGUID());

            if (me->IsVendor())
                AddGossipItemFor(player, NPC_GOSSIP_MENU, 2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRADE);

            if (me->IsInnkeeper())
            {
                AddGossipItemFor(player, NPC_GOSSIP_MENU, 1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INN);

                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    "두 번째 귀환지로 저장합니다.",
                    GOSSIP_SENDER_MAIN,
                    GOSSIP_ACTION_SECOND_HOMEBIND_SAVE
                );
            }

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "멘토를 신청하고 싶습니다.",
                GOSSIP_SENDER_MAIN,
                GOSSIP_ACTION_MENTOR_APPLY,
                "멘토에게 보여줄 간단한 인사글을 입력하세요.",
                0,
                true
            );

            player->TalkedToCreature(me->GetEntry(), me->GetGUID());
            SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            if (action == GOSSIP_ACTION_INFO_DEF + 1 && IsHolidayActive(HOLIDAY_HALLOWS_END) && !player->HasAura(SPELL_TRICK_OR_TREATED))
            {
                player->CastSpell(player, SPELL_TRICK_OR_TREATED, true);

                if (urand(0, 1))
                    player->CastSpell(player, SPELL_TREAT, true);
                else
                {
                    uint32 trickspell = 0;
                    switch (urand(0, 13))
                    {
                        case 0: trickspell = 24753; break; // cannot cast, random 30sec
                        case 1: trickspell = 24713; break; // lepper gnome costume
                        case 2: trickspell = 24735; break; // male ghost costume
                        case 3: trickspell = 24736; break; // female ghostcostume
                        case 4: trickspell = 24710; break; // male ninja costume
                        case 5: trickspell = 24711; break; // female ninja costume
                        case 6: trickspell = 24708; break; // male pirate costume
                        case 7: trickspell = 24709; break; // female pirate costume
                        case 8: trickspell = 24723; break; // skeleton costume
                        case 9: trickspell = 24753; break; // Trick
                        case 10: trickspell = 24924; break; // Hallow's End Candy
                        case 11: trickspell = 24925; break; // Hallow's End Candy
                        case 12: trickspell = 24926; break; // Hallow's End Candy
                        case 13: trickspell = 24927; break; // Hallow's End Candy
                    }
                    player->CastSpell(player, trickspell, true);
                }

                CloseGossipMenuFor(player);
                return true;
            }

            CloseGossipMenuFor(player);

            switch (action)
            {
                case GOSSIP_ACTION_TRADE:
                    player->GetSession()->SendListInventory(me->GetGUID());
                    break;

                case GOSSIP_ACTION_INN:
                    player->SetBindPoint(me->GetGUID());
                    break;

                case GOSSIP_ACTION_SECOND_HOMEBIND_SAVE:
                    if (me->IsInnkeeper())
                        SaveSecondHomebind(player);
                    break;
            }

            return true;
        }

        bool OnGossipSelectCode(Player* player, uint32 /*menuId*/, uint32 gossipListId, char const* code) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            if (action == GOSSIP_ACTION_MENTOR_APPLY)
            {
                SaveMentorApplication(player, code);
                CloseGossipMenuFor(player);
                return true;
            }

            CloseGossipMenuFor(player);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_innkeeperAI(creature);
    }
};

void AddSC_npc_innkeeper()
{
    new npc_innkeeper();
}