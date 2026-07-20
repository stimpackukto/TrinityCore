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
 * Custom Update Log
 * 2026-06-14: Nagrand/Halaa state save key normalized to pvp_type='nagrand'.
 * 2026-06-14: Explicit saveState arguments added to FactionTakeOver calls used by custom logic.
 * 2026-06-14: This file remains responsible only for Halaa mechanics; outdoor-war scheduling/reward logic should read pvp_type='nagrand'.
 * 2026-06-16: Halaa NPC swap changed to fixed team phases: Horde NPC phase 1, Alliance NPC phase 2.
 * 2026-06-16: Nagrand outdoor-war controller link added for neutral start, owner changes, and guard count updates.
 * 2026-06-17: Nagrand server-wide team buff handling added using CustomTeamBuffUtil.
 */

#include "OutdoorPvPNA.h"
#include "../Custom/outdoorpvp/CustomOutdoorPvpWarController.h"
#include "CustomTeamBuffUtil.h"
#include "Creature.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "OutdoorPvPMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldStatePackets.h"
#include "DatabaseEnv.h"
#include "CreatureAI.h"
#include "ObjectGuid.h"
#include <cctype>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

uint32 const NA_CREDIT_MARKER = 24867; // kill credit for pks
uint32 const NA_GUARDS_MAX = 15;
uint32 const NA_HORDE_GUARD_ENTRY = 18192;
uint32 const NA_ALLIANCE_GUARD_ENTRY = 18256;
uint32 const NA_BUFF_ZONE = 3518;
uint32 const NA_HALAA_GRAVEYARD = 993;
uint32 const NA_HALAA_GRAVEYARD_ZONE = 3518; // need to add zone id, not area id
uint32 const NA_RESPAWN_TIME = 3600000; // one hour to capture after defeating all guards
uint32 const NA_GUARD_CHECK_TIME = 500; // every half second
uint32 const NA_CUSTOM_STATE_SAVE_TIME = 1000; // one second
uint32 const NA_NPC_ACTIVE_PHASE = 1;
uint32 const NA_NPC_INACTIVE_PHASE = 2;
uint32 const NA_SUPPORT_NPC_COUNT = 15;
uint32 const NA_HORDE_NPC_MEMORY_SLOT_BASE = 3000;
uint32 const NA_ALLIANCE_NPC_MEMORY_SLOT_BASE = 4000;
uint32 const FLIGHT_NODES_NUM = 4;
uint32 const FlightPathStartNodes[FLIGHT_NODES_NUM] = { 103, 105, 107, 109 };
uint32 const FlightPathEndNodes[FLIGHT_NODES_NUM] = { 104, 106, 108, 110 };

uint32 const NA_CAPTURE_SPEED_MULTIPLIER = 1; // 테스트용: 할라아 점령 게이지 이동 속도 20배
uint32 const NA_NEUTRAL_START_CAPTURE_SPEED_MULTIPLIER = 80;

char const* NA_CUSTOM_STATE_PVP_TYPE = "nagrand";
char const* NA_CUSTOM_STATE_KEY = "state";

std::vector<ObjectGuid> CustomHalaaHordeNpcGuids;
std::vector<ObjectGuid> CustomHalaaHordeGuardNpcGuids;

std::vector<ObjectGuid> CustomHalaaAllianceNpcGuids;
std::vector<ObjectGuid> CustomHalaaAllianceGuardNpcGuids;

static void SetNagrandServerBuffTeam(uint32 ownerTeam);

uint32 const HalaaGuardTypes[NA_GUARDS_MAX] =
{
    NA_NPC_GUARD_01,
    NA_NPC_GUARD_02,
    NA_NPC_GUARD_03,
    NA_NPC_GUARD_04,
    NA_NPC_GUARD_05,
    NA_NPC_GUARD_06,
    NA_NPC_GUARD_07,
    NA_NPC_GUARD_08,
    NA_NPC_GUARD_09,
    NA_NPC_GUARD_10,
    NA_NPC_GUARD_11,
    NA_NPC_GUARD_12,
    NA_NPC_GUARD_13,
    NA_NPC_GUARD_14,
    NA_NPC_GUARD_15
};

//중립구간
void OPvPCapturePointNA::ResetForEventStartNeutral()
{
    sObjectMgr->RemoveGraveyardLink(NA_HALAA_GRAVEYARD, NA_HALAA_GRAVEYARD_ZONE, ALLIANCE, false);
    sObjectMgr->RemoveGraveyardLink(NA_HALAA_GRAVEYARD, NA_HALAA_GRAVEYARD_ZONE, HORDE, false);

    m_ControllingFaction = 0;

    m_HalaaState = HALAA_N;
    m_State = OBJECTIVESTATE_NEUTRAL;

    m_GuardsAlive = 0;
    m_capturable = true;
    m_RespawnTimer = NA_RESPAWN_TIME;

    m_WyvernStateSouth = 0;
    m_WyvernStateNorth = 0;
    m_WyvernStateEast = 0;
    m_WyvernStateWest = 0;

    m_value = 0;

    DeSpawnGOs();

    ApplyNPCPhaseForOwner();

    ApplyCustomCapturePointArtKit();
    UpdateHalaaWorldState();

    UpdateWyvernRoostWorldState(NA_ROOST_S);
    UpdateWyvernRoostWorldState(NA_ROOST_N);
    UpdateWyvernRoostWorldState(NA_ROOST_W);
    UpdateWyvernRoostWorldState(NA_ROOST_E);

    m_PvP->SendUpdateWorldState(NA_UI_HORDE_GUARDS_SHOW, 0);
    m_PvP->SendUpdateWorldState(NA_UI_ALLIANCE_GUARDS_SHOW, 0);
    m_PvP->SendUpdateWorldState(NA_UI_GUARDS_LEFT, 0);

    SetNagrandServerBuffTeam(0);

    SaveCustomState();

    OutdoorPvpWar_OnNagrandStateChanged(0, 0);
}

static bool CustomHalaaReadInt(std::string const& state, char const* key, int32& value)
{
    std::string searchKey = "\"";
    searchKey += key;
    searchKey += "\"";

    std::size_t pos = state.find(searchKey);

    if (pos == std::string::npos)
        return false;

    pos = state.find(':', pos);

    if (pos == std::string::npos)
        return false;

    ++pos;

    while (pos < state.size() && std::isspace(static_cast<unsigned char>(state[pos])))
        ++pos;

    bool negative = false;

    if (pos < state.size() && state[pos] == '-')
    {
        negative = true;
        ++pos;
    }

    if (pos >= state.size() || !std::isdigit(static_cast<unsigned char>(state[pos])))
        return false;

    int32 result = 0;

    while (pos < state.size() && std::isdigit(static_cast<unsigned char>(state[pos])))
    {
        result = result * 10 + int32(state[pos] - '0');
        ++pos;
    }

    value = negative ? -result : result;
    return true;
}

static bool CustomHalaaReadUInt(std::string const& state, char const* key, uint32& value)
{
    int32 result = 0;

    if (!CustomHalaaReadInt(state, key, result))
        return false;

    if (result < 0)
        return false;

    value = uint32(result);
    return true;
}

static bool CustomHalaaRoostIsOpen(uint32 state)
{
    return (state & WYVERN_ALLIANCE) != 0 || (state & WYVERN_HORDE) != 0;
}

// 스왑 엔피씨 구조체
enum NAAllianceSpawnType
{
    NA_ALLIANCE_SPAWN_NORMAL,
    NA_ALLIANCE_SPAWN_GUARD
};

struct NAAllianceSpawnInfo
{
    uint32 Entry;
    uint32 MapId;
    Position Pos;
    NAAllianceSpawnType Type;
};

NAAllianceSpawnInfo const NAAllianceSpawns[] =
{
    { 18817, 530, { -1591.18f, 8020.39f, -22.2042f, 4.59022f }, NA_ALLIANCE_SPAWN_NORMAL },
    { 18822, 530, { -1588.00f, 8019.00f, -22.2042f, 4.06662f }, NA_ALLIANCE_SPAWN_NORMAL },
    { 21485, 530, { -1521.93f, 7927.37f, -20.2299f, 3.24631f }, NA_ALLIANCE_SPAWN_NORMAL },
    { 21487, 530, { -1540.33f, 7971.95f, -20.7186f, 3.07178f }, NA_ALLIANCE_SPAWN_NORMAL },
    { 21488, 530, { -1570.01f, 7993.80f, -22.4505f, 5.02655f }, NA_ALLIANCE_SPAWN_NORMAL },

    { 18256, 530, { -1654.06f, 8000.46f, -26.5900f, 3.37000f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1487.18f, 7899.10f, -19.5300f, 0.95400f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1480.88f, 7908.79f, -19.1900f, 4.48500f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1540.56f, 7995.44f, -20.4500f, 0.94700f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1546.95f, 8000.85f, -20.7200f, 6.03500f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1595.31f, 7860.53f, -21.5100f, 3.74700f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1642.31f, 7995.59f, -25.8000f, 3.31700f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1545.46f, 7995.35f, -20.6300f, 1.09400f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1487.58f, 7907.99f, -19.2700f, 5.56700f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1651.54f, 7988.56f, -26.5289f, 2.98451f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1602.46f, 7866.43f, -22.1177f, 4.74729f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1591.22f, 7875.29f, -22.3536f, 4.34587f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1603.75f, 8000.36f, -24.1800f, 4.51600f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1585.73f, 7994.68f, -23.2900f, 4.43900f }, NA_ALLIANCE_SPAWN_GUARD },
    { 18256, 530, { -1595.50f, 7991.27f, -23.5300f, 4.73800f }, NA_ALLIANCE_SPAWN_GUARD }
};

