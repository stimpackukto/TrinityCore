// quadral
#ifndef ARENA_1V1_H
#define ARENA_1V1_H

#include "DBCStores.h"
#include "ScriptMgr.h"
#include "ArenaTeamMgr.h"
#include "Common.h"
#include "DisableMgr.h"
#include "BattlegroundMgr.h"
#include "Battleground.h"
#include "ArenaTeam.h"
#include "Language.h"
#include "World.h"
#include "Player.h"
#include "Chat.h"
#include "WorldSession.h"
#include "Log.h"
#include "ScriptedGossip.h"
#include "ScriptedCreature.h"
#include "CreatureAI.h"
#include "Config.h"

bool config_Enable;
uint8 config_minLevel;
uint32 config_Costs;
bool config_VendorRating;
float config_ArenaPointsMulti;
bool config_BlockForbiddenTalents;

void MindsearIsMyHero()
{
    config_Enable = sConfigMgr->GetBoolDefault("Arena.1v1.Enable", true);
    config_minLevel = uint8(sConfigMgr->GetIntDefault("Arena.1v1.MinLevel", 80));
    config_Costs = uint32(sConfigMgr->GetIntDefault("Arena.1v1.Costs", 400000));

    config_VendorRating = sConfigMgr->GetBoolDefault("Arena.1v1.VendorRating", false);
    config_ArenaPointsMulti = sConfigMgr->GetFloatDefault("Arena.1v1.ArenaPointsMulti", 0.64f);
    config_BlockForbiddenTalents = sConfigMgr->GetBoolDefault("Arena.1v1.BlockForbiddenTalents", true);
}

// TalentTab.dbc -> TalentTabID
const uint32 FORBIDDEN_TALENTS_IN_1V1_ARENA[] =
{
    // Healer
    201, // PriestDiscipline
    202, // PriestHoly
    382, // PaladinHoly
    262, // ShamanRestoration
    282, // DruidRestoration

    // Tanks
    //383, // PaladinProtection
    //163, // WarriorProtection

    0 // End
};

// Return false, if player have invested more than 35 talentpoints in a forbidden talenttree.
static bool Arena1v1CheckTalents(Player* player)
{
    if (!player)
        return false;

    if (config_BlockForbiddenTalents == false)
        return true;

    uint32 count = 0;

    for (uint32 talentId = 0; talentId < sTalentStore.GetNumRows(); ++talentId)
    {
        TalentEntry const* talentInfo = sTalentStore.LookupEntry(talentId);

        if (!talentInfo)
            continue;

        for (int32 rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
        {
            uint32 rankSpellId = talentInfo->SpellRank[rank];

            if (rankSpellId == 0)
                continue;

            if (player->HasTalent(rankSpellId, player->GetActiveSpec()))
            {
                for (int8 i = 0; FORBIDDEN_TALENTS_IN_1V1_ARENA[i] != 0; ++i)
                {
                    if (FORBIDDEN_TALENTS_IN_1V1_ARENA[i] == talentInfo->TabID)
                        count += uint32(rank + 1);
                }
            }
        }
    }

    if (count >= 36)
    {
        ChatHandler(player->GetSession()).SendSysMessage("1v1 투기장에 참여할 수 없습니다. 금지된 특성 트리에 너무 많은 특성 포인트를 투자했습니다.");
        return false;
    }

    return true;
}

#endif