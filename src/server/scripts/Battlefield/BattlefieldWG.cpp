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

/// @todo Implement proper support for vehicle+player teleportation
/// @todo Use spell victory/defeat in wg instead of RewardMarkOfHonor() && RewardHonor
/// @todo Add proper implement of achievement

#include "BattlefieldWG.h"
#include "AchievementMgr.h"
#include "BattlefieldMgr.h"
#include "Battleground.h"
#include "CreatureTextMgr.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Log.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "Random.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "TemporarySummon.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldStatePackets.h"

#include <unordered_map>
#include "../Custom/DiscordEvent.h"
#include "Config.h"
#include <cmath>
#include "CellImpl.h"
#include "GridNotifiers.h"
#include <cmath>
#include <list>

#include "Spell.h"
#include "SpellMgr.h"
#include "GridNotifiersImpl.h"

struct BfWGCoordGY
{
    Position Pos;
    uint32 GraveyardID;
    uint32 TextID;          // for gossip menu
    TeamId StartControl;
};

// 7 in sql, 7 in header
BfWGCoordGY const WGGraveyard[BATTLEFIELD_WG_GRAVEYARD_MAX] =
{
    { { 5104.750f, 2300.940f, 368.579f, 0.733038f }, 1329, BATTLEFIELD_WG_GOSSIPTEXT_GY_NE,       TEAM_NEUTRAL  },
    { { 5099.120f, 3466.036f, 368.484f, 5.317802f }, 1330, BATTLEFIELD_WG_GOSSIPTEXT_GY_NW,       TEAM_NEUTRAL  },
    { { 4314.648f, 2408.522f, 392.642f, 6.268125f }, 1333, BATTLEFIELD_WG_GOSSIPTEXT_GY_SE,       TEAM_NEUTRAL  },
    { { 4331.716f, 3235.695f, 390.251f, 0.008500f }, 1334, BATTLEFIELD_WG_GOSSIPTEXT_GY_SW,       TEAM_NEUTRAL  },
    { { 5537.986f, 2897.493f, 517.057f, 4.819249f }, 1285, BATTLEFIELD_WG_GOSSIPTEXT_GY_KEEP,     TEAM_NEUTRAL  },
    { { 5032.454f, 3711.382f, 372.468f, 3.971623f }, 1331, BATTLEFIELD_WG_GOSSIPTEXT_GY_HORDE,    TEAM_HORDE    },
    { { 5140.790f, 2179.120f, 390.950f, 1.972220f }, 1332, BATTLEFIELD_WG_GOSSIPTEXT_GY_ALLIANCE, TEAM_ALLIANCE },
};

uint32 const ClockWorldState[]         = { 3781, 4354 };
uint32 const WintergraspFaction[]      = { FACTION_ALLIANCE_GENERIC_WG, FACTION_HORDE_GENERIC_WG, FACTION_FRIENDLY };

static float const WG_SPIRIT_IMMUNITY_GRAVEYARD_RADIUS = 70.0f;
static uint32 WGSpiritImmunityCheckTimer = 0;

static bool IsPlayerInWintergraspGraveyardArea(BattlefieldWG* wg, Player* player)
{
    if (!wg || !player)
        return false;

    if (player->GetMapId() != BATTLEFIELD_WG_MAPID)
        return false;

    TeamId teamId = player->GetTeamId();

    if (teamId != TEAM_ALLIANCE && teamId != TEAM_HORDE)
        return false;

    for (uint8 i = 0; i < BATTLEFIELD_WG_GRAVEYARD_MAX; ++i)
    {
        BfGraveyard* graveyard = wg->GetGraveyardById(i);
        if (!graveyard)
            continue;

        if (graveyard->GetControlTeamId() != teamId)
            continue;

        if (player->GetDistance2d(WGGraveyard[i].Pos.GetPositionX(), WGGraveyard[i].Pos.GetPositionY()) <= WG_SPIRIT_IMMUNITY_GRAVEYARD_RADIUS)
            return true;
    }

    return false;
}

static std::string const kWintergraspDiscordUrl = "https://stimpack.pe.kr/";
static int const kWintergraspDiscordColor = 0x3B82F6;

static std::unordered_map<std::string, time_t> WGDiscordCooldowns;

static char const* GetWGTeamName(TeamId team)
{
    switch (team)
    {
        case TEAM_ALLIANCE:
            return "얼라이언스";
        case TEAM_HORDE:
            return "호드";
        default:
            break;
    }

    return "중립";
}

static std::string GetWGWorkshopName(uint8 workshopId)
{
    switch (workshopId)
    {
        case BATTLEFIELD_WG_WORKSHOP_NE:
            return "가라앉은 경기장";
        case BATTLEFIELD_WG_WORKSHOP_NW:
            return "파괴된 신전";
        case BATTLEFIELD_WG_WORKSHOP_SE:
            return "동부불꽃 작업장";
        case BATTLEFIELD_WG_WORKSHOP_SW:
            return "서부불꽃 작업장";
        case BATTLEFIELD_WG_WORKSHOP_KEEP_WEST:
            return "요새 서쪽 작업장";
        case BATTLEFIELD_WG_WORKSHOP_KEEP_EAST:
            return "요새 동쪽 작업장";
        default:
            break;
    }

    return "알 수 없는 작업장";
}

static std::string GetWGBuildingName(uint32 entry)
{
    switch (entry)
    {
        case GO_WINTERGRASP_FORTRESS_TOWER_1:
            return "요새 북서쪽 탑";
        case GO_WINTERGRASP_FORTRESS_TOWER_2:
            return "요새 남서쪽 탑";
        case GO_WINTERGRASP_FORTRESS_TOWER_3:
            return "요새 남동쪽 탑";
        case GO_WINTERGRASP_FORTRESS_TOWER_4:
            return "요새 북동쪽 탑";
        case GO_WINTERGRASP_SHADOWSIGHT_TOWER:
            return "그늘눈 경비탑";
        case GO_WINTERGRASP_WINTER_S_EDGE_TOWER:
            return "겨울 칼날 경비탑";
        case GO_WINTERGRASP_FLAMEWATCH_TOWER:
            return "불꽃감시 경비탑";
        case GO_WINTERGRASP_FORTRESS_GATE:
            return "겨울손아귀 요새 관문";
        case GO_WINTERGRASP_VAULT_GATE:
            return "겨울손아귀 최후의 관문";
        default:
            break;
    }

    return "겨울손아귀 방어 시설";
}

static bool CanSendWGDiscordEvent(std::string const& key, uint32 cooldownSec)
{
    time_t now = GameTime::GetGameTime();

    auto itr = WGDiscordCooldowns.find(key);
    if (itr != WGDiscordCooldowns.end())
    {
        if (uint32(now - itr->second) < cooldownSec)
            return false;
    }

    WGDiscordCooldowns[key] = now;
    return true;
}

static void SendWGDiscordEvent(std::string const& key, std::string const& title, std::string const& body, uint32 cooldownSec = 30)
{
    if (!CanSendWGDiscordEvent(key, cooldownSec))
        return;

    SendServerDiscordEvent(
        title,
        body,
        kWintergraspDiscordUrl,
        kWintergraspDiscordColor,
        "wintergrasp"
    );
}

Position const WintergraspStalkerPos   = { 4948.985f, 2937.789f, 550.5172f,  1.815142f };

Position const WintergraspRelicPos     = { 5440.379f, 2840.493f, 430.2816f, -1.832595f };
QuaternionData const WintergraspRelicRot    = { 0.f, 0.f, -0.7933531f, 0.6087617f };

uint8 const WG_MAX_OBJ              = 32;
uint8 const WG_MAX_TURRET           = 15;
uint8 const WG_MAX_TELEPORTER       = 12;
uint8 const WG_MAX_WORKSHOP         = 6;
uint8 const WG_MAX_TOWER            = 7;

// *****************************************************
// ************ Destructible (Wall, Tower..) ***********
// *****************************************************

struct WintergraspBuildingSpawnData
{
    uint32 entry;
    uint32 WorldState;
    Position pos;
    QuaternionData rot;
    WintergraspGameObjectBuildingType type;
};