enum NAHordeSpawnType
{
    NA_HORDE_SPAWN_NORMAL,
    NA_HORDE_SPAWN_GUARD
};

struct NAHordeSpawnInfo
{
    uint32 Entry;
    uint32 MapId;
    Position Pos;
    NAHordeSpawnType Type;
};

NAHordeSpawnInfo const NAHordeSpawnIns[] =
{
    { 18816, 530, { -1523.92f, 7951.76f, -17.6942f, 3.51172f }, NA_HORDE_SPAWN_NORMAL },
    { 18821, 530, { -1527.75f, 7952.46f, -17.6948f, 3.99317f }, NA_HORDE_SPAWN_NORMAL },
    { 21474, 530, { -1520.14f, 7927.11f, -20.2527f, 3.39389f }, NA_HORDE_SPAWN_NORMAL },
    { 21484, 530, { -1524.84f, 7930.34f, -20.1820f, 3.64050f }, NA_HORDE_SPAWN_NORMAL },
    { 21483, 530, { -1570.01f, 7993.80f, -22.4505f, 5.02655f }, NA_HORDE_SPAWN_NORMAL },

    { 18192, 530, { -1654.06f, 8000.46f, -26.5900f, 3.37000f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1487.18f, 7899.10f, -19.5300f, 0.95400f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1480.88f, 7908.79f, -19.1900f, 4.48500f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1540.56f, 7995.44f, -20.4500f, 0.94700f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1546.95f, 8000.85f, -20.7200f, 6.03500f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1595.31f, 7860.53f, -21.5100f, 3.74700f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1642.31f, 7995.59f, -25.8000f, 3.31700f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1545.46f, 7995.35f, -20.6300f, 1.09400f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1487.58f, 7907.99f, -19.2700f, 5.56700f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1651.54f, 7988.56f, -26.5289f, 2.98451f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1602.46f, 7866.43f, -22.1177f, 4.74729f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1591.22f, 7875.29f, -22.3536f, 4.34587f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1550.60f, 7944.45f, -21.6300f, 3.55900f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1545.57f, 7935.83f, -21.1300f, 3.44800f }, NA_HORDE_SPAWN_GUARD },
    { 18192, 530, { -1550.86f, 7937.56f, -21.7000f, 3.80100f }, NA_HORDE_SPAWN_GUARD }
};

static Creature* CustomHalaaGetNpcByGuid(Map* map, ObjectGuid const& guid)
{
    if (!map || guid.IsEmpty())
        return nullptr;

    return map->GetCreature(guid);
}

static void CustomHalaaHideNpc(Creature* creature)
{
    // 없는 NPC는 처리하지 않는다.
    if (!creature)
        return;

    // 현재 점령 진영이 아닌 NPC는 2페이즈로 숨긴다.
    // 플레이어는 1페이즈 기준으로 두고, NPC만 점령 상태에 따라 1/2페이즈를 스왑한다.
    creature->SetPhaseMask(NA_NPC_INACTIVE_PHASE, true);

    // 전투를 끊고 비공격 상태로 전환한다.
    creature->CombatStop(true);
    creature->SetReactState(REACT_PASSIVE);

    // 공격 불가 플래그를 부여한다.
    creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);

    // 죽은 경비병이나 현재 표시하지 않을 NPC는 같은 위상 안에서도 보이지 않게 한다.
    creature->SetVisible(false);
}

static void CustomHalaaApplyGuardEquipment(Creature* creature)
{
    if (!creature)
        return;

    uint32 entry = creature->GetEntry();

    if (entry != NA_HORDE_GUARD_ENTRY && entry != NA_ALLIANCE_GUARD_ENTRY)
        return;

    // 할라아 경비병은 스폰/재스폰/표시 복원 시점에 무기 표시를 고정한다.
    creature->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 0, 19019);
    creature->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 1, 0);
    creature->SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + 2, 0);
}

static uint32 g_NagrandServerBuffTeam = 0;

static void RemoveNagrandServerBuff(Player* player)
{
    if (!player)
        return;

    player->RemoveAurasDueToSpell(NA_CAPTURE_BUFF);
}

static void ApplyNagrandServerBuff(Player* player)
{
    if (!player || !player->IsInWorld())
        return;

    RemoveNagrandServerBuff(player);

    if (g_NagrandServerBuffTeam == ALLIANCE && player->GetTeam() == ALLIANCE)
    {
        CustomTeamBuffUtil::ApplySpellToPlayer(
            player,
            NA_CAPTURE_BUFF,
            -1,
            true,
            false,
            true,
            true
        );

        return;
    }

    if (g_NagrandServerBuffTeam == HORDE && player->GetTeam() == HORDE)
    {
        CustomTeamBuffUtil::ApplySpellToPlayer(
            player,
            NA_CAPTURE_BUFF,
            -1,
            true,
            false,
            true,
            true
        );

        return;
    }
}

static void ClearNagrandServerBuffForAllPlayers()
{
    CustomTeamBuffUtil::RemoveSpellFromTeam(TEAM_ALLIANCE, NA_CAPTURE_BUFF);
    CustomTeamBuffUtil::RemoveSpellFromTeam(TEAM_HORDE, NA_CAPTURE_BUFF);
}

static void UpdateNagrandServerBuffForAllPlayers()
{
    ClearNagrandServerBuffForAllPlayers();

    if (g_NagrandServerBuffTeam == ALLIANCE)
    {
        CustomTeamBuffUtil::ApplySpellToTeam(
            TEAM_ALLIANCE,
            NA_CAPTURE_BUFF,
            -1,
            true,
            false,
            true,
            true
        );

        return;
    }

    if (g_NagrandServerBuffTeam == HORDE)
    {
        CustomTeamBuffUtil::ApplySpellToTeam(
            TEAM_HORDE,
            NA_CAPTURE_BUFF,
            -1,
            true,
            false,
            true,
            true
        );

        return;
    }
}

static void SetNagrandServerBuffTeam(uint32 ownerTeam)
{
    if (ownerTeam != ALLIANCE && ownerTeam != HORDE)
        ownerTeam = 0;

    if (g_NagrandServerBuffTeam == ownerTeam)
        return;

    g_NagrandServerBuffTeam = ownerTeam;
    UpdateNagrandServerBuffForAllPlayers();
}

// spawned when the alliance is attacking, horde is in control
go_type const HordeControlGOs[NA_CONTROL_GO_NUM] =
{
    { 182267, 530, { -1815.8000f, 8036.5100f, -26.2354f, -2.897250f }, { 0.0f, 0.0f, 0.992546f, -0.121869f } }, //ALLY_ROOST_SOUTH
    { 182280, 530, { -1507.9500f, 8132.1000f, -19.5585f, -1.343900f }, { 0.0f, 0.0f, 0.622515f, -0.782608f } }, //ALLY_ROOST_WEST
    { 182281, 530, { -1384.5200f, 7779.3300f, -11.1663f, -0.575959f }, { 0.0f, 0.0f, 0.284015f, -0.958820f } }, //ALLY_ROOST_NORTH
    { 182282, 530, { -1650.1100f, 7732.5600f, -15.4505f, -2.809980f }, { 0.0f, 0.0f, 0.986286f, -0.165048f } }, //ALLY_ROOST_EAST

    { 182222, 530, { -1825.4022f, 8039.2602f, -26.0800f, -2.897250f }, { 0.0f, 0.0f, 0.992546f, -0.121869f } }, //HORDE_BOMB_WAGON_SOUTH
    { 182272, 530, { -1515.3700f, 8136.9100f, -20.4200f, -1.343900f }, { 0.0f, 0.0f, 0.622515f, -0.782608f } }, //HORDE_BOMB_WAGON_WEST
    { 182273, 530, { -1377.9500f, 7773.4400f, -10.3100f, -0.575959f }, { 0.0f, 0.0f, 0.284015f, -0.958820f } }, //HORDE_BOMB_WAGON_NORTH
    { 182274, 530, { -1659.8700f, 7733.1500f, -15.7500f, -2.809980f }, { 0.0f, 0.0f, 0.986286f, -0.165048f } }, //HORDE_BOMB_WAGON_EAST

    { 182266, 530, { -1815.8000f, 8036.5100f, -26.2354f, -2.897250f }, { 0.0f, 0.0f, 0.992546f, -0.121869f } }, //DESTROYED_ALLY_ROOST_SOUTH
    { 182275, 530, { -1507.9500f, 8132.1000f, -19.5585f, -1.343900f }, { 0.0f, 0.0f, 0.622515f, -0.782608f } }, //DESTROYED_ALLY_ROOST_WEST
    { 182276, 530, { -1384.5200f, 7779.3300f, -11.1663f, -0.575959f }, { 0.0f, 0.0f, 0.284015f, -0.958820f } }, //DESTROYED_ALLY_ROOST_NORTH
    { 182277, 530, { -1650.1100f, 7732.5600f, -15.4505f, -2.809980f }, { 0.0f, 0.0f, 0.986286f, -0.165048f } }  //DESTROYED_ALLY_ROOST_EAST
};

