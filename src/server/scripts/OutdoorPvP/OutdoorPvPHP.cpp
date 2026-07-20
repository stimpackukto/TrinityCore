/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "OutdoorPvPHP.h"
#include "CustomTeamBuffUtil.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Map.h"
#include "OutdoorPvPMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldStatePackets.h"

#include "../Custom/outdoorpvp/CustomOutdoorPvpWarController.h"

#include <cmath>
#include <sstream>
#include <string>

uint32 const OutdoorPvPHPBuffZonesNum = 6;
uint32 const OutdoorPvPHPBuffZones[OutdoorPvPHPBuffZonesNum] = { 3483, 3563, 3562, 3713, 3714, 3836 };
uint32 const HP_CREDITMARKER[HP_TOWER_NUM] = { 19032, 19028, 19029 };

uint32 const HP_MAP_N[HP_TOWER_NUM] = { 0x9b5, 0x9b2, 0x9a8 };
uint32 const HP_MAP_A[HP_TOWER_NUM] = { 0x9b3, 0x9b0, 0x9a7 };
uint32 const HP_MAP_H[HP_TOWER_NUM] = { 0x9b4, 0x9b1, 0x9a6 };

uint32 const HP_TowerArtKit_A[HP_TOWER_NUM] = { 65, 62, 67 };
uint32 const HP_TowerArtKit_H[HP_TOWER_NUM] = { 64, 61, 68 };
uint32 const HP_TowerArtKit_N[HP_TOWER_NUM] = { 66, 63, 69 };

go_type const HPCapturePoints[HP_TOWER_NUM] =
{
    { 182175, 530, { -471.462f, 3451.09f, 34.6432f,  0.174533f }, { 0.0f, 0.0f, 0.087156f,  0.996195f } },
    { 182174, 530, { -184.889f, 3476.93f, 38.2050f, -0.017453f }, { 0.0f, 0.0f, 0.008727f, -0.999962f } },
    { 182173, 530, { -290.016f, 3702.42f, 56.6729f,  0.034907f }, { 0.0f, 0.0f, 0.017452f,  0.999848f } }
};

go_type const HPTowerFlags[HP_TOWER_NUM] =
{
    { 183514, 530, { -467.078f, 3528.17f, 64.7121f,  3.14159f }, { 0.0f, 0.0f, 1.000000f,  0.000000f } },
    { 182525, 530, { -187.887f, 3459.38f, 60.0403f, -3.12414f }, { 0.0f, 0.0f, 0.999962f, -0.008727f } },
    { 183515, 530, { -289.610f, 3696.83f, 75.9447f,  3.12414f }, { 0.0f, 0.0f, 0.999962f,  0.008727f } }
};

uint32 const HP_LANG_CAPTURE_A[HP_TOWER_NUM] =
{
    TEXT_BROKEN_HILL_TAKEN_ALLIANCE,
    TEXT_OVERLOOK_TAKEN_ALLIANCE,
    TEXT_STADIUM_TAKEN_ALLIANCE
};

uint32 const HP_LANG_CAPTURE_H[HP_TOWER_NUM] =
{
    TEXT_BROKEN_HILL_TAKEN_HORDE,
    TEXT_OVERLOOK_TAKEN_HORDE,
    TEXT_STADIUM_TAKEN_HORDE
};

namespace
{
    struct HellfireCapturePointState
    {
        uint32 State = OBJECTIVESTATE_NEUTRAL;
        float Value = 0.0f;
        uint32 Team = TEAM_NEUTRAL;
    };

    static TeamId g_HellfireServerBuffTeam = TEAM_NEUTRAL;
    static HellfireCapturePointState g_HellfireCapturePoints[HP_TOWER_NUM];
    static uint32 g_HellfireCapturePointSaveTimer[HP_TOWER_NUM] = { 0, 0, 0 };

    static char const* OUTDOORPVP_HP_TYPE = "hellfire_peninsula";
    static char const* OUTDOORPVP_HP_STATE_KEY = "state";

