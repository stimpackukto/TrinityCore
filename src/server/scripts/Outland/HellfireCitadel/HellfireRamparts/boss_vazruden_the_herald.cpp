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

/*
 * Custom Update - 2026-07-01
 *
 * Vazruden encounter combat reacquire and middle movement fix
 *
 * GetVictim()이 잠깐 끊겼다는 이유만으로 유저가 없는 것으로 판단하지 않도록 수정.
 * 근처 유저가 있으면 다시 AttackStart를 시도하고,
 * 실제로 근처 유저가 없을 때만 소환몹 정리 및 리셋 처리.
 * 사자 바즈루덴이 중앙 이동 중 1초마다 MovePoint를 재호출하면서
 * 이동이 초기화되는 문제를 MovementInform 도착 처리 방식으로 수정.
 */

/* ScriptData
Name: Boss_Vazruden_the_Herald
%Complete: 90
Comment:
Category: Hellfire Citadel, Hellfire Ramparts
EndScriptData */

#include "ScriptMgr.h"
#include "hellfire_ramparts.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"

enum Says
{
    SAY_INTRO                     = 0,

    SAY_WIPE                      = 0,
    SAY_AGGRO                     = 1,
    SAY_KILL                      = 2,
    SAY_DIE                       = 3,

    EMOTE                         = 0
};

enum Spells
{
    SPELL_FIREBALL                = 34653,
    SPELL_CONE_OF_FIRE            = 30926,
    SPELL_SUMMON_LIQUID_FIRE      = 23971,
    SPELL_SUMMON_LIQUID_FIRE_H    = 30928,
    SPELL_BELLOWING_ROAR          = 39427,
    SPELL_REVENGE                 = 19130,
    SPELL_REVENGE_H               = 40392,
    SPELL_KIDNEY_SHOT             = 30621,
    SPELL_FIRE_NOVA_VISUAL        = 19823
};

enum MovementPoints
{
    POINT_VAZRUDEN_MIDDLE         = 1
};

const float VazrudenMiddle[3] = { -1406.5f, 1746.5f, 81.2f };

const float VazrudenRing[2][3] =
{
    { -1430.0f, 1705.0f, 112.0f },
    { -1377.0f, 1760.0f, 112.0f }
};

const float VAZRUDEN_COMBAT_PLAYER_CHECK_RANGE = 160.0f;

static Player* SelectVazrudenEncounterPlayer(Creature* source)
{
    if (!source)
        return nullptr;

    return source->SelectNearestPlayer(VAZRUDEN_COMBAT_PLAYER_CHECK_RANGE);
}

struct boss_nazan : public BossAI
{
    boss_nazan(Creature* creature) : BossAI(creature, DATA_NAZAN)
    {
        Initialize();
        flight = true;
        BellowingRoar_Timer = 0;
        ConeOfFire_Timer = 0;
    }

    void Initialize()
    {
        Fireball_Timer = 4000;
        Fly_Timer = 45000;
        Turn_Timer = 0;
    }

    void Reset() override
    {
        Initialize();
        _Reset();
    }

    void JustEngagedWith(Unit* /*who*/) override { }

    void IsSummonedBy(WorldObject* summoner) override
    {
        if (summoner->GetEntry() == NPC_VAZRUDEN_HERALD)
            VazrudenGUID = summoner->GetGUID();
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned && summoned->GetEntry() == NPC_LIQUID_FIRE)
        {
            summoned->SetLevel(me->GetLevel());
            summoned->SetFaction(me->GetFaction());
            summoned->CastSpell(summoned, DUNGEON_MODE(SPELL_SUMMON_LIQUID_FIRE, SPELL_SUMMON_LIQUID_FIRE_H), true);
            summoned->CastSpell(summoned, SPELL_FIRE_NOVA_VISUAL, true);
        }
    }

    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == uint32(SPELL_FIREBALL))
            me->SummonCreature(NPC_LIQUID_FIRE, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), target->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN, 30s);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
        {
            if (Player* player = SelectVazrudenEncounterPlayer(me))
                AttackStart(player);

            if (!UpdateVictim())
                return;
        }

        if (Fireball_Timer <= diff)
        {
            if (Unit* victim = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(victim, SPELL_FIREBALL, true);
            Fireball_Timer = urand(4000, 7000);
        }
        else
            Fireball_Timer -= diff;

        if (flight)
        {
            Creature* Vazruden = ObjectAccessor::GetCreature(*me, VazrudenGUID);
            if (Fly_Timer < diff || !(Vazruden && Vazruden->IsAlive() && Vazruden->HealthAbovePct(20)))
            {
                flight = false;
                BellowingRoar_Timer = 6000;
                ConeOfFire_Timer = 12000;
                me->SetDisableGravity(false);
                me->SetWalk(true);
                me->GetMotionMaster()->Clear();

                Unit* victim = SelectTarget(SelectTargetMethod::MinDistance, 0);
                if (!victim)
                    victim = SelectVazrudenEncounterPlayer(me);

                if (victim)
                {
                    AttackStart(victim);
                    DoStartMovement(victim);
                }

                Talk(EMOTE);
                return;
            }
            else
                Fly_Timer -= diff;

            if (Turn_Timer <= diff)
            {
                uint32 waypoint = (Fly_Timer / 10000) % 2;
                if (!me->IsWithinDist3d(VazrudenRing[waypoint][0], VazrudenRing[waypoint][1], VazrudenRing[waypoint][2], 5))
                    me->GetMotionMaster()->MovePoint(0, VazrudenRing[waypoint][0], VazrudenRing[waypoint][1], VazrudenRing[waypoint][2]);
                Turn_Timer = 10000;
            }
            else
                Turn_Timer -= diff;
        }
        else
        {
            if (ConeOfFire_Timer <= diff)
            {
                DoCast(me, SPELL_CONE_OF_FIRE);
                ConeOfFire_Timer = 12000;
                Fireball_Timer = 4000;
            }
            else
                ConeOfFire_Timer -= diff;

            if (IsHeroic())
            {
                if (BellowingRoar_Timer <= diff)
                {
                    DoCast(me, SPELL_BELLOWING_ROAR);
                    BellowingRoar_Timer = 45000;
                }
                else
                    BellowingRoar_Timer -= diff;
            }

            DoMeleeAttackIfReady();
        }
    }

