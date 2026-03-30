/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at your
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

#include "GameObject.h"
#include "InstanceScript.h"
#include "Player.h"
#include "Log.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "trial_of_the_champion.h"

// ★ 그리드 검색용 추가
#include "CellImpl.h"
#include "GridNotifiersImpl.h"

ObjectData const creatureData[] =
{
    { NPC_ARELAS,       DATA_ANNOUNCER    },
    { NPC_JAEREN,       DATA_ANNOUNCER    },
    { NPC_TIRION,       DATA_TIRION       },
    { NPC_VARIAN,       DATA_VARIAN       },
    { NPC_JAINA,        DATA_JAINA        },
    { NPC_GARROSH,      DATA_GARROSH      },
    { NPC_THRALL,       DATA_THRALL       },
    { NPC_BLACK_KNIGHT, DATA_BLACK_KNIGHT },
    { 0,                0                 } // END
};

ObjectData const gameObjectData[] =
{
    { GO_MAIN_GATE,           DATA_MAIN_GATE         },
    { GO_CHAMPION_S_CACHE,    DATA_CHAMPION_S_CACHE  },
    { GO_CHAMPION_S_CACHE_H,  DATA_CHAMPION_S_CACHE  },
    { GO_EADRIC_S_CACHE,      DATA_EADRIC_S_CACHE    },
    { GO_EADRIC_S_CACHE_H,    DATA_EADRIC_S_CACHE    },
    { GO_CONFESSOR_S_CACHE,   DATA_CONFESSOR_S_CACHE },
    { GO_CONFESSOR_S_CACHE_H, DATA_CONFESSOR_S_CACHE },
    { 0,                      0                      } // END
};

DoorData const doorData[] =
{
    { GO_NORTH_PORTCULLIS, DATA_GRAND_CHAMPIONS,  DOOR_TYPE_ROOM },
    { GO_NORTH_PORTCULLIS, DATA_ARGENT_CHALLENGE, DOOR_TYPE_ROOM },
    { GO_NORTH_PORTCULLIS, DATA_BLACK_KNIGHT,     DOOR_TYPE_ROOM },
    { 0,                   0,                     DOOR_TYPE_ROOM } // END
};

uint8 const GrandChampionsCount = 3;

// ─────────────────────────────────────────────────────────────
// Argent Warhorse / Battleworg 스폰 정의
//  - 진영과 무관하게, 좌표/엔트리 고정으로 번갈아 배치
// ─────────────────────────────────────────────────────────────
struct ArgentMountSpawnDef
{
    uint32  entry;
    Position pos;
};