    static bool IsValidHellfireTeam(uint32 team)
    {
        return team == TEAM_ALLIANCE || team == TEAM_HORDE || team == TEAM_NEUTRAL;
    }

    static bool IsValidObjectiveState(uint32 state)
    {
        return state <= OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE;
    }

    static std::string BuildHellfireStateJson()
    {
        std::ostringstream ss;

        ss << "{";
        ss << "\"server_buff_team\":" << uint32(g_HellfireServerBuffTeam) << ",";
        ss << "\"capture_points\":[";

        for (uint32 i = 0; i < HP_TOWER_NUM; ++i)
        {
            if (i > 0)
                ss << ",";

            ss << "{";
            ss << "\"state\":" << g_HellfireCapturePoints[i].State << ",";
            ss << "\"value\":" << g_HellfireCapturePoints[i].Value << ",";
            ss << "\"team\":" << g_HellfireCapturePoints[i].Team;
            ss << "}";
        }

        ss << "]";
        ss << "}";

        return ss.str();
    }

    static uint32 ExtractJsonNumber(std::string const& json, std::string const& key, uint32 defaultValue)
    {
        std::string findKey = "\"" + key + "\"";
        std::size_t pos = json.find(findKey);

        if (pos == std::string::npos)
            return defaultValue;

        pos = json.find(':', pos);

        if (pos == std::string::npos)
            return defaultValue;

        ++pos;

        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
            ++pos;

        uint32 value = 0;
        bool found = false;

        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
        {
            found = true;
            value = value * 10 + uint32(json[pos] - '0');
            ++pos;
        }

        return found ? value : defaultValue;
    }

    static uint32 ExtractJsonNumberFromObject(std::string const& objectJson, std::string const& key, uint32 defaultValue)
    {
        std::string findKey = "\"" + key + "\"";
        std::size_t pos = objectJson.find(findKey);

        if (pos == std::string::npos)
            return defaultValue;

        pos = objectJson.find(':', pos);

        if (pos == std::string::npos)
            return defaultValue;

        ++pos;

        while (pos < objectJson.size() && (objectJson[pos] == ' ' || objectJson[pos] == '\t'))
            ++pos;

        uint32 value = 0;
        bool found = false;

        while (pos < objectJson.size() && objectJson[pos] >= '0' && objectJson[pos] <= '9')
        {
            found = true;
            value = value * 10 + uint32(objectJson[pos] - '0');
            ++pos;
        }

        return found ? value : defaultValue;
    }

    static float ExtractJsonFloatFromObject(std::string const& objectJson, std::string const& key, float defaultValue)
    {
        std::string findKey = "\"" + key + "\"";
        std::size_t pos = objectJson.find(findKey);

        if (pos == std::string::npos)
            return defaultValue;

        pos = objectJson.find(':', pos);

        if (pos == std::string::npos)
            return defaultValue;

        ++pos;

        while (pos < objectJson.size() && (objectJson[pos] == ' ' || objectJson[pos] == '\t'))
            ++pos;

        bool negative = false;

        if (pos < objectJson.size() && objectJson[pos] == '-')
        {
            negative = true;
            ++pos;
        }

        float value = 0.0f;
        bool found = false;

        while (pos < objectJson.size() && objectJson[pos] >= '0' && objectJson[pos] <= '9')
        {
            found = true;
            value = value * 10.0f + float(objectJson[pos] - '0');
            ++pos;
        }

        if (pos < objectJson.size() && objectJson[pos] == '.')
        {
            ++pos;

            float scale = 0.1f;

            while (pos < objectJson.size() && objectJson[pos] >= '0' && objectJson[pos] <= '9')
            {
                found = true;
                value += float(objectJson[pos] - '0') * scale;
                scale *= 0.1f;
                ++pos;
            }
        }

        if (!found)
            return defaultValue;

        return negative ? -value : value;
    }

