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

#include "ScriptMgr.h"
#include "blackrock_depths.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "TemporarySummon.h"
#include "WorldSession.h"

#include "EventMap.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include <sstream>

//go_shadowforge_brazier
class go_shadowforge_brazier : public GameObjectScript
{
    public:
        go_shadowforge_brazier() : GameObjectScript("go_shadowforge_brazier") { }

        struct go_shadowforge_brazierAI : public GameObjectAI
        {
            go_shadowforge_brazierAI(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

            InstanceScript* instance;

            bool OnGossipHello(Player* /*player*/) override
            {
                if (instance->GetData(TYPE_LYCEUM) == IN_PROGRESS)
                    instance->SetData(TYPE_LYCEUM, DONE);
                else
                    instance->SetData(TYPE_LYCEUM, IN_PROGRESS);
                // If used both braziers, open linked doors (North and South)
                if (instance->GetData(TYPE_LYCEUM) == DONE)
                {
                    instance->HandleGameObject(instance->GetGuidData(DATA_GOLEM_DOOR_N), true);
                    instance->HandleGameObject(instance->GetGuidData(DATA_GOLEM_DOOR_S), true);
                }

                return false;
            }
        };

        GameObjectAI* GetAI(GameObject* go) const override
        {
            return GetBlackrockDepthsAI<go_shadowforge_brazierAI>(go);
        }
};

// npc_grimstone
enum Grimstone
{
    NPC_GRIMSTONE                                          = 10096,
    NPC_THELDREN                                           = 16059,

    //4 or 6 in total? 1+2+1 / 2+2+2 / 3+3. Depending on this, code should be changed.
    MAX_NPC_AMOUNT                                         = 4
};

uint32 RingMob[]=
{
    8925,                                                   // Dredge Worm
    8926,                                                   // Deep Stinger
    8927,                                                   // Dark Screecher
    8928,                                                   // Burrowing Thundersnout
    8933,                                                   // Cave Creeper
    8932,                                                   // Borer Beetle
};

uint32 RingBoss[]=
{
    9027,                                                   // Gorosh
    9028,                                                   // Grizzle
    9029,                                                   // Eviscerator
    9030,                                                   // Ok'thor
    9031,                                                   // Anub'shiah
    9032,                                                   // Hedrum
};

class at_ring_of_law : public AreaTriggerScript
{
public:
    at_ring_of_law() : AreaTriggerScript("at_ring_of_law") { }

    bool OnTrigger(Player* player, AreaTriggerEntry const* /*at*/) override
    {
        if (InstanceScript* instance = player->GetInstanceScript())
        {
            if (instance->GetData(TYPE_RING_OF_LAW) == IN_PROGRESS || instance->GetData(TYPE_RING_OF_LAW) == DONE)
                return false;

            instance->SetData(TYPE_RING_OF_LAW, IN_PROGRESS);
            player->SummonCreature(NPC_GRIMSTONE, 625.559f, -205.618f, -52.735f, 2.609f, TEMPSUMMON_DEAD_DESPAWN);

            return false;
        }
        return false;
    }
};

// npc_grimstone
enum GrimstoneTexts
{
    SAY_TEXT1          = 0,
    SAY_TEXT2          = 1,
    SAY_TEXT3          = 2,
    SAY_TEXT4          = 3,
    SAY_TEXT5          = 4,
    SAY_TEXT6          = 5
};

static constexpr uint32 PATH_ESCORT_GRIMSTONE = 80770;

/// @todo implement quest part of event (different end boss)
class npc_grimstone : public CreatureScript
{
public:
    npc_grimstone() : CreatureScript("npc_grimstone") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetBlackrockDepthsAI<npc_grimstoneAI>(creature);
    }

    struct npc_grimstoneAI : public EscortAI
    {
        npc_grimstoneAI(Creature* creature) : EscortAI(creature)
        {
            instance = creature->GetInstanceScript();
            Initialize();
        }