static ArgentMountSpawnDef const kArgentMountSpawns[] =
{
    // 필요하면 좌표/각도는 자유롭게 조정 가능
    { VEHICLE_ARGENT_WARHORSE_A,   { 699.943f, 643.370f, 412.474f, 5.77704f } },
    { VEHICLE_ARGENT_BATTLEWORG_H, { 790.490f, 646.533f, 412.474f, 3.71755f } },
    { VEHICLE_ARGENT_WARHORSE_A,   { 777.564f, 660.300f, 412.467f, 4.34587f } },
    { VEHICLE_ARGENT_BATTLEWORG_H, { 704.943f, 651.330f, 412.475f, 5.60251f } },
    { VEHICLE_ARGENT_WARHORSE_A,   { 793.009f, 592.667f, 412.475f, 2.56529f } },
    { VEHICLE_ARGENT_BATTLEWORG_H, { 768.255f, 661.606f, 412.470f, 4.55531f } },

    { VEHICLE_ARGENT_WARHORSE_A,   { 722.363f, 660.745f, 412.468f, 4.83456f } },
    { VEHICLE_ARGENT_BATTLEWORG_H, { 702.967f, 587.649f, 412.475f, 0.61087f } },
    { VEHICLE_ARGENT_WARHORSE_A,   { 774.898f, 573.736f, 412.475f, 2.14675f } },
    { VEHICLE_ARGENT_BATTLEWORG_H, { 787.439f, 584.969f, 412.476f, 2.47837f } },
    { VEHICLE_ARGENT_WARHORSE_A,   { 712.594f, 576.260f, 412.476f, 0.89012f } },
    { VEHICLE_ARGENT_BATTLEWORG_H, { 720.570f, 571.285f, 412.475f, 1.06465f } },

    { VEHICLE_ARGENT_WARHORSE_A,   { 716.665f, 573.495f, 412.475f, 0.97738f } },
    { VEHICLE_ARGENT_BATTLEWORG_H, { 770.486f, 571.552f, 412.475f, 2.05949f } },
    { VEHICLE_ARGENT_WARHORSE_A,   { 700.531f, 591.927f, 412.475f, 0.52360f } },
    { VEHICLE_ARGENT_BATTLEWORG_H, { 778.741f, 576.049f, 412.476f, 2.23402f } },
    { VEHICLE_ARGENT_WARHORSE_A,   { 705.497f, 583.945f, 412.476f, 0.69813f } },
    { VEHICLE_ARGENT_BATTLEWORG_H, { 717.443f, 660.646f, 412.467f, 4.92814f } },

    { VEHICLE_ARGENT_WARHORSE_A,   { 702.165f, 647.267f, 412.475f, 5.68977f } },
    { VEHICLE_ARGENT_BATTLEWORG_H, { 726.826f, 661.201f, 412.472f, 4.66003f } },
    { VEHICLE_ARGENT_WARHORSE_A,   { 788.016f, 650.788f, 412.475f, 3.80482f } },
    { VEHICLE_ARGENT_BATTLEWORG_H, { 773.097f, 660.733f, 412.467f, 4.45059f } },
    { VEHICLE_ARGENT_WARHORSE_A,   { 793.052f, 642.851f, 412.474f, 3.63029f } },
    { VEHICLE_ARGENT_BATTLEWORG_H, { 790.177f, 589.059f, 412.475f, 2.56563f } },
};

class instance_trial_of_the_champion : public InstanceMapScript
{
public:
    instance_trial_of_the_champion() : InstanceMapScript(ToCScriptName, 650) { }

    struct instance_trial_of_the_champion_InstanceMapScript : public InstanceScript
    {
        instance_trial_of_the_champion_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(MAX_ENCOUNTER);
            LoadObjectData(creatureData, gameObjectData);
            LoadDoorData(doorData);

            _teamInInstance       = 0;
            _argentSoldierDeaths  = 0;
            _argentMountsSpawned  = false;
        }

        // ─────────────────────────────────────────────────
        // DB CreatureEntry 치환
        // ─────────────────────────────────────────────────
        uint32 GetCreatureEntry(uint32 /*guidLow*/, CreatureData const* data) override
        {
            uint32 entry = data->id;
            switch (entry)
            {
                case NPC_JAEREN:
                    if (_teamInInstance == ALLIANCE)
                        return NPC_ARELAS;
                    break;
                default:
                    break;
            }

            return entry;
        }