    static bool ExtractCapturePointObject(std::string const& json, uint32 index, std::string& outObject)
    {
        std::size_t pos = json.find("\"capture_points\"");

        if (pos == std::string::npos)
            return false;

        pos = json.find('[', pos);

        if (pos == std::string::npos)
            return false;

        ++pos;

        uint32 currentIndex = 0;

        while (pos < json.size())
        {
            pos = json.find('{', pos);

            if (pos == std::string::npos)
                return false;

            std::size_t end = json.find('}', pos);

            if (end == std::string::npos)
                return false;

            if (currentIndex == index)
            {
                outObject = json.substr(pos, end - pos + 1);
                return true;
            }

            pos = end + 1;
            ++currentIndex;
        }

        return false;
    }

    static void SaveHellfireOutdoorPvPState()
    {
        std::string json = BuildHellfireStateJson();

        LoginDatabase.PExecute(
            "INSERT INTO appdata.outdoorpvp_state "
            "(pvp_type, state_key, state_value) "
            "VALUES ('{}', '{}', '{}') "
            "ON DUPLICATE KEY UPDATE state_value = VALUES(state_value)",
            OUTDOORPVP_HP_TYPE,
            OUTDOORPVP_HP_STATE_KEY,
            json
        );
    }

    static void SaveHellfireCapturePointState(uint32 index, uint32 state, float value, uint32 team)
    {
        if (index >= HP_TOWER_NUM)
            return;

        g_HellfireCapturePoints[index].State = state;
        g_HellfireCapturePoints[index].Value = value;
        g_HellfireCapturePoints[index].Team = team;

        SaveHellfireOutdoorPvPState();
    }

    static void LoadHellfireOutdoorPvPState()
    {
        QueryResult result = LoginDatabase.PQuery(
            "SELECT state_value FROM appdata.outdoorpvp_state "
            "WHERE pvp_type = '{}' AND state_key = '{}'",
            OUTDOORPVP_HP_TYPE,
            OUTDOORPVP_HP_STATE_KEY
        );

        if (!result)
            return;

        Field* fields = result->Fetch();
        std::string json = fields[0].GetString();

        uint32 team = ExtractJsonNumber(json, "server_buff_team", uint32(TEAM_NEUTRAL));

        if (IsValidHellfireTeam(team))
            g_HellfireServerBuffTeam = TeamId(team);

        for (uint32 i = 0; i < HP_TOWER_NUM; ++i)
        {
            std::string objectJson;

            if (!ExtractCapturePointObject(json, i, objectJson))
                continue;

            uint32 state = ExtractJsonNumberFromObject(objectJson, "state", uint32(OBJECTIVESTATE_NEUTRAL));
            float value = ExtractJsonFloatFromObject(objectJson, "value", 0.0f);
            uint32 cpTeam = ExtractJsonNumberFromObject(objectJson, "team", uint32(TEAM_NEUTRAL));

            if (!IsValidObjectiveState(state))
                state = OBJECTIVESTATE_NEUTRAL;

            if (!IsValidHellfireTeam(cpTeam))
                cpTeam = TEAM_NEUTRAL;

            g_HellfireCapturePoints[i].State = state;
            g_HellfireCapturePoints[i].Value = value;
            g_HellfireCapturePoints[i].Team = cpTeam;
        }
    }

    static void RemoveHellfireServerBuff(Player* player)
    {
        if (!player)
            return;

        player->RemoveAurasDueToSpell(AllianceBuff);
        player->RemoveAurasDueToSpell(HordeBuff);
    }

    static void ApplyHellfireServerBuff(Player* player)
    {
        if (!player || !player->IsInWorld())
            return;

        RemoveHellfireServerBuff(player);

        if (g_HellfireServerBuffTeam == TEAM_ALLIANCE && player->GetTeamId() == TEAM_ALLIANCE)
        {
            CustomTeamBuffUtil::ApplySpellToPlayer(
                player,
                AllianceBuff,
                -1,
                true,
                false,
                true,
                true
            );

            return;
        }

        if (g_HellfireServerBuffTeam == TEAM_HORDE && player->GetTeamId() == TEAM_HORDE)
        {
            CustomTeamBuffUtil::ApplySpellToPlayer(
                player,
                HordeBuff,
                -1,
                true,
                false,
                true,
                true
            );

            return;
        }
    }

