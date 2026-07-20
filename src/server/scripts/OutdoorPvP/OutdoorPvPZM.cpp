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
 *
 * Custom Update - 2026-06-19
 * - Added custom Zangarmarsh outdoor war flow.
 * - During custom war, both beacons unlock grave guardian phase.
 * - Graveyard can be captured only after guardian NPCs are killed.
 * - Captured graveyard starts attack wave hold phase.
 * - Holding the graveyard for 5 minutes triggers custom victory.
 * - If either beacon is lost during the event, guardians, waves and graveyard control reset.
 */

#include "OutdoorPvPZM.h"
#include "../Custom/outdoorpvp/CustomOutdoorPvpWarController.h"
#include "Creature.h"
#include "GossipDef.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "OutdoorPvPMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldStatePackets.h"

bool OutdoorPvpWar_IsZangarmarshWarActive();
void OutdoorPvpWar_OnZangarmarshVictory(uint32 winnerTeam);

uint8 const OutdoorPvPZMBuffZonesNum = 5;

// the buff is cast in these zones
uint32 const OutdoorPvPZMBuffZones[OutdoorPvPZMBuffZonesNum] = { 3521, 3607, 3717, 3715, 3716 };

// linked when the central tower is controlled
uint32 const ZM_GRAVEYARD_ZONE = 3521;

// linked when the central tower is controlled
uint32 const ZM_GRAVEYARD_ID = 969;

// banners 182527, 182528, 182529, gotta check them ingame
go_type const ZM_Banner_A = { 182527, 530, { 253.54f, 7083.81f, 36.7728f, -0.017453f }, { 0.0f, 0.0f, 0.008727f, -0.999962f } };
go_type const ZM_Banner_H = { 182528, 530, { 253.54f, 7083.81f, 36.7728f, -0.017453f }, { 0.0f, 0.0f, 0.008727f, -0.999962f } };
go_type const ZM_Banner_N = { 182529, 530, { 253.54f, 7083.81f, 36.7728f, -0.017453f }, { 0.0f, 0.0f, 0.008727f, -0.999962f } };

// horde field scout spawn data
creature_type const ZM_HordeFieldScout = { 18564, 530, { 296.625f, 7818.4f, 42.6294f, 5.18363f } };

// alliance field scout spawn data
creature_type const ZM_AllianceFieldScout = { 18581, 530, { 374.395f, 6230.08f, 22.8351f, 0.593412f } };

uint32 const ZM_WAR_GRAVE_ELITE_ENTRY = 0;
uint32 const ZM_WAR_GRAVE_GUARD_ENTRY = 0;
uint32 const ZM_WAR_ALLIANCE_WAVE_ENTRY = 0;
uint32 const ZM_WAR_HORDE_WAVE_ENTRY = 0;

uint32 const ZM_WAR_GRAVE_GUARD_COUNT = 9;
uint32 const ZM_WAR_WAVE_COUNT = 8;
uint32 const ZM_WAR_HOLD_TIME = 5 * 60 * 1000;
uint32 const ZM_WAR_WAVE_RESPAWN_TIME = 60 * 1000;

creature_type const ZM_WarGraveElite = { ZM_WAR_GRAVE_ELITE_ENTRY, 530, { 253.54f, 7083.81f, 36.7728f, 3.14f } };

creature_type const ZM_WarGraveGuards[8] =
{
    { ZM_WAR_GRAVE_GUARD_ENTRY, 530, { 244.54f, 7075.81f, 36.7728f, 0.78f } },
    { ZM_WAR_GRAVE_GUARD_ENTRY, 530, { 253.54f, 7071.81f, 36.7728f, 1.57f } },
    { ZM_WAR_GRAVE_GUARD_ENTRY, 530, { 262.54f, 7075.81f, 36.7728f, 2.35f } },
    { ZM_WAR_GRAVE_GUARD_ENTRY, 530, { 266.54f, 7083.81f, 36.7728f, 3.14f } },
    { ZM_WAR_GRAVE_GUARD_ENTRY, 530, { 262.54f, 7091.81f, 36.7728f, 3.92f } },
    { ZM_WAR_GRAVE_GUARD_ENTRY, 530, { 253.54f, 7095.81f, 36.7728f, 4.71f } },
    { ZM_WAR_GRAVE_GUARD_ENTRY, 530, { 244.54f, 7091.81f, 36.7728f, 5.49f } },
    { ZM_WAR_GRAVE_GUARD_ENTRY, 530, { 240.54f, 7083.81f, 36.7728f, 0.00f } }
};

creature_type const ZM_WarAttackWavePositions[8] =
{
    { 0, 530, { 215.54f, 7048.81f, 36.7728f, 0.70f } },
    { 0, 530, { 226.54f, 7041.81f, 36.7728f, 0.80f } },
    { 0, 530, { 281.54f, 7048.81f, 36.7728f, 2.40f } },
    { 0, 530, { 292.54f, 7058.81f, 36.7728f, 2.50f } },
    { 0, 530, { 292.54f, 7108.81f, 36.7728f, 3.70f } },
    { 0, 530, { 281.54f, 7118.81f, 36.7728f, 3.80f } },
    { 0, 530, { 226.54f, 7118.81f, 36.7728f, 5.40f } },
    { 0, 530, { 215.54f, 7108.81f, 36.7728f, 5.50f } }
};