        // ─────────────────────────────────────────────────
        // 플레이어 입장 시 처리
        //  - 1넴 미완료면: 남은 챔피언/탈것/레서 전부 제거 + 아나운서 원위치
        //  - 이후 Warhorse/Battleworg 초기 스폰
        // ─────────────────────────────────────────────────
        void OnPlayerEnter(Player* player) override
        {
            // 인스턴스 진영 기록
            if (!_teamInInstance)
                _teamInInstance = player->GetTeam();

			WorldObject* source = player;
			auto DespawnByEntry = [source](uint32 entry)
            {
                std::list<Creature*> list;
                Trinity::AllCreaturesOfEntryInRange check(source, entry, 200.0f);
                Trinity::CreatureListSearcher<Trinity::AllCreaturesOfEntryInRange> searcher(source, list, check);
                Cell::VisitGridObjects(source, searcher, 200.0f);

                for (Creature* c : list)
                    c->DespawnOrUnsummon();
            };

            // 1넴이 아직 DONE이 아닌 경우에만 정리/초기화
            if (GetBossState(DATA_GRAND_CHAMPIONS) != DONE)
            {
                // 1) 그랜드 챔피언 본체 10명(얼/호드 모두) 제거
                static uint32 const sGrandChampEntries[] =
                {
                    // Horde
                    NPC_MOKRA,
                    NPC_ERESSEA,
                    NPC_RUNOK,
                    NPC_ZULTORE,
                    NPC_VISCERI,
                    // Alliance
                    NPC_JACOB,
                    NPC_AMBROSE,
                    NPC_COLOSOS,
                    NPC_JAELYNE,
                    NPC_LANA
                };

                for (uint32 entry : sGrandChampEntries)
                    DespawnByEntry(entry);

                // 2) 그랜드 챔피언 전용 탈것 10종 제거
                static uint32 const sGrandChampMountEntries[] =
                {
                    // Alliance mounts
                    VEHICLE_MARSHAL_JACOB_ALERIUS_MOUNT,
                    VEHICLE_AMBROSE_BOLTSPARK_MOUNT,
                    VEHICLE_COLOSOS_MOUNT,
                    VEHICLE_EVENSONG_MOUNT,
                    VEHICLE_LANA_STOUTHAMMER_MOUNT,
                    // Horde mounts
                    VEHICLE_MOKRA_SKULLCRUSHER_MOUNT,
                    VEHICLE_ERESSEA_DAWNSINGER_MOUNT,
                    VEHICLE_RUNOK_WILDMANE_MOUNT,
                    VEHICLE_ZUL_TORE_MOUNT,
                    VEHICLE_DEATHSTALKER_VESCERI_MOUNT
                };

                for (uint32 entry : sGrandChampMountEntries)
                    DespawnByEntry(entry);

                // 3) Lesser Champion 10종 제거
                static uint32 const sLesserEntries[] =
                {
                    // Horde Lesser
                    NPC_ORGRIMMAR_CHAMPION,
                    NPC_SILVERMOON_CHAMPION,
                    NPC_THUNDER_BLUFF_CHAMPION,
                    NPC_SEN_JIN_CHAMPION,
                    NPC_UNDERCITY_CHAMPION,
                    // Alliance Lesser
                    NPC_STORMWIND_CHAMPION,
                    NPC_GNOMEREGAN_CHAMPION,
                    NPC_EXODAR_CHAMPION,
                    NPC_DARNASSUS_CHAMPION,
                    NPC_IRONFORGE_CHAMPION
                };

                for (uint32 entry : sLesserEntries)
                    DespawnByEntry(entry);

                // 4) Warhorse / Battleworg 포함 차량들(_vehicles) 정리 + 방어 버프 제거
                SetData(DATA_REMOVE_VEHICLES, 0);

                // 5) 내부 GUID/카운터 리셋
                for (uint8 i = 0; i < GrandChampionsCount; ++i)
                {
                    _grandChampionGUIDs[i].Clear();
                    _grandChampionVehicleGUIDs[i].Clear();
                }
                _argentSoldierDeaths  = 0;
                _argentMountsSpawned  = false;

                // 6) 아나운서 원위치 복귀
                if (Creature* announcer = GetCreature(DATA_ANNOUNCER))
                {
                    announcer->GetMotionMaster()->Clear();
                    announcer->GetMotionMaster()->MoveTargetedHome();
                    announcer->SetReactState(REACT_PASSIVE);
                    announcer->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
					announcer->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
					announcer->AI()->Reset();
                }

				 // Warhorse / Battleworg 초기 스폰 (중복 방지는 내부 플래그로 제어)
				SpawnInitialArgentMounts();
            }

			// 2넴이 아직 DONE이 아닌 경우에만 정리/초기화
            if (GetBossState(DATA_ARGENT_CHALLENGE) != DONE)
            {
                static uint32 const sBossArgentEntries[] =
                {
                    // Argent Champions
					NPC_EADRIC,
					NPC_PALETRESS,

					NPC_ARGENT_LIGHWIELDER,
					NPC_ARGENT_MONK,
					NPC_PRIESTESS,

					// Memories
					NPC_MEMORY_HOGGER,
					NPC_MEMORY_VANCLEEF,
					NPC_MEMORY_MUTANUS,
					NPC_MEMORY_HEROD,
					NPC_MEMORY_LUCIFRON,
					NPC_MEMORY_THUNDERAAN,
					NPC_MEMORY_CHROMAGGUS,
					NPC_MEMORY_HAKKAR,
					NPC_MEMORY_VEKNILASH,
					NPC_MEMORY_KALITHRESH,
					NPC_MEMORY_MALCHEZAAR,
					NPC_MEMORY_GRUUL,
					NPC_MEMORY_VASHJ,
					NPC_MEMORY_ARCHIMONDE,
					NPC_MEMORY_ILLIDAN,
					NPC_MEMORY_DELRISSA,
					NPC_MEMORY_MURU,
					NPC_MEMORY_INGVAR,
					NPC_MEMORY_CYANIGOSA,
					NPC_MEMORY_ECK,
					NPC_MEMORY_ONYXIA,
					NPC_MEMORY_HEIGAN,
					NPC_MEMORY_IGNIS,
					NPC_MEMORY_VEZAX,
					NPC_MEMORY_ALGALON,
                };

                for (uint32 entry : sBossArgentEntries)
                    DespawnByEntry(entry);

                // 6) 아나운서 원위치 복귀
                if (Creature* announcer = GetCreature(DATA_ANNOUNCER))
                {
                    announcer->GetMotionMaster()->Clear();
                    announcer->GetMotionMaster()->MoveTargetedHome();
                    announcer->SetReactState(REACT_PASSIVE);
                    announcer->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
					announcer->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
					announcer->AI()->Reset();
                }
            }

        }