        enum GrimstoneEvents
        {
            EVENT_START_INTRO       = 1,
            EVENT_RESUME_TO_POINT_1 = 2,
            EVENT_OPEN_MOB_GATE     = 3,
            EVENT_SUMMON_MOB_1      = 4,
            EVENT_SUMMON_MOB_2      = 5,
            EVENT_SUMMON_MOB_4      = 6,
            EVENT_MOB_WAVE_DONE     = 7,
            EVENT_OPEN_BOSS_GATE    = 8,
            EVENT_SUMMON_BOSS       = 9
        };

        InstanceScript* instance;

        EventMap _events;

        uint8 MobSpawnId;
        uint8 MobSpawnCount;
        uint8 MobAliveCount;

        ObjectGuid RingMobGUID[MAX_NPC_AMOUNT];
        ObjectGuid RingBossGUID;

        bool CanWalk;
        bool WaitingForMobs;
        bool WaitingForBoss;
        bool EventDone;

        void Initialize()
        {
            _events.Reset();

            MobSpawnId = rand32() % 6;

            MobSpawnCount = 0;
            MobAliveCount = 0;

            WaitingForMobs = false;
            WaitingForBoss = false;
            EventDone = false;

            for (uint8 i = 0; i < MAX_NPC_AMOUNT; ++i)
                RingMobGUID[i].Clear();

            RingBossGUID.Clear();

            CanWalk = false;
        }

        void Reset() override
        {
            Initialize();
            me->SetVisible(true);
        }

        void JustAppeared() override
        {
            /*
             * 그림스톤이 소환되면 1초 뒤 이벤트 시작.
             */
            _events.ScheduleEvent(EVENT_START_INTRO, Milliseconds(1000));
        }

        void SendRingMessageToPlayers(std::string const& text)
        {
            if (!me->GetMap())
                return;

            Map::PlayerList const& players = me->GetMap()->GetPlayers();

            if (players.isEmpty())
                return;

            for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
            {
                Player* player = itr->GetSource();

                if (!player)
                    continue;

                if (!player->IsInWorld())
                    continue;

                if (!player->GetSession())
                    continue;

                if (player->GetDistance(me) > 120.0f)
                    continue;

                player->GetSession()->SendAreaTriggerMessage(text.c_str());
            }
        }

        void SendRingMobCounter()
        {
            std::ostringstream message;
            message << "법의 심판장 몬스터: "
                    << uint32(MobAliveCount)
                    << "/"
                    << uint32(MAX_NPC_AMOUNT);

            SendRingMessageToPlayers(message.str());
        }

        void HandleGameObject(uint32 id, bool open)
        {
            if (!instance)
                return;

            instance->HandleGameObject(instance->GetGuidData(id), open);
        }

        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            /*
             * 이벤트 발생형 구조에서는 WaypointReached가 완료 처리를 하지 않는다.
             * 이동 도착 후 필요한 다음 이벤트 예약만 담당한다.
             */

            switch (waypointId)
            {
                case 0:
                    Talk(SAY_TEXT1);
                    CanWalk = false;
                    _events.ScheduleEvent(EVENT_RESUME_TO_POINT_1, Milliseconds(5000));
                    break;

                case 1:
                    Talk(SAY_TEXT2);
                    CanWalk = false;
                    _events.ScheduleEvent(EVENT_OPEN_MOB_GATE, Milliseconds(5000));
                    break;

                case 2:
                    CanWalk = false;
                    break;

                case 3:
                    Talk(SAY_TEXT3);
                    break;

                case 4:
                    Talk(SAY_TEXT4);
                    CanWalk = false;
                    break;

                case 5:
                    /*
                     * 기존 원본은 여기서 DONE 처리했지만,
                     * 이제 완료 처리는 UpdateAI의 CheckRingBossState() 한 곳에서만 한다.
                     */
                    CanWalk = false;
                    break;
            }
        }

