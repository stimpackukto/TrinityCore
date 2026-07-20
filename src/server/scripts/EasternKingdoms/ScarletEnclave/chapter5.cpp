/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Custom Update - 2026-06-30
 *
 * The Light of Dawn event stability fix
 *
 * - NPCChangeTarget no longer depends only on Darion's threat list.
 * - Dawn and Scourge NPCs now re-target each other directly.
 * - Fixed swapped summon entries for Abomination and Warrior.
 * - Added safe quest objective fallback when credit spell does not update quest.
 * - Fixed Thassarian/Koltira returning to original spawn by locking battle home position.
 * - Removed forced reverse AttackStart to prevent existing spawned NPC evade reset.
 */

/*
 * Custom Update - 2026-07-06
 *
 * The Light of Dawn event scheduler rebuild
 *
 * - Replaced manual diff countdown blocks with TaskScheduler reservations.
 * - Scene progression now schedules the next step instead of polling a phase timer.
 * - Combat spells, speech, spawn refresh, target refresh, leash, Tirion pre-end summon,
 *   and battle finish are scheduled tasks.
 * - Event serial guards prevent old scheduled tasks from affecting the next event run.
 * - Added cleanup-side state invalidation and gossip flag restoration.
 */

/*
 * Custom Update - 2026-07-06
 *
 * The Light of Dawn post-battle step 10 resume fix
 *
 * - Delays Tirion's battle-end shortcut to the real 295 second point instead of 5 seconds.
 * - Adds a guarded post-battle fallback so the scene cannot remain stuck at step 10
 *   when EscortAI does not reach waypoint 2/3 after battle finish.
 */

/*
 * Custom Update - 2026-07-05
 *
 * The Light of Dawn event completion safety fix
 *
 * - Quest completion distance now uses the Light's Hope Chapel battlefield center,
 *   not Darion's current position.
 * - Added Darion position correction when battle ends too far from the battlefield.
 * - Added timeout fallback for escort waypoint dependent steps 34 and 36.
 * - Guarded late waypoint callbacks so they cannot pause the escort after fallback.
 * - Added SummonCreature null checks for battle setup, reinforce spawns, and charge scene summons.
 * - Shortened the final post-Lich-King dialogue sequence so the event does not look stalled.
 * - Keeps Tirion alive through the chapel drag scene and restores him if the summon disappeared.
 * - Reduced battle spread and retarget frequency so knockbacks are less likely to push Darion off path.
 * - Added a battlefield leash so combat NPCs cannot remain outside the chapel battle radius.
 */

/*
 * Custom Update - 2026-07-07
 *
 * The Light of Dawn normal scene case order restore
 *
 * - Restored scene case numbers to the normal database/dialogue order.
 * - Moved Darion SAY53 chapel handoff preparation back to case 46.
 * - Restored Lich King summon sequence to case 29 after memory Darion death and Alexandros SAY42.
 * - Re-aligned waypoint 6 and 7 guards with charge wait case 34 and knockback wait case 36.
 */

/*
 * Custom Update - 2026-07-06
 *
 * The Light of Dawn scene step number reorder
 *
 * - Reordered scene case numbers to match the actual cinematic progression.
 * - Moved Darion chapel reposition from old step 46 to new step 11.
 * - Moved the Lich King preparation summon from old step 29 to new step 28.
 * - Shifted dependent scene steps and waypoint/fallback guards to the new numbering.
 */

/*
 * Custom Update - 2026-07-07
 *
 * The Light of Dawn ExecuteSceneStep integration
 *
 * - Scene progress no longer depends on escort waypoint callbacks.
 * - Battle start, post-battle return, chapel handoff, charge, and knockback waits
 *   are executed directly through ExecuteSceneStep.
 * - WaypointReached is kept as a passive log-only guard so database path timing
 *   cannot advance or pause the event out of order.
 */

/*
 * Custom Update - 2026-07-07
 *
 * The Light of Dawn direct approach arrival guard
 *
 * - Case 9 now only starts the battlefield approach movement and keeps Darion mounted.
 * - Case 10 can start battle only after Darion actually reaches the battlefield anchor.
 * - Removed the early case 10 teleport path that made the scene look like it had already arrived.
 */

/*
 * Custom Update - 2026-07-07
 *
 * The Light of Dawn post-battle Tirion position fix
 *
 * - Tirion is now locked to the battlefield anchor after battle finish, not the later
 *   Ashbringer handoff position.
 * - Existing Tirion movement paths are cleared before scene position correction so an
 *   old MovePoint cannot pull him away after teleport correction.
 */

/*
 * Custom Update - 2026-07-07
 *
 * The Light of Dawn post-battle death knight restore fix
 *
 * - Restores Koltira, Orbaz, and Thassarian to alive, standing, mounted, non-combat state
 *   before moving them out after battlefield trash cleanup.
 * - Clears stale threat, target, casts, and motion so dead/knockback states cannot be carried
 *   into the chapel return sequence.
 */

/*
 * Custom Update - 2026-07-07
 *
 * The Light of Dawn Darion battle swap and Tirion pull fix
 *
 * - Keeps the quest Darion as the scene controller before combat, then swaps to a summoned
 *   combat Darion when the battle starts so Mograine can actually fight.
 * - Keeps the combat Darion active through the post-battle scene and swaps back to the
 *   quest Darion immediately before quest credit.
 * - Battle end now has Tirion apply the Light of Dawn pull spell to Mograine, Koltira,
 *   Orbaz, and Thassarian before they move to their scene positions instead of relying on
 *   delete/teleport correction.
 */

/*
 * Custom Update - 2026-07-07
 *
 * The Light of Dawn Darion GUID separation fix
 *
 * - Splits memory Darion and final summoned Darion into separate GUID variables.
 * - uiMemoryDarionGUID is used only for NPC_DARION_MOGRAINE memory scene.
 */

/*
 * Custom Update - 2026-07-07
 *
 * The Light of Dawn direct case scene rebuild
 *
 * - Tirion and Darion scene handling is now written directly inside each scene case.
 * - Removed scene-case dependency on shared Darion/Tirion helper calls so each case shows
 *   exactly which creature is selected, summoned, reset, moved, hidden, or talked.
 * - Case 24 now guarantees Tirion exists before the Lich King scene, preventing the
 *   late case 46 reappearance.
 * - Case 30 keeps Alexandros, Tirion, and scene Darion visible during the Lich King entrance.
 */

/*
 * Custom Update - 2026-07-07
 *
 * The Light of Dawn direct case step rebuild follow-up
 *
 * - Inlines Darion combat swap in case 10 instead of hiding it behind a helper.
 * - Inlines quest Darion restoration in case 71 instead of using a restore helper.
 * - Keeps memory Darion, combat Darion, final Darion, quest Darion, Tirion, Alexandros,
 *   and Lich King visible in the scene cases where they are created, moved, hidden, or restored.
 */

/*
 * Custom Update - 2026-07-07
 *
 * The Light of Dawn direct speech target safety fix
 *
 * - Case 11, 12, and 13 now directly restore Tirion inside each case before speaking.
 * - Case 14 now directly restores the active Darion speaker before speaking.
 * - Scene steps no longer advance silently when the required speech actor is missing.
 */

/*
 * Custom Update - 2026-07-07
 *
 * The Light of Dawn case NPC state file logging
 *
 * - Removes player chat step logging from the event script.
 * - Writes each executed scene case and important NPC state to
 *   /home/esotar/log/light_of_dawn_case_npc_state.log.
 * - Keeps existing scene flow, timers, summons, movement, and dialogue logic unchanged.
 */

/*
 * Custom Update - 2026-07-08
 *
 * The Light of Dawn Darion post-battle position fix
 *
 * - Keeps the combat Darion recovery/safety blocks on LightofDawnLoc[24].
 * - Prevents later Darion restore blocks from pulling him back to LightofDawnLoc[1]
 *   after FinishLightOfDawnBattle moves him to the chapel Darion scene position.
 */

/*
 * Custom Update - 2026-07-08
 *
 * The Light of Dawn summon variable naming cleanup
 *
 * - Replaced generic local creature variables with role-specific creature names.
 * - Keeps GUID-based creature re-fetch, summon timing, movement, dialogue, and cleanup logic unchanged.
 */

/*
 * Custom Update - 2026-07-08
 *
 * The Light of Dawn single Darion GUID fix
 *
 * - Keeps memory Darion as uiMemoryDarionGUID for the flashback scene only.
 * - Removes separate battle/final Darion GUID handling.
 * - Uses the controller Darion (me) from event start through cleanup case 73.
 * - Stops combat/final Darion re-summons and restores me directly for battle, scene, and quest credit.
 */

/*
 * Custom Update - 2026-07-08
 *
 * The Light of Dawn temporary operation log pause
 *
 * - Disabled the temporary case/NPC state file logging for live operation.
 * - Kept the logging helper functions in place so they can be restored later if needed.
 */

#include <fstream>

#include "ScriptMgr.h"
#include "GameObject.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "TaskScheduler.h"

#define LESS_MOB // if you do not have a good server and do not want it to be laggy as hell

//Light of Dawn
enum mograine
{
#ifdef LESS_MOB
    ENCOUNTER_DK_NUMBER               = 5,
    ENCOUNTER_DK_TIMER                = 10,
    ENCOUNTER_DEFENDER_NUMBER         = 10,
    ENCOUNTER_EARTHSHATTER_NUMBER     = 1,
    ENCOUNTER_ABOMINATION_NUMBER      = 2,
    ENCOUNTER_BEHEMOTH_NUMBER         = 1,
    ENCOUNTER_GHOUL_NUMBER            = 5,
    ENCOUNTER_WARRIOR_NUMBER          = 1,
#else
    ENCOUNTER_DK_NUMBER               = 5,
    ENCOUNTER_DK_TIMER                = 10,
    ENCOUNTER_DEFENDER_NUMBER         = 20,
    ENCOUNTER_EARTHSHATTER_NUMBER     = 20,
    ENCOUNTER_ABOMINATION_NUMBER      = 3,
    ENCOUNTER_BEHEMOTH_NUMBER         = 2,
    ENCOUNTER_GHOUL_NUMBER            = 10,
    ENCOUNTER_WARRIOR_NUMBER          = 2,
#endif
    ENCOUNTER_TOTAL_DAWN              = 300,
    ENCOUNTER_TOTAL_SCOURGE           = 10000,

    QUEST_THE_LIGHT_OF_DAWN           = 12801,

    WORLD_STATE_REMAINS               = 3592,
    WORLD_STATE_COUNTDOWN             = 3603,
    WORLD_STATE_EVENT_BEGIN           = 3605,

    SAY_LIGHT_OF_DAWN01               = 0,
    SAY_LIGHT_OF_DAWN02               = 1,
    SAY_LIGHT_OF_DAWN03               = 2,
    SAY_LIGHT_OF_DAWN04               = 3,
    SAY_LIGHT_OF_DAWN05               = 4,
    SAY_LIGHT_OF_DAWN06               = 5,
    SAY_LIGHT_OF_DAWN07               = 6,
    SAY_LIGHT_OF_DAWN08               = 7,
    SAY_LIGHT_OF_DAWN09               = 8,
    SAY_LIGHT_OF_DAWN25               = 24,
    SAY_LIGHT_OF_DAWN26               = 25,
    SAY_LIGHT_OF_DAWN27               = 26,
    SAY_LIGHT_OF_DAWN28               = 27,
    SAY_LIGHT_OF_DAWN29               = 28,
    SAY_LIGHT_OF_DAWN30               = 29,
    SAY_LIGHT_OF_DAWN31               = 30,
    SAY_LIGHT_OF_DAWN32               = 31,
    SAY_LIGHT_OF_DAWN33               = 32,
    SAY_LIGHT_OF_DAWN34               = 33,
    SAY_LIGHT_OF_DAWN35               = 34,
    SAY_LIGHT_OF_DAWN36               = 35,
    SAY_LIGHT_OF_DAWN37               = 36,
    SAY_LIGHT_OF_DAWN38               = 37,
    SAY_LIGHT_OF_DAWN39               = 38,
    SAY_LIGHT_OF_DAWN40               = 39,
    SAY_LIGHT_OF_DAWN41               = 40,
    SAY_LIGHT_OF_DAWN42               = 41,
    SAY_LIGHT_OF_DAWN43               = 42,
    SAY_LIGHT_OF_DAWN44               = 43,
    SAY_LIGHT_OF_DAWN45               = 44,
    SAY_LIGHT_OF_DAWN46               = 45,
    SAY_LIGHT_OF_DAWN47               = 46,
    SAY_LIGHT_OF_DAWN48               = 47,
    SAY_LIGHT_OF_DAWN49               = 48,
    SAY_LIGHT_OF_DAWN50               = 49,
    SAY_LIGHT_OF_DAWN51               = 50,
    SAY_LIGHT_OF_DAWN52               = 51,
    SAY_LIGHT_OF_DAWN53               = 52,
    SAY_LIGHT_OF_DAWN54               = 53,
    SAY_LIGHT_OF_DAWN55               = 54,
    SAY_LIGHT_OF_DAWN56               = 55,
    SAY_LIGHT_OF_DAWN57               = 56,
    SAY_LIGHT_OF_DAWN58               = 57,
    SAY_LIGHT_OF_DAWN59               = 58,
    SAY_LIGHT_OF_DAWN60               = 59,
    SAY_LIGHT_OF_DAWN61               = 60,
    SAY_LIGHT_OF_DAWN62               = 61,
    SAY_LIGHT_OF_DAWN63               = 62,
    SAY_LIGHT_OF_DAWN64               = 63,
    SAY_LIGHT_OF_DAWN65               = 64,
    SAY_LIGHT_OF_DAWN66               = 65,
    SAY_LIGHT_OF_DAWN67               = 66,
    SAY_LIGHT_OF_DAWN68               = 67,

    EMOTE_LIGHT_OF_DAWN01             = 68,
    EMOTE_LIGHT_OF_DAWN02             = 69,
    EMOTE_LIGHT_OF_DAWN03             = 70,
    EMOTE_LIGHT_OF_DAWN04             = 71,
    EMOTE_LIGHT_OF_DAWN05             = 72,
    EMOTE_LIGHT_OF_DAWN06             = 73,
    EMOTE_LIGHT_OF_DAWN07             = 74,
    EMOTE_LIGHT_OF_DAWN08             = 75,
    EMOTE_LIGHT_OF_DAWN09             = 76,
    EMOTE_LIGHT_OF_DAWN10             = 77,
    EMOTE_LIGHT_OF_DAWN11             = 78,
    EMOTE_LIGHT_OF_DAWN12             = 79,
    EMOTE_LIGHT_OF_DAWN13             = 80,
    EMOTE_LIGHT_OF_DAWN14             = 81,
    EMOTE_LIGHT_OF_DAWN15             = 82,
    EMOTE_LIGHT_OF_DAWN16             = 83,
    EMOTE_LIGHT_OF_DAWN17             = 84,
    EMOTE_LIGHT_OF_DAWN18             = 85,

    GO_LIGHT_OF_DAWN                  = 191330,
    SPELL_THE_LIGHT_OF_DAWN_Q         = 53606,

    NPC_HIGHLORD_DARION_MOGRAINE      = 29173,
    SPELL_ANTI_MAGIC_ZONE1            = 52893,
    SPELL_DEATH_STRIKE                = 53639,
    SPELL_DEATH_EMBRACE               = 53635,
    SPELL_ICY_TOUCH1                  = 49723,
    SPELL_THE_LIGHT_OF_DAWN           = 53658,
    SPELL_THE_MIGHT_OF_MOGRAINE       = 53642,
    SPELL_UNHOLY_BLIGHT               = 53640,
    SPELL_ALEXANDROS_MOGRAINE_SPAWN   = 53667,
    SPELL_MOGRAINE_CHARGE             = 53679,
    SPELL_ASHBRINGER                  = 53701,

    NPC_KOLTIRA_DEATHWEAVER           = 29199,
    NPC_ORBAZ_BLOODBANE               = 29204,
    NPC_THASSARIAN                    = 29200,
    SPELL_BLOOD_STRIKE1               = 52374,
    SPELL_DEATH_GRIP                  = 49576,
    SPELL_ICY_TOUCH2                  = 52372,
    SPELL_PLAGUE_STRIKE1              = 50668,

    NPC_THE_LICH_KING                 = 29183,
    SPELL_APOCALYPSE                  = 53210,
    SPELL_TELEPORT_VISUAL             = 52233,
    SPELL_SOUL_FEAST_ALEX             = 53677,
    SPELL_SOUL_FEAST_TIRION           = 53685,
    SPELL_ICEBOUND_VISAGE             = 53274,
    SPELL_REBUKE                      = 53680,

    NPC_RAMPAGING_ABOMINATION         = 29186,
    SPELL_CLEAVE1                     = 53633,
    SPELL_SCOURGE_HOOK                = 50335,
    SPELL_SCOURGE_AGGRO_AURA          = 53624,

    NPC_FLESH_BEHEMOTH                = 29190,
    SPELL_STOMP                       = 53634,
    SPELL_THUNDERCLAP                 = 36706,
    SPELL_HERO_AGGRO_AURA             = 53627,

    NPC_ACHERUS_GHOUL                 = 29219,
    SPELL_GHOULPLOSION                = 53632,

    NPC_WARRIOR_OF_THE_FROZEN_WASTES  = 29206,

    NPC_HIGHLORD_ALEXANDROS_MOGRAINE  = 29227,
    NPC_DARION_MOGRAINE               = 29228,

    NPC_HIGHLORD_TIRION_FORDRING      = 29175,
    EQUIP_HIGHLORD_TIRION_FORDRING    = 13262,
    SPELL_LAY_ON_HANDS                = 53778,
    SPELL_REBIRTH_OF_THE_ASHBRINGER   = 53702,
    SPELL_TIRION_CHARGE               = 53705,
    SPELL_TIRION_CHARGE_VISUAL        = 53706,

    NPC_KORFAX_CHAMPION_OF_THE_LIGHT  = 29176,
    SPELL_CLEAVE                      = 53631,
    SPELL_HEROIC_LEAP                 = 53625,