    static void ClearHellfireServerBuffForAllPlayers()
    {
        CustomTeamBuffUtil::RemoveSpellFromTeam(TEAM_ALLIANCE, AllianceBuff);
        CustomTeamBuffUtil::RemoveSpellFromTeam(TEAM_ALLIANCE, HordeBuff);

        CustomTeamBuffUtil::RemoveSpellFromTeam(TEAM_HORDE, AllianceBuff);
        CustomTeamBuffUtil::RemoveSpellFromTeam(TEAM_HORDE, HordeBuff);
    }

    static void UpdateHellfireServerBuffForAllPlayers()
    {
        ClearHellfireServerBuffForAllPlayers();

        if (g_HellfireServerBuffTeam == TEAM_ALLIANCE)
        {
            CustomTeamBuffUtil::ApplySpellToTeam(
                TEAM_ALLIANCE,
                AllianceBuff,
                -1,
                true,
                false,
                true,
                true
            );

            return;
        }

        if (g_HellfireServerBuffTeam == TEAM_HORDE)
        {
            CustomTeamBuffUtil::ApplySpellToTeam(
                TEAM_HORDE,
                HordeBuff,
                -1,
                true,
                false,
                true,
                true
            );

            return;
        }
    }

    static void SetHellfireServerBuffTeam(TeamId team)
    {
        if (g_HellfireServerBuffTeam == team)
            return;

        g_HellfireServerBuffTeam = team;
        SaveHellfireOutdoorPvPState();
        UpdateHellfireServerBuffForAllPlayers();
    }
}

OPvPCapturePointHP::OPvPCapturePointHP(OutdoorPvP* pvp, OutdoorPvPHPTowerType type) : OPvPCapturePoint(pvp), m_TowerType(type)
{
    SetCapturePointData(HPCapturePoints[type].entry, HPCapturePoints[type].map, HPCapturePoints[type].pos, HPCapturePoints[type].rot);
    AddObject(type, HPTowerFlags[type].entry, HPTowerFlags[type].map, HPTowerFlags[type].pos, HPTowerFlags[type].rot);
}

OutdoorPvPHP::OutdoorPvPHP()
{
    m_TypeId = OUTDOOR_PVP_HP;
    m_AllianceTowersControlled = 0;
    m_HordeTowersControlled = 0;
    SetMapFromZone(OutdoorPvPHPBuffZones[0]);
}

bool OutdoorPvPHP::SetupOutdoorPvP()
{
    m_AllianceTowersControlled = 0;
    m_HordeTowersControlled = 0;

    LoadHellfireOutdoorPvPState();

    for (uint32 i = 0; i < OutdoorPvPHPBuffZonesNum; ++i)
        RegisterZone(OutdoorPvPHPBuffZones[i]);

    OPvPCapturePointHP* brokenHill = new OPvPCapturePointHP(this, HP_TOWER_BROKEN_HILL);
    AddCapturePoint(brokenHill);
    brokenHill->LoadSavedState();

    OPvPCapturePointHP* overlook = new OPvPCapturePointHP(this, HP_TOWER_OVERLOOK);
    AddCapturePoint(overlook);
    overlook->LoadSavedState();

    OPvPCapturePointHP* stadium = new OPvPCapturePointHP(this, HP_TOWER_STADIUM);
    AddCapturePoint(stadium);
    stadium->LoadSavedState();

    return true;
}

void OutdoorPvPHP::HandlePlayerEnterZone(Player* player, uint32 zone)
{
    ApplyHellfireServerBuff(player);
    OutdoorPvP::HandlePlayerEnterZone(player, zone);
}

