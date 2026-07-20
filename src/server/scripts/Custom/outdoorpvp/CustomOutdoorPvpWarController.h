#ifndef CUSTOM_OUTDOOR_PVP_WAR_CONTROLLER_H
#define CUSTOM_OUTDOOR_PVP_WAR_CONTROLLER_H

#include "Define.h"
#include "SharedDefines.h"

class Player;

void OutdoorPvpWar_OnHellfireFlagOwnerChanged(uint8 pointIndex, TeamId ownerTeam);
void OutdoorPvpWar_OnSilithusTurnIn(Player* player, uint32 count);

bool OutdoorPvpWar_ConsumeHellfireNeutralResetRequest();
bool OutdoorPvpWar_ConsumeSilithusScoreResetRequest();

bool OutdoorPvpWar_IsHellfireWarActive();
bool OutdoorPvpWar_IsSilithusWarActive();

bool OutdoorPvpWar_ConsumeNagrandNeutralResetRequest();
bool OutdoorPvpWar_IsNagrandWarActive();
void OutdoorPvpWar_OnNagrandStateChanged(uint32 ownerTeam, uint32 guardsAlive);

bool OutdoorPvpWar_IsZangarmarshWarActive();
void OutdoorPvpWar_OnZangarmarshVictory(uint32 winnerTeam);

#endif