        void OnCreatureCreate(Creature* creature) override
        {
            if (!_teamInInstance)
            {
                Map::PlayerList const& players = instance->GetPlayers();
                if (!players.isEmpty())
                    if (Player* player = players.begin()->GetSource())
                        _teamInInstance = player->GetTeam();
            }

            switch (creature->GetEntry())
            {
                case NPC_MOKRA:
                case NPC_ERESSEA:
                case NPC_RUNOK:
                case NPC_ZULTORE:
                case NPC_VISCERI:
                case NPC_JACOB:
                case NPC_AMBROSE:
                case NPC_COLOSOS:
                case NPC_JAELYNE:
                case NPC_LANA:
                    SetGrandChampionData(creature);
                    break;

                // Warhorse/Battleworg 및 기타 차량들은
                // _vehicles 벡터에 기록해서 DATA_REMOVE_VEHICLES 에서 일괄 제거
                case VEHICLE_ARGENT_WARHORSE_COSMETIC:
                case VEHICLE_ARGENT_WARHORSE_A:
                case VEHICLE_ARGENT_BATTLEWORG_COSMETIC:
                case VEHICLE_ARGENT_BATTLEWORG_H:
                    _vehicles.push_back(creature->GetGUID());
                    break;

                case NPC_EADRIC:
                case NPC_PALETRESS:
                    _argentChampionGUID = creature->GetGUID();
                    break;

                case VEHICLE_BLACK_KNIGHT:
                    _blackKnightVehicleGUID = creature->GetGUID();
                    break;

                // Setting passive and unattackable flags to Lesser Champions and Grand Champions' vehicles
                case NPC_DARNASSUS_CHAMPION:
                case NPC_EXODAR_CHAMPION:
                case NPC_STORMWIND_CHAMPION:
                case NPC_GNOMEREGAN_CHAMPION:
                case NPC_IRONFORGE_CHAMPION:
                case NPC_UNDERCITY_CHAMPION:
                case NPC_THUNDER_BLUFF_CHAMPION:
                case NPC_ORGRIMMAR_CHAMPION:
                case NPC_SILVERMOON_CHAMPION:
                case NPC_SEN_JIN_CHAMPION:
                case VEHICLE_AMBROSE_BOLTSPARK_MOUNT:
                case VEHICLE_COLOSOS_MOUNT:
                case VEHICLE_MARSHAL_JACOB_ALERIUS_MOUNT:
                case VEHICLE_MOKRA_SKULLCRUSHER_MOUNT:
                case VEHICLE_ERESSEA_DAWNSINGER_MOUNT:
                case VEHICLE_RUNOK_WILDMANE_MOUNT:
                case VEHICLE_ZUL_TORE_MOUNT:
                case VEHICLE_EVENSONG_MOUNT:
                case VEHICLE_DEATHSTALKER_VESCERI_MOUNT:
                case VEHICLE_LANA_STOUTHAMMER_MOUNT:
                    creature->SetReactState(REACT_PASSIVE);
                    creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
                    break;
                default:
                    break;
            }

            InstanceScript::OnCreatureCreate(creature);
        }