uint32 const ZM_WarGraveCreatureTypes[9] =
{
    ZM_WAR_GRAVE_ELITE,
    ZM_WAR_GRAVE_GUARD_1,
    ZM_WAR_GRAVE_GUARD_2,
    ZM_WAR_GRAVE_GUARD_3,
    ZM_WAR_GRAVE_GUARD_4,
    ZM_WAR_GRAVE_GUARD_5,
    ZM_WAR_GRAVE_GUARD_6,
    ZM_WAR_GRAVE_GUARD_7,
    ZM_WAR_GRAVE_GUARD_8
};

uint32 const ZM_WarWaveCreatureTypes[8] =
{
    ZM_WAR_WAVE_1,
    ZM_WAR_WAVE_2,
    ZM_WAR_WAVE_3,
    ZM_WAR_WAVE_4,
    ZM_WAR_WAVE_5,
    ZM_WAR_WAVE_6,
    ZM_WAR_WAVE_7,
    ZM_WAR_WAVE_8
};

struct zm_beacon
{
    uint32 ui_tower_n;
    uint32 ui_tower_h;
    uint32 ui_tower_a;
    uint32 map_tower_n;
    uint32 map_tower_h;
    uint32 map_tower_a;
    uint32 event_enter;
    uint32 event_leave;
};

zm_beacon const ZMBeaconInfo[ZM_NUM_BEACONS] =
{
    { 2560, 2559, 2558, 2652, 2651, 2650, 11807, 11806 },
    { 2557, 2556, 2555, 2646, 2645, 2644, 11805, 11804 }
};

uint32 const ZMBeaconCaptureA[ZM_NUM_BEACONS] =
{
    TEXT_EAST_BEACON_TAKEN_ALLIANCE,
    TEXT_WEST_BEACON_TAKEN_ALLIANCE
};

uint32 const ZMBeaconCaptureH[ZM_NUM_BEACONS] =
{
    TEXT_EAST_BEACON_TAKEN_HORDE,
    TEXT_WEST_BEACON_TAKEN_HORDE
};

go_type const ZMCapturePoints[ZM_NUM_BEACONS] =
{
    { 182523, 530, { 303.243f, 6841.36f, 40.1245f, -1.58825f }, { 0.0f, 0.0f, 0.71325f, -0.700909f } },
    { 182522, 530, { 336.466f, 7340.26f, 41.4984f, -1.58825f }, { 0.0f, 0.0f, 0.71325f, -0.700909f } }
};

OPvPCapturePointZM_Beacon::OPvPCapturePointZM_Beacon(OutdoorPvP* pvp, ZM_BeaconType type) : OPvPCapturePoint(pvp), m_TowerType(type), m_TowerState(ZM_TOWERSTATE_N)
{
    SetCapturePointData(ZMCapturePoints[type].entry, ZMCapturePoints[type].map, ZMCapturePoints[type].pos, ZMCapturePoints[type].rot);
}