// spawned when the horde is attacking, alliance is in control
go_type const AllianceControlGOs[NA_CONTROL_GO_NUM] =
{
    { 182301, 530, { -1815.8000f, 8036.5100f, -26.2354f, -2.897250f }, { 0.0f, 0.0f, 0.992546f, -0.121869f } }, //HORDE_ROOST_SOUTH
    { 182302, 530, { -1507.9500f, 8132.1000f, -19.5585f, -1.343900f }, { 0.0f, 0.0f, 0.622515f, -0.782608f } }, //HORDE_ROOST_WEST
    { 182303, 530, { -1384.5200f, 7779.3300f, -11.1663f, -0.575959f }, { 0.0f, 0.0f, 0.284015f, -0.958820f } }, //HORDE_ROOST_NORTH
    { 182304, 530, { -1650.1100f, 7732.5600f, -15.4505f, -2.809980f }, { 0.0f, 0.0f, 0.986286f, -0.165048f } }, //HORDE_ROOST_EAST

    { 182305, 530, { -1825.4022f, 8039.2602f, -26.0800f, -2.897250f }, { 0.0f, 0.0f, 0.992546f, -0.121869f } }, //ALLY_BOMB_WAGON_SOUTH
    { 182306, 530, { -1515.3700f, 8136.9100f, -20.4200f, -1.343900f }, { 0.0f, 0.0f, 0.622515f, -0.782608f } }, //ALLY_BOMB_WAGON_WEST
    { 182307, 530, { -1377.9500f, 7773.4400f, -10.3100f, -0.575959f }, { 0.0f, 0.0f, 0.284015f, -0.958820f } }, //ALLY_BOMB_WAGON_NORTH
    { 182308, 530, { -1659.8700f, 7733.1500f, -15.7500f, -2.809980f }, { 0.0f, 0.0f, 0.986286f, -0.165048f } }, //ALLY_BOMB_WAGON_EAST

    { 182297, 530, { -1815.8000f, 8036.5100f, -26.2354f, -2.897250f }, { 0.0f, 0.0f, 0.992546f, -0.121869f } }, //DESTROYED_HORDE_ROOST_SOUTH
    { 182298, 530, { -1507.9500f, 8132.1000f, -19.5585f, -1.343900f }, { 0.0f, 0.0f, 0.622515f, -0.782608f } }, //DESTROYED_HORDE_ROOST_WEST
    { 182299, 530, { -1384.5200f, 7779.3300f, -11.1663f, -0.575959f }, { 0.0f, 0.0f, 0.284015f, -0.958820f } }, //DESTROYED_HORDE_ROOST_NORTH
    { 182300, 530, { -1650.1100f, 7732.5600f, -15.4505f, -2.809980f }, { 0.0f, 0.0f, 0.986286f, -0.165048f } }  //DESTROYED_HORDE_ROOST_EAST
};

static bool CustomHalaaGetControlGoTypeByEntry(uint32 ownerTeam, uint32 entry, uint32& goType)
{
    go_type const* gos = nullptr;

    if (ownerTeam == ALLIANCE)
        gos = AllianceControlGOs;
    else if (ownerTeam == HORDE)
        gos = HordeControlGOs;
    else
        return false;

    for (uint32 i = 0; i < NA_CONTROL_GO_NUM; ++i)
    {
        if (gos[i].entry == entry)
        {
            goType = i;
            return true;
        }
    }

    return false;
}

static bool CustomHalaaGetControlGoTypeByEntryAny(uint32 entry, uint32& goType)
{
    for (uint32 i = 0; i < NA_CONTROL_GO_NUM; ++i)
    {
        if (HordeControlGOs[i].entry == entry)
        {
            goType = i;
            return true;
        }

        if (AllianceControlGOs[i].entry == entry)
        {
            goType = i;
            return true;
        }
    }

    return false;
}

static uint32 CustomHalaaGetFlightSpellByRoost(uint32 roost)
{
    switch (roost)
    {
        case NA_ROOST_S:
            return NA_SPELL_FLY_SOUTH;
        case NA_ROOST_W:
            return NA_SPELL_FLY_WEST;
        case NA_ROOST_N:
            return NA_SPELL_FLY_NORTH;
        case NA_ROOST_E:
            return NA_SPELL_FLY_EAST;
        default:
            break;
    }

    return 0;
}

OutdoorPvPNA::OutdoorPvPNA()
{
    m_TypeId = OUTDOOR_PVP_NA;
    m_obj = nullptr;
}

void OutdoorPvPNA::HandleKillImpl(Player* player, Unit* killed)
{
    if (!killed)
        return;

    if (m_obj)
        m_obj->HandleGuardDeath(killed);

    if (killed->GetTypeId() == TYPEID_PLAYER && player && player->GetTeam() != killed->ToPlayer()->GetTeam())
    {
        player->KilledMonsterCredit(NA_CREDIT_MARKER); // 0 guid, btw it isn't even used in killedmonster function :S
        if (player->GetTeam() == ALLIANCE)
            player->CastSpell(player, NA_KILL_TOKEN_ALLIANCE, true);
        else
            player->CastSpell(player, NA_KILL_TOKEN_HORDE, true);
    }
}


uint32 OPvPCapturePointNA::GetAliveGuardsCount()
{
    uint32 cnt = 0;

    Map* map = m_PvP ? m_PvP->GetMap() : nullptr;

    if (!map)
        return 0;

    std::vector<ObjectGuid>* guardGuids = nullptr;

    if (m_ControllingFaction == HORDE)
        guardGuids = &CustomHalaaHordeGuardNpcGuids;
    else if (m_ControllingFaction == ALLIANCE)
        guardGuids = &CustomHalaaAllianceGuardNpcGuids;
    else
        return 0;

    for (ObjectGuid const& guid : *guardGuids)
    {
        Creature* creature = CustomHalaaGetNpcByGuid(map, guid);

        if (!creature)
            continue;

        if (creature->IsAlive()
            && !creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE)
            && (creature->GetPhaseMask() & NA_NPC_ACTIVE_PHASE))
            ++cnt;
    }

    return cnt;
}

uint32 OPvPCapturePointNA::GetControllingFaction() const
{
    return m_ControllingFaction;
}

void OPvPCapturePointNA::ApplyCustomCapturePointArtKit()
{
    uint32 artkit = 21;

    switch (m_State)
    {
        case OBJECTIVESTATE_ALLIANCE:
        case OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE:
            artkit = 2;
            break;
        case OBJECTIVESTATE_HORDE:
        case OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE:
            artkit = 1;
            break;
        default:
            if (m_ControllingFaction == ALLIANCE)
                artkit = 2;
            else if (m_ControllingFaction == HORDE)
                artkit = 1;
            break;
    }

    auto bounds = m_PvP->GetMap()->GetGameObjectBySpawnIdStore().equal_range(m_capturePointSpawnId);
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
        itr->second->SetGoArtKit(artkit);
}

void OPvPCapturePointNA::HandleGuardDeath(Unit* killed)
{
    if (!killed || killed->GetTypeId() != TYPEID_UNIT)
        return;

    uint32 entry = killed->GetEntry();

    if (m_ControllingFaction == HORDE)
    {
        if (entry != NA_HORDE_GUARD_ENTRY)
            return;
    }
    else if (m_ControllingFaction == ALLIANCE)
    {
        if (entry != NA_ALLIANCE_GUARD_ENTRY)
            return;
    }
    else
        return;

    if (m_GuardsAlive == 0)
        return;

    --m_GuardsAlive;

    if (m_GuardsAlive == 0)
        m_capturable = true;

    m_PvP->SendUpdateWorldState(NA_UI_GUARDS_LEFT, m_GuardsAlive);

    // 죽은 경비병은 즉시 숨긴다.
    // 여기서 ApplyStoredGuardVisibility를 다시 호출하면 죽은 경비병이 다시 살아날 수 있으므로 호출하지 않는다.
    if (Creature* creature = killed->ToCreature())
        CustomHalaaHideNpc(creature);

    SaveCustomState();

    OutdoorPvpWar_OnNagrandStateChanged(m_ControllingFaction, m_GuardsAlive);
}

void OPvPCapturePointNA::SaveCustomState()
{
    uint32 guardsAlive = m_GuardsAlive;

    if (guardsAlive > NA_GUARDS_MAX)
        guardsAlive = NA_GUARDS_MAX;

    float capturePercentFloat = 50.0f;

    if (m_maxValue > m_minValue)
        capturePercentFloat = ((float(m_value) - float(m_minValue)) / (float(m_maxValue) - float(m_minValue))) * 100.0f;

    if (capturePercentFloat < 0.0f)
        capturePercentFloat = 0.0f;
    else if (capturePercentFloat > 100.0f)
        capturePercentFloat = 100.0f;

    uint32 capturePercent = uint32(capturePercentFloat + 0.5f);

    std::ostringstream state;
    state << "{\"owner_team\":" << m_ControllingFaction
          << ",\"guards_alive\":" << guardsAlive
          << ",\"capturable\":" << (m_capturable ? 1 : 0)
          << ",\"halaa_state\":" << m_HalaaState
          << ",\"objective_state\":" << uint32(m_State)
          << ",\"capture_value\":" << int32(m_value)
          << ",\"capture_percent\":" << capturePercent
          << ",\"capture_min\":" << int32(m_minValue)
          << ",\"capture_max\":" << int32(m_maxValue)
          << ",\"alliance_players\":" << m_activePlayers[0].size()
          << ",\"horde_players\":" << m_activePlayers[1].size()
          << ",\"wyvern_n\":" << m_WyvernStateNorth
          << ",\"wyvern_s\":" << m_WyvernStateSouth
          << ",\"wyvern_e\":" << m_WyvernStateEast
          << ",\"wyvern_w\":" << m_WyvernStateWest
          << "}";

    std::ostringstream sql;
    sql << "INSERT INTO appdata.outdoorpvp_state "
        << "(pvp_type, state_key, state_value, updated_at) "
        << "VALUES "
        << "('" << NA_CUSTOM_STATE_PVP_TYPE << "', '" << NA_CUSTOM_STATE_KEY << "', '" << state.str() << "', NOW()) "
        << "ON DUPLICATE KEY UPDATE "
        << "state_value = VALUES(state_value), "
        << "updated_at = NOW()";

    LoginDatabase.Execute(sql.str().c_str());
}

