/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// Custom Update - 2026-06-23
// - Blackwing Lair Razorgore 1~2 player mode support.

#include "ScriptMgr.h"
#include "blackwing_lair.h"
#include "InstanceScript.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellHistory.h"
#include "SpellScript.h"

#include <algorithm>
#include <vector>

enum Say
{
    SAY_EGGS_BROKEN1        = 0,
    SAY_EGGS_BROKEN2        = 1,
    SAY_EGGS_BROKEN3        = 2,
    SAY_DEATH               = 3,
};

enum Spells
{
    // @todo orb uses the wrong spell, this needs sniffs
    SPELL_MINDCONTROL       = 42013,
    SPELL_CHANNEL           = 45537,
    SPELL_EGG_DESTROY       = 19873,

    SPELL_CLEAVE            = 22540,
    SPELL_WARSTOMP          = 24375,
    SPELL_FIREBALLVOLLEY    = 22425,
    SPELL_CONFLAGRATION     = 23023
};

enum Summons
{
    NPC_ELITE_DRACHKIN      = 12422,
    NPC_ELITE_WARRIOR       = 12458,
    NPC_WARRIOR             = 12416,
    NPC_MAGE                = 12420,
    NPC_WARLOCK             = 12459,

    GO_EGG                  = 177807
};

enum EVENTS
{
    EVENT_CLEAVE            = 1,
    EVENT_STOMP             = 2,
    EVENT_FIREBALL          = 3,
    EVENT_CONFLAGRATION     = 4
};

namespace
{
    constexpr uint32 RAZORGORE_PHASE_TWO_HEALTH_PERCENT = 25;
    constexpr uint32 RAZORGORE_PHASE_TWO_DAMAGE_REDUCTION_PERCENT = 65;
    constexpr uint32 RAZORGORE_ORB_PLAYER_DAMAGE_REDUCTION_PERCENT = 90;

    std::vector<ObjectGuid> RazorgoreOrbProtectedPlayers;

    bool IsRazorgoreAddEntry(uint32 entry)
    {
        switch (entry)
        {
            case NPC_ELITE_DRACHKIN:
            case NPC_ELITE_WARRIOR:
            case NPC_WARRIOR:
            case NPC_MAGE:
            case NPC_WARLOCK:
                return true;
            default:
                break;
        }

        return false;
    }

    bool HasRazorgoreOrbProtection(Player* player)
    {
        if (!player)
            return false;

        return std::find(RazorgoreOrbProtectedPlayers.begin(), RazorgoreOrbProtectedPlayers.end(), player->GetGUID()) != RazorgoreOrbProtectedPlayers.end();
    }

    void AddRazorgoreOrbProtection(Player* player)
    {
        if (!player)
            return;

        if (HasRazorgoreOrbProtection(player))
            return;

        RazorgoreOrbProtectedPlayers.push_back(player->GetGUID());
    }

    void ClearRazorgoreOrbProtection()
    {
        RazorgoreOrbProtectedPlayers.clear();
    }

    void ApplyDamageReduction(uint32& damage, uint32 reductionPercent)
    {
        if (!damage)
            return;

        if (reductionPercent >= 100)
        {
            damage = 0;
            return;
        }

        damage = uint32(uint64(damage) * (100 - reductionPercent) / 100);
    }
}

struct boss_razorgore : public BossAI
{
    boss_razorgore(Creature* creature) : BossAI(creature, DATA_RAZORGORE_THE_UNTAMED), originalMaxHealth(creature->GetMaxHealth())
    {
        Initialize();
    }

    void Initialize()
    {
        secondPhase = false;
    }