void OPvPCapturePointZM_Beacon::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(ZMBeaconInfo[m_TowerType].ui_tower_n, (m_TowerState & ZM_TOWERSTATE_N) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(ZMBeaconInfo[m_TowerType].map_tower_n, (m_TowerState & ZM_TOWERSTATE_N) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(ZMBeaconInfo[m_TowerType].ui_tower_a, (m_TowerState & ZM_TOWERSTATE_A) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(ZMBeaconInfo[m_TowerType].map_tower_a, (m_TowerState & ZM_TOWERSTATE_A) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(ZMBeaconInfo[m_TowerType].ui_tower_h, (m_TowerState & ZM_TOWERSTATE_H) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(ZMBeaconInfo[m_TowerType].map_tower_h, (m_TowerState & ZM_TOWERSTATE_H) != 0 ? 1 : 0);
}

void OPvPCapturePointZM_Beacon::UpdateTowerState()
{
    m_PvP->SendUpdateWorldState(uint32(ZMBeaconInfo[m_TowerType].ui_tower_n), uint32((m_TowerState & ZM_TOWERSTATE_N) != 0));
    m_PvP->SendUpdateWorldState(uint32(ZMBeaconInfo[m_TowerType].map_tower_n), uint32((m_TowerState & ZM_TOWERSTATE_N) != 0));
    m_PvP->SendUpdateWorldState(uint32(ZMBeaconInfo[m_TowerType].ui_tower_a), uint32((m_TowerState & ZM_TOWERSTATE_A) != 0));
    m_PvP->SendUpdateWorldState(uint32(ZMBeaconInfo[m_TowerType].map_tower_a), uint32((m_TowerState & ZM_TOWERSTATE_A) != 0));
    m_PvP->SendUpdateWorldState(uint32(ZMBeaconInfo[m_TowerType].ui_tower_h), uint32((m_TowerState & ZM_TOWERSTATE_H) != 0));
    m_PvP->SendUpdateWorldState(uint32(ZMBeaconInfo[m_TowerType].map_tower_h), uint32((m_TowerState & ZM_TOWERSTATE_H) != 0));
}

void OPvPCapturePointZM_Beacon::ChangeState()
{
    // if changing from controlling alliance to horde
    if (m_OldState == OBJECTIVESTATE_ALLIANCE)
    {
        if (uint32 alliance_towers = ((OutdoorPvPZM*)m_PvP)->GetAllianceTowersControlled())
            ((OutdoorPvPZM*)m_PvP)->SetAllianceTowersControlled(--alliance_towers);
    }
    // if changing from controlling horde to alliance
    else if (m_OldState == OBJECTIVESTATE_HORDE)
    {
        if (uint32 horde_towers = ((OutdoorPvPZM*)m_PvP)->GetHordeTowersControlled())
            ((OutdoorPvPZM*)m_PvP)->SetHordeTowersControlled(--horde_towers);
    }

    switch (m_State)
    {
        case OBJECTIVESTATE_ALLIANCE:
        {
            m_TowerState = ZM_TOWERSTATE_A;
            uint32 alliance_towers = ((OutdoorPvPZM*)m_PvP)->GetAllianceTowersControlled();
            if (alliance_towers < ZM_NUM_BEACONS)
                ((OutdoorPvPZM*)m_PvP)->SetAllianceTowersControlled(++alliance_towers);
            m_PvP->SendDefenseMessage(ZM_GRAVEYARD_ZONE, ZMBeaconCaptureA[m_TowerType]);
            break;
        }
        case OBJECTIVESTATE_HORDE:
        {
            m_TowerState = ZM_TOWERSTATE_H;
            uint32 horde_towers = ((OutdoorPvPZM*)m_PvP)->GetHordeTowersControlled();
            if (horde_towers < ZM_NUM_BEACONS)
                ((OutdoorPvPZM*)m_PvP)->SetHordeTowersControlled(++horde_towers);
            m_PvP->SendDefenseMessage(ZM_GRAVEYARD_ZONE, ZMBeaconCaptureH[m_TowerType]);
            break;
        }
        case OBJECTIVESTATE_NEUTRAL:
        case OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE:
        case OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE:
        case OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE:
        case OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE:
            m_TowerState = ZM_TOWERSTATE_N;
            break;
    }

    UpdateTowerState();
}

bool OutdoorPvPZM::Update(uint32 diff)
{
    bool changed = OutdoorPvP::Update(diff);
    if (changed)
    {
        if (m_AllianceTowersControlled == ZM_NUM_BEACONS)
            m_Graveyard->SetBeaconState(ALLIANCE);
        else if (m_HordeTowersControlled == ZM_NUM_BEACONS)
            m_Graveyard->SetBeaconState(HORDE);
        else
            m_Graveyard->SetBeaconState(0);
    }

    if (m_Graveyard)
        m_Graveyard->UpdateCustomWar(diff);

    return changed;
}

void OutdoorPvPZM::HandlePlayerEnterZone(Player* player, uint32 zone)
{
    if (player->GetTeam() == ALLIANCE)
    {
        if (m_Graveyard->GetGraveyardState() & ZM_GRAVEYARD_A)
            player->CastSpell(player, ZM_CAPTURE_BUFF, true);
    }
    else
    {
        if (m_Graveyard->GetGraveyardState() & ZM_GRAVEYARD_H)
            player->CastSpell(player, ZM_CAPTURE_BUFF, true);
    }
    OutdoorPvP::HandlePlayerEnterZone(player, zone);
}

void OutdoorPvPZM::HandlePlayerLeaveZone(Player* player, uint32 zone)
{
    // remove buffs
    player->RemoveAurasDueToSpell(ZM_CAPTURE_BUFF);
    // remove flag
    player->RemoveAurasDueToSpell(ZM_BATTLE_STANDARD_A);
    player->RemoveAurasDueToSpell(ZM_BATTLE_STANDARD_H);
    OutdoorPvP::HandlePlayerLeaveZone(player, zone);
}

OutdoorPvPZM::OutdoorPvPZM()
{
    m_TypeId = OUTDOOR_PVP_ZM;
    m_Graveyard = nullptr;
    m_AllianceTowersControlled = 0;
    m_HordeTowersControlled = 0;
}

bool OutdoorPvPZM::SetupOutdoorPvP()
{
    m_AllianceTowersControlled = 0;
    m_HordeTowersControlled = 0;

    SetMapFromZone(OutdoorPvPZMBuffZones[0]);

    // add the zones affected by the pvp buff
    for (uint8 i = 0; i < OutdoorPvPZMBuffZonesNum; ++i)
        RegisterZone(OutdoorPvPZMBuffZones[i]);

    AddCapturePoint(new OPvPCapturePointZM_Beacon(this, ZM_BEACON_WEST));
    AddCapturePoint(new OPvPCapturePointZM_Beacon(this, ZM_BEACON_EAST));
    m_Graveyard = new OPvPCapturePointZM_Graveyard(this);
    AddCapturePoint(m_Graveyard); // though the update function isn't used, the handleusego is!

    return true;
}

void OutdoorPvPZM::HandleKillImpl(Player* player, Unit* killed)
{
    if (m_Graveyard && m_Graveyard->HandleCustomWarCreatureDeath(killed))
        return;

    if (killed->GetTypeId() != TYPEID_PLAYER)
        return;

    if (player->GetTeam() == ALLIANCE && killed->ToPlayer()->GetTeam() != ALLIANCE)
        player->CastSpell(player, ZM_AlliancePlayerKillReward, true);
    else if (player->GetTeam() == HORDE && killed->ToPlayer()->GetTeam() != HORDE)
        player->CastSpell(player, ZM_HordePlayerKillReward, true);
}

bool OPvPCapturePointZM_Graveyard::Update(uint32 /*diff*/)
{
    bool retval = m_State != m_OldState;
    m_State = m_OldState;
    return retval;
}

int32 OPvPCapturePointZM_Graveyard::HandleOpenGo(Player* player, GameObject* go)
{
    if (IsCustomWarActive() && !IsCustomWarCenterReady())
        return -1;

    int32 retval = OPvPCapturePoint::HandleOpenGo(player, go);
    if (retval >= 0)
    {
        if (player->HasAura(ZM_BATTLE_STANDARD_A) && m_GraveyardState != ZM_GRAVEYARD_A)
        {
            m_GraveyardState = ZM_GRAVEYARD_A;
            DelObject(0);   // only one gotype is used in the whole outdoor pvp, no need to call it a constant
            AddObject(0, ZM_Banner_A.entry, ZM_Banner_A.map, ZM_Banner_A.pos, ZM_Banner_A.rot);
            sObjectMgr->RemoveGraveyardLink(ZM_GRAVEYARD_ID, ZM_GRAVEYARD_ZONE, HORDE);          // rem gy
            sObjectMgr->AddGraveyardLink(ZM_GRAVEYARD_ID, ZM_GRAVEYARD_ZONE, ALLIANCE, false);   // add gy
            m_PvP->TeamApplyBuff(TEAM_ALLIANCE, ZM_CAPTURE_BUFF);
            player->RemoveAurasDueToSpell(ZM_BATTLE_STANDARD_A);
            m_PvP->SendDefenseMessage(ZM_GRAVEYARD_ZONE, TEXT_TWIN_SPIRE_RUINS_TAKEN_ALLIANCE);

            if (IsCustomWarActive())
                StartCustomWarHold(ALLIANCE);
        }
        else if (player->HasAura(ZM_BATTLE_STANDARD_H) && m_GraveyardState != ZM_GRAVEYARD_H)
        {
            m_GraveyardState = ZM_GRAVEYARD_H;
            DelObject(0);   // only one gotype is used in the whole outdoor pvp, no need to call it a constant
            AddObject(0, ZM_Banner_H.entry, ZM_Banner_H.map, ZM_Banner_H.pos, ZM_Banner_H.rot);
            sObjectMgr->RemoveGraveyardLink(ZM_GRAVEYARD_ID, ZM_GRAVEYARD_ZONE, ALLIANCE);          // rem gy
            sObjectMgr->AddGraveyardLink(ZM_GRAVEYARD_ID, ZM_GRAVEYARD_ZONE, HORDE, false);   // add gy
            m_PvP->TeamApplyBuff(TEAM_HORDE, ZM_CAPTURE_BUFF);
            player->RemoveAurasDueToSpell(ZM_BATTLE_STANDARD_H);
            m_PvP->SendDefenseMessage(ZM_GRAVEYARD_ZONE, TEXT_TWIN_SPIRE_RUINS_TAKEN_HORDE);

            if (IsCustomWarActive())
                StartCustomWarHold(HORDE);
        }
        UpdateTowerState();
    }
    return retval;
}

OPvPCapturePointZM_Graveyard::OPvPCapturePointZM_Graveyard(OutdoorPvP* pvp) : OPvPCapturePoint(pvp)
{
    m_BothControllingFaction = 0;
    m_GraveyardState = ZM_GRAVEYARD_N;
    m_FlagCarrierGUID.Clear();

    m_CustomWarState = ZM_WAR_STATE_NONE;
    m_CustomWarBeaconOwner = 0;
    m_CustomWarHoldTimer = 0;
    m_CustomWarWaveTimer = 0;
    m_CustomWarGraveGuardsAlive = 0;

    // add field scouts here
    AddCreature(ZM_ALLIANCE_FIELD_SCOUT, ZM_AllianceFieldScout.entry, ZM_AllianceFieldScout.map, ZM_AllianceFieldScout.pos);
    AddCreature(ZM_HORDE_FIELD_SCOUT, ZM_HordeFieldScout.entry, ZM_HordeFieldScout.map, ZM_HordeFieldScout.pos);
    // add neutral banner
    AddObject(0, ZM_Banner_N.entry, ZM_Banner_N.map, ZM_Banner_N.pos, ZM_Banner_N.rot);
}

void OPvPCapturePointZM_Graveyard::UpdateTowerState()
{
    m_PvP->SendUpdateWorldState(ZM_MAP_GRAVEYARD_N, uint32((m_GraveyardState & ZM_GRAVEYARD_N) != 0));
    m_PvP->SendUpdateWorldState(ZM_MAP_GRAVEYARD_H, uint32((m_GraveyardState & ZM_GRAVEYARD_H) != 0));
    m_PvP->SendUpdateWorldState(ZM_MAP_GRAVEYARD_A, uint32((m_GraveyardState & ZM_GRAVEYARD_A) != 0));

    m_PvP->SendUpdateWorldState(ZM_MAP_ALLIANCE_FLAG_READY, uint32(m_BothControllingFaction == ALLIANCE));
    m_PvP->SendUpdateWorldState(ZM_MAP_ALLIANCE_FLAG_NOT_READY, uint32(m_BothControllingFaction != ALLIANCE));
    m_PvP->SendUpdateWorldState(ZM_MAP_HORDE_FLAG_READY, uint32(m_BothControllingFaction == HORDE));
    m_PvP->SendUpdateWorldState(ZM_MAP_HORDE_FLAG_NOT_READY, uint32(m_BothControllingFaction != HORDE));
}

void OPvPCapturePointZM_Graveyard::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(ZM_MAP_GRAVEYARD_N, (m_GraveyardState & ZM_GRAVEYARD_N) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(ZM_MAP_GRAVEYARD_H, (m_GraveyardState & ZM_GRAVEYARD_H) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(ZM_MAP_GRAVEYARD_A, (m_GraveyardState & ZM_GRAVEYARD_A) != 0 ? 1 : 0);
    packet.Worldstates.emplace_back(ZM_MAP_ALLIANCE_FLAG_READY, m_BothControllingFaction == ALLIANCE ? 1 : 0);
    packet.Worldstates.emplace_back(ZM_MAP_ALLIANCE_FLAG_NOT_READY, m_BothControllingFaction != ALLIANCE ? 1 : 0);
    packet.Worldstates.emplace_back(ZM_MAP_HORDE_FLAG_READY, m_BothControllingFaction == HORDE ? 1 : 0);
    packet.Worldstates.emplace_back(ZM_MAP_HORDE_FLAG_NOT_READY, m_BothControllingFaction != HORDE ? 1 : 0);
}

void OPvPCapturePointZM_Graveyard::SetBeaconState(uint32 controlling_faction)
{
    // nothing to do here
    if (m_BothControllingFaction == controlling_faction)
        return;
    m_BothControllingFaction = controlling_faction;

    switch (controlling_faction)
    {
    case ALLIANCE:
        // if ally already controls the gy and taken back both beacons, return, nothing to do for us
        if (m_GraveyardState & ZM_GRAVEYARD_A)
            return;
        // ally doesn't control the gy, but controls the side beacons -> add gossip option, add neutral banner
        break;
    case HORDE:
        // if horde already controls the gy and taken back both beacons, return, nothing to do for us
        if (m_GraveyardState & ZM_GRAVEYARD_H)
            return;
        // horde doesn't control the gy, but controls the side beacons -> add gossip option, add neutral banner
        break;
    default:
        // if the graveyard is not neutral, then leave it that way
        // if the graveyard is neutral, then we have to dispel the buff from the flag carrier
        if (m_GraveyardState & ZM_GRAVEYARD_N)
        {
            // gy was neutral, thus neutral banner was spawned, it is possible that someone was taking the flag to the gy
            if (!m_FlagCarrierGUID.IsEmpty())
            {
                // remove flag from carrier, reset flag carrier guid
                Player* p = ObjectAccessor::FindPlayer(m_FlagCarrierGUID);
                if (p)
                {
                   p->RemoveAurasDueToSpell(ZM_BATTLE_STANDARD_A);
                   p->RemoveAurasDueToSpell(ZM_BATTLE_STANDARD_H);
                }
                m_FlagCarrierGUID.Clear();
            }
        }
        break;
    }

    if (IsCustomWarActive())
    {
        if (!IsCustomWarBeaconOwnerValid())
            ResetCustomWarBattle();
        else if (m_CustomWarState != ZM_WAR_STATE_NONE && m_CustomWarState != ZM_WAR_STATE_WAIT_BEACONS && m_CustomWarBeaconOwner != m_BothControllingFaction)
            ResetCustomWarBattle();
    }

    // send worldstateupdate
    UpdateTowerState();
}

bool OPvPCapturePointZM_Graveyard::CanTalkTo(Player* player, Creature* c, GossipMenuItems const& /*gso*/)
{
    if (IsCustomWarActive() && !IsCustomWarCenterReady())
        return false;

    std::map<uint32, uint32>::iterator itr = m_CreatureTypes.find(c->GetSpawnId());

    if (itr != m_CreatureTypes.end())
    {
        if (itr->second == ZM_ALLIANCE_FIELD_SCOUT && player->GetTeam() == ALLIANCE && m_BothControllingFaction == ALLIANCE && !m_FlagCarrierGUID && m_GraveyardState != ZM_GRAVEYARD_A)
            return true;
        else if (itr->second == ZM_HORDE_FIELD_SCOUT && player->GetTeam() == HORDE && m_BothControllingFaction == HORDE && !m_FlagCarrierGUID && m_GraveyardState != ZM_GRAVEYARD_H)
            return true;
    }
    return false;
}

bool OPvPCapturePointZM_Graveyard::HandleGossipOption(Player* player, Creature* creature, uint32 /*gossipid*/)
{
    if (IsCustomWarActive() && !IsCustomWarCenterReady())
        return false;

    std::map<uint32, uint32>::iterator itr = m_CreatureTypes.find(creature->GetSpawnId());

    if (itr != m_CreatureTypes.end())
    {
        // if the flag is already taken, then return
        if (!m_FlagCarrierGUID.IsEmpty())
            return true;
        if (itr->second == ZM_ALLIANCE_FIELD_SCOUT)
        {
            creature->CastSpell(player, ZM_BATTLE_STANDARD_A, true);
            m_FlagCarrierGUID = player->GetGUID();
        }
        else if (itr->second == ZM_HORDE_FIELD_SCOUT)
        {
            creature->CastSpell(player, ZM_BATTLE_STANDARD_H, true);
            m_FlagCarrierGUID = player->GetGUID();
        }
        UpdateTowerState();
        player->PlayerTalkClass->SendCloseGossip();
        return true;
    }
    return false;
}

bool OPvPCapturePointZM_Graveyard::HandleDropFlag(Player* /*player*/, uint32 spellId)
{
    switch (spellId)
    {
    case ZM_BATTLE_STANDARD_A:
        m_FlagCarrierGUID.Clear();
        return true;
    case ZM_BATTLE_STANDARD_H:
        m_FlagCarrierGUID.Clear();
        return true;
    }
    return false;
}

uint32 OPvPCapturePointZM_Graveyard::GetGraveyardState() const
{
    return m_GraveyardState;
}

bool OPvPCapturePointZM_Graveyard::IsCustomWarActive() const
{
    return OutdoorPvpWar_IsZangarmarshWarActive();
}

bool OPvPCapturePointZM_Graveyard::IsCustomWarBeaconOwnerValid() const
{
    return m_BothControllingFaction == ALLIANCE || m_BothControllingFaction == HORDE;
}

bool OPvPCapturePointZM_Graveyard::IsCustomWarCenterReady() const
{
    return m_CustomWarState == ZM_WAR_STATE_CENTER_READY;
}

void OPvPCapturePointZM_Graveyard::UpdateCustomWar(uint32 diff)
{
    if (!IsCustomWarActive())
    {
        if (m_CustomWarState != ZM_WAR_STATE_NONE)
            ResetCustomWarBattle();

        return;
    }

    if (!IsCustomWarBeaconOwnerValid())
    {
        if (m_CustomWarState != ZM_WAR_STATE_WAIT_BEACONS)
            ResetCustomWarBattle();

        m_CustomWarState = ZM_WAR_STATE_WAIT_BEACONS;
        return;
    }

    if (m_CustomWarState != ZM_WAR_STATE_NONE && m_CustomWarState != ZM_WAR_STATE_WAIT_BEACONS && m_CustomWarBeaconOwner != m_BothControllingFaction)
    {
        ResetCustomWarBattle();
        return;
    }

    if (m_CustomWarState == ZM_WAR_STATE_NONE || m_CustomWarState == ZM_WAR_STATE_WAIT_BEACONS)
    {
        StartCustomWarGuardPhase(m_BothControllingFaction);
        return;
    }

    if (m_CustomWarState == ZM_WAR_STATE_CENTER_HOLD)
        UpdateCustomWarHold(diff);
}

void OPvPCapturePointZM_Graveyard::ResetCustomWarBattle()
{
    DespawnCustomWarGraveGuards();
    DespawnCustomWarAttackWave();

    m_CustomWarBeaconOwner = 0;
    m_CustomWarHoldTimer = 0;
    m_CustomWarWaveTimer = 0;
    m_CustomWarGraveGuardsAlive = 0;
    m_CustomWarState = IsCustomWarActive() ? ZM_WAR_STATE_WAIT_BEACONS : ZM_WAR_STATE_NONE;

    ResetCustomWarCenterToNeutral();
}

void OPvPCapturePointZM_Graveyard::ResetCustomWarCenterToNeutral()
{
    if (!m_FlagCarrierGUID.IsEmpty())
    {
        if (Player* player = ObjectAccessor::FindPlayer(m_FlagCarrierGUID))
        {
            player->RemoveAurasDueToSpell(ZM_BATTLE_STANDARD_A);
            player->RemoveAurasDueToSpell(ZM_BATTLE_STANDARD_H);
        }

        m_FlagCarrierGUID.Clear();
    }

    m_GraveyardState = ZM_GRAVEYARD_N;

    DelObject(0);
    AddObject(0, ZM_Banner_N.entry, ZM_Banner_N.map, ZM_Banner_N.pos, ZM_Banner_N.rot);

    sObjectMgr->RemoveGraveyardLink(ZM_GRAVEYARD_ID, ZM_GRAVEYARD_ZONE, ALLIANCE);
    sObjectMgr->RemoveGraveyardLink(ZM_GRAVEYARD_ID, ZM_GRAVEYARD_ZONE, HORDE);

    UpdateTowerState();
}

void OPvPCapturePointZM_Graveyard::StartCustomWarGuardPhase(uint32 controllingFaction)
{
    if (!IsCustomWarActive())
        return;

    if (controllingFaction != ALLIANCE && controllingFaction != HORDE)
        return;

    if (m_CustomWarState == ZM_WAR_STATE_GRAVE_GUARDS || m_CustomWarState == ZM_WAR_STATE_CENTER_READY || m_CustomWarState == ZM_WAR_STATE_CENTER_HOLD)
        return;

    ResetCustomWarCenterToNeutral();

    m_CustomWarBeaconOwner = controllingFaction;
    SpawnCustomWarGraveGuards(controllingFaction);

    m_CustomWarState = ZM_WAR_STATE_GRAVE_GUARDS;
}

void OPvPCapturePointZM_Graveyard::SpawnCustomWarGraveGuards(uint32 /*controllingFaction*/)
{
    DespawnCustomWarGraveGuards();

    m_CustomWarGraveGuardsAlive = 0;

    if (ZM_WAR_GRAVE_ELITE_ENTRY != 0)
    {
        AddCreature(ZM_WAR_GRAVE_ELITE, ZM_WarGraveElite.entry, ZM_WarGraveElite.map, ZM_WarGraveElite.pos);
        ++m_CustomWarGraveGuardsAlive;
    }

    for (uint8 i = 0; i < 8; ++i)
    {
        if (ZM_WAR_GRAVE_GUARD_ENTRY == 0)
            continue;

        AddCreature(ZM_WarGraveCreatureTypes[i + 1], ZM_WarGraveGuards[i].entry, ZM_WarGraveGuards[i].map, ZM_WarGraveGuards[i].pos);
        ++m_CustomWarGraveGuardsAlive;
    }

    if (m_CustomWarGraveGuardsAlive == 0)
        UnlockCustomWarCenter();
}

void OPvPCapturePointZM_Graveyard::DespawnCustomWarGraveGuards()
{
    for (uint8 i = 0; i < ZM_WAR_GRAVE_GUARD_COUNT; ++i)
        DelCreature(ZM_WarGraveCreatureTypes[i]);
}

bool OPvPCapturePointZM_Graveyard::HandleCustomWarCreatureDeath(Unit* killed)
{
    if (!killed)
        return false;

    if (!IsCustomWarActive())
        return false;

    if (killed->GetTypeId() != TYPEID_UNIT)
        return false;

    Creature* creature = killed->ToCreature();
    if (!creature)
        return false;

    std::map<uint32, uint32>::iterator itr = m_CreatureTypes.find(creature->GetSpawnId());
    if (itr == m_CreatureTypes.end())
        return false;

    uint32 creatureType = itr->second;

    for (uint8 i = 0; i < ZM_WAR_GRAVE_GUARD_COUNT; ++i)
    {
        if (creatureType != ZM_WarGraveCreatureTypes[i])
            continue;

        if (m_CustomWarGraveGuardsAlive > 0)
            --m_CustomWarGraveGuardsAlive;

        if (m_CustomWarGraveGuardsAlive == 0)
            UnlockCustomWarCenter();

        return true;
    }

    return false;
}

void OPvPCapturePointZM_Graveyard::UnlockCustomWarCenter()
{
    if (!IsCustomWarActive())
        return;

    if (!IsCustomWarBeaconOwnerValid())
    {
        ResetCustomWarBattle();
        return;
    }

    m_CustomWarState = ZM_WAR_STATE_CENTER_READY;
    UpdateTowerState();
}

void OPvPCapturePointZM_Graveyard::StartCustomWarHold(uint32 controllingFaction)
{
    if (!IsCustomWarActive())
        return;

    if (m_CustomWarState != ZM_WAR_STATE_CENTER_READY)
        return;

    if (controllingFaction != m_BothControllingFaction)
    {
        ResetCustomWarBattle();
        return;
    }

    m_CustomWarHoldTimer = 0;
    m_CustomWarWaveTimer = 0;
    m_CustomWarState = ZM_WAR_STATE_CENTER_HOLD;

    SpawnCustomWarAttackWave(controllingFaction);
}

void OPvPCapturePointZM_Graveyard::UpdateCustomWarHold(uint32 diff)
{
    if (!IsCustomWarBeaconOwnerValid())
    {
        ResetCustomWarBattle();
        return;
    }

    if (m_CustomWarBeaconOwner != m_BothControllingFaction)
    {
        ResetCustomWarBattle();
        return;
    }

    uint32 graveyardOwner = 0;

    if (m_GraveyardState & ZM_GRAVEYARD_A)
        graveyardOwner = ALLIANCE;
    else if (m_GraveyardState & ZM_GRAVEYARD_H)
        graveyardOwner = HORDE;

    if (graveyardOwner == 0 || graveyardOwner != m_BothControllingFaction)
    {
        ResetCustomWarBattle();
        return;
    }

    m_CustomWarHoldTimer += diff;
    m_CustomWarWaveTimer += diff;

    if (m_CustomWarWaveTimer >= ZM_WAR_WAVE_RESPAWN_TIME)
    {
        m_CustomWarWaveTimer = 0;
        SpawnCustomWarAttackWave(graveyardOwner);
    }

    if (m_CustomWarHoldTimer >= ZM_WAR_HOLD_TIME)
    {
        OutdoorPvpWar_OnZangarmarshVictory(graveyardOwner);
        ResetCustomWarBattle();
    }
}

void OPvPCapturePointZM_Graveyard::SpawnCustomWarAttackWave(uint32 controllingFaction)
{
    DespawnCustomWarAttackWave();

    uint32 waveEntry = 0;

    if (controllingFaction == ALLIANCE)
        waveEntry = ZM_WAR_HORDE_WAVE_ENTRY;
    else if (controllingFaction == HORDE)
        waveEntry = ZM_WAR_ALLIANCE_WAVE_ENTRY;

    if (waveEntry == 0)
        return;

    for (uint8 i = 0; i < ZM_WAR_WAVE_COUNT; ++i)
        AddCreature(ZM_WarWaveCreatureTypes[i], waveEntry, ZM_WarAttackWavePositions[i].map, ZM_WarAttackWavePositions[i].pos);
}

void OPvPCapturePointZM_Graveyard::DespawnCustomWarAttackWave()
{
    for (uint8 i = 0; i < ZM_WAR_WAVE_COUNT; ++i)
        DelCreature(ZM_WarWaveCreatureTypes[i]);
}

uint32 OutdoorPvPZM::GetAllianceTowersControlled() const
{
    return m_AllianceTowersControlled;
}

void OutdoorPvPZM::SetAllianceTowersControlled(uint32 count)
{
    m_AllianceTowersControlled = count;
}

uint32 OutdoorPvPZM::GetHordeTowersControlled() const
{
    return m_HordeTowersControlled;
}

void OutdoorPvPZM::SetHordeTowersControlled(uint32 count)
{
    m_HordeTowersControlled = count;
}

void OutdoorPvPZM::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(ZM_WORLDSTATE_UNK_1, 1);

    for (OPvPCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        itr->second->FillInitialWorldStates(packet);
}

void OutdoorPvPZM::SendRemoveWorldStates(Player* player)
{
    player->SendUpdateWorldState(ZM_WORLDSTATE_UNK_1, 1);
    player->SendUpdateWorldState(ZM_UI_TOWER_EAST_N, 0);
    player->SendUpdateWorldState(ZM_UI_TOWER_EAST_H, 0);
    player->SendUpdateWorldState(ZM_UI_TOWER_EAST_A, 0);
    player->SendUpdateWorldState(ZM_UI_TOWER_WEST_N, 0);
    player->SendUpdateWorldState(ZM_UI_TOWER_WEST_H, 0);
    player->SendUpdateWorldState(ZM_UI_TOWER_WEST_A, 0);
    player->SendUpdateWorldState(ZM_MAP_TOWER_EAST_N, 0);
    player->SendUpdateWorldState(ZM_MAP_TOWER_EAST_H, 0);
    player->SendUpdateWorldState(ZM_MAP_TOWER_EAST_A, 0);
    player->SendUpdateWorldState(ZM_MAP_GRAVEYARD_H, 0);
    player->SendUpdateWorldState(ZM_MAP_GRAVEYARD_A, 0);
    player->SendUpdateWorldState(ZM_MAP_GRAVEYARD_N, 0);
    player->SendUpdateWorldState(ZM_MAP_TOWER_WEST_N, 0);
    player->SendUpdateWorldState(ZM_MAP_TOWER_WEST_H, 0);
    player->SendUpdateWorldState(ZM_MAP_TOWER_WEST_A, 0);
    player->SendUpdateWorldState(ZM_MAP_HORDE_FLAG_READY, 0);
    player->SendUpdateWorldState(ZM_MAP_HORDE_FLAG_NOT_READY, 0);
    player->SendUpdateWorldState(ZM_MAP_ALLIANCE_FLAG_NOT_READY, 0);
    player->SendUpdateWorldState(ZM_MAP_ALLIANCE_FLAG_READY, 0);
}

class OutdoorPvP_zangarmarsh : public OutdoorPvPScript
{
    public:
        OutdoorPvP_zangarmarsh() : OutdoorPvPScript("outdoorpvp_zm") { }

        OutdoorPvP* GetOutdoorPvP() const override
        {
            return new OutdoorPvPZM();
        }
};

void AddSC_outdoorpvp_zm()
{
    new OutdoorPvP_zangarmarsh();
}