//엔피씨 위상 컨트롤
void OPvPCapturePointNA::HideNpc(Creature* creature)
{
    creature->CombatStop();
    creature->SetReactState(REACT_PASSIVE);
    creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);
    creature->SetPhaseMask(2, true);
    creature->SetVisible(false);
}

void OPvPCapturePointNA::ShowNpc(Creature* creature, bool aggressive)
{
    if (!creature)
        return;

    creature->SetPhaseMask(1, true);

    if (!creature->IsAlive())
        creature->Respawn(true);

    creature->SetVisible(true);
    creature->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);

    if (aggressive)
    {
        creature->SetReactState(REACT_AGGRESSIVE);

        if (Unit* target = creature->SelectNearestTarget(80.0f))
            if (creature->AI())
                creature->AI()->AttackStart(target);
    }
    else
    {
        creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
        creature->SetReactState(REACT_PASSIVE);
        creature->CombatStop();
    }
}

Creature* OPvPCapturePointNA::SpawnCreature(uint32 entry, Position const& pos)
{
    Map* map = m_PvP ? m_PvP->GetMap() : nullptr;

    if (!map)
        return nullptr;

    Creature* creature = new Creature();

    if (!creature->Create(map->GenerateLowGuid<HighGuid::Unit>(), map, NA_NPC_INACTIVE_PHASE, entry, pos))
    {
        delete creature;
        return nullptr;
    }

    creature->SetHomePosition(pos);

    map->AddToMap(creature);
    creature->setActive(true);
    creature->SetFarVisible(true);

    return creature;
}

//기본 엠피시 소환
void OPvPCapturePointNA::CreateControlNPCsForTeam()
{
    CustomHalaaAllianceNpcGuids.clear();
    CustomHalaaAllianceGuardNpcGuids.clear();

    CustomHalaaHordeNpcGuids.clear();
    CustomHalaaHordeGuardNpcGuids.clear();

    for (NAAllianceSpawnInfo const& spawn : NAAllianceSpawns)
    {
        if (Creature* creature = SpawnCreature(spawn.Entry, spawn.Pos))
        {
            if (spawn.Type == NA_ALLIANCE_SPAWN_GUARD)
            {
                CustomHalaaApplyGuardEquipment(creature);
                CustomHalaaAllianceGuardNpcGuids.push_back(creature->GetGUID());
            }
            else
                CustomHalaaAllianceNpcGuids.push_back(creature->GetGUID());

            HideNpc(creature);
        }
    }

    for (NAHordeSpawnInfo const& spawn : NAHordeSpawnIns)
    {
        if (Creature* creature = SpawnCreature(spawn.Entry, spawn.Pos))
        {
            if (spawn.Type == NA_HORDE_SPAWN_GUARD)
            {
                CustomHalaaApplyGuardEquipment(creature);
                CustomHalaaHordeGuardNpcGuids.push_back(creature->GetGUID());
            }
            else
                CustomHalaaHordeNpcGuids.push_back(creature->GetGUID());

            HideNpc(creature);
        }
    }
}






void OPvPCapturePointNA::LoadCustomState()
{
    std::ostringstream loadSql;
    loadSql << "SELECT state_value "
            << "FROM appdata.outdoorpvp_state "
            << "WHERE pvp_type = '" << NA_CUSTOM_STATE_PVP_TYPE << "' "
            << "AND state_key = '" << NA_CUSTOM_STATE_KEY << "' "
            << "LIMIT 1";

    QueryResult result = LoginDatabase.Query(loadSql.str().c_str());

    if (!result)
    {
        FactionTakeOver(HORDE, false);
        SaveCustomState();
        return;
    }

    Field* fields = result->Fetch();
    std::string state = fields[0].GetString();

    uint32 ownerTeam = 0;

    if (!CustomHalaaReadUInt(state, "owner_team", ownerTeam))
    {
        FactionTakeOver(HORDE, false);
        SaveCustomState();
        return;
    }

    uint32 guardsAlive = NA_GUARDS_MAX;
    uint32 capturable = 0;
    uint32 halaaState = HALAA_N;
    uint32 objectiveState = OBJECTIVESTATE_NEUTRAL;
    uint32 wyvernNorth = 0;
    uint32 wyvernSouth = 0;
    uint32 wyvernEast = 0;
    uint32 wyvernWest = 0;
    int32 captureValue = 0;
    int32 captureMin = int32(m_minValue);
    int32 captureMax = int32(m_maxValue);

    CustomHalaaReadUInt(state, "guards_alive", guardsAlive);
    CustomHalaaReadUInt(state, "capturable", capturable);
    CustomHalaaReadUInt(state, "halaa_state", halaaState);
    CustomHalaaReadUInt(state, "objective_state", objectiveState);
    CustomHalaaReadInt(state, "capture_value", captureValue);
    CustomHalaaReadInt(state, "capture_min", captureMin);
    CustomHalaaReadInt(state, "capture_max", captureMax);
    CustomHalaaReadUInt(state, "wyvern_n", wyvernNorth);
    CustomHalaaReadUInt(state, "wyvern_s", wyvernSouth);
    CustomHalaaReadUInt(state, "wyvern_e", wyvernEast);
    CustomHalaaReadUInt(state, "wyvern_w", wyvernWest);

    if (guardsAlive > NA_GUARDS_MAX)
        guardsAlive = NA_GUARDS_MAX;

    if (ownerTeam != ALLIANCE && ownerTeam != HORDE)
    {
        FactionTakeOver(HORDE, false);
        SaveCustomState();
        return;
    }

    sObjectMgr->RemoveGraveyardLink(NA_HALAA_GRAVEYARD, NA_HALAA_GRAVEYARD_ZONE, ALLIANCE, false);
    sObjectMgr->RemoveGraveyardLink(NA_HALAA_GRAVEYARD, NA_HALAA_GRAVEYARD_ZONE, HORDE, false);

    m_ControllingFaction = ownerTeam;

    sObjectMgr->AddGraveyardLink(NA_HALAA_GRAVEYARD, NA_HALAA_GRAVEYARD_ZONE, m_ControllingFaction, false);

    m_HalaaState = halaaState;
    m_GuardsAlive = guardsAlive;
    m_capturable = guardsAlive == 0 && capturable != 0;

    if (m_GuardsAlive > 0)
        m_capturable = false;

    m_WyvernStateNorth = wyvernNorth;
    m_WyvernStateSouth = wyvernSouth;
    m_WyvernStateEast = wyvernEast;
    m_WyvernStateWest = wyvernWest;

    switch (objectiveState)
    {
        case OBJECTIVESTATE_NEUTRAL:
            m_State = OBJECTIVESTATE_NEUTRAL;
            break;
        case OBJECTIVESTATE_ALLIANCE:
            m_State = OBJECTIVESTATE_ALLIANCE;
            break;
        case OBJECTIVESTATE_HORDE:
            m_State = OBJECTIVESTATE_HORDE;
            break;
        case OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE:
            m_State = OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE;
            break;
        case OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE:
            m_State = OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE;
            break;
        case OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE:
            m_State = OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE;
            break;
        case OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE:
            m_State = OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE;
            break;
        default:
            m_State = ownerTeam == ALLIANCE ? OBJECTIVESTATE_ALLIANCE : OBJECTIVESTATE_HORDE;
            break;
    }

    m_value = captureValue;
    m_minValue = captureMin;
    m_maxValue = captureMax;

    ApplyCustomCapturePointArtKit();

    ApplyNPCPhaseForOwner();

    DeSpawnGOs();

    go_type const* gos = ownerTeam == ALLIANCE ? AllianceControlGOs : HordeControlGOs;

    auto restoreRoost = [this, gos](uint32 destroyedRoost, uint32 roost, uint32 bombWagon, uint32 roostState)
    {
        DelObject(destroyedRoost);
        DelObject(roost);
        DelObject(bombWagon);

        if (CustomHalaaRoostIsOpen(roostState))
        {
            AddObject(roost, gos[roost].entry, gos[roost].map, gos[roost].pos, gos[roost].rot);
            AddObject(bombWagon, gos[bombWagon].entry, gos[bombWagon].map, gos[bombWagon].pos, gos[bombWagon].rot);
        }
        else
        {
            AddObject(destroyedRoost, gos[destroyedRoost].entry, gos[destroyedRoost].map, gos[destroyedRoost].pos, gos[destroyedRoost].rot);
        }
    };

    restoreRoost(NA_DESTROYED_ROOST_S, NA_ROOST_S, NA_BOMB_WAGON_S, m_WyvernStateSouth);
    restoreRoost(NA_DESTROYED_ROOST_N, NA_ROOST_N, NA_BOMB_WAGON_N, m_WyvernStateNorth);
    restoreRoost(NA_DESTROYED_ROOST_W, NA_ROOST_W, NA_BOMB_WAGON_W, m_WyvernStateWest);
    restoreRoost(NA_DESTROYED_ROOST_E, NA_ROOST_E, NA_BOMB_WAGON_E, m_WyvernStateEast);

    m_PvP->SendUpdateWorldState(NA_UI_HORDE_GUARDS_SHOW, ownerTeam == HORDE ? 1 : 0);
    m_PvP->SendUpdateWorldState(NA_UI_ALLIANCE_GUARDS_SHOW, ownerTeam == ALLIANCE ? 1 : 0);
    m_PvP->SendUpdateWorldState(NA_UI_GUARDS_LEFT, m_GuardsAlive);

    UpdateHalaaWorldState();
    UpdateWyvernRoostWorldState(NA_ROOST_S);
    UpdateWyvernRoostWorldState(NA_ROOST_N);
    UpdateWyvernRoostWorldState(NA_ROOST_W);
    UpdateWyvernRoostWorldState(NA_ROOST_E);

    SetNagrandServerBuffTeam(m_ControllingFaction);
}





