    NPC_LORD_MAXWELL_TYROSUS          = 29178,
    NPC_LEONID_BARTHALOMEW_THE_REVERED = 29179,
    NPC_DUKE_NICHOLAS_ZVERENHOFF      = 29180,

    NPC_COMMANDER_ELIGOR_DAWNBRINGER  = 29177,
    SPELL_HOLY_LIGHT2                 = 37979,

    NPC_RAYNE                         = 29181,
    SPELL_REJUVENATION                = 20664,
    SPELL_STARFALL                    = 20678,
    SPELL_TRANQUILITY                 = 25817,
    SPELL_WRATH                       = 21807,

    NPC_DEFENDER_OF_THE_LIGHT         = 29174,
    SPELL_HOLY_LIGHT1                 = 29427,
    SPELL_HOLY_STRIKE                 = 53643,
    SPELL_HOLY_WRATH                  = 53638,
    SPELL_UPPERCUT                    = 53629,

    NPC_RIMBLAT_EARTHSHATTER          = 29182,
    SPELL_CHAIN_HEAL                  = 33642,
    SPELL_THUNDER                     = 53630
};

void UpdateWorldState(Map* map, uint32 id, uint32 state)
{
    Map::PlayerList const& players = map->GetPlayers();

    if (!players.isEmpty())
    {
        for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        {
            if (Player* player = itr->GetSource())
                player->SendUpdateWorldState(id, state);
        }
    }
}

Position const LightofDawnLoc[] =
{
    {2281.335f, -5300.409f, 85.170f, 1.55f},     // 0 Tirion Fordring loc
    {2283.896f, -5287.914f, 83.066f, 1.55f}, // 1 Tirion Fordring loc2
    {2281.461f, -5263.014f, 81.164f, 0},     // 2 Tirion charges
    {2262.277f, -5293.477f, 82.167f, 0},     // 3 Tirion run
    {2270.286f, -5287.73f, 82.262f, 0},      // 4 Tirion relocate
    {2269.511f, -5288.289f, 82.225f, 0},     // 5 Tirion forward
    {2262.277f, -5293.477f, 82.167f, 0},     // 6 Tirion runs to Darion
    {2270.286f, -5287.73f, 82.262f, 0},
    {2269.511f, -5288.289f, 82.225f, 0},
    {2273.205f, -5288.848f, 82.617f, 0},     // 9 Korfax loc1
    {2274.739f, -5287.926f, 82.684f, 0},     // 10 Korfax loc2
    {2253.673f, -5318.004f, 81.724f, 0},     // 11 Korfax kicked
    {2287.028f, -5309.644f, 87.253f, 0},     // 12 Maxwell loc1
    {2286.978f, -5308.025f, 86.83f, 0},      // 13 Maxwell loc2
    {2248.877f, -5307.586f, 82.166f, 0},     // 14 maxwell kicked
    {2278.58f, -5316.933f, 88.319f, 0},      // 15 Eligor loc1
    {2278.535f, -5315.479f, 88.08f, 0},      // 16 Eligor loc2
    {2259.416f, -5304.505f, 82.149f, 0},     // 17 eligor kicked
    {2289.259f, -5280.355f, 82.112f, 0},     // 18 Koltira loc1
    {2289.02f, -5281.985f, 82.207f, 0},      // 19 Koltira loc2
    {2273.289f, -5273.675f, 81.701f, 0},     // 20 Thassarian loc1
    {2273.332f, -5275.544f, 81.849f, 0},     // 21 Thassarian loc2
    {2281.198f, -5257.397f, 80.224f, 4.66f}, // 22 Alexandros loc1
    {2281.156f, -5259.934f, 80.647f, 0},     // 23 Alexandros loc2
    {2281.294f, -5281.895f, 82.445f, 1.35f}, // 24 Darion loc1
    {2281.093f, -5263.013f, 81.125f, 0},     // 25 Darion loc1
    {2281.313f, -5250.282f, 79.322f, 4.69f}, // 26 Lich King spawns
    {2281.523f, -5261.058f, 80.877f, 0},     // 27 Lich king move forwards
    {2272.709f, -5255.552f, 78.226f, 0},     // 28 Lich king kicked
    {2273.972f, -5257.676f, 78.862f, 0},     // 29 Lich king moves forward
	{2281.293f, -5281.895f,	82.444f, 0},     // 30 전투지역
};

static Position const OrbazBloodbaneReturnLoc = {2287.303223f, -5273.214844f, 81.898872f, 4.085474f};

static constexpr float LIGHT_OF_DAWN_COMPLETION_RADIUS = 700.0f;
static constexpr float LIGHT_OF_DAWN_BATTLE_TARGET_RADIUS = 40.0f;
static constexpr float LIGHT_OF_DAWN_BATTLE_LEASH_RADIUS = 45.0f;
static constexpr uint32 LIGHT_OF_DAWN_TARGET_CHECK_TIMER = 20000;
static constexpr uint32 LIGHT_OF_DAWN_BATTLE_LEASH_TIMER = 1000;
static constexpr uint32 LIGHT_OF_DAWN_BATTLE_APPROACH_CHECK_TIMER = 2000;
static constexpr uint32 LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET = 15;
//static constexpr uint32 LIGHT_OF_DAWN_TIRION_MESSAGE_SKIP_TIMER = 15000; 15초 전투

static constexpr uint32 LIGHT_OF_DAWN_TIRION_MESSAGE_SKIP_TIMER = 300000;
static constexpr char const* LIGHT_OF_DAWN_CASE_STATE_LOG_FILE = "/home/esotar/log/light_of_dawn_case_npc_state.log";

static bool IsWithinLightOfDawnRadius(Unit const* unit, Position const& position, float radius)
{
    if (!unit)
        return false;

    float const dx = unit->GetPositionX() - position.GetPositionX();
    float const dy = unit->GetPositionY() - position.GetPositionY();
    float const dz = unit->GetPositionZ() - position.GetPositionZ();

    return (dx * dx + dy * dy + dz * dz) <= radius * radius;
}

static bool IsNearLightOfDawnBattlefield(Player const* player)
{
    return IsWithinLightOfDawnRadius(player, LightofDawnLoc[0], LIGHT_OF_DAWN_COMPLETION_RADIUS);
}

static constexpr uint32 PATH_ESCORT_MOGRAINE = 233386;

class npc_highlord_darion_mograine : public CreatureScript
{
public:
    npc_highlord_darion_mograine() : CreatureScript("npc_highlord_darion_mograine") { }

    struct npc_highlord_darion_mograineAI : public EscortAI
    {
        npc_highlord_darion_mograineAI(Creature* creature) : EscortAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            bIsBattle = false;
            uiStep = 0;
            uiTotal_dawn = ENCOUNTER_TOTAL_DAWN;
            uiTotal_scourge = ENCOUNTER_TOTAL_SCOURGE;
            uiSummon_counter = 0;
            bTirionChapelHandOffDone = false;
            bSceneStepReady = false;
            bBattleFinishRequested = false;
            bBattleApproachStarted = false;
            bBattleStartedForScene = false;
            bBattleFinishedForScene = false;
            ++uiEventSerial;
        }

        bool bIsBattle;
        uint32 uiStep;
        uint32 uiTotal_dawn;
        uint32 uiTotal_scourge;
        uint32 uiSummon_counter;
        bool bTirionChapelHandOffDone;
        bool bSceneStepReady;
        bool bBattleFinishRequested;
        bool bBattleApproachStarted;
        bool bBattleStartedForScene;
        bool bBattleFinishedForScene;
        uint32 uiEventSerial = 0;
        TaskScheduler _scheduler;

        ObjectGuid uiTirionGUID;
        ObjectGuid uiAlexandrosGUID;
        ObjectGuid uiMemoryDarionGUID;
        ObjectGuid uiKorfaxGUID;
        ObjectGuid uiMaxwellGUID;
        ObjectGuid uiEligorGUID;
        ObjectGuid uiRayneGUID;
        ObjectGuid uiDefenderGUID[ENCOUNTER_DEFENDER_NUMBER];
        ObjectGuid uiEarthshatterGUID[ENCOUNTER_EARTHSHATTER_NUMBER];

        ObjectGuid uiKoltiraGUID;
        ObjectGuid uiOrbazGUID;
        ObjectGuid uiThassarianGUID;
        ObjectGuid uiLichKingGUID;
        ObjectGuid uiAbominationGUID[ENCOUNTER_ABOMINATION_NUMBER];
        ObjectGuid uiBehemothGUID[ENCOUNTER_BEHEMOTH_NUMBER];
        ObjectGuid uiGhoulGUID[ENCOUNTER_GHOUL_NUMBER];
        ObjectGuid uiWarriorGUID[ENCOUNTER_WARRIOR_NUMBER];

        void SendStepChat(std::string const& /*message*/)
        {
            // 채팅창 로그는 더 이상 사용하지 않는다.
            // case/NPC 상태 기록은 StepChat()에서 파일로만 남긴다.
        }

        std::string GuidState(ObjectGuid const& guid) const
        {
            return guid.IsEmpty() ? "empty" : guid.ToString();
        }

        std::string CreatureState(Creature* creature, char const* label) const
        {
            if (!creature)
                return Trinity::StringFormat("{} guid=empty exists=no alive=no", label);

            return Trinity::StringFormat("{} entry={} guid={} exists=yes alive={} visible={} combat={} x={} y={} z={}",
                label,
                creature->GetEntry(),
                creature->GetGUID().ToString(),
                creature->IsAlive() ? "yes" : "no",
                creature->IsVisible() ? "yes" : "no",
                creature->IsInCombat() ? "yes" : "no",
                creature->GetPositionX(),
                creature->GetPositionY(),
                creature->GetPositionZ());
        }

        std::string CreatureState(ObjectGuid const& guid, char const* label) const
        {
            Creature* creature = ObjectAccessor::GetCreature(*me, guid);

            if (!creature)
                return Trinity::StringFormat("{} guid={} exists=no alive=no", label, GuidState(guid));

            return CreatureState(creature, label);
        }

        std::string CreatureArrayState(ObjectGuid const* guids, uint32 count, char const* label) const
        {
            uint32 existsCount = 0;
            uint32 aliveCount = 0;
            uint32 combatCount = 0;

            for (uint32 i = 0; i < count; ++i)
            {
                if (Creature* creature = ObjectAccessor::GetCreature(*me, guids[i]))
                {
                    ++existsCount;

                    if (creature->IsAlive())
                        ++aliveCount;

                    if (creature->IsInCombat())
                        ++combatCount;
                }
            }

            return Trinity::StringFormat("{} total={} exists={} alive={} combat={} missing={}",
                label,
                count,
                existsCount,
                aliveCount,
                combatCount,
                count - existsCount);
        }

        void StartCaseNpcStateLog()
        {
            // 실제 운영 중에는 임시 case/NPC 상태 파일 로그를 남기지 않는다.
            // 필요하면 이 함수와 StepChat()만 다시 원래 방식으로 되돌리면 된다.
        }

        void WriteCaseNpcStateLog()
        {
            // 실제 운영 중에는 임시 case/NPC 상태 파일 로그를 남기지 않는다.
        }

        void StepChat()
        {
            // 실제 운영 중에는 임시 case/NPC 상태 파일 로그를 남기지 않는다.
        }

        char const* GetSceneStepName(uint32 step) const
        {
            switch (step)
            {
                case 0: return "대기";
                case 1: return "이벤트 시작";
                case 2: return "다리온 SAY04 - 스컬지 병사 집결";
                case 3: return "다리온 SAY05 - 일어나라";
                case 4: return "구울 준비 병력 소환";
                case 5: return "누더기골렘 준비 병력 소환";
                case 6: return "전사 준비 병력 소환";
                case 7: return "거수 준비 병력 소환";
                case 8: return "다리온 SAY06 - 전투 준비 대사";
                case 9: return "호위 병력 전장 이동 시작";
                case 10: return "전투 시작/종료 후 직접 전환";
                case 11: return "티리온 SAY28";
                case 12: return "티리온 SAY29";
                case 13: return "티리온 SAY30";
                case 14: return "다리온 SAY31";
                case 15: return "알렉산드로스 소환 + EMOTE06";
                case 16: return "알렉산드로스 SAY32 - 입장 이동";
                case 17: return "다리온 SAY34 - 무릎 꿇음";
                case 18: return "기억 속 다리온 소환 + SAY35";
                case 19: return "기억 속 다리온 이동 + EMOTE07";
                case 20: return "기억 속 다리온 SAY36";
                case 21: return "기억 속 다리온 EMOTE08";
                case 22: return "알렉산드로스 SAY37";
                case 23: return "기억 속 다리온 SAY38";
                case 24: return "알렉산드로스 SAY39 + 티리온 이동";
                case 25: return "기억 속 다리온 SAY40";
                case 26: return "알렉산드로스 SAY41";
                case 27: return "기억 속 다리온 사망";
                case 28: return "알렉산드로스 SAY42";
                case 29: return "리치 왕 SAY43 - 소환";
                case 30: return "알렉산드로스 EMOTE09 유지 + 리치 왕 SAY45";
                case 31: return "다리온 EMOTE10 + SAY44";
                case 32: return "리치 왕 이동";
                case 33: return "다리온 돌진";
                case 34: return "돌진 이동 지점 대기";
                case 35: return "리치 왕 SAY46 - 발차기";
                case 36: return "넉백 이동 지점 대기";
                case 37: return "넉백 후 다리온 무릎 꿇음";
                case 38: return "티리온 SAY47";
                case 39: return "리치 왕 SAY48";
                case 40: return "리치 왕 SAY49";
                case 41: return "리치 왕 EMOTE11 + SAY51 + 티리온 영혼 포식";
                case 42: return "예배당 수비병 돌진 + 맥스웰 SAY50";
                case 43: return "리치 왕 EMOTE13 + 수비병 넉백";
                case 44: return "수비병 일어섬";
                case 45: return "다리온 SAY52";
                case 46: return "다리온 SAY53 - 티리온 외침 및 파멸의 인도자 준비";
                case 47: return "파멸의 인도자 전달 + 다리온 EMOTE15";
                case 48: return "빛의 새벽 게임오브젝트 + 티리온 시각 효과";
                case 49: return "티리온 SAY54";
                case 50: return "리치 왕 SAY55";
                case 51: return "티리온 SAY56";
                case 52: return "티리온 EMOTE16 + 돌진";
                case 53: return "리치 왕 SAY57";
                case 54: return "리치 왕 후퇴";
                case 55: return "리치 왕 무릎 꿇음";
                case 56: return "리치 왕 일어섬";
                case 57: return "리치 왕 SAY58";
                case 58: return "리치 왕 SAY59";
                case 59: return "리치 왕 순간이동 시각 효과 + 티리온 이동";
                case 60: return "리치 왕 EMOTE17 + 사라짐";
                case 61: return "티리온 SAY60";
                case 62: return "티리온 예배당 이동";
                case 63: return "티리온 SAY61 + 이동";
                case 64: return "티리온 SAY62";
                case 65: return "티리온 SAY63";
                case 66: return "티리온 SAY64";
                case 67: return "티리온 SAY65";
                case 68: return "티리온 SAY66";
                case 69: return "티리온 SAY67";
                case 70: return "다리온 SAY68 - 최종 대사";
                case 71: return "퀘스트 완료 처리";
                case 72: return "최종 대기";
                case 73: return "정리 및 소환 해제";
                default: return "알 수 없음";
            }
        }

        void Reset() override
        {

			if (uiStep != 0 || bIsBattle)
				return;

            if (!HasEscortState(STATE_ESCORT_ESCORTING))
            {
                Initialize();

                me->SetStandState(UNIT_STAND_STATE_STAND);
                me->Mount(25279);
                me->SetVisible(true);

                UpdateWorldState(me->GetMap(), WORLD_STATE_REMAINS, 0);
                UpdateWorldState(me->GetMap(), WORLD_STATE_EVENT_BEGIN, 0);

                if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                    tirion->setDeathState(JUST_DIED);
                if (Creature* alexandros = ObjectAccessor::GetCreature(*me, uiAlexandrosGUID))
                    alexandros->setDeathState(JUST_DIED);
                if (Creature* memoryDarion = ObjectAccessor::GetCreature(*me, uiMemoryDarionGUID))
                    memoryDarion->setDeathState(JUST_DIED);
                if (Creature* korfax = ObjectAccessor::GetCreature(*me, uiKorfaxGUID))
                    korfax->setDeathState(JUST_DIED);
                if (Creature* maxwell = ObjectAccessor::GetCreature(*me, uiMaxwellGUID))
                    maxwell->setDeathState(JUST_DIED);
                if (Creature* eligor = ObjectAccessor::GetCreature(*me, uiEligorGUID))
                    eligor->setDeathState(JUST_DIED);
                if (Creature* rayne = ObjectAccessor::GetCreature(*me, uiRayneGUID))
                    rayne->setDeathState(JUST_DIED);

                uiTirionGUID.Clear();
                uiAlexandrosGUID.Clear();
                uiMemoryDarionGUID.Clear();
                uiKorfaxGUID.Clear();
                uiMaxwellGUID.Clear();
                uiEligorGUID.Clear();
                uiRayneGUID.Clear();

                for (uint8 i = 0; i < ENCOUNTER_DEFENDER_NUMBER; ++i)
                {
                    if (Creature* defender = ObjectAccessor::GetCreature(*me, uiDefenderGUID[i]))
                        defender->setDeathState(JUST_DIED);
                    uiDefenderGUID[i].Clear();
                }

                for (uint8 i = 0; i < ENCOUNTER_EARTHSHATTER_NUMBER; ++i)
                {
                    if (Creature* earthshatter = ObjectAccessor::GetCreature(*me, uiEarthshatterGUID[i]))
                        earthshatter->setDeathState(JUST_DIED);
                    uiEarthshatterGUID[i].Clear();
                }

                if (Creature* koltira = ObjectAccessor::GetCreature(*me, uiKoltiraGUID))
                    koltira->Respawn();
                if (Creature* orbaz = ObjectAccessor::GetCreature(*me, uiOrbazGUID))
                    orbaz->Respawn();
                if (Creature* thassarian = ObjectAccessor::GetCreature(*me, uiThassarianGUID))
                    thassarian->Respawn();
                if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                    lichKing->Respawn();

                uiKoltiraGUID.Clear();
                uiOrbazGUID.Clear();
                uiThassarianGUID.Clear();
                uiLichKingGUID.Clear();

                for (uint8 i = 0; i < ENCOUNTER_ABOMINATION_NUMBER; ++i)
                {
                    if (Creature* abomination = ObjectAccessor::GetCreature(*me, uiAbominationGUID[i]))
                        abomination->setDeathState(JUST_DIED);
                    uiAbominationGUID[i].Clear();
                }

                for (uint8 i = 0; i < ENCOUNTER_BEHEMOTH_NUMBER; ++i)
                {
                    if (Creature* behemoth = ObjectAccessor::GetCreature(*me, uiBehemothGUID[i]))
                        behemoth->setDeathState(JUST_DIED);
                    uiBehemothGUID[i].Clear();
                }

                for (uint8 i = 0; i < ENCOUNTER_GHOUL_NUMBER; ++i)
                {
                    if (Creature* ghoul = ObjectAccessor::GetCreature(*me, uiGhoulGUID[i]))
                        ghoul->setDeathState(JUST_DIED);
                    uiGhoulGUID[i].Clear();
                }

                for (uint8 i = 0; i < ENCOUNTER_WARRIOR_NUMBER; ++i)
                {
                    if (Creature* warrior = ObjectAccessor::GetCreature(*me, uiWarriorGUID[i]))
                        warrior->setDeathState(JUST_DIED);
                    uiWarriorGUID[i].Clear();
                }

            }
        }