void OutdoorPvPHP::HandlePlayerLeaveZone(Player* player, uint32 zone)
{
    OutdoorPvP::HandlePlayerLeaveZone(player, zone);
}

bool OutdoorPvPHP::Update(uint32 diff)
{
    bool resetForWar = false;

    if (OutdoorPvpWar_ConsumeHellfireNeutralResetRequest())
    {
        resetForWar = true;

        m_AllianceTowersControlled = 0;
        m_HordeTowersControlled = 0;

        SetHellfireServerBuffTeam(TEAM_NEUTRAL);

        for (OPvPCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        {
            OPvPCapturePointHP* capturePoint = static_cast<OPvPCapturePointHP*>(itr->second);

            if (!capturePoint)
                continue;

            capturePoint->ResetToNeutralForWarStart();
        }

        SendUpdateWorldState(HP_UI_TOWER_COUNT_A, m_AllianceTowersControlled);
        SendUpdateWorldState(HP_UI_TOWER_COUNT_H, m_HordeTowersControlled);

        SaveHellfireOutdoorPvPState();
    }

    bool changed = OutdoorPvP::Update(diff);

    if (changed)
    {
        if (m_AllianceTowersControlled == 3)
            SetHellfireServerBuffTeam(TEAM_ALLIANCE);
        else if (m_HordeTowersControlled == 3)
            SetHellfireServerBuffTeam(TEAM_HORDE);
        else
            SetHellfireServerBuffTeam(TEAM_NEUTRAL);

        SendUpdateWorldState(HP_UI_TOWER_COUNT_A, m_AllianceTowersControlled);
        SendUpdateWorldState(HP_UI_TOWER_COUNT_H, m_HordeTowersControlled);
    }

    return changed || resetForWar;
}

void OutdoorPvPHP::SendRemoveWorldStates(Player* player)
{
    player->SendUpdateWorldState(HP_UI_TOWER_DISPLAY_A, 0);
    player->SendUpdateWorldState(HP_UI_TOWER_DISPLAY_H, 0);
    player->SendUpdateWorldState(HP_UI_TOWER_COUNT_H, 0);
    player->SendUpdateWorldState(HP_UI_TOWER_COUNT_A, 0);

    for (int i = 0; i < HP_TOWER_NUM; ++i)
    {
        player->SendUpdateWorldState(HP_MAP_N[i], 0);
        player->SendUpdateWorldState(HP_MAP_A[i], 0);
        player->SendUpdateWorldState(HP_MAP_H[i], 0);
    }
}

void OutdoorPvPHP::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(HP_UI_TOWER_DISPLAY_A, 1);
    packet.Worldstates.emplace_back(HP_UI_TOWER_DISPLAY_H, 1);
    packet.Worldstates.emplace_back(HP_UI_TOWER_COUNT_A, m_AllianceTowersControlled);
    packet.Worldstates.emplace_back(HP_UI_TOWER_COUNT_H, m_HordeTowersControlled);

    for (OPvPCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        itr->second->FillInitialWorldStates(packet);
}

void OPvPCapturePointHP::LoadSavedState()
{
    HellfireCapturePointState const& saved = g_HellfireCapturePoints[m_TowerType];

    uint32 savedState = saved.State;

    if (!IsValidObjectiveState(savedState))
        savedState = OBJECTIVESTATE_NEUTRAL;

    m_OldState = m_State;
    m_State = ObjectiveStates(savedState);
    m_value = saved.Value;
    m_team = TeamId(saved.Team);

    uint32 artkit = 21;
    uint32 artkit2 = HP_TowerArtKit_N[m_TowerType];

    switch (m_State)
    {
        case OBJECTIVESTATE_ALLIANCE:
        case OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE:
        {
            artkit = 2;
            artkit2 = HP_TowerArtKit_A[m_TowerType];

            if (m_State == OBJECTIVESTATE_ALLIANCE)
            {
                uint32 allianceTowers = ((OutdoorPvPHP*)m_PvP)->GetAllianceTowersControlled();

                if (allianceTowers < 3)
                    ((OutdoorPvPHP*)m_PvP)->SetAllianceTowersControlled(++allianceTowers);
            }

            break;
        }

        case OBJECTIVESTATE_HORDE:
        case OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE:
        {
            artkit = 1;
            artkit2 = HP_TowerArtKit_H[m_TowerType];

            if (m_State == OBJECTIVESTATE_HORDE)
            {
                uint32 hordeTowers = ((OutdoorPvPHP*)m_PvP)->GetHordeTowersControlled();

                if (hordeTowers < 3)
                    ((OutdoorPvPHP*)m_PvP)->SetHordeTowersControlled(++hordeTowers);
            }

            break;
        }

        case OBJECTIVESTATE_NEUTRAL:
        case OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE:
        case OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE:
        default:
            break;
    }

    Map* map = m_PvP->GetMap();

    if (!map)
        return;

    auto bounds = map->GetGameObjectBySpawnIdStore().equal_range(m_capturePointSpawnId);

    for (auto itr = bounds.first; itr != bounds.second; ++itr)
        itr->second->SetGoArtKit(artkit);

    bounds = map->GetGameObjectBySpawnIdStore().equal_range(m_Objects[m_TowerType]);

    for (auto itr = bounds.first; itr != bounds.second; ++itr)
        itr->second->SetGoArtKit(artkit2);
}

void OPvPCapturePointHP::ResetToNeutralForWarStart()
{
    m_OldState = m_State;
    m_State = ObjectiveStates(OBJECTIVESTATE_NEUTRAL);
    m_value = 0.0f;
    m_team = TeamId(TEAM_NEUTRAL);

    g_HellfireCapturePointSaveTimer[m_TowerType] = 0;

    SaveHellfireCapturePointState(
        uint32(m_TowerType),
        uint32(OBJECTIVESTATE_NEUTRAL),
        0.0f,
        uint32(TEAM_NEUTRAL)
    );

    Map* map = m_PvP->GetMap();

    if (map)
    {
        auto bounds = map->GetGameObjectBySpawnIdStore().equal_range(m_capturePointSpawnId);

        for (auto itr = bounds.first; itr != bounds.second; ++itr)
            itr->second->SetGoArtKit(21);

        bounds = map->GetGameObjectBySpawnIdStore().equal_range(m_Objects[m_TowerType]);

        for (auto itr = bounds.first; itr != bounds.second; ++itr)
            itr->second->SetGoArtKit(HP_TowerArtKit_N[m_TowerType]);
    }

    m_PvP->SendUpdateWorldState(HP_MAP_N[m_TowerType], 1);
    m_PvP->SendUpdateWorldState(HP_MAP_A[m_TowerType], 0);
    m_PvP->SendUpdateWorldState(HP_MAP_H[m_TowerType], 0);

    OutdoorPvpWar_OnHellfireFlagOwnerChanged(uint8(m_TowerType), TeamId(TEAM_NEUTRAL));
}

bool OPvPCapturePointHP::Update(uint32 diff)
{
    bool changed = OPvPCapturePoint::Update(diff);

    HellfireCapturePointState const& saved = g_HellfireCapturePoints[m_TowerType];

    bool stateChanged = saved.State != uint32(m_State);
    bool teamChanged = saved.Team != uint32(m_team);
    bool valueChanged = std::fabs(saved.Value - m_value) >= 1.0f;

    if (!stateChanged && !teamChanged && !valueChanged)
        return changed;

    if (!stateChanged && !teamChanged)
    {
        g_HellfireCapturePointSaveTimer[m_TowerType] += diff;

        if (g_HellfireCapturePointSaveTimer[m_TowerType] < 1000)
            return changed;
    }

    g_HellfireCapturePointSaveTimer[m_TowerType] = 0;

    SaveHellfireCapturePointState(
        uint32(m_TowerType),
        uint32(m_State),
        m_value,
        uint32(m_team)
    );

    return changed;
}

void OPvPCapturePointHP::ChangeState()
{
    uint32 field = 0;

    switch (m_OldState)
    {
        case OBJECTIVESTATE_NEUTRAL:
            field = HP_MAP_N[m_TowerType];
            break;
        case OBJECTIVESTATE_ALLIANCE:
            field = HP_MAP_A[m_TowerType];
            if (uint32 alliance_towers = ((OutdoorPvPHP*)m_PvP)->GetAllianceTowersControlled())
                ((OutdoorPvPHP*)m_PvP)->SetAllianceTowersControlled(--alliance_towers);
            break;
        case OBJECTIVESTATE_HORDE:
            field = HP_MAP_H[m_TowerType];
            if (uint32 horde_towers = ((OutdoorPvPHP*)m_PvP)->GetHordeTowersControlled())
                ((OutdoorPvPHP*)m_PvP)->SetHordeTowersControlled(--horde_towers);
            break;
        case OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE:
            field = HP_MAP_N[m_TowerType];
            break;
        case OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE:
            field = HP_MAP_N[m_TowerType];
            break;
        case OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE:
            field = HP_MAP_A[m_TowerType];
            break;
        case OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE:
            field = HP_MAP_H[m_TowerType];
            break;
    }

    if (field)
    {
        m_PvP->SendUpdateWorldState(field, 0);
        field = 0;
    }

    uint32 artkit = 21;
    uint32 artkit2 = HP_TowerArtKit_N[m_TowerType];

    switch (m_State)
    {
        case OBJECTIVESTATE_NEUTRAL:
            field = HP_MAP_N[m_TowerType];
            break;
        case OBJECTIVESTATE_ALLIANCE:
        {
            field = HP_MAP_A[m_TowerType];
            artkit = 2;
            artkit2 = HP_TowerArtKit_A[m_TowerType];

            uint32 alliance_towers = ((OutdoorPvPHP*)m_PvP)->GetAllianceTowersControlled();

            if (alliance_towers < 3)
                ((OutdoorPvPHP*)m_PvP)->SetAllianceTowersControlled(++alliance_towers);

            m_PvP->SendDefenseMessage(OutdoorPvPHPBuffZones[0], HP_LANG_CAPTURE_A[m_TowerType]);
            break;
        }
        case OBJECTIVESTATE_HORDE:
        {
            field = HP_MAP_H[m_TowerType];
            artkit = 1;
            artkit2 = HP_TowerArtKit_H[m_TowerType];

            uint32 horde_towers = ((OutdoorPvPHP*)m_PvP)->GetHordeTowersControlled();

            if (horde_towers < 3)
                ((OutdoorPvPHP*)m_PvP)->SetHordeTowersControlled(++horde_towers);

            m_PvP->SendDefenseMessage(OutdoorPvPHPBuffZones[0], HP_LANG_CAPTURE_H[m_TowerType]);
            break;
        }
        case OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE:
            field = HP_MAP_N[m_TowerType];
            break;
        case OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE:
            field = HP_MAP_N[m_TowerType];
            break;
        case OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE:
            field = HP_MAP_A[m_TowerType];
            artkit = 2;
            artkit2 = HP_TowerArtKit_A[m_TowerType];
            break;
        case OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE:
            field = HP_MAP_H[m_TowerType];
            artkit = 1;
            artkit2 = HP_TowerArtKit_H[m_TowerType];
            break;
    }

    Map* map = m_PvP->GetMap();

    if (map)
    {
        auto bounds = map->GetGameObjectBySpawnIdStore().equal_range(m_capturePointSpawnId);

        for (auto itr = bounds.first; itr != bounds.second; ++itr)
            itr->second->SetGoArtKit(artkit);

        bounds = map->GetGameObjectBySpawnIdStore().equal_range(m_Objects[m_TowerType]);

        for (auto itr = bounds.first; itr != bounds.second; ++itr)
            itr->second->SetGoArtKit(artkit2);
    }

    if (field)
        m_PvP->SendUpdateWorldState(field, 1);

    if (m_State == OBJECTIVESTATE_ALLIANCE || m_State == OBJECTIVESTATE_HORDE)
        SendObjectiveComplete(HP_CREDITMARKER[m_TowerType], ObjectGuid::Empty);

    SaveHellfireCapturePointState(
        uint32(m_TowerType),
        uint32(m_State),
        m_value,
        uint32(m_team)
    );

	TeamId ownerTeam = TeamId(TEAM_NEUTRAL);

	if (m_State == OBJECTIVESTATE_ALLIANCE)
		ownerTeam = TEAM_ALLIANCE;
	else if (m_State == OBJECTIVESTATE_HORDE)
		ownerTeam = TEAM_HORDE;

	OutdoorPvpWar_OnHellfireFlagOwnerChanged(uint8(m_TowerType), ownerTeam);
}

void OPvPCapturePointHP::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    switch (m_State)
    {
        case OBJECTIVESTATE_ALLIANCE:
        case OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE:
            packet.Worldstates.emplace_back(HP_MAP_N[m_TowerType], 0);
            packet.Worldstates.emplace_back(HP_MAP_A[m_TowerType], 1);
            packet.Worldstates.emplace_back(HP_MAP_H[m_TowerType], 0);
            break;
        case OBJECTIVESTATE_HORDE:
        case OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE:
            packet.Worldstates.emplace_back(HP_MAP_N[m_TowerType], 0);
            packet.Worldstates.emplace_back(HP_MAP_A[m_TowerType], 0);
            packet.Worldstates.emplace_back(HP_MAP_H[m_TowerType], 1);
            break;
        case OBJECTIVESTATE_NEUTRAL:
        case OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE:
        case OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE:
        default:
            packet.Worldstates.emplace_back(HP_MAP_N[m_TowerType], 1);
            packet.Worldstates.emplace_back(HP_MAP_A[m_TowerType], 0);
            packet.Worldstates.emplace_back(HP_MAP_H[m_TowerType], 0);
            break;
    }
}