        void SetGrandChampionData(Creature* creature)
        {
            for (uint8 i = 0; i < GrandChampionsCount; ++i)
            {
                if (!_grandChampionGUIDs[i])
                {
                    _grandChampionGUIDs[i] = creature->GetGUID();
                    break;
                }
            }
        }

        bool SetBossState(uint32 id, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(id, state))
                return false;

            switch (id)
            {
                case DATA_GRAND_CHAMPIONS:
                    if (state == DONE)
                    {
                        // 1넴 완료 후에는 더 이상 Warhorse/Battleworg 스폰 안 함
                        _argentMountsSpawned = false;

                        if (Creature* announcer = GetCreature(DATA_ANNOUNCER))
                        {
                            // On heroic mode we must bind players to the instance
                            if (instance->IsHeroic())
                                instance->ToInstanceMap()->PermBindAllPlayers();

                            announcer->GetMotionMaster()->MovePoint(1, announcerWaitPos);
                            announcer->AI()->SetData(DATA_GRAND_CHAMPIONS_DONE, 0);

                            DoRespawnGameObject(GetObjectGuid(DATA_CHAMPION_S_CACHE), Seconds(1 * DAY));
                            if (GameObject* cache = GetGameObject(DATA_CHAMPION_S_CACHE))
                                cache->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        }
                    }
                    break;
                case DATA_ARGENT_CHALLENGE:
                    if (state == SPECIAL)
                    {
                        if (Creature* champion = instance->GetCreature(_argentChampionGUID))
                        {
                            champion->InterruptNonMeleeSpells(true);
                            champion->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
                            champion->SetReactState(REACT_PASSIVE);
                            champion->SetHealth(1);
                            champion->CombatStop(true);
                            champion->SetRegenerateHealth(false);
                            champion->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
                            champion->SetHomePosition(745.87f, 625.88f, 411.17f, champion->GetHomePosition().GetOrientation());
                            champion->GetMotionMaster()->MoveTargetedHome();
                        }
                    }
                    else if (state == DONE)
                    {
                        if (Creature* announcer = GetCreature(DATA_ANNOUNCER))
                        {
                            // On heroic mode we must bind players to the instance
                            if (instance->IsHeroic())
                                instance->ToInstanceMap()->PermBindAllPlayers();

                            announcer->GetMotionMaster()->MovePoint(1, announcerWaitPos);

                            uint32 dataType;
                            switch (_argentChampionGUID.GetEntry())
                            {
                                case NPC_EADRIC:
                                    dataType = DATA_EADRIC_S_CACHE;
                                    break;
                                case NPC_PALETRESS:
                                    dataType = DATA_CONFESSOR_S_CACHE;
                                    break;
                                default:
                                    TC_LOG_DEBUG("script", "_argentChampionGUID == something went wrong");
                                    ABORT();
                                    break;
                            }

                            DoRespawnGameObject(GetObjectGuid(dataType), Seconds(1 * DAY));
                            if (GameObject* cache = GetGameObject(dataType))
                                cache->RemoveFlag(GO_FLAG_NOT_SELECTABLE);
                        }
                    }
                    break;
                case DATA_BLACK_KNIGHT:
                    if (state == DONE)
                    {
                        if (Creature* announcer = GetCreature(DATA_ANNOUNCER))
                            announcer->AI()->SetData(DATA_BLACK_KNIGHT_DONE, 0);
                    }
                    break;
                default:
                    break;
            }
            return true;
        }

        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case DATA_REMOVE_VEHICLES:
                {
                    // Warhorse / Battleworg 포함 모든 _vehicles 제거
                    for (ObjectGuid const& guid : _vehicles)
                        if (Creature* vehicle = instance->GetCreature(guid))
                            vehicle->DespawnOrUnsummon();

                    _vehicles.clear();

                    // 필요 시 다시 소환할 수 있도록 플래그 리셋
                    _argentMountsSpawned = false;

                    // We must remove defense spells from players
                    Map::PlayerList const& players = instance->GetPlayers();
                    for (auto itr = players.begin(); itr != players.end(); ++itr)
                    {
                        Player* plr = itr->GetSource();
                        if (!plr)
                            continue;

                        plr->RemoveAurasDueToSpell(62552); // Actual defense spell
                        plr->RemoveAurasDueToSpell(63130); // Shield Level 1 (visual only)
                        plr->RemoveAurasDueToSpell(63131); // Shield Level 2 (visual only)
                        plr->RemoveAurasDueToSpell(63132); // Shield Level 3 (visual only)
                    }
                    break;
                }
                case DATA_ARGENT_SOLDIER_DEFEATED:
                    _argentSoldierDeaths = data;
                    if (_argentSoldierDeaths == 9)
                    {
                        if (Creature* boss = instance->GetCreature(_argentChampionGUID))
                        {
                            boss->SetFaction(FACTION_MONSTER_2);
                            boss->SetHomePosition(747.02f, 637.65f, 411.57f, centerOrientation);
                            boss->GetMotionMaster()->MovePoint(1, boss->GetHomePosition());
                        }
                    }
                    break;
                default:
                    break;
            }
        }

        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_TEAM_IN_INSTANCE:
                    return _teamInInstance;
                case DATA_ARGENT_SOLDIER_DEFEATED:
                    return _argentSoldierDeaths;
                default:
                    break;
            }

            return 0;
        }

        ObjectGuid GetGuidData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_GRAND_CHAMPION_1:
                case DATA_GRAND_CHAMPION_2:
                case DATA_GRAND_CHAMPION_3:
                    return _grandChampionGUIDs[type - DATA_GRAND_CHAMPION_1];
                case DATA_GRAND_CHAMPION_VEHICLE_1:
                case DATA_GRAND_CHAMPION_VEHICLE_2:
                case DATA_GRAND_CHAMPION_VEHICLE_3:
                    return _grandChampionVehicleGUIDs[type - DATA_GRAND_CHAMPION_VEHICLE_1];
                case DATA_ARGENT_CHAMPION:
                    return _argentChampionGUID;
                case DATA_BLACK_KNIGHT_VEHICLE:
                    return _blackKnightVehicleGUID;
                default:
                    break;
            }

            return InstanceScript::GetGuidData(type);
        }

        void SetGuidData(uint32 type, ObjectGuid data) override
        {
            switch (type)
            {
                case DATA_GRAND_CHAMPION_VEHICLE_1:
                case DATA_GRAND_CHAMPION_VEHICLE_2:
                case DATA_GRAND_CHAMPION_VEHICLE_3:
                    _grandChampionVehicleGUIDs[type - DATA_GRAND_CHAMPION_VEHICLE_1] = data;
                    break;
                default:
                    break;
            }
        }

    private:
        // Warhorse/Battleworg 초기 스폰
        void SpawnInitialArgentMounts()
        {
            // 이미 스폰된 상태면 다시 안 함
            if (_argentMountsSpawned)
                return;

            // 1넴 이미 끝났으면 소환 안 함
            if (GetBossState(DATA_GRAND_CHAMPIONS) == DONE)
                return;

            // 인스턴스 진영 정보
            uint32 team = GetData(DATA_TEAM_IN_INSTANCE); // ALLIANCE / HORDE

            for (ArgentMountSpawnDef const& def : kArgentMountSpawns)
            {
                if (Creature* mount = instance->SummonCreature(def.entry, def.pos))
                {
                    mount->SetReactState(REACT_PASSIVE);
                    // OnCreatureCreate 에서 _vehicles 에 자동 등록됨 (Warhorse/Warhorse_H/코스메틱)

                    if (team == ALLIANCE && def.entry == VEHICLE_ARGENT_BATTLEWORG_H)
                        mount->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

                    if (team == HORDE && def.entry == VEHICLE_ARGENT_WARHORSE_A)
                        mount->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                }
            }

            _argentMountsSpawned = true; // ★ 실제로 플래그 활성화
        }

    private:
        ObjectGuid _grandChampionVehicleGUIDs[GrandChampionsCount];
        ObjectGuid _grandChampionGUIDs[GrandChampionsCount];

        ObjectGuid _argentChampionGUID;
        ObjectGuid _blackKnightVehicleGUID;

        GuidVector _vehicles;

        uint32 _teamInInstance;
        uint8  _argentSoldierDeaths;

        bool   _argentMountsSpawned;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_trial_of_the_champion_InstanceMapScript(map);
    }
};

void AddSC_instance_trial_of_the_champion()
{
    new instance_trial_of_the_champion();
}
