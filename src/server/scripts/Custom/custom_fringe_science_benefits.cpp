// custom_fringe_science_benefits.cpp

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Creature.h"
#include "Player.h"

namespace
{
    enum FringeScienceBenefitsData
    {
        QUEST_FRINGE_SCIENCE_BENEFITS = 13373,

        NPC_BOMBARDMENT_INFANTRY      = 32399,
        NPC_BOMBARDMENT_CAPTAIN       = 32410,
        NPC_GARGOYLE_AMBUSHER         = 32188,

        SPELL_MISSION_COMPLETE        = 61282,

        GOSSIP_ACTION_START_BOMBING   = GOSSIP_ACTION_INFO_DEF + 1
    };

    static void GiveQuestCredit(Player* player, uint32 entry, uint8 count)
    {
        if (!player)
            return;

        for (uint8 i = 0; i < count; ++i)
            player->KilledMonsterCredit(entry);
    }
}

class npc_rizzy_ratchwiggle_fringe_science_benefits : public CreatureScript
{
public:
    npc_rizzy_ratchwiggle_fringe_science_benefits()
        : CreatureScript("npc_rizzy_ratchwiggle_fringe_science_benefits")
    {
    }

    struct npc_rizzy_ratchwiggle_fringe_science_benefitsAI : public ScriptedAI
    {
        npc_rizzy_ratchwiggle_fringe_science_benefitsAI(Creature* creature)
            : ScriptedAI(creature)
        {
        }

        bool OnGossipHello(Player* player) override
        {
            if (!player)
                return true;

            if (me->IsQuestGiver())
                player->PrepareQuestMenu(me->GetGUID());

            if (player->GetQuestStatus(QUEST_FRINGE_SCIENCE_BENEFITS) == QUEST_STATUS_INCOMPLETE)
            {
                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    "폭격 임무를 시작한다.",
                    GOSSIP_SENDER_MAIN,
                    GOSSIP_ACTION_START_BOMBING
                );
            }

            player->TalkedToCreature(me->GetEntry(), me->GetGUID());
            SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            if (!player)
                return true;

            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

            ClearGossipMenuFor(player);
            CloseGossipMenuFor(player);

            if (action != GOSSIP_ACTION_START_BOMBING)
                return true;

            if (player->GetQuestStatus(QUEST_FRINGE_SCIENCE_BENEFITS) != QUEST_STATUS_INCOMPLETE)
            {
                me->Whisper("지금은 수행할 폭격 임무가 없어.", LANG_UNIVERSAL, player);
                return true;
            }

            me->Whisper("좋아! 폭격 임무 처리 완료다. 이제 Tezzla에게 돌아가!", LANG_UNIVERSAL, player);

            GiveQuestCredit(player, NPC_BOMBARDMENT_INFANTRY, 40);
            GiveQuestCredit(player, NPC_BOMBARDMENT_CAPTAIN, 8);
            GiveQuestCredit(player, NPC_GARGOYLE_AMBUSHER, 15);

            player->CastSpell(player, SPELL_MISSION_COMPLETE, true);

            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_rizzy_ratchwiggle_fringe_science_benefitsAI(creature);
    }
};

void AddSC_custom_fringe_science_benefits()
{
    new npc_rizzy_ratchwiggle_fringe_science_benefits();
}