        void SummonRingMob()
        {
            if (MobSpawnCount >= MAX_NPC_AMOUNT)
                return;

            if (Creature* summon = me->SummonCreature(RingMob[MobSpawnId], 608.960f, -235.322f, -53.907f, 1.857f, TEMPSUMMON_DEAD_DESPAWN))
            {
                RingMobGUID[MobSpawnCount] = summon->GetGUID();
                ++MobAliveCount;
            }

            ++MobSpawnCount;
        }

        void SummonRingBoss()
        {
            /*
             * 보스 완료 처리는 SummonedCreatureDies에서 하지 않는다.
             * UpdateAI의 CheckRingBossState()가 RingBossGUID를 계속 확인해서 처리한다.
             */
            WaitingForBoss = true;
            RingBossGUID.Clear();

            if (Creature* summon = me->SummonCreature(RingBoss[rand32() % 6], 644.300f, -175.989f, -53.739f, 3.418f, TEMPSUMMON_DEAD_DESPAWN))
                RingBossGUID = summon->GetGUID();
        }

        void TryFinishMobWave()
        {
            if (!WaitingForMobs)
                return;

            if (MobSpawnCount < MAX_NPC_AMOUNT)
                return;

            if (MobAliveCount)
                return;

            WaitingForMobs = false;

            SendRingMobCounter();

            _events.ScheduleEvent(EVENT_MOB_WAVE_DONE, Milliseconds(1000));
        }

        void MarkRingMobRemoved(ObjectGuid const& guid)
        {
            if (guid.IsEmpty())
                return;

            for (uint8 i = 0; i < MAX_NPC_AMOUNT; ++i)
            {
                if (RingMobGUID[i] != guid)
                    continue;

                RingMobGUID[i].Clear();

                if (MobAliveCount)
                    --MobAliveCount;

                SendRingMobCounter();

                TryFinishMobWave();
                return;
            }
        }

        void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
        {
            if (!summon)
                return;

            /*
             * 여기서는 1웨이브 쫄 카운트만 처리한다.
             * 보스 완료 처리는 CheckRingBossState() 한 곳에서만 처리한다.
             */
            MarkRingMobRemoved(summon->GetGUID());
        }

        void SummonedCreatureDespawn(Creature* summon) override
        {
            if (!summon)
                return;

            /*
             * 쫄이 죽음 훅 없이 despawn만 들어와도 카운트 처리.
             * 보스 완료 처리는 여기서 하지 않는다.
             */
            MarkRingMobRemoved(summon->GetGUID());
        }

        void CompleteRingOfLaw()
        {
            if (EventDone)
                return;

            EventDone = true;

            me->SetVisible(true);

            /*
             * 보스 사망 후 법의 심판장 창살 전부 개방.
             */
            HandleGameObject(DATA_ARENA1, true);
            HandleGameObject(DATA_ARENA2, true);
            HandleGameObject(DATA_ARENA3, true);
            HandleGameObject(DATA_ARENA4, true);

            if (instance)
            {
                instance->UpdateEncounterStateForKilledCreature(NPC_GRIMSTONE, me);
                instance->SetData(TYPE_RING_OF_LAW, DONE);
            }

            RingBossGUID.Clear();
            WaitingForBoss = false;
            WaitingForMobs = false;
            CanWalk = false;

            /*
             * 완료 후 남아있는 예약 이벤트 제거.
             * 중복 실행을 막는다.
             */
            _events.Reset();

            TC_LOG_DEBUG("scripts", "npc_grimstone: event completed by UpdateAI boss state check.");

            /*
             * 그림스톤은 법의 심판장 진행용 소환 NPC이므로
             * 완료 처리 후 3초 뒤 소환 해제한다.
             */
            me->DespawnOrUnsummon(Milliseconds(3000));
        }