void OPvPCapturePointNA::ApplyStoredGuardVisibility(uint32 team, uint32 guardsAlive)
{
    if (team != ALLIANCE && team != HORDE)
        return;

    if (guardsAlive > NA_GUARDS_MAX)
        guardsAlive = NA_GUARDS_MAX;

    m_GuardsAlive = guardsAlive;

    // 여기서는 NPC를 생성하지 않는다.
    // SetupOutdoorPvP에서 생성된 GUID 기준으로 표시/숨김만 처리한다.
    ApplyNPCPhaseForOwner();
}


void OPvPCapturePointNA::ApplyNPCPhaseForOwner()
{
    ApplyNPCPhaseForOwner(false);
}

void OPvPCapturePointNA::ApplyNPCPhaseForOwner(bool respawnGuards)
{
    Map* map = m_PvP ? m_PvP->GetMap() : nullptr;

    if (!map)
        return;

    auto hideList = [this, map](std::vector<ObjectGuid> const& guids)
    {
        for (ObjectGuid const& guid : guids)
            if (Creature* creature = CustomHalaaGetNpcByGuid(map, guid))
                HideNpc(creature);
    };

    hideList(CustomHalaaHordeNpcGuids);
    hideList(CustomHalaaHordeGuardNpcGuids);
    hideList(CustomHalaaAllianceNpcGuids);
    hideList(CustomHalaaAllianceGuardNpcGuids);

    std::vector<ObjectGuid>* normalGuids = nullptr;
    std::vector<ObjectGuid>* guardGuids = nullptr;

    if (m_ControllingFaction == HORDE)
    {
        normalGuids = &CustomHalaaHordeNpcGuids;
        guardGuids = &CustomHalaaHordeGuardNpcGuids;
    }
    else if (m_ControllingFaction == ALLIANCE)
    {
        normalGuids = &CustomHalaaAllianceNpcGuids;
        guardGuids = &CustomHalaaAllianceGuardNpcGuids;
    }
    else
        return;

    for (ObjectGuid const& guid : *normalGuids)
        if (Creature* creature = CustomHalaaGetNpcByGuid(map, guid))
            ShowNpc(creature, false);

    uint32 guardsToShow = m_GuardsAlive;

    if (guardsToShow > NA_GUARDS_MAX)
        guardsToShow = NA_GUARDS_MAX;

    uint32 shownGuards = 0;

    for (ObjectGuid const& guid : *guardGuids)
    {
        Creature* creature = CustomHalaaGetNpcByGuid(map, guid);

        if (!creature)
            continue;

        if (shownGuards >= guardsToShow)
        {
            HideNpc(creature);
            continue;
        }

        if (!creature->IsAlive())
        {
            if (!respawnGuards)
            {
                HideNpc(creature);
                continue;
            }

            creature->Respawn(true);
        }

        CustomHalaaApplyGuardEquipment(creature);
        ShowNpc(creature, true);
        ++shownGuards;
    }
}

void OPvPCapturePointNA::SpawnNPCsForTeam(uint32 team)
{
    if (team != ALLIANCE && team != HORDE)
        return;

    // NPC 생성 금지.
    // 점령 전환 시에는 새 점령 진영 가드를 다시 살려서 표시한다.
    ApplyNPCPhaseForOwner(true);
}

void OPvPCapturePointNA::SpawnGOsForTeam(uint32 team)
{
    go_type const* gos = nullptr;
    if (team == ALLIANCE)
        gos = AllianceControlGOs;
    else if (team == HORDE)
        gos = HordeControlGOs;
    else
        return;

    // roosts and bomb wagons are spawned when someone uses the matching destroyed roost
    static ControlGOTypes const GoTypes[] =
    {
        NA_DESTROYED_ROOST_S,
        NA_DESTROYED_ROOST_W,
        NA_DESTROYED_ROOST_N,
        NA_DESTROYED_ROOST_E
    };

    for (ControlGOTypes goType : GoTypes)
        AddObject(goType, gos[goType].entry, gos[goType].map, gos[goType].pos, gos[goType].rot);
}

void OPvPCapturePointNA::DeSpawnGOs()
{
    for (int i = 0; i < NA_CONTROL_GO_NUM; ++i)
    {
        DelObject(i);
    }
}

void OPvPCapturePointNA::FactionTakeOver(uint32 team, bool saveState)
{
    if (m_ControllingFaction)
        sObjectMgr->RemoveGraveyardLink(NA_HALAA_GRAVEYARD, NA_HALAA_GRAVEYARD_ZONE, m_ControllingFaction, false);

    m_ControllingFaction = team;
    if (m_ControllingFaction)
        sObjectMgr->AddGraveyardLink(NA_HALAA_GRAVEYARD, NA_HALAA_GRAVEYARD_ZONE, m_ControllingFaction, false);


    DeSpawnGOs();
    SpawnGOsForTeam(team);

    // 점령이 바뀌면 경비병 생존 수를 먼저 초기화한다.
    m_GuardsAlive = NA_GUARDS_MAX;
    m_capturable = false;

    // 호드/얼라이언스 NPC는 이미 메모리에 올라간 GUID 기준으로 표시/숨김만 제어한다.
    SpawnNPCsForTeam(team);

    SetNagrandServerBuffTeam(team);

    OutdoorPvpWar_OnNagrandStateChanged(m_ControllingFaction, m_GuardsAlive);

    this->UpdateHalaaWorldState();
    if (team == ALLIANCE)
    {
        m_WyvernStateSouth = WYVERN_NEU_HORDE;
        m_WyvernStateNorth = WYVERN_NEU_HORDE;
        m_WyvernStateEast = WYVERN_NEU_HORDE;
        m_WyvernStateWest = WYVERN_NEU_HORDE;
        m_PvP->SendUpdateWorldState(NA_UI_HORDE_GUARDS_SHOW, 0);
        m_PvP->SendUpdateWorldState(NA_UI_ALLIANCE_GUARDS_SHOW, 1);
        m_PvP->SendUpdateWorldState(NA_UI_GUARDS_LEFT, m_GuardsAlive);
        m_PvP->SendDefenseMessage(NA_HALAA_GRAVEYARD_ZONE, TEXT_HALAA_TAKEN_ALLIANCE);
    }
    else
    {
        m_WyvernStateSouth = WYVERN_NEU_ALLIANCE;
        m_WyvernStateNorth = WYVERN_NEU_ALLIANCE;
        m_WyvernStateEast = WYVERN_NEU_ALLIANCE;
        m_WyvernStateWest = WYVERN_NEU_ALLIANCE;
        m_PvP->SendUpdateWorldState(NA_UI_HORDE_GUARDS_SHOW, 1);
        m_PvP->SendUpdateWorldState(NA_UI_ALLIANCE_GUARDS_SHOW, 0);
        m_PvP->SendUpdateWorldState(NA_UI_GUARDS_LEFT, m_GuardsAlive);
        m_PvP->SendDefenseMessage(NA_HALAA_GRAVEYARD_ZONE, TEXT_HALAA_TAKEN_HORDE);
    }
    UpdateWyvernRoostWorldState(NA_ROOST_S);
    UpdateWyvernRoostWorldState(NA_ROOST_N);
    UpdateWyvernRoostWorldState(NA_ROOST_W);
    UpdateWyvernRoostWorldState(NA_ROOST_E);

    if (saveState)
        SaveCustomState();
}

OPvPCapturePointNA::OPvPCapturePointNA(OutdoorPvP* pvp) : OPvPCapturePoint(pvp), m_capturable(true), m_GuardsAlive(0), m_ControllingFaction(0), m_WyvernStateNorth(0), m_WyvernStateSouth(0), m_WyvernStateEast(0),
    m_WyvernStateWest(0), m_HalaaState(HALAA_N), m_RespawnTimer(NA_RESPAWN_TIME), m_GuardCheckTimer(NA_GUARD_CHECK_TIME), m_CustomStateSaveTimer(NA_CUSTOM_STATE_SAVE_TIME)
{
    SetCapturePointData(182210, 530, { -1572.57f, 7945.3f, -22.475f, 2.05949f }, { 0.0f, 0.0f, 0.857167f, 0.515038f });
}