WintergraspBuildingSpawnData const WGGameObjectBuilding[WG_MAX_OBJ] =
{
    // Wall (Not spawned in db)
    // Entry  WS      X          Y          Z           O                rX   rY   rZ             rW             Type
    { 190219, 3749, { 5371.457f, 3047.472f, 407.5710f,  3.14159300f }, { 0.f, 0.f, -1.000000000f, 0.00000000f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 190220, 3750, { 5331.264f, 3047.105f, 407.9228f,  0.05235888f }, { 0.f, 0.f,  0.026176450f, 0.99965730f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191795, 3764, { 5385.841f, 2909.490f, 409.7127f,  0.00872424f }, { 0.f, 0.f,  0.004362106f, 0.99999050f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191796, 3772, { 5384.452f, 2771.835f, 410.2704f,  3.14159300f }, { 0.f, 0.f, -1.000000000f, 0.00000000f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191799, 3762, { 5371.436f, 2630.610f, 408.8163f,  3.13285800f }, { 0.f, 0.f,  0.999990500f, 0.00436732f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191800, 3766, { 5301.838f, 2909.089f, 409.8661f,  0.00872424f }, { 0.f, 0.f,  0.004362106f, 0.99999050f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191801, 3770, { 5301.063f, 2771.411f, 409.9014f,  3.14159300f }, { 0.f, 0.f, -1.000000000f, 0.00000000f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191802, 3751, { 5280.197f, 2995.583f, 408.8249f,  1.61442800f }, { 0.f, 0.f,  0.722363500f, 0.69151360f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191803, 3752, { 5279.136f, 2956.023f, 408.6041f,  1.57079600f }, { 0.f, 0.f,  0.707106600f, 0.70710690f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191804, 3767, { 5278.685f, 2882.513f, 409.5388f,  1.57079600f }, { 0.f, 0.f,  0.707106600f, 0.70710690f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191806, 3769, { 5279.502f, 2798.945f, 409.9983f,  1.57079600f }, { 0.f, 0.f,  0.707106600f, 0.70710690f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191807, 3759, { 5279.937f, 2724.766f, 409.9452f,  1.56207000f }, { 0.f, 0.f,  0.704014800f, 0.71018530f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191808, 3760, { 5279.601f, 2683.786f, 409.8488f,  1.55334100f }, { 0.f, 0.f,  0.700908700f, 0.71325110f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191809, 3761, { 5330.955f, 2630.777f, 409.2826f,  3.13285800f }, { 0.f, 0.f,  0.999990500f, 0.00436732f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 190369, 3753, { 5256.085f, 2933.963f, 409.3571f,  3.13285800f }, { 0.f, 0.f,  0.999990500f, 0.00436732f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 190370, 3758, { 5257.463f, 2747.327f, 409.7427f, -3.13285800f }, { 0.f, 0.f, -0.999990500f, 0.00436732f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 190371, 3754, { 5214.960f, 2934.089f, 409.1905f, -0.00872424f }, { 0.f, 0.f, -0.004362106f, 0.99999050f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 190372, 3757, { 5215.821f, 2747.566f, 409.1884f, -3.13285800f }, { 0.f, 0.f, -0.999990500f, 0.00436732f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 190374, 3755, { 5162.273f, 2883.043f, 410.2556f,  1.57952200f }, { 0.f, 0.f,  0.710185100f, 0.70401500f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 190376, 3756, { 5163.724f, 2799.838f, 409.2270f,  1.57952200f }, { 0.f, 0.f,  0.710185100f, 0.70401500f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },

    // Tower of keep (Not spawned in db)
    { 190221, 3711, { 5281.154f, 3044.588f, 407.8434f,  3.115388f }, { 0.f, 0.f,  0.9999142f, 0.013101960f }, BATTLEFIELD_WG_OBJECTTYPE_KEEP_TOWER }, // NW
    { 190373, 3713, { 5163.757f, 2932.228f, 409.1904f,  3.124123f }, { 0.f, 0.f,  0.9999619f, 0.008734641f }, BATTLEFIELD_WG_OBJECTTYPE_KEEP_TOWER }, // SW
    { 190377, 3714, { 5166.397f, 2748.368f, 409.1884f, -1.570796f }, { 0.f, 0.f, -0.7071066f, 0.707106900f }, BATTLEFIELD_WG_OBJECTTYPE_KEEP_TOWER }, // SE
    { 190378, 3712, { 5281.192f, 2632.479f, 409.0985f, -1.588246f }, { 0.f, 0.f, -0.7132492f, 0.700910500f }, BATTLEFIELD_WG_OBJECTTYPE_KEEP_TOWER }, // NE

    // Wall (with passage) (Not spawned in db)
    { 191797, 3765, { 5343.290f, 2908.860f, 409.5757f, 0.00872424f }, { 0.f, 0.f,  0.004362106f, 0.9999905f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191798, 3771, { 5342.719f, 2771.386f, 409.6249f, 3.14159300f }, { 0.f, 0.f, -1.000000000f, 0.0000000f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },
    { 191805, 3768, { 5279.126f, 2840.797f, 409.7826f, 1.57952200f }, { 0.f, 0.f,  0.710185100f, 0.7040150f }, BATTLEFIELD_WG_OBJECTTYPE_WALL },

    // South tower (Not spawned in db)
    { 190356, 3704, { 4557.173f, 3623.943f, 395.8828f,  1.675516f }, { 0.f, 0.f,  0.7431450f, 0.669130400f }, BATTLEFIELD_WG_OBJECTTYPE_TOWER }, // W
    { 190357, 3705, { 4398.172f, 2822.497f, 405.6270f, -3.124123f }, { 0.f, 0.f, -0.9999619f, 0.008734641f }, BATTLEFIELD_WG_OBJECTTYPE_TOWER }, // S
    { 190358, 3706, { 4459.105f, 1944.326f, 434.9912f, -2.002762f }, { 0.f, 0.f, -0.8422165f, 0.539139500f }, BATTLEFIELD_WG_OBJECTTYPE_TOWER }, // E

    // Door of forteress (Not spawned in db)
    { GO_WINTERGRASP_FORTRESS_GATE, 3763, { 5162.991f, 2841.232f, 410.1892f, -3.132858f }, { 0.f, 0.f, -0.9999905f, 0.00436732f }, BATTLEFIELD_WG_OBJECTTYPE_DOOR },

    // Last door (Not spawned in db)
    { GO_WINTERGRASP_VAULT_GATE, 3773, { 5397.108f, 2841.54f, 425.9014f, 3.141593f }, { 0.f, 0.f, -1.f, 0.f }, BATTLEFIELD_WG_OBJECTTYPE_DOOR_LAST },
};

struct StaticWintergraspTowerInfo
{
    uint8 TowerId;

    struct
    {
        uint8 Damaged;
        uint8 Destroyed;
    } TextIds;
};

StaticWintergraspTowerInfo const TowerData[WG_MAX_TOWER] =
{
    { BATTLEFIELD_WG_TOWER_FORTRESS_NW,   { BATTLEFIELD_WG_TEXT_NW_KEEPTOWER_DAMAGE,   BATTLEFIELD_WG_TEXT_NW_KEEPTOWER_DESTROY   } },
    { BATTLEFIELD_WG_TOWER_FORTRESS_SW,   { BATTLEFIELD_WG_TEXT_SW_KEEPTOWER_DAMAGE,   BATTLEFIELD_WG_TEXT_SW_KEEPTOWER_DESTROY   } },
    { BATTLEFIELD_WG_TOWER_FORTRESS_SE,   { BATTLEFIELD_WG_TEXT_SE_KEEPTOWER_DAMAGE,   BATTLEFIELD_WG_TEXT_SE_KEEPTOWER_DESTROY   } },
    { BATTLEFIELD_WG_TOWER_FORTRESS_NE,   { BATTLEFIELD_WG_TEXT_NE_KEEPTOWER_DAMAGE,   BATTLEFIELD_WG_TEXT_NE_KEEPTOWER_DESTROY   } },
    { BATTLEFIELD_WG_TOWER_SHADOWSIGHT,   { BATTLEFIELD_WG_TEXT_WESTERN_TOWER_DAMAGE,  BATTLEFIELD_WG_TEXT_WESTERN_TOWER_DESTROY  } },
    { BATTLEFIELD_WG_TOWER_WINTER_S_EDGE, { BATTLEFIELD_WG_TEXT_SOUTHERN_TOWER_DAMAGE, BATTLEFIELD_WG_TEXT_SOUTHERN_TOWER_DESTROY } },
    { BATTLEFIELD_WG_TOWER_FLAMEWATCH,    { BATTLEFIELD_WG_TEXT_EASTERN_TOWER_DAMAGE,  BATTLEFIELD_WG_TEXT_EASTERN_TOWER_DESTROY  } }
};

Position const WGTurret[WG_MAX_TURRET] =
{
    { 5391.19f, 3060.8f,  419.616f, 1.69557f },
    { 5266.75f, 2976.5f,  421.067f, 3.20354f },
    { 5234.86f, 2948.8f,  420.88f,  1.61311f },
    { 5323.05f, 2923.7f,  421.645f, 1.5817f },
    { 5363.82f, 2923.87f, 421.709f, 1.60527f },
    { 5264.04f, 2861.34f, 421.587f, 3.21142f },
    { 5264.68f, 2819.78f, 421.656f, 3.15645f },
    { 5322.16f, 2756.69f, 421.646f, 4.69978f },
    { 5363.78f, 2756.77f, 421.629f, 4.78226f },
    { 5236.2f,  2732.68f, 421.649f, 4.72336f },
    { 5265.02f, 2704.63f, 421.7f,   3.12507f },
    { 5350.87f, 2616.03f, 421.243f, 4.72729f },
    { 5390.95f, 2615.5f,  421.126f, 4.6409f },
    { 5148.8f,  2820.24f, 421.621f, 3.16043f },
    { 5147.98f, 2861.93f, 421.63f,  3.18792f },
};

struct WintergraspObjectPositionData
{
    Position Pos;
    uint32 HordeEntry;
    uint32 AllianceEntry;
};

struct WintergraspGameObjectData
{
    Position Pos;
    QuaternionData Rot;
    uint32 HordeEntry;
    uint32 AllianceEntry;
};

enum WintergraspPortalEntry
{
    GO_WG_RETURN_PORTAL_INSIDE_FORTRESS = 192819,
    GO_WG_VEHICLE_TELEPORTER            = 192951
};

WintergraspGameObjectData const WGPortalDefenderData[WG_MAX_TELEPORTER] =
{
    // Player teleporter
    { { 5153.408f, 2901.349f, 409.1913f, -0.06981169f }, { 0.f, 0.f, -0.03489876f, 0.9993908f }, 190763, 191575 },
    { { 5268.698f, 2666.421f, 409.0985f, -0.71558490f }, { 0.f, 0.f, -0.35020730f, 0.9366722f }, 190763, 191575 },
    { { 5197.050f, 2944.814f, 409.1913f,  2.33874000f }, { 0.f, 0.f,  0.92050460f, 0.3907318f }, 190763, 191575 },
    { { 5196.671f, 2737.345f, 409.1892f, -2.93213900f }, { 0.f, 0.f, -0.99452110f, 0.1045355f }, 190763, 191575 },
    { { 5314.580f, 3055.852f, 408.8620f,  0.54105060f }, { 0.f, 0.f,  0.26723770f, 0.9636307f }, 190763, 191575 },
    { { 5391.277f, 2828.094f, 418.6752f, -2.16420600f }, { 0.f, 0.f, -0.88294700f, 0.4694727f }, 190763, 191575 },
    { { 5153.931f, 2781.671f, 409.2455f,  1.65806200f }, { 0.f, 0.f,  0.73727700f, 0.6755905f }, 190763, 191575 },
    { { 5311.445f, 2618.931f, 409.0916f, -2.37364400f }, { 0.f, 0.f, -0.92718320f, 0.3746083f }, 190763, 191575 },
    { { 5269.208f, 3013.838f, 408.8276f, -1.76278200f }, { 0.f, 0.f, -0.77162460f, 0.6360782f }, 190763, 191575 },

    { { 5401.634f, 2853.667f, 418.6748f,  2.63544400f }, { 0.f, 0.f,  0.96814730f, 0.2503814f }, 192819, 192819 }, // return portal inside fortress, neutral
    // Vehicle teleporter
    { { 5314.515f, 2703.687f, 408.5502f, -0.89011660f }, { 0.f, 0.f, -0.43051050f, 0.9025856f }, 192951, 192951 },
    { { 5316.252f, 2977.042f, 408.5385f, -0.82030330f }, { 0.f, 0.f, -0.39874840f, 0.9170604f }, 192951, 192951 }
};

// *********************************************************
// **********Tower Element(GameObject, Creature)************
// *********************************************************

struct WintergraspTowerData
{
    uint32 towerEntry;                  // Gameobject id of tower
    std::vector<WintergraspGameObjectData> GameObject;   // Gameobject position and entry (Horde/Alliance)

    // Creature: Turrets and Guard /// @todo: Killed on Tower destruction ? Tower damage ? Requires confirming
    std::vector<WintergraspObjectPositionData> CreatureBottom;
};

uint8 const WG_MAX_ATTACKTOWERS = 3;
// 192414 : 0 in sql, 1 in header
// 192278 : 0 in sql, 3 in header
WintergraspTowerData const AttackTowers[WG_MAX_ATTACKTOWERS] =
{
    // West tower
    {
        190356,
        {
            { { 4559.113f, 3606.216f, 419.9992f, 4.799657f }, { 0.f, 0.f, -0.67558960f, 0.73727790f }, 192488, 192501 },    // Flag on tower
            { { 4539.420f, 3622.490f, 420.0342f, 3.211419f }, { 0.f, 0.f, -0.99939060f, 0.03490613f }, 192488, 192501 },    // Flag on tower
            { { 4555.258f, 3641.648f, 419.9740f, 1.675514f }, { 0.f, 0.f,  0.74314400f, 0.66913150f }, 192488, 192501 },    // Flag on tower
            { { 4574.872f, 3625.911f, 420.0792f, 0.087266f }, { 0.f, 0.f,  0.04361916f, 0.99904820f }, 192488, 192501 },    // Flag on tower
            { { 4433.899f, 3534.142f, 360.2750f, 4.433136f }, { 0.f, 0.f, -0.79863550f, 0.60181500f }, 192269, 192278 },    // Flag near workshop
            { { 4572.933f, 3475.519f, 363.0090f, 1.422443f }, { 0.f, 0.f,  0.65275960f, 0.75756520f }, 192269, 192277 }     // Flag near bridge
        },
        {
            { { 4418.688477f, 3506.251709f, 358.975494f,  4.293305f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },      // Roaming Guard
			{ { 4579.499023f, 3537.372559f, 381.610596f, 4.687243f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4536.357422f, 3538.457520f, 379.762787f, 4.687243f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4459.090332f, 3616.248535f, 380.151917f, 4.075418f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4489.091309f, 3594.052734f, 386.496887f, 3.864931f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A }
        }
    },
    // South Tower
    {
        190357,
        {
            { { 4416.004f, 2822.666f, 429.8512f, 6.2657330f }, { 0.f, 0.f, -0.00872612f, 0.99996190f }, 192488, 192501 },    // Flag on tower
            { { 4398.819f, 2804.698f, 429.7920f, 4.6949370f }, { 0.f, 0.f, -0.71325020f, 0.70090960f }, 192488, 192501 },    // Flag on tower
            { { 4387.622f, 2719.566f, 389.9351f, 4.7385700f }, { 0.f, 0.f, -0.69779010f, 0.71630230f }, 192366, 192414 },    // Flag near tower
            { { 4464.124f, 2855.453f, 406.1106f, 0.8290324f }, { 0.f, 0.f,  0.40274720f, 0.91531130f }, 192366, 192429 },    // Flag near tower
            { { 4526.457f, 2810.181f, 391.1997f, 3.2899610f }, { 0.f, 0.f, -0.99724960f, 0.07411628f }, 192269, 192278 }     // Flag near bridge
        },
        {
			{ { 4703.195801f, 2856.375732f, 387.634003f, 3.308872f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4699.613281f, 2877.590820f, 387.569183f, 3.308872f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4511.977539f, 2842.484863f, 390.298615f, 0.128796f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4523.923340f, 2813.272705f, 391.308014f, 0.182203f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4393.114746f, 2892.599121f, 394.335327f, 1.401919f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4413.712402f, 2889.087158f, 395.112366f, 1.401919f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4456.977539f, 2809.390869f, 401.098022f, 0.171988f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4452.523438f, 2835.033203f, 401.545441f, 6.141014f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4422.523438f, 2773.903564f, 400.840363f, 5.500914f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4410.603027f, 2755.304688f, 401.322021f, 5.874763f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A }
        }
    },
    // East Tower
    {
        190358,
        {
            { { 4466.793f, 1960.418f, 459.1437f, 1.151916f }, { 0.f, 0.f,  0.5446386f, 0.8386708f }, 192488, 192501 },     // Flag on tower
            { { 4475.351f, 1937.031f, 459.0702f, 5.846854f }, { 0.f, 0.f, -0.2164392f, 0.9762961f }, 192488, 192501 },     // Flag on tower
            { { 4451.758f, 1928.104f, 459.0759f, 4.276057f }, { 0.f, 0.f, -0.8433914f, 0.5372996f }, 192488, 192501 },     // Flag on tower
            { { 4442.987f, 1951.898f, 459.0930f, 2.740162f }, { 0.f, 0.f,  0.9799242f, 0.1993704f }, 192488, 192501 }      // Flag on tower
        },
        {
            { { 4501.060059f, 1990.280029f, 431.157013f, 1.029740f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },     // Standing Guard
            { { 4463.830078f, 2015.180054f, 430.299988f, 1.431170f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },     // Standing Guard
            { { 4494.580078f, 1943.760010f, 435.627014f, 6.195920f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },     // Standing Guard
            { { 4450.149902f, 1897.579956f, 435.045013f, 4.398230f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },     // Standing Guard
            { { 4428.870117f, 1906.869995f, 432.648010f, 3.996800f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A }      // Standing Guard
        }
    }
};

struct WintergraspTowerCannonData
{
    uint32 towerEntry;
    std::vector<Position> TowerCannonBottom;
    std::vector<Position> TurretTop;
};

uint8 const WG_MAX_TOWER_CANNON = 7;

WintergraspTowerCannonData const TowerCannon[WG_MAX_TOWER_CANNON] =
{
    {
        190221,  //요새 NW 타워  5255.88 3047.63 438.499571 3.13677 
        {
            // no cannons at bottom
        },
        {
            { 5255.88f, 3047.63f, 438.499f, 3.13677f },
            { 5280.90f, 3071.32f, 438.499f, 1.62879f }
        }
    },
    {
        190373,  //요새 SW 타워
        {
            // no cannons at bottom
        },
        {
            { 5138.59f, 2935.16f, 439.845f, 3.11723f },
            { 5163.06f, 2959.52f, 439.846f, 1.47258f }
        }
    },
    {
        190377,  //요새 SE 타워
        {
            // no cannons at bottom
        },
        {
            { 5163.84f, 2723.74f, 439.844f, 1.39940f },
            { 5139.69f, 2747.40f, 439.844f, 3.17221f }
        }
    },
    {
        190378,  //요새 NE 타워
        {
            // no cannons at bottom
        },
        {
            { 5278.21f, 2607.23f, 439.755f, 4.71944f },
            { 5255.01f, 2631.98f, 439.755f, 3.15257f }
        }
    },
    {
        190356,  // 그늘눈 경비탑
        {
            { 4582.424316f, 3603.667236f, 402.886932f, 5.597791f },
            { 4537.070801f, 3599.375732f, 402.886902f, 4.073336f },
            { 4532.275391f, 3644.490723f, 402.887390f, 2.445204f },
            { 4576.818848f, 3648.403320f, 402.887665f, 0.912897f }
        },
        {
            { 4581.741699f, 3626.714355f, 426.538910f, 0.143995f },
            { 4559.846191f, 3599.195312f, 426.539154f, 4.883096f },
            { 4532.698242f, 3621.486328f, 426.539124f, 3.254180f },
            { 4554.606934f, 3648.444824f, 426.539093f, 1.732863f }
        }
    },
    {
        190357,  // 겨울 칼날 경비탑
        {
            { 4420.094727f, 2845.245850f, 412.631866f, 0.824970f },
            { 4420.863281f, 2800.535400f, 412.630798f, 5.613548f },
            { 4376.420898f, 2800.006348f, 412.631073f, 4.004267f },
            { 4375.283203f, 2844.636963f, 412.630554f, 2.419333f }
        },
        {
            { 4397.928711f, 2847.002686f, 436.283569f, 1.724253f },
            { 4422.312012f, 2822.943604f, 436.283173f, 0.055296f },
            { 4398.740234f, 2798.107666f, 436.283142f, 4.720570f },
            { 4373.537109f, 2822.291504f, 436.284119f, 3.218888f }
        }
    },
    {
        190358,  // 불꽃감시 경비탑
        {
            { 4448.816895f, 1955.262573f, 441.994873f, 0.428359f },
            { 4470.083984f, 1914.121216f, 441.994904f, 5.095991f },
            { 4429.500977f, 1933.223267f, 441.995056f, 3.362617f },
            { 4448.127930f, 1974.443604f, 441.995148f, 1.994452f }
        },
        {
            { 4469.466309f, 1966.941406f, 465.647339f, 1.206699f },
            { 4481.547363f, 1934.015259f, 465.647308f, 5.763600f },
            { 4448.988770f, 1922.220459f, 465.647461f, 4.266635f },
            { 4436.888184f, 1954.833496f, 465.647522f, 2.749245f }
        }
    }
};

struct WGTrashSpawnInfo
{
    uint32 Entry;
    Position Pos;
};

WGTrashSpawnInfo const WGTrashSpawns[] =
{
    // 30846 - Glacial Spirit
    { 30846, { 4689.55f, 2588.1f, 335.327f, 2.0499f } },
    { 30846, { 4877.99f, 3215.59f, 349.572f, 4.59316f } },
    { 30846, { 4583.73f, 3069.43f, 335.79f, 2.00935f } },
    { 30846, { 4694.42f, 2660.07f, 336.139f, 0.255026f } },
    { 30846, { 5182.14f, 3261.53f, 352.746f, 0.590342f } },
    { 30846, { 4827.06f, 3218.72f, 352.66f, 5.46865f } },
    { 30846, { 5096.51f, 3637.21f, 352.623f, 0.282101f } },
    { 30846, { 4675.14f, 2707.97f, 338.059f, 1.84271f } },
    { 30846, { 4572.26f, 3003.85f, 334.634f, 5.14655f } },
    { 30846, { 4925.61f, 2077.11f, 414.309f, 5.9837f } },
    { 30846, { 5171.25f, 3580.06f, 352.622f, 4.79287f } },
    { 30846, { 5111.87f, 3512.72f, 354.518f, 4.49388f } },
    { 30846, { 4686.07f, 3358.93f, 336.39f, 3.03004f } },
    { 30846, { 4622.56f, 3177.89f, 343.194f, 1.79115f } },
    { 30846, { 5204.71f, 3337.12f, 352.883f, 2.28206f } },
    { 30846, { 5274.75f, 3370.94f, 352.893f, 3.17938f } },
    { 30846, { 4738.18f, 3378.69f, 335.586f, 2.04301f } },
    { 30846, { 4908.1f, 1996.07f, 420.287f, 0.153202f } },
    { 30846, { 4680.19f, 2515.12f, 338.027f, 0.6831f } },
    { 30846, { 4978.86f, 3576.16f, 352.683f, 3.47471f } },
    { 30846, { 4630.58f, 3005.83f, 335.74f, 1.16629f } },
    { 30846, { 5216.07f, 3425.03f, 352.86f, 2.27177f } },
    { 30846, { 4775.79f, 3473.27f, 352.465f, 5.36047f } },
    { 30846, { 5289.42f, 3314.61f, 353.495f, 3.59508f } },
    { 30846, { 4621.66f, 3046.16f, 337.014f, 1.08866f } },
    { 30846, { 4661.27f, 3323.91f, 334.64f, 1.44487f } },
    { 30846, { 4854.33f, 1971.22f, 417.521f, 5.1201f } },
    { 30846, { 4691.28f, 3238.81f, 340.525f, 2.24532f } },
    { 30846, { 4628.97f, 3104.99f, 340.128f, 4.05496f } },
    { 30846, { 4524.78f, 2963.2f, 347.633f, 0.586103f } },
    { 30846, { 5142.44f, 3348.53f, 353.525f, 0.612001f } },
    { 30846, { 5105.44f, 3410.52f, 353.705f, 1.43943f } },
    { 30846, { 4888.21f, 3497.29f, 348.279f, 3.44531f } },
    { 30846, { 4873.92f, 3538.88f, 337.848f, 5.16005f } },
    { 30846, { 4986.15f, 3698.07f, 346.102f, 2.30063f } },
    { 30846, { 4889.77f, 3171.95f, 352.573f, 5.52568f } },
    { 30846, { 4945.13f, 3211.14f, 345.899f, 1.78028f } },
    { 30846, { 4925.16f, 3637.69f, 336.189f, 5.30131f } },
    { 30846, { 4822.67f, 3180.47f, 352.591f, 3.38911f } },
    { 30846, { 4996.46f, 3220.24f, 345.899f, 1.05557f } },
    { 30846, { 4821.73f, 3501.55f, 333.29f, 4.15202f } },
    { 30846, { 4755.39f, 3552.49f, 349.398f, 1.55471f } },
    { 30846, { 4803.15f, 3134.7f, 350.018f, 1.82873f } },
    { 30846, { 4589.63f, 2329.17f, 340.5f, 1.96046f } },
    { 30846, { 4683.87f, 3557.53f, 347.166f, 3.53641f } },
    { 30846, { 4573.76f, 2376.27f, 334.806f, 4.24246f } },
    { 30846, { 4717.7f, 2342.11f, 338.149f, 3.0678f } },
    { 30846, { 4682.79f, 2311.82f, 337.047f, 0.868671f } },
    { 30846, { 4773.65f, 3184.28f, 346.345f, 2.55019f } },
    { 30846, { 4489.78f, 2410.96f, 352.259f, 5.05479f } },
    { 30846, { 4787.87f, 3227.57f, 352.543f, 0.538954f } },
    { 30846, { 4608.34f, 2428.72f, 336.872f, 3.13573f } },
    { 30846, { 4817.9f, 2141.85f, 336.087f, 2.45904f } },
    { 30846, { 4531.39f, 2451.69f, 350.649f, 5.59499f } },
    { 30846, { 4775.26f, 2194.86f, 334.122f, 0.492956f } },
    { 30846, { 4826.78f, 2215.43f, 333.711f, 1.86415f } },
    { 30846, { 4782.11f, 2248.57f, 349.133f, 5.45306f } },
    { 30846, { 4853.58f, 2053.08f, 394.042f, 3.04679f } },
    { 30846, { 4817.47f, 2014.95f, 401.938f, 5.16164f } },
    { 30846, { 4725.69f, 2261.02f, 337.54f, 2.76032f } },
    { 30846, { 4670.61f, 3187.49f, 343.136f, 3.25079f } },
    { 30846, { 4748.96f, 3180.21f, 349.524f, 2.92986f } },
    { 30846, { 4627.8f, 2362.98f, 333.347f, 4.74339f } },
    { 30846, { 4672.68f, 2779.12f, 338.747f, 1.49849f } },
    { 30846, { 4840.01f, 2831.54f, 346.966f, 3.50606f } },
    { 30846, { 4615.34f, 2738.8f, 345.254f, 5.2002f } },
    { 30846, { 4603.27f, 2788.44f, 334.735f, 5.32646f } },
    { 30846, { 4656.99f, 2460.82f, 335.597f, 1.52435f } },
    { 30846, { 4567.8f, 2836.47f, 348.893f, 4.80579f } },
    { 30846, { 5039.58f, 3589.58f, 353.197f, 2.98176f } },
    { 30846, { 5131.98f, 3605.52f, 352.892f, 4.03332f } },

    // 30849 - Chilled Earth Elemental
    { 30849, { 4724.14f, 3023.03f, 357.07f, 2.07377f } },
    { 30849, { 4402.46f, 2536.18f, 368.144f, 4.05318f } },
    { 30849, { 4758.89f, 2759.37f, 355.925f, 0.616189f } },
    { 30849, { 5072.92f, 2547.22f, 359.361f, 2.12185f } },
    { 30849, { 4585.57f, 2656.52f, 379.22f, 5.2706f } },
    { 30849, { 4661.45f, 2209.93f, 359.827f, 1.71621f } },
    { 30849, { 4819.77f, 2525.28f, 360.236f, 0.172158f } },
    { 30849, { 5116.65f, 2339.26f, 357.835f, 5.8196f } },
    { 30849, { 5129.76f, 2545.37f, 366.021f, 4.42132f } },
    { 30849, { 5248.6f, 2410.12f, 365.864f, 1.87671f } },
    { 30849, { 5069.81f, 2393.82f, 358.357f, 2.70691f } },
    { 30849, { 5120.63f, 2482.88f, 357.084f, 3.15846f } },
    { 30849, { 4581.11f, 2479.83f, 356.676f, 1.38399f } },
    { 30849, { 5113.0f, 2419.4f, 357.371f, 2.48173f } },
    { 30849, { 5106.28f, 3244.84f, 356.617f, 6.12696f } },
    { 30849, { 4952.88f, 2650.97f, 357.348f, 2.33141f } },
    { 30849, { 4783.14f, 2987.15f, 356.366f, 5.51822f } },
    { 30849, { 4511.95f, 2580.17f, 356.539f, 3.80415f } },
    { 30849, { 4920.75f, 3132.85f, 357.325f, 1.01521f } },
    { 30849, { 4602.48f, 2194.29f, 358.938f, 4.62069f } },
    { 30849, { 4972.42f, 2752.24f, 374.293f, 4.1734f } },
    { 30849, { 4715.46f, 3135.49f, 357.418f, 3.3733f } },
    { 30849, { 4989.06f, 3088.64f, 357.16f, 5.51382f } },
    { 30849, { 4374.41f, 3051.63f, 357.185f, 3.2587f } },
    { 30849, { 5081.12f, 2586.9f, 364.012f, 2.70754f } },
    { 30849, { 4752.59f, 2686.37f, 356.93f, 1.60445f } },
    { 30849, { 4417.34f, 2588.77f, 359.225f, 4.21174f } },
    { 30849, { 4982.31f, 3150.53f, 355.821f, 0.621733f } },
    { 30849, { 5198.47f, 2400.87f, 357.811f, 6.09233f } },
    { 30849, { 5148.35f, 3077.66f, 378.617f, 2.30801f } },
    { 30849, { 4954.05f, 2578.04f, 359.531f, 3.22827f } },
    { 30849, { 4816.06f, 2569.97f, 358.079f, 4.14084f } },
    { 30849, { 5088.33f, 2452.76f, 358.128f, 1.44475f } },
    { 30849, { 4741.97f, 2610.15f, 353.628f, 3.50084f } },
    { 30849, { 4917.1f, 2568.43f, 361.781f, 2.03022f } },
    { 30849, { 4864.81f, 2609.88f, 356.589f, 0.178893f } },
    { 30849, { 5043.54f, 2329.88f, 359.129f, 1.84723f } },
    { 30849, { 4807.91f, 3071.82f, 357.644f, 5.31699f } },
    { 30849, { 4877.42f, 2685.97f, 357.254f, 4.7469f } },
    { 30849, { 4344.59f, 3015.37f, 357.139f, 3.03624f } },
    { 30849, { 5092.04f, 3184.08f, 356.584f, 5.30346f } },
    { 30849, { 5095.09f, 3096.31f, 365.844f, 0.246351f } },
    { 30849, { 4721.82f, 2479.68f, 354.079f, 0.637859f } },
    { 30849, { 4620.45f, 2258.97f, 357.687f, 2.12215f } },
    { 30849, { 5154.0f, 2239.86f, 367.669f, 2.75339f } },
    { 30849, { 4537.05f, 2534.57f, 359.304f, 3.99622f } },
    { 30849, { 4754.27f, 2520.92f, 361.039f, 0.819296f } },
    { 30849, { 4816.85f, 2748.6f, 357.007f, 1.73854f } },
    { 30849, { 4912.62f, 2687.42f, 364.903f, 3.0498f } },
    { 30849, { 5048.19f, 2245.72f, 356.52f, 0.317315f } },
    { 30849, { 5227.51f, 2345.33f, 368.245f, 2.0103f } },
    { 30849, { 4816.63f, 2624.93f, 355.758f, 2.76636f } },
    { 30849, { 4833.85f, 2789.67f, 358.741f, 0.017142f } },
    { 30849, { 5143.38f, 3184.41f, 362.846f, 3.02974f } },

    // 30876 - Earthbound Revenant
    { 30876, { 4873.42f, 2652.43f, 357.654f, 0.76254f } },
    { 30876, { 4389.34f, 2508.53f, 370.818f, 2.11255f } },
    { 30876, { 5173.06f, 2249.31f, 371.722f, 2.83934f } },
    { 30876, { 5216.38f, 2383.63f, 361.764f, 4.19153f } },
    { 30876, { 4712.17f, 3103.18f, 357.862f, 2.56202f } },
    { 30876, { 5093.51f, 2531.22f, 362.62f, 4.29335f } },
    { 30876, { 4737.18f, 2548.74f, 368.521f, 1.63842f } },
    { 30876, { 4948.96f, 3113.54f, 358.345f, 4.24874f } },
    { 30876, { 4629.28f, 2217.87f, 360.606f, 6.04854f } },
    { 30876, { 4781.8f, 3025.19f, 358.45f, 6.1112f } },
    { 30876, { 4609.38f, 2548.96f, 375.02f, 4.64568f } },
    { 30876, { 4749.34f, 2724.37f, 359.097f, 1.52935f } },
    { 30876, { 5123.66f, 3225.15f, 357.609f, 0.978378f } },
    { 30876, { 5086.53f, 2422.25f, 358.788f, 4.05321f } },
    { 30876, { 4906.22f, 2612.16f, 360.119f, 2.36181f } },

    // 30877 - Water Revenant
    { 30877, { 4701.05f, 2620.23f, 335.868f, 3.42586f } },
    { 30877, { 5164.58f, 3538.6f, 353.628f, 0.479253f } },
    { 30877, { 5226.99f, 3385.88f, 353.644f, 0.787289f } },
    { 30877, { 4718.32f, 3353.8f, 336.015f, 3.46938f } },
    { 30877, { 4852.41f, 3513.47f, 331.376f, 5.56367f } },
    { 30877, { 4680.12f, 3261.2f, 335.64f, 5.7209f } },
    { 30877, { 5068.52f, 3514.3f, 354.219f, 5.94507f } },
    { 30877, { 4650.11f, 3216.19f, 336.037f, 0.462086f } },
    { 30877, { 4704.64f, 3453.58f, 353.475f, 5.79484f } },
    { 30877, { 4555.87f, 2976.34f, 335.639f, 0.399384f } },
    { 30877, { 5141.5f, 3416.66f, 353.632f, 5.51932f } },
    { 30877, { 4972.7f, 3210.85f, 346.899f, 2.70996f } },
    { 30877, { 4781.65f, 3148.91f, 346.899f, 5.31853f } },
    { 30877, { 4715.96f, 3519.97f, 344.724f, 0.793317f } },
    { 30877, { 4653.8f, 2342.48f, 334.472f, 1.18402f } },
    { 30877, { 4576.24f, 2360.43f, 334.347f, 3.19344f } },
    { 30877, { 4539.82f, 2426.31f, 345.484f, 1.58316f } },
    { 30877, { 4757.16f, 2220.46f, 335.432f, 2.83542f } },
    { 30877, { 4788.23f, 1975.37f, 420.174f, 0.598566f } },
    { 30877, { 4902.13f, 2235.0f, 347.502f, 0.766064f } },
    { 30877, { 4883.96f, 1985.27f, 410.708f, 3.55263f } },
    { 30877, { 4822.82f, 2041.69f, 404.181f, 2.35956f } },
    { 30877, { 4903.36f, 2049.29f, 402.789f, 2.98618f } },
    { 30877, { 4595.95f, 2848.9f, 335.764f, 0.52775f } },
    { 30877, { 4639.69f, 2769.04f, 335.584f, 3.56645f } },
    { 30877, { 4612.82f, 2909.1f, 335.637f, 3.27896f } },
    { 30877, { 4811.98f, 2179.59f, 334.711f, 0.231615f } },
};

uint32 const WG_MAX_TRASH_SPAWN = sizeof(WGTrashSpawns) / sizeof(WGTrashSpawns[0]);

// 기존 함수명은 유지하지만 실제 동작은 작업장별 공성 전차 웨이브로 변경한다.
// 헤더 수정 최소화를 위해 전역 관리값으로 처리한다.
static constexpr uint32 WG_WORKSHOP_SIEGE_FIRST_DELAY = 2 * MINUTE * IN_MILLISECONDS;
static constexpr uint32 WG_WORKSHOP_SIEGE_INTERVAL = 30 * IN_MILLISECONDS;
static constexpr uint8 WG_WORKSHOP_SIEGE_MAX_ALIVE = 5;
static constexpr float WG_ELITE_DEMOLISHER_STRUCTURE_FRONT_ARC = 1.57079632f; // 전방 180도
static constexpr uint32 WG_ELITE_DEMOLISHER_STRUCTURE_FAIL_LIMIT = 3;

static constexpr float WG_ELITE_DEMOLISHER_STRUCTURE_MELEE_SCAN_RANGE = 5.0f;
static constexpr uint32 WG_ELITE_DEMOLISHER_STRUCTURE_MELEE_SCAN_TIMER = 500;

static uint32 WGWorkshopSiegeWaveTimer = 0;
static bool WGWorkshopSiegeWaveEnabled = false;
static bool WGWorkshopSiegeFirstWaveAnnounced = false;

static std::vector<ObjectGuid> WGWorkshopSiegeGuids[WG_MAX_WORKSHOP];

uint8 const WGWorkshopSiegeWorkshopIds[] =
{
    BATTLEFIELD_WG_WORKSHOP_NE,
    BATTLEFIELD_WG_WORKSHOP_NW,
    BATTLEFIELD_WG_WORKSHOP_SE,
    BATTLEFIELD_WG_WORKSHOP_SW
};

struct WGEliteDemolisherPathData
{
    uint8 WorkshopId;
    char const* WorkshopName;
    Position const* Points;
    uint32 PointCount;
};

// 가라앉은 경기장
Position const WGEliteDemolisherPath_SunkenRing[] =
{
    { 4953.689941f, 2411.239990f, 320.977997f, 1.221730f },
    { 4966.397461f, 2461.66333f, 323.027283f, 0.848202f },
    { 5015.941895f, 2550.250488f, 347.463776f, 0.294496f },
    { 5237.320801f, 2534.881348f, 372.781281f, 0.375396f },
    { 5338.325195f, 2572.10376f, 397.799438f, 1.27703f },
    { 5347.557617f, 2603.480713f, 405.499664f, 1.591190f },  //성벽앞
	{ 5368.039062f, 2612.241211f, 408.273224f, 1.434895f },
	{ 5369.510254f, 2649.468262f, 409.09903f, 2.023159f },  //우측성벽
    { 5306.084473f, 2737.952637f, 409.191528f, 1.786754f },
};

/* 5237.320801 2534.881348 372.781281 571 0.375396
  5329.477051 2571.198486 395.885132 571 0.375396
  5329.669922 2614.481689 408.517822 571 1.437252f 
  5347.557617 2603.480713 405.499664 571 1.591190
  5368.039062 2612.241211 408.273224 571 1.434895f 
  5369.510254 2649.468262 409.09903 571 2.023159
  5330.087891 2643.87793 409.099518 571 1.594331
  */
// 나중에 추가 예정
// 파괴된 신전
Position const WGEliteDemolisherPath_BrokenTemple[] =
{
};

// 동부불꽃 작업장
Position const WGEliteDemolisherPath_EastSpark[] =
{
};

// 서부불꽃 작업장
Position const WGEliteDemolisherPath_WestSpark[] =
{
};

WGEliteDemolisherPathData const WGEliteDemolisherPaths[] =
{
    {
        BATTLEFIELD_WG_WORKSHOP_NE,
        "가라앉은 경기장",
        WGEliteDemolisherPath_SunkenRing,
        uint32(sizeof(WGEliteDemolisherPath_SunkenRing) / sizeof(WGEliteDemolisherPath_SunkenRing[0]))
    },

    // 나중에 좌표 채우면 주석 해제
    /*
    {
        BATTLEFIELD_WG_WORKSHOP_NW,
        "파괴된 신전",
        WGEliteDemolisherPath_BrokenTemple,
        uint32(sizeof(WGEliteDemolisherPath_BrokenTemple) / sizeof(WGEliteDemolisherPath_BrokenTemple[0]))
    },
    {
        BATTLEFIELD_WG_WORKSHOP_SE,
        "동부불꽃 작업장",
        WGEliteDemolisherPath_EastSpark,
        uint32(sizeof(WGEliteDemolisherPath_EastSpark) / sizeof(WGEliteDemolisherPath_EastSpark[0]))
    },
    {
        BATTLEFIELD_WG_WORKSHOP_SW,
        "서부불꽃 작업장",
        WGEliteDemolisherPath_WestSpark,
        uint32(sizeof(WGEliteDemolisherPath_WestSpark) / sizeof(WGEliteDemolisherPath_WestSpark[0]))
    },
    */
};

uint32 const WG_ELITE_DEMOLISHER_PATH_COUNT =
    sizeof(WGEliteDemolisherPaths) / sizeof(WGEliteDemolisherPaths[0]);

	WGEliteDemolisherPathData const* GetEliteDemolisherPathByWorkshopId(uint8 workshopId)
{
    for (uint32 i = 0; i < WG_ELITE_DEMOLISHER_PATH_COUNT; ++i)
    {
        if (WGEliteDemolisherPaths[i].WorkshopId == workshopId)
            return &WGEliteDemolisherPaths[i];
    }

    return nullptr;
}


// *********************************************************
// *****************WorkShop Data & Element*****************
// *********************************************************

struct StaticWintergraspWorkshopInfo
{
    uint8 WorkshopId;
    uint32 WorldStateId;

    struct
    {
        uint8 AllianceCapture;
        uint8 AllianceAttack;
        uint8 HordeCapture;
        uint8 HordeAttack;
    } TextIds;
};

enum WintergraspControlObjectType : uint8
{
    WG_CONTROL_TYPE_TOWER = 0,
    WG_CONTROL_TYPE_WORKSHOP = 1
};

struct WintergraspControlledGuardSpawnData
{
    Position Pos;
    uint32 HordeEntry;
    uint32 AllianceEntry;
};

struct WintergraspControlledGuardGroupData
{
    uint8 ControlType;
    uint8 ControlId;
    std::vector<WintergraspControlledGuardSpawnData> Guards;
};

uint8 const WG_MAX_CONTROLLED_GUARD_GROUP = 4;

static std::vector<ObjectGuid> WGControlledGuardGUIDs[WG_MAX_CONTROLLED_GUARD_GROUP][PVP_TEAMS_COUNT];

WintergraspControlledGuardGroupData const WGControlledGuardData[WG_MAX_CONTROLLED_GUARD_GROUP] =
{
    // 가라앉은 경기장 작업장 - 경비병 6명
    {
        WG_CONTROL_TYPE_WORKSHOP,
        BATTLEFIELD_WG_WORKSHOP_NE,
        {
			//정비공
			{ { 4939.76f,	2389.06f,	326.153f,	3.26377f },NPC_WG_GOBLIN_MECHANIC,NPC_WG_GNOMISH_ENGINEER},

            // 경기장 내부
            { { 4973.387695f, 2483.007568f, 327.282654f, 0.962100f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4992.764648f, 2471.363770f, 327.421936f, 1.242488f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            { { 4852.925781f, 2402.493408f, 321.921295f, 3.079535f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4854.228027f, 2423.447998f, 321.292206f, 3.079535f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            // 동부 교각 방향 다리 입구
            { { 4692.104980f, 2391.781250f, 369.210388f, 3.623031f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4683.687012f, 2413.844238f, 370.082611f, 3.623031f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A }
        }
    },

    // 파괴된 신전 작업장 - 경비병 10명
    {
        WG_CONTROL_TYPE_WORKSHOP,
        BATTLEFIELD_WG_WORKSHOP_NW,
        {
			//정비공
			{ { 4964.89f,	3383.06f,	382.911f,	6.12611f },NPC_WG_GOBLIN_MECHANIC,NPC_WG_GNOMISH_ENGINEER},

            // 서부 교각 방향 다리 입구
            { { 4780.043457f, 3280.044678f, 365.616302f, 3.034771f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4782.278809f, 3300.890137f, 365.623993f, 3.034771f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            // 파괴된 신전 서쪽 입구
            { { 4890.087402f, 3322.313232f, 372.441742f, 3.557057f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4879.944824f, 3345.305420f, 372.178040f, 3.557057f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            // 파괴된 신전 남쪽 입구
            { { 4884.403809f, 3274.364990f, 377.342133f, 4.834900f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4956.219727f, 3274.104980f, 377.511719f, 4.672323f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            // 파괴된 신전 중앙 길목
            { { 4989.513184f, 3291.207764f, 376.360382f, 5.595167f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 5007.905762f, 3308.645996f, 375.488068f, 5.241737f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            // 파괴된 신전 북쪽 입구
            { { 5013.368652f, 3368.302979f, 376.945892f, 1.239349f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4994.074219f, 3374.943115f, 376.643127f, 1.239349f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A }
        }
    },

    // 동부불꽃 작업장 - 경비병 9명
    {
        WG_CONTROL_TYPE_WORKSHOP,
        BATTLEFIELD_WG_WORKSHOP_SE,
        {
			//정비공
			{ { 4357.67f,	2357.99f,	382.007f,	1.67552f },NPC_WG_GOBLIN_MECHANIC,NPC_WG_GNOMISH_ENGINEER},

			// 작업장 내부 / 북쪽
            { { 4417.993652f, 2331.977295f, 370.945740f, 5.925813f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4420.685059f, 2352.122070f, 371.973907f, 5.925813f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            // 작업장 서쪽 입구
            { { 4391.853027f, 2300.901855f, 374.660278f, 4.706877f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4350.297852f, 2300.285645f, 374.660278f, 4.676247f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            // 작업장 중앙
            { { 4411.500000f, 2392.246094f, 376.261688f, 0.506566f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            // 작업장 남쪽 입구
            { { 4391.052246f, 2412.810059f, 374.661194f, 1.493026f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4348.953125f, 2413.635254f, 374.661194f, 1.487529f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            // 가라앉은 경기장 방향 다리 입구
            { { 4514.700684f, 2343.036133f, 369.614258f, 0.354202f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4524.788574f, 2315.756592f, 368.989075f, 0.354202f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A }
        }
    },

    // 서부불꽃 작업장 - 경비병 10명
    {
        WG_CONTROL_TYPE_WORKSHOP,
        BATTLEFIELD_WG_WORKSHOP_SW,
        {
			//정비공
			{ { 4354.15f,	3312.82f,	378.046f,	1.67552f },NPC_WG_GOBLIN_MECHANIC,NPC_WG_GNOMISH_ENGINEER},

            // 서부 교각 방향 다리 입구
            { { 4598.876465f, 3325.328369f, 365.623779f, 6.215632f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4607.960938f, 3353.466553f, 364.497070f, 5.744393f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            // 서부불꽃 작업장 남쪽 입구
            { { 4364.798828f, 3249.345459f, 373.006714f, 4.874167f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4400.758789f, 3250.129150f, 373.489960f, 4.603205f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            // 서부불꽃 작업장 동쪽 입구
            { { 4449.114258f, 3262.678955f, 371.144928f, 0.104445f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4420.425293f, 3289.222412f, 371.969116f, 0.369124f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            // 서부불꽃 작업장 북쪽 길목
            { { 4420.343750f, 3320.423096f, 370.674927f, 6.103316f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4422.795898f, 3355.178711f, 372.304260f, 0.666790f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },

            // 서부불꽃 작업장 서쪽 입구
            { { 4391.585938f, 3359.791016f, 368.375702f, 1.457686f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A },
            { { 4373.050293f, 3361.896484f, 368.977234f, 1.457686f }, BATTLEFIELD_WG_NPC_GUARD_H, BATTLEFIELD_WG_NPC_GUARD_A }
        }
    }
};

static int32 FindWintergraspControlledGuardGroup(uint8 controlType, uint8 controlId)
{
    for (uint8 i = 0; i < WG_MAX_CONTROLLED_GUARD_GROUP; ++i)
    {
        if (WGControlledGuardData[i].ControlType == controlType &&
            WGControlledGuardData[i].ControlId == controlId)
            return i;
    }

    return -1;
}

static void UpdateWintergraspControlledGuardGroup(BattlefieldWG* wg, uint8 controlType, uint8 controlId, TeamId controlTeam)
{
    if (!wg)
        return;

    int32 groupIndex = FindWintergraspControlledGuardGroup(controlType, controlId);
    if (groupIndex < 0)
        return;

    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
    {
        for (ObjectGuid const& guid : WGControlledGuardGUIDs[groupIndex][team])
        {
            Creature* creature = wg->GetCreature(guid);
            if (!creature)
                continue;

            if (controlTeam != TEAM_ALLIANCE && controlTeam != TEAM_HORDE)
            {
                wg->HideNpc(creature);
                continue;
            }

            if (team == controlTeam)
            {
                creature->SetFaction(WintergraspFaction[team]);
                wg->ShowNpc(creature, true);
            }
            else
            {
                wg->HideNpc(creature);
            }
        }
    }
}

static void SpawnWintergraspControlledGuards(BattlefieldWG* wg)
{
    if (!wg)
        return;

    for (uint8 groupIndex = 0; groupIndex < WG_MAX_CONTROLLED_GUARD_GROUP; ++groupIndex)
    {
        WGControlledGuardGUIDs[groupIndex][TEAM_ALLIANCE].clear();
        WGControlledGuardGUIDs[groupIndex][TEAM_HORDE].clear();

        for (WintergraspControlledGuardSpawnData const& guardData : WGControlledGuardData[groupIndex].Guards)
        {
            if (Creature* hordeGuard = wg->SpawnCreature(guardData.HordeEntry, guardData.Pos))
            {
                WGControlledGuardGUIDs[groupIndex][TEAM_HORDE].push_back(hordeGuard->GetGUID());
                wg->HideNpc(hordeGuard);
            }

            if (Creature* allianceGuard = wg->SpawnCreature(guardData.AllianceEntry, guardData.Pos))
            {
                WGControlledGuardGUIDs[groupIndex][TEAM_ALLIANCE].push_back(allianceGuard->GetGUID());
                wg->HideNpc(allianceGuard);
            }
        }
    }
}

StaticWintergraspWorkshopInfo const WorkshopData[WG_MAX_WORKSHOP] =
{
    { BATTLEFIELD_WG_WORKSHOP_NE, WS_BATTLEFIELD_WG_WORKSHOP_NE, { BATTLEFIELD_WG_TEXT_SUNKEN_RING_CAPTURE_ALLIANCE,   BATTLEFIELD_WG_TEXT_SUNKEN_RING_ATTACK_ALLIANCE,   BATTLEFIELD_WG_TEXT_SUNKEN_RING_CAPTURE_HORDE,   BATTLEFIELD_WG_TEXT_SUNKEN_RING_ATTACK_HORDE   } },
    { BATTLEFIELD_WG_WORKSHOP_NW, WS_BATTLEFIELD_WG_WORKSHOP_NW, { BATTLEFIELD_WG_TEXT_BROKEN_TEMPLE_CAPTURE_ALLIANCE, BATTLEFIELD_WG_TEXT_BROKEN_TEMPLE_ATTACK_ALLIANCE, BATTLEFIELD_WG_TEXT_BROKEN_TEMPLE_CAPTURE_HORDE, BATTLEFIELD_WG_TEXT_BROKEN_TEMPLE_ATTACK_HORDE } },
    { BATTLEFIELD_WG_WORKSHOP_SE, WS_BATTLEFIELD_WG_WORKSHOP_SE, { BATTLEFIELD_WG_TEXT_EASTSPARK_CAPTURE_ALLIANCE,     BATTLEFIELD_WG_TEXT_EASTSPARK_ATTACK_ALLIANCE,     BATTLEFIELD_WG_TEXT_EASTSPARK_CAPTURE_HORDE,     BATTLEFIELD_WG_TEXT_EASTSPARK_ATTACK_HORDE     } },
    { BATTLEFIELD_WG_WORKSHOP_SW, WS_BATTLEFIELD_WG_WORKSHOP_SW, { BATTLEFIELD_WG_TEXT_WESTSPARK_CAPTURE_ALLIANCE,     BATTLEFIELD_WG_TEXT_WESTSPARK_ATTACK_ALLIANCE,     BATTLEFIELD_WG_TEXT_WESTSPARK_CAPTURE_HORDE,     BATTLEFIELD_WG_TEXT_WESTSPARK_ATTACK_HORDE     } },
    // KEEP WORKSHOPS - It can't be taken, so it doesn't have a textids
    { BATTLEFIELD_WG_WORKSHOP_KEEP_WEST, WS_BATTLEFIELD_WG_WORKSHOP_K_W, { 0, 0, 0, 0 } },
    { BATTLEFIELD_WG_WORKSHOP_KEEP_EAST, WS_BATTLEFIELD_WG_WORKSHOP_K_E, { 0, 0, 0, 0 } }
};

static bool IsWintergraspKeepWorkshop(uint8 workshopId)
{
    return workshopId == BATTLEFIELD_WG_WORKSHOP_KEEP_WEST ||
           workshopId == BATTLEFIELD_WG_WORKSHOP_KEEP_EAST;
}

static int32 GetWintergraspWorkshopGraveyardId(uint8 workshopId)
{
    switch (workshopId)
    {
        case BATTLEFIELD_WG_WORKSHOP_NE:
            return BATTLEFIELD_WG_GY_WORKSHOP_NE;

        case BATTLEFIELD_WG_WORKSHOP_NW:
            return BATTLEFIELD_WG_GY_WORKSHOP_NW;

        case BATTLEFIELD_WG_WORKSHOP_SE:
            return BATTLEFIELD_WG_GY_WORKSHOP_SE;

        case BATTLEFIELD_WG_WORKSHOP_SW:
            return BATTLEFIELD_WG_GY_WORKSHOP_SW;

        default:
            break;
    }

    return -1;
}

static void GiveWintergraspWorkshopGraveyardControl(BattlefieldWG* wg, uint8 workshopId, TeamId teamId)
{
    if (!wg)
        return;

    int32 graveyardId = GetWintergraspWorkshopGraveyardId(workshopId);
    if (graveyardId < 0)
        return;

    if (BfGraveyard* graveyard = wg->GetGraveyardById(uint8(graveyardId)))
        graveyard->GiveControlTo(teamId);
}

BattlefieldWG::~BattlefieldWG()
{
    for (WintergraspWorkshop* workshop : Workshops)
        delete workshop;

    for (BfWGGameObjectBuilding* building : BuildingsInZone)
        delete building;
}

bool BattlefieldWG::SetupBattlefield()
{
    m_TypeId = BATTLEFIELD_WG;                              // See enum BattlefieldTypes
    m_BattleId = BATTLEFIELD_BATTLEID_WG;
    m_ZoneId = AREA_WINTERGRASP;
    m_MapId = BATTLEFIELD_WG_MAPID;
    m_Map = sMapMgr->FindMap(m_MapId, 0);

    InitStalker(BATTLEFIELD_WG_NPC_STALKER, WintergraspStalkerPos);

    m_MaxPlayer = sWorld->getIntConfig(CONFIG_WINTERGRASP_PLR_MAX);
    m_IsEnabled = sWorld->getBoolConfig(CONFIG_WINTERGRASP_ENABLE);
    m_MinPlayer = sWorld->getIntConfig(CONFIG_WINTERGRASP_PLR_MIN);
    m_MinLevel = sWorld->getIntConfig(CONFIG_WINTERGRASP_PLR_MIN_LVL);
    m_BattleTime = sWorld->getIntConfig(CONFIG_WINTERGRASP_BATTLETIME) * MINUTE * IN_MILLISECONDS;
    m_NoWarBattleTime = sWorld->getIntConfig(CONFIG_WINTERGRASP_NOBATTLETIME) * MINUTE * IN_MILLISECONDS;
    m_RestartAfterCrash = sWorld->getIntConfig(CONFIG_WINTERGRASP_RESTART_AFTER_CRASH) * MINUTE * IN_MILLISECONDS;

    m_TimeForAcceptInvite = 20;
    m_StartGroupingTimer = 15 * MINUTE * IN_MILLISECONDS;
    m_StartGrouping = false;

    m_tenacityTeam = TEAM_NEUTRAL;
    m_tenacityStack = 0;

	WGWorkshopSiegeWaveTimer = 0;
	WGWorkshopSiegeWaveEnabled = false;
	WGWorkshopSiegeFirstWaveAnnounced = false;

	for (uint8 i = 0; i < WG_MAX_WORKSHOP; ++i)
		WGWorkshopSiegeGuids[i].clear();

    KickPosition.Relocate(5728.117f, 2714.346f, 697.733f, 0);
    KickPosition.m_mapId = m_MapId;

    RegisterZone(m_ZoneId);

    m_Data32.resize(BATTLEFIELD_WG_DATA_MAX);

    m_saveTimer = 60000;

    // Init Graveyards
    SetGraveyardNumber(BATTLEFIELD_WG_GRAVEYARD_MAX);

    // Load from db
    if ((sWorld->getWorldState(WS_BATTLEFIELD_WG_ACTIVE) == 0) && (sWorld->getWorldState(WS_BATTLEFIELD_WG_DEFENDER) == 0)
            && (sWorld->getWorldState(ClockWorldState[0]) == 0))
    {
        sWorld->setWorldState(WS_BATTLEFIELD_WG_ACTIVE, uint64(false));
        sWorld->setWorldState(WS_BATTLEFIELD_WG_DEFENDER, uint64(urand(0, 1)));
        sWorld->setWorldState(ClockWorldState[0], uint64(m_NoWarBattleTime));
    }

    m_isActive = sWorld->getWorldState(WS_BATTLEFIELD_WG_ACTIVE) != 0;
    m_DefenderTeam = TeamId(sWorld->getWorldState(WS_BATTLEFIELD_WG_DEFENDER));

    m_Timer = sWorld->getWorldState(ClockWorldState[0]);
    if (m_isActive)
    {
        m_isActive = false;
        m_Timer = m_RestartAfterCrash;
    }

    SetData(BATTLEFIELD_WG_DATA_WON_A, uint32(sWorld->getWorldState(WS_BATTLEFIELD_WG_ATTACKED_A)));
    SetData(BATTLEFIELD_WG_DATA_DEF_A, uint32(sWorld->getWorldState(WS_BATTLEFIELD_WG_DEFENDED_A)));
    SetData(BATTLEFIELD_WG_DATA_WON_H, uint32(sWorld->getWorldState(WS_BATTLEFIELD_WG_ATTACKED_H)));
    SetData(BATTLEFIELD_WG_DATA_DEF_H, uint32(sWorld->getWorldState(WS_BATTLEFIELD_WG_DEFENDED_H)));

    for (uint8 i = 0; i < BATTLEFIELD_WG_GRAVEYARD_MAX; i++)
    {
        BfGraveyardWG* graveyard = new BfGraveyardWG(this);

        // When between games, the graveyard is controlled by the defending team
        if (WGGraveyard[i].StartControl == TEAM_NEUTRAL)
            graveyard->Initialize(m_DefenderTeam, WGGraveyard[i].GraveyardID);
        else
            graveyard->Initialize(WGGraveyard[i].StartControl, WGGraveyard[i].GraveyardID);

        graveyard->SetTextId(WGGraveyard[i].TextID);
        m_GraveyardList[i] = graveyard;
    }

    Workshops.resize(WG_MAX_WORKSHOP);
	// Spawn workshop creatures and gameobjects
	for (uint8 i = 0; i < WG_MAX_WORKSHOP; i++)
	{
		WintergraspWorkshop* workshop = new WintergraspWorkshop(this, i);

		if (IsWintergraspKeepWorkshop(workshop->GetId()))
			workshop->GiveControlTo(GetDefenderTeam(), true);
		else
			workshop->GiveControlTo(GetAttackerTeam(), true);

		// Note: Capture point is added once the gameobject is created.
		Workshops[i] = workshop;
	}

    // Spawn turrets and hide them per default
    for (uint8 i = 0; i < WG_MAX_TURRET; i++)
    {
        Position towerCannonPos = WGTurret[i].GetPosition();
        if (Creature* creature = SpawnCreature(NPC_WINTERGRASP_TOWER_CANNON, towerCannonPos))
        {
            CanonList.push_back(creature->GetGUID());
            HideNpc(creature);
        }
    }

	// Spawn Wintergrasp trash mobs and show them during peace time
	for (uint32 i = 0; i < WG_MAX_TRASH_SPAWN; ++i)
	{
		if (Creature* creature = SpawnCreature(WGTrashSpawns[i].Entry, WGTrashSpawns[i].Pos))
		{
			TrashMobList.push_back(creature->GetGUID());

			if (IsWarTime())
				HideNpc(creature);
			else
				ShowNpc(creature, true);
		}
	}

    BuildingsInZone.resize(WG_MAX_OBJ);
    // Spawn all gameobjects
    for (uint8 i = 0; i < WG_MAX_OBJ; i++)
    {
        if (GameObject* go = SpawnGameObject(WGGameObjectBuilding[i].entry, WGGameObjectBuilding[i].pos, WGGameObjectBuilding[i].rot))
        {
            BfWGGameObjectBuilding* b = new BfWGGameObjectBuilding(this, WGGameObjectBuilding[i].type, WGGameObjectBuilding[i].WorldState);
            b->Init(go);
            if (!IsEnabled() && go->GetEntry() == GO_WINTERGRASP_VAULT_GATE)
                go->SetDestructibleState(GO_DESTRUCTIBLE_DESTROYED);

            BuildingsInZone[i] = b;
        }
    }

	SpawnWintergraspControlledGuards(this);

    for (WintergraspWorkshop* workshop : Workshops)
    {
        if (!workshop)
            continue;

        UpdateWintergraspControlledGuardGroup(this, WG_CONTROL_TYPE_WORKSHOP, workshop->GetId(), workshop->GetTeamControl());
    }

    // Spawning portal defender
	for (uint8 i = 0; i < WG_MAX_TELEPORTER; ++i)
	{
		WintergraspGameObjectData const& teleporter = WGPortalDefenderData[i];

		// 192819: 요새 내부 귀환 포탈은 중립 상시 포탈
		if (teleporter.HordeEntry == GO_WG_RETURN_PORTAL_INSIDE_FORTRESS &&
			teleporter.AllianceEntry == GO_WG_RETURN_PORTAL_INSIDE_FORTRESS)
		{
			if (GameObject* go = SpawnGameObject(teleporter.HordeEntry, teleporter.Pos, teleporter.Rot))
			{
				go->SetFaction(WintergraspFaction[TEAM_NEUTRAL]);
				go->SetRespawnTime(RESPAWN_IMMEDIATELY);
			}

			continue;
		}

		// 192951: 차량 포탈은 방어팀 전용
		// entry는 같지만 호드용/얼라용 GUID를 각각 만들어서 DefenderPortalList로 제어한다.
		if (teleporter.HordeEntry == GO_WG_VEHICLE_TELEPORTER &&
			teleporter.AllianceEntry == GO_WG_VEHICLE_TELEPORTER)
		{
			if (GameObject* goAlliance = SpawnGameObject(teleporter.AllianceEntry, teleporter.Pos, teleporter.Rot))
			{
				goAlliance->SetFaction(WintergraspFaction[TEAM_ALLIANCE]);
				DefenderPortalList[TEAM_ALLIANCE].push_back(goAlliance->GetGUID());
			}

			if (GameObject* goHorde = SpawnGameObject(teleporter.HordeEntry, teleporter.Pos, teleporter.Rot))
			{
				goHorde->SetFaction(WintergraspFaction[TEAM_HORDE]);
				DefenderPortalList[TEAM_HORDE].push_back(goHorde->GetGUID());
			}

			continue;
		}

		// 일반 방어팀 플레이어 포탈
		if (GameObject* goAlliance = SpawnGameObject(teleporter.AllianceEntry, teleporter.Pos, teleporter.Rot))
		{
			goAlliance->SetFaction(WintergraspFaction[TEAM_ALLIANCE]);
			DefenderPortalList[TEAM_ALLIANCE].push_back(goAlliance->GetGUID());
		}

		if (GameObject* goHorde = SpawnGameObject(teleporter.HordeEntry, teleporter.Pos, teleporter.Rot))
		{
			goHorde->SetFaction(WintergraspFaction[TEAM_HORDE]);
			DefenderPortalList[TEAM_HORDE].push_back(goHorde->GetGUID());
		}
	}

	UpdateDefenderPortals();

    UpdateCounterVehicle(true);
    return true;
}

bool BattlefieldWG::Update(uint32 diff)
{
    bool m_return = Battlefield::Update(diff);
    if (m_saveTimer <= diff)
    {
        sWorld->setWorldState(WS_BATTLEFIELD_WG_ACTIVE, m_isActive);
        sWorld->setWorldState(WS_BATTLEFIELD_WG_DEFENDER, m_DefenderTeam);
        sWorld->setWorldState(ClockWorldState[0], m_Timer);
        sWorld->setWorldState(WS_BATTLEFIELD_WG_ATTACKED_A, GetData(BATTLEFIELD_WG_DATA_WON_A));
        sWorld->setWorldState(WS_BATTLEFIELD_WG_DEFENDED_A, GetData(BATTLEFIELD_WG_DATA_DEF_A));
        sWorld->setWorldState(WS_BATTLEFIELD_WG_ATTACKED_H, GetData(BATTLEFIELD_WG_DATA_WON_H));
        sWorld->setWorldState(WS_BATTLEFIELD_WG_DEFENDED_H, GetData(BATTLEFIELD_WG_DATA_DEF_H));
        m_saveTimer = 60 * IN_MILLISECONDS;
    }
    else
        m_saveTimer -= diff;

	WGSpiritImmunityCheckTimer += diff;

	if (WGSpiritImmunityCheckTimer >= 1000)
	{
		for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
		{
			for (ObjectGuid const& guid : m_players[team])
			{
				Player* player = ObjectAccessor::FindPlayer(guid);
				if (!player)
					continue;

				if (!player->HasAura(SPELL_SPIRITUAL_IMMUNITY))
					continue;

				if (!IsPlayerInWintergraspGraveyardArea(this, player))
					player->RemoveAurasDueToSpell(SPELL_SPIRITUAL_IMMUNITY);
			}
		}

		WGSpiritImmunityCheckTimer = 0;
	}

	if (WGWorkshopSiegeWaveEnabled && IsWarTime())
	{
		if (WGWorkshopSiegeWaveTimer <= diff)
		{
			WGWorkshopSiegeWaveTimer = WG_WORKSHOP_SIEGE_INTERVAL;
			SpawnSingleEliteDemolisher();
		}
		else
		{
			WGWorkshopSiegeWaveTimer -= diff;
		}
	}


    return m_return;
}

void BattlefieldWG::OnBattleStart()
{
	HideTrashMobs();

	WGWorkshopSiegeWaveEnabled = true;
	WGWorkshopSiegeWaveTimer = WG_WORKSHOP_SIEGE_FIRST_DELAY;
	WGWorkshopSiegeFirstWaveAnnounced = false;

	for (uint8 i = 0; i < WG_MAX_WORKSHOP; ++i)
		WGWorkshopSiegeGuids[i].clear();

	SendWarning(BATTLEFIELD_WG_TEXT_SIEGE_WAVE_LEADY);

    // Spawn titan relic
    if (GameObject* relic = SpawnGameObject(GO_WINTERGRASP_TITAN_S_RELIC, WintergraspRelicPos, WintergraspRelicRot))
    {
        // Update faction of relic, only attacker can click on
        relic->SetFaction(WintergraspFaction[GetAttackerTeam()]);
        // Set in use (not allow to click on before last door is broken)
        relic->SetFlag(GO_FLAG_IN_USE | GO_FLAG_NOT_SELECTABLE);
        m_titansRelicGUID = relic->GetGUID();
    }
    else
        TC_LOG_ERROR("bg.battlefield", "WG: Failed to spawn titan relic.");

    // Update tower visibility and update faction
    for (auto itr = CanonList.begin(); itr != CanonList.end(); ++itr)
    {
        if (Creature* creature = GetCreature(*itr))
        {
            ShowNpc(creature, true);
            creature->SetFaction(WintergraspFaction[GetDefenderTeam()]);
        }
    }

    // Rebuild all wall
    for (BfWGGameObjectBuilding* building : BuildingsInZone)
    {
        building->Rebuild();
        building->UpdateTurretAttack(false);
    }

    SetData(BATTLEFIELD_WG_DATA_BROKEN_TOWER_ATT, 0);
    SetData(BATTLEFIELD_WG_DATA_BROKEN_TOWER_DEF, 0);
    SetData(BATTLEFIELD_WG_DATA_DAMAGED_TOWER_ATT, 0);
    SetData(BATTLEFIELD_WG_DATA_DAMAGED_TOWER_DEF, 0);

    // Update graveyard (in no war time all graveyard is to deffender, in war time, depend of base)
    for (WintergraspWorkshop* workshop : Workshops)
        workshop->UpdateGraveyardAndWorkshop();

    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
    {
        for (auto itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
        {
            // Kick player in orb room, TODO: offline player ?
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
            {
                float x, y, z;
                player->GetPosition(x, y, z);
                if (5500 > x && x > 5392 && y < 2880 && y > 2800 && z < 480)
                    player->TeleportTo(571, 5349.8686f, 2838.481f, 409.240f, 0.046328f);
                SendInitWorldStatesTo(player);
            }
        }
    }

    // Initialize vehicle counter
    UpdateCounterVehicle(true);
    // Send start warning to all players

	ApplyWintergraspControlPhaseToAll();

	uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
	if (realmId == 1)
		SendWGDiscordEvent(
			"battle_start",
			"⚔️ 겨울손아귀 전투 시작",
			std::string("공격팀: ") + GetWGTeamName(GetAttackerTeam()) + "\n"
			"방어팀: " + GetWGTeamName(GetDefenderTeam()) + "\n"
			"작업장과 경비탑을 장악해 전황을 뒤집어 보세요.",
			5
		);

    SendWarning(BATTLEFIELD_WG_TEXT_START_BATTLE);
}

void BattlefieldWG::UpdateCounterVehicle(bool init)
{
    if (init)
    {
        SetData(BATTLEFIELD_WG_DATA_VEHICLE_H, 0);
        SetData(BATTLEFIELD_WG_DATA_VEHICLE_A, 0);
    }
    SetData(BATTLEFIELD_WG_DATA_MAX_VEHICLE_H, 0);
    SetData(BATTLEFIELD_WG_DATA_MAX_VEHICLE_A, 0);

    for (WintergraspWorkshop* workshop : Workshops)
    {
        if (workshop->GetTeamControl() == TEAM_ALLIANCE)
            UpdateData(BATTLEFIELD_WG_DATA_MAX_VEHICLE_A, 4);
        else if (workshop->GetTeamControl() == TEAM_HORDE)
            UpdateData(BATTLEFIELD_WG_DATA_MAX_VEHICLE_H, 4);
    }

    UpdateVehicleCountWG();
}

void BattlefieldWG::UpdateDefenderPortals()
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
    {
        for (ObjectGuid const& guid : DefenderPortalList[team])
        {
            if (GameObject* portal = GetGameObject(guid))
            {
                portal->SetFaction(WintergraspFaction[team]);

                if (team == GetDefenderTeam())
                    portal->SetRespawnTime(RESPAWN_IMMEDIATELY);
                else
                    portal->SetRespawnTime(RESPAWN_ONE_DAY);
            }
        }
    }
}

void BattlefieldWG::OnBattleEnd(bool endByTimer)
{
	ClearWGResurrectQueue();
	RemoveSingleEliteDemolisher();

    // Remove relic
    if (!m_titansRelicGUID.IsEmpty())
        if (GameObject* relic = GetGameObject(m_titansRelicGUID))
            relic->RemoveFromWorld();
    m_titansRelicGUID.Clear();

    // change collision wall state closed
    for (BfWGGameObjectBuilding* building : BuildingsInZone)
    {
        building->RebuildGate();
    }

    // successful defense
    if (endByTimer)
        UpdateData(GetDefenderTeam() == TEAM_HORDE ? BATTLEFIELD_WG_DATA_DEF_H : BATTLEFIELD_WG_DATA_DEF_A, 1);
    // successful attack (note that teams have already been swapped, so defender team is the one who won)
    else
        UpdateData(GetDefenderTeam() == TEAM_HORDE ? BATTLEFIELD_WG_DATA_WON_H : BATTLEFIELD_WG_DATA_WON_A, 1);

    // Remove turret
    for (auto itr = CanonList.begin(); itr != CanonList.end(); ++itr)
    {
        if (Creature* creature = GetCreature(*itr))
        {
            if (!endByTimer)
                creature->SetFaction(WintergraspFaction[GetDefenderTeam()]);
            HideNpc(creature);
        }
    }

	for (BfWGGameObjectBuilding* building : BuildingsInZone)
    {
        if (!building)
            continue;

        building->UpdateTurretAttack(true);
    }

    // Update all graveyard, control is to defender when no wartime
    for (uint8 i = 0; i < BATTLEFIELD_WG_GY_HORDE; i++)
        if (BfGraveyard* graveyard = GetGraveyardById(i))
            graveyard->GiveControlTo(GetDefenderTeam());

	for (WintergraspWorkshop* workshop : Workshops)
	{
		if (!workshop)
			continue;

		UpdateWintergraspControlledGuardGroup(this, WG_CONTROL_TYPE_WORKSHOP, workshop->GetId(), workshop->GetTeamControl());
	}

    // Update portals
    UpdateDefenderPortals();

    // Saving data
    for (BfWGGameObjectBuilding* building : BuildingsInZone)
        building->Save();

    for (WintergraspWorkshop* workshop : Workshops)
        workshop->Save();

    for (auto itr = m_PlayersInWar[GetDefenderTeam()].begin(); itr != m_PlayersInWar[GetDefenderTeam()].end(); ++itr)
    {
        if (Player* player = ObjectAccessor::FindPlayer(*itr))
        {
            player->CastSpell(player, SPELL_ESSENCE_OF_WINTERGRASP, true);
            player->CastSpell(player, SPELL_VICTORY_REWARD, true);
            // Complete victory quests
            player->AreaExploredOrEventHappens(QUEST_VICTORY_WINTERGRASP_A);
            player->AreaExploredOrEventHappens(QUEST_VICTORY_WINTERGRASP_H);
            // Send Wintergrasp victory achievement
            DoCompleteOrIncrementAchievement(ACHIEVEMENTS_WIN_WG, player);
            // Award achievement for succeeding in Wintergrasp in 10 minutes or less
            if (!endByTimer && GetTimer() <= 10000)
                DoCompleteOrIncrementAchievement(ACHIEVEMENTS_WIN_WG_TIMER_10, player);
        }
    }

    for (auto itr = m_PlayersInWar[GetAttackerTeam()].begin(); itr != m_PlayersInWar[GetAttackerTeam()].end(); ++itr)
        if (Player* player = ObjectAccessor::FindPlayer(*itr))
            player->CastSpell(player, SPELL_DEFEAT_REWARD, true);

    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
    {
        for (auto itr = m_PlayersInWar[team].begin(); itr != m_PlayersInWar[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                RemoveAurasFromPlayer(player);

        m_PlayersInWar[team].clear();

        for (auto itr = m_vehicles[team].begin(); itr != m_vehicles[team].end(); ++itr)
            if (Creature* creature = GetCreature(*itr))
                if (creature->IsVehicle())
                    creature->DespawnOrUnsummon();

        m_vehicles[team].clear();
    }

	ShowTrashMobs();

    ApplyWintergraspControlPhaseToAll();

	uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
	if (realmId == 1)
		SendWGDiscordEvent(
			"battle_end",
			endByTimer ? "🛡️ 겨울손아귀 방어 성공" : "🏰 겨울손아귀 요새 점령",
			std::string(GetWGTeamName(GetDefenderTeam())) + "가 겨울손아귀를 차지했습니다.\n"
			"다음 전투를 준비하세요.",
			5
		);

    if (!endByTimer) // win alli/horde
        SendWarning(GetDefenderTeam() == TEAM_ALLIANCE ? BATTLEFIELD_WG_TEXT_FORTRESS_CAPTURE_ALLIANCE : BATTLEFIELD_WG_TEXT_FORTRESS_CAPTURE_HORDE);
    else // defend alli/horde
        SendWarning(GetDefenderTeam() == TEAM_ALLIANCE ? BATTLEFIELD_WG_TEXT_FORTRESS_DEFEND_ALLIANCE : BATTLEFIELD_WG_TEXT_FORTRESS_DEFEND_HORDE);
}

// *******************************************************
// ******************* Reward System *********************
// *******************************************************
void BattlefieldWG::DoCompleteOrIncrementAchievement(uint32 achievement, Player* player, uint8 /*incrementNumber*/)
{
    AchievementEntry const* achievementEntry = sAchievementStore.LookupEntry(achievement);

    if (!achievementEntry)
        return;

    switch (achievement)
    {
        case ACHIEVEMENTS_WIN_WG_100:
        {
            // player->UpdateAchievementCriteria();
            break;
        }
        default:
        {
            if (player)
                player->CompletedAchievement(achievementEntry);
            break;
        }
    }

}

void BattlefieldWG::OnStartGrouping()
{
    SendWarning(BATTLEFIELD_WG_TEXT_START_GROUPING);
}

uint8 BattlefieldWG::GetSpiritGraveyardId(uint32 areaId) const
{
    switch (areaId)
    {
        case AREA_WINTERGRASP_FORTRESS:
            return BATTLEFIELD_WG_GY_KEEP;
        case AREA_THE_SUNKEN_RING:
            return BATTLEFIELD_WG_GY_WORKSHOP_NE;
        case AREA_THE_BROKEN_TEMPLATE:
            return BATTLEFIELD_WG_GY_WORKSHOP_NW;
        case AREA_WESTPARK_WORKSHOP:
            return BATTLEFIELD_WG_GY_WORKSHOP_SW;
        case AREA_EASTPARK_WORKSHOP:
            return BATTLEFIELD_WG_GY_WORKSHOP_SE;
        case AREA_WINTERGRASP:
            return BATTLEFIELD_WG_GY_ALLIANCE;
        case AREA_THE_CHILLED_QUAGMIRE:
            return BATTLEFIELD_WG_GY_HORDE;
        default:
            TC_LOG_ERROR("bg.battlefield", "BattlefieldWG::GetSpiritGraveyardId: Unexpected Area Id {}", areaId);
            break;
    }

    return 0;
}

void BattlefieldWG::OnCreatureCreate(Creature* creature)
{
    // Accessing to db spawned creatures
    switch (creature->GetEntry())
    {
        case NPC_DWARVEN_SPIRIT_GUIDE:
        case NPC_TAUNKA_SPIRIT_GUIDE:
        {
            TeamId teamId = (creature->GetEntry() == NPC_DWARVEN_SPIRIT_GUIDE ? TEAM_ALLIANCE : TEAM_HORDE);
            uint8 graveyardId = GetSpiritGraveyardId(creature->GetAreaId());
            if (m_GraveyardList[graveyardId])
                m_GraveyardList[graveyardId]->SetSpirit(creature, teamId);
            break;
        }
    }

    // untested code - not sure if it is valid.
    if (IsWarTime())
    {
        switch (creature->GetEntry())
        {
            case NPC_WINTERGRASP_SIEGE_ENGINE_ALLIANCE:
            case NPC_WINTERGRASP_SIEGE_ENGINE_HORDE:
            case NPC_WINTERGRASP_CATAPULT:
            case NPC_WINTERGRASP_DEMOLISHER:
            {
                if (!creature->ToTempSummon() || !creature->ToTempSummon()->GetSummonerGUID() || !ObjectAccessor::FindPlayer(creature->ToTempSummon()->GetSummonerGUID()))
                {
                    creature->DespawnOrUnsummon();
                    return;
                }

                Player* creator = ObjectAccessor::FindPlayer(creature->ToTempSummon()->GetSummonerGUID());
                TeamId team = creator->GetTeamId();

                if (team == TEAM_HORDE)
                {
                    if (GetData(BATTLEFIELD_WG_DATA_VEHICLE_H) < GetData(BATTLEFIELD_WG_DATA_MAX_VEHICLE_H))
                    {
                        UpdateData(BATTLEFIELD_WG_DATA_VEHICLE_H, 1);
                        creature->AddAura(SPELL_HORDE_FLAG, creature);
                        m_vehicles[team].insert(creature->GetGUID());
                        UpdateVehicleCountWG();
                    }
                    else
                    {
                        creature->DespawnOrUnsummon();
                        return;
                    }
                }
                else
                {
                    if (GetData(BATTLEFIELD_WG_DATA_VEHICLE_A) < GetData(BATTLEFIELD_WG_DATA_MAX_VEHICLE_A))
                    {
                        UpdateData(BATTLEFIELD_WG_DATA_VEHICLE_A, 1);
                        creature->AddAura(SPELL_ALLIANCE_FLAG, creature);
                        m_vehicles[team].insert(creature->GetGUID());
                        UpdateVehicleCountWG();
                    }
                    else
                    {
                        creature->DespawnOrUnsummon();
                        return;
                    }
                }

                creature->CastSpell(creator, SPELL_GRAB_PASSENGER, true);
                break;
            }
        }
    }
}

void BattlefieldWG::OnCreatureRemove(Creature* /*creature*/)
{
/* possibly can be used later
    if (IsWarTime())
    {
        switch (creature->GetEntry())
        {
            case NPC_WINTERGRASP_SIEGE_ENGINE_ALLIANCE:
            case NPC_WINTERGRASP_SIEGE_ENGINE_HORDE:
            case NPC_WINTERGRASP_CATAPULT:
            case NPC_WINTERGRASP_DEMOLISHER:
            {
                uint8 team;
                if (creature->GetFaction() == WintergraspFaction[TEAM_ALLIANCE])
                    team = TEAM_ALLIANCE;
                else if (creature->GetFaction() == WintergraspFaction[TEAM_HORDE])
                    team = TEAM_HORDE;
                else
                    return;

                m_vehicles[team].erase(creature->GetGUID());
                if (team == TEAM_HORDE)
                    UpdateData(BATTLEFIELD_WG_DATA_VEHICLE_H, -1);
                else
                    UpdateData(BATTLEFIELD_WG_DATA_VEHICLE_A, -1);
                UpdateVehicleCountWG();

                break;
            }
        }
    }*/
}

void BattlefieldWG::OnGameObjectCreate(GameObject* go)
{
    uint8 workshopId = 0;

    switch (go->GetEntry())
    {
        case GO_WINTERGRASP_FACTORY_BANNER_NE:
            workshopId = BATTLEFIELD_WG_WORKSHOP_NE;
            break;
        case GO_WINTERGRASP_FACTORY_BANNER_NW:
            workshopId = BATTLEFIELD_WG_WORKSHOP_NW;
            break;
        case GO_WINTERGRASP_FACTORY_BANNER_SE:
            workshopId = BATTLEFIELD_WG_WORKSHOP_SE;
            break;
        case GO_WINTERGRASP_FACTORY_BANNER_SW:
            workshopId = BATTLEFIELD_WG_WORKSHOP_SW;
            break;
        default:
            return;
    }

    for (WintergraspWorkshop* workshop : Workshops)
    {
        if (workshop->GetId() == workshopId)
        {
            WintergraspCapturePoint* capturePoint = new WintergraspCapturePoint(this, GetAttackerTeam());

            capturePoint->SetCapturePointData(go);
            capturePoint->LinkToWorkshop(workshop);
            AddCapturePoint(capturePoint);
            break;
        }
    }
}

// Called when player kill a unit in wg zone
void BattlefieldWG::HandleKill(Player* killer, Unit* victim)
{
    if (killer == victim)
        return;

    if (victim->GetTypeId() == TYPEID_PLAYER)
    {
        HandlePromotion(killer, victim);

        // Allow to Skin non-released corpse
        victim->SetUnitFlag(UNIT_FLAG_SKINNABLE);
    }

    /// @todoRecent PvP activity worldstate
}

bool BattlefieldWG::FindAndRemoveVehicleFromList(Unit* vehicle)
{
    for (uint32 team = 0; team < PVP_TEAMS_COUNT; ++team)
    {
        auto itr = m_vehicles[team].find(vehicle->GetGUID());
        if (itr != m_vehicles[team].end())
        {
            m_vehicles[team].erase(itr);

            if (team == TEAM_HORDE)
                UpdateData(BATTLEFIELD_WG_DATA_VEHICLE_H, -1);
            else
                UpdateData(BATTLEFIELD_WG_DATA_VEHICLE_A, -1);
            return true;
        }
    }

    return false;
}

void BattlefieldWG::OnUnitDeath(Unit* unit)
{
    if (IsWarTime())
        if (unit->IsVehicle())
            if (FindAndRemoveVehicleFromList(unit))
                UpdateVehicleCountWG();
}

void BattlefieldWG::HandlePromotion(Player* playerKiller, Unit* unitKilled)
{
    uint32 teamId = playerKiller->GetTeamId();

    for (auto iter = m_PlayersInWar[teamId].begin(); iter != m_PlayersInWar[teamId].end(); ++iter)
        if (Player* player = ObjectAccessor::FindPlayer(*iter))
            if (player->GetDistance2d(unitKilled) < 40.0f)
                PromotePlayer(player);
}

// Update rank for player
void BattlefieldWG::PromotePlayer(Player* killer)
{
    if (!m_isActive)
        return;
    // Updating rank of player
    if (Aura* auraRecruit = killer->GetAura(SPELL_RECRUIT))
    {
        if (auraRecruit->GetStackAmount() >= 5)
        {
            killer->RemoveAura(SPELL_RECRUIT);
            killer->CastSpell(killer, SPELL_CORPORAL, true);
            if (Creature* stalker = GetCreature(StalkerGuid))
                sCreatureTextMgr->SendChat(stalker, BATTLEFIELD_WG_TEXT_RANK_CORPORAL, killer, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_NORMAL, 0, TEAM_OTHER, false, killer);
        }
        else
            killer->CastSpell(killer, SPELL_RECRUIT, true);
    }
    else if (Aura* auraCorporal = killer->GetAura(SPELL_CORPORAL))
    {
        if (auraCorporal->GetStackAmount() >= 5)
        {
            killer->RemoveAura(SPELL_CORPORAL);
            killer->CastSpell(killer, SPELL_LIEUTENANT, true);
            if (Creature* stalker = GetCreature(StalkerGuid))
                sCreatureTextMgr->SendChat(stalker, BATTLEFIELD_WG_TEXT_RANK_FIRST_LIEUTENANT, killer, CHAT_MSG_ADDON, LANG_ADDON, TEXT_RANGE_NORMAL, 0, TEAM_OTHER, false, killer);
        }
        else
            killer->CastSpell(killer, SPELL_CORPORAL, true);
    }
}

void BattlefieldWG::RemoveAurasFromPlayer(Player* player)
{
    player->RemoveAurasDueToSpell(SPELL_RECRUIT);
    player->RemoveAurasDueToSpell(SPELL_CORPORAL);
    player->RemoveAurasDueToSpell(SPELL_LIEUTENANT);
    player->RemoveAurasDueToSpell(SPELL_TOWER_CONTROL);
    player->RemoveAurasDueToSpell(SPELL_SPIRITUAL_IMMUNITY);
    player->RemoveAurasDueToSpell(SPELL_TENACITY);
    player->RemoveAurasDueToSpell(SPELL_ESSENCE_OF_WINTERGRASP);
    player->RemoveAurasDueToSpell(SPELL_WINTERGRASP_RESTRICTED_FLIGHT_AREA);
}

void BattlefieldWG::OnPlayerJoinWar(Player* player)
{
    RemoveAurasFromPlayer(player);
	ApplyWintergraspControlPhase(player);

    player->CastSpell(player, SPELL_RECRUIT, true);

    if (player->GetZoneId() != m_ZoneId)
    {
        if (player->GetTeamId() == GetDefenderTeam())
            player->TeleportTo(571, 5345, 2842, 410, 3.14f);
        else
        {
            if (player->GetTeamId() == TEAM_HORDE)
                player->TeleportTo(571, 5025.857422f, 3674.628906f, 362.737122f, 4.135169f);
            else
                player->TeleportTo(571, 5101.284f, 2186.564f, 373.549f, 3.812f);
        }
    }

    UpdateTenacity();

    if (player->GetTeamId() == GetAttackerTeam())
    {
        if (GetData(BATTLEFIELD_WG_DATA_BROKEN_TOWER_ATT) < 3)
            player->SetAuraStack(SPELL_TOWER_CONTROL, player, 3 - GetData(BATTLEFIELD_WG_DATA_BROKEN_TOWER_ATT));
    }
    else
    {
        if (GetData(BATTLEFIELD_WG_DATA_BROKEN_TOWER_ATT) > 0)
           player->SetAuraStack(SPELL_TOWER_CONTROL, player, GetData(BATTLEFIELD_WG_DATA_BROKEN_TOWER_ATT));
    }
    SendInitWorldStatesTo(player);
}

void BattlefieldWG::OnPlayerLeaveWar(Player* player)
{
	RemovePlayerFromWGResurrectQueue(player->GetGUID());
	player->RemoveAurasDueToSpell(SPELL_SPIRITUAL_IMMUNITY);

    // Remove all aura from WG /// @todo false we can go out of this zone on retail and keep Rank buff, remove on end of WG
    if (!player->GetSession()->PlayerLogout())
    {
        if (Creature* vehicle = player->GetVehicleCreatureBase())   // Remove vehicle of player if he go out.
            vehicle->DespawnOrUnsummon();

        RemoveAurasFromPlayer(player);
    }

    player->RemoveAurasDueToSpell(SPELL_HORDE_CONTROLS_FACTORY_PHASE_SHIFT);
    player->RemoveAurasDueToSpell(SPELL_ALLIANCE_CONTROLS_FACTORY_PHASE_SHIFT);
    player->RemoveAurasDueToSpell(SPELL_HORDE_CONTROL_PHASE_SHIFT);
    player->RemoveAurasDueToSpell(SPELL_ALLIANCE_CONTROL_PHASE_SHIFT);
    UpdateTenacity();
}

void BattlefieldWG::OnPlayerLeaveZone(Player* player)
{
	RemovePlayerFromWGResurrectQueue(player->GetGUID());
	player->RemoveAurasDueToSpell(SPELL_SPIRITUAL_IMMUNITY);

    if (!m_isActive)
        RemoveAurasFromPlayer(player);

    player->RemoveAurasDueToSpell(SPELL_HORDE_CONTROLS_FACTORY_PHASE_SHIFT);
    player->RemoveAurasDueToSpell(SPELL_ALLIANCE_CONTROLS_FACTORY_PHASE_SHIFT);
    player->RemoveAurasDueToSpell(SPELL_HORDE_CONTROL_PHASE_SHIFT);
    player->RemoveAurasDueToSpell(SPELL_ALLIANCE_CONTROL_PHASE_SHIFT);
}

void BattlefieldWG::OnPlayerEnterZone(Player* player)
{
    if (!m_isActive)
        RemoveAurasFromPlayer(player);

    ApplyWintergraspControlPhase(player);

    SendInitWorldStatesTo(player);
}

uint32 BattlefieldWG::GetData(uint32 data) const
{
    switch (data)
    {
        // Used to determine when the phasing spells must be cast
        // See: SpellArea::IsFitToRequirements
        case AREA_THE_SUNKEN_RING:
        case AREA_THE_BROKEN_TEMPLATE:
        case AREA_WESTPARK_WORKSHOP:
        case AREA_EASTPARK_WORKSHOP:
            // Graveyards and Workshops are controlled by the same team.
            if (BfGraveyard const* graveyard = GetGraveyardById(GetSpiritGraveyardId(data)))
                return graveyard->GetControlTeamId();
            break;
        default:
            break;
    }

    return Battlefield::GetData(data);
}

void BattlefieldWG::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(WS_BATTLEFIELD_WG_DEFENDED_A, GetData(BATTLEFIELD_WG_DATA_DEF_A));
    packet.Worldstates.emplace_back(WS_BATTLEFIELD_WG_DEFENDED_H, GetData(BATTLEFIELD_WG_DATA_DEF_H));
    packet.Worldstates.emplace_back(WS_BATTLEFIELD_WG_ATTACKED_A, GetData(BATTLEFIELD_WG_DATA_WON_A));
    packet.Worldstates.emplace_back(WS_BATTLEFIELD_WG_ATTACKED_H, GetData(BATTLEFIELD_WG_DATA_WON_H));
    packet.Worldstates.emplace_back(WS_BATTLEFIELD_WG_ATTACKER, GetAttackerTeam());
    packet.Worldstates.emplace_back(WS_BATTLEFIELD_WG_DEFENDER, GetDefenderTeam());

    // Note: cleanup these two, their names look awkward
    packet.Worldstates.emplace_back(WS_BATTLEFIELD_WG_ACTIVE, IsWarTime() ? 0 : 1);
    packet.Worldstates.emplace_back(WS_BATTLEFIELD_WG_SHOW_WORLDSTATE, IsWarTime() ? 1 : 0);

    for (uint32 itr = 0; itr < 2; ++itr)
        packet.Worldstates.emplace_back(ClockWorldState[itr], int32(GameTime::GetGameTime()) + int32(m_Timer) / int32(1000));

    packet.Worldstates.emplace_back(WS_BATTLEFIELD_WG_VEHICLE_H, GetData(BATTLEFIELD_WG_DATA_VEHICLE_H));
    packet.Worldstates.emplace_back(WS_BATTLEFIELD_WG_MAX_VEHICLE_H, GetData(BATTLEFIELD_WG_DATA_MAX_VEHICLE_H));
    packet.Worldstates.emplace_back(WS_BATTLEFIELD_WG_VEHICLE_A, GetData(BATTLEFIELD_WG_DATA_VEHICLE_A));
    packet.Worldstates.emplace_back(WS_BATTLEFIELD_WG_MAX_VEHICLE_A, GetData(BATTLEFIELD_WG_DATA_MAX_VEHICLE_A));

    for (BfWGGameObjectBuilding* building : BuildingsInZone)
        building->FillInitialWorldStates(packet);

    for (WintergraspWorkshop* workshop : Workshops)
        workshop->FillInitialWorldStates(packet);
}

void BattlefieldWG::SendInitWorldStatesToAll()
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        for (auto itr = m_players[team].begin(); itr != m_players[team].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                SendInitWorldStatesTo(player);
}

void BattlefieldWG::BrokenWallOrTower(TeamId team, BfWGGameObjectBuilding* building)
{
    if (team == GetDefenderTeam())
    {
        for (auto itr = m_PlayersInWar[GetAttackerTeam()].begin(); itr != m_PlayersInWar[GetAttackerTeam()].end(); ++itr)
        {
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                if (player->GetDistance2d(ASSERT_NOTNULL(GetGameObject(building->GetGUID()))) < 50.0f)
                    player->KilledMonsterCredit(QUEST_CREDIT_DEFEND_SIEGE);
        }
    }
}

// Called when a tower is broke
void BattlefieldWG::UpdatedDestroyedTowerCount(TeamId team)
{
    // Southern tower
    if (team == GetAttackerTeam())
    {
        // Update counter
        UpdateData(BATTLEFIELD_WG_DATA_DAMAGED_TOWER_ATT, -1);
        UpdateData(BATTLEFIELD_WG_DATA_BROKEN_TOWER_ATT, 1);

        // Remove buff stack on attackers
        for (auto itr = m_PlayersInWar[GetAttackerTeam()].begin(); itr != m_PlayersInWar[GetAttackerTeam()].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->RemoveAuraFromStack(SPELL_TOWER_CONTROL);

        // Add buff stack to defenders and give achievement/quest credit
        for (auto itr = m_PlayersInWar[GetDefenderTeam()].begin(); itr != m_PlayersInWar[GetDefenderTeam()].end(); ++itr)
        {
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
            {
                player->CastSpell(player, SPELL_TOWER_CONTROL, true);
                player->KilledMonsterCredit(QUEST_CREDIT_TOWERS_DESTROYED);
                DoCompleteOrIncrementAchievement(ACHIEVEMENTS_WG_TOWER_DESTROY, player);
            }
        }

        // If all three south towers are destroyed (ie. all attack towers), remove ten minutes from battle time
        if (GetData(BATTLEFIELD_WG_DATA_BROKEN_TOWER_ATT) == 3)
        {
            if (int32(m_Timer - 600000) < 0)
                m_Timer = 0;
            else
                m_Timer -= 600000;
            SendInitWorldStatesToAll();
        }
    }
    else // Keep tower
    {
        UpdateData(BATTLEFIELD_WG_DATA_DAMAGED_TOWER_DEF, -1);
        UpdateData(BATTLEFIELD_WG_DATA_BROKEN_TOWER_DEF, 1);
    }
}

void BattlefieldWG::ProcessEvent(WorldObject* obj, uint32 eventId)
{
    if (!obj || !IsWarTime())
        return;

    // We handle only gameobjects here
    GameObject* go = obj->ToGameObject();
    if (!go)
        return;

    // On click on titan relic
    if (go->GetEntry() == GO_WINTERGRASP_TITAN_S_RELIC)
    {
        if (CanInteractWithRelic())
            EndBattle(false);
        else if (GameObject* relic = GetRelic())
            relic->SetRespawnTime(RESPAWN_IMMEDIATELY);
    }

    // if destroy or damage event, search the wall/tower and update worldstate/send warning message
    for (BfWGGameObjectBuilding* building : BuildingsInZone)
    {
        if (go->GetGUID() == building->GetGUID())
        {
            if (GameObject* buildingGo = GetGameObject(building->GetGUID()))
            {
                if (buildingGo->GetGOInfo()->building.damagedEvent == eventId)
                    building->Damaged();
                else if (buildingGo->GetGOInfo()->building.destroyedEvent == eventId)
                    building->Destroyed();
                break;
            }
        }
    }
}

// Called when a tower is damaged, used for honor reward calcul
void BattlefieldWG::UpdateDamagedTowerCount(TeamId team)
{
    if (team == GetAttackerTeam())
        UpdateData(BATTLEFIELD_WG_DATA_DAMAGED_TOWER_ATT, 1);
    else
        UpdateData(BATTLEFIELD_WG_DATA_DAMAGED_TOWER_DEF, 1);
}

// Update vehicle count WorldState to player
void BattlefieldWG::UpdateVehicleCountWG()
{
    SendUpdateWorldState(WS_BATTLEFIELD_WG_VEHICLE_H,     GetData(BATTLEFIELD_WG_DATA_VEHICLE_H));
    SendUpdateWorldState(WS_BATTLEFIELD_WG_MAX_VEHICLE_H, GetData(BATTLEFIELD_WG_DATA_MAX_VEHICLE_H));
    SendUpdateWorldState(WS_BATTLEFIELD_WG_VEHICLE_A,     GetData(BATTLEFIELD_WG_DATA_VEHICLE_A));
    SendUpdateWorldState(WS_BATTLEFIELD_WG_MAX_VEHICLE_A, GetData(BATTLEFIELD_WG_DATA_MAX_VEHICLE_A));
}

void BattlefieldWG::UpdateTenacity()
{
    uint32 alliancePlayers = m_PlayersInWar[TEAM_ALLIANCE].size();
	uint32 hordePlayers = m_PlayersInWar[TEAM_HORDE].size();
	int32 newStack = 0;

	if (alliancePlayers && hordePlayers)
	{
		uint32 diff = alliancePlayers > hordePlayers
			? alliancePlayers - hordePlayers
			: hordePlayers - alliancePlayers;

		uint32 stack = diff;

		if (stack > 5)
			stack = 5;

		if (alliancePlayers < hordePlayers)
			newStack = int32(stack);      // 얼라이언스가 적음
		else if (alliancePlayers > hordePlayers)
			newStack = -int32(stack);     // 호드가 적음
	}

    if (newStack == int32(m_tenacityStack))
        return;

    m_tenacityStack = newStack;
    // Remove old buff
    if (m_tenacityTeam != TEAM_NEUTRAL)
    {
        for (auto itr = m_players[m_tenacityTeam].begin(); itr != m_players[m_tenacityTeam].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                if (player->GetLevel() >= m_MinLevel)
                    player->RemoveAurasDueToSpell(SPELL_TENACITY);

        for (auto itr = m_vehicles[m_tenacityTeam].begin(); itr != m_vehicles[m_tenacityTeam].end(); ++itr)
            if (Creature* creature = GetCreature(*itr))
                creature->RemoveAurasDueToSpell(SPELL_TENACITY_VEHICLE);
    }

    // Apply new buff
    if (newStack)
    {
        m_tenacityTeam = newStack > 0 ? TEAM_ALLIANCE : TEAM_HORDE;

        if (newStack < 0)
            newStack = -newStack;
        if (newStack > 20)
            newStack = 20;

        uint32 buff_honor = SPELL_GREATEST_HONOR;
        if (newStack < 15)
            buff_honor = SPELL_GREATER_HONOR;
        if (newStack < 10)
            buff_honor = SPELL_GREAT_HONOR;
        if (newStack < 5)
            buff_honor = 0;

        for (auto itr = m_PlayersInWar[m_tenacityTeam].begin(); itr != m_PlayersInWar[m_tenacityTeam].end(); ++itr)
            if (Player* player = ObjectAccessor::FindPlayer(*itr))
                player->SetAuraStack(SPELL_TENACITY, player, newStack);

        for (auto itr = m_vehicles[m_tenacityTeam].begin(); itr != m_vehicles[m_tenacityTeam].end(); ++itr)
            if (Creature* creature = GetCreature(*itr))
                creature->SetAuraStack(SPELL_TENACITY_VEHICLE, creature, newStack);

        if (buff_honor != 0)
        {
            for (auto itr = m_PlayersInWar[m_tenacityTeam].begin(); itr != m_PlayersInWar[m_tenacityTeam].end(); ++itr)
                if (Player* player = ObjectAccessor::FindPlayer(*itr))
                    player->CastSpell(player, buff_honor, true);

            for (auto itr = m_vehicles[m_tenacityTeam].begin(); itr != m_vehicles[m_tenacityTeam].end(); ++itr)
                if (Creature* creature = GetCreature(*itr))
                    creature->CastSpell(creature, buff_honor, true);
        }
    }
    else
        m_tenacityTeam = TEAM_NEUTRAL;
}

WintergraspCapturePoint::WintergraspCapturePoint(BattlefieldWG* battlefield, TeamId teamInControl) : BfCapturePoint(battlefield)
{
    m_Bf = battlefield;
    m_team = teamInControl;
    m_Workshop = nullptr;
}

void WintergraspCapturePoint::ChangeTeam(TeamId /*oldTeam*/)
{
    ASSERT(m_Workshop);
    m_Workshop->GiveControlTo(m_team);
}

BfGraveyardWG::BfGraveyardWG(BattlefieldWG* battlefield) : BfGraveyard(battlefield)
{
    m_Bf = battlefield;
    m_GossipTextId = 0;
}

void BfGraveyardWG::AddPlayer(ObjectGuid playerGuid)
{
    if (!m_Bf)
        return;

    static_cast<BattlefieldWG*>(m_Bf)->AddPlayerToWGResurrectQueue(playerGuid);
}

void BfGraveyardWG::RemovePlayer(ObjectGuid playerGuid)
{
    if (!m_Bf)
        return;

    static_cast<BattlefieldWG*>(m_Bf)->RemovePlayerFromWGResurrectQueue(playerGuid);
}

void BfGraveyardWG::Resurrect()
{
    if (!m_Bf)
        return;

    TeamId teamId = GetControlTeamId();

    if (teamId != TEAM_ALLIANCE && teamId != TEAM_HORDE)
        return;

    static_cast<BattlefieldWG*>(m_Bf)->ResurrectWGTeamQueue(teamId);
}

void BattlefieldWG::AddPlayerToWGResurrectQueue(ObjectGuid playerGuid)
{
    Player* player = ObjectAccessor::FindPlayer(playerGuid);
    if (!player)
        return;

    TeamId teamId = player->GetTeamId();

    if (teamId != TEAM_ALLIANCE && teamId != TEAM_HORDE)
        return;

    m_WGResurrectQueue[TEAM_ALLIANCE].erase(playerGuid);
    m_WGResurrectQueue[TEAM_HORDE].erase(playerGuid);

    m_WGResurrectQueue[teamId].insert(playerGuid);
}

void BattlefieldWG::RemovePlayerFromWGResurrectQueue(ObjectGuid playerGuid)
{
    m_WGResurrectQueue[TEAM_ALLIANCE].erase(playerGuid);
    m_WGResurrectQueue[TEAM_HORDE].erase(playerGuid);
}

void BattlefieldWG::ResurrectWGTeamQueue(TeamId teamId)
{
    if (teamId != TEAM_ALLIANCE && teamId != TEAM_HORDE)
        return;

    if (m_WGResurrectQueue[teamId].empty())
        return;

    GuidUnorderedSet resurrectList = m_WGResurrectQueue[teamId];
    m_WGResurrectQueue[teamId].clear();

    for (ObjectGuid const& guid : resurrectList)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player)
            continue;

        if (player->IsAlive())
            continue;

        if (player->GetTeamId() != teamId)
            continue;

        player->ResurrectPlayer(1.0f);
        player->CastSpell(player, SPELL_SPIRITUAL_IMMUNITY, true);
        player->SpawnCorpseBones();
    }
}

void BattlefieldWG::ClearWGResurrectQueue()
{
    m_WGResurrectQueue[TEAM_ALLIANCE].clear();
    m_WGResurrectQueue[TEAM_HORDE].clear();
}

BfWGGameObjectBuilding::BfWGGameObjectBuilding(BattlefieldWG* wg, WintergraspGameObjectBuildingType type, uint32 worldState)
{
    ASSERT(wg);

    _wg = wg;
    _teamControl = TEAM_NEUTRAL;
    _type = type;
    _worldState = worldState;
    _state = BATTLEFIELD_WG_OBJECTSTATE_NONE;
    _staticTowerInfo = nullptr;
}

void BfWGGameObjectBuilding::Rebuild()
{
    switch (_type)
    {
        case BATTLEFIELD_WG_OBJECTTYPE_KEEP_TOWER:
        case BATTLEFIELD_WG_OBJECTTYPE_DOOR_LAST:
        case BATTLEFIELD_WG_OBJECTTYPE_DOOR:
        case BATTLEFIELD_WG_OBJECTTYPE_WALL:
            _teamControl = _wg->GetDefenderTeam();      // Objects that are part of the keep should be the defender's
            break;
        case BATTLEFIELD_WG_OBJECTTYPE_TOWER:
            _teamControl = _wg->GetAttackerTeam();      // The towers in the south should be the attacker's
            break;
        default:
            _teamControl = TEAM_NEUTRAL;
            break;
    }

    if (GameObject* build = _wg->GetGameObject(_buildGUID))
    {
        // Rebuild gameobject
        if (build->IsDestructibleBuilding())
        {
            build->SetDestructibleState(GO_DESTRUCTIBLE_REBUILDING, nullptr, true);
            if (build->GetEntry() == GO_WINTERGRASP_VAULT_GATE)
                if (GameObject* go = build->FindNearestGameObject(GO_WINTERGRASP_KEEP_COLLISION_WALL, 50.0f))
                    go->SetGoState(GO_STATE_ACTIVE);

            // Update worldstate
            _state = WintergraspGameObjectState(BATTLEFIELD_WG_OBJECTSTATE_ALLIANCE_INTACT - (_teamControl * 3));
            _wg->SendUpdateWorldState(_worldState, _state);
        }
        UpdateCreatureAndGo();
        build->SetFaction(WintergraspFaction[_teamControl]);
    }
}

void BfWGGameObjectBuilding::RebuildGate()
{
    if (GameObject* build = _wg->GetGameObject(_buildGUID))
    {
        if (build->IsDestructibleBuilding() && build->GetEntry() == GO_WINTERGRASP_VAULT_GATE)
        {
            if (GameObject* go = build->FindNearestGameObject(GO_WINTERGRASP_KEEP_COLLISION_WALL, 50.0f))
                go->SetGoState(GO_STATE_READY); //not GO_STATE_ACTIVE
        }
    }
}

void BfWGGameObjectBuilding::Damaged()
{
    // Update worldstate
    _state = WintergraspGameObjectState(BATTLEFIELD_WG_OBJECTSTATE_ALLIANCE_DAMAGE - (_teamControl * 3));
    _wg->SendUpdateWorldState(_worldState, _state);

	uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
	if (realmId == 1)
		SendWGDiscordEvent(
			"building_damaged_" + std::to_string(_buildGUID.GetEntry()),
			"⚠️ 방어 시설 피해 발생",
			GetWGBuildingName(_buildGUID.GetEntry()) + "이 공격받고 있습니다.\n"
			"전황이 흔들리기 시작했습니다.",
			60
		);

    // Send warning message
    if (_staticTowerInfo)
        _wg->SendWarning(_staticTowerInfo->TextIds.Damaged);

    for (ObjectGuid guid : m_CreatureTopList[_wg->GetAttackerTeam()])
        if (Creature* creature = _wg->GetCreature(guid))
            _wg->HideNpc(creature);

    for (ObjectGuid guid : m_TurretTopList)
        if (Creature* creature = _wg->GetCreature(guid))
            _wg->HideNpc(creature);

    if (_type == BATTLEFIELD_WG_OBJECTTYPE_KEEP_TOWER)
        _wg->UpdateDamagedTowerCount(_wg->GetDefenderTeam());
    else if (_type == BATTLEFIELD_WG_OBJECTTYPE_TOWER)
        _wg->UpdateDamagedTowerCount(_wg->GetAttackerTeam());
}

void BfWGGameObjectBuilding::Destroyed()
{
    // Update worldstate
    _state = WintergraspGameObjectState(BATTLEFIELD_WG_OBJECTSTATE_ALLIANCE_DESTROY - (_teamControl * 3));
    _wg->SendUpdateWorldState(_worldState, _state);

	uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
	if (realmId == 1)
		SendWGDiscordEvent(
			"building_destroyed_" + std::to_string(_buildGUID.GetEntry()),
			"💥 방어 시설 파괴",
			GetWGBuildingName(_buildGUID.GetEntry()) + "이 무너졌습니다.\n"
			"전투의 흐름이 크게 바뀌고 있습니다.",
			5
		);

    // Warn players
    if (_staticTowerInfo)
        _wg->SendWarning(_staticTowerInfo->TextIds.Destroyed);

	for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
	{
		for (ObjectGuid guid : m_CreatureBottomList[team])
			if (Creature* creature = _wg->GetCreature(guid))
				_wg->HideNpc(creature);
	}

	for (ObjectGuid guid : m_TowerCannonBottomList)
	{
		if (Creature* creature = _wg->GetCreature(guid))
			_wg->HideNpc(creature);
	}

	for (ObjectGuid guid : m_TurretTopList)
	{
		if (Creature* creature = _wg->GetCreature(guid))
			_wg->HideNpc(creature);
	}

    switch (_type)
    {
        // Inform the global wintergrasp script of the destruction of this object
        case BATTLEFIELD_WG_OBJECTTYPE_TOWER:
        case BATTLEFIELD_WG_OBJECTTYPE_KEEP_TOWER:
            _wg->UpdatedDestroyedTowerCount(_teamControl);
            break;
        case BATTLEFIELD_WG_OBJECTTYPE_DOOR_LAST:
		{
			uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
			if (realmId == 1)
				SendWGDiscordEvent(
					"relic_opened",
					"🚨 티탄 유물 노출",
					"겨울손아귀 최후의 관문이 돌파되었습니다.\n"
					"공격팀이 티탄 유물을 노리고 있습니다.",
					5
				);

            if (GameObject* build = _wg->GetGameObject(_buildGUID))
                if (GameObject* go = build->FindNearestGameObject(GO_WINTERGRASP_KEEP_COLLISION_WALL, 50.0f))
                    go->SetGoState(GO_STATE_ACTIVE);
            _wg->SetRelicInteractible(true);
            if (_wg->GetRelic())
                _wg->GetRelic()->RemoveFlag(GO_FLAG_IN_USE | GO_FLAG_NOT_SELECTABLE);
            else
                TC_LOG_ERROR("bg.battlefield.wg", "Titan Relic not found.");
            break;
		}
        default:
            break;
    }

    _wg->BrokenWallOrTower(_teamControl, this);
}

void BfWGGameObjectBuilding::Init(GameObject* go)
{
    if (!go)
        return;

    // GameObject associated to object
    _buildGUID = go->GetGUID();

    switch (_type)
    {
        case BATTLEFIELD_WG_OBJECTTYPE_KEEP_TOWER:
        case BATTLEFIELD_WG_OBJECTTYPE_DOOR_LAST:
        case BATTLEFIELD_WG_OBJECTTYPE_DOOR:
        case BATTLEFIELD_WG_OBJECTTYPE_WALL:
            _teamControl = _wg->GetDefenderTeam();           // Objects that are part of the keep should be the defender's
            break;
        case BATTLEFIELD_WG_OBJECTTYPE_TOWER:
            _teamControl = _wg->GetAttackerTeam();           // The towers in the south should be the attacker's
            break;
        default:
            _teamControl = TEAM_NEUTRAL;
            break;
    }

    _state = WintergraspGameObjectState(sWorld->getWorldState(_worldState));
    switch (_state)
    {
        case BATTLEFIELD_WG_OBJECTSTATE_NEUTRAL_INTACT:
        case BATTLEFIELD_WG_OBJECTSTATE_ALLIANCE_INTACT:
        case BATTLEFIELD_WG_OBJECTSTATE_HORDE_INTACT:
            go->SetDestructibleState(GO_DESTRUCTIBLE_REBUILDING, nullptr, true);
            break;
        case BATTLEFIELD_WG_OBJECTSTATE_NEUTRAL_DESTROY:
        case BATTLEFIELD_WG_OBJECTSTATE_ALLIANCE_DESTROY:
        case BATTLEFIELD_WG_OBJECTSTATE_HORDE_DESTROY:
            go->SetDestructibleState(GO_DESTRUCTIBLE_DESTROYED);
            break;
        case BATTLEFIELD_WG_OBJECTSTATE_NEUTRAL_DAMAGE:
        case BATTLEFIELD_WG_OBJECTSTATE_ALLIANCE_DAMAGE:
        case BATTLEFIELD_WG_OBJECTSTATE_HORDE_DAMAGE:
            go->SetDestructibleState(GO_DESTRUCTIBLE_DAMAGED);
            break;
        default:
            break;
    }

    int32 towerId = -1;
    switch (go->GetEntry())
    {
        case GO_WINTERGRASP_FORTRESS_TOWER_1:
            towerId = BATTLEFIELD_WG_TOWER_FORTRESS_NW;
            break;
        case GO_WINTERGRASP_FORTRESS_TOWER_2:
            towerId = BATTLEFIELD_WG_TOWER_FORTRESS_SW;
            break;
        case GO_WINTERGRASP_FORTRESS_TOWER_3:
            towerId = BATTLEFIELD_WG_TOWER_FORTRESS_SE;
            break;
        case GO_WINTERGRASP_FORTRESS_TOWER_4:
            towerId = BATTLEFIELD_WG_TOWER_FORTRESS_NE;
            break;
        case GO_WINTERGRASP_SHADOWSIGHT_TOWER:
            towerId = BATTLEFIELD_WG_TOWER_SHADOWSIGHT;
            break;
        case GO_WINTERGRASP_WINTER_S_EDGE_TOWER:
            towerId = BATTLEFIELD_WG_TOWER_WINTER_S_EDGE;
            break;
        case GO_WINTERGRASP_FLAMEWATCH_TOWER:
            towerId = BATTLEFIELD_WG_TOWER_FLAMEWATCH;
            break;
    }

    if (towerId >= BATTLEFIELD_WG_TOWER_SHADOWSIGHT) // Attacker towers
    {
        // Spawn associate gameobjects
        for (WintergraspGameObjectData const& gobData : AttackTowers[towerId - 4].GameObject)
        {
            if (GameObject* goHorde = _wg->SpawnGameObject(gobData.HordeEntry, gobData.Pos, gobData.Rot))
                m_GameObjectList[TEAM_HORDE].push_back(goHorde->GetGUID());

            if (GameObject* goAlliance = _wg->SpawnGameObject(gobData.AllianceEntry, gobData.Pos, gobData.Rot))
                m_GameObjectList[TEAM_ALLIANCE].push_back(goAlliance->GetGUID());
        }

        // Spawn associate npc bottom
        for (WintergraspObjectPositionData const& creatureData : AttackTowers[towerId - 4].CreatureBottom)
        {
            if (Creature* creature = _wg->SpawnCreature(creatureData.HordeEntry, creatureData.Pos))
                m_CreatureBottomList[TEAM_HORDE].push_back(creature->GetGUID());

            if (Creature* creature = _wg->SpawnCreature(creatureData.AllianceEntry, creatureData.Pos))
                m_CreatureBottomList[TEAM_ALLIANCE].push_back(creature->GetGUID());
        }
    }

    if (towerId >= 0)
    {
        ASSERT(towerId < WG_MAX_TOWER);
        _staticTowerInfo = &TowerData[towerId];

        // Spawn Turret bottom
        for (Position const& turretPos : TowerCannon[towerId].TowerCannonBottom)
        {
            if (Creature* turret = _wg->SpawnCreature(NPC_WINTERGRASP_TOWER_CANNON, turretPos))
            {
                m_TowerCannonBottomList.push_back(turret->GetGUID());
                switch (go->GetEntry())
                {
                    case GO_WINTERGRASP_FORTRESS_TOWER_1:
                    case GO_WINTERGRASP_FORTRESS_TOWER_2:
                    case GO_WINTERGRASP_FORTRESS_TOWER_3:
                    case GO_WINTERGRASP_FORTRESS_TOWER_4:
                        turret->SetFaction(WintergraspFaction[_wg->GetDefenderTeam()]);
                        break;
                    case GO_WINTERGRASP_SHADOWSIGHT_TOWER:
                    case GO_WINTERGRASP_WINTER_S_EDGE_TOWER:
                    case GO_WINTERGRASP_FLAMEWATCH_TOWER:
                        turret->SetFaction(WintergraspFaction[_wg->GetAttackerTeam()]);
                        break;
                }

                _wg->HideNpc(turret);
            }
        }

        // Spawn Turret top
        for (Position const& towerCannonPos : TowerCannon[towerId].TurretTop)
        {
            if (Creature* turret = _wg->SpawnCreature(NPC_WINTERGRASP_TOWER_CANNON, towerCannonPos))
            {
                m_TurretTopList.push_back(turret->GetGUID());
                switch (go->GetEntry())
                {
                    case GO_WINTERGRASP_FORTRESS_TOWER_1:
                    case GO_WINTERGRASP_FORTRESS_TOWER_2:
                    case GO_WINTERGRASP_FORTRESS_TOWER_3:
                    case GO_WINTERGRASP_FORTRESS_TOWER_4:
                        turret->SetFaction(WintergraspFaction[_wg->GetDefenderTeam()]);
                        break;
                    case GO_WINTERGRASP_SHADOWSIGHT_TOWER:
                    case GO_WINTERGRASP_WINTER_S_EDGE_TOWER:
                    case GO_WINTERGRASP_FLAMEWATCH_TOWER:
                        turret->SetFaction(WintergraspFaction[_wg->GetAttackerTeam()]);
                        break;
                }
                _wg->HideNpc(turret);
            }
        }
        UpdateCreatureAndGo();
    }
}

void BfWGGameObjectBuilding::UpdateCreatureAndGo()
{
    for (ObjectGuid guid : m_CreatureTopList[_wg->GetDefenderTeam()])
        if (Creature* creature = _wg->GetCreature(guid))
            _wg->HideNpc(creature);

    for (ObjectGuid guid : m_CreatureTopList[_wg->GetAttackerTeam()])
        if (Creature* creature = _wg->GetCreature(guid))
            _wg->ShowNpc(creature, true);

    for (ObjectGuid guid : m_CreatureBottomList[_wg->GetDefenderTeam()])
        if (Creature* creature = _wg->GetCreature(guid))
            _wg->HideNpc(creature);

    for (ObjectGuid guid : m_CreatureBottomList[_wg->GetAttackerTeam()])
        if (Creature* creature = _wg->GetCreature(guid))
            _wg->ShowNpc(creature, true);

    for (ObjectGuid guid : m_GameObjectList[_wg->GetDefenderTeam()])
        if (GameObject* go = _wg->GetGameObject(guid))
            go->SetRespawnTime(RESPAWN_ONE_DAY);

    for (ObjectGuid guid : m_GameObjectList[_wg->GetAttackerTeam()])
        if (GameObject* go = _wg->GetGameObject(guid))
            go->SetRespawnTime(RESPAWN_IMMEDIATELY);
}

void BfWGGameObjectBuilding::UpdateTurretAttack(bool disable)
{
    for (ObjectGuid guid : m_TowerCannonBottomList)
    {
        if (Creature* creature = _wg->GetCreature(guid))
        {
            if (disable)
                _wg->HideNpc(creature);
            else
                _wg->ShowNpc(creature, true);

            switch (_buildGUID.GetEntry())
            {
                case GO_WINTERGRASP_FORTRESS_TOWER_1:
                case GO_WINTERGRASP_FORTRESS_TOWER_2:
                case GO_WINTERGRASP_FORTRESS_TOWER_3:
                case GO_WINTERGRASP_FORTRESS_TOWER_4:
                {
                    creature->SetFaction(WintergraspFaction[_wg->GetDefenderTeam()]);
                    break;
                }
                case GO_WINTERGRASP_SHADOWSIGHT_TOWER:
                case GO_WINTERGRASP_WINTER_S_EDGE_TOWER:
                case GO_WINTERGRASP_FLAMEWATCH_TOWER:
                {
                    creature->SetFaction(WintergraspFaction[_wg->GetAttackerTeam()]);
                    break;
                }
            }
        }
    }

    for (ObjectGuid guid : m_TurretTopList)
    {
        if (Creature* creature = _wg->GetCreature(guid))
        {
            if (disable)
                _wg->HideNpc(creature);
            else
                _wg->ShowNpc(creature, true);

            switch (_buildGUID.GetEntry())
            {
                case GO_WINTERGRASP_FORTRESS_TOWER_1:
                case GO_WINTERGRASP_FORTRESS_TOWER_2:
                case GO_WINTERGRASP_FORTRESS_TOWER_3:
                case GO_WINTERGRASP_FORTRESS_TOWER_4:
                {
                    creature->SetFaction(WintergraspFaction[_wg->GetDefenderTeam()]);
                    break;
                }
                case GO_WINTERGRASP_SHADOWSIGHT_TOWER:
                case GO_WINTERGRASP_WINTER_S_EDGE_TOWER:
                case GO_WINTERGRASP_FLAMEWATCH_TOWER:
                {
                    creature->SetFaction(WintergraspFaction[_wg->GetAttackerTeam()]);
                    break;
                }
            }
        }
    }
}

void BfWGGameObjectBuilding::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(_worldState, _state);
}

void BfWGGameObjectBuilding::Save()
{
    sWorld->setWorldState(_worldState, _state);
}

WintergraspWorkshop::WintergraspWorkshop(BattlefieldWG* wg, uint8 type)
{
    ASSERT(wg && type < WG_MAX_WORKSHOP);

    _wg = wg;
    _state = BATTLEFIELD_WG_OBJECTSTATE_NONE;
    _teamControl = TEAM_NEUTRAL;
    _staticInfo = &WorkshopData[type];
}

uint8 WintergraspWorkshop::GetId() const
{
    return _staticInfo->WorkshopId;
}

void WintergraspWorkshop::GiveControlTo(TeamId teamId, bool init /*= false*/)
{
    switch (teamId)
    {
        case TEAM_NEUTRAL:
        {
            // Send warning message to all player for inform a faction attack a workshop
            // alliance / horde attacking workshop
            _wg->SendWarning(_teamControl == TEAM_ALLIANCE ? _staticInfo->TextIds.HordeAttack : _staticInfo->TextIds.AllianceAttack);

			if (!init && !IsWintergraspKeepWorkshop(_staticInfo->WorkshopId))
			{
				if (_teamControl == TEAM_ALLIANCE || _teamControl == TEAM_HORDE)
				{
					TeamId attackTeam = _teamControl == TEAM_ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE;

					uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
					if (realmId == 1)
						SendWGDiscordEvent(
							"workshop_attack_" + std::to_string(_staticInfo->WorkshopId),
							"🔥 작업장 교전 발생",
							GetWGWorkshopName(_staticInfo->WorkshopId) + "이 " + GetWGTeamName(attackTeam) + "의 공격을 받고 있습니다.\n"
							"이 거점을 잃으면 차량 생산력이 흔들릴 수 있습니다.",
							60
						);
				}
			}

            break;
        }
        case TEAM_ALLIANCE:
        {
            // Updating worldstate
            _state = BATTLEFIELD_WG_OBJECTSTATE_ALLIANCE_INTACT;
            _wg->SendUpdateWorldState(_staticInfo->WorldStateId, _state);

            // Warning message
            if (!init)
                _wg->SendWarning(_staticInfo->TextIds.AllianceCapture); // workshop taken - alliance

            // Found associate graveyard and update it
            GiveWintergraspWorkshopGraveyardControl(_wg, _staticInfo->WorkshopId, TEAM_ALLIANCE);

            _teamControl = teamId;

			if (!init && !IsWintergraspKeepWorkshop(_staticInfo->WorkshopId))
			{
				uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
				if (realmId == 1)
						SendWGDiscordEvent(
							"workshop_capture_" + std::to_string(_staticInfo->WorkshopId),
							"🏗️ 작업장 점령",
							GetWGWorkshopName(_staticInfo->WorkshopId) + "을 얼라이언스가 장악했습니다.\n"
							"얼라이언스의 차량 생산력이 강화됩니다.",
							5
						);
			}
            break;
        }
        case TEAM_HORDE:
        {
            // Update worldstate
            _state = BATTLEFIELD_WG_OBJECTSTATE_HORDE_INTACT;
            _wg->SendUpdateWorldState(_staticInfo->WorldStateId, _state);

            // Warning message
            if (!init)
                _wg->SendWarning(_staticInfo->TextIds.HordeCapture); // workshop taken - horde

            // Update graveyard control
            GiveWintergraspWorkshopGraveyardControl(_wg, _staticInfo->WorkshopId, TEAM_HORDE);

            _teamControl = teamId;

			if (!init && !IsWintergraspKeepWorkshop(_staticInfo->WorkshopId))
			{
				uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
				if (realmId == 1)
						SendWGDiscordEvent(
							"workshop_capture_" + std::to_string(_staticInfo->WorkshopId),
							"🏗️ 작업장 점령",
							GetWGWorkshopName(_staticInfo->WorkshopId) + "을 호드가 장악했습니다.\n"
							"호드의 차량 생산력이 강화됩니다.",
							5
						);
			}
            break;
        }
    }
    
	if (teamId == TEAM_ALLIANCE || teamId == TEAM_HORDE)
        UpdateWintergraspControlledGuardGroup(_wg, WG_CONTROL_TYPE_WORKSHOP, _staticInfo->WorkshopId, _teamControl);

    if (!init)
        _wg->UpdateCounterVehicle(false);
}

void WintergraspWorkshop::UpdateGraveyardAndWorkshop()
{
    if (IsWintergraspKeepWorkshop(_staticInfo->WorkshopId))
        GiveControlTo(_wg->GetDefenderTeam(), true);
    else
        GiveControlTo(_wg->GetAttackerTeam(), true);
}

void WintergraspWorkshop::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(_staticInfo->WorldStateId, _state);
}

void WintergraspWorkshop::Save()
{
    sWorld->setWorldState(_staticInfo->WorldStateId, _state);
}

class Battlefield_wintergrasp : public BattlefieldScript
{
public:
    Battlefield_wintergrasp() : BattlefieldScript("battlefield_wg") { }

    Battlefield* GetBattlefield() const override
    {
        return new BattlefieldWG();
    }
};

enum CustomWintergraspTowerCannon
{
    SPELL_WG_TOWER_CANNON_FIRE = 51421,

    WG_TOWER_CANNON_MIN_RANGE = 10,
    WG_TOWER_CANNON_MAX_RANGE = 70,

    WG_TOWER_CANNON_SEARCH_TIMER = 1000,

    // 자동 포탑이므로 유저 수동 발사보다 느리게
    WG_TOWER_CANNON_FIRE_TIMER = 8000
};

float const WG_TOWER_CANNON_PI = 3.14159265f;
float const WG_TOWER_CANNON_TWO_PI = 6.28318530f;

// 좌우 90도 = 전방 180도
float const WG_TOWER_CANNON_FRONT_HALF_ARC = 1.57079632f;

class npc_wg_tower_cannon_ai : public CreatureScript
{
public:
    npc_wg_tower_cannon_ai() : CreatureScript("npc_wg_tower_cannon_ai") { }

    struct npc_wg_tower_cannon_aiAI : public ScriptedAI
    {
        npc_wg_tower_cannon_aiAI(Creature* creature) : ScriptedAI(creature) { }

        uint32 SearchTimer = WG_TOWER_CANNON_SEARCH_TIMER;
        uint32 FireTimer = WG_TOWER_CANNON_FIRE_TIMER;

        ObjectGuid CurrentTargetGuid = ObjectGuid::Empty;

        float BaseOrientation = 0.0f;
        bool BaseOrientationSaved = false;

        bool IsWintergraspWarTime() const
        {
            Battlefield* wintergrasp = sBattlefieldMgr->GetBattlefieldByBattleId(BATTLEFIELD_BATTLEID_WG);
            if (!wintergrasp)
                return false;

            return wintergrasp->IsWarTime();
        }

        float NormalizeAngle(float angle) const
        {
            while (angle < 0.0f)
                angle += WG_TOWER_CANNON_TWO_PI;

            while (angle >= WG_TOWER_CANNON_TWO_PI)
                angle -= WG_TOWER_CANNON_TWO_PI;

            return angle;
        }

        void SaveBaseOrientation()
        {
            if (BaseOrientationSaved)
                return;

            BaseOrientation = NormalizeAngle(me->GetOrientation());
            BaseOrientationSaved = true;
        }

        float GetAngleToTarget(Unit* target) const
        {
            return std::atan2(
                target->GetPositionY() - me->GetPositionY(),
                target->GetPositionX() - me->GetPositionX()
            );
        }

        bool IsTargetInFrontArc(Unit* target)
        {
            if (!target)
                return false;

            SaveBaseOrientation();

            float targetAngle = NormalizeAngle(GetAngleToTarget(target));
            float diff = std::fabs(targetAngle - BaseOrientation);

            if (diff > WG_TOWER_CANNON_PI)
                diff = WG_TOWER_CANNON_TWO_PI - diff;

            return diff <= WG_TOWER_CANNON_FRONT_HALF_ARC;
        }

        void FaceTarget(Unit* target)
        {
            if (!target)
                return;

            me->SetFacingTo(GetAngleToTarget(target));
        }

        void ClearCurrentTarget()
        {
            CurrentTargetGuid = ObjectGuid::Empty;
            me->AttackStop();
            me->SetTarget(ObjectGuid::Empty);
        }

        bool CanAttackTarget(Unit* target)
        {
            if (!target)
                return false;

            if (!target->IsAlive())
                return false;

            if (!me->IsValidAttackTarget(target))
                return false;

            float distance = me->GetDistance(target);

            if (distance < float(WG_TOWER_CANNON_MIN_RANGE))
                return false;

            if (distance > float(WG_TOWER_CANNON_MAX_RANGE))
                return false;

            if (!IsTargetInFrontArc(target))
                return false;

            return true;
        }

        Unit* SelectCannonTarget()
        {
            std::list<Unit*> targets;

            Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(me, me, float(WG_TOWER_CANNON_MAX_RANGE));
            Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(me, targets, check);
            Cell::VisitAllObjects(me, searcher, float(WG_TOWER_CANNON_MAX_RANGE));

            targets.remove_if([this](Unit* target)
            {
                return !CanAttackTarget(target);
            });

            if (targets.empty())
                return nullptr;

            targets.sort(Trinity::ObjectDistanceOrderPred(me));
            return targets.front();
        }

        Unit* GetCurrentTarget()
        {
            if (CurrentTargetGuid.IsEmpty())
                return nullptr;

            return ObjectAccessor::GetUnit(*me, CurrentTargetGuid);
        }

        void Reset() override
        {
            SearchTimer = WG_TOWER_CANNON_SEARCH_TIMER;
            FireTimer = WG_TOWER_CANNON_FIRE_TIMER;

            CurrentTargetGuid = ObjectGuid::Empty;

            SaveBaseOrientation();

            me->SetReactState(REACT_PASSIVE);
            me->AttackStop();
            me->SetTarget(ObjectGuid::Empty);
        }

        void AttackStart(Unit* /*target*/) override
        {
        }

        void UpdateAI(uint32 diff) override
        {
            if (!IsWintergraspWarTime())
            {
                ClearCurrentTarget();
                return;
            }

            if (!me->IsAlive())
                return;

            Unit* target = GetCurrentTarget();

            if (!CanAttackTarget(target))
            {
                target = nullptr;
                CurrentTargetGuid = ObjectGuid::Empty;

                if (SearchTimer <= diff)
                {
                    SearchTimer = WG_TOWER_CANNON_SEARCH_TIMER;

                    target = SelectCannonTarget();
                    if (target)
                    {
                        CurrentTargetGuid = target->GetGUID();
                        me->SetTarget(CurrentTargetGuid);
                        FaceTarget(target);
                    }
                    else
                    {
                        me->SetTarget(ObjectGuid::Empty);
                    }
                }
                else
                {
                    SearchTimer -= diff;
                }

                return;
            }

            me->SetTarget(CurrentTargetGuid);
            FaceTarget(target);

            if (FireTimer <= diff)
            {
                FireTimer = WG_TOWER_CANNON_FIRE_TIMER;

                DoCast(target, SPELL_WG_TOWER_CANNON_FIRE, true);
            }
            else
            {
                FireTimer -= diff;
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_wg_tower_cannon_aiAI(creature);
    }
};

class npc_wg_give_promotion_credit : public CreatureScript
{
public:
    npc_wg_give_promotion_credit() : CreatureScript("npc_wg_give_promotion_credit") { }

    struct npc_wg_give_promotion_creditAI : public ScriptedAI
    {
        npc_wg_give_promotion_creditAI(Creature* creature) : ScriptedAI(creature) { }

        void JustDied(Unit* killer) override
        {
            if (!killer || killer->GetTypeId() != TYPEID_PLAYER)
                return;

            BattlefieldWG* wintergrasp = static_cast<BattlefieldWG*>(sBattlefieldMgr->GetBattlefieldByBattleId(BATTLEFIELD_BATTLEID_WG));
            if (!wintergrasp)
                return;

            wintergrasp->HandlePromotion(killer->ToPlayer(), me);
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_wg_give_promotion_creditAI(creature);
    }
};

void BattlefieldWG::ApplyWintergraspControlPhase(Player* player)
{
    if (!player)
        return;

    player->RemoveAurasDueToSpell(SPELL_HORDE_CONTROL_PHASE_SHIFT);
    player->RemoveAurasDueToSpell(SPELL_ALLIANCE_CONTROL_PHASE_SHIFT);

    player->AddAura(
        GetDefenderTeam() == TEAM_HORDE
            ? SPELL_HORDE_CONTROL_PHASE_SHIFT
            : SPELL_ALLIANCE_CONTROL_PHASE_SHIFT,
        player
    );
}

void BattlefieldWG::ApplyWintergraspControlPhaseToAll()
{
    for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
    {
        for (ObjectGuid const& guid : m_players[team])
        {
            if (Player* player = ObjectAccessor::FindPlayer(guid))
                ApplyWintergraspControlPhase(player);
        }
    }
}

void BattlefieldWG::HideTrashMobs()
{
    for (auto const& guid : TrashMobList)
    {
        if (Creature* creature = GetCreature(guid))
            HideNpc(creature);
    }
}

void BattlefieldWG::ShowTrashMobs()
{
    for (auto const& guid : TrashMobList)
    {
        if (Creature* creature = GetCreature(guid))
            ShowNpc(creature, true);
    }
}

//전차 AI 관련
void BattlefieldWG::SpawnSingleEliteDemolisher()
{
    if (!IsWarTime())
        return;

    uint32 spawnedCount = 0;

    for (uint8 workshopId : WGWorkshopSiegeWorkshopIds)
    {
        if (workshopId >= WG_MAX_WORKSHOP)
            continue;

        WintergraspWorkshop* targetWorkshop = nullptr;

        for (WintergraspWorkshop* workshop : Workshops)
        {
            if (!workshop)
                continue;

            if (workshop->GetId() == workshopId)
            {
                targetWorkshop = workshop;
                break;
            }
        }

        if (!targetWorkshop)
            continue;

        // 공격팀이 가진 외부 작업장에서만 생산한다.
        // 방어팀에게 넘어간 작업장은 기존 전차는 유지하되, 새 생산만 중지한다.
        if (targetWorkshop->GetTeamControl() != GetAttackerTeam())
            continue;

        WGEliteDemolisherPathData const* path = GetEliteDemolisherPathByWorkshopId(workshopId);
        if (!path || !path->Points || path->PointCount < 2)
            continue;

        std::vector<ObjectGuid>& siegeList = WGWorkshopSiegeGuids[workshopId];

        for (auto itr = siegeList.begin(); itr != siegeList.end();)
        {
            Creature* creature = GetCreature(*itr);

            if (!creature || !creature->IsAlive())
            {
                if (creature)
                    creature->DespawnOrUnsummon();

                itr = siegeList.erase(itr);
                continue;
            }

            ++itr;
        }

        if (siegeList.size() >= WG_WORKSHOP_SIEGE_MAX_ALIVE)
            continue;

        Position const& spawnPos = path->Points[0];

        Creature* elite = SpawnCreature(NPC_WG_CUSTOM_SIEGE_ELITE_DEMOLISHER, spawnPos);
        if (!elite)
            continue;

        elite->SetFaction(WintergraspFaction[GetAttackerTeam()]);
        elite->SetReactState(REACT_AGGRESSIVE);
        elite->SetRespawnTime(0);
        elite->SetHomePosition(spawnPos);

        if (elite->AI())
        {
            elite->AI()->SetData(WG_ELITE_DEMOLISHER_DATA_WORKSHOP_ID, workshopId);
            elite->AI()->Reset();
        }

        siegeList.push_back(elite->GetGUID());
        ++spawnedCount;

        TC_LOG_INFO(
            "bg.battlefield",
            "WG workshop siege demolisher spawned. workshopId={}, attackerTeam={}, faction={}, guid={}",
            uint32(workshopId),
            uint32(GetAttackerTeam()),
            elite->GetFaction(),
            elite->GetGUID().ToString()
        );
    }

    if (spawnedCount > 0 && !WGWorkshopSiegeFirstWaveAnnounced)
    {
        WGWorkshopSiegeFirstWaveAnnounced = true;
		SendWarning(BATTLEFIELD_WG_TEXT_SIEGE_WAVE_START);
    }
}

void BattlefieldWG::RemoveSingleEliteDemolisher()
{
    WGWorkshopSiegeWaveEnabled = false;
    WGWorkshopSiegeWaveTimer = 0;
    WGWorkshopSiegeFirstWaveAnnounced = false;

    for (uint8 i = 0; i < WG_MAX_WORKSHOP; ++i)
    {
        for (ObjectGuid const& guid : WGWorkshopSiegeGuids[i])
        {
            if (Creature* creature = GetCreature(guid))
                creature->DespawnOrUnsummon();
        }

        WGWorkshopSiegeGuids[i].clear();
    }
}

class npc_wg_elite_demolisher_ai : public CreatureScript
{
public:
    npc_wg_elite_demolisher_ai() : CreatureScript("npc_wg_elite_demolisher_ai") { }

    struct npc_wg_elite_demolisher_aiAI : public ScriptedAI
    {
        npc_wg_elite_demolisher_aiAI(Creature* creature) : ScriptedAI(creature) { }

        uint32 SearchTimer = WG_ELITE_DEMOLISHER_SEARCH_TIMER;
        uint32 RangedSpellTimer = 1000;
        uint32 MeleeSpellTimer = 1000;

        ObjectGuid CurrentTargetGuid = ObjectGuid::Empty;
        ObjectGuid CurrentGameObjectTargetGuid = ObjectGuid::Empty;
        uint8 CurrentTargetType = WG_ELITE_TARGET_NONE;

        WGEliteDemolisherPathData const* CurrentPath = nullptr;
        uint32 CurrentPathPoint = 0;
        bool IsMovingPath = false;
        bool PathFinished = false;
        uint8 WorkshopId = BATTLEFIELD_WG_WORKSHOP_NE;

        void SetData(uint32 type, uint32 data) override
        {
            if (type != WG_ELITE_DEMOLISHER_DATA_WORKSHOP_ID)
                return;

            WorkshopId = uint8(data);
        }

        bool IsWintergraspWarTime() const
        {
            Battlefield* wintergrasp = sBattlefieldMgr->GetBattlefieldByBattleId(BATTLEFIELD_BATTLEID_WG);
            if (!wintergrasp)
                return false;

            return wintergrasp->IsWarTime();
        }

        static bool IsWintergraspVehicleEntry(uint32 entry)
        {
            switch (entry)
            {
                case NPC_WINTERGRASP_CATAPULT:
                case NPC_WINTERGRASP_DEMOLISHER:
                case NPC_WINTERGRASP_SIEGE_ENGINE_ALLIANCE:
                case NPC_WINTERGRASP_SIEGE_ENGINE_HORDE:
                case NPC_WG_CUSTOM_SIEGE_ELITE_DEMOLISHER:
                    return true;
                default:
                    break;
            }

            return false;
        }

        TeamId GetWintergraspUnitTeam(Unit* unit) const
        {
            if (!unit)
                return TEAM_NEUTRAL;

            if (Player* player = unit->ToPlayer())
                return player->GetTeamId();

            Creature* creature = unit->ToCreature();
            if (!creature)
                return TEAM_NEUTRAL;

            switch (creature->GetEntry())
            {
                case NPC_WINTERGRASP_SIEGE_ENGINE_ALLIANCE:
                    return TEAM_ALLIANCE;

                case NPC_WINTERGRASP_SIEGE_ENGINE_HORDE:
                    return TEAM_HORDE;

                default:
                    break;
            }

            if (creature->HasAura(SPELL_ALLIANCE_FLAG))
                return TEAM_ALLIANCE;

            if (creature->HasAura(SPELL_HORDE_FLAG))
                return TEAM_HORDE;

            uint32 faction = creature->GetFaction();

            if (faction == WintergraspFaction[TEAM_ALLIANCE])
                return TEAM_ALLIANCE;

            if (faction == WintergraspFaction[TEAM_HORDE])
                return TEAM_HORDE;

            return TEAM_NEUTRAL;
        }

        bool IsEnemyWintergraspUnit(Unit* target)
        {
            if (!target)
                return false;

            if (target == me)
                return false;

            TeamId myTeam = GetWintergraspUnitTeam(me);
            TeamId targetTeam = GetWintergraspUnitTeam(target);

            if ((myTeam == TEAM_ALLIANCE || myTeam == TEAM_HORDE) &&
                (targetTeam == TEAM_ALLIANCE || targetTeam == TEAM_HORDE))
                return myTeam != targetTeam;

            return me->IsValidAttackTarget(target);
        }

        void PrepareTargetCreatureForNpcAttack(Unit* target)
        {
            if (!target)
                return;

            Creature* creature = target->ToCreature();
            if (!creature)
                return;

            creature->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            creature->RemoveUnitFlag(UNIT_FLAG_PACIFIED);
            creature->RemoveUnitFlag(UNIT_FLAG_IMMUNE_TO_NPC);

            if (creature->GetEntry() == NPC_WINTERGRASP_TOWER_CANNON)
            {
                TeamId myTeam = GetWintergraspUnitTeam(me);

                if (myTeam == TEAM_ALLIANCE)
                    creature->SetFaction(WintergraspFaction[TEAM_HORDE]);
                else if (myTeam == TEAM_HORDE)
                    creature->SetFaction(WintergraspFaction[TEAM_ALLIANCE]);
            }
        }

        uint32 GetTargetPriority(Unit* target) const
        {
            if (!target)
                return 100;

            if (Creature* creature = target->ToCreature())
            {
                if (creature->GetEntry() == NPC_WINTERGRASP_TOWER_CANNON)
                    return 1;

                if (IsWintergraspVehicleEntry(creature->GetEntry()))
                    return 2;

                return 3;
            }

            if (target->ToPlayer())
                return 4;

            return 5;
        }

        float GetAngleToTarget(Unit* target) const
        {
            return std::atan2(
                target->GetPositionY() - me->GetPositionY(),
                target->GetPositionX() - me->GetPositionX()
            );
        }

        float GetAngleToObject(WorldObject* target) const
        {
            return std::atan2(
                target->GetPositionY() - me->GetPositionY(),
                target->GetPositionX() - me->GetPositionX()
            );
        }

        void FaceTarget(Unit* target)
        {
            if (!target)
                return;

            me->SetFacingTo(GetAngleToTarget(target));
            me->SetTarget(target->GetGUID());
        }

        void FaceObject(WorldObject* target)
        {
            if (!target)
                return;

            me->SetFacingTo(GetAngleToObject(target));
            me->SetTarget(target->GetGUID());
        }

        bool CanAttackTarget(Unit* target)
        {
            if (!target)
                return false;

            if (target == me)
                return false;

            if (!target->IsAlive())
                return false;

            if (!IsEnemyWintergraspUnit(target))
                return false;

            PrepareTargetCreatureForNpcAttack(target);

            if (!me->IsWithinDistInMap(target, float(WG_ELITE_DEMOLISHER_TARGET_RANGE)))
                return false;

            if (!me->IsWithinLOSInMap(target))
                return false;

            return true;
        }

        bool IsEliteDemolisherStructureType(WintergraspGameObjectBuildingType type) const
        {
            switch (type)
            {
                case BATTLEFIELD_WG_OBJECTTYPE_KEEP_TOWER:
                case BATTLEFIELD_WG_OBJECTTYPE_DOOR_LAST:
                case BATTLEFIELD_WG_OBJECTTYPE_DOOR:
                case BATTLEFIELD_WG_OBJECTTYPE_WALL:
                    return true;
                default:
                    break;
            }

            return false;
        }

        bool CanSeeGameObjectForSpell(GameObject* go) const
        {
            if (!go)
                return false;

            return me->IsWithinLOSInMap(go);
        }

        bool IsGameObjectInCastRange(GameObject* go) const
        {
            if (!go)
                return false;

            float distance = me->GetDistance(go);

            if (distance >= float(WG_ELITE_DEMOLISHER_RANGED_MIN_RANGE) &&
                distance <= float(WG_ELITE_DEMOLISHER_TARGET_RANGE))
                return true;

            if (distance <= float(WG_ELITE_DEMOLISHER_MELEE_RANGE))
                return true;

            return false;
        }

        bool CanAttackGameObject(GameObject* go)
        {
            if (!go)
                return false;

            if (!go->IsInWorld())
                return false;

            if (!go->IsDestructibleBuilding())
                return false;

            if (go->GetDestructibleState() == GO_DESTRUCTIBLE_DESTROYED)
                return false;

            if (!me->IsWithinDistInMap(go, float(WG_ELITE_DEMOLISHER_TARGET_RANGE)))
                return false;

            if (!IsGameObjectInCastRange(go))
                return false;

            if (!CanSeeGameObjectForSpell(go))
                return false;

            return true;
        }

        Unit* GetCurrentTarget()
        {
            if (CurrentTargetGuid.IsEmpty())
                return nullptr;

            return ObjectAccessor::GetUnit(*me, CurrentTargetGuid);
        }

        GameObject* GetCurrentGameObjectTarget()
        {
            if (CurrentGameObjectTargetGuid.IsEmpty())
                return nullptr;

            return ObjectAccessor::GetGameObject(*me, CurrentGameObjectTargetGuid);
        }

        Unit* SelectEliteTarget()
        {
            std::list<Unit*> targets;

            Trinity::AnyUnitInObjectRangeCheck check(me, float(WG_ELITE_DEMOLISHER_TARGET_RANGE));
            Trinity::UnitListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(me, targets, check);
            Cell::VisitAllObjects(me, searcher, float(WG_ELITE_DEMOLISHER_TARGET_RANGE));

            targets.remove_if([this](Unit* target)
            {
                return !CanAttackTarget(target);
            });

            if (targets.empty())
                return nullptr;

            targets.sort([this](Unit* left, Unit* right)
            {
                uint32 leftPriority = GetTargetPriority(left);
                uint32 rightPriority = GetTargetPriority(right);

                if (leftPriority != rightPriority)
                    return leftPriority < rightPriority;

                return me->GetDistance(left) < me->GetDistance(right);
            });

            return targets.front();
        }

        GameObject* SelectStructureTarget()
        {
            GameObject* bestTarget = nullptr;
            float bestDistance = float(WG_ELITE_DEMOLISHER_TARGET_RANGE) + 1.0f;

            for (uint8 i = 0; i < WG_MAX_OBJ; ++i)
            {
                if (!IsEliteDemolisherStructureType(WGGameObjectBuilding[i].type))
                    continue;

                GameObject* go = me->FindNearestGameObject(
                    WGGameObjectBuilding[i].entry,
                    float(WG_ELITE_DEMOLISHER_TARGET_RANGE)
                );

                if (!CanAttackGameObject(go))
                    continue;

                float distance = me->GetDistance(go);
                if (distance >= bestDistance)
                    continue;

                bestTarget = go;
                bestDistance = distance;
            }

            return bestTarget;
        }

        void ClearCurrentTarget()
        {
            CurrentTargetGuid = ObjectGuid::Empty;
            CurrentGameObjectTargetGuid = ObjectGuid::Empty;
            CurrentTargetType = WG_ELITE_TARGET_NONE;

            me->AttackStop();
            me->SetTarget(ObjectGuid::Empty);
            me->GetMotionMaster()->Clear();

            IsMovingPath = false;
        }

        void MoveToCurrentPathPoint()
        {
            if (PathFinished)
                return;

            if (!CurrentPath || !CurrentPath->Points || !CurrentPath->PointCount)
            {
                PathFinished = true;
                IsMovingPath = false;
                return;
            }

            if (CurrentPathPoint >= CurrentPath->PointCount)
            {
                PathFinished = true;
                IsMovingPath = false;
                return;
            }

            Position const& pos = CurrentPath->Points[CurrentPathPoint];

            IsMovingPath = true;

            me->AttackStop();
            me->SetTarget(ObjectGuid::Empty);
            me->GetMotionMaster()->Clear();
            me->GetMotionMaster()->MovePoint(
                CurrentPathPoint,
                pos.GetPositionX(),
                pos.GetPositionY(),
                pos.GetPositionZ()
            );
        }

        void ResumePath()
        {
            if (PathFinished)
                return;

            if (IsMovingPath)
                return;

            MoveToCurrentPathPoint();
        }

        void MovementInform(uint32 type, uint32 id) override
        {
            if (type != POINT_MOTION_TYPE)
                return;

            if (!CurrentPath || !CurrentPath->Points || !CurrentPath->PointCount)
                return;

            if (id != CurrentPathPoint)
                return;

            if (CurrentPathPoint < CurrentPath->PointCount)
            {
                Position const& pos = CurrentPath->Points[CurrentPathPoint];
                me->SetFacingTo(pos.GetOrientation());
            }

            ++CurrentPathPoint;
            IsMovingPath = false;

            if (CurrentPathPoint >= CurrentPath->PointCount)
            {
                PathFinished = true;
                return;
            }

            MoveToCurrentPathPoint();
        }

        void Reset() override
        {
            SearchTimer = WG_ELITE_DEMOLISHER_SEARCH_TIMER;
            RangedSpellTimer = 1000;
            MeleeSpellTimer = 1000;

            CurrentTargetGuid = ObjectGuid::Empty;
            CurrentGameObjectTargetGuid = ObjectGuid::Empty;
            CurrentTargetType = WG_ELITE_TARGET_NONE;

            CurrentPathPoint = 0;
            IsMovingPath = false;
            PathFinished = false;

            CurrentPath = GetEliteDemolisherPathByWorkshopId(WorkshopId);

            me->SetReactState(REACT_AGGRESSIVE);
            me->SetWalk(false);
            me->AttackStop();
            me->SetTarget(ObjectGuid::Empty);
            me->GetMotionMaster()->Clear();

            if (!CurrentPath || !CurrentPath->Points || CurrentPath->PointCount < 2)
            {
                PathFinished = true;
                IsMovingPath = false;
                return;
            }

            CurrentPathPoint = 1;

            MoveToCurrentPathPoint();
        }

        void JustDied(Unit* /*killer*/) override
        {
            ClearCurrentTarget();
        }

        void AttackStart(Unit* target) override
        {
            if (!CanAttackTarget(target))
                return;

            CurrentTargetGuid = target->GetGUID();
            CurrentGameObjectTargetGuid = ObjectGuid::Empty;
            CurrentTargetType = WG_ELITE_TARGET_UNIT;

            PrepareTargetCreatureForNpcAttack(target);

            me->SetReactState(REACT_AGGRESSIVE);
            me->SetInCombatWith(target);
            target->SetInCombatWith(me);

            FaceTarget(target);

            me->AttackStop();

            IsMovingPath = false;

            float distance = me->GetDistance(target);

            if (distance < float(WG_ELITE_DEMOLISHER_RANGED_MIN_RANGE) &&
                distance > float(WG_ELITE_DEMOLISHER_MELEE_RANGE))
            {
                me->GetMotionMaster()->Clear();
                me->GetMotionMaster()->MoveChase(target, float(WG_ELITE_DEMOLISHER_CHASE_DISTANCE));
            }
            else
            {
                me->GetMotionMaster()->Clear();
            }
        }

        void SetGameObjectTarget(GameObject* target)
        {
            if (!target)
                return;

            CurrentTargetGuid = ObjectGuid::Empty;
            CurrentGameObjectTargetGuid = target->GetGUID();
            CurrentTargetType = WG_ELITE_TARGET_GAMEOBJECT;

            me->AttackStop();
            me->GetMotionMaster()->Clear();
            FaceObject(target);

            IsMovingPath = false;
        }

        void SetUnitTarget(Unit* target)
        {
            if (!target)
                return;

            CurrentTargetGuid = target->GetGUID();
            CurrentGameObjectTargetGuid = ObjectGuid::Empty;
            CurrentTargetType = WG_ELITE_TARGET_UNIT;

            AttackStart(target);
        }

        bool SelectNewTarget()
        {
            GameObject* structureTarget = SelectStructureTarget();

            if (structureTarget)
            {
                SetGameObjectTarget(structureTarget);
                return true;
            }

            Unit* unitTarget = SelectEliteTarget();

            if (unitTarget)
            {
                SetUnitTarget(unitTarget);
                return true;
            }

            CurrentTargetType = WG_ELITE_TARGET_NONE;
            return false;
        }

        void CastRangedSpell(Unit* target)
        {
            if (!target)
                return;

            PrepareTargetCreatureForNpcAttack(target);
            FaceTarget(target);

            me->AttackStop();
            me->GetMotionMaster()->Clear();
            IsMovingPath = false;

            me->CastSpell(target, SPELL_WG_ELITE_DEMOLISHER_RANGED, true);
        }

        void CastMeleeSpell(Unit* target)
        {
            if (!target)
                return;

            PrepareTargetCreatureForNpcAttack(target);
            FaceTarget(target);

            me->AttackStop();
            me->GetMotionMaster()->Clear();
            IsMovingPath = false;

            me->CastSpell(target, SPELL_WG_ELITE_DEMOLISHER_MELEE, true);
        }

        void CastRangedSpell(GameObject* target)
        {
            if (!target)
                return;

            if (!CanAttackGameObject(target))
                return;

            FaceObject(target);

            me->AttackStop();
            me->GetMotionMaster()->Clear();
            IsMovingPath = false;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(SPELL_WG_ELITE_DEMOLISHER_RANGED);
            if (!spellInfo)
                return;

            SpellCastTargets targets;
            targets.SetGOTarget(target);
            targets.SetDst(*target);

            Spell* spell = new Spell(me, spellInfo, TRIGGERED_FULL_MASK);
            spell->prepare(targets);
        }

        void CastMeleeSpell(GameObject* target)
        {
            if (!target)
                return;

            if (!CanAttackGameObject(target))
                return;

            FaceObject(target);

            me->AttackStop();
            me->GetMotionMaster()->Clear();
            IsMovingPath = false;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(SPELL_WG_ELITE_DEMOLISHER_MELEE);
            if (!spellInfo)
                return;

            SpellCastTargets targets;
            targets.SetGOTarget(target);
            targets.SetDst(*target);

            Spell* spell = new Spell(me, spellInfo, TRIGGERED_FULL_MASK);
            spell->prepare(targets);
        }

        bool UpdateGameObjectTarget(uint32 diff)
        {
            GameObject* target = GetCurrentGameObjectTarget();

            if (!CanAttackGameObject(target))
            {
                ClearCurrentTarget();
                ResumePath();
                return false;
            }

            FaceObject(target);

            float distance = me->GetDistance(target);

            if (distance >= float(WG_ELITE_DEMOLISHER_RANGED_MIN_RANGE))
            {
                me->AttackStop();
                me->GetMotionMaster()->Clear();
                IsMovingPath = false;

                if (RangedSpellTimer <= diff)
                {
                    RangedSpellTimer = WG_ELITE_DEMOLISHER_RANGED_SPELL_TIMER;
                    CastRangedSpell(target);
                }
                else
                {
                    RangedSpellTimer -= diff;
                }

                return true;
            }

            if (distance <= float(WG_ELITE_DEMOLISHER_MELEE_RANGE))
            {
                me->AttackStop();
                me->GetMotionMaster()->Clear();
                IsMovingPath = false;

                if (MeleeSpellTimer <= diff)
                {
                    MeleeSpellTimer = WG_ELITE_DEMOLISHER_MELEE_SPELL_TIMER;
                    CastMeleeSpell(target);
                }
                else
                {
                    MeleeSpellTimer -= diff;
                }

                return true;
            }

            ClearCurrentTarget();
            ResumePath();
            return false;
        }

        bool UpdateUnitTarget(uint32 diff)
        {
            Unit* target = GetCurrentTarget();

            if (!CanAttackTarget(target))
            {
                ClearCurrentTarget();
                ResumePath();
                return false;
            }

            PrepareTargetCreatureForNpcAttack(target);
            FaceTarget(target);

            float distance = me->GetDistance(target);

            if (distance >= float(WG_ELITE_DEMOLISHER_RANGED_MIN_RANGE))
            {
                me->AttackStop();
                me->GetMotionMaster()->Clear();
                IsMovingPath = false;

                if (RangedSpellTimer <= diff)
                {
                    RangedSpellTimer = WG_ELITE_DEMOLISHER_RANGED_SPELL_TIMER;
                    CastRangedSpell(target);
                }
                else
                {
                    RangedSpellTimer -= diff;
                }

                return true;
            }

            if (distance > float(WG_ELITE_DEMOLISHER_MELEE_RANGE))
            {
                me->AttackStop();
                IsMovingPath = false;
                me->GetMotionMaster()->MoveChase(target, float(WG_ELITE_DEMOLISHER_CHASE_DISTANCE));
                return true;
            }

            me->AttackStop();
            me->GetMotionMaster()->Clear();
            IsMovingPath = false;

            if (MeleeSpellTimer <= diff)
            {
                MeleeSpellTimer = WG_ELITE_DEMOLISHER_MELEE_SPELL_TIMER;
                CastMeleeSpell(target);
            }
            else
            {
                MeleeSpellTimer -= diff;
            }

            return true;
        }

        void UpdateAI(uint32 diff) override
        {
            if (!IsWintergraspWarTime())
            {
                ClearCurrentTarget();
                return;
            }

            if (!me->IsAlive())
                return;

            if (CurrentTargetType == WG_ELITE_TARGET_GAMEOBJECT)
            {
                UpdateGameObjectTarget(diff);
                return;
            }

            if (CurrentTargetType == WG_ELITE_TARGET_UNIT)
            {
                UpdateUnitTarget(diff);
                return;
            }

            if (SearchTimer <= diff)
            {
                SearchTimer = WG_ELITE_DEMOLISHER_SEARCH_TIMER;

                if (SelectNewTarget())
                    return;
            }
            else
            {
                SearchTimer -= diff;
            }

            ResumePath();
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_wg_elite_demolisher_aiAI(creature);
    }
};

void AddSC_BF_wintergrasp() {
    new Battlefield_wintergrasp();
    new npc_wg_give_promotion_credit();
	new npc_wg_tower_cannon_ai();
	new npc_wg_elite_demolisher_ai();
}