void OutdoorPvPHP::HandleKillImpl(Player* player, Unit* killed)
{
    if (killed->GetTypeId() != TYPEID_PLAYER)
        return;

    if (player->GetTeam() == ALLIANCE && killed->ToPlayer()->GetTeam() != ALLIANCE)
        player->CastSpell(player, AlliancePlayerKillReward, true);
    else if (player->GetTeam() == HORDE && killed->ToPlayer()->GetTeam() != HORDE)
        player->CastSpell(player, HordePlayerKillReward, true);
}

uint32 OutdoorPvPHP::GetAllianceTowersControlled() const
{
    return m_AllianceTowersControlled;
}

void OutdoorPvPHP::SetAllianceTowersControlled(uint32 count)
{
    m_AllianceTowersControlled = count;
}

uint32 OutdoorPvPHP::GetHordeTowersControlled() const
{
    return m_HordeTowersControlled;
}

void OutdoorPvPHP::SetHordeTowersControlled(uint32 count)
{
    m_HordeTowersControlled = count;
}

class player_hellfire_server_buff : public PlayerScript
{
public:
    player_hellfire_server_buff() : PlayerScript("player_hellfire_server_buff") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        ApplyHellfireServerBuff(player);
    }

    void OnMapChanged(Player* player) override
    {
        ApplyHellfireServerBuff(player);
    }
};

class OutdoorPvP_hellfire_peninsula : public OutdoorPvPScript
{
public:
    OutdoorPvP_hellfire_peninsula() : OutdoorPvPScript("outdoorpvp_hp") { }

    OutdoorPvP* GetOutdoorPvP() const override
    {
        return new OutdoorPvPHP();
    }
};

void AddSC_outdoorpvp_hp()
{
    new OutdoorPvP_hellfire_peninsula();
    new player_hellfire_server_buff();
}