bool OutdoorPvPNA::SetupOutdoorPvP()
{
    // m_TypeId = OUTDOOR_PVP_NA; _MUST_ be set in ctor, because of spawns cleanup

    SetMapFromZone(NA_BUFF_ZONE);
    RegisterZone(NA_BUFF_ZONE);

    m_obj = new OPvPCapturePointNA(this);

    AddCapturePoint(m_obj);

    // 기본 NPC는 서버 시작 시 여기서 1번만 생성한다.
    m_obj->CreateControlNPCsForTeam();

    // DB에 저장된 할라아 점령 상태를 복원한다.
    // 이후부터는 NPC를 다시 생성하지 않고 표시/숨김만 처리한다.
    m_obj->LoadCustomState();

    return true;
}

void OutdoorPvPNA::HandlePlayerEnterZone(Player* player, uint32 zone)
{
    m_obj->ApplyCustomCapturePointArtKit();
    m_obj->ApplyNPCPhaseForOwner();

    ApplyNagrandServerBuff(player);

    OutdoorPvP::HandlePlayerEnterZone(player, zone);
}

void OutdoorPvPNA::HandlePlayerLeaveZone(Player* player, uint32 zone)
{
    OutdoorPvP::HandlePlayerLeaveZone(player, zone);
}

void OutdoorPvPNA::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    m_obj->ApplyCustomCapturePointArtKit();
    m_obj->ApplyNPCPhaseForOwner();
    m_obj->FillInitialWorldStates(packet);
}