        void CheckRingBossState()
        {
            if (EventDone)
                return;

            if (!WaitingForBoss)
                return;

            /*
             * 보스 소환 실패 또는 GUID가 비어 있는 상태.
             * 이 경우도 UpdateAI의 보스 체크 경로에서 완료 처리한다.
             */
            if (RingBossGUID.IsEmpty())
            {
                CompleteRingOfLaw();
                return;
            }

            Creature* boss = ObjectAccessor::GetCreature(*me, RingBossGUID);

            /*
             * 보스가 월드에서 사라졌거나 죽은 상태면 완료 처리.
             * 보스 완료는 이 함수 하나에서만 한다.
             */
            if (!boss || !boss->IsAlive() || boss->isDead())
            {
                CompleteRingOfLaw();
                return;
            }
        }

        void UpdateAI(uint32 diff) override
        {
            /*
             * 보스 완료 처리 단일 경로.
             *
             * SummonedCreatureDies / SummonedCreatureDespawn에서는 보스 완료를 처리하지 않는다.
             * WaitingForBoss 상태가 되면 UpdateAI가 계속 RingBossGUID를 확인하다가
             * 보스가 죽었거나 사라지면 CompleteRingOfLaw()를 실행한다.
             */
            CheckRingBossState();

            if (EventDone)
                return;

            _events.Update(diff);

            while (uint32 eventId = _events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_START_INTRO:
                    {
                        Talk(SAY_TEXT5);
                        HandleGameObject(DATA_ARENA4, false);

                        LoadPath(PATH_ESCORT_GRIMSTONE);
                        Start(false);

                        CanWalk = true;
                        break;
                    }

                    case EVENT_RESUME_TO_POINT_1:
                    {
                        CanWalk = true;
                        break;
                    }

                    case EVENT_OPEN_MOB_GATE:
                    {
                        HandleGameObject(DATA_ARENA1, true);
                        _events.ScheduleEvent(EVENT_SUMMON_MOB_1, Milliseconds(3000));
                        break;
                    }

                    case EVENT_SUMMON_MOB_1:
                    {
                        CanWalk = false;
                        me->SetVisible(false);

                        SummonRingMob();

                        _events.ScheduleEvent(EVENT_SUMMON_MOB_2, Milliseconds(8000));
                        break;
                    }

                    case EVENT_SUMMON_MOB_2:
                    {
                        SummonRingMob();
                        SummonRingMob();

                        _events.ScheduleEvent(EVENT_SUMMON_MOB_4, Milliseconds(8000));
                        break;
                    }

                    case EVENT_SUMMON_MOB_4:
                    {
                        SummonRingMob();

                        WaitingForMobs = true;

                        SendRingMobCounter();
                        TryFinishMobWave();
                        break;
                    }

                    case EVENT_MOB_WAVE_DONE:
                    {
                        me->SetVisible(true);

                        HandleGameObject(DATA_ARENA1, false);
                        Talk(SAY_TEXT6);

                        CanWalk = false;

                        _events.ScheduleEvent(EVENT_OPEN_BOSS_GATE, Milliseconds(5000));
                        break;
                    }

                    case EVENT_OPEN_BOSS_GATE:
                    {
                        HandleGameObject(DATA_ARENA2, true);

                        _events.ScheduleEvent(EVENT_SUMMON_BOSS, Milliseconds(5000));
                        break;
                    }

                    case EVENT_SUMMON_BOSS:
                    {
                        me->SetVisible(false);
                        CanWalk = false;

                        SummonRingBoss();
                        break;
                    }

                    default:
                        break;
                }
            }

            if (CanWalk)
                EscortAI::UpdateAI(diff);
        }
    };
};

// npc_phalanx
enum PhalanxSpells
{
    SPELL_THUNDERCLAP                   = 8732,
    SPELL_FIREBALLVOLLEY                = 22425,
    SPELL_MIGHTYBLOW                    = 14099
};

