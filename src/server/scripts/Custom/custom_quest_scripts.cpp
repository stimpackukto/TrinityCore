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

#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <filesystem>

//quest 13086 The Last Line Of Defense
enum Spells
{
    SPELL_REGEN_BLOCKER_1 = 57385,
    SPELL_REGEN_BLOCKER_2 = 57412,
    SPELL_COMBAT_1        = 57477,
    SPELL_COMBAT_2        = 53363
};

enum Events
{
    EVENT_COMBAT_1 = 1,
    EVENT_COMBAT_2
};

class npc_frostbrood_destroyer : public CreatureScript
{
public:
    npc_frostbrood_destroyer() : CreatureScript("npc_frostbrood_destroyer") { }

    struct npc_frostbrood_destroyerAI : public ScriptedAI
    {
        EventMap events;
        bool regenBlocked = false;
        uint32 noRegenTimer = 0;

        npc_frostbrood_destroyerAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override
        {
            regenBlocked = false;
            noRegenTimer = 0;
            me->SetRegenerateHealth(true);
            events.Reset();
        }


        void JustEngagedWith(Unit* who) override
        {
            if (!who || !who->IsAlive())
                return;

            events.ScheduleEvent(EVENT_COMBAT_1, 5s, 10s);
            events.ScheduleEvent(EVENT_COMBAT_2, 10s, 15s);
        }

        void SpellHit(WorldObject* /*caster*/, SpellInfo const* spell) override
        {
            if (spell->Id == SPELL_REGEN_BLOCKER_1 || spell->Id == SPELL_REGEN_BLOCKER_2)
            {
                noRegenTimer = 0;
                if (!regenBlocked)
                {
                    regenBlocked = true;
                    me->SetRegenerateHealth(false);
                }
            }
        }

        void DamageTaken(Unit* attacker, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* spellInfo) override
        {
            if (!me->IsInCombat() && attacker && attacker->IsPlayer())
            {
                if (!spellInfo || (spellInfo->Id != SPELL_REGEN_BLOCKER_1 && spellInfo->Id != SPELL_REGEN_BLOCKER_2))
                    me->Attack(attacker, true);
            }
        }

        void UpdateAI(uint32 diff) override
        {
            if (regenBlocked)
            {
                noRegenTimer += diff;
                if (noRegenTimer >= 5000)
                {
                    regenBlocked = false;
                    me->SetRegenerateHealth(true);
                }
            }

            if (!UpdateVictim())
                return;

            events.Update(diff);

            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {

                    case EVENT_COMBAT_1:
                        me->CastSpell(me->GetVictim(), SPELL_COMBAT_1, false);
                        events.ScheduleEvent(EVENT_COMBAT_1, 10s, 14s);
                        break;

                    case EVENT_COMBAT_2:
                        me->CastSpell(me->GetVictim(), SPELL_COMBAT_2, false);
                        events.ScheduleEvent(EVENT_COMBAT_2, 15s, 25s);
                        break;
                }
            }

            DoMeleeAttackIfReady();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_frostbrood_destroyerAI(creature);
    }
};


#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "MotionMaster.h"

namespace
{
    static constexpr uint32 POINT_RANDOM_MOVE = 1001;

    struct MovePos
    {
        uint32 mapId;
        float x;
        float y;
        float z;
        float o;
    };

    static constexpr MovePos RANDOM_POSITIONS[2] =
    {
        { 571, 994.286f, -5312.0f, 175.574f, 1.18682f },
        { 571, 925.647f, -5299.53f, 175.687f, 1.90241f }
    };
}

class npc_dragonflayer_defender_reactive : public CreatureScript
{
public:
    npc_dragonflayer_defender_reactive() : CreatureScript("npc_dragonflayer_defender_reactive") { }

    struct npc_dragonflayer_defender_reactiveAI : public ScriptedAI
    {
        npc_dragonflayer_defender_reactiveAI(Creature* creature) : ScriptedAI(creature) { }

        bool _moveScheduled = false;
        bool _moveStarted   = false;

        float _targetX = 0.0f;
        float _targetY = 0.0f;
        float _targetZ = 0.0f;
        float _targetO = 0.0f;

        void Reset() override
        {
            _moveScheduled = false;
            _moveStarted   = false;

            uint32 index = urand(0, 1);
            MovePos const& pos = RANDOM_POSITIONS[index];

            if (me->GetMapId() != pos.mapId)
                return;

            _targetX = pos.x;
            _targetY = pos.y;
            _targetZ = pos.z;
            _targetO = pos.o;

            _moveScheduled = true;
        }

        void UpdateAI(uint32 /*diff*/) override
        {
            if (_moveScheduled && !_moveStarted)
            {
                _moveStarted = true;

                me->SetWalk(false);
                me->ClearUnitState(UNIT_STATE_ROOT);

                me->GetMotionMaster()->Clear();
                me->GetMotionMaster()->MovePoint(
                    POINT_RANDOM_MOVE,
                    _targetX,
                    _targetY,
                    _targetZ
                );

                return;
            }

            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();
        }

        void MovementInform(uint32 type, uint32 id) override
        {
            if (type != POINT_MOTION_TYPE)
                return;

            if (id != POINT_RANDOM_MOVE)
                return;

            me->SetFacingTo(_targetO);
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_dragonflayer_defender_reactiveAI(creature);
    }
};


void AddSC_custom_quest_scripts()
{
	//최후의 방어
	new npc_frostbrood_destroyer();

	//섬멸
	new npc_dragonflayer_defender_reactive();
}