void OPvPCapturePointNA::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    if (m_ControllingFaction == ALLIANCE)
    {
        packet.Worldstates.emplace_back(NA_UI_HORDE_GUARDS_SHOW, 0);
        packet.Worldstates.emplace_back(NA_UI_ALLIANCE_GUARDS_SHOW, 1);
    }
    else if (m_ControllingFaction == HORDE)
    {
        packet.Worldstates.emplace_back(NA_UI_HORDE_GUARDS_SHOW, 1);
        packet.Worldstates.emplace_back(NA_UI_ALLIANCE_GUARDS_SHOW, 0);
    }
    else
    {
        packet.Worldstates.emplace_back(NA_UI_HORDE_GUARDS_SHOW, 0);
        packet.Worldstates.emplace_back(NA_UI_ALLIANCE_GUARDS_SHOW, 0);
    }

    packet.Worldstates.emplace_back(NA_UI_GUARDS_MAX, NA_GUARDS_MAX);
    packet.Worldstates.emplace_back(NA_UI_GUARDS_LEFT, m_GuardsAlive);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_NORTH_NEU_H, (m_WyvernStateNorth & WYVERN_NEU_HORDE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_NORTH_NEU_A, (m_WyvernStateNorth & WYVERN_NEU_ALLIANCE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_NORTH_H, (m_WyvernStateNorth & WYVERN_HORDE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_NORTH_A, (m_WyvernStateNorth & WYVERN_ALLIANCE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_SOUTH_NEU_H, (m_WyvernStateSouth & WYVERN_NEU_HORDE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_SOUTH_NEU_A, (m_WyvernStateSouth & WYVERN_NEU_ALLIANCE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_SOUTH_H, (m_WyvernStateSouth & WYVERN_HORDE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_SOUTH_A, (m_WyvernStateSouth & WYVERN_ALLIANCE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_WEST_NEU_H, (m_WyvernStateWest & WYVERN_NEU_HORDE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_WEST_NEU_A, (m_WyvernStateWest & WYVERN_NEU_ALLIANCE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_WEST_H, (m_WyvernStateWest & WYVERN_HORDE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_WEST_A, (m_WyvernStateWest & WYVERN_ALLIANCE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_EAST_NEU_H, (m_WyvernStateEast & WYVERN_NEU_HORDE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_EAST_NEU_A, (m_WyvernStateEast & WYVERN_NEU_ALLIANCE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_EAST_H, (m_WyvernStateEast & WYVERN_HORDE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_WYVERN_EAST_A, (m_WyvernStateEast & WYVERN_ALLIANCE) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_HALAA_NEUTRAL, (m_HalaaState & HALAA_N) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_HALAA_NEU_A, (m_HalaaState & HALAA_N_A) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_HALAA_NEU_H, (m_HalaaState & HALAA_N_H) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_HALAA_HORDE, (m_HalaaState & HALAA_H) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(NA_MAP_HALAA_ALLIANCE, (m_HalaaState & HALAA_A) != 0 ? 1 : 0);
}

void OutdoorPvPNA::SendRemoveWorldStates(Player* player)
{
    player->SendUpdateWorldState(NA_UI_HORDE_GUARDS_SHOW, 0);
    player->SendUpdateWorldState(NA_UI_ALLIANCE_GUARDS_SHOW, 0);
    player->SendUpdateWorldState(NA_UI_GUARDS_MAX, 0);
    player->SendUpdateWorldState(NA_UI_GUARDS_LEFT, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_NORTH_NEU_H, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_NORTH_NEU_A, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_NORTH_H, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_NORTH_A, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_SOUTH_NEU_H, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_SOUTH_NEU_A, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_SOUTH_H, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_SOUTH_A, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_WEST_NEU_H, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_WEST_NEU_A, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_WEST_H, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_WEST_A, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_EAST_NEU_H, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_EAST_NEU_A, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_EAST_H, 0);
    player->SendUpdateWorldState(NA_MAP_WYVERN_EAST_A, 0);
    player->SendUpdateWorldState(NA_MAP_HALAA_NEUTRAL, 0);
    player->SendUpdateWorldState(NA_MAP_HALAA_NEU_A, 0);
    player->SendUpdateWorldState(NA_MAP_HALAA_NEU_H, 0);
    player->SendUpdateWorldState(NA_MAP_HALAA_HORDE, 0);
    player->SendUpdateWorldState(NA_MAP_HALAA_ALLIANCE, 0);
}

bool OutdoorPvPNA::Update(uint32 diff)
{
    return m_obj->Update(diff);
}


static void CustomHalaaGiveBombs(Player* player)
{
    if (!player)
        return;

    uint32 noSpaceForCount = 0;
    ItemPosCountVec dest;

    int32 count = 10;
    uint32 itemid = 24538;

    InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemid, count, &noSpaceForCount);

    if (msg != EQUIP_ERR_OK)
        count -= noSpaceForCount;

    if (count <= 0 || dest.empty())
        return;

    Item* item = player->StoreNewItem(dest, itemid, true);

    if (item)
        player->SendNewItem(item, count, true, false);
}

static bool CustomHalaaStartWyvernFlight(Player* player, uint32 roost, uint32 /*spellId*/)
{
    if (!player)
        return false;

    if (roost >= FLIGHT_NODES_NUM)
        return false;

    // 같은 클릭에서 HandleOpenGo()와 HandleCustomSpell()이 연속으로 들어올 수 있다.
    // 같은 플레이어가 같은 초에 다시 들어오면 이미 비행 시작 처리된 것으로 본다.
    static ObjectGuid::LowType lastTaxiPlayerGuid = 0;
    static std::time_t lastTaxiStartedAt = 0;

    std::time_t now = std::time(nullptr);
    ObjectGuid::LowType playerGuid = player->GetGUID().GetCounter();

    if (lastTaxiPlayerGuid == playerGuid && lastTaxiStartedAt == now)
        return true;

    std::vector<uint32> nodes;
    nodes.reserve(2);
    nodes.push_back(FlightPathStartNodes[roost]);
    nodes.push_back(FlightPathEndNodes[roost]);

    // 할라아 와이번은 캐릭터 본체만 움직이면 안 된다.
    // 반드시 택시 경로를 시작해서 클라이언트가 와이번에 탑승한 상태로 이동해야 한다.
    // 아제로스코어 원본처럼 spellId를 넘기지 않고 nodes만 넘긴다.
    if (!player->ActivateTaxiPathTo(nodes))
        return false;

    lastTaxiPlayerGuid = playerGuid;
    lastTaxiStartedAt = now;

    CustomHalaaGiveBombs(player);

    player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP);
    player->UpdatePvP(true, true);

    return true;
}

bool OPvPCapturePointNA::HandleCustomSpell(Player* /*player*/, uint32 spellId, GameObject* /*go*/)
{
    // 복구된 와이번 둥지 클릭 비행은 go_halaa_wyvern_taxi::OnReportUse 한 곳에서만 처리한다.
    // 이 함수는 GAMEOBJECT_TYPE_SPELLCASTER 경로로 들어온 와이번 주문을 소비만 해서
    // 기본 주문/택시 흐름으로 중복 진입하지 않게 막는다.
    switch (spellId)
    {
        case NA_SPELL_FLY_SOUTH:
        case NA_SPELL_FLY_NORTH:
        case NA_SPELL_FLY_WEST:
        case NA_SPELL_FLY_EAST:
            return true;
        default:
            break;
    }

    return false;
}

int32 OPvPCapturePointNA::HandleOpenGo(Player* player, GameObject* go)
{
    if (!player || !go)
        return -1;

    int32 retval = -1;
    uint32 goType = 0;

    // 복구된 와이번/화약마차/파괴된 와이번은 Entry 기준으로 직접 판정한다.
    // 점령 진영값이나 DB 저장 상태가 꼬여도 호드/얼라 GO 배열 전체에서 찾는다.
    if (CustomHalaaGetControlGoTypeByEntryAny(go->GetEntry(), goType))
        retval = int32(goType);
    else
        retval = OPvPCapturePoint::HandleOpenGo(player, go);

    if (retval < 0 || retval >= int32(NA_CONTROL_GO_NUM))
        return -1;

    go_type const* gos = nullptr;

    if (m_ControllingFaction == ALLIANCE)
        gos = AllianceControlGOs;
    else if (m_ControllingFaction == HORDE)
        gos = HordeControlGOs;
    else
        return -1;

    int32 del = -1;
    int32 del2 = -1;
    int32 add = -1;
    int32 add2 = -1;

    switch (retval)
    {
        // 복구된 와이번 보금자리 클릭 비행은 go_halaa_wyvern_taxi::OnReportUse 한 곳에서만 처리한다.
        // HandleOpenGo에서는 파괴된 둥지 복구와 화약마차 처리만 담당한다.
        case NA_ROOST_S:
        case NA_ROOST_N:
        case NA_ROOST_W:
        case NA_ROOST_E:
            return -1;

        // 파괴된 남쪽 와이번 보금자리 클릭
        // 파괴된 보금자리 제거 → 복구 보금자리 + 화약마차 생성.
        case NA_DESTROYED_ROOST_S:
            del = NA_DESTROYED_ROOST_S;
            add = NA_ROOST_S;
            add2 = NA_BOMB_WAGON_S;

            if (m_ControllingFaction == HORDE)
                m_WyvernStateSouth = WYVERN_ALLIANCE;
            else
                m_WyvernStateSouth = WYVERN_HORDE;

            UpdateWyvernRoostWorldState(NA_ROOST_S);
            break;

        // 파괴된 북쪽 와이번 보금자리 클릭
        case NA_DESTROYED_ROOST_N:
            del = NA_DESTROYED_ROOST_N;
            add = NA_ROOST_N;
            add2 = NA_BOMB_WAGON_N;

            if (m_ControllingFaction == HORDE)
                m_WyvernStateNorth = WYVERN_ALLIANCE;
            else
                m_WyvernStateNorth = WYVERN_HORDE;

            UpdateWyvernRoostWorldState(NA_ROOST_N);
            break;

        // 파괴된 서쪽 와이번 보금자리 클릭
        case NA_DESTROYED_ROOST_W:
            del = NA_DESTROYED_ROOST_W;
            add = NA_ROOST_W;
            add2 = NA_BOMB_WAGON_W;

            if (m_ControllingFaction == HORDE)
                m_WyvernStateWest = WYVERN_ALLIANCE;
            else
                m_WyvernStateWest = WYVERN_HORDE;

            UpdateWyvernRoostWorldState(NA_ROOST_W);
            break;

        // 파괴된 동쪽 와이번 보금자리 클릭
        case NA_DESTROYED_ROOST_E:
            del = NA_DESTROYED_ROOST_E;
            add = NA_ROOST_E;
            add2 = NA_BOMB_WAGON_E;

            if (m_ControllingFaction == HORDE)
                m_WyvernStateEast = WYVERN_ALLIANCE;
            else
                m_WyvernStateEast = WYVERN_HORDE;

            UpdateWyvernRoostWorldState(NA_ROOST_E);
            break;

        // 남쪽 화약마차 클릭
        // 화약마차 + 복구 보금자리 제거 → 파괴된 보금자리로 복귀.
        case NA_BOMB_WAGON_S:
            del = NA_BOMB_WAGON_S;
            del2 = NA_ROOST_S;
            add = NA_DESTROYED_ROOST_S;

            if (m_ControllingFaction == HORDE)
                m_WyvernStateSouth = WYVERN_NEU_ALLIANCE;
            else
                m_WyvernStateSouth = WYVERN_NEU_HORDE;

            UpdateWyvernRoostWorldState(NA_ROOST_S);
            break;

        // 북쪽 화약마차 클릭
        case NA_BOMB_WAGON_N:
            del = NA_BOMB_WAGON_N;
            del2 = NA_ROOST_N;
            add = NA_DESTROYED_ROOST_N;

            if (m_ControllingFaction == HORDE)
                m_WyvernStateNorth = WYVERN_NEU_ALLIANCE;
            else
                m_WyvernStateNorth = WYVERN_NEU_HORDE;

            UpdateWyvernRoostWorldState(NA_ROOST_N);
            break;

        // 서쪽 화약마차 클릭
        case NA_BOMB_WAGON_W:
            del = NA_BOMB_WAGON_W;
            del2 = NA_ROOST_W;
            add = NA_DESTROYED_ROOST_W;

            if (m_ControllingFaction == HORDE)
                m_WyvernStateWest = WYVERN_NEU_ALLIANCE;
            else
                m_WyvernStateWest = WYVERN_NEU_HORDE;

            UpdateWyvernRoostWorldState(NA_ROOST_W);
            break;

        // 동쪽 화약마차 클릭
        case NA_BOMB_WAGON_E:
            del = NA_BOMB_WAGON_E;
            del2 = NA_ROOST_E;
            add = NA_DESTROYED_ROOST_E;

            if (m_ControllingFaction == HORDE)
                m_WyvernStateEast = WYVERN_NEU_ALLIANCE;
            else
                m_WyvernStateEast = WYVERN_NEU_HORDE;

            UpdateWyvernRoostWorldState(NA_ROOST_E);
            break;

        default:
            return -1;
    }

    if (del > -1)
        DelObject(del);

    if (del2 > -1)
        DelObject(del2);

    if (add > -1)
    {
        DelObject(add);
        AddObject(add, gos[add].entry, gos[add].map, gos[add].pos, gos[add].rot);
    }

    if (add2 > -1)
    {
        DelObject(add2);
        AddObject(add2, gos[add2].entry, gos[add2].map, gos[add2].pos, gos[add2].rot);
    }

    SaveCustomState();

    return retval;
}

bool OPvPCapturePointNA::Update(uint32 diff)
{
    if (OutdoorPvpWar_ConsumeNagrandNeutralResetRequest())
        ResetForEventStartNeutral();

    ApplyCustomCapturePointArtKit();

    // NPC 표시/숨김은 매 틱마다 반복하지 않는다.
    // 점령 변경, 서버 상태 복원, 플레이어 진입, 경비병 사망 시점에서만 처리한다.

    if (m_GuardCheckTimer < diff)
    {
        m_GuardCheckTimer = NA_GUARD_CHECK_TIME;

        // 폭탄/비행 공격은 일반 플레이어 킬 이벤트로 항상 들어오지 않을 수 있다.
        // 그래서 실제로 살아 있는 경비병 수를 주기적으로 확인해서
        // HandleGuardDeath()가 놓친 킬 카운트를 보정한다.
        std::vector<ObjectGuid>* guardGuids = nullptr;

		if (m_ControllingFaction == HORDE)
			guardGuids = &CustomHalaaHordeGuardNpcGuids;
		else if (m_ControllingFaction == ALLIANCE)
			guardGuids = &CustomHalaaAllianceGuardNpcGuids;

		if (guardGuids && !guardGuids->empty())
		{
			uint32 actualGuardsAlive = GetAliveGuardsCount();

			if (actualGuardsAlive < m_GuardsAlive)
			{
				m_GuardsAlive = actualGuardsAlive;

				if (m_GuardsAlive == 0)
					m_capturable = true;

				m_PvP->SendUpdateWorldState(NA_UI_GUARDS_LEFT, m_GuardsAlive);
				SaveCustomState();

				OutdoorPvpWar_OnNagrandStateChanged(m_ControllingFaction, m_GuardsAlive);
			}
			else
				m_PvP->SendUpdateWorldState(NA_UI_GUARDS_LEFT, m_GuardsAlive);
		}
		else
			m_PvP->SendUpdateWorldState(NA_UI_GUARDS_LEFT, m_GuardsAlive);
    }
    else
        m_GuardCheckTimer -= diff;

	OutdoorPvpWar_OnNagrandStateChanged(m_ControllingFaction, m_GuardsAlive);

    // 먼저 점령 지역 안의 activePlayers를 최신화한다.
    // diff 0이라 게이지 수치는 이동하지 않는다.
    OPvPCapturePoint::Update(0);

    uint32 alliancePlayers = m_activePlayers[0].size();
    uint32 hordePlayers = m_activePlayers[1].size();

    // 양쪽 인원이 같거나 아무도 없으면 게이지를 움직이지 않는다.
    if (alliancePlayers == hordePlayers)
        return false;

    // 현재 게이지를 실제로 미는 진영을 계산한다.
    uint32 pushingTeam = alliancePlayers > hordePlayers ? ALLIANCE : HORDE;

    // 경비병이 0명이면 적 진영도 게이지를 밀 수 있다.
    bool guardWallDown = m_GuardsAlive == 0;

    // 경비병이 살아 있어도 현재 점령 진영이 자기 방향으로 굳히는 것은 허용한다.
    bool ownerPushing = m_ControllingFaction == pushingTeam;

    // 경비병이 살아 있고, 현재 점령 진영이 아닌 쪽이 밀고 있으면 차단한다.
    // 즉, 적은 경비병 0명일 때만 밀 수 있고,
    // 점령한 진영은 경비병이 있어도 자기 게이지를 계속 굳힐 수 있다.
    if (!guardWallDown && !ownerPushing)
    {
        m_capturable = false;
        return false;
    }

    // 경비병이 0명인 상태에서 적 진영이 밀고 있으면 제한 시간 타이머를 진행한다.
    bool enemyPushingWithNoGuards = guardWallDown && m_ControllingFaction != pushingTeam;

    if (enemyPushingWithNoGuards)
    {
        m_capturable = true;

        if (m_RespawnTimer < diff)
        {
            if (m_ControllingFaction)
                FactionTakeOver(m_ControllingFaction, true);

            m_RespawnTimer = NA_RESPAWN_TIME;
        }
        else
            m_RespawnTimer -= diff;
    }

    // 점령 진영이 자기 방향으로 굳히는 구간은 경비병 수와 상관없이 게이지를 허용한다.
    if (ownerPushing)
        m_capturable = true;

    uint32 captureDiff = diff * NA_CAPTURE_SPEED_MULTIPLIER;

	if (m_ControllingFaction == 0 && m_State == OBJECTIVESTATE_NEUTRAL)
		captureDiff = diff * NA_NEUTRAL_START_CAPTURE_SPEED_MULTIPLIER;

	bool result = OPvPCapturePoint::Update(captureDiff);

    // 경비병이 살아 있으면 다시 적 진영 점령은 잠근다.
    // 단, 다음 틱에서 ownerPushing이면 다시 임시로 열어준다.
    if (m_GuardsAlive > 0)
        m_capturable = false;
    else
        m_capturable = true;

    if (m_CustomStateSaveTimer <= diff)
    {
        m_CustomStateSaveTimer = NA_CUSTOM_STATE_SAVE_TIME;
        SaveCustomState();
    }
    else
        m_CustomStateSaveTimer -= diff;

    return result;
}

void OPvPCapturePointNA::ChangeState()
{
    uint32 artkit = 21;
    switch (m_State)
    {
        case OBJECTIVESTATE_NEUTRAL:
            m_HalaaState = HALAA_N;
            break;
        case OBJECTIVESTATE_ALLIANCE:
            m_HalaaState = HALAA_A;
            FactionTakeOver(ALLIANCE, true);
            artkit = 2;
            break;
        case OBJECTIVESTATE_HORDE:
            m_HalaaState = HALAA_H;
            FactionTakeOver(HORDE, true);
            artkit = 1;
            break;
        case OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE:
            m_HalaaState = HALAA_N_A;
            break;
        case OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE:
            m_HalaaState = HALAA_N_H;
            break;
        case OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE:
            m_HalaaState = HALAA_N_A;
            artkit = 2;
            break;
        case OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE:
            m_HalaaState = HALAA_N_H;
            artkit = 1;
        break;
    }

    ApplyCustomCapturePointArtKit();

    UpdateHalaaWorldState();

    if (m_State != OBJECTIVESTATE_ALLIANCE && m_State != OBJECTIVESTATE_HORDE)
        SaveCustomState();
}

void OPvPCapturePointNA::UpdateHalaaWorldState()
{
    m_PvP->SendUpdateWorldState(NA_MAP_HALAA_NEUTRAL, uint32((m_HalaaState & HALAA_N) != 0));
    m_PvP->SendUpdateWorldState(NA_MAP_HALAA_NEU_A, uint32((m_HalaaState & HALAA_N_A) != 0));
    m_PvP->SendUpdateWorldState(NA_MAP_HALAA_NEU_H, uint32((m_HalaaState & HALAA_N_H) != 0));
    m_PvP->SendUpdateWorldState(NA_MAP_HALAA_HORDE, uint32((m_HalaaState & HALAA_H) != 0));
    m_PvP->SendUpdateWorldState(NA_MAP_HALAA_ALLIANCE, uint32((m_HalaaState & HALAA_A) != 0));
}

void OPvPCapturePointNA::UpdateWyvernRoostWorldState(uint32 roost)
{
    switch (roost)
    {
        case NA_ROOST_S:
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_SOUTH_NEU_H, uint32((m_WyvernStateSouth & WYVERN_NEU_HORDE) != 0));
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_SOUTH_NEU_A, uint32((m_WyvernStateSouth & WYVERN_NEU_ALLIANCE) != 0));
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_SOUTH_H, uint32((m_WyvernStateSouth & WYVERN_HORDE) != 0));
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_SOUTH_A, uint32((m_WyvernStateSouth & WYVERN_ALLIANCE) != 0));
            break;
        case NA_ROOST_N:
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_NORTH_NEU_H, uint32((m_WyvernStateNorth & WYVERN_NEU_HORDE) != 0));
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_NORTH_NEU_A, uint32((m_WyvernStateNorth & WYVERN_NEU_ALLIANCE) != 0));
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_NORTH_H, uint32((m_WyvernStateNorth & WYVERN_HORDE) != 0));
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_NORTH_A, uint32((m_WyvernStateNorth & WYVERN_ALLIANCE) != 0));
            break;
        case NA_ROOST_W:
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_WEST_NEU_H, uint32((m_WyvernStateWest & WYVERN_NEU_HORDE) != 0));
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_WEST_NEU_A, uint32((m_WyvernStateWest & WYVERN_NEU_ALLIANCE) != 0));
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_WEST_H, uint32((m_WyvernStateWest & WYVERN_HORDE) != 0));
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_WEST_A, uint32((m_WyvernStateWest & WYVERN_ALLIANCE) != 0));
            break;
        case NA_ROOST_E:
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_EAST_NEU_H, uint32((m_WyvernStateEast & WYVERN_NEU_HORDE) != 0));
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_EAST_NEU_A, uint32((m_WyvernStateEast & WYVERN_NEU_ALLIANCE) != 0));
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_EAST_H, uint32((m_WyvernStateEast & WYVERN_HORDE) != 0));
            m_PvP->SendUpdateWorldState(NA_MAP_WYVERN_EAST_A, uint32((m_WyvernStateEast & WYVERN_ALLIANCE) != 0));
            break;
    }
}