private:
    uint32 Fireball_Timer;
    uint32 ConeOfFire_Timer;
    uint32 BellowingRoar_Timer;
    uint32 Fly_Timer;
    uint32 Turn_Timer;
    bool flight;
    ObjectGuid VazrudenGUID;
};

struct boss_vazruden : public BossAI
{
    boss_vazruden(Creature* creature) : BossAI(creature, DATA_VAZRUDEN)
    {
        Initialize();
    }

    void Initialize()
    {
        Revenge_Timer = 4000;
        UnsummonCheck = 2000;
        WipeSaid = false;
    }

    void Reset() override
    {
        Initialize();
        _Reset();
    }

    void JustEngagedWith(Unit* who) override
    {
        Talk(SAY_AGGRO);
        BossAI::JustEngagedWith(who);
    }

    void KilledUnit(Unit* who) override
    {
        if (who && who->GetEntry() != NPC_VAZRUDEN)
            Talk(SAY_KILL);
    }

    void JustDied(Unit* killer) override
    {
        if (killer && killer != me)
            Talk(SAY_DIE);
        _JustDied();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
        {
            if (Player* player = SelectVazrudenEncounterPlayer(me))
            {
                UnsummonCheck = 2000;
                AttackStart(player);
            }

            if (!UpdateVictim())
            {
                if (UnsummonCheck < diff && me->IsAlive())
                {
                    if (!WipeSaid)
                    {
                        Talk(SAY_WIPE);
                        WipeSaid = true;
                    }

                    me->DisappearAndDie();
                }
                else
                    UnsummonCheck -= diff;

                return;
            }
        }

        if (Revenge_Timer <= diff)
        {
            if (Unit* victim = me->GetVictim())
                DoCast(victim, DUNGEON_MODE(SPELL_REVENGE, SPELL_REVENGE_H));
            Revenge_Timer = 5000;
        }
        else
            Revenge_Timer -= diff;

        DoMeleeAttackIfReady();
    }

private:
    uint32 Revenge_Timer;
    bool WipeSaid;
    uint32 UnsummonCheck;
};

struct boss_vazruden_the_herald : public ScriptedAI
{
    boss_vazruden_the_herald(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        summoned = false;
        sentryDown = false;
        movingToMiddle = false;
    }

    void Initialize()
    {
        phase = 0;
        waypoint = 0;
        check = 0;
        movingToMiddle = false;
    }

    void Reset() override
    {
        Initialize();
        UnsummonAdds();
    }

    void UnsummonAdds()
    {
        if (summoned)
        {
            Creature* Nazan = ObjectAccessor::GetCreature(*me, NazanGUID);
            if (!Nazan)
                Nazan = me->FindNearestCreature(NPC_NAZAN, 5000);
            if (Nazan)
            {
                Nazan->DisappearAndDie();
                NazanGUID.Clear();
            }

            Creature* Vazruden = ObjectAccessor::GetCreature(*me, VazrudenGUID);
            if (!Vazruden)
                Vazruden = me->FindNearestCreature(NPC_VAZRUDEN, 5000);
            if (Vazruden)
            {
                Vazruden->DisappearAndDie();
                VazrudenGUID.Clear();
            }

            summoned = false;
            me->ClearUnitState(UNIT_STATE_ROOT);
            me->SetVisible(true);
        }
    }

    void SummonAdds()
    {
        if (!summoned)
        {
            Player* player = SelectVazrudenEncounterPlayer(me);

            if (Creature* Vazruden = me->SummonCreature(NPC_VAZRUDEN, VazrudenMiddle[0], VazrudenMiddle[1], VazrudenMiddle[2], 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 100min))
            {
                VazrudenGUID = Vazruden->GetGUID();

                if (player)
                    Vazruden->AI()->AttackStart(player);
            }

            if (Creature* Nazan = me->SummonCreature(NPC_NAZAN, VazrudenMiddle[0], VazrudenMiddle[1], VazrudenMiddle[2], 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 100min))
            {
                NazanGUID = Nazan->GetGUID();

                if (player)
                    Nazan->AI()->AttackStart(player);
            }

            summoned = true;
            movingToMiddle = false;
            me->SetVisible(false);
            me->AddUnitState(UNIT_STATE_ROOT);
        }
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (phase == 0)
        {
            phase = 1;
            check = 0;
            movingToMiddle = false;
            Talk(SAY_INTRO);
        }
    }