    void Reset() override
    {
        _Reset();

        Initialize();

        me->SetMaxHealth(originalMaxHealth);
        me->SetFullHealth();

        ClearRazorgoreOrbProtection();

        instance->SetData(DATA_EGG_EVENT, NOT_STARTED);
    }

    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);

        ClearRazorgoreOrbProtection();

        instance->SetData(DATA_EGG_EVENT, NOT_STARTED);
    }

    void DoChangePhase()
    {
        events.ScheduleEvent(EVENT_CLEAVE, 15s);
        events.ScheduleEvent(EVENT_STOMP, 35s);
        events.ScheduleEvent(EVENT_FIREBALL, 7s);
        events.ScheduleEvent(EVENT_CONFLAGRATION, 12s);

        secondPhase = true;

        ClearRazorgoreOrbProtection();

        me->RemoveAllAuras();

        uint32 phaseTwoHealth = originalMaxHealth * RAZORGORE_PHASE_TWO_HEALTH_PERCENT / 100;
        if (!phaseTwoHealth)
            phaseTwoHealth = 1;

        me->SetMaxHealth(phaseTwoHealth);
        me->SetFullHealth();
    }

    void DoAction(int32 action) override
    {
        if (action == ACTION_PHASE_TWO)
            DoChangePhase();
    }

    void DamageTaken(Unit* /*who*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // @todo this is wrong - razorgore should still take damage, he should just nuke the whole room and respawn if he dies during P1
        if (!secondPhase)
            damage = 0;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CLEAVE:
                    DoCastVictim(SPELL_CLEAVE);
                    events.ScheduleEvent(EVENT_CLEAVE, 7s, 10s);
                    break;
                case EVENT_STOMP:
                    DoCastVictim(SPELL_WARSTOMP);
                    events.ScheduleEvent(EVENT_STOMP, 15s, 25s);
                    break;
                case EVENT_FIREBALL:
                    DoCastVictim(SPELL_FIREBALLVOLLEY);
                    events.ScheduleEvent(EVENT_FIREBALL, 12s, 15s);
                    break;
                case EVENT_CONFLAGRATION:
                    DoCastVictim(SPELL_CONFLAGRATION);
                    events.ScheduleEvent(EVENT_CONFLAGRATION, 30s);
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }
        DoMeleeAttackIfReady();
    }

private:
    bool secondPhase;
    uint32 originalMaxHealth;
};

struct go_orb_of_domination : public GameObjectAI
{
    go_orb_of_domination(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

    InstanceScript* instance;

    bool OnGossipHello(Player* player) override
    {
        if (instance->GetData(DATA_EGG_EVENT) != DONE)
        {
            if (Creature* razorgore = instance->GetCreature(DATA_RAZORGORE_THE_UNTAMED))
            {
                AddRazorgoreOrbProtection(player);

                razorgore->Attack(player, true);
                player->CastSpell(razorgore, SPELL_MINDCONTROL);
            }
        }
        return true;
    }
};

// 19873 - Destroy Egg
class spell_egg_event : public SpellScript
{
    PrepareSpellScript(spell_egg_event);

    void HandleOnHit()
    {
        if (InstanceScript* instance = GetCaster()->GetInstanceScript())
            instance->SetData(DATA_EGG_EVENT, SPECIAL);

        if (Unit* caster = GetCaster())
            if (SpellHistory* spellHistory = caster->GetSpellHistory())
                spellHistory->ResetCooldown(SPELL_EGG_DESTROY, true);
    }

    void Register() override
    {
        OnHit += SpellHitFn(spell_egg_event::HandleOnHit);
    }
};

class razorgore_one_two_player_mode : public UnitScript
{
public:
    razorgore_one_two_player_mode() : UnitScript("razorgore_one_two_player_mode") { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!attacker || !victim || !damage)
            return;

        if (attacker->GetMapId() != 469 || victim->GetMapId() != 469)
            return;

        if (Player* player = victim->ToPlayer())
        {
            if (HasRazorgoreOrbProtection(player) && IsRazorgoreAddEntry(attacker->GetEntry()))
            {
                ApplyDamageReduction(damage, RAZORGORE_ORB_PLAYER_DAMAGE_REDUCTION_PERCENT);
                return;
            }
        }

        if (attacker->GetEntry() == NPC_RAZORGORE)
            ApplyDamageReduction(damage, RAZORGORE_PHASE_TWO_DAMAGE_REDUCTION_PERCENT);
    }
};

void AddSC_boss_razorgore()
{
    RegisterBlackwingLairCreatureAI(boss_razorgore);
    RegisterBlackwingLairGameObjectAI(go_orb_of_domination);
    RegisterSpellScript(spell_egg_event);
    new razorgore_one_two_player_mode();
}