class npc_phalanx : public CreatureScript
{
public:
    npc_phalanx() : CreatureScript("npc_phalanx") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetBlackrockDepthsAI<npc_phalanxAI>(creature);
    }

    struct npc_phalanxAI : public ScriptedAI
    {
        npc_phalanxAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            ThunderClap_Timer = 12000;
            FireballVolley_Timer = 0;
            MightyBlow_Timer = 15000;
        }

        uint32 ThunderClap_Timer;
        uint32 FireballVolley_Timer;
        uint32 MightyBlow_Timer;

        void Reset() override
        {
            Initialize();
        }

        void UpdateAI(uint32 diff) override
        {
            //Return since we have no target
            if (!UpdateVictim())
                return;

            //ThunderClap_Timer
            if (ThunderClap_Timer <= diff)
            {
                DoCastVictim(SPELL_THUNDERCLAP);
                ThunderClap_Timer = 10000;
            } else ThunderClap_Timer -= diff;

            //FireballVolley_Timer
            if (HealthBelowPct(51))
            {
                if (FireballVolley_Timer <= diff)
                {
                    DoCastVictim(SPELL_FIREBALLVOLLEY);
                    FireballVolley_Timer = 15000;
                } else FireballVolley_Timer -= diff;
            }

            //MightyBlow_Timer
            if (MightyBlow_Timer <= diff)
            {
                DoCastVictim(SPELL_MIGHTYBLOW);
                MightyBlow_Timer = 10000;
            } else MightyBlow_Timer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

// npc_lokhtos_darkbargainer
enum Lokhtos
{
    QUEST_A_BINDING_CONTRACT                               = 7604,
    ITEM_SULFURON_INGOT                                    = 17203,
    ITEM_THRORIUM_BROTHERHOOD_CONTRACT                     = 18628,
    SPELL_CREATE_THORIUM_BROTHERHOOD_CONTRACT_DND          = 23059,
    GOSSIP_ITEM_SHOW_ACCESS_MID                            = 4781,       // Show me what I have access to, Lokhtos.
    GOSSIP_ITEM_SHOW_ACCESS_OID                            = 0,
};

#define GOSSIP_ITEM_GET_CONTRACT    "Get Thorium Brotherhood Contract"  // miss in db,maybe wrong

class npc_lokhtos_darkbargainer : public CreatureScript
{
    public:
        npc_lokhtos_darkbargainer() : CreatureScript("npc_lokhtos_darkbargainer") { }

        struct npc_lokhtos_darkbargainerAI : public ScriptedAI
        {
            npc_lokhtos_darkbargainerAI(Creature* creature) : ScriptedAI(creature) { }

            bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
            {
                uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

                ClearGossipMenuFor(player);
                if (action == GOSSIP_ACTION_INFO_DEF + 1)
                {
                    CloseGossipMenuFor(player);
                    player->CastSpell(player, SPELL_CREATE_THORIUM_BROTHERHOOD_CONTRACT_DND, false);
                }
                if (action == GOSSIP_ACTION_TRADE)
                    player->GetSession()->SendListInventory(me->GetGUID());

                return true;
            }

            bool OnGossipHello(Player* player) override
            {
                InitGossipMenuFor(player, GOSSIP_ITEM_SHOW_ACCESS_MID);
                if (me->IsQuestGiver())
                    player->PrepareQuestMenu(me->GetGUID());

                if (me->IsVendor() && player->GetReputationRank(59) >= REP_FRIENDLY)
                    AddGossipItemFor(player, GOSSIP_ITEM_SHOW_ACCESS_MID, GOSSIP_ITEM_SHOW_ACCESS_OID, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRADE);

                if (!player->GetQuestRewardStatus(QUEST_A_BINDING_CONTRACT) &&
                    !player->HasItemCount(ITEM_THRORIUM_BROTHERHOOD_CONTRACT, 1, true) &&
                    player->HasItemCount(ITEM_SULFURON_INGOT))
                {
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_ITEM_GET_CONTRACT, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                }

                if (player->GetReputationRank(59) < REP_FRIENDLY)
                    SendGossipMenuFor(player, 3673, me->GetGUID());
                else
                    SendGossipMenuFor(player, 3677, me->GetGUID());

                return true;
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetBlackrockDepthsAI<npc_lokhtos_darkbargainerAI>(creature);
        }
};

// npc_rocknot
enum Rocknot
{
    SAY_GOT_BEER       = 0,
    QUEST_ALE          = 4295,
    SPELL_DRUNKEN_RAGE = 14872,
    PATH_ESCORT_ROCKNOT = 76026
};

class npc_rocknot : public CreatureScript
{
public:
    npc_rocknot() : CreatureScript("npc_rocknot") { }

    struct npc_rocknotAI : public EscortAI
    {
        npc_rocknotAI(Creature* creature) : EscortAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
        }

        void Initialize()
        {
            BreakKeg_Timer = 0;
            BreakDoor_Timer = 0;
        }

        InstanceScript* instance;

        uint32 BreakKeg_Timer;
        uint32 BreakDoor_Timer;

        void Reset() override
        {
            if (HasEscortState(STATE_ESCORT_ESCORTING))
                return;

            Initialize();
        }

        void DoGo(uint32 id, uint32 state)
        {
            if (GameObject* go = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(id)))
                go->SetGoState((GOState)state);
        }

        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            switch (waypointId)
            {
                case 1:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_KICK);
                    break;
                case 2:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK_UNARMED);
                    break;
                case 3:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK_UNARMED);
                    break;
                case 4:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_KICK);
                    break;
                case 5:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_KICK);
                    BreakKeg_Timer = 2000;
                    break;
            }
        }

        void UpdateAI(uint32 diff) override
        {
            if (BreakKeg_Timer)
            {
                if (BreakKeg_Timer <= diff)
                {
                    DoGo(DATA_GO_BAR_KEG, 0);
                    BreakKeg_Timer = 0;
                    BreakDoor_Timer = 1000;
                } else BreakKeg_Timer -= diff;
            }

            if (BreakDoor_Timer)
            {
                if (BreakDoor_Timer <= diff)
                {
                    DoGo(DATA_GO_BAR_DOOR, 2);
                    DoGo(DATA_GO_BAR_KEG_TRAP, 0);               //doesn't work very well, leaving code here for future
                    //spell by trap has effect61, this indicate the bar go hostile

                    if (Unit* tmp = ObjectAccessor::GetUnit(*me, instance->GetGuidData(DATA_PHALANX)))
                        tmp->SetFaction(FACTION_MONSTER);

                    //for later, this event(s) has alot more to it.
                    //optionally, DONE can trigger bar to go hostile.
                    instance->SetData(TYPE_BAR, DONE);

                    BreakDoor_Timer = 0;
                } else BreakDoor_Timer -= diff;
            }

            EscortAI::UpdateAI(diff);
        }

        void OnQuestReward(Player* /*player*/, Quest const* quest, uint32 /*item*/) override
        {
            if (instance->GetData(TYPE_BAR) == DONE || instance->GetData(TYPE_BAR) == SPECIAL)
                return;

            if (quest->GetQuestId() == QUEST_ALE)
            {
                if (instance->GetData(TYPE_BAR) != IN_PROGRESS)
                    instance->SetData(TYPE_BAR, IN_PROGRESS);

                instance->SetData(TYPE_BAR, SPECIAL);

                //keep track of amount in instance script, returns SPECIAL if amount ok and event in progress
                if (instance->GetData(TYPE_BAR) == SPECIAL)
                {
                    Talk(SAY_GOT_BEER);
                    DoCastSelf(SPELL_DRUNKEN_RAGE, false);

                    LoadPath(PATH_ESCORT_ROCKNOT);
                    Start(false);
                }
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetBlackrockDepthsAI<npc_rocknotAI>(creature);
    }
};

void AddSC_blackrock_depths()
{
    new go_shadowforge_brazier();
    new at_ring_of_law();
    new npc_grimstone();
    new npc_phalanx();
    new npc_lokhtos_darkbargainer();
    new npc_rocknot();
}