static void CustomHalaaWyvernTaxiConsole(std::string const& message)
{
    std::cout << "[HalaaWyvernTaxi] " << message << std::endl;
}

class go_halaa_wyvern_taxi : public GameObjectScript
{
public:
    go_halaa_wyvern_taxi() : GameObjectScript("go_halaa_wyvern_taxi") { }

    struct go_halaa_wyvern_taxiAI : public GameObjectAI
    {
        go_halaa_wyvern_taxiAI(GameObject* go) : GameObjectAI(go) { }

        bool OnGossipHello(Player* player) override
        {
            std::ostringstream ss;
            ss << "OnGossipHello: player=" << (player ? player->GetName() : "null")
               << " entry=" << me->GetEntry()
               << " guid=" << me->GetGUID().GetCounter()
               << " type=" << uint32(me->GetGoType());
            CustomHalaaWyvernTaxiConsole(ss.str());

            return false;
        }

        bool OnReportUse(Player* player) override
        {
            std::ostringstream ss;
            ss << "OnReportUse: player=" << (player ? player->GetName() : "null")
               << " entry=" << me->GetEntry()
               << " guid=" << me->GetGUID().GetCounter()
               << " type=" << uint32(me->GetGoType());
            CustomHalaaWyvernTaxiConsole(ss.str());

            if (!player)
                return false;

            uint32 goType = 0;

            if (!CustomHalaaGetControlGoTypeByEntryAny(me->GetEntry(), goType))
                return false;

            switch (goType)
            {
                case NA_ROOST_S:
                    CustomHalaaWyvernTaxiConsole("restored SOUTH roost clicked: start taxi");
                    return CustomHalaaStartWyvernFlight(player, NA_ROOST_S, NA_SPELL_FLY_SOUTH);
                case NA_ROOST_N:
                    CustomHalaaWyvernTaxiConsole("restored NORTH roost clicked: start taxi");
                    return CustomHalaaStartWyvernFlight(player, NA_ROOST_N, NA_SPELL_FLY_NORTH);
                case NA_ROOST_W:
                    CustomHalaaWyvernTaxiConsole("restored WEST roost clicked: start taxi");
                    return CustomHalaaStartWyvernFlight(player, NA_ROOST_W, NA_SPELL_FLY_WEST);
                case NA_ROOST_E:
                    CustomHalaaWyvernTaxiConsole("restored EAST roost clicked: start taxi");
                    return CustomHalaaStartWyvernFlight(player, NA_ROOST_E, NA_SPELL_FLY_EAST);
                default:
                    break;
            }

            return false;
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_halaa_wyvern_taxiAI(go);
    }
};

class OutdoorPvP_nagrand : public OutdoorPvPScript
{
    public:
        OutdoorPvP_nagrand() : OutdoorPvPScript("outdoorpvp_na") { }

        OutdoorPvP* GetOutdoorPvP() const override
        {
            return new OutdoorPvPNA();
        }
};

class player_nagrand_server_buff : public PlayerScript
{
public:
    player_nagrand_server_buff() : PlayerScript("player_nagrand_server_buff") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        ApplyNagrandServerBuff(player);
    }

    void OnMapChanged(Player* player) override
    {
        ApplyNagrandServerBuff(player);
    }
};

void AddSC_outdoorpvp_na()
{
    new OutdoorPvP_nagrand();
    new go_halaa_wyvern_taxi();
	new player_nagrand_server_buff();
}