        void AttackStart(Unit* who) override
        {
            if (!who)
                return;

            if (who == me)
                return;

            if (me->Attack(who, true))
            {
                AddThreat(who, 0.0f);
                me->SetInCombatWith(who);
                who->SetInCombatWith(me);
                DoStartMovement(who);
            }
        }

        void MoveInLineOfSight(Unit* who) override
        {
            if (!who)
                return;

            if (me->IsValidAttackTarget(who))
                if (me->IsWithinDistInMap(who, 20) && me->IsWithinLOSInMap(who))
                    AttackStart(who);
        }

        void SetHoldState(bool bOnHold)
        {
            SetEscortPaused(bOnHold);
        }

        Creature* EnsureLightOfDawnSceneCreature(ObjectGuid& guid, uint32 entry, Position const& position)
        {
            Creature* creature = ObjectAccessor::GetCreature(*me, guid);
            if (!creature || !creature->IsAlive())
            {
                if (creature)
                    creature->DespawnOrUnsummon();

                guid.Clear();
                creature = me->SummonCreature(entry, position, TEMPSUMMON_MANUAL_DESPAWN, 0s);
                if (!creature)
                    return nullptr;

                guid = creature->GetGUID();
                creature->GetMotionMaster()->Clear();
                creature->GetMotionMaster()->MoveIdle();
                creature->NearTeleportTo(position.GetPositionX(), position.GetPositionY(), position.GetPositionZ(), position.GetOrientation());
                creature->SetHomePosition(position.GetPositionX(), position.GetPositionY(), position.GetPositionZ(), position.GetOrientation());
                creature->SetHealth(creature->GetMaxHealth());
                creature->SetStandState(UNIT_STAND_STATE_STAND);
                creature->SetWalk(true);
                creature->SetSpeedRate(MOVE_RUN, 1.0f);
                creature->Dismount();
            }

            creature->SetFaction(me->GetFaction());
            creature->SetVisible(true);
            creature->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
            creature->AttackStop();
            creature->CombatStop(true);
            creature->InterruptNonMeleeSpells(false);
            creature->GetThreatManager().ClearAllThreat();
            creature->SetTarget(ObjectGuid::Empty);

            return creature;
        }

        void EnsureLightOfDawnSceneSummonsBeforeLog()
        {
            if (uiStep >= 30 && uiStep <= 70)
            {
                EnsureLightOfDawnSceneCreature(uiTirionGUID, NPC_HIGHLORD_TIRION_FORDRING, LightofDawnLoc[1]);
            }

            if (uiStep >= 30 && uiStep <= 43)
            {
                EnsureLightOfDawnSceneCreature(uiKorfaxGUID, NPC_KORFAX_CHAMPION_OF_THE_LIGHT, LightofDawnLoc[10]);
                EnsureLightOfDawnSceneCreature(uiMaxwellGUID, NPC_LORD_MAXWELL_TYROSUS, LightofDawnLoc[13]);
                EnsureLightOfDawnSceneCreature(uiEligorGUID, NPC_COMMANDER_ELIGOR_DAWNBRINGER, LightofDawnLoc[16]);
            }
        }

        void DespawnLightOfDawnGuid(ObjectGuid& guid)
        {

            if (Creature* creature = ObjectAccessor::GetCreature(*me, guid))
            {
                creature->DespawnOrUnsummon();
            }

            guid.Clear();
        }

        void CleanupLightOfDawnEventState()
        {
            UpdateWorldState(me->GetMap(), WORLD_STATE_REMAINS, 0);
            UpdateWorldState(me->GetMap(), WORLD_STATE_COUNTDOWN, 0);
            UpdateWorldState(me->GetMap(), WORLD_STATE_EVENT_BEGIN, 0);

            DespawnLightOfDawnGuid(uiTirionGUID);
            DespawnLightOfDawnGuid(uiAlexandrosGUID);
            DespawnLightOfDawnGuid(uiMemoryDarionGUID);
            DespawnLightOfDawnGuid(uiKorfaxGUID);
            DespawnLightOfDawnGuid(uiMaxwellGUID);
            DespawnLightOfDawnGuid(uiEligorGUID);
            DespawnLightOfDawnGuid(uiRayneGUID);

            DespawnLightOfDawnGuid(uiKoltiraGUID);
            DespawnLightOfDawnGuid(uiOrbazGUID);
            DespawnLightOfDawnGuid(uiThassarianGUID);
            DespawnLightOfDawnGuid(uiLichKingGUID);

            for (uint8 i = 0; i < ENCOUNTER_DEFENDER_NUMBER; ++i)
                DespawnLightOfDawnGuid(uiDefenderGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_EARTHSHATTER_NUMBER; ++i)
                DespawnLightOfDawnGuid(uiEarthshatterGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_ABOMINATION_NUMBER; ++i)
                DespawnLightOfDawnGuid(uiAbominationGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_BEHEMOTH_NUMBER; ++i)
                DespawnLightOfDawnGuid(uiBehemothGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_GHOUL_NUMBER; ++i)
                DespawnLightOfDawnGuid(uiGhoulGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_WARRIOR_NUMBER; ++i)
                DespawnLightOfDawnGuid(uiWarriorGUID[i]);

            me->RemoveAllAuras();
            me->CombatStop(true);
            me->AttackStop();
            me->InterruptNonMeleeSpells(false);
            me->SetStandState(UNIT_STAND_STATE_STAND);
            me->SetVisible(true);
            me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
            SetHoldState(false);

            Initialize();
        }

        void WaypointReached(uint32 waypointId, uint32 pathId) override
        {
        }


        void EnterEvadeMode(EvadeReason why) override
        {
            if (!bIsBattle)
                EscortAI::EnterEvadeMode(why);
        }

        void UpdateAI(uint32 diff) override
        {
            EscortAI::UpdateAI(diff);
            _scheduler.Update(diff);

            if (!bIsBattle && bSceneStepReady)
            {
                bSceneStepReady = false;
                ExecuteSceneStep();
            }

            if (bIsBattle)
                DoMeleeAttackIfReady();
            else if (uiStep == 0 && me->IsInCombat())
                DoMeleeAttackIfReady();
        }