    void JustSummoned(Creature* summon) override
    {
        if (!summon)
            return;

        Unit* victim = me->GetVictim();

        if (summon->GetEntry() == NPC_NAZAN)
        {
            summon->SetDisableGravity(true);
            summon->SetSpeedRate(MOVE_FLIGHT, 2.5f);
        }

        if (victim)
            summon->AI()->AttackStart(victim);
        else if (Player* player = SelectVazrudenEncounterPlayer(me))
            summon->AI()->AttackStart(player);
    }

    void SentryDownBy(Unit* killer)
    {
        if (sentryDown)
        {
            AttackStartNoMove(killer);
            sentryDown = false;
        }
        else
            sentryDown = true;
    }

    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE || id != POINT_VAZRUDEN_MIDDLE)
            return;

        movingToMiddle = false;

        if (phase != 1)
            return;

        SummonAdds();
        phase = 2;
        check = 2000;
    }

    void UpdateAI(uint32 diff) override
    {
        switch (phase)
        {
            case 0:
                return;
            case 1:
                if (check <= diff)
                {
                    if (me->IsWithinDist3d(VazrudenMiddle[0], VazrudenMiddle[1], VazrudenMiddle[2], 5))
                    {
                        SummonAdds();
                        phase = 2;
                        check = 2000;
                        return;
                    }

                    if (!movingToMiddle)
                    {
                        me->GetMotionMaster()->Clear();
                        me->GetMotionMaster()->MovePoint(POINT_VAZRUDEN_MIDDLE, VazrudenMiddle[0], VazrudenMiddle[1], VazrudenMiddle[2]);
                        movingToMiddle = true;
                    }

                    check = 500;
                }
                else
                    check -= diff;
                break;
            default:
                if (check <= diff)
                {
                    Creature* Nazan = ObjectAccessor::GetCreature(*me, NazanGUID);
                    Creature* Vazruden = ObjectAccessor::GetCreature(*me, VazrudenGUID);

                    bool nazanAlive = Nazan && Nazan->IsAlive();
                    bool vazrudenAlive = Vazruden && Vazruden->IsAlive();

                    if (nazanAlive || vazrudenAlive)
                    {
                        Player* player = SelectVazrudenEncounterPlayer(me);

                        if (!player && Nazan)
                            player = SelectVazrudenEncounterPlayer(Nazan);

                        if (!player && Vazruden)
                            player = SelectVazrudenEncounterPlayer(Vazruden);

                        if (player)
                        {
                            if (nazanAlive && !Nazan->GetVictim())
                                Nazan->AI()->AttackStart(player);

                            if (vazrudenAlive && !Vazruden->GetVictim())
                                Vazruden->AI()->AttackStart(player);

                            check = 2000;
                            return;
                        }

                        UnsummonAdds();
                        EnterEvadeMode();
                        return;
                    }

                    if (!nazanAlive && !vazrudenAlive)
                    {
                        me->DisappearAndDie();
                    }

                    check = 2000;
                }
                else
                    check -= diff;
                break;
        }
    }

private:
    uint32 phase;
    uint32 waypoint;
    uint32 check;
    bool sentryDown;
    ObjectGuid NazanGUID;
    ObjectGuid VazrudenGUID;
    bool movingToMiddle;
    bool summoned;
};

struct npc_hellfire_sentry : public ScriptedAI
{
    npc_hellfire_sentry(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        KidneyShot_Timer = urand(3000, 7000);
    }

    void Reset() override
    {
        Initialize();
    }

    void JustEngagedWith(Unit* /*who*/) override { }

    void JustDied(Unit* killer) override
    {
        if (!killer)
            return;

        if (Creature* herald = me->FindNearestCreature(NPC_VAZRUDEN_HERALD, 150))
            ENSURE_AI(boss_vazruden_the_herald, herald->AI())->SentryDownBy(killer);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (KidneyShot_Timer <= diff)
        {
            if (Unit* victim = me->GetVictim())
                DoCast(victim, SPELL_KIDNEY_SHOT);
            KidneyShot_Timer = 20000;
        }
        else
            KidneyShot_Timer -= diff;

        DoMeleeAttackIfReady();
    }

private:
    uint32 KidneyShot_Timer;
};

void AddSC_boss_vazruden_the_herald()
{
    RegisterHellfireRampartsCreatureAI(boss_nazan);
    RegisterHellfireRampartsCreatureAI(boss_vazruden);
    RegisterHellfireRampartsCreatureAI(boss_vazruden_the_herald);
    RegisterHellfireRampartsCreatureAI(npc_hellfire_sentry);
}