        void ExecuteSceneStep()
        {
                    EnsureLightOfDawnSceneSummonsBeforeLog();
                    StepChat();

                    switch (uiStep)
                    {
                        case 0:
                            break;

                        case 1:
                            UpdateWorldState(me->GetMap(), WORLD_STATE_COUNTDOWN, 0);
                            UpdateWorldState(me->GetMap(), WORLD_STATE_EVENT_BEGIN, 1);
                            me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
							me->RemoveNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
                            JumpToNextStep(3000);
                            break;

                        case 2:
                            Talk(SAY_LIGHT_OF_DAWN04);
                            if (Creature* pKoltira = GetClosestCreatureWithEntry(me, NPC_KOLTIRA_DEATHWEAVER, 50.0f))
                                uiKoltiraGUID = pKoltira->GetGUID();
                            if (Creature* pOrbaz = GetClosestCreatureWithEntry(me, NPC_ORBAZ_BLOODBANE, 50.0f))
                                uiOrbazGUID = pOrbaz->GetGUID();
                            if (Creature* pThassarian = GetClosestCreatureWithEntry(me, NPC_THASSARIAN, 50.0f))
                                uiThassarianGUID = pThassarian->GetGUID();
                            JumpToNextStep(10000);
                            break;

                        case 3:
                            Talk(SAY_LIGHT_OF_DAWN05);
                            JumpToNextStep(3000);
                            break;

                        case 4:
                            DoCast(me, 33271);

                            if (uiSummon_counter < ENCOUNTER_GHOUL_NUMBER)
                            {
                                if (Creature* ghoul = me->SummonCreature(NPC_ACHERUS_GHOUL, (me->GetPositionX() - 12) + rand32() % 24, (me->GetPositionY() - 12) + rand32() % 24, me->GetPositionZ(), 0, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                                {
                                    ghoul->SetWalk(false);
                                    ghoul->SetFaction(FACTION_UNDEAD_SCOURGE_3);
                                    uiGhoulGUID[uiSummon_counter] = ghoul->GetGUID();
                                }

                                ++uiSummon_counter;
                            }
                            else
                            {
                                uiSummon_counter = 0;
                                ++uiStep;
                            }
                            ScheduleCurrentStep(500);
                            break;

                        case 5:
                            DoCast(me, 33271);

                            if (uiSummon_counter < ENCOUNTER_ABOMINATION_NUMBER)
                            {
                                if (Creature* abomination = me->SummonCreature(NPC_RAMPAGING_ABOMINATION, (me->GetPositionX() - 12) + rand32() % 24, (me->GetPositionY() - 12) + rand32() % 24, me->GetPositionZ(), 0, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                                {
                                    abomination->SetWalk(false);
                                    abomination->SetFaction(FACTION_UNDEAD_SCOURGE_3);
                                    uiAbominationGUID[uiSummon_counter] = abomination->GetGUID();
                                }

                                ++uiSummon_counter;
                            }
                            else
                            {
                                uiSummon_counter = 0;
                                ++uiStep;
                            }
                            ScheduleCurrentStep(500);
                            break;

                        case 6:
                            DoCast(me, 33271);

                            if (uiSummon_counter < ENCOUNTER_WARRIOR_NUMBER)
                            {
                                if (Creature* warrior = me->SummonCreature(NPC_WARRIOR_OF_THE_FROZEN_WASTES, (me->GetPositionX() - 12) + rand32() % 24, (me->GetPositionY() - 12) + rand32() % 24, me->GetPositionZ(), 0, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                                {
                                    warrior->SetWalk(false);
                                    warrior->SetFaction(FACTION_UNDEAD_SCOURGE_3);
                                    uiWarriorGUID[uiSummon_counter] = warrior->GetGUID();
                                }

                                ++uiSummon_counter;
                            }
                            else
                            {
                                uiSummon_counter = 0;
                                ++uiStep;
                            }
                            ScheduleCurrentStep(500);
                            break;

                        case 7:
                            DoCast(me, 33271);

                            if (uiSummon_counter < ENCOUNTER_BEHEMOTH_NUMBER)
                            {
                                if (Creature* behemoth = me->SummonCreature(NPC_FLESH_BEHEMOTH, (me->GetPositionX() - 12) + rand32() % 24, (me->GetPositionY() - 12) + rand32() % 24, me->GetPositionZ(), 0, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                                {
                                    behemoth->SetWalk(false);
                                    behemoth->SetFaction(FACTION_UNDEAD_SCOURGE_3);
                                    uiBehemothGUID[uiSummon_counter] = behemoth->GetGUID();
                                }

                                ++uiSummon_counter;
                            }
                            else
                            {
                                uiSummon_counter = 0;
                                ++uiStep;
                            }
                            ScheduleCurrentStep(500);
                            break;

                        case 8:
                            Talk(SAY_LIGHT_OF_DAWN06);
                            JumpToNextStep(5000);
                            break;

                        case 9:
                            if (!bBattleApproachStarted)
                            {
                                bBattleApproachStarted = true;

                                // 여기서는 아직 전투 지점에 도착한 것이 아니므로 말에서 내리면 안 된다.
                                // 말에서 내리는 처리는 실제 도착 확인 후 case 10에서만 한다.
                                me->SetStandState(UNIT_STAND_STATE_STAND);
                                me->SetSpeedRate(MOVE_RUN, 1.0f);
                                me->SetWalk(false);
                                me->GetMotionMaster()->Clear();
                                me->GetMotionMaster()->MovePoint(0, LightofDawnLoc[0]);

                                if (Creature* koltira = ObjectAccessor::GetCreature(*me, uiKoltiraGUID))
                                {
                                    koltira->SetWalk(false);
                                    koltira->GetMotionMaster()->MovePoint(0, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }));
                                }

                                if (Creature* orbaz = ObjectAccessor::GetCreature(*me, uiOrbazGUID))
                                {
                                    orbaz->SetWalk(false);
                                    orbaz->GetMotionMaster()->MovePoint(0, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }));
                                }

                                if (Creature* thassarian = ObjectAccessor::GetCreature(*me, uiThassarianGUID))
                                {
                                    thassarian->SetWalk(false);
                                    thassarian->GetMotionMaster()->MovePoint(0, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }));
                                }

                                for (uint8 i = 0; i < ENCOUNTER_ABOMINATION_NUMBER; ++i)
                                    if (Creature* abomination = ObjectAccessor::GetCreature(*me, uiAbominationGUID[i]))
                                        abomination->GetMotionMaster()->MovePoint(0, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }));

                                for (uint8 i = 0; i < ENCOUNTER_BEHEMOTH_NUMBER; ++i)
                                    if (Creature* behemoth = ObjectAccessor::GetCreature(*me, uiBehemothGUID[i]))
                                        behemoth->GetMotionMaster()->MovePoint(0, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }));

                                for (uint8 i = 0; i < ENCOUNTER_GHOUL_NUMBER; ++i)
                                    if (Creature* ghoul = ObjectAccessor::GetCreature(*me, uiGhoulGUID[i]))
                                        ghoul->GetMotionMaster()->MovePoint(0, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }));

                                for (uint8 i = 0; i < ENCOUNTER_WARRIOR_NUMBER; ++i)
                                    if (Creature* warrior = ObjectAccessor::GetCreature(*me, uiWarriorGUID[i]))
                                        warrior->GetMotionMaster()->MovePoint(0, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }));
                            }

                            if (!IsWithinLightOfDawnRadius(me, LightofDawnLoc[0], 30.0f))
                            {
                                ScheduleCurrentStep(LIGHT_OF_DAWN_BATTLE_APPROACH_CHECK_TIMER);
                                break;
                            }

                            JumpToNextStep(500);
                            break;

                        case 10:
                        {
                            if (!bBattleStartedForScene)
                            {
                                if (!IsWithinLightOfDawnRadius(me, LightofDawnLoc[0], 30.0f))
                                {
                                    // case 10은 실제 전투 지점 도착 후에만 시작한다.
                                    // 여기서 순간이동시키면 case 9 이동 시작만으로 도착 처리되는 문제가 다시 생긴다.
                                    uiStep = 9;
                                    bBattleApproachStarted = false;
                                    ScheduleCurrentStep(LIGHT_OF_DAWN_BATTLE_APPROACH_CHECK_TIMER);
                                    break;
                                }

                                bBattleStartedForScene = true;

                                SetHoldState(true);

                                Creature* combatDarion = me;
                                if (!combatDarion->IsAlive())
                                    combatDarion->Respawn();

                                if (combatDarion)
                                {
                                    if (!combatDarion->IsAlive())
                                        combatDarion->Respawn();

                                    combatDarion->InterruptNonMeleeSpells(false);
                                    combatDarion->AttackStop();
                                    combatDarion->CombatStop(true);
                                    combatDarion->GetThreatManager().ClearAllThreat();
                                    combatDarion->SetTarget(ObjectGuid::Empty);
                                    combatDarion->GetMotionMaster()->Clear();
                                    combatDarion->GetMotionMaster()->MoveIdle();
                                    combatDarion->RemoveAllAuras();
                                    combatDarion->SetVisible(true);
                                    combatDarion->SetHealth(combatDarion->GetMaxHealth());
                                    combatDarion->SetFaction(me->GetFaction());
                                    combatDarion->SetReactState(REACT_AGGRESSIVE);
                                    combatDarion->SetStandState(UNIT_STAND_STATE_STAND);
                                    combatDarion->SetWalk(false);
                                    combatDarion->SetSpeedRate(MOVE_RUN, 1.0f);
                                    combatDarion->Dismount();
                                    combatDarion->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                                    combatDarion->NearTeleportTo(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation());
                                    combatDarion->SetHomePosition(LightofDawnLoc[0].GetPositionX(), LightofDawnLoc[0].GetPositionY(), LightofDawnLoc[0].GetPositionZ(), LightofDawnLoc[0].GetOrientation());
                                }

                                if (combatDarion)
                                {
                                    me->InterruptNonMeleeSpells(false);
                                    me->AttackStop();
                                    me->CombatStop(true);
                                    me->RemoveAllAuras();
                                    me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                                    me->SetVisible(true);
                                }

                                SpawnNPC();

                                if (combatDarion && combatDarion->IsAlive())
                                    combatDarion->SetHomePosition(combatDarion->GetPositionX(), combatDarion->GetPositionY(), combatDarion->GetPositionZ(), combatDarion->GetOrientation());

                                if (Creature* koltira = ObjectAccessor::GetCreature(*me, uiKoltiraGUID))
                                    if (koltira->IsAlive())
                                        koltira->SetHomePosition(koltira->GetPositionX(), koltira->GetPositionY(), koltira->GetPositionZ(), koltira->GetOrientation());
                                if (Creature* orbaz = ObjectAccessor::GetCreature(*me, uiOrbazGUID))
                                    if (orbaz->IsAlive())
                                        orbaz->SetHomePosition(orbaz->GetPositionX(), orbaz->GetPositionY(), orbaz->GetPositionZ(), orbaz->GetOrientation());
                                if (Creature* thassarian = ObjectAccessor::GetCreature(*me, uiThassarianGUID))
                                    if (thassarian->IsAlive())
                                        thassarian->SetHomePosition(thassarian->GetPositionX(), thassarian->GetPositionY(), thassarian->GetPositionZ(), thassarian->GetOrientation());
                                if (Creature* korfax = ObjectAccessor::GetCreature(*me, uiKorfaxGUID))
                                    if (korfax->IsAlive())
                                        korfax->SetHomePosition(korfax->GetPositionX(), korfax->GetPositionY(), korfax->GetPositionZ(), korfax->GetOrientation());
                                if (Creature* maxwell = ObjectAccessor::GetCreature(*me, uiMaxwellGUID))
                                    if (maxwell->IsAlive())
                                        maxwell->SetHomePosition(maxwell->GetPositionX(), maxwell->GetPositionY(), maxwell->GetPositionZ(), maxwell->GetOrientation());
                                if (Creature* eligor = ObjectAccessor::GetCreature(*me, uiEligorGUID))
                                    if (eligor->IsAlive())
                                        eligor->SetHomePosition(eligor->GetPositionX(), eligor->GetPositionY(), eligor->GetPositionZ(), eligor->GetOrientation());
                                if (Creature* rayne = ObjectAccessor::GetCreature(*me, uiRayneGUID))
                                    if (rayne->IsAlive())
                                        rayne->SetHomePosition(rayne->GetPositionX(), rayne->GetPositionY(), rayne->GetPositionZ(), rayne->GetOrientation());

                                for (uint8 i = 0; i < ENCOUNTER_GHOUL_NUMBER; ++i)
                                    if (Creature* ghoul = ObjectAccessor::GetCreature(*me, uiGhoulGUID[i]))
                                        if (ghoul->IsAlive())
                                            ghoul->SetHomePosition(ghoul->GetPositionX(), ghoul->GetPositionY(), ghoul->GetPositionZ(), ghoul->GetOrientation());
                                for (uint8 i = 0; i < ENCOUNTER_WARRIOR_NUMBER; ++i)
                                    if (Creature* warrior = ObjectAccessor::GetCreature(*me, uiWarriorGUID[i]))
                                        if (warrior->IsAlive())
                                            warrior->SetHomePosition(warrior->GetPositionX(), warrior->GetPositionY(), warrior->GetPositionZ(), warrior->GetOrientation());
                                for (uint8 i = 0; i < ENCOUNTER_ABOMINATION_NUMBER; ++i)
                                    if (Creature* abomination = ObjectAccessor::GetCreature(*me, uiAbominationGUID[i]))
                                        if (abomination->IsAlive())
                                            abomination->SetHomePosition(abomination->GetPositionX(), abomination->GetPositionY(), abomination->GetPositionZ(), abomination->GetOrientation());
                                for (uint8 i = 0; i < ENCOUNTER_BEHEMOTH_NUMBER; ++i)
                                    if (Creature* behemoth = ObjectAccessor::GetCreature(*me, uiBehemothGUID[i]))
                                        if (behemoth->IsAlive())
                                            behemoth->SetHomePosition(behemoth->GetPositionX(), behemoth->GetPositionY(), behemoth->GetPositionZ(), behemoth->GetOrientation());
                                for (uint8 i = 0; i < ENCOUNTER_DEFENDER_NUMBER; ++i)
                                    if (Creature* defender = ObjectAccessor::GetCreature(*me, uiDefenderGUID[i]))
                                        if (defender->IsAlive())
                                            defender->SetHomePosition(defender->GetPositionX(), defender->GetPositionY(), defender->GetPositionZ(), defender->GetOrientation());
                                for (uint8 i = 0; i < ENCOUNTER_EARTHSHATTER_NUMBER; ++i)
                                    if (Creature* earthshatter = ObjectAccessor::GetCreature(*me, uiEarthshatterGUID[i]))
                                        if (earthshatter->IsAlive())
                                            earthshatter->SetHomePosition(earthshatter->GetPositionX(), earthshatter->GetPositionY(), earthshatter->GetPositionZ(), earthshatter->GetOrientation());

                                if (Creature* korfax = ObjectAccessor::GetCreature(*me, uiKorfaxGUID))
                                    korfax->AI()->Talk(SAY_LIGHT_OF_DAWN07);
                                if (Creature* maxwell = ObjectAccessor::GetCreature(*me, uiMaxwellGUID))
                                    maxwell->AI()->Talk(SAY_LIGHT_OF_DAWN08);

                                for (uint8 i = 0; i < ENCOUNTER_GHOUL_NUMBER; ++i)
                                    NPCChangeTarget(uiGhoulGUID[i]);
                                for (uint8 i = 0; i < ENCOUNTER_WARRIOR_NUMBER; ++i)
                                    NPCChangeTarget(uiWarriorGUID[i]);
                                for (uint8 i = 0; i < ENCOUNTER_ABOMINATION_NUMBER; ++i)
                                    NPCChangeTarget(uiAbominationGUID[i]);
                                for (uint8 i = 0; i < ENCOUNTER_BEHEMOTH_NUMBER; ++i)
                                    NPCChangeTarget(uiBehemothGUID[i]);

                                NPCChangeTarget(uiKoltiraGUID);
                                NPCChangeTarget(uiOrbazGUID);
                                NPCChangeTarget(uiThassarianGUID);
                                NPCChangeTarget(me->GetGUID());

                                for (uint8 i = 0; i < ENCOUNTER_DEFENDER_NUMBER; ++i)
                                    NPCChangeTarget(uiDefenderGUID[i]);
                                for (uint8 i = 0; i < ENCOUNTER_EARTHSHATTER_NUMBER; ++i)
                                    NPCChangeTarget(uiEarthshatterGUID[i]);

                                NPCChangeTarget(uiKorfaxGUID);
                                NPCChangeTarget(uiMaxwellGUID);
                                NPCChangeTarget(uiEligorGUID);
                                NPCChangeTarget(uiRayneGUID);

                                if (combatDarion)
                                {
                                    combatDarion->Dismount();
                                    combatDarion->CastSpell(combatDarion, SPELL_THE_MIGHT_OF_MOGRAINE, true);
                                }

                                if (Creature* koltira = ObjectAccessor::GetCreature(*me, uiKoltiraGUID))
                                    koltira->Dismount();
                                if (Creature* thassarian = ObjectAccessor::GetCreature(*me, uiThassarianGUID))
                                    thassarian->Dismount();

                                bIsBattle = true;

                                {
                                    uint32 serial = uiEventSerial;

                                    ScheduleAntiMagicZone(serial, Milliseconds(urand(1000, 6000)));
                                    ScheduleDeathStrike(serial, Milliseconds(urand(5000, 10000)));
                                    ScheduleDeathEmbrace(serial, Milliseconds(urand(5000, 10000)));
                                    ScheduleIcyTouch(serial, Milliseconds(urand(5000, 10000)));
                                    ScheduleUnholyBlight(serial, Milliseconds(urand(5000, 10000)));
                                    ScheduleFightSpeech(serial, Milliseconds(15000));
                                    ScheduleBattleLeash(serial);
                                    ScheduleBattleSpawn(serial);
                                    ScheduleTargetRefresh(serial);

                                    _scheduler.Schedule(Milliseconds(LIGHT_OF_DAWN_TIRION_MESSAGE_SKIP_TIMER), [this, serial](TaskContext /*context*/)
                                    {
                                        RequestBattleFinishAfterTirionMessage(serial);
                                    });

                                    _scheduler.Schedule(Seconds(300), [this, serial](TaskContext /*context*/)
                                    {
                                        if (IsActiveBattleSerial(serial))
                                            FinishLightOfDawnBattle();
                                    });
                                }

                                break;
                            }

                            if (!bBattleFinishedForScene)
                            {
                                break;
                            }

                            bBattleFinishedForScene = false;

                            SetHoldState(true);

                            Creature* sceneDarion = me;
                            if (!sceneDarion->IsAlive())
                                sceneDarion->Respawn();

                            if (sceneDarion)
                            {
                                sceneDarion->SetVisible(true);
                                sceneDarion->SetWalk(true);
                                sceneDarion->AI()->Talk(EMOTE_LIGHT_OF_DAWN05);

                                if (sceneDarion->HasAura(SPELL_THE_LIGHT_OF_DAWN))
                                    sceneDarion->RemoveAurasDueToSpell(SPELL_THE_LIGHT_OF_DAWN);
                            }

                            if (Creature* koltira = ObjectAccessor::GetCreature(*me, uiKoltiraGUID))
                            {
                                if (koltira->HasAura(SPELL_THE_LIGHT_OF_DAWN))
                                    koltira->RemoveAurasDueToSpell(SPELL_THE_LIGHT_OF_DAWN);
                                koltira->SetWalk(true);
                                koltira->GetMotionMaster()->MovePoint(0, LightofDawnLoc[19]);
                            }

                            if (Creature* thassarian = ObjectAccessor::GetCreature(*me, uiThassarianGUID))
                            {
                                if (thassarian->HasAura(SPELL_THE_LIGHT_OF_DAWN))
                                    thassarian->RemoveAurasDueToSpell(SPELL_THE_LIGHT_OF_DAWN);
                                thassarian->SetWalk(true);
                                thassarian->GetMotionMaster()->MovePoint(0, LightofDawnLoc[21]);
                            }

                            if (Creature* korfax = ObjectAccessor::GetCreature(*me, uiKorfaxGUID))
                            {
                                korfax->SetWalk(true);
                                korfax->SetEmoteState(EMOTE_STATE_READY2H);
                                korfax->GetMotionMaster()->MovePoint(0, LightofDawnLoc[10]);
                            }

                            if (Creature* maxwell = ObjectAccessor::GetCreature(*me, uiMaxwellGUID))
                            {
                                maxwell->SetWalk(true);
                                maxwell->GetMotionMaster()->MovePoint(0, LightofDawnLoc[13]);
                            }

                            if (Creature* eligor = ObjectAccessor::GetCreature(*me, uiEligorGUID))
                            {
                                eligor->SetWalk(true);
                                eligor->GetMotionMaster()->MovePoint(0, LightofDawnLoc[16]);
                            }

                            // 전투 직후 티리온은 후반부 파멸의 인도자 전달 위치가 아니라
                            // 전투 종료 대사 위치인 전장 기준점에 있어야 한다.
                            Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID);
                            if (!tirion || !tirion->IsAlive())
                            {
                                if (tirion)
                                    tirion->DespawnOrUnsummon();

                                uiTirionGUID.Clear();
                                tirion = me->SummonCreature(NPC_HIGHLORD_TIRION_FORDRING, LightofDawnLoc[0], TEMPSUMMON_MANUAL_DESPAWN, 0s);
                                if (tirion)
                                    uiTirionGUID = tirion->GetGUID();
                            }

                            if (tirion)
                            {
                                tirion->SetFaction(me->GetFaction());
                                tirion->SetVisible(true);
                                tirion->RemoveAllAuras();
                                tirion->CombatStop(true);
                                tirion->AttackStop();
                                tirion->InterruptNonMeleeSpells(false);
                                tirion->SetWalk(true);
                                tirion->SetSpeedRate(MOVE_RUN, 1.0f);
                                tirion->SetStandState(UNIT_STAND_STATE_STAND);
                                tirion->GetMotionMaster()->Clear();
                                tirion->GetMotionMaster()->MoveIdle();
                                tirion->NearTeleportTo(LightofDawnLoc[0].GetPositionX(), LightofDawnLoc[0].GetPositionY(), LightofDawnLoc[0].GetPositionZ(), LightofDawnLoc[0].GetOrientation());
                                tirion->SetHomePosition(LightofDawnLoc[0].GetPositionX(), LightofDawnLoc[0].GetPositionY(), LightofDawnLoc[0].GetPositionZ(), LightofDawnLoc[0].GetOrientation());
                            }

                            if (sceneDarion)
                            {
                                sceneDarion->AI()->Talk(SAY_LIGHT_OF_DAWN27);
                                sceneDarion->SetStandState(UNIT_STAND_STATE_KNEEL);
                            }

                            if (Creature* koltira = ObjectAccessor::GetCreature(*me, uiKoltiraGUID))
                            {
                                koltira->Dismount();
                                koltira->SetStandState(UNIT_STAND_STATE_KNEEL);
                            }
                            if (Creature* thassarian = ObjectAccessor::GetCreature(*me, uiThassarianGUID))
                            {
                                thassarian->Dismount();
                                thassarian->SetStandState(UNIT_STAND_STATE_KNEEL);
                            }
							if (Creature* orbaz = ObjectAccessor::GetCreature(*me, uiOrbazGUID))
                            {
                                orbaz->Dismount();
                                orbaz->SetStandState(UNIT_STAND_STATE_KNEEL);
                            }

                            JumpToNextStep(10000);
                            break;
                        }

                        case 11:
                        {
                            Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID);
                            if (!tirion || !tirion->IsAlive())
                            {
                                if (tirion)
                                    tirion->DespawnOrUnsummon();

                                uiTirionGUID.Clear();
                                tirion = me->SummonCreature(NPC_HIGHLORD_TIRION_FORDRING, LightofDawnLoc[0], TEMPSUMMON_MANUAL_DESPAWN, 0s);
                                if (tirion)
                                    uiTirionGUID = tirion->GetGUID();
                            }

                            if (!tirion)
                            {
                                ScheduleCurrentStep(1000);
                                break;
                            }

                            tirion->SetFaction(me->GetFaction());
                            tirion->SetVisible(true);
                            tirion->RemoveAllAuras();
                            tirion->CombatStop(true);
                            tirion->AttackStop();
                            tirion->InterruptNonMeleeSpells(false);
                            tirion->SetWalk(true);
                            tirion->SetSpeedRate(MOVE_RUN, 1.0f);
                            tirion->SetStandState(UNIT_STAND_STATE_STAND);
                            tirion->GetMotionMaster()->Clear();
                            tirion->GetMotionMaster()->MoveIdle();
                            tirion->NearTeleportTo(LightofDawnLoc[0].GetPositionX(), LightofDawnLoc[0].GetPositionY(), LightofDawnLoc[0].GetPositionZ(), LightofDawnLoc[0].GetOrientation());
                            tirion->SetHomePosition(LightofDawnLoc[0].GetPositionX(), LightofDawnLoc[0].GetPositionY(), LightofDawnLoc[0].GetPositionZ(), LightofDawnLoc[0].GetOrientation());
                            tirion->AI()->Talk(SAY_LIGHT_OF_DAWN28);

							me->SetFacingTo(4.792f);
							me->SetStandState(UNIT_STAND_STATE_STAND);

                            JumpToNextStep(21000);
                            break;
                        }

                        case 12:
                        {
                            Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID);
                            if (!tirion || !tirion->IsAlive())
                            {
                                if (tirion)
                                    tirion->DespawnOrUnsummon();

                                uiTirionGUID.Clear();
                                tirion = me->SummonCreature(NPC_HIGHLORD_TIRION_FORDRING, LightofDawnLoc[0], TEMPSUMMON_MANUAL_DESPAWN, 0s);
                                if (tirion)
                                    uiTirionGUID = tirion->GetGUID();
                            }

                            if (!tirion)
                            {
                                ScheduleCurrentStep(1000);
                                break;
                            }

                            tirion->SetFaction(me->GetFaction());
                            tirion->SetVisible(true);
                            tirion->RemoveAllAuras();
                            tirion->CombatStop(true);
                            tirion->AttackStop();
                            tirion->InterruptNonMeleeSpells(false);
                            tirion->SetWalk(true);
                            tirion->SetSpeedRate(MOVE_RUN, 1.0f);
                            tirion->SetStandState(UNIT_STAND_STATE_STAND);
                            tirion->GetMotionMaster()->Clear();
                            tirion->GetMotionMaster()->MoveIdle();
                            tirion->NearTeleportTo(LightofDawnLoc[0].GetPositionX(), LightofDawnLoc[0].GetPositionY(), LightofDawnLoc[0].GetPositionZ(), LightofDawnLoc[0].GetOrientation());
                            tirion->SetHomePosition(LightofDawnLoc[0].GetPositionX(), LightofDawnLoc[0].GetPositionY(), LightofDawnLoc[0].GetPositionZ(), LightofDawnLoc[0].GetOrientation());
                            tirion->AI()->Talk(SAY_LIGHT_OF_DAWN29);

                            JumpToNextStep(13000);
                            break;
                        }

                        case 13:
                        {
                            Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID);
                            if (!tirion || !tirion->IsAlive())
                            {
                                if (tirion)
                                    tirion->DespawnOrUnsummon();

                                uiTirionGUID.Clear();
                                tirion = me->SummonCreature(NPC_HIGHLORD_TIRION_FORDRING, LightofDawnLoc[0], TEMPSUMMON_MANUAL_DESPAWN, 0s);
                                if (tirion)
                                    uiTirionGUID = tirion->GetGUID();
                            }

                            if (!tirion)
                            {
                                ScheduleCurrentStep(1000);
                                break;
                            }

                            tirion->SetFaction(me->GetFaction());
                            tirion->SetVisible(true);
                            tirion->RemoveAllAuras();
                            tirion->CombatStop(true);
                            tirion->AttackStop();
                            tirion->InterruptNonMeleeSpells(false);
                            tirion->SetWalk(true);
                            tirion->SetSpeedRate(MOVE_RUN, 1.0f);
                            tirion->SetStandState(UNIT_STAND_STATE_STAND);
                            tirion->GetMotionMaster()->Clear();
                            tirion->GetMotionMaster()->MoveIdle();
                            tirion->NearTeleportTo(LightofDawnLoc[0].GetPositionX(), LightofDawnLoc[0].GetPositionY(), LightofDawnLoc[0].GetPositionZ(), LightofDawnLoc[0].GetOrientation());
                            tirion->SetHomePosition(LightofDawnLoc[0].GetPositionX(), LightofDawnLoc[0].GetPositionY(), LightofDawnLoc[0].GetPositionZ(), LightofDawnLoc[0].GetOrientation());
                            tirion->AI()->Talk(SAY_LIGHT_OF_DAWN30);

                            JumpToNextStep(13000);
                            break;
                        }

                        case 14:
                        {
                            Creature* darion = me;
                            if (!darion->IsAlive())
                                darion->Respawn();

                            darion->InterruptNonMeleeSpells(false);
                            darion->AttackStop();
                            darion->CombatStop(true);
                            darion->GetThreatManager().ClearAllThreat();
                            darion->SetTarget(ObjectGuid::Empty);
                            darion->GetMotionMaster()->Clear();
                            darion->GetMotionMaster()->MoveIdle();
                            darion->RemoveAllAuras();
                            darion->SetVisible(true);
                            darion->SetHealth(darion->GetMaxHealth());
                            darion->SetFaction(me->GetFaction());
                            darion->SetReactState(REACT_AGGRESSIVE);
                            darion->SetStandState(UNIT_STAND_STATE_STAND);
                            darion->SetWalk(true);
                            darion->SetSpeedRate(MOVE_RUN, 1.0f);
                            darion->NearTeleportTo(LightofDawnLoc[24].GetPositionX(), LightofDawnLoc[24].GetPositionY(), LightofDawnLoc[24].GetPositionZ(), LightofDawnLoc[24].GetOrientation());
                            darion->SetHomePosition(LightofDawnLoc[24].GetPositionX(), LightofDawnLoc[24].GetPositionY(), LightofDawnLoc[24].GetPositionZ(), LightofDawnLoc[24].GetOrientation());
                            darion->AI()->Talk(SAY_LIGHT_OF_DAWN31);

                            JumpToNextStep(7000);
                            break;
                        }

                        case 15:
							me->SetFacingTo(1.751620f);

                            if (Creature* alexandros = me->SummonCreature(NPC_HIGHLORD_ALEXANDROS_MOGRAINE, LightofDawnLoc[22], TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                            {
                                alexandros->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                                alexandros->CastSpell(alexandros, SPELL_ALEXANDROS_MOGRAINE_SPAWN, true);
                                alexandros->AI()->Talk(EMOTE_LIGHT_OF_DAWN06);
                                uiAlexandrosGUID = alexandros->GetGUID();
                            }
                            JumpToNextStep(4000);
                            break;

                        case 16:
                        {
                            if (Creature* alexandros = ObjectAccessor::GetCreature(*me, uiAlexandrosGUID))
                            {
                                alexandros->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                                alexandros->GetMotionMaster()->MovePoint(0, LightofDawnLoc[23]);
                                alexandros->AI()->Talk(SAY_LIGHT_OF_DAWN32);
                            }

                            Creature* darion = me;
                            if (!darion->IsAlive())
                                darion->Respawn();

                            if (darion)
                                darion->AI()->Talk(SAY_LIGHT_OF_DAWN33);

                            JumpToNextStep(5000);
                            break;
                        }

                        case 17:
                        {
                            Creature* darion = me;
                            if (!darion->IsAlive())
                                darion->Respawn();

                            if (darion)
                            {
                                darion->SetStandState(UNIT_STAND_STATE_KNEEL);
                                darion->AI()->Talk(SAY_LIGHT_OF_DAWN34);
                            }

                            JumpToNextStep(5000);
                            break;
                        }

                        case 18:
                            if (Creature* memoryDarion = me->SummonCreature(NPC_DARION_MOGRAINE, LightofDawnLoc[24], TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                            {
                                memoryDarion->AI()->Talk(SAY_LIGHT_OF_DAWN35);
                                memoryDarion->SetWalk(false);
                                uiMemoryDarionGUID = memoryDarion->GetGUID();
                            }
                            JumpToNextStep(4000);
                            break;

                        case 19:
                            if (Creature* memoryDarion = ObjectAccessor::GetCreature(*me, uiMemoryDarionGUID))
                            {
                                memoryDarion->AI()->Talk(EMOTE_LIGHT_OF_DAWN07);
                                memoryDarion->GetMotionMaster()->MovePoint(0, LightofDawnLoc[25]);
                            }
                            JumpToNextStep(4000);
                            break;

                        case 20:
                            if (Creature* memoryDarion = ObjectAccessor::GetCreature(*me, uiMemoryDarionGUID))
                                memoryDarion->AI()->Talk(SAY_LIGHT_OF_DAWN36);
                            JumpToNextStep(4000);
                            break;

                        case 21:
                            if (Creature* memoryDarion = ObjectAccessor::GetCreature(*me, uiMemoryDarionGUID))
                                memoryDarion->AI()->Talk(EMOTE_LIGHT_OF_DAWN08);
                            JumpToNextStep(4000);
                            break;

                        case 22:
                            if (Creature* alexandros = ObjectAccessor::GetCreature(*me, uiAlexandrosGUID))
                                alexandros->AI()->Talk(SAY_LIGHT_OF_DAWN37);
                            JumpToNextStep(8000);
                            break;

                        case 23:
                            if (Creature* memoryDarion = ObjectAccessor::GetCreature(*me, uiMemoryDarionGUID))
                                memoryDarion->AI()->Talk(SAY_LIGHT_OF_DAWN38);
                            JumpToNextStep(8000);
                            break;

                        case 24:
                        {
                            if (Creature* alexandros = ObjectAccessor::GetCreature(*me, uiAlexandrosGUID))
                                alexandros->AI()->Talk(SAY_LIGHT_OF_DAWN39);

                            Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID);
                            if (!tirion || !tirion->IsAlive())
                            {
                                if (tirion)
                                    tirion->DespawnOrUnsummon();

                                uiTirionGUID.Clear();
                                tirion = me->SummonCreature(NPC_HIGHLORD_TIRION_FORDRING, LightofDawnLoc[1], TEMPSUMMON_MANUAL_DESPAWN, 0s);
                                if (tirion)
                                    uiTirionGUID = tirion->GetGUID();
                            }

                            if (tirion)
                            {
                                tirion->SetFaction(me->GetFaction());
                                tirion->SetVisible(true);
                                tirion->RemoveAllAuras();
                                tirion->CombatStop(true);
                                tirion->AttackStop();
                                tirion->InterruptNonMeleeSpells(false);
                                tirion->SetWalk(true);
                                tirion->SetSpeedRate(MOVE_RUN, 1.0f);
                                tirion->SetStandState(UNIT_STAND_STATE_STAND);
                                tirion->GetMotionMaster()->Clear();
                                tirion->SetHomePosition(LightofDawnLoc[1].GetPositionX(), LightofDawnLoc[1].GetPositionY(), LightofDawnLoc[1].GetPositionZ(), LightofDawnLoc[1].GetOrientation());
                                tirion->GetMotionMaster()->MovePoint(0, LightofDawnLoc[1]);
                            }

                            JumpToNextStep(15000);
                            break;
                        }

                        case 25:
                            if (Creature* memoryDarion = ObjectAccessor::GetCreature(*me, uiMemoryDarionGUID))
                                memoryDarion->AI()->Talk(SAY_LIGHT_OF_DAWN40);
                            JumpToNextStep(11000);
                            break;

                        case 26:
                            if (Creature* alexandros = ObjectAccessor::GetCreature(*me, uiAlexandrosGUID))
                                alexandros->AI()->Talk(SAY_LIGHT_OF_DAWN41);
                            JumpToNextStep(5000);
                            break;

                        case 27:
                            if (Creature* memoryDarion = ObjectAccessor::GetCreature(*me, uiMemoryDarionGUID))
                                memoryDarion->setDeathState(JUST_DIED);
                            JumpToNextStep(24000);
                            break;

                        case 28:
                            if (Creature* alexandros = ObjectAccessor::GetCreature(*me, uiAlexandrosGUID))
                                alexandros->AI()->Talk(SAY_LIGHT_OF_DAWN42);
                            JumpToNextStep(6000);
                            break;

                        case 29:
                            if (Creature* lichKing = me->SummonCreature(NPC_THE_LICH_KING, LightofDawnLoc[26], TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                            {
                                lichKing->AI()->Talk(SAY_LIGHT_OF_DAWN43);
                                uiLichKingGUID = lichKing->GetGUID();

                                if (Unit* pAlex = ObjectAccessor::GetCreature(*me, uiAlexandrosGUID))
                                    lichKing->CastSpell(pAlex, SPELL_SOUL_FEAST_ALEX, false);
                            }
                            JumpToNextStep(2000);
                            break;

                        case 30:
                        {
                            if (Creature* alexandros = ObjectAccessor::GetCreature(*me, uiAlexandrosGUID))
                            {
                                alexandros->SetVisible(true);
                                alexandros->AI()->Talk(EMOTE_LIGHT_OF_DAWN09);
                            }

                            Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID);
                            if (!tirion || !tirion->IsAlive())
                            {
                                if (tirion)
                                    tirion->DespawnOrUnsummon();

                                uiTirionGUID.Clear();
                                tirion = me->SummonCreature(NPC_HIGHLORD_TIRION_FORDRING, LightofDawnLoc[1], TEMPSUMMON_MANUAL_DESPAWN, 0s);
                                if (tirion)
                                    uiTirionGUID = tirion->GetGUID();
                            }

                            if (tirion)
                            {
                                tirion->SetFaction(me->GetFaction());
                                tirion->SetVisible(true);
                                tirion->SetStandState(UNIT_STAND_STATE_STAND);
                                tirion->SetWalk(true);
                                tirion->SetSpeedRate(MOVE_RUN, 1.0f);
                                tirion->GetMotionMaster()->Clear();
                                tirion->GetMotionMaster()->MoveIdle();
                                tirion->NearTeleportTo(LightofDawnLoc[1].GetPositionX(), LightofDawnLoc[1].GetPositionY(), LightofDawnLoc[1].GetPositionZ(), LightofDawnLoc[1].GetOrientation());
                                tirion->SetHomePosition(LightofDawnLoc[1].GetPositionX(), LightofDawnLoc[1].GetPositionY(), LightofDawnLoc[1].GetPositionZ(), LightofDawnLoc[1].GetOrientation());
                            }

                            Creature* darion = me;
                            if (!darion->IsAlive())
                                darion->Respawn();

                            if (darion)
                            {
                                darion->SetVisible(true);
                                darion->SetStandState(UNIT_STAND_STATE_STAND);
                            }

                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                            {
                                lichKing->InterruptNonMeleeSpells(false);
                                lichKing->AI()->Talk(SAY_LIGHT_OF_DAWN45);
                            }
                            JumpToNextStep(3000);
                            break;
                        }

                        case 31:
                        {
                            Creature* darion = me;
                            if (!darion->IsAlive())
                                darion->Respawn();

                            if (darion)
                            {
                                darion->SetVisible(true);
                                darion->SetStandState(UNIT_STAND_STATE_STAND);
                                darion->AI()->Talk(EMOTE_LIGHT_OF_DAWN10);
                                darion->AI()->Talk(SAY_LIGHT_OF_DAWN44);
                            }

                            JumpToNextStep(3000);
                            break;
                        }

                        case 32:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                                lichKing->GetMotionMaster()->MovePoint(0, LightofDawnLoc[27]);
                            JumpToNextStep(6000);
                            break;

                        case 33:
                        {
                            Creature* darion = me;
                            if (!darion->IsAlive())
                                darion->Respawn();

                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                            {
                                if (darion)
                                {
                                    darion->SetVisible(true);
                                    darion->CastSpell(darion, SPELL_MOGRAINE_CHARGE, false);
                                    darion->GetMotionMaster()->MovePoint(0, lichKing->GetPositionX(), lichKing->GetPositionY(), lichKing->GetPositionZ());
                                }
                            }

                            if (darion)
                            {
                                darion->SetSpeedRate(MOVE_RUN, 3.0f);
                                darion->SetWalk(false);
                            }
                            JumpToNextStep(2500);
                            break;
                        }

                        case 34:
                        {
                            SetHoldState(true);
                            Creature* darion = me;
                            if (!darion->IsAlive())
                                darion->Respawn();

                            if (darion)
                            {
                                darion->SetVisible(true);
                                darion->HandleEmoteCommand(EMOTE_ONESHOT_SPECIALATTACK1H);
                            }
                            JumpToNextStep(1000);
                            break;
                        }

                        case 35:
                        {
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                            {
                                lichKing->HandleEmoteCommand(EMOTE_ONESHOT_KICK);
                                lichKing->AI()->Talk(SAY_LIGHT_OF_DAWN46);
                            }

                            Creature* darion = me;
                            if (!darion->IsAlive())
                                darion->Respawn();

                            if (darion)
                            {
                                darion->SetVisible(true);
                                darion->SetSpeedRate(MOVE_RUN, 6.0f);
                                darion->SetStandState(UNIT_STAND_STATE_DEAD);
                                darion->GetMotionMaster()->MovePoint(0, LightofDawnLoc[3]);
                            }
                            JumpToNextStep(2500);
                            break;
                        }

                        case 36:
                            SetHoldState(true);
                            JumpToNextStep(2000);
                            break;

                        case 37:
                        {
                            Creature* darion = me;
                            if (!darion->IsAlive())
                                darion->Respawn();

                            if (darion)
                            {
                                darion->SetVisible(true);
                                darion->SetStandState(UNIT_STAND_STATE_DEAD);
                            }
                            JumpToNextStep(3000);
                            break;
                        }

                        case 38:
                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                                tirion->AI()->Talk(SAY_LIGHT_OF_DAWN47);
                            JumpToNextStep(8000);
                            break;

                        case 39:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                                lichKing->AI()->Talk(SAY_LIGHT_OF_DAWN48);
                            JumpToNextStep(15000);
                            break;

                        case 40:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                                lichKing->AI()->Talk(SAY_LIGHT_OF_DAWN49);
                            JumpToNextStep(17000);
                            break;

                        case 41:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                            {
                                lichKing->AI()->Talk(EMOTE_LIGHT_OF_DAWN11);
                                lichKing->AI()->Talk(SAY_LIGHT_OF_DAWN51);

                                if (Creature* pTirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                                {
                                    pTirion->SetStandState(UNIT_STAND_STATE_KNEEL);
                                    lichKing->CastSpell(pTirion, SPELL_SOUL_FEAST_TIRION, false);
                                    pTirion->AI()->Talk(EMOTE_LIGHT_OF_DAWN12);
                                }
                            }
                            JumpToNextStep(2000);
                            break;

                        case 42:
                        {
                            float fLichPositionX = 0;
                            float fLichPositionY = 0;
                            float fLichPositionZ = 0;

                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                            {
                                fLichPositionX = lichKing->GetPositionX();
                                fLichPositionY = lichKing->GetPositionY();
                                fLichPositionZ = lichKing->GetPositionZ();
                            }

                            if (!fLichPositionX || !fLichPositionY)
                            {
                                fLichPositionX = LightofDawnLoc[27].GetPositionX();
                                fLichPositionY = LightofDawnLoc[27].GetPositionY();
                                fLichPositionZ = LightofDawnLoc[27].GetPositionZ();
                            }

                            if (fLichPositionX && fLichPositionY)
                            {
                                if (Creature* defender = me->SummonCreature(NPC_DEFENDER_OF_THE_LIGHT, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % 10), float(rand32() % 10), 0.0f, 0.0f }), TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 10s))
                                {
                                    defender->SetEmoteState(EMOTE_STATE_ATTACK_UNARMED);
                                    defender->SetWalk(false);
                                    defender->SetSpeedRate(MOVE_RUN, 2.0f);
                                    defender->SetFaction(me->GetFaction());
                                    defender->GetMotionMaster()->MovePoint(0, fLichPositionX, fLichPositionY, fLichPositionZ);
                                    uiDefenderGUID[0] = defender->GetGUID();
                                }

                                if (Creature* earthshatter = me->SummonCreature(NPC_RIMBLAT_EARTHSHATTER, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % 10), float(rand32() % 10), 0.0f, 0.0f }), TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 10s))
                                {
                                    earthshatter->SetEmoteState(EMOTE_STATE_ATTACK_UNARMED);
                                    earthshatter->SetWalk(false);
                                    earthshatter->SetSpeedRate(MOVE_RUN, 2.0f);
                                    earthshatter->SetFaction(me->GetFaction());
                                    earthshatter->GetMotionMaster()->MovePoint(0, fLichPositionX, fLichPositionY, fLichPositionZ);
                                    uiEarthshatterGUID[0] = earthshatter->GetGUID();
                                }
                            }

                            if (Creature* maxwell = ObjectAccessor::GetCreature(*me, uiMaxwellGUID))
                            {
                                maxwell->SetEmoteState(EMOTE_STATE_ATTACK_UNARMED);
                                maxwell->SetWalk(false);
                                maxwell->SetSpeedRate(MOVE_RUN, 2.0f);
                                maxwell->GetMotionMaster()->MovePoint(0, fLichPositionX, fLichPositionY, fLichPositionZ);
                                maxwell->AI()->Talk(SAY_LIGHT_OF_DAWN50);
                            }

                            if (Creature* korfax = ObjectAccessor::GetCreature(*me, uiKorfaxGUID))
                            {
                                korfax->SetEmoteState(EMOTE_STATE_ATTACK_UNARMED);
                                korfax->SetWalk(false);
                                korfax->SetSpeedRate(MOVE_RUN, 2.0f);
                                korfax->HandleEmoteCommand(EMOTE_STATE_ATTACK_UNARMED);
                                korfax->GetMotionMaster()->MovePoint(0, fLichPositionX, fLichPositionY, fLichPositionZ);
                            }

                            if (Creature* eligor = ObjectAccessor::GetCreature(*me, uiEligorGUID))
                            {
                                eligor->SetEmoteState(EMOTE_STATE_ATTACK_UNARMED);
                                eligor->SetWalk(false);
                                eligor->SetSpeedRate(MOVE_RUN, 2.0f);
                                eligor->GetMotionMaster()->MovePoint(0, fLichPositionX, fLichPositionY, fLichPositionZ);
                            }

                            JumpToNextStep(4500);
                            break;
                        }

                        case 43:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                                lichKing->AI()->Talk(EMOTE_LIGHT_OF_DAWN13);

                            if (Creature* maxwell = ObjectAccessor::GetCreature(*me, uiMaxwellGUID))
                            {
                                maxwell->SetEmoteState(EMOTE_ONESHOT_NONE);
                                maxwell->SetSpeedRate(MOVE_RUN, 6.0f);
                                maxwell->SetStandState(UNIT_STAND_STATE_DEAD);
                                maxwell->GetMotionMaster()->MovePoint(0, LightofDawnLoc[14]);
                            }

                            if (Creature* korfax = ObjectAccessor::GetCreature(*me, uiKorfaxGUID))
                            {
                                korfax->SetEmoteState(EMOTE_ONESHOT_NONE);
                                korfax->SetSpeedRate(MOVE_RUN, 6.0f);
                                korfax->SetStandState(UNIT_STAND_STATE_DEAD);
                                korfax->GetMotionMaster()->MovePoint(0, LightofDawnLoc[11]);
                            }

                            if (Creature* eligor = ObjectAccessor::GetCreature(*me, uiEligorGUID))
                            {
                                eligor->SetEmoteState(EMOTE_ONESHOT_NONE);
                                eligor->SetSpeedRate(MOVE_RUN, 6.0f);
                                eligor->SetStandState(UNIT_STAND_STATE_DEAD);
                                eligor->GetMotionMaster()->MovePoint(0, LightofDawnLoc[17]);
                            }

                            if (Creature* defender = ObjectAccessor::GetCreature(*me, uiDefenderGUID[0]))
                            {
                                defender->SetSpeedRate(MOVE_RUN, 6.0f);
                                defender->SetStandState(UNIT_STAND_STATE_DEAD);
                                defender->GetMotionMaster()->MovePoint(0, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % 10), float(rand32() % 10), 0.0f, 0.0f }));
                            }

                            if (Creature* earthshatter = ObjectAccessor::GetCreature(*me, uiEarthshatterGUID[0]))
                            {
                                earthshatter->SetSpeedRate(MOVE_RUN, 6.0f);
                                earthshatter->SetStandState(UNIT_STAND_STATE_DEAD);
                                earthshatter->GetMotionMaster()->MovePoint(0, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % 10), float(rand32() % 10), 0.0f, 0.0f }));
                            }

                            JumpToNextStep(3000);
                            break;

                        case 44:
                            if (Creature* maxwell = ObjectAccessor::GetCreature(*me, uiMaxwellGUID))
                                maxwell->SetStandState(UNIT_STAND_STATE_STAND);
                            if (Creature* korfax = ObjectAccessor::GetCreature(*me, uiKorfaxGUID))
                                korfax->SetStandState(UNIT_STAND_STATE_STAND);
                            if (Creature* eligor = ObjectAccessor::GetCreature(*me, uiEligorGUID))
                                eligor->SetStandState(UNIT_STAND_STATE_STAND);
                            JumpToNextStep(1000);
                            break;

                        case 45:
                        {
                            Creature* darion = me;
                            if (!darion->IsAlive())
                                darion->Respawn();

                            if (darion)
                            {
                                darion->SetVisible(true);
                                darion->AI()->Talk(SAY_LIGHT_OF_DAWN52);
                            }
                            JumpToNextStep(5000);
                            break;
                        }

                        case 46:
                        {
                            Creature* darion = me;
                            if (!darion->IsAlive())
                                darion->Respawn();

                            if (darion)
                            {
                                darion->SetVisible(true);
                                darion->SetSpeedRate(MOVE_RUN, 1.0f);
                                darion->SetWalk(true);
                                darion->SetStandState(UNIT_STAND_STATE_STAND);
                                darion->AI()->Talk(SAY_LIGHT_OF_DAWN53);
                            }

                            Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID);
                            if (!tirion || !tirion->IsAlive())
                            {
                                if (tirion)
                                    tirion->DespawnOrUnsummon();

                                uiTirionGUID.Clear();
                                tirion = me->SummonCreature(NPC_HIGHLORD_TIRION_FORDRING, LightofDawnLoc[1], TEMPSUMMON_MANUAL_DESPAWN, 0s);
                                if (tirion)
                                    uiTirionGUID = tirion->GetGUID();
                            }

                            if (tirion)
                            {
                                tirion->SetFaction(me->GetFaction());
                                tirion->SetVisible(true);
                                tirion->CombatStop(true);
                                tirion->AttackStop();
                                tirion->InterruptNonMeleeSpells(false);
                                tirion->SetWalk(true);
                                tirion->SetSpeedRate(MOVE_RUN, 1.0f);
                                tirion->SetStandState(UNIT_STAND_STATE_STAND);
                                tirion->GetMotionMaster()->Clear();
                                tirion->GetMotionMaster()->MoveIdle();
                                tirion->NearTeleportTo(LightofDawnLoc[1].GetPositionX(), LightofDawnLoc[1].GetPositionY(), LightofDawnLoc[1].GetPositionZ(), LightofDawnLoc[1].GetOrientation());
                                tirion->SetHomePosition(LightofDawnLoc[1].GetPositionX(), LightofDawnLoc[1].GetPositionY(), LightofDawnLoc[1].GetPositionZ(), LightofDawnLoc[1].GetOrientation());
                            }

                            JumpToNextStep(3000);
                            break;
                        }

                        case 47:
                        {
                            Creature* darion = me;
                            if (!darion->IsAlive())
                                darion->Respawn();

                            Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID);
                            if (!tirion || !tirion->IsAlive())
                            {
                                if (tirion)
                                    tirion->DespawnOrUnsummon();

                                uiTirionGUID.Clear();
                                tirion = me->SummonCreature(NPC_HIGHLORD_TIRION_FORDRING, LightofDawnLoc[1], TEMPSUMMON_MANUAL_DESPAWN, 0s);
                                if (tirion)
                                    uiTirionGUID = tirion->GetGUID();
                            }

                            if (tirion)
                            {
                                tirion->SetFaction(me->GetFaction());
                                tirion->SetVisible(true);
                                tirion->CombatStop(true);
                                tirion->AttackStop();
                                tirion->InterruptNonMeleeSpells(false);
                                tirion->SetWalk(true);
                                tirion->SetSpeedRate(MOVE_RUN, 1.0f);
                                tirion->SetStandState(UNIT_STAND_STATE_STAND);
                                tirion->GetMotionMaster()->Clear();
                                tirion->GetMotionMaster()->MoveIdle();
                                tirion->NearTeleportTo(LightofDawnLoc[1].GetPositionX(), LightofDawnLoc[1].GetPositionY(), LightofDawnLoc[1].GetPositionZ(), LightofDawnLoc[1].GetOrientation());
                                tirion->SetHomePosition(LightofDawnLoc[1].GetPositionX(), LightofDawnLoc[1].GetPositionY(), LightofDawnLoc[1].GetPositionZ(), LightofDawnLoc[1].GetOrientation());
                            }

                            if (darion)
                            {
                                darion->SetVisible(true);
                                darion->SetVirtualItem(0, uint32(EQUIP_UNEQUIP));

                                if (tirion && !bTirionChapelHandOffDone)
                                    darion->CastSpell(tirion, SPELL_ASHBRINGER, true);

                                darion->AI()->Talk(EMOTE_LIGHT_OF_DAWN14);
                                darion->SetStandState(UNIT_STAND_STATE_KNEEL);
                                darion->AI()->Talk(EMOTE_LIGHT_OF_DAWN15);
                            }

                            if (tirion)
                            {
                                tirion->SetStandState(UNIT_STAND_STATE_STAND);
                                tirion->SetVirtualItem(0, uint32(EQUIP_HIGHLORD_TIRION_FORDRING));
                                tirion->CastSpell(tirion, SPELL_REBIRTH_OF_THE_ASHBRINGER, false);
                            }

                            bTirionChapelHandOffDone = true;
                            JumpToNextStep(1000);
                            break;
                        }

                        case 48:
                            me->SummonGameObject(GO_LIGHT_OF_DAWN, 2283.896f, -5287.914f, 83.066f, 0.f, QuaternionData(), 30s);

                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                            {
								if (tirion->HasAura(SPELL_SOUL_FEAST_TIRION))
									tirion->RemoveAurasDueToSpell(SPELL_SOUL_FEAST_TIRION);
                                if (tirion->HasAura(SPELL_REBIRTH_OF_THE_ASHBRINGER))
                                    tirion->RemoveAurasDueToSpell(SPELL_REBIRTH_OF_THE_ASHBRINGER);
                                tirion->CastSpell(tirion, 41542, false);
                                tirion->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
                            }

                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                                lichKing->InterruptNonMeleeSpells(false);

                            JumpToNextStep(2500);
                            break;

                        case 49:
                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                                tirion->AI()->Talk(SAY_LIGHT_OF_DAWN54);
                            JumpToNextStep(4000);
                            break;

                        case 50:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                                lichKing->AI()->Talk(SAY_LIGHT_OF_DAWN55);
                            JumpToNextStep(5000);
                            break;

                        case 51:
                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                                tirion->AI()->Talk(SAY_LIGHT_OF_DAWN56);
                            JumpToNextStep(1000);
                            break;

                        case 52:
                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                            {
                                tirion->AI()->Talk(EMOTE_LIGHT_OF_DAWN16);
                                tirion->CastSpell(tirion, SPELL_TIRION_CHARGE, false);
                                tirion->SetEmoteState(EMOTE_STATE_READY2H);
                                tirion->SetSpeedRate(MOVE_RUN, 3.0f);
                                tirion->SetWalk(false);
                                tirion->GetMotionMaster()->MovePoint(0, LightofDawnLoc[2]);
								tirion->SetFacingTo(2.520523f);

                                if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                                    lichKing->Relocate(LightofDawnLoc[28]);
                            }
                            JumpToNextStep(1500);
                            break;

                        case 53:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                                lichKing->AI()->Talk(SAY_LIGHT_OF_DAWN57);

							if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
								tirion->SetFacingTo(2.520523f);

							JumpToNextStep(1000);
                            break;

                        case 54:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                            {
                                lichKing->SetSpeedRate(MOVE_RUN, 1.0f);
                                lichKing->SetWalk(true);
                                lichKing->GetMotionMaster()->MovePoint(0, LightofDawnLoc[29]);
                            }
                            JumpToNextStep(4000);
                            break;

                        case 55:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                                lichKing->SetStandState(UNIT_STAND_STATE_KNEEL);
                            JumpToNextStep(2000);
                            break;

                        case 56:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                                lichKing->SetStandState(UNIT_STAND_STATE_STAND);
                            JumpToNextStep(1500);
                            break;

                        case 57:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                                lichKing->AI()->Talk(SAY_LIGHT_OF_DAWN58);
                            JumpToNextStep(10000);
                            break;

                        case 58:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                                lichKing->AI()->Talk(SAY_LIGHT_OF_DAWN59);
                            JumpToNextStep(10000);
                            break;

                        case 59:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                                lichKing->CastSpell(lichKing, SPELL_TELEPORT_VISUAL, false);

                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                            {
                                tirion->SetEmoteState(EMOTE_ONESHOT_NONE);
                                tirion->SetSpeedRate(MOVE_RUN, 1.0f);
                                tirion->GetMotionMaster()->MovePoint(0, LightofDawnLoc[6]);
                            }
                            JumpToNextStep(2500);
                            break;

                        case 60:
                            if (Creature* lichKing = ObjectAccessor::GetCreature(*me, uiLichKingGUID))
                            {
                                lichKing->AI()->Talk(EMOTE_LIGHT_OF_DAWN17);
                                lichKing->KillSelf();
                            }
                            JumpToNextStep(10000);
                            break;

                        case 61:
                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                                tirion->AI()->Talk(SAY_LIGHT_OF_DAWN60);
                            JumpToNextStep(3000);
                            break;

                        case 62:
                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                            {
                                tirion->SetWalk(true);
                                tirion->GetMotionMaster()->MovePoint(0, LightofDawnLoc[7]);
                            }
                            JumpToNextStep(5500);
                            break;

                        case 63:
                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                            {
                                tirion->GetMotionMaster()->MovePoint(0, LightofDawnLoc[8]);
                                tirion->AI()->Talk(SAY_LIGHT_OF_DAWN61);
                            }
                            JumpToNextStep(15000);
                            break;

                        case 64:
                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                                tirion->AI()->Talk(SAY_LIGHT_OF_DAWN62);
                            JumpToNextStep(7000);
                            break;

                        case 65:
                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                                tirion->AI()->Talk(SAY_LIGHT_OF_DAWN63);
                            JumpToNextStep(10000);
                            break;

                        case 66:
                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                                tirion->AI()->Talk(SAY_LIGHT_OF_DAWN64);
                            JumpToNextStep(11000);
                            break;

                        case 67:
                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                                tirion->AI()->Talk(SAY_LIGHT_OF_DAWN65);
                            JumpToNextStep(10000);
                            break;

                        case 68:
                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                                tirion->AI()->Talk(SAY_LIGHT_OF_DAWN66);
                            JumpToNextStep(8000);
                            break;

                        case 69:
                            if (Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID))
                                tirion->AI()->Talk(SAY_LIGHT_OF_DAWN67);
                            JumpToNextStep(3000);
                            break;

                        case 70:
                        {
                            Creature* darion = me;
                            if (!darion->IsAlive())
                                darion->Respawn();

                            if (darion)
                            {
                                darion->SetVisible(true);
                                darion->SetStandState(UNIT_STAND_STATE_STAND);
                                darion->AI()->Talk(SAY_LIGHT_OF_DAWN68);
                            }
                            JumpToNextStep(13000);
                            break;
                        }

                        case 71:
                        {
                            me->InterruptNonMeleeSpells(false);
                            me->AttackStop();
                            me->CombatStop(true);
                            me->GetThreatManager().ClearAllThreat();
                            me->SetTarget(ObjectGuid::Empty);
                            me->GetMotionMaster()->Clear();
                            me->GetMotionMaster()->MoveIdle();
                            me->RemoveAllAuras();
                            me->SetVisible(true);
                            me->SetHealth(me->GetMaxHealth());
                            me->SetStandState(UNIT_STAND_STATE_STAND);
                            me->SetWalk(true);
                            me->SetSpeedRate(MOVE_RUN, 1.0f);
                            me->Dismount();
                            me->SetHomePosition(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation());
							me->GetMotionMaster()->MovePoint(0, LightofDawnLoc[24]);

                            Map::PlayerList const& PlayerList = me->GetMap()->GetPlayers();

                            if (!PlayerList.isEmpty())
                            {
                                for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
                                {
                                    Player* player = i->GetSource();

                                    if (!player)
                                        continue;

                                    if (player->GetQuestStatus(QUEST_THE_LIGHT_OF_DAWN) != QUEST_STATUS_INCOMPLETE)
                                    {
                                        continue;
                                    }

                                    if (!IsNearLightOfDawnBattlefield(player))
                                    {
                                        continue;
                                    }

                                    player->CastSpell(player, SPELL_THE_LIGHT_OF_DAWN_Q, true);

                                    if (player->GetQuestStatus(QUEST_THE_LIGHT_OF_DAWN) == QUEST_STATUS_INCOMPLETE)
                                    {
                                        player->AreaExploredOrEventHappens(QUEST_THE_LIGHT_OF_DAWN);
                                    }
                                }
                            }
                            JumpToNextStep(6000);
                            break;
                        }

                        case 72:
							me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
							me->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
							me->Mount(25279);

							if (Creature* koltira = ObjectAccessor::GetCreature(*me, uiKoltiraGUID))
                            {
                                koltira->Mount(25279);
                                koltira->SetStandState(UNIT_STAND_STATE_STAND);
								koltira->SetFacingTo(1.558f);
                            }
                            if (Creature* thassarian = ObjectAccessor::GetCreature(*me, uiThassarianGUID))
                            {
                                thassarian->Mount(25279);
                                thassarian->SetStandState(UNIT_STAND_STATE_STAND);
								thassarian->SetFacingTo(1.558f);
                            }
							if (Creature* orbaz = ObjectAccessor::GetCreature(*me, uiOrbazGUID))
                            {
                                orbaz->Mount(25279);
                                orbaz->SetStandState(UNIT_STAND_STATE_STAND);
								orbaz->SetFacingTo(1.558f);
                            }

                            JumpToNextStep(60000);
                            break;

                        case 73:
                            CleanupLightOfDawnEventState();
                            me->DespawnOrUnsummon();
                            break;

                        default:
                            break;
                    }
        }

        bool IsActiveBattleSerial(uint32 serial) const
        {
            return serial == uiEventSerial && bIsBattle;
        }

        void ScheduleCurrentStep(uint32 uiTimer)
        {
            uint32 serial = uiEventSerial;
            _scheduler.Schedule(Milliseconds(uiTimer), [this, serial](TaskContext /*context*/)
            {
                if (serial != uiEventSerial || bIsBattle)
                {
                    return;
                }

                bSceneStepReady = true;
            });
        }

        bool SendTirionBattleSkipMessage()
        {
                    Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID);

                    if (!tirion)
                    {
                        tirion = me->SummonCreature(NPC_HIGHLORD_TIRION_FORDRING, LightofDawnLoc[0].GetPositionWithOffset({ 0.0f, 0.0f, 0.0f, 1.528f }), TEMPSUMMON_MANUAL_DESPAWN, 0s);
                        if (!tirion)
                        {
                            return false;
                        }

                        uiTirionGUID = tirion->GetGUID();
                    }

                    tirion->SetFaction(me->GetFaction());
                    tirion->SetVirtualItem(0, uint32(EQUIP_UNEQUIP));
                    tirion->AI()->Talk(SAY_LIGHT_OF_DAWN25);

                    return true;
        }

        void RequestBattleFinishAfterTirionMessage(uint32 serial)
        {
                    if (!IsActiveBattleSerial(serial))
                    {
                        return;
                    }

                    if (bBattleFinishRequested)
                    {
                        return;
                    }

                    if (!SendTirionBattleSkipMessage())
                    {
                        return;
                    }

                    bBattleFinishRequested = true;

                    _scheduler.Schedule(Seconds(1), [this, serial](TaskContext /*context*/)
                    {
                        if (IsActiveBattleSerial(serial))
                        {
                            FinishLightOfDawnBattle();
                        }
                    });
        }


        void FinishLightOfDawnBattle()
        {
                    if (!bIsBattle)
                    {
                        return;
                    }

                    bBattleFinishRequested = true;
                    bIsBattle = false;

                    Creature* darion = me;
                    if (!darion->IsAlive())
                        darion->Respawn();

                    if (!darion)
                    {
                        bIsBattle = true;
                        _scheduler.Schedule(Seconds(1), [this](TaskContext /*context*/)
                        {
                            FinishLightOfDawnBattle();
                        });
                        return;
                    }

                    if (darion->HasAura(SPELL_THE_MIGHT_OF_MOGRAINE))
                        darion->RemoveAurasDueToSpell(SPELL_THE_MIGHT_OF_MOGRAINE);

                    darion->RemoveAllAuras();
                    darion->CombatStop(true);
                    darion->AttackStop();
                    darion->InterruptNonMeleeSpells(false);
                    darion->SetWalk(false);

                    EngagementOver();

                    for (uint8 i = 0; i < ENCOUNTER_DEFENDER_NUMBER; ++i)
                        DespawnNPC(uiDefenderGUID[i]);
                    for (uint8 i = 0; i < ENCOUNTER_EARTHSHATTER_NUMBER; ++i)
                        DespawnNPC(uiEarthshatterGUID[i]);
                    for (uint8 i = 0; i < ENCOUNTER_ABOMINATION_NUMBER; ++i)
                        DespawnNPC(uiAbominationGUID[i]);
                    for (uint8 i = 0; i < ENCOUNTER_BEHEMOTH_NUMBER; ++i)
                        DespawnNPC(uiBehemothGUID[i]);
                    for (uint8 i = 0; i < ENCOUNTER_GHOUL_NUMBER; ++i)
                        DespawnNPC(uiGhoulGUID[i]);
                    for (uint8 i = 0; i < ENCOUNTER_WARRIOR_NUMBER; ++i)
                        DespawnNPC(uiWarriorGUID[i]);

                    if (Creature* korfax = ObjectAccessor::GetCreature(*me, uiKorfaxGUID))
                    {
                        korfax->RemoveAllAuras();
                        korfax->CombatStop(true);
                        korfax->AttackStop();
                        korfax->SetFaction(me->GetFaction());
                        korfax->SetWalk(false);
                        korfax->GetMotionMaster()->MovePoint(0, LightofDawnLoc[9]);
                    }

                    if (Creature* maxwell = ObjectAccessor::GetCreature(*me, uiMaxwellGUID))
                    {
                        maxwell->RemoveAllAuras();
                        maxwell->CombatStop(true);
                        maxwell->AttackStop();
                        maxwell->SetFaction(me->GetFaction());
                        maxwell->SetWalk(false);
                        maxwell->GetMotionMaster()->MovePoint(0, LightofDawnLoc[12]);
                    }

                    if (Creature* eligor = ObjectAccessor::GetCreature(*me, uiEligorGUID))
                    {
                        eligor->RemoveAllAuras();
                        eligor->CombatStop(true);
                        eligor->AttackStop();
                        eligor->SetFaction(me->GetFaction());
                        eligor->SetWalk(false);
                        eligor->GetMotionMaster()->MovePoint(0, LightofDawnLoc[15]);
                    }

                    DespawnNPC(uiRayneGUID);

                    Creature* tirion = ObjectAccessor::GetCreature(*me, uiTirionGUID);
                    if (!tirion || !tirion->IsAlive())
                    {
                        if (tirion)
                            tirion->DespawnOrUnsummon();

                        uiTirionGUID.Clear();
                        tirion = me->SummonCreature(NPC_HIGHLORD_TIRION_FORDRING, LightofDawnLoc[0], TEMPSUMMON_MANUAL_DESPAWN, 0s);
                        if (tirion)
                            uiTirionGUID = tirion->GetGUID();
                    }

                    if (tirion)
                    {
                        tirion->SetFaction(me->GetFaction());
                        tirion->SetVisible(true);
                        tirion->RemoveAllAuras();
                        tirion->CombatStop(true);
                        tirion->AttackStop();
                        tirion->InterruptNonMeleeSpells(false);
                        tirion->SetWalk(true);
                        tirion->SetSpeedRate(MOVE_RUN, 1.0f);
                        tirion->SetStandState(UNIT_STAND_STATE_STAND);
                        tirion->GetMotionMaster()->Clear();
                        tirion->GetMotionMaster()->MoveIdle();
                        tirion->NearTeleportTo(LightofDawnLoc[0].GetPositionX(), LightofDawnLoc[0].GetPositionY(), LightofDawnLoc[0].GetPositionZ(), LightofDawnLoc[0].GetOrientation());
                        tirion->SetHomePosition(LightofDawnLoc[0].GetPositionX(), LightofDawnLoc[0].GetPositionY(), LightofDawnLoc[0].GetPositionZ(), LightofDawnLoc[0].GetOrientation());
                    }

                    if (darion)
                    {
                        if (!darion->IsAlive())
                            darion->Respawn();
                        darion->InterruptNonMeleeSpells(false);
                        darion->AttackStop();
                        darion->CombatStop(true);
                        darion->GetThreatManager().ClearAllThreat();
                        darion->SetTarget(ObjectGuid::Empty);
                        darion->GetMotionMaster()->Clear();
                        darion->GetMotionMaster()->MoveIdle();
                        darion->RemoveAllAuras();
                        darion->SetVisible(true);
                        darion->SetHealth(darion->GetMaxHealth());
                        darion->SetFaction(me->GetFaction());
                        darion->SetReactState(REACT_AGGRESSIVE);
                        darion->SetStandState(UNIT_STAND_STATE_STAND);
                        darion->SetWalk(false);
                        darion->SetSpeedRate(MOVE_RUN, 1.0f);
                        darion->Dismount();
                        darion->SetHomePosition(LightofDawnLoc[24].GetPositionX(), LightofDawnLoc[24].GetPositionY(), LightofDawnLoc[24].GetPositionZ(), LightofDawnLoc[24].GetOrientation());

                        if (tirion)
                            tirion->CastSpell(darion, SPELL_THE_LIGHT_OF_DAWN, false);
                        else
                            darion->CastSpell(darion, SPELL_THE_LIGHT_OF_DAWN, false);

                        darion->GetMotionMaster()->MovePoint(0, LightofDawnLoc[24]);
                    }

                    if (Creature* koltira = ObjectAccessor::GetCreature(*me, uiKoltiraGUID))
                    {
                        if (!koltira->IsAlive())
                            koltira->Respawn();
                        koltira->InterruptNonMeleeSpells(false);
                        koltira->AttackStop();
                        koltira->CombatStop(true);
                        koltira->GetThreatManager().ClearAllThreat();
                        koltira->SetTarget(ObjectGuid::Empty);
                        koltira->GetMotionMaster()->Clear();
                        koltira->GetMotionMaster()->MoveIdle();
                        koltira->RemoveAllAuras();
                        koltira->SetVisible(true);
                        koltira->SetHealth(koltira->GetMaxHealth());
                        koltira->SetFaction(me->GetFaction());
                        koltira->SetReactState(REACT_AGGRESSIVE);
                        koltira->SetStandState(UNIT_STAND_STATE_STAND);
                        koltira->SetWalk(false);
                        koltira->SetSpeedRate(MOVE_RUN, 1.0f);
                        koltira->Mount(25279);
                        koltira->SetHomePosition(LightofDawnLoc[18].GetPositionX(), LightofDawnLoc[18].GetPositionY(), LightofDawnLoc[18].GetPositionZ(), LightofDawnLoc[18].GetOrientation());

                        if (tirion)
                            tirion->CastSpell(koltira, SPELL_THE_LIGHT_OF_DAWN, false);
                        else
                            koltira->CastSpell(koltira, SPELL_THE_LIGHT_OF_DAWN, false);

                        koltira->GetMotionMaster()->MovePoint(0, LightofDawnLoc[18]);
                    }

                    if (Creature* orbaz = ObjectAccessor::GetCreature(*me, uiOrbazGUID))
                    {
                        if (!orbaz->IsAlive())
                            orbaz->Respawn();
                        orbaz->InterruptNonMeleeSpells(false);
                        orbaz->AttackStop();
                        orbaz->CombatStop(true);
                        orbaz->GetThreatManager().ClearAllThreat();
                        orbaz->SetTarget(ObjectGuid::Empty);
                        orbaz->GetMotionMaster()->Clear();
                        orbaz->GetMotionMaster()->MoveIdle();
                        orbaz->RemoveAllAuras();
                        orbaz->SetVisible(true);
                        orbaz->SetHealth(orbaz->GetMaxHealth());
                        orbaz->SetFaction(me->GetFaction());
                        orbaz->SetReactState(REACT_AGGRESSIVE);
                        orbaz->SetStandState(UNIT_STAND_STATE_STAND);
                        orbaz->SetWalk(false);
                        orbaz->SetSpeedRate(MOVE_RUN, 1.0f);
                        orbaz->Mount(25279);
                        orbaz->SetHomePosition(OrbazBloodbaneReturnLoc.GetPositionX(), OrbazBloodbaneReturnLoc.GetPositionY(), OrbazBloodbaneReturnLoc.GetPositionZ(), OrbazBloodbaneReturnLoc.GetOrientation());

                        if (tirion)
                            tirion->CastSpell(orbaz, SPELL_THE_LIGHT_OF_DAWN, false);
                        else
                            orbaz->CastSpell(orbaz, SPELL_THE_LIGHT_OF_DAWN, false);

                        orbaz->AI()->Talk(EMOTE_LIGHT_OF_DAWN04);
                        orbaz->GetMotionMaster()->MovePoint(0, OrbazBloodbaneReturnLoc);
                    }

                    if (Creature* thassarian = ObjectAccessor::GetCreature(*me, uiThassarianGUID))
                    {
                        if (!thassarian->IsAlive())
                            thassarian->Respawn();
                        thassarian->InterruptNonMeleeSpells(false);
                        thassarian->AttackStop();
                        thassarian->CombatStop(true);
                        thassarian->GetThreatManager().ClearAllThreat();
                        thassarian->SetTarget(ObjectGuid::Empty);
                        thassarian->GetMotionMaster()->Clear();
                        thassarian->GetMotionMaster()->MoveIdle();
                        thassarian->RemoveAllAuras();
                        thassarian->SetVisible(true);
                        thassarian->SetHealth(thassarian->GetMaxHealth());
                        thassarian->SetFaction(me->GetFaction());
                        thassarian->SetReactState(REACT_AGGRESSIVE);
                        thassarian->SetStandState(UNIT_STAND_STATE_STAND);
                        thassarian->SetWalk(false);
                        thassarian->SetSpeedRate(MOVE_RUN, 1.0f);
                        thassarian->Mount(25279);
                        thassarian->SetHomePosition(LightofDawnLoc[20].GetPositionX(), LightofDawnLoc[20].GetPositionY(), LightofDawnLoc[20].GetPositionZ(), LightofDawnLoc[20].GetOrientation());

                        if (tirion)
                            tirion->CastSpell(thassarian, SPELL_THE_LIGHT_OF_DAWN, false);
                        else
                            thassarian->CastSpell(thassarian, SPELL_THE_LIGHT_OF_DAWN, false);

                        thassarian->GetMotionMaster()->MovePoint(0, LightofDawnLoc[20]);
                    }

                    if (tirion)
                    {
                        tirion->AI()->Talk(SAY_LIGHT_OF_DAWN26);
                    }

                    bBattleFinishedForScene = true;
                    uiStep = 10;
                    ScheduleCurrentStep(8000);
        }

        void ScheduleAntiMagicZone(uint32 serial, Milliseconds delay)
        {
            _scheduler.Schedule(delay, [this, serial](TaskContext /*context*/)
            {
                if (!IsActiveBattleSerial(serial))
                {
                    return;
                }

                Creature* darion = me;
                if (!darion->IsAlive())
                    darion->Respawn();

                if (darion)
                    darion->CastSpell(darion, SPELL_ANTI_MAGIC_ZONE1, false);
                ScheduleAntiMagicZone(serial, Milliseconds(urand(25000, 30000)));
            });
        }

        void ScheduleDeathStrike(uint32 serial, Milliseconds delay)
        {
            _scheduler.Schedule(delay, [this, serial](TaskContext /*context*/)
            {
                if (!IsActiveBattleSerial(serial))
                {
                    return;
                }

                Creature* darion = me;
                if (!darion->IsAlive())
                    darion->Respawn();

                if (darion)
                    if (Unit* victim = darion->GetVictim())
                        darion->CastSpell(victim, SPELL_DEATH_STRIKE, false);
                ScheduleDeathStrike(serial, Milliseconds(urand(5000, 10000)));
            });
        }

        void ScheduleDeathEmbrace(uint32 serial, Milliseconds delay)
        {
            _scheduler.Schedule(delay, [this, serial](TaskContext /*context*/)
            {
                if (!IsActiveBattleSerial(serial))
                {
                    return;
                }

                Creature* darion = me;
                if (!darion->IsAlive())
                    darion->Respawn();

                if (darion)
                    if (Unit* victim = darion->GetVictim())
                        darion->CastSpell(victim, SPELL_DEATH_EMBRACE, false);
                ScheduleDeathEmbrace(serial, Milliseconds(urand(5000, 10000)));
            });
        }

        void ScheduleIcyTouch(uint32 serial, Milliseconds delay)
        {
            _scheduler.Schedule(delay, [this, serial](TaskContext /*context*/)
            {
                if (!IsActiveBattleSerial(serial))
                {
                    return;
                }

                Creature* darion = me;
                if (!darion->IsAlive())
                    darion->Respawn();

                if (darion)
                    if (Unit* victim = darion->GetVictim())
                        darion->CastSpell(victim, SPELL_ICY_TOUCH1, false);
                ScheduleIcyTouch(serial, Milliseconds(urand(5000, 10000)));
            });
        }

        void ScheduleUnholyBlight(uint32 serial, Milliseconds delay)
        {
            _scheduler.Schedule(delay, [this, serial](TaskContext /*context*/)
            {
                if (!IsActiveBattleSerial(serial))
                {
                    return;
                }

                Creature* darion = me;
                if (!darion->IsAlive())
                    darion->Respawn();

                if (darion)
                    if (Unit* victim = darion->GetVictim())
                        darion->CastSpell(victim, SPELL_UNHOLY_BLIGHT, false);
                ScheduleUnholyBlight(serial, Milliseconds(urand(5000, 10000)));
            });
        }

        void ScheduleFightSpeech(uint32 serial, Milliseconds delay)
        {
            _scheduler.Schedule(delay, [this, serial](TaskContext /*context*/)
            {
                if (!IsActiveBattleSerial(serial))
                {
                    return;
                }

                Creature* darion = me;
                if (!darion->IsAlive())
                    darion->Respawn();

                if (darion)
                    darion->AI()->Talk(SAY_LIGHT_OF_DAWN09);
                ScheduleFightSpeech(serial, Milliseconds(urand(15000, 20000)));
            });
        }

        void ScheduleBattleLeash(uint32 serial)
        {
            _scheduler.Schedule(Milliseconds(LIGHT_OF_DAWN_BATTLE_LEASH_TIMER), [this, serial](TaskContext /*context*/)
            {
                if (!IsActiveBattleSerial(serial))
                {
                    return;
                }

                KeepBattlefieldInsideLeash();
                ScheduleBattleLeash(serial);
            });
        }

        void ScheduleBattleSpawn(uint32 serial)
        {
            _scheduler.Schedule(Seconds(1), [this, serial](TaskContext /*context*/)
            {
                if (!IsActiveBattleSerial(serial))
                {
                    return;
                }

                SpawnNPC();
                LockAllBattleHomePositions();
                ScheduleBattleSpawn(serial);
            });
        }

        void ScheduleTargetRefresh(uint32 serial)
        {
            _scheduler.Schedule(Milliseconds(LIGHT_OF_DAWN_TARGET_CHECK_TIMER), [this, serial](TaskContext /*context*/)
            {
                if (!IsActiveBattleSerial(serial))
                {
                    return;
                }

                for (uint8 i = 0; i < ENCOUNTER_GHOUL_NUMBER; ++i)
                    NPCChangeTarget(uiGhoulGUID[i]);
                for (uint8 i = 0; i < ENCOUNTER_WARRIOR_NUMBER; ++i)
                    NPCChangeTarget(uiWarriorGUID[i]);
                for (uint8 i = 0; i < ENCOUNTER_ABOMINATION_NUMBER; ++i)
                    NPCChangeTarget(uiAbominationGUID[i]);
                for (uint8 i = 0; i < ENCOUNTER_BEHEMOTH_NUMBER; ++i)
                    NPCChangeTarget(uiBehemothGUID[i]);

                NPCChangeTarget(uiKoltiraGUID);
                NPCChangeTarget(uiOrbazGUID);
                NPCChangeTarget(uiThassarianGUID);
                NPCChangeTarget(me->GetGUID());

                for (uint8 i = 0; i < ENCOUNTER_DEFENDER_NUMBER; ++i)
                    NPCChangeTarget(uiDefenderGUID[i]);
                for (uint8 i = 0; i < ENCOUNTER_EARTHSHATTER_NUMBER; ++i)
                    NPCChangeTarget(uiEarthshatterGUID[i]);

                NPCChangeTarget(uiKorfaxGUID);
                NPCChangeTarget(uiMaxwellGUID);
                NPCChangeTarget(uiEligorGUID);
                NPCChangeTarget(uiRayneGUID);

                ScheduleTargetRefresh(serial);
            });
        }

        void StartBattleSchedules()
        {
            uint32 serial = uiEventSerial;

            ScheduleAntiMagicZone(serial, Milliseconds(urand(1000, 6000)));
            ScheduleDeathStrike(serial, Milliseconds(urand(5000, 10000)));
            ScheduleDeathEmbrace(serial, Milliseconds(urand(5000, 10000)));
            ScheduleIcyTouch(serial, Milliseconds(urand(5000, 10000)));
            ScheduleUnholyBlight(serial, Milliseconds(urand(5000, 10000)));
            ScheduleFightSpeech(serial, Milliseconds(15000));
            ScheduleBattleLeash(serial);
            ScheduleBattleSpawn(serial);
            ScheduleTargetRefresh(serial);

            _scheduler.Schedule(Milliseconds(LIGHT_OF_DAWN_TIRION_MESSAGE_SKIP_TIMER), [this, serial](TaskContext /*context*/)
            {
                RequestBattleFinishAfterTirionMessage(serial);
            });

            _scheduler.Schedule(Seconds(300), [this, serial](TaskContext /*context*/)
            {
                if (IsActiveBattleSerial(serial))
                    FinishLightOfDawnBattle();
            });
        }

        void JumpToNextStep(uint32 uiTimer)
        {
            ++uiStep;
            ScheduleCurrentStep(uiTimer);
        }

        void LockBattleHomePosition(Creature* creature)
        {
            if (!creature)
                return;

            if (!creature->IsAlive())
                return;

            creature->SetHomePosition(
                creature->GetPositionX(),
                creature->GetPositionY(),
                creature->GetPositionZ(),
                creature->GetOrientation()
            );
        }

        void LockBattleHomePosition(ObjectGuid guid)
        {
            if (Creature* creature = ObjectAccessor::GetCreature(*me, guid))
                LockBattleHomePosition(creature);
        }

        void KeepCreatureInsideBattlefield(Creature* creature)
        {
            if (!creature)
                return;

            if (!creature->IsAlive())
                return;

            if (IsWithinLightOfDawnRadius(creature, LightofDawnLoc[0], LIGHT_OF_DAWN_BATTLE_LEASH_RADIUS))
                return;

            Position const& position = LightofDawnLoc[0];

            // 전투지역 밖으로 밀려난 몹은 기존 전투/위협/대상/이동 경로를 모두 끊고
            // 전투 중심으로 보정한다. 이전 chase/path/threat가 남으면 순간이동 후에도
            // 다시 바깥 대상이나 이전 경로를 따라가며 이벤트가 꼬일 수 있다.
            creature->InterruptNonMeleeSpells(false);
            creature->AttackStop();
            creature->CombatStop(true);
            creature->GetThreatManager().ClearAllThreat();
            creature->SetTarget(ObjectGuid::Empty);

            creature->GetMotionMaster()->Clear();
            creature->GetMotionMaster()->MoveIdle();

            creature->SetReactState(REACT_AGGRESSIVE);
            creature->SetWalk(false);
            creature->SetSpeedRate(MOVE_RUN, 1.0f);
            creature->SetStandState(UNIT_STAND_STATE_STAND);

            creature->NearTeleportTo(position.GetPositionX(), position.GetPositionY(), position.GetPositionZ(), creature->GetOrientation());
            creature->SetHomePosition(position.GetPositionX(), position.GetPositionY(), position.GetPositionZ(), creature->GetOrientation());

            // 순간이동 과정에서 다시 걸릴 수 있는 잔여 전투 상태까지 한 번 더 제거한다.
            creature->AttackStop();
            creature->CombatStop(true);
            creature->GetThreatManager().ClearAllThreat();
            creature->SetTarget(ObjectGuid::Empty);
            creature->GetMotionMaster()->Clear();
            creature->GetMotionMaster()->MoveIdle();
        }

        void KeepCreatureInsideBattlefield(ObjectGuid guid)
        {
            if (Creature* creature = ObjectAccessor::GetCreature(*me, guid))
                KeepCreatureInsideBattlefield(creature);
        }

        void KeepBattlefieldInsideLeash()
        {
            KeepCreatureInsideBattlefield(me);

            KeepCreatureInsideBattlefield(uiKoltiraGUID);
            KeepCreatureInsideBattlefield(uiOrbazGUID);
            KeepCreatureInsideBattlefield(uiThassarianGUID);

            KeepCreatureInsideBattlefield(uiKorfaxGUID);
            KeepCreatureInsideBattlefield(uiMaxwellGUID);
            KeepCreatureInsideBattlefield(uiEligorGUID);
            KeepCreatureInsideBattlefield(uiRayneGUID);

            for (uint8 i = 0; i < ENCOUNTER_GHOUL_NUMBER; ++i)
                KeepCreatureInsideBattlefield(uiGhoulGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_WARRIOR_NUMBER; ++i)
                KeepCreatureInsideBattlefield(uiWarriorGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_ABOMINATION_NUMBER; ++i)
                KeepCreatureInsideBattlefield(uiAbominationGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_BEHEMOTH_NUMBER; ++i)
                KeepCreatureInsideBattlefield(uiBehemothGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_DEFENDER_NUMBER; ++i)
                KeepCreatureInsideBattlefield(uiDefenderGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_EARTHSHATTER_NUMBER; ++i)
                KeepCreatureInsideBattlefield(uiEarthshatterGUID[i]);
        }

        void LockAllBattleHomePositions()
        {
            LockBattleHomePosition(me);
            LockBattleHomePosition(me);

            LockBattleHomePosition(uiKoltiraGUID);
            LockBattleHomePosition(uiOrbazGUID);
            LockBattleHomePosition(uiThassarianGUID);

            LockBattleHomePosition(uiKorfaxGUID);
            LockBattleHomePosition(uiMaxwellGUID);
            LockBattleHomePosition(uiEligorGUID);
            LockBattleHomePosition(uiRayneGUID);

            for (uint8 i = 0; i < ENCOUNTER_GHOUL_NUMBER; ++i)
                LockBattleHomePosition(uiGhoulGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_WARRIOR_NUMBER; ++i)
                LockBattleHomePosition(uiWarriorGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_ABOMINATION_NUMBER; ++i)
                LockBattleHomePosition(uiAbominationGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_BEHEMOTH_NUMBER; ++i)
                LockBattleHomePosition(uiBehemothGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_DEFENDER_NUMBER; ++i)
                LockBattleHomePosition(uiDefenderGUID[i]);

            for (uint8 i = 0; i < ENCOUNTER_EARTHSHATTER_NUMBER; ++i)
                LockBattleHomePosition(uiEarthshatterGUID[i]);
        }

        bool IsScourgeGuid(ObjectGuid guid)
        {
            if (guid == me->GetGUID())
                return true;

            if (guid == uiKoltiraGUID || guid == uiOrbazGUID || guid == uiThassarianGUID)
                return true;

            for (uint8 i = 0; i < ENCOUNTER_GHOUL_NUMBER; ++i)
                if (guid == uiGhoulGUID[i])
                    return true;

            for (uint8 i = 0; i < ENCOUNTER_WARRIOR_NUMBER; ++i)
                if (guid == uiWarriorGUID[i])
                    return true;

            for (uint8 i = 0; i < ENCOUNTER_ABOMINATION_NUMBER; ++i)
                if (guid == uiAbominationGUID[i])
                    return true;

            for (uint8 i = 0; i < ENCOUNTER_BEHEMOTH_NUMBER; ++i)
                if (guid == uiBehemothGUID[i])
                    return true;

            return false;
        }

        Creature* GetValidBattleTarget(Creature* attacker, ObjectGuid guid)
        {
            if (!attacker)
                return nullptr;

            Creature* target = ObjectAccessor::GetCreature(*me, guid);

            if (!target)
                return nullptr;

            if (target == attacker)
                return nullptr;

            if (!target->IsAlive())
                return nullptr;

            if (!attacker->IsWithinDistInMap(target, LIGHT_OF_DAWN_BATTLE_TARGET_RADIUS))
                return nullptr;

            return target;
        }

        Creature* SelectDawnTarget(Creature* attacker)
        {
            if (Creature* target = GetValidBattleTarget(attacker, uiKorfaxGUID))
                return target;

            if (Creature* target = GetValidBattleTarget(attacker, uiMaxwellGUID))
                return target;

            if (Creature* target = GetValidBattleTarget(attacker, uiEligorGUID))
                return target;

            if (Creature* target = GetValidBattleTarget(attacker, uiRayneGUID))
                return target;

            for (uint8 i = 0; i < ENCOUNTER_DEFENDER_NUMBER; ++i)
                if (Creature* target = GetValidBattleTarget(attacker, uiDefenderGUID[i]))
                    return target;

            for (uint8 i = 0; i < ENCOUNTER_EARTHSHATTER_NUMBER; ++i)
                if (Creature* target = GetValidBattleTarget(attacker, uiEarthshatterGUID[i]))
                    return target;

            return nullptr;
        }

        Creature* SelectScourgeTarget(Creature* attacker)
        {
            if (Creature* target = GetValidBattleTarget(attacker, uiKoltiraGUID))
                return target;

            if (Creature* target = GetValidBattleTarget(attacker, uiOrbazGUID))
                return target;

            if (Creature* target = GetValidBattleTarget(attacker, uiThassarianGUID))
                return target;

            if (Creature* target = GetValidBattleTarget(attacker, me->GetGUID()))
                return target;

            for (uint8 i = 0; i < ENCOUNTER_GHOUL_NUMBER; ++i)
                if (Creature* target = GetValidBattleTarget(attacker, uiGhoulGUID[i]))
                    return target;

            for (uint8 i = 0; i < ENCOUNTER_WARRIOR_NUMBER; ++i)
                if (Creature* target = GetValidBattleTarget(attacker, uiWarriorGUID[i]))
                    return target;

            for (uint8 i = 0; i < ENCOUNTER_ABOMINATION_NUMBER; ++i)
                if (Creature* target = GetValidBattleTarget(attacker, uiAbominationGUID[i]))
                    return target;

            for (uint8 i = 0; i < ENCOUNTER_BEHEMOTH_NUMBER; ++i)
                if (Creature* target = GetValidBattleTarget(attacker, uiBehemothGUID[i]))
                    return target;

            return nullptr;
        }

        void NPCChangeTarget(ObjectGuid ui_GUID)
        {
            Creature* attacker = ObjectAccessor::GetCreature(*me, ui_GUID);

            if (!attacker)
            {
                return;
            }

            if (!attacker->IsAlive())
            {
                return;
            }

            Creature* target = IsScourgeGuid(ui_GUID) ? SelectDawnTarget(attacker) : SelectScourgeTarget(attacker);

            if (!target)
            {
                return;
            }

            LockBattleHomePosition(attacker);
            LockBattleHomePosition(target);

            attacker->SetReactState(REACT_AGGRESSIVE);

            AddThreat(target, 1000.0f, attacker);
            attacker->AI()->AttackStart(target);
        }

        void SpawnNPC()
        {
            for (uint8 i = 0; i < ENCOUNTER_GHOUL_NUMBER; ++i)
            {
                Creature* ghoul = ObjectAccessor::GetCreature(*me, uiGhoulGUID[i]);
                if (!ghoul)
                {
                    if (Creature* summonedGhoul = me->SummonCreature(NPC_ACHERUS_GHOUL, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }), TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                    {
                        summonedGhoul->SetFaction(FACTION_UNDEAD_SCOURGE_3);
                        uiGhoulGUID[i] = summonedGhoul->GetGUID();
                    }
                }
            }

            for (uint8 i = 0; i < ENCOUNTER_ABOMINATION_NUMBER; ++i)
            {
                Creature* abomination = ObjectAccessor::GetCreature(*me, uiAbominationGUID[i]);
                if (!abomination)
                {
                    if (Creature* summonedAbomination = me->SummonCreature(NPC_RAMPAGING_ABOMINATION, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }), TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                    {
                        summonedAbomination->SetFaction(FACTION_UNDEAD_SCOURGE_3);
                        uiAbominationGUID[i] = summonedAbomination->GetGUID();
                    }
                }
            }

            for (uint8 i = 0; i < ENCOUNTER_WARRIOR_NUMBER; ++i)
            {
                Creature* warrior = ObjectAccessor::GetCreature(*me, uiWarriorGUID[i]);
                if (!warrior)
                {
                    if (Creature* summonedWarrior = me->SummonCreature(NPC_WARRIOR_OF_THE_FROZEN_WASTES, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }), TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                    {
                        summonedWarrior->SetFaction(FACTION_UNDEAD_SCOURGE_3);
                        uiWarriorGUID[i] = summonedWarrior->GetGUID();
                    }
                }
            }

            for (uint8 i = 0; i < ENCOUNTER_BEHEMOTH_NUMBER; ++i)
            {
                Creature* behemoth = ObjectAccessor::GetCreature(*me, uiBehemothGUID[i]);
                if (!behemoth)
                {
                    if (Creature* summonedBehemoth = me->SummonCreature(NPC_FLESH_BEHEMOTH, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }), TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                    {
                        summonedBehemoth->SetFaction(FACTION_UNDEAD_SCOURGE_3);
                        uiBehemothGUID[i] = summonedBehemoth->GetGUID();
                    }
                }
            }

            for (uint8 i = 0; i < ENCOUNTER_DEFENDER_NUMBER; ++i)
            {
                Creature* defender = ObjectAccessor::GetCreature(*me, uiDefenderGUID[i]);
                if (!defender)
                {
                    if (Creature* summonedDefender = me->SummonCreature(NPC_DEFENDER_OF_THE_LIGHT, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }), TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                    {
                        summonedDefender->SetFaction(FACTION_SCARLET_CRUSADE);
                        AddThreat(summonedDefender, 0.0f);
                        uiDefenderGUID[i] = summonedDefender->GetGUID();
                    }
                }
            }

            for (uint8 i = 0; i < ENCOUNTER_EARTHSHATTER_NUMBER; ++i)
            {
                Creature* earthshatter = ObjectAccessor::GetCreature(*me, uiEarthshatterGUID[i]);
                if (!earthshatter)
                {
                    if (Creature* summonedEarthshatter = me->SummonCreature(NPC_RIMBLAT_EARTHSHATTER, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }), TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                    {
                        summonedEarthshatter->SetFaction(FACTION_SCARLET_CRUSADE);
                        AddThreat(summonedEarthshatter, 0.0f);
                        uiEarthshatterGUID[i] = summonedEarthshatter->GetGUID();
                    }
                }
            }

            Creature* korfax = ObjectAccessor::GetCreature(*me, uiKorfaxGUID);
            if (!korfax)
            {
                if (Creature* summonedKorfax = me->SummonCreature(NPC_KORFAX_CHAMPION_OF_THE_LIGHT, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }), TEMPSUMMON_MANUAL_DESPAWN, 0s))
                {
                    summonedKorfax->SetFaction(FACTION_SCARLET_CRUSADE);
                    AddThreat(summonedKorfax, 0.0f);
                    uiKorfaxGUID = summonedKorfax->GetGUID();
                }
            }

            Creature* maxwell = ObjectAccessor::GetCreature(*me, uiMaxwellGUID);
            if (!maxwell)
            {
                if (Creature* summonedMaxwell = me->SummonCreature(NPC_LORD_MAXWELL_TYROSUS, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }), TEMPSUMMON_MANUAL_DESPAWN, 0s))
                {
                    summonedMaxwell->SetFaction(FACTION_SCARLET_CRUSADE);
                    AddThreat(summonedMaxwell, 0.0f);
                    uiMaxwellGUID = summonedMaxwell->GetGUID();
                }
            }

            Creature* eligor = ObjectAccessor::GetCreature(*me, uiEligorGUID);
            if (!eligor)
            {
                if (Creature* summonedEligor = me->SummonCreature(NPC_COMMANDER_ELIGOR_DAWNBRINGER, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }), TEMPSUMMON_MANUAL_DESPAWN, 0s))
                {
                    summonedEligor->SetFaction(FACTION_SCARLET_CRUSADE);
                    AddThreat(summonedEligor, 0.0f);
                    uiEligorGUID = summonedEligor->GetGUID();
                }
            }

            Creature* rayne = ObjectAccessor::GetCreature(*me, uiRayneGUID);
            if (!rayne)
            {
                if (Creature* summonedRayne = me->SummonCreature(NPC_RAYNE, LightofDawnLoc[0].GetPositionWithOffset({ float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), float(rand32() % LIGHT_OF_DAWN_BATTLE_SPAWN_OFFSET), 0.0f, 0.0f }), TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30min))
                {
                    summonedRayne->SetFaction(FACTION_SCARLET_CRUSADE);
                    AddThreat(summonedRayne, 0.0f);
                    uiRayneGUID = summonedRayne->GetGUID();
                }
            }
        }

        void DespawnNPC(ObjectGuid pGUID)
        {
            if (Creature* creature = ObjectAccessor::GetCreature(*me, pGUID))
            {
                if (creature->IsAlive())
                {
                    creature->SetVisible(false);
                    creature->KillSelf();
                }
            }
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            switch (action)
            {
                case GOSSIP_ACTION_INFO_DEF + 1:
                    CloseGossipMenuFor(player);
                    Initialize();
                    StartCaseNpcStateLog();
                    uiStep = 1;
                    me->GetMotionMaster()->Clear();
                    me->GetMotionMaster()->MoveIdle();
                    ScheduleCurrentStep(3000);
                    break;

                default:
                    break;
            }

            return true;
        }

        bool OnGossipHello(Player* player) override
        {

            if (me->IsQuestGiver())
                player->PrepareQuestMenu(me->GetGUID());

            if (player->GetQuestStatus(QUEST_THE_LIGHT_OF_DAWN) == QUEST_STATUS_INCOMPLETE)
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "전 준비되었습니다. 대영주님 바로 희망의 빛 예배당을 포위 공격합시다!", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
            }

            SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());

            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_highlord_darion_mograineAI(creature);
    }
};

class npc_the_lich_king_tirion_dawn : public CreatureScript
{
public:
    npc_the_lich_king_tirion_dawn() : CreatureScript("npc_the_lich_king_tirion_dawn") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_the_lich_king_tirion_dawnAI(creature);
    }

    struct npc_the_lich_king_tirion_dawnAI : public ScriptedAI
    {
        npc_the_lich_king_tirion_dawnAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override { }
        void AttackStart(Unit* /*who*/) override { }
        void UpdateAI(uint32 /*diff*/) override { }
        void JustDied(Unit* /*killer*/) override { }
    };
};

void AddSC_the_scarlet_enclave_c5()
{
    new npc_highlord_darion_mograine();
    new npc_the_lich_king_tirion_dawn();
}
