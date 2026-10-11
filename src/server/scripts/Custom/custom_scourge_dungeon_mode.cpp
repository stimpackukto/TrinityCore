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
 * Custom Update - 2026-08-02
 *
 * Scourge sabotage bomb-carrier flag aura
 *
 * - Uses the Warsong Gulch flag carrier auras as the bomb-carrier marker.
 * - Applies Warsong Flag 23333 to Alliance bomb carriers.
 * - Applies Silverwing Flag 23335 to Horde bomb carriers.
 * - Removes both flag auras when the bomb is installed, dropped, returned,
 *   the round ends, the next round starts, or the player leaves the match.
 * - Requires the map restriction exception in SpellInfo.cpp for active
 *   sabotage participants inside Stratholme map 329.
 */

/*
 * Custom Update - 2026-08-02
 *
 * Scourge sabotage round-end revive, freeze, and HUD recovery
 *
 * - Revives every participant as the first round-end action.
 * - Stops and roots all participants at their round-end positions.
 * - Releases the root at the next round start and on match exit.
 * - Returns the current sabotage HUD state with SCOURGE_MODE:GET.
 * - Keeps round-break players alive instead of returning them to spectator mode.
 */

/*
 * Custom Update - 2026-08-02
 *
 * Scourge sabotage best-of-three round system
 *
 * - Uses a 10-minute round timer before bomb installation.
 * - Starts a 60-second explosion timer after installation completes.
 * - Gives Team B the round when Team A is eliminated, the bomb is disarmed,
 *   or the 10-minute installation limit expires.
 * - Gives Team A the round when the installed bomb explodes.
 * - Randomly assigns the attacker and defender roles from round two onward.
 * - Plays up to three rounds and ends the match when one side reaches two wins.
 * - Sends round, score, phase, and remaining-time data to the LRSC addon UI.
 */

/*
 * Custom Update - 2026-08-02
 *
 * Scourge sabotage raid groups and invite lock
 *
 * - Removes the custom five-player team limit.
 * - Uses one automatic raid group for each competitor side.
 * - Supports 1v1 through the client/core raid limit of 40v40.
 * - Keeps excess queued players waiting for the next match.
 * - Party and raid invitations are blocked in GroupHandler.cpp
 *   while either player is participating in sabotage.
 */

/*
 * Custom Update - 2026-08-02
 *
 * Scourge sabotage bomb install and disarm
 *
 * - Displays the eight selected boss locations in Korean.
 * - Uses spell 900206 as the bomb installation cast.
 * - Uses spell 900207 as the bomb disarm cast.
 * - Consumes item 56942 only when installation completes.
 * - Awards the round to Team B after a successful disarm.
 */

/*
 * Custom Update - 2026-08-02
 *
 * Scourge sabotage hardcoded boss target positions
 *
 * - Removes the world database query for boss positions.
 * - Uses the 15 verified Stratholme boss coordinates directly.
 * - Randomly selects three different coordinates for every match.
 * - Fixes the Field and WorldDatabase compile errors caused by the query code.
 */

/*
 * Custom Update - 2026-08-02
 *
 * Scourge sabotage boss position query fix
 *
 * - Removes the incorrect creature_template.rank = 3 restriction.
 * - Uses Stratholme boss ScriptName values and known boss entries.
 * - Allows the boss-position query to retry after a failed load.
 */

/*
 * Custom Update - 2026-08-02
 *
 * Scourge sabotage random boss bomb targets
 *
 * - Reads Stratholme boss spawn positions from the world database.
 * - Randomly selects three different boss positions for every match.
 * - Dynamically creates bomb targets A, B, and C in the match instance.
 * - Removes the old fixed target spawns from the world database.
 * - Despawns the dynamic targets when the match is dissolved.
 */

/*
 * Custom Update - 2026-08-01
 *
 * Scourge sabotage dedicated mount scroll
 *
 * - Gives item 18063 after a successful sabotage teleport.
 * - Item 18063 casts dedicated spell 900208 through the server.
 * - Uses the mounted speed configured directly in spell 900208.
 * - Removes the mount aura and every item 18063 on sabotage exit and logout.
 * - Removes stale item 18063 on login outside sabotage.
 */

/*
 * Custom Update - 2026-07-31
 *
 * Scourge dungeon mode persistence
 *
 * - Uses the characters database.
 * - Receives LRSC addon whispers through PlayerScript::OnChat.
 * - Stores the enabled state per character.
 * - Creates a default database row on login.
 * - Restores and sends the database state on login and GET requests.
 * - Applies phase mask 2 inside Stratholme map 329 when enabled.
 * - Restores phase mask 1 after leaving the custom dungeon phase.
 * - Adds an NPC-managed sabotage queue.
 * - Supports faction war and same-faction A/B team modes.
 * - Creates one shared Stratholme instance for each completed match.
 * - Teleports A/B teams to different Stratholme entrances.
 * - Allows phase 2 only for players assigned to that shared instance.
 * - Blocks manual solo phase-2 entry while the rune is enabled.
 * - Keeps matched opponents valid inside one shared dungeon instance
 *   without placing A/B teams in the same party.
 * - Creates one normal party for Team A and one for Team B.
 * - Applies Free-For-All PvP to all match participants.
 * - Party members remain friendly; everybody else is hostile.
 * - Restores the original PvP flags after leaving.
 * - Relies on the Player.cpp map-329 FFA area override.
 * - Maintains one active Alliance-vs-Alliance session.
 * - Maintains one active Horde-vs-Horde session.
 * - Maintains one active Alliance-vs-Horde session.
 * - New applicants immediately join the matching active session.
 * - Adds one Team-A bomb supply object.
 * - Adds three clickable bomb target objects: A, B, and C.
 * - Gives one custom bomb item to one Team-A carrier per match.
 * - Despawns the bomb object immediately after pickup.
 * - Drops a new bomb object at the carrier's death position.
 * - Marks active match players for spectator and mount core rules.
 * - Keeps released players as ghosts inside the active instance.
 */

/*
 * Custom Update - 2026-08-07
 *
 * Scourge sabotage single round-bomb spawn path
 *
 * - Removes the immediate bomb spawn from StartSabotageRound.
 * - The round bomb is created only by the existing post-teleport retry path.
 * - Prevents two independent creation paths from producing duplicate
 *   bomb supply objects during a round transition.
 * - Keeps player-position X/Y and Z +0.50 bomb placement.
 */

/*
 * Custom Update - 2026-08-07
 *
 * Scourge sabotage daily execution-fragment reward
 *
 * - Changes the execution-fragment restriction from lifetime once
 *   per account to once per account per calendar day.
 * - Uses reward_date = CURRENT_DATE in the characters database.
 * - Keeps every previous sabotage reward and gameplay behavior.
 */

/*
 * Custom Update - 2026-08-07
 *
 * Scourge sabotage round bomb player-position spawn
 *
 * - Spawns the attacking-team bomb at the actual Team-A player position.
 * - Uses the player's current X/Y instead of the configured start offset.
 * - Raises only Z by 0.50 yards to avoid floor or terrain clipping.
 * - Uses the player's current orientation.
 * - Keeps round bomb spawn retry and every previous sabotage fix.
 */

/*
 * Custom Update - 2026-08-05
 *
 * Scourge sabotage round bomb spawn retry
 *
 * - Waits until an attacking player has completed the round teleport.
 * - Spawns the bomb only after the new attacker start grid is active.
 * - Retries the round bomb spawn during the active phase when the first
 *   immediate attempt runs before teleport completion.
 * - Does not respawn the bomb while it is carried or already installed.
 */

/*
 * Custom Update - 2026-08-05
 *
 * Scourge sabotage queue application notifications
 *
 * - Announces faction-war applications to every online player.
 * - Announces same-faction applications only to online players of
 *   the applicant's faction.
 * - Applies to both normal queue entry and immediate active-match joining.
 * - Keeps applicant confirmation and all previous sabotage behavior.
 */

/*
 * Custom Update - 2026-08-05
 *
 * Scourge sabotage round bomb position rotation
 *
 * - Forces the attacking Team-A start position to change every round.
 * - The round bomb supply already spawns relative to Team-A start.
 * - Therefore the bomb supply position also changes on every round switch.
 * - Keeps target avoidance, minimum start distance and every prior fix.
 */

/*
 * Custom Update - 2026-08-05
 *
 * Scourge sabotage portal mount compile correction
 *
 * - Removes an accidental preservedMountSpell argument from
 *   GetSabotageRoutePortalArrivalPosition.
 * - Adds the missing preservedMountSpell argument to the matching fmt log.
 * - Keeps defender elimination and portal mount preservation unchanged.
 */

/*
 * Custom Update - 2026-08-05
 *
 * Scourge sabotage defender elimination final correction
 *
 * - Keeps both attacker and defender elimination checks.
 * - Counts a connected dead or ghost participant even if the core moved
 *   that character outside the active Stratholme instance after death.
 * - Only a living participant still inside the active match instance
 *   prevents team elimination.
 * - Preserves the portal mount restoration and every earlier feature.
 */

/*
 * Custom Update - 2026-08-05
 *
 * Scourge sabotage portal mount preservation
 *
 * - Remembers the active sabotage mount aura before portal teleport.
 * - Restores mount spell 900208 or bomb-carrier mount spell 900211
 *   only when the player was mounted before using the portal.
 * - Leaves unmounted players unmounted after portal movement.
 */

/*
 * Custom Update - 2026-08-05
 *
 * Scourge sabotage both-team elimination handling
 *
 * - Keeps the existing attacker elimination rule.
 * - Adds defender elimination detection during active and armed phases.
 * - Ends the round immediately with A-team victory when B-team is wiped.
 * - Reuses one shared team elimination helper.
 */

/*
 * Custom Update - 2026-08-04
 *
 * Scourge sabotage paired route portals
 *
 * - Replaces the temporary physical bridge route with two clickable portals.
 * - Teleports a participant from either endpoint to the opposite endpoint.
 * - Keeps the portal pair for the entire match and removes it at match cleanup.
 * - Uses the supplied A-area coordinates and endpoint orientations.
 * - Sends both endpoints through the existing MAP:ROUTES overlay packet.
 */

/*
 * Custom Update - 2026-08-04
 *
 * Scourge sabotage route portal map overlay
 *
 * - Stores only route definitions whose bridge objects actually spawned.
 * - Sends normalized start/end points to the LegacyRune map overlay.
 * - Clears the client route overlay when no active route exists.
 * - Uses the same full-map coordinate conversion as players and bomb targets.
 */

/*
 * Custom Update - 2026-08-04
 *
 * Scourge sabotage temporary route portals
 *
 * - Loads additional route definitions from the world database.
 * - Builds each route by interpolating temporary gameobject segments
 *   between a configured start point and end point.
 * - Keeps route objects for the entire match instead of recreating them
 *   every round.
 * - Deletes every route object when the match ends or is dissolved.
 * - Does not block the match when a route is missing or fails to spawn.
 */

/*
 * Custom Update - 2026-08-04
 *
 * Scourge sabotage match rewards
 *
 * - Winner: invasion fragments 56940 x3 and death runes 22484 x3.
 * - Loser: invasion fragment 56940 x1 and death rune 22484 x1.
 * - A winning account receives one random execution fragment
 *   from 56924~56928 once per calendar day.
 * - Rewards are granted only to connected players still registered
 *   in the completed match.
 * - The daily execution-fragment claim is persisted by account ID
 *   and reward date.
 */

/*
 * Custom Update - 2026-08-04
 *
 * Scourge sabotage randomized round start positions
 *
 * - Expands the start pool from two entrances to six verified positions.
 * - Selects two different positions for the attacking and defending teams.
 * - Keeps the first selected pair for the initial world transfer.
 * - Selects a new pair from round two onward.
 * - Keeps the two teams at least 100 yards apart when possible.
 * - Excludes active bomb-target positions from later start selection.
 * - Excludes the first selected starts from the initial bomb-target draw.
 * - Spawns the bomb and defender scout relative to the selected starts.
 */

/*
 * Custom Update - 2026-08-04
 *
 * Scourge sabotage Stratholme full-map overlay packets
 *
 * - Sends normalized full-map coordinates for the current player.
 * - Shows the carried or dropped bomb only to the current attacking team.
 * - Shows the bomb to both teams only after the bomb is installed.
 * - Sends the three selected bomb-target positions.
 * - Sends connected allies from the same competitor side.
 * - Uses the current Stratholme.tga calibration without changing the Minimap.
 */

/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage random target HUD and minimap support
 *
 * - Sends the three boss locations selected for the current match.
 * - Sends each target's installation state.
 * - Sends the player's current world position for minimap marker placement.
 * - Restores the target markers after UI reload and world entry.
 * - Clears all target markers when the sabotage HUD is hidden.
 */

/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage raid entry stabilization
 *
 * - Prevents the sabotage raid from being removed during the first
 *   Stratholme world-transfer timing window.
 * - Registers a newly created group before converting it to a raid.
 * - Resolves active sabotage raids directly through GroupMgr.
 * - Resends the raid update after an authorized instance entry.
 */

/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage first-entry state preservation
 *
 * - Keeps the assigned team raid while the player is transferring
 *   from the normal world into the shared Stratholme instance.
 * - Prevents RestoreSabotageCombatState from running during that transfer.
 * - Prevents the false SABOTAGE:0 packet that hid the first-entry HUD.
 * - Keeps same-team players friendly through the assigned raid.
 */

/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage manual party leave and invitation rules
 *
 * - Detects a manual party or raid leave during an active sabotage match.
 * - Hides the sabotage HUD immediately.
 * - Removes the player from the active match and every sabotage queue.
 * - Restores the normal combat state and phase.
 * - Teleports the player to the homebind location.
 * - Defers the full exit by one world update to avoid group-removal reentry.
 */

/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage manual leave full reset
 *
 * - Performs a complete character cleanup before leaving the battlefield.
 * - Repeats the cleanup after the homebind world transfer completes.
 * - Removes the actual mounted aura, not only custom spell 900208.
 * - Sends a terminal addon EXIT state after the destination map loads.
 */

/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage authoritative first-entry HUD state
 *
 * - Every active HUD transmission first sends SABOTAGE:1.
 * - Covers world entry, zone update, periodic HUD refresh and addon GET.
 * - Prevents the client EXIT lock from surviving into a new match.
 */

/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage active-join raid repair
 *
 * - Rebuilds a missing team raid with every connected member of that team.
 * - Adds an active-match joiner to the existing team raid and verifies membership.
 * - Rebinds the repaired raid to the shared Stratholme instance.
 * - Cancels the homebind timer for every repaired team member.
 * - Keeps the existing first-entry HUD and manual-leave behavior unchanged.
 */

/*
 * Custom Update - 2026-08-03
 *
 * Scourge Rune addon queue management
 *
 * - Keeps npc_scourge_sabotage_queue_manager unchanged and usable.
 * - Adds LRSC addon commands for the same queue actions exposed by the NPC.
 * - Sends a compact queue-menu state to the LegacyRune addon.
 * - Allows the addon to join, cancel, inspect and explain the queue.
 * - Disabling the rune automatically cancels a non-launched queue state.
 */

/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage team raid self-repair
 *
 * - Does not depend on Group.cpp preserving a one-member raid.
 * - Recreates a missing team raid after TrinityCore disbands a two-member raid.
 * - Rebinds the recreated raid to the active shared Stratholme instance.
 * - Cancels the remaining member's instance homebind timer immediately.
 * - Merges active-match members that were placed in separate team raids.
 * - Keeps the NPC queue manager and addon menu behavior unchanged.
 */

/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage universal exit cleanup
 *
 * - Distinguishes the authorized entry world transfer from every other teleport.
 * - Hearthstones, portals, summons and all external map transfers leave the match.
 * - Leaving the shared map or shared instance removes the player from the match.
 * - Removes the sabotage raid, bomb, flags, mount, FFA state and phase.
 * - Hides the HUD and minimap and cancels the instance homebind timer.
 * - Adds a periodic fallback in case a map-change callback is missed.
 * - Keeps the NPC queue manager and Lua files unchanged.
 */

/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage queue return position
 *
 * - Stores the application position directly in SabotagePlayerQueueState.
 * - Preserves the position while waiting, matching, requeueing, and active joining.
 * - Returns the player after normal match completion.
 * - Returns the player after manual party leave.
 * - Returns the player after hearthstone, portal, summon, or any external transfer.
 * - Cancelling before battlefield entry only removes the queue state.
 * - Uses homebind only when no queue return position exists or teleport fails.
 * - Keeps NPC and addon Lua behavior unchanged.
 */

/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage hearthstone exit exception
 *
 * - Detects the normal Hearthstone spell before the world transfer.
 * - Cleans every sabotage state after the Hearthstone teleport.
 * - Keeps the character at the Hearthstone homebind destination.
 * - Does not return a Hearthstone user to the saved queue position.
 * - Normal match completion and manual party leave still return to the
 *   position stored when the player joined the sabotage queue.
 * - Other external transfers keep the existing queue-return behavior.
 */


/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage spectator living-player visibility
 *
 * - Keeps the released participant as a ghost at the death position.
 * - Allows the sabotage ghost spectator to detect both living players
 *   and other ghosts inside the current phase and instance.
 * - Refreshes the spectator's client visibility immediately.
 * - Restores the normal ghost-only detection state when leaving sabotage.
 */


/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage round-end and match-end revive guarantee
 *
 * - Revives dead and ghost participants at their exact round-end positions.
 * - Restores normal living-player visibility before the next round.
 * - Keeps every revived participant frozen at that position during the break.
 * - Revives any remaining dead participant again before final match return.
 * - Restores full health and normal visibility before teleporting the player
 *   to the position stored when the sabotage queue was joined.
 */


/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage delayed-death round revive fix
 *
 * - Handles the TrinityCore death-update race at round end.
 * - Revives both corpse and released-ghost states at the same position.
 * - Rechecks dead participants throughout BREAK and FINISHED phases.
 * - Reapplies the round-end revive if a delayed KillPlayer or repop
 *   callback changes the player back to a dead state after the first revive.
 * - Keeps the existing next-round and final queue-return behavior unchanged.
 */


/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage GM-command style resurrection
 *
 * - Uses the same online resurrection sequence as the GM revive command.
 * - Clears any pending resurrection request before forcing the revive.
 * - Calls ResurrectPlayer, SpawnCorpseBones, and SaveToDB after success.
 * - Reuses one helper for round end, next-round preparation, and match exit.
 * - Logs the result so failed resurrection attempts are visible.
 */


/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage round-break resurrection bypass
 *
 * - Keeps the normal sabotage resurrection ban during an active round.
 * - Temporarily releases only the core Player resurrection lock while the
 *   server performs the round-end, next-round, or match-return revive.
 * - Keeps the script-local participant state active during the bypass.
 * - Restores the core participant flag immediately after the revive call.
 */


/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage spectator, defender scout, and bomb-carrier features
 *
 * - Gives released ghost spectators three-times movement speed.
 * - Restores the player's original run and swim rates on revive or exit.
 * - Spawns defender scout object 961204 at the current B-team start.
 * - The active defender can toggle hidden scouting mode only through
 *   that object; scouting mode uses two-times movement speed.
 * - Clears scouting mode on death, round end, match exit, or logout.
 * - Forces bomb carriers to use skeletal horse spell 900211.
 * - Removes spell 900211 when the bomb is installed, dropped, returned,
 *   the round ends, or the player leaves the match.
 */


/*
 * Custom Update - 2026-08-03
 *
 * Scourge sabotage mount-item selection and scout restriction
 *
 * - The mount scroll selects spell 900208 or 900211 only when the item is used.
 * - Bomb pickup and periodic carrier refresh no longer force a mount cast.
 * - Uses spell 37800 so the scout can see their own translucent state.
 * - Blocks melee attacks and spell casts while defender scouting is active.
 * - Keeps movement, chat, and the defender scout object available.
 * - Restores the original pacified and silenced flags when scouting ends.
 */




/*
 * Custom Update - 2026-08-04
 *
 * Scourge sabotage smoke object 961205
 *
 * - Uses the smoke-grenade user's current position immediately.
 * - Spawns only GameObject 961205 at that position.
 * - Clears the selected target of every player within thirty yards once,
 *   at the exact moment GameObject 961205 is created.
 * - Sends SMSG_CLEAR_TARGET with the previously selected target GUID so
 *   the WoW client actually removes its visible character target.
 * - Removes GameObject 961205 automatically after thirteen seconds.
 * - Adds no repeated target clearing, aura, concealment, silence, speed,
 *   targeting restriction, or combat modification.
 */

#include "custom_scourge_dungeon_mode.h"

#include "Chat.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "GroupMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceSaveMgr.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Spell.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
    std::string const SCOURGE_MODE_ADDON_PREFIX = "LRSC";
    std::string const SCOURGE_MODE_PAYLOAD_PREFIX = "SCOURGE_MODE:";
    std::string const SCOURGE_MODE_GET_COMMAND = "SCOURGE_MODE:GET";
    std::string const SCOURGE_MODE_SET_PREFIX = "SCOURGE_MODE:SET:";
    std::string const SCOURGE_MODE_MENU_GET_COMMAND =
        "SCOURGE_MODE:MENU:GET";
    std::string const SCOURGE_MODE_QUEUE_JOIN_FACTION_COMMAND =
        "SCOURGE_MODE:QUEUE:JOIN:FACTION";
    std::string const SCOURGE_MODE_QUEUE_JOIN_SAME_COMMAND =
        "SCOURGE_MODE:QUEUE:JOIN:SAME";
    std::string const SCOURGE_MODE_QUEUE_CANCEL_COMMAND =
        "SCOURGE_MODE:QUEUE:CANCEL";
    std::string const SCOURGE_MODE_QUEUE_STATUS_COMMAND =
        "SCOURGE_MODE:QUEUE:STATUS";
    std::string const SCOURGE_MODE_QUEUE_RULES_COMMAND =
        "SCOURGE_MODE:QUEUE:RULES";
    std::string const SCOURGE_MODE_WSG_JOIN = "SCOURGE_MODE:WSG:JOIN";
    std::string const SCOURGE_MODE_WSG_CANCEL = "SCOURGE_MODE:WSG:CANCEL";


    char const* SCOURGE_MODE_TABLE =
        "`custom_scourge_dungeon_mode`";

    static constexpr uint32 MAP_STRATHOLME = 329;
    static constexpr uint32 NORMAL_PHASE_MASK = 1;
    static constexpr uint32 SCOURGE_PHASE_MASK = 2;

    static constexpr uint32
        SABOTAGE_ENTRY_TRANSFER_GRACE_MS = 15000;

    static constexpr uint32
        SABOTAGE_HEARTHSTONE_SPELL = 8690;

    static constexpr uint32
        SABOTAGE_HEARTHSTONE_EXIT_GRACE_MS = 20000;

    static constexpr uint32 SABOTAGE_MOUNT_ITEM = 18063;
    static constexpr uint32 SABOTAGE_MOUNT_SPELL = 900208;
    static constexpr uint32
        SABOTAGE_BOMB_CARRIER_MOUNT_SPELL = 900211;

    static constexpr float
        SABOTAGE_SPECTATOR_SPEED_RATE = 3.0f;

    static constexpr float
        SABOTAGE_DEFENDER_SCOUT_SPEED_RATE = 2.0f;

    static constexpr uint32
        SABOTAGE_DEFENDER_SCOUT_VISUAL_SPELL = 37800;

    static constexpr uint32
        SABOTAGE_SMOKE_GRENADE_ITEM = 56943;

    static constexpr uint32
        SABOTAGE_SMOKE_GAMEOBJECT = 961205;

    static constexpr uint32
        SABOTAGE_SMOKE_GAMEOBJECT_DURATION_MS =
            13 * 1000;

    static constexpr float
        SABOTAGE_SMOKE_TARGET_CLEAR_RADIUS = 30.0f;

    // Custom clones of the Warsong Gulch flag carrier auras.
    // 900209 = clone of 23333 with AURA_INTERRUPT_FLAG_MOUNT removed.
    // 900210 = clone of 23335 with AURA_INTERRUPT_FLAG_MOUNT removed.
    static constexpr uint32 SABOTAGE_WARSONG_FLAG_SPELL = 900209;
    static constexpr uint32 SABOTAGE_SILVERWING_FLAG_SPELL = 900210;

    std::unordered_map<uint32, bool> ScourgeModeStates;
    std::unordered_set<uint32> ScourgeModePhasePlayers;
    std::unordered_set<uint32> ScourgeSabotageParticipantGuids;
    std::unordered_set<uint32> SabotagePendingPartyLeaveGuids;
    std::unordered_set<uint32> SabotagePendingPostTeleportResetGuids;
    std::unordered_map<uint32, uint32>
        SabotagePendingEntryTransferMs;

    std::unordered_map<uint32, uint32>
        SabotagePendingHearthstoneExitMs;

    struct SabotageMovementRestoreState
    {
        float runRate;
        float swimRate;

        SabotageMovementRestoreState()
            : runRate(1.0f),
              swimRate(1.0f)
        {
        }
    };

    std::unordered_map<uint32, SabotageMovementRestoreState>
        SabotageMovementRestoreStates;

    std::unordered_set<uint32>
        SabotageDefenderScoutGuids;

    struct SabotageDefenderScoutRestoreState
    {
        bool hadPacifiedFlag;
        bool hadSilencedFlag;

        SabotageDefenderScoutRestoreState()
            : hadPacifiedFlag(false),
              hadSilencedFlag(false)
        {
        }
    };

    std::unordered_map<
        uint32,
        SabotageDefenderScoutRestoreState
    > SabotageDefenderScoutRestoreStates;

    struct SabotageCombatRestoreState
    {
        uint8 originalPvpFlags;
        ObjectGuid sabotageGroupGuid;

        SabotageCombatRestoreState()
            : originalPvpFlags(0),
              sabotageGroupGuid(ObjectGuid::Empty)
        {
        }
    };

    std::unordered_map<uint32, SabotageCombatRestoreState>
        SabotageCombatRestoreStates;

    /*
     * Forward declarations.
     *
     * ApplyScourgeDungeonPhase is defined before the sabotage match
     * structures and HUD helpers. Only pointers and references are used
     * there, so complete structure definitions are not required yet.
     */
    struct SabotagePlayerQueueState;
    struct SabotagePreparedMatch;

    bool GetSabotagePlayerMatchContext(
        Player* player,
        SabotagePreparedMatch*& match,
        SabotagePlayerQueueState*& state
    );

    void SendSabotageHudStateToPlayer(
        Player* player,
        SabotagePreparedMatch const& match
    );

    bool GetSabotageMatchInstanceId(
        Player const* player,
        uint32& instanceId
    );

    void SendSabotageMessage(
        Player* player,
        std::string const& text
    );

    bool RefreshSabotageFreeForAll(
        Player* player
    );

    char const* GetSabotageTargetName(
        uint32 gameObjectEntry
    );

    void ClearSabotagePendingBombAction(
        ObjectGuid guid
    );

    void TryCreateSabotageMatches();

    bool ExitSabotagePlayerAfterExternalTransfer(
        Player* player,
        char const* reason
    );

    void ProcessSabotageExternalTransfers(
        uint32 diff
    );

    Player* FindSabotageRoundAnchor(
        SabotagePreparedMatch const& match
    );

    std::vector<ObjectGuid> GetSabotageAllPlayers(
        SabotagePreparedMatch const& match
    );

    bool DropSabotageBombAtCarrierPosition(
        Player* carrier,
        bool manualDrop = false
    );

    uint32 GetCharacterGuid(Player const* player)
    {
        if (!player)
            return 0;

        return uint32(player->GetGUID().GetCounter());
    }

    bool IsScourgeSabotageParticipant(
        Player const* player
    )
    {
        if (!player)
            return false;

        /*
         * Custom Update - 2026-08-03
         *
         * MiscHandler.cpp uses Player::IsScourgeSabotageParticipant()
         * to keep released spirits at the death position.
         *
         * The universal exit patch introduced a script-local GUID cache.
         * Keep both states synchronized and treat the Player flag as
         * authoritative for core-side spirit-release handling.
         */
        if (player->IsScourgeSabotageParticipant())
            return true;

        uint32 guid = GetCharacterGuid(player);

        return guid &&
            ScourgeSabotageParticipantGuids.find(guid) !=
                ScourgeSabotageParticipantGuids.end();
    }

    void SetScourgeSabotageParticipant(
        Player* player,
        bool active
    )
    {
        if (!player)
            return;

        /*
         * Keep the Player flag synchronized with the script-local cache.
         * Without this call, HandleRepopRequest sees false and executes
         * RepopAtGraveyard even though the player is in an active match.
         */
        player->SetScourgeSabotageParticipant(
            active
        );

        uint32 guid = GetCharacterGuid(player);

        if (!guid)
            return;

        if (active)
        {
            ScourgeSabotageParticipantGuids.insert(guid);
        }
        else
        {
            ScourgeSabotageParticipantGuids.erase(guid);
        }
    }

    bool IsSabotageDefenderScoutActive(
        Player const* player
    )
    {
        uint32 guidLow =
            GetCharacterGuid(player);

        return guidLow &&
            SabotageDefenderScoutGuids.find(
                guidLow
            ) !=
                SabotageDefenderScoutGuids.end();
    }

    void ApplySabotageTemporaryMovementSpeed(
        Player* player,
        float rate
    )
    {
        if (!player || rate <= 0.0f)
            return;

        uint32 guidLow =
            GetCharacterGuid(player);

        if (!guidLow)
            return;

        auto stateItr =
            SabotageMovementRestoreStates.find(
                guidLow
            );

        if (stateItr ==
            SabotageMovementRestoreStates.end())
        {
            SabotageMovementRestoreState state;
            state.runRate =
                player->GetSpeedRate(
                    MOVE_RUN
                );
            state.swimRate =
                player->GetSpeedRate(
                    MOVE_SWIM
                );

            SabotageMovementRestoreStates[
                guidLow
            ] = state;
        }

        player->SetSpeedRate(
            MOVE_RUN,
            rate
        );

        player->SetSpeedRate(
            MOVE_SWIM,
            rate
        );
    }

    void RestoreSabotageTemporaryMovementSpeed(
        Player* player
    )
    {
        if (!player)
            return;

        uint32 guidLow =
            GetCharacterGuid(player);

        if (!guidLow)
            return;

        auto stateItr =
            SabotageMovementRestoreStates.find(
                guidLow
            );

        if (stateItr ==
            SabotageMovementRestoreStates.end())
        {
            return;
        }

        player->SetSpeedRate(
            MOVE_RUN,
            stateItr->second.runRate
        );

        player->SetSpeedRate(
            MOVE_SWIM,
            stateItr->second.swimRate
        );

        SabotageMovementRestoreStates.erase(
            stateItr
        );
    }

    void DisableSabotageDefenderScout(
        Player* player,
        bool notify
    )
    {
        if (!player)
            return;

        uint32 guidLow =
            GetCharacterGuid(player);

        if (!guidLow)
            return;

        bool wasActive =
            SabotageDefenderScoutGuids.erase(
                guidLow
            ) != 0;

        auto restoreItr =
            SabotageDefenderScoutRestoreStates.find(
                guidLow
            );

        bool hadRestoreState =
            restoreItr !=
                SabotageDefenderScoutRestoreStates.end();

        bool hadVisualAura =
            player->HasAura(
                SABOTAGE_DEFENDER_SCOUT_VISUAL_SPELL
            );

        if (!wasActive &&
            !hadRestoreState &&
            !hadVisualAura)
        {
            return;
        }

        player->RemoveAurasDueToSpell(
            SABOTAGE_DEFENDER_SCOUT_VISUAL_SPELL
        );

        player->m_serverSideVisibility.SetValue(
            SERVERSIDE_VISIBILITY_GM,
            SEC_PLAYER
        );

        if (hadRestoreState)
        {
            if (!restoreItr->second.hadPacifiedFlag)
            {
                player->RemoveUnitFlag(
                    UNIT_FLAG_PACIFIED
                );
            }

            if (!restoreItr->second.hadSilencedFlag)
            {
                player->RemoveUnitFlag(
                    UNIT_FLAG_SILENCED
                );
            }

            SabotageDefenderScoutRestoreStates.erase(
                restoreItr
            );
        }
        else
        {
            player->RemoveUnitFlag(
                UNIT_FLAG_PACIFIED
            );

            player->RemoveUnitFlag(
                UNIT_FLAG_SILENCED
            );
        }

        RestoreSabotageTemporaryMovementSpeed(
            player
        );

        player->UpdateObjectVisibility(
            true
        );

        if (notify)
        {
            SendSabotageMessage(
                player,
                "정찰 상태를 해제했습니다. "
                "투명 효과와 행동 제한이 해제되고 "
                "다른 참가자에게 다시 보입니다."
            );
        }

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage defender scout disabled: player={}, guid={}, map={}, instance={}, active={}, visual={}, restoreState={}",
            player->GetName(),
            guidLow,
            player->GetMapId(),
            player->GetInstanceId(),
            wasActive ? 1 : 0,
            hadVisualAura ? 1 : 0,
            hadRestoreState ? 1 : 0
        );
    }

    bool EnableSabotageDefenderScout(
        Player* player
    )
    {
        if (!player || !player->IsAlive())
            return false;

        uint32 guidLow =
            GetCharacterGuid(player);

        if (!guidLow)
            return false;

        /*
         * Apply the self-visible translucent effect before registering the
         * scout state. OnSpellCast blocks every later player spell while
         * the state is active.
         */
        SpellCastResult visualResult =
            player->CastSpell(
                player,
                SABOTAGE_DEFENDER_SCOUT_VISUAL_SPELL,
                true
            );

        if (visualResult != SPELL_CAST_OK)
        {
            SendSabotageMessage(
                player,
                "정찰 투명 효과를 적용하지 못했습니다."
            );

            TC_LOG_ERROR(
                "custom.scourge",
                "Failed to apply sabotage defender scout visual: player={}, guid={}, spell={}, result={}",
                player->GetName(),
                guidLow,
                SABOTAGE_DEFENDER_SCOUT_VISUAL_SPELL,
                uint32(visualResult)
            );

            return false;
        }

        SabotageDefenderScoutRestoreState restoreState;
        restoreState.hadPacifiedFlag =
            player->HasUnitFlag(
                UNIT_FLAG_PACIFIED
            );
        restoreState.hadSilencedFlag =
            player->HasUnitFlag(
                UNIT_FLAG_SILENCED
            );

        SabotageDefenderScoutRestoreStates[
            guidLow
        ] = restoreState;

        SabotageDefenderScoutGuids.insert(
            guidLow
        );

        player->CombatStopWithPets();
        player->AttackStop();
        player->InterruptNonMeleeSpells(
            true
        );
        player->SetSelection(
            ObjectGuid::Empty
        );

        player->SetUnitFlag(
            UNIT_FLAG_PACIFIED
        );

        player->SetUnitFlag(
            UNIT_FLAG_SILENCED
        );

        ApplySabotageTemporaryMovementSpeed(
            player,
            SABOTAGE_DEFENDER_SCOUT_SPEED_RATE
        );

        /*
         * Other normal players cannot detect the scout.
         * Spell 37800 remains visible to the scout themselves so the
         * translucent state is always recognizable.
         */
        player->m_serverSideVisibility.SetValue(
            SERVERSIDE_VISIBILITY_GM,
            SEC_ADMINISTRATOR
        );

        player->UpdateObjectVisibility(
            true
        );

        SendSabotageMessage(
            player,
            "정찰 상태가 적용되었습니다. "
            "캐릭터가 반투명하게 표시되고 다른 참가자에게는 보이지 않습니다. "
            "이동속도는 2배이며 이동과 채팅만 가능합니다. "
            "정찰 장치를 다시 클릭해야 정상 상태로 돌아옵니다."
        );

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage defender scout enabled: player={}, guid={}, map={}, instance={}, visualSpell={}, runRate={}, pacified={}, silenced={}",
            player->GetName(),
            guidLow,
            player->GetMapId(),
            player->GetInstanceId(),
            SABOTAGE_DEFENDER_SCOUT_VISUAL_SPELL,
            player->GetSpeedRate(
                MOVE_RUN
            ),
            player->HasUnitFlag(
                UNIT_FLAG_PACIFIED
            ) ? 1 : 0,
            player->HasUnitFlag(
                UNIT_FLAG_SILENCED
            ) ? 1 : 0
        );

        return true;
    }

    void EnsureScourgeModeRow(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        uint32 guid = GetCharacterGuid(player);
        uint32 accountId = player->GetSession()->GetAccountId();

        if (!guid || !accountId)
            return;

        CharacterDatabase.PExecute(
            "INSERT IGNORE INTO {} "
            "(`guid`, `account_id`, `enabled`) "
            "VALUES ({}, {}, 0)",
            SCOURGE_MODE_TABLE,
            guid,
            accountId
        );
    }

    bool LoadScourgeModeState(uint32 guid)
    {
        if (!guid)
            return false;

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT `enabled` "
            "FROM {} "
            "WHERE `guid` = {} "
            "LIMIT 1",
            SCOURGE_MODE_TABLE,
            guid
        );

        if (!result)
            return false;

        return result->Fetch()[0].GetUInt8() != 0;
    }

    void SaveScourgeModeState(Player* player, bool enabled)
    {
        if (!player || !player->GetSession())
            return;

        uint32 guid = GetCharacterGuid(player);
        uint32 accountId = player->GetSession()->GetAccountId();

        if (!guid || !accountId)
            return;

        CharacterDatabase.PExecute(
            "INSERT INTO {} "
            "(`guid`, `account_id`, `enabled`) "
            "VALUES ({}, {}, {}) "
            "ON DUPLICATE KEY UPDATE "
            "`account_id` = VALUES(`account_id`), "
            "`enabled` = VALUES(`enabled`), "
            "`updated_at` = CURRENT_TIMESTAMP",
            SCOURGE_MODE_TABLE,
            guid,
            accountId,
            enabled ? 1 : 0
        );

        TC_LOG_INFO(
            "custom.scourge",
            "Scourge dungeon mode saved: player={}, guid={}, enabled={}",
            player->GetName(),
            guid,
            enabled ? 1 : 0
        );
    }

    void CacheScourgeModeState(Player const* player, bool enabled)
    {
        uint32 guid = GetCharacterGuid(player);
        if (!guid)
            return;

        ScourgeModeStates[guid] = enabled;
    }

    void RemoveCachedScourgeModeState(Player const* player)
    {
        uint32 guid = GetCharacterGuid(player);
        if (!guid)
            return;

        ScourgeModeStates.erase(guid);
        ScourgeModePhasePlayers.erase(guid);
    }

    void RemoveSabotageMountSupport(
        Player* player
    )
    {
        if (!player)
            return;

        player->RemoveAurasDueToSpell(
            SABOTAGE_MOUNT_SPELL
        );

        player->RemoveAurasDueToSpell(
            SABOTAGE_BOMB_CARRIER_MOUNT_SPELL
        );

        uint32 itemCount =
            player->GetItemCount(
                SABOTAGE_MOUNT_ITEM,
                true
            );

        if (itemCount)
        {
            player->DestroyItemCount(
                SABOTAGE_MOUNT_ITEM,
                itemCount,
                true
            );
        }
    }

    void RemoveSabotageBombCarrierFlag(
        Player* player
    )
    {
        if (!player)
            return;

        player->RemoveAurasDueToSpell(
            SABOTAGE_WARSONG_FLAG_SPELL
        );

        player->RemoveAurasDueToSpell(
            SABOTAGE_SILVERWING_FLAG_SPELL
        );

        bool hadCarrierMount =
            player->HasAura(
                SABOTAGE_BOMB_CARRIER_MOUNT_SPELL
            );

        player->RemoveAurasDueToSpell(
            SABOTAGE_BOMB_CARRIER_MOUNT_SPELL
        );

        if (hadCarrierMount)
        {
            player->Dismount();
        }
    }

    bool ApplySabotageBombCarrierFlag(
        Player* player
    )
    {
        if (!player)
            return false;

        RemoveSabotageBombCarrierFlag(
            player
        );

        uint32 flagSpell = 0;

        /*
         * Alliance carries the Horde Warsong flag.
         * Horde carries the Alliance Silverwing flag.
         */
        if (player->GetTeam() == ALLIANCE)
        {
            flagSpell =
                SABOTAGE_WARSONG_FLAG_SPELL;
        }
        else if (player->GetTeam() == HORDE)
        {
            flagSpell =
                SABOTAGE_SILVERWING_FLAG_SPELL;
        }
        else
        {
            return false;
        }

        SpellCastResult castResult =
            player->CastSpell(
                player,
                flagSpell,
                true
            );

        if (castResult != SPELL_CAST_OK)
        {
            TC_LOG_ERROR(
                "custom.scourge",
                "Failed to apply sabotage bomb carrier flag: player={}, guid={}, spell={}, result={}",
                player->GetName(),
                player->GetGUID().GetCounter(),
                flagSpell,
                uint32(castResult)
            );

            SendSabotageMessage(
                player,
                "폭탄 운반 깃발 효과를 적용하지 못했습니다."
            );

            return false;
        }

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage bomb carrier flag applied: player={}, guid={}, spell={}",
            player->GetName(),
            player->GetGUID().GetCounter(),
            flagSpell
        );

        return true;
    }

    bool GiveSabotageMountItem(
        Player* player
    )
    {
        if (!player)
            return false;

        if (player->HasItemCount(
            SABOTAGE_MOUNT_ITEM,
            1,
            true
        ))
        {
            return true;
        }

        if (!player->AddItem(
            SABOTAGE_MOUNT_ITEM,
            1
        ))
        {
            SendSabotageMessage(
                player,
                "가방 공간이 부족하여 폭파전 전용 탈것 두루마리를 지급하지 못했습니다."
            );

            return false;
        }

        SendSabotageMessage(
            player,
            "폭파전 전용 탈것 두루마리가 지급되었습니다."
        );

        return true;
    }

    bool RestoreSabotageCombatState(
        Player* player
    )
    {
        if (!player)
            return false;

        ClearSabotagePendingBombAction(
            player->GetGUID()
        );

        /*
         * Custom Update - 2026-08-02
         *
         * Clear a round-end position lock whenever the player leaves the
         * sabotage match or the normal character state is restored.
         */
        player->SetControlled(
            false,
            UNIT_STATE_ROOT
        );

        RemoveSabotageBombCarrierFlag(
            player
        );

        RemoveSabotageMountSupport(
            player
        );

        SetScourgeSabotageParticipant(
            player,
            false
        );

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SABOTAGE:0",
            player
        );

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SPECTATOR:0",
            player
        );

        uint32 guid =
            GetCharacterGuid(player);

        if (!guid)
            return false;

        auto itr =
            SabotageCombatRestoreStates.find(guid);

        if (itr ==
            SabotageCombatRestoreStates.end())
        {
            return false;
        }

        SabotageCombatRestoreState state =
            itr->second;

        player->ReplaceAllPvpFlags(
            UnitPVPStateFlags(
                state.originalPvpFlags
            )
        );

        for (Unit* controlled :
             player->m_Controlled)
        {
            if (!controlled)
                continue;

            controlled->RestoreFaction();

            controlled->ReplaceAllPvpFlags(
                player->GetPvpFlags()
            );
        }

        Group* group = player->GetGroup();

        if (group &&
            !state.sabotageGroupGuid.IsEmpty() &&
            group->GetGUID() ==
                state.sabotageGroupGuid)
        {
            player->RemoveFromGroup();
        }

        player->UpdateObjectVisibility(true);

        SabotageCombatRestoreStates.erase(
            itr
        );

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage FFA state restored: player={}, guid={}, pvpFlags={}",
            player->GetName(),
            guid,
            uint32(state.originalPvpFlags)
        );

        return true;
    }

    bool RestoreSabotageInstanceValidity(
        Player* player
    )
    {
        if (!player ||
            player->GetMapId() != MAP_STRATHOLME)
        {
            return false;
        }

        uint32 expectedInstanceId = 0;

        bool authorized =
            GetSabotageMatchInstanceId(
                player,
                expectedInstanceId
            );

        if (!authorized ||
            !expectedInstanceId ||
            player->GetInstanceId() !=
                expectedInstanceId)
        {
            return false;
        }

        /*
         * TrinityCore normally considers an ungrouped player invalid
         * when more than one non-GM player is inside a dungeon.
         *
         * A/B teams use two separate parties and share one instance.
         * Keep the authorized sabotage exception as an additional
         * safeguard against the normal homebind removal.
         */
        player->m_InstanceValid = true;
        player->UpdateHomebindTime(0);

        TC_LOG_DEBUG(
            "custom.scourge",
            "Sabotage instance validity restored: player={}, instance={}",
            player->GetName(),
            expectedInstanceId
        );

        return true;
    }

    void ApplyScourgeDungeonPhase(Player* player)
    {
        if (!player)
            return;

        uint32 guid = GetCharacterGuid(player);
        if (!guid)
            return;

        if (player->GetMapId() == MAP_STRATHOLME)
        {
            uint32 expectedInstanceId = 0;

            bool authorized =
                GetSabotageMatchInstanceId(
                    player,
                    expectedInstanceId
                );

            if (authorized &&
                expectedInstanceId != 0 &&
                player->GetInstanceId() ==
                    expectedInstanceId)
            {
                SabotagePendingEntryTransferMs.erase(
                    guid
                );

                player->SetPhaseMask(
                    SCOURGE_PHASE_MASK,
                    true
                );

                ScourgeModePhasePlayers.insert(guid);

                RestoreSabotageInstanceValidity(
                    player
                );

                RefreshSabotageFreeForAll(
                    player
                );

                if (Group* raid = player->GetGroup())
                {
                    if (!raid->isRaidGroup())
                    {
                        raid->ConvertToRaid();
                    }

                    raid->SendUpdate();

                    TC_LOG_INFO(
                        "custom.scourge",
                        "Sabotage raid refreshed after entry: player={}, raid={}, members={}, isRaid={}",
                        player->GetName(),
                        raid->GetGUID().GetCounter(),
                        raid->GetMembersCount(),
                        raid->isRaidGroup() ? 1 : 0
                    );
                }
                else
                {
                    TC_LOG_ERROR(
                        "custom.scourge",
                        "Sabotage player entered without team raid: player={}, guid={}, instance={}",
                        player->GetName(),
                        guid,
                        expectedInstanceId
                    );
                }

                SetScourgeSabotageParticipant(
                    player,
                    true
                );

                player->WhisperAddon(
                    SCOURGE_MODE_ADDON_PREFIX +
                        "\tSCOURGE_MODE:SABOTAGE:1",
                    player
                );

                SabotagePreparedMatch* activeMatch = nullptr;
                SabotagePlayerQueueState* activeState = nullptr;

                if (GetSabotagePlayerMatchContext(
                        player,
                        activeMatch,
                        activeState
                    ) &&
                    activeMatch)
                {
                    SendSabotageHudStateToPlayer(
                        player,
                        *activeMatch
                    );
                }

                TC_LOG_INFO(
                    "custom.scourge",
                    "Sabotage Stratholme entry authorized: player={}, guid={}, instance={}, phase={}",
                    player->GetName(),
                    guid,
                    expectedInstanceId,
                    SCOURGE_PHASE_MASK
                );

                return;
            }

            bool enabled =
                CustomScourgeDungeonMode::IsEnabled(
                    player
                );

            /*
             * Only TeleportSabotagePlayer may create this entry-transfer
             * marker. A different map or instance without this marker is
             * an actual battlefield exit, not an entry timing window.
             */
            if (authorized &&
                expectedInstanceId != 0)
            {
                auto transferItr =
                    SabotagePendingEntryTransferMs.find(
                        guid
                    );

                if (transferItr !=
                    SabotagePendingEntryTransferMs.end())
                {
                    TC_LOG_DEBUG(
                        "custom.scourge",
                        "Sabotage entry transfer pending: player={}, guid={}, actualInstance={}, expectedInstance={}, remainingMs={}",
                        player->GetName(),
                        guid,
                        player->GetInstanceId(),
                        expectedInstanceId,
                        transferItr->second
                    );

                    return;
                }

                ExitSabotagePlayerAfterExternalTransfer(
                    player,
                    "폭파전 전용 인스턴스를 벗어나 경기에서 이탈했습니다."
                );

                return;
            }

            if (enabled)
            {
                RestoreSabotageCombatState(
                    player
                );

                player->SetPhaseMask(
                    NORMAL_PHASE_MASK,
                    true
                );

                ScourgeModePhasePlayers.erase(guid);

                SendSabotageMessage(
                    player,
                    "폭파전 팀이 구성된 경우에만 "
                    "스트라솔름 페이즈 2에 입장할 수 있습니다. "
                    "귀환지로 이동합니다."
                );

                TC_LOG_WARN(
                    "custom.scourge",
                    "Unauthorized phase-2 Stratholme entry blocked: player={}, guid={}, actualInstance={}, expectedInstance={}",
                    player->GetName(),
                    guid,
                    player->GetInstanceId(),
                    expectedInstanceId
                );

                player->TeleportTo(
                    player->m_homebindMapId,
                    player->m_homebindX,
                    player->m_homebindY,
                    player->m_homebindZ,
                    player->GetOrientation()
                );

                return;
            }

            RestoreSabotageCombatState(
                player
            );

            player->SetPhaseMask(
                NORMAL_PHASE_MASK,
                true
            );

            ScourgeModePhasePlayers.erase(guid);

            TC_LOG_INFO(
                "custom.scourge",
                "Normal Stratholme phase applied: player={}, guid={}, map={}, phase={}",
                player->GetName(),
                guid,
                MAP_STRATHOLME,
                NORMAL_PHASE_MASK
            );

            return;
        }

        uint32 pendingInstanceId = 0;

        if (GetSabotageMatchInstanceId(
                player,
                pendingInstanceId
            ) &&
            pendingInstanceId != 0)
        {
            auto transferItr =
                SabotagePendingEntryTransferMs.find(
                    guid
                );

            if (transferItr !=
                SabotagePendingEntryTransferMs.end())
            {
                TC_LOG_DEBUG(
                    "custom.scourge",
                    "Sabotage world entry transfer preserved: player={}, guid={}, currentMap={}, currentInstance={}, expectedInstance={}, remainingMs={}, group={}",
                    player->GetName(),
                    guid,
                    player->GetMapId(),
                    player->GetInstanceId(),
                    pendingInstanceId,
                    transferItr->second,
                    player->GetGroup()
                        ? player->GetGroup()->GetGUID().GetCounter()
                        : 0
                );

                return;
            }

            ExitSabotagePlayerAfterExternalTransfer(
                player,
                "폭파전 지역을 벗어나 경기에서 이탈했습니다."
            );

            return;
        }

        RestoreSabotageCombatState(
            player
        );

        auto itr =
            ScourgeModePhasePlayers.find(guid);

        if (itr == ScourgeModePhasePlayers.end())
            return;

        player->SetPhaseMask(
            NORMAL_PHASE_MASK,
            true
        );

        ScourgeModePhasePlayers.erase(itr);

        TC_LOG_INFO(
            "custom.scourge",
            "Scourge dungeon phase restored: player={}, guid={}, map={}, phase={}",
            player->GetName(),
            guid,
            player->GetMapId(),
            NORMAL_PHASE_MASK
        );
    }


    enum SabotageQueueType : uint8
    {
        SABOTAGE_QUEUE_NONE = 0,
        SABOTAGE_QUEUE_FACTION_WAR = 1,
        SABOTAGE_QUEUE_SAME_FACTION = 2
    };

    enum SabotageAssignedTeam : uint8
    {
        SABOTAGE_TEAM_NONE = 0,
        SABOTAGE_TEAM_A = 1,
        SABOTAGE_TEAM_B = 2
    };

    enum SabotageBombActionType : uint8
    {
        SABOTAGE_BOMB_ACTION_NONE = 0,
        SABOTAGE_BOMB_ACTION_INSTALL = 1,
        SABOTAGE_BOMB_ACTION_DISARM = 2
    };


    enum SabotageRoundPhase : uint8
    {
        SABOTAGE_ROUND_PHASE_WAITING = 0,
        SABOTAGE_ROUND_PHASE_ACTIVE = 1,
        SABOTAGE_ROUND_PHASE_BOMB_ARMED = 2,
        SABOTAGE_ROUND_PHASE_BREAK = 3,
        SABOTAGE_ROUND_PHASE_FINISHED = 4
    };

    enum SabotageCompetitorSide : uint8
    {
        SABOTAGE_COMPETITOR_NONE = 0,
        SABOTAGE_COMPETITOR_ONE = 1,
        SABOTAGE_COMPETITOR_TWO = 2
    };

    char const* GetSabotageTeamName(
        SabotageAssignedTeam team
    );

    struct SabotagePendingBombAction
    {
        SabotageBombActionType actionType;
        uint32 matchId;
        uint32 targetIndex;
        ObjectGuid targetGuid;
        uint32 spellId;

        SabotagePendingBombAction()
            : actionType(SABOTAGE_BOMB_ACTION_NONE),
              matchId(0),
              targetIndex(0),
              targetGuid(ObjectGuid::Empty),
              spellId(0)
        {
        }
    };

    static constexpr uint32 SABOTAGE_MIN_TEAM_SIZE = 1;
    static constexpr uint32 SABOTAGE_MAX_TEAM_SIZE = MAX_RAID_SIZE;

    static constexpr char const*
        SABOTAGE_ROUTE_PORTAL_TABLE =
            "custom_scourge_sabotage_route_portal";

    struct SabotageRoutePortalDefinition
    {
        uint32 routeId;
        std::string routeName;
        uint32 gameObjectEntry;

        float startX;
        float startY;
        float startZ;
        float startO;

        float endX;
        float endY;
        float endZ;
        float endO;

        float arrivalOffset;
        float zOffset;

        ObjectGuid startPortalGuid;
        ObjectGuid endPortalGuid;

        SabotageRoutePortalDefinition()
            : routeId(0),
              routeName(),
              gameObjectEntry(0),
              startX(0.0f),
              startY(0.0f),
              startZ(0.0f),
              startO(0.0f),
              endX(0.0f),
              endY(0.0f),
              endZ(0.0f),
              endO(0.0f),
              arrivalOffset(2.50f),
              zOffset(0.0f),
              startPortalGuid(ObjectGuid::Empty),
              endPortalGuid(ObjectGuid::Empty)
        {
        }
    };

    static constexpr uint32
        SABOTAGE_REWARD_INVASION_FRAGMENT_ITEM = 56940;

    static constexpr uint32
        SABOTAGE_REWARD_DEATH_RUNE_ITEM = 22484;

    static constexpr uint32
        SABOTAGE_REWARD_WIN_ITEM_COUNT = 3;

    static constexpr uint32
        SABOTAGE_REWARD_LOSS_ITEM_COUNT = 1;

    static constexpr std::array<
        uint32,
        5
    > SABOTAGE_REWARD_EXECUTION_FRAGMENT_ITEMS =
    {{
        56924,
        56925,
        56926,
        56927,
        56928
    }};

    static constexpr uint32 SABOTAGE_BOMB_ITEM = 56942;
    static constexpr uint32 SABOTAGE_BOMB_SUPPLY_GO = 961200;
    static constexpr uint32
        SABOTAGE_DEFENDER_SCOUT_GO = 961204;

    static constexpr uint32 SABOTAGE_BOMB_INSTALL_SPELL = 900206;
    static constexpr uint32 SABOTAGE_BOMB_DISARM_SPELL = 900207;

    static constexpr float SABOTAGE_BOMB_ACTION_DISTANCE = 7.0f;
    static constexpr uint32 SABOTAGE_TARGET_A_GO = 961201;
    static constexpr uint32 SABOTAGE_TARGET_B_GO = 961202;
    static constexpr uint32 SABOTAGE_TARGET_C_GO = 961203;
    static constexpr uint32 SABOTAGE_TARGET_COUNT = 3;


    static constexpr uint32 SABOTAGE_TOTAL_ROUNDS = 3;
    static constexpr uint32 SABOTAGE_ROUNDS_TO_WIN = 2;
    static constexpr uint32 SABOTAGE_ROUND_LIMIT_MS = 10 * 60 * 1000;
    static constexpr uint32 SABOTAGE_BOMB_EXPLOSION_MS = 60 * 1000;
    static constexpr uint32 SABOTAGE_ROUND_BREAK_MS = 10 * 1000;
    static constexpr uint32 SABOTAGE_FIRST_ROUND_WAIT_MS = 5 * 1000;
    static constexpr uint32 SABOTAGE_HUD_UPDATE_MS = 1000;

    struct SabotageBossPosition
    {
        uint32 creatureEntry;
        char const* locationName;
        float x;
        float y;
        float z;
        float orientation;

        SabotageBossPosition(
            uint32 entry,
            char const* name,
            float positionX,
            float positionY,
            float positionZ,
            float facing
        )
            : creatureEntry(entry),
              locationName(name),
              x(positionX),
              y(positionY),
              z(positionZ),
              orientation(facing)
        {
        }
    };

    static std::array<
        SabotageBossPosition,
        8
    > const SabotageBossPositions =
    {{
        // 39690 - Timmy the Cruel
        SabotageBossPosition(
            10808,
            "잔혹한 티미",
            3625.36f,
            -3188.11f,
            130.398f,
            4.83456f
        ),

        // 52149 - Grand Crusader Dathrohan
        SabotageBossPosition(
            10812,
            "대십자군 사령관 다스로한",
            3415.84f,
            -3044.54f,
            136.814f,
            5.42797f
        ),

        // 54237 - Magistrate Barthilas
        SabotageBossPosition(
            10435,
            "집정관 바실라스",
            3663.23f,
            -3619.14f,
            137.984f,
            5.34071f
        ),

        // 54238 - Baroness Anastari
        SabotageBossPosition(
            10436,
            "남작부인 아나스타리",
            3855.34f,
            -3715.75f,
            148.175f,
            1.27409f
        ),

        // 54239 - Nerub'enkan
        SabotageBossPosition(
            10437,
            "네룹엔칸",
            3854.98f,
            -3528.84f,
            144.293f,
            5.23599f
        ),

        // 54240 - Maleki the Pallid
        SabotageBossPosition(
            10438,
            "냉혈한 말레키",
            4035.81f,
            -3647.78f,
            135.738f,
            2.42601f
        ),

        // 54241 - Baron Rivendare
        SabotageBossPosition(
            10440,
            "남작 리븐데어",
            4035.83f,
            -3336.31f,
            115.144f,
            4.67748f
        ),

        // 54433 - Cannon Master Willey
        SabotageBossPosition(
            10997,
            "포병대장 윌리",
            3573.57f,
            -2937.31f,
            125.085f,
            3.78736f
        ),

    }};

    static constexpr int32
        SABOTAGE_NO_BOSS_POSITION_INDEX = -1;

    static constexpr float
        SABOTAGE_MIN_START_DISTANCE_SQ =
            100.0f * 100.0f;

    struct SabotageStartPosition
    {
        char const* locationName;
        float x;
        float y;
        float z;
        float orientation;
        int32 bossPositionIndex;

        SabotageStartPosition(
            char const* name,
            float positionX,
            float positionY,
            float positionZ,
            float facing,
            int32 targetPositionIndex
        )
            : locationName(name),
              x(positionX),
              y(positionY),
              z(positionZ),
              orientation(facing),
              bossPositionIndex(targetPositionIndex)
        {
        }
    };

    static std::array<
        SabotageStartPosition,
        6
    > const SabotageStartPositions =
    {{
        SabotageStartPosition(
            "정문",
            3392.92f,
            -3395.03f,
            142.77f,
            1.52f,
            SABOTAGE_NO_BOSS_POSITION_INDEX
        ),

        SabotageStartPosition(
            "후문",
            3593.15f,
            -3646.56f,
            138.50f,
            5.33f,
            SABOTAGE_NO_BOSS_POSITION_INDEX
        ),

        SabotageStartPosition(
            "잔혹한 티미 광장",
            3625.36f,
            -3188.11f,
            130.398f,
            4.83456f,
            0
        ),

        SabotageStartPosition(
            "성당 광장",
            3415.84f,
            -3044.54f,
            136.814f,
            5.42797f,
            1
        ),

        SabotageStartPosition(
            "집정관 바실라스 광장",
            3663.23f,
            -3619.14f,
            137.984f,
            5.34071f,
            2
        ),

        SabotageStartPosition(
            "포병대장 윌리 광장",
            3573.57f,
            -2937.31f,
            125.085f,
            3.78736f,
            7
        )
    }};

    struct SabotagePlayerQueueState
    {
        SabotageQueueType queueType;
        bool matched;
        uint32 matchId;
        SabotageAssignedTeam assignedTeam;
        SabotageCompetitorSide competitorSide;
        uint32 faction;
        bool returnPositionStored;
        uint32 returnMapId;
        float returnX;
        float returnY;
        float returnZ;
        float returnOrientation;

        SabotagePlayerQueueState()
            : queueType(SABOTAGE_QUEUE_NONE),
              matched(false),
              matchId(0),
              assignedTeam(SABOTAGE_TEAM_NONE),
              competitorSide(SABOTAGE_COMPETITOR_NONE),
              faction(0),
              returnPositionStored(false),
              returnMapId(0),
              returnX(0.0f),
              returnY(0.0f),
              returnZ(0.0f),
              returnOrientation(0.0f)
        {
        }
    };

    void StoreSabotageQueueReturnPosition(
        Player* player,
        SabotagePlayerQueueState& state
    )
    {
        if (!player ||
            state.returnPositionStored)
        {
            return;
        }

        state.returnPositionStored = true;
        state.returnMapId =
            player->GetMapId();
        state.returnX =
            player->GetPositionX();
        state.returnY =
            player->GetPositionY();
        state.returnZ =
            player->GetPositionZ();
        state.returnOrientation =
            player->GetOrientation();

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage queue return position stored: player={}, guid={}, map={}, x={}, y={}, z={}, o={}",
            player->GetName(),
            GetCharacterGuid(player),
            state.returnMapId,
            state.returnX,
            state.returnY,
            state.returnZ,
            state.returnOrientation
        );
    }

    bool TeleportSabotagePlayerToQueueReturnPosition(
        Player* player,
        SabotagePlayerQueueState const& state
    )
    {
        if (!player)
            return false;

        if (state.returnPositionStored)
        {
            bool teleported =
                player->TeleportTo(
                    state.returnMapId,
                    state.returnX,
                    state.returnY,
                    state.returnZ,
                    state.returnOrientation
                );

            if (teleported)
            {
                TC_LOG_INFO(
                    "custom.scourge",
                    "Sabotage player returned to queue position: player={}, guid={}, map={}, x={}, y={}, z={}, o={}",
                    player->GetName(),
                    GetCharacterGuid(player),
                    state.returnMapId,
                    state.returnX,
                    state.returnY,
                    state.returnZ,
                    state.returnOrientation
                );

                return true;
            }

            TC_LOG_ERROR(
                "custom.scourge",
                "Failed to return sabotage player to queue position: player={}, guid={}, map={}, x={}, y={}, z={}, o={}",
                player->GetName(),
                GetCharacterGuid(player),
                state.returnMapId,
                state.returnX,
                state.returnY,
                state.returnZ,
                state.returnOrientation
            );
        }

        bool homeTeleported =
            player->TeleportTo(
                player->m_homebindMapId,
                player->m_homebindX,
                player->m_homebindY,
                player->m_homebindZ,
                player->GetOrientation()
            );

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage homebind fallback used: player={}, guid={}, teleported={}",
            player->GetName(),
            GetCharacterGuid(player),
            homeTeleported ? 1 : 0
        );

        return homeTeleported;
    }

    struct SabotagePreparedMatch
    {
        uint32 matchId;
        SabotageQueueType queueType;
        uint32 faction;
        uint32 instanceId;
        bool launched;
        InstanceSave* instanceSave;
        ObjectGuid teamAGroupGuid;
        ObjectGuid teamBGroupGuid;
        ObjectGuid sideOneGroupGuid;
        ObjectGuid sideTwoGroupGuid;
        ObjectGuid bombCarrierGuid;
        ObjectGuid bombObjectGuid;
        ObjectGuid defenderScoutObjectGuid;
        bool routePortalObjectsSpawned;
        std::vector<ObjectGuid> routePortalObjectGuids;
        std::vector<SabotageRoutePortalDefinition>
            routePortalDefinitions;
        bool bombTargetsSpawned;
        bool smokeUsedThisRound;
        ObjectGuid smokeObjectGuid;
        uint32 smokeObjectRemainingMs;
        std::array<ObjectGuid, SABOTAGE_TARGET_COUNT>
            bombTargetGuids;
        std::array<bool, SABOTAGE_TARGET_COUNT>
            bombTargetInstalled;
        std::array<uint32, SABOTAGE_TARGET_COUNT>
            bombTargetPositionIndices;
        std::vector<ObjectGuid> teamA;
        std::vector<ObjectGuid> teamB;
        std::vector<ObjectGuid> sideOne;
        std::vector<ObjectGuid> sideTwo;
        uint32 currentRound;
        uint32 sideOneScore;
        uint32 sideTwoScore;
        SabotageRoundPhase roundPhase;
        uint32 roundTimerMs;
        uint32 bombExplosionTimerMs;
        uint32 roundBreakTimerMs;
        uint32 hudUpdateTimerMs;
        uint32 installedTargetIndex;
        bool sideOneAttacking;
        SabotageCompetitorSide matchWinnerSide;
        uint32 teamAStartPositionIndex;
        uint32 teamBStartPositionIndex;

        SabotagePreparedMatch()
            : matchId(0),
              queueType(SABOTAGE_QUEUE_NONE),
              faction(0),
              instanceId(0),
              launched(false),
              instanceSave(nullptr),
              teamAGroupGuid(ObjectGuid::Empty),
              teamBGroupGuid(ObjectGuid::Empty),
              sideOneGroupGuid(ObjectGuid::Empty),
              sideTwoGroupGuid(ObjectGuid::Empty),
              bombCarrierGuid(ObjectGuid::Empty),
              bombObjectGuid(ObjectGuid::Empty),
              defenderScoutObjectGuid(ObjectGuid::Empty),
              routePortalObjectsSpawned(false),
              routePortalObjectGuids(),
              routePortalDefinitions(),
              bombTargetsSpawned(false),
              smokeUsedThisRound(false),
              smokeObjectGuid(ObjectGuid::Empty),
              smokeObjectRemainingMs(0),
              bombTargetGuids{},
              bombTargetInstalled{},
              bombTargetPositionIndices{},
              currentRound(0),
              sideOneScore(0),
              sideTwoScore(0),
              roundPhase(SABOTAGE_ROUND_PHASE_WAITING),
              roundTimerMs(0),
              bombExplosionTimerMs(0),
              roundBreakTimerMs(SABOTAGE_FIRST_ROUND_WAIT_MS),
              hudUpdateTimerMs(0),
              installedTargetIndex(SABOTAGE_TARGET_COUNT),
              sideOneAttacking(true),
              matchWinnerSide(SABOTAGE_COMPETITOR_NONE),
              teamAStartPositionIndex(0),
              teamBStartPositionIndex(1)
        {
        }
    };

    std::deque<ObjectGuid> SabotageFactionAllianceQueue;
    std::deque<ObjectGuid> SabotageFactionHordeQueue;
    std::deque<ObjectGuid> SabotageSameAllianceQueue;
    std::deque<ObjectGuid> SabotageSameHordeQueue;

    std::unordered_map<uint32, SabotagePlayerQueueState>
        SabotageQueueStates;

    std::unordered_map<uint32, SabotagePreparedMatch>
        SabotagePreparedMatches;

    SabotageStartPosition const&
        GetSabotageStartPosition(
            SabotagePreparedMatch const& match,
            SabotageAssignedTeam team
        )
    {
        uint32 index =
            team == SABOTAGE_TEAM_B
                ? match.teamBStartPositionIndex
                : match.teamAStartPositionIndex;

        if (index >= SabotageStartPositions.size())
        {
            index =
                team == SABOTAGE_TEAM_B
                    ? 1
                    : 0;
        }

        return SabotageStartPositions[index];
    }

    bool IsSabotageBossPositionUsedByStart(
        SabotagePreparedMatch const& match,
        uint32 bossPositionIndex
    )
    {
        SabotageStartPosition const& teamAStart =
            GetSabotageStartPosition(
                match,
                SABOTAGE_TEAM_A
            );

        SabotageStartPosition const& teamBStart =
            GetSabotageStartPosition(
                match,
                SABOTAGE_TEAM_B
            );

        return
            teamAStart.bossPositionIndex ==
                int32(bossPositionIndex) ||
            teamBStart.bossPositionIndex ==
                int32(bossPositionIndex);
    }

    bool IsSabotageStartBlockedByTargets(
        SabotagePreparedMatch const& match,
        uint32 startPositionIndex
    )
    {
        if (!match.bombTargetsSpawned ||
            startPositionIndex >=
                SabotageStartPositions.size())
        {
            return false;
        }

        int32 bossPositionIndex =
            SabotageStartPositions[
                startPositionIndex
            ].bossPositionIndex;

        if (bossPositionIndex <
            0)
        {
            return false;
        }

        for (uint32 index = 0;
             index < SABOTAGE_TARGET_COUNT;
             ++index)
        {
            if (
                match.bombTargetPositionIndices[index] ==
                uint32(bossPositionIndex)
            )
            {
                return true;
            }
        }

        return false;
    }

    float GetSabotageStartDistanceSq(
        SabotageStartPosition const& left,
        SabotageStartPosition const& right
    )
    {
        float deltaX =
            left.x - right.x;

        float deltaY =
            left.y - right.y;

        return
            deltaX * deltaX +
            deltaY * deltaY;
    }

    void SelectSabotageRoundStartPositions(
        SabotagePreparedMatch& match,
        bool avoidCurrentPair
    )
    {
        uint32 const previousTeamAStartPositionIndex =
            match.teamAStartPositionIndex;

        std::vector<uint32> availableStarts;
        availableStarts.reserve(
            SabotageStartPositions.size()
        );

        for (uint32 index = 0;
             index < SabotageStartPositions.size();
             ++index)
        {
            if (IsSabotageStartBlockedByTargets(
                    match,
                    index
                ))
            {
                continue;
            }

            availableStarts.push_back(index);
        }

        if (availableStarts.size() < 2)
        {
            availableStarts.clear();

            for (uint32 index = 0;
                 index < SabotageStartPositions.size();
                 ++index)
            {
                availableStarts.push_back(index);
            }
        }

        uint32 selectedA =
            availableStarts.front();

        uint32 selectedB =
            availableStarts.back();

        bool selected = false;

        for (uint32 attempt = 0;
             attempt < 32;
             ++attempt)
        {
            uint32 candidateA =
                availableStarts[
                    urand(
                        0,
                        uint32(
                            availableStarts.size() - 1
                        )
                    )
                ];

            uint32 candidateB =
                availableStarts[
                    urand(
                        0,
                        uint32(
                            availableStarts.size() - 1
                        )
                    )
                ];

            if (candidateA == candidateB)
                continue;

            if (avoidCurrentPair &&
                candidateA ==
                    previousTeamAStartPositionIndex)
            {
                continue;
            }

            if (
                GetSabotageStartDistanceSq(
                    SabotageStartPositions[
                        candidateA
                    ],
                    SabotageStartPositions[
                        candidateB
                    ]
                ) <
                SABOTAGE_MIN_START_DISTANCE_SQ
            )
            {
                continue;
            }

            selectedA = candidateA;
            selectedB = candidateB;
            selected = true;
            break;
        }

        if (!selected)
        {
            for (uint32 leftIndex = 0;
                 leftIndex <
                    availableStarts.size() &&
                    !selected;
                 ++leftIndex)
            {
                for (uint32 rightIndex =
                        leftIndex + 1;
                     rightIndex <
                        availableStarts.size();
                     ++rightIndex)
                {
                    uint32 candidateA =
                        availableStarts[
                            leftIndex
                        ];

                    uint32 candidateB =
                        availableStarts[
                            rightIndex
                        ];

                    if (avoidCurrentPair &&
                        candidateA ==
                            previousTeamAStartPositionIndex)
                    {
                        continue;
                    }

                    if (
                        GetSabotageStartDistanceSq(
                            SabotageStartPositions[
                                candidateA
                            ],
                            SabotageStartPositions[
                                candidateB
                            ]
                        ) <
                        SABOTAGE_MIN_START_DISTANCE_SQ
                    )
                    {
                        continue;
                    }

                    selectedA = candidateA;
                    selectedB = candidateB;
                    selected = true;
                    break;
                }
            }
        }

        if (!selected)
        {
            selectedA = availableStarts[0];

            if (avoidCurrentPair &&
                selectedA ==
                    previousTeamAStartPositionIndex)
            {
                for (uint32 candidateA :
                    availableStarts)
                {
                    if (candidateA !=
                        previousTeamAStartPositionIndex)
                    {
                        selectedA = candidateA;
                        break;
                    }
                }
            }

            selectedB = availableStarts[0];

            for (uint32 candidateB :
                availableStarts)
            {
                if (candidateB != selectedA)
                {
                    selectedB = candidateB;
                    break;
                }
            }
        }

        match.teamAStartPositionIndex =
            selectedA;

        match.teamBStartPositionIndex =
            selectedB;

        SabotageStartPosition const& teamAStart =
            SabotageStartPositions[
                selectedA
            ];

        SabotageStartPosition const& teamBStart =
            SabotageStartPositions[
                selectedB
            ];

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage round starts selected: match={}, round={}, previousTeamAStart={}, teamAStart={}({}), teamBStart={}({}), distanceSq={}",
            match.matchId,
            match.currentRound,
            previousTeamAStartPositionIndex,
            selectedA,
            teamAStart.locationName,
            selectedB,
            teamBStart.locationName,
            GetSabotageStartDistanceSq(
                teamAStart,
                teamBStart
            )
        );
    }

    void GetSabotageStartOffsetPosition(
        SabotageStartPosition const& start,
        float forwardOffset,
        float rightOffset,
        float& x,
        float& y,
        float& z,
        float& orientation
    )
    {
        float facingCos =
            std::cos(start.orientation);

        float facingSin =
            std::sin(start.orientation);

        x =
            start.x +
            facingCos * forwardOffset -
            facingSin * rightOffset;

        y =
            start.y +
            facingSin * forwardOffset +
            facingCos * rightOffset;

        z = start.z;
        orientation = start.orientation;
    }

    std::unordered_map<uint32, SabotagePendingBombAction>
        SabotagePendingBombActions;

    void ClearSabotagePendingBombAction(
        ObjectGuid guid
    )
    {
        uint32 guidLow =
            uint32(guid.GetCounter());

        if (!guidLow)
            return;

        SabotagePendingBombActions.erase(
            guidLow
        );
    }

    uint32 NextSabotageMatchId = 1;

    uint32 ActiveFactionWarMatchId = 0;
    uint32 ActiveSameAllianceMatchId = 0;
    uint32 ActiveSameHordeMatchId = 0;

    uint32* GetActiveSabotageMatchSlot(
        SabotageQueueType queueType,
        uint32 faction
    )
    {
        if (queueType ==
            SABOTAGE_QUEUE_FACTION_WAR)
        {
            return &ActiveFactionWarMatchId;
        }

        if (queueType !=
            SABOTAGE_QUEUE_SAME_FACTION)
        {
            return nullptr;
        }

        if (faction == ALLIANCE)
            return &ActiveSameAllianceMatchId;

        if (faction == HORDE)
            return &ActiveSameHordeMatchId;

        return nullptr;
    }

    SabotagePreparedMatch* FindActiveSabotageMatch(
        SabotageQueueType queueType,
        uint32 faction
    )
    {
        uint32* slot =
            GetActiveSabotageMatchSlot(
                queueType,
                faction
            );

        if (!slot || !*slot)
            return nullptr;

        auto itr =
            SabotagePreparedMatches.find(*slot);

        if (itr ==
                SabotagePreparedMatches.end() ||
            !itr->second.launched ||
            !itr->second.instanceId ||
            !itr->second.instanceSave)
        {
            *slot = 0;
            return nullptr;
        }

        return &itr->second;
    }

    void RegisterActiveSabotageMatch(
        SabotagePreparedMatch const& match
    )
    {
        uint32* slot =
            GetActiveSabotageMatchSlot(
                match.queueType,
                match.faction
            );

        if (slot)
            *slot = match.matchId;
    }

    void ClearActiveSabotageMatch(
        uint32 matchId
    )
    {
        if (ActiveFactionWarMatchId == matchId)
            ActiveFactionWarMatchId = 0;

        if (ActiveSameAllianceMatchId == matchId)
            ActiveSameAllianceMatchId = 0;

        if (ActiveSameHordeMatchId == matchId)
            ActiveSameHordeMatchId = 0;
    }

    bool GetSabotageMatchInstanceId(
        Player const* player,
        uint32& instanceId
    )
    {
        instanceId = 0;

        uint32 guidLow =
            GetCharacterGuid(player);

        if (!guidLow)
            return false;

        auto stateItr =
            SabotageQueueStates.find(guidLow);

        if (stateItr ==
            SabotageQueueStates.end())
        {
            return false;
        }

        SabotagePlayerQueueState const& state =
            stateItr->second;

        if (!state.matched ||
            !state.matchId)
        {
            return false;
        }

        auto matchItr =
            SabotagePreparedMatches.find(
                state.matchId
            );

        if (matchItr ==
            SabotagePreparedMatches.end())
        {
            return false;
        }

        SabotagePreparedMatch const& match =
            matchItr->second;

        if (!match.launched ||
            !match.instanceId)
        {
            return false;
        }

        instanceId = match.instanceId;
        return true;
    }

    bool GetSabotagePlayerMatchContext(
        Player* player,
        SabotagePreparedMatch*& match,
        SabotagePlayerQueueState*& state
    )
    {
        match = nullptr;
        state = nullptr;

        if (!player ||
            player->GetMapId() != MAP_STRATHOLME)
        {
            return false;
        }

        uint32 guidLow =
            GetCharacterGuid(player);

        if (!guidLow)
            return false;

        auto stateItr =
            SabotageQueueStates.find(guidLow);

        if (stateItr ==
            SabotageQueueStates.end())
        {
            return false;
        }

        if (!stateItr->second.matched ||
            !stateItr->second.matchId ||
            stateItr->second.assignedTeam ==
                SABOTAGE_TEAM_NONE)
        {
            return false;
        }

        auto matchItr =
            SabotagePreparedMatches.find(
                stateItr->second.matchId
            );

        if (matchItr ==
            SabotagePreparedMatches.end())
        {
            return false;
        }

        if (!matchItr->second.launched ||
            !matchItr->second.instanceId ||
            player->GetInstanceId() !=
                matchItr->second.instanceId)
        {
            return false;
        }

        match = &matchItr->second;
        state = &stateItr->second;
        return true;
    }

    void SendSabotageMatchMessage(
        SabotagePreparedMatch const& match,
        std::string const& text
    )
    {
        for (ObjectGuid const& guid : match.teamA)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                SendSabotageMessage(
                    player,
                    text
                );
            }
        }

        for (ObjectGuid const& guid : match.teamB)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                SendSabotageMessage(
                    player,
                    text
                );
            }
        }
    }

    bool LoadSabotageRoutePortalDefinitions(
        std::vector<SabotageRoutePortalDefinition>& definitions
    )
    {
        definitions.clear();

        QueryResult result =
            WorldDatabase.Query(
                "SELECT "
                "`route_id`, "
                "`route_name`, "
                "`gameobject_entry`, "
                "`start_x`, "
                "`start_y`, "
                "`start_z`, "
                "`start_o`, "
                "`end_x`, "
                "`end_y`, "
                "`end_z`, "
                "`end_o`, "
                "`arrival_offset`, "
                "`z_offset` "
                "FROM `custom_scourge_sabotage_route_portal` "
                "WHERE `enabled` = 1 "
                "ORDER BY `route_id`"
            );

        if (!result)
            return false;

        do
        {
            Field* fields =
                result->Fetch();

            SabotageRoutePortalDefinition definition;

            definition.routeId =
                fields[0].GetUInt32();

            definition.routeName =
                fields[1].GetString();

            definition.gameObjectEntry =
                fields[2].GetUInt32();

            definition.startX =
                fields[3].GetFloat();

            definition.startY =
                fields[4].GetFloat();

            definition.startZ =
                fields[5].GetFloat();

            definition.startO =
                fields[6].GetFloat();

            definition.endX =
                fields[7].GetFloat();

            definition.endY =
                fields[8].GetFloat();

            definition.endZ =
                fields[9].GetFloat();

            definition.endO =
                fields[10].GetFloat();

            definition.arrivalOffset =
                fields[11].GetFloat();

            definition.zOffset =
                fields[12].GetFloat();

            if (!definition.routeId ||
                !definition.gameObjectEntry)
            {
                TC_LOG_ERROR(
                    "custom.scourge",
                    "Sabotage route portal skipped: invalid route or gameobject entry, route={}, entry={}",
                    definition.routeId,
                    definition.gameObjectEntry
                );

                continue;
            }

            if (definition.arrivalOffset <
                0.0f)
            {
                definition.arrivalOffset =
                    0.0f;
            }
            else if (definition.arrivalOffset >
                10.0f)
            {
                definition.arrivalOffset =
                    10.0f;
            }

            definitions.push_back(
                definition
            );
        }
        while (result->NextRow());

        return !definitions.empty();
    }

    GameObject* SpawnSabotageRoutePortalObject(
        Player* anchor,
        uint32 gameObjectEntry,
        float x,
        float y,
        float z,
        float orientation
    )
    {
        if (!anchor ||
            !anchor->IsInWorld() ||
            anchor->GetMapId() != MAP_STRATHOLME ||
            !gameObjectEntry)
        {
            return nullptr;
        }

        Map* map =
            anchor->GetMap();

        if (!map)
            return nullptr;

        Position position(
            x,
            y,
            z,
            orientation
        );

        QuaternionData rotation =
            QuaternionData::fromEulerAnglesZYX(
                orientation,
                0.0f,
                0.0f
            );

        GameObject* portalObject =
            new GameObject();

        if (!portalObject->Create(
                map->GenerateLowGuid<
                    HighGuid::GameObject
                >(),
                gameObjectEntry,
                map,
                SCOURGE_PHASE_MASK,
                position,
                rotation,
                255,
                GO_STATE_READY
            ))
        {
            delete portalObject;
            return nullptr;
        }

        portalObject->SetSpawnedByDefault(
            false
        );

        portalObject->SetRespawnTime(
            DAY
        );

        if (!map->AddToMap(
                portalObject
            ))
        {
            delete portalObject;
            return nullptr;
        }

        return portalObject;
    }

    void DeleteSabotageRoutePortalObjects(
        SabotagePreparedMatch& match
    )
    {
        if (match.routePortalObjectGuids.empty())
        {
            match.routePortalDefinitions.clear();
            match.routePortalObjectsSpawned =
                false;

            return;
        }

        Map* map =
            sMapMgr->FindMap(
                MAP_STRATHOLME,
                match.instanceId
            );

        if (map)
        {
            for (ObjectGuid const& guid :
                match.routePortalObjectGuids)
            {
                if (guid.IsEmpty())
                    continue;

                if (GameObject* portalObject =
                    map->GetGameObject(guid))
                {
                    portalObject->Delete();
                }
            }
        }

        match.routePortalObjectGuids.clear();
        match.routePortalDefinitions.clear();
        match.routePortalObjectsSpawned = false;
    }

    bool SpawnSabotageRoutePortalDefinition(
        Player* anchor,
        SabotagePreparedMatch& match,
        SabotageRoutePortalDefinition& definition
    )
    {
        GameObject* startPortal =
            SpawnSabotageRoutePortalObject(
                anchor,
                definition.gameObjectEntry,
                definition.startX,
                definition.startY,
                definition.startZ +
                    definition.zOffset,
                definition.startO
            );

        if (!startPortal)
        {
            TC_LOG_ERROR(
                "custom.scourge",
                "Sabotage start portal spawn failed: match={}, route={}({}), entry={}, x={}, y={}, z={}, o={}",
                match.matchId,
                definition.routeId,
                definition.routeName,
                definition.gameObjectEntry,
                definition.startX,
                definition.startY,
                definition.startZ +
                    definition.zOffset,
                definition.startO
            );

            return false;
        }

        GameObject* endPortal =
            SpawnSabotageRoutePortalObject(
                anchor,
                definition.gameObjectEntry,
                definition.endX,
                definition.endY,
                definition.endZ +
                    definition.zOffset,
                definition.endO
            );

        if (!endPortal)
        {
            startPortal->Delete();

            TC_LOG_ERROR(
                "custom.scourge",
                "Sabotage end portal spawn failed: match={}, route={}({}), entry={}, x={}, y={}, z={}, o={}",
                match.matchId,
                definition.routeId,
                definition.routeName,
                definition.gameObjectEntry,
                definition.endX,
                definition.endY,
                definition.endZ +
                    definition.zOffset,
                definition.endO
            );

            return false;
        }

        definition.startPortalGuid =
            startPortal->GetGUID();

        definition.endPortalGuid =
            endPortal->GetGUID();

        match.routePortalObjectGuids.push_back(
            definition.startPortalGuid
        );

        match.routePortalObjectGuids.push_back(
            definition.endPortalGuid
        );

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage route portal pair spawned: match={}, instance={}, route={}({}), entry={}, startGuid={}, endGuid={}, start=({}, {}, {}, {}), end=({}, {}, {}, {})",
            match.matchId,
            match.instanceId,
            definition.routeId,
            definition.routeName,
            definition.gameObjectEntry,
            definition.startPortalGuid.GetCounter(),
            definition.endPortalGuid.GetCounter(),
            definition.startX,
            definition.startY,
            definition.startZ +
                definition.zOffset,
            definition.startO,
            definition.endX,
            definition.endY,
            definition.endZ +
                definition.zOffset,
            definition.endO
        );

        return true;
    }

    bool EnsureSabotageRoutePortalObjectsSpawned(
        SabotagePreparedMatch& match
    )
    {
        if (match.routePortalObjectsSpawned)
            return true;

        Player* anchor =
            FindSabotageRoundAnchor(
                match
            );

        if (!anchor)
            return false;

        std::vector<SabotageRoutePortalDefinition>
            definitions;

        if (!LoadSabotageRoutePortalDefinitions(
                definitions
            ))
        {
            TC_LOG_INFO(
                "custom.scourge",
                "No enabled sabotage route portals found: match={}, table={}",
                match.matchId,
                SABOTAGE_ROUTE_PORTAL_TABLE
            );

            return true;
        }

        DeleteSabotageRoutePortalObjects(
            match
        );

        uint32 spawnedRouteCount = 0;

        for (
            SabotageRoutePortalDefinition definition :
            definitions
        )
        {
            if (!SpawnSabotageRoutePortalDefinition(
                    anchor,
                    match,
                    definition
                ))
            {
                continue;
            }

            match.routePortalDefinitions.push_back(
                definition
            );

            ++spawnedRouteCount;
        }

        match.routePortalObjectsSpawned =
            !match.routePortalObjectGuids.empty();

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage route portals completed: match={}, instance={}, routes={}, objects={}",
            match.matchId,
            match.instanceId,
            spawnedRouteCount,
            match.routePortalObjectGuids.size()
        );

        return true;
    }

    SabotageRoutePortalDefinition const*
        FindSabotageRoutePortalDefinition(
            SabotagePreparedMatch const& match,
            ObjectGuid portalGuid,
            bool& startPortal
        )
    {
        startPortal = false;

        for (
            SabotageRoutePortalDefinition const&
                definition :
            match.routePortalDefinitions
        )
        {
            if (definition.startPortalGuid ==
                portalGuid)
            {
                startPortal = true;
                return &definition;
            }

            if (definition.endPortalGuid ==
                portalGuid)
            {
                startPortal = false;
                return &definition;
            }
        }

        return nullptr;
    }

    void GetSabotageRoutePortalArrivalPosition(
        SabotageRoutePortalDefinition const& definition,
        bool clickedStartPortal,
        float& x,
        float& y,
        float& z,
        float& orientation
    )
    {
        if (clickedStartPortal)
        {
            x = definition.endX;
            y = definition.endY;
            z =
                definition.endZ +
                definition.zOffset;
            orientation =
                definition.endO;
        }
        else
        {
            x = definition.startX;
            y = definition.startY;
            z =
                definition.startZ +
                definition.zOffset;
            orientation =
                definition.startO;
        }

        x +=
            std::cos(orientation) *
            definition.arrivalOffset;

        y +=
            std::sin(orientation) *
            definition.arrivalOffset;
    }

    GameObject* SpawnSabotageBombObject(
        Player* anchor,
        float x,
        float y,
        float z,
        float orientation
    )
    {
        if (!anchor ||
            !anchor->IsInWorld() ||
            anchor->GetMapId() != MAP_STRATHOLME)
        {
            return nullptr;
        }

        Map* map = anchor->GetMap();

        if (!map)
            return nullptr;

        Position position(
            x,
            y,
            z + 0.20f,
            orientation
        );

        QuaternionData rotation =
            QuaternionData::fromEulerAnglesZYX(
                orientation,
                0.0f,
                0.0f
            );

        GameObject* bombObject =
            new GameObject();

        if (!bombObject->Create(
            map->GenerateLowGuid<
                HighGuid::GameObject
            >(),
            SABOTAGE_BOMB_SUPPLY_GO,
            map,
            SCOURGE_PHASE_MASK,
            position,
            rotation,
            255,
            GO_STATE_READY
        ))
        {
            delete bombObject;
            return nullptr;
        }

        bombObject->SetSpawnedByDefault(
            false
        );

        bombObject->SetRespawnTime(
            DAY
        );

        if (!map->AddToMap(bombObject))
        {
            delete bombObject;
            return nullptr;
        }

        return bombObject;
    }

    GameObject* SpawnSabotageDefenderScoutGameObject(
        Player* anchor,
        SabotageStartPosition const& start
    )
    {
        if (!anchor ||
            !anchor->IsInWorld() ||
            anchor->GetMapId() != MAP_STRATHOLME)
        {
            return nullptr;
        }

        Map* map = anchor->GetMap();

        if (!map)
            return nullptr;

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float orientation = 0.0f;

        GetSabotageStartOffsetPosition(
            start,
            2.0f,
            2.0f,
            x,
            y,
            z,
            orientation
        );

        Position position(
            x,
            y,
            z + 0.20f,
            orientation
        );

        QuaternionData rotation =
            QuaternionData::fromEulerAnglesZYX(
                orientation,
                0.0f,
                0.0f
            );

        GameObject* scoutObject =
            new GameObject();

        if (!scoutObject->Create(
            map->GenerateLowGuid<
                HighGuid::GameObject
            >(),
            SABOTAGE_DEFENDER_SCOUT_GO,
            map,
            SCOURGE_PHASE_MASK,
            position,
            rotation,
            255,
            GO_STATE_READY
        ))
        {
            delete scoutObject;
            return nullptr;
        }

        scoutObject->SetSpawnedByDefault(
            false
        );

        scoutObject->SetRespawnTime(
            DAY
        );

        if (!map->AddToMap(
                scoutObject
            ))
        {
            delete scoutObject;
            return nullptr;
        }

        return scoutObject;
    }

    void DeleteSabotageDefenderScoutObject(
        SabotagePreparedMatch& match
    )
    {
        if (match.defenderScoutObjectGuid.IsEmpty())
            return;

        Map* map =
            sMapMgr->FindMap(
                MAP_STRATHOLME,
                match.instanceId
            );

        if (map)
        {
            if (GameObject* scoutObject =
                map->GetGameObject(
                    match.defenderScoutObjectGuid
                ))
            {
                scoutObject->Delete();
            }
        }

        match.defenderScoutObjectGuid.Clear();
    }

    bool SpawnSabotageDefenderScoutObject(
        SabotagePreparedMatch& match
    )
    {
        DeleteSabotageDefenderScoutObject(
            match
        );

        Player* anchor =
            FindSabotageRoundAnchor(
                match
            );

        if (!anchor)
            return false;

        SabotageStartPosition const& start =
            GetSabotageStartPosition(
                match,
                SABOTAGE_TEAM_B
            );

        GameObject* scoutObject =
            SpawnSabotageDefenderScoutGameObject(
                anchor,
                start
            );

        if (!scoutObject)
            return false;

        match.defenderScoutObjectGuid =
            scoutObject->GetGUID();

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage defender scout object spawned: match={}, instance={}, goGuid={}, entry={}, x={}, y={}, z={}",
            match.matchId,
            match.instanceId,
            scoutObject->GetGUID().GetCounter(),
            SABOTAGE_DEFENDER_SCOUT_GO,
            scoutObject->GetPositionX(),
            scoutObject->GetPositionY(),
            scoutObject->GetPositionZ()
        );

        return true;
    }

    void ClearSabotageDefenderScoutStates(
        SabotagePreparedMatch const& match
    )
    {
        for (ObjectGuid const& guid :
            GetSabotageAllPlayers(
                match
            ))
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                ))
            {
                DisableSabotageDefenderScout(
                    player,
                    false
                );
            }
        }
    }

    bool LoadSabotageBossPositions()
    {
        static bool logged = false;

        if (!logged)
        {
            logged = true;

            TC_LOG_INFO(
                "custom.scourge",
                "Loaded hardcoded Stratholme boss positions for sabotage targets: count={}",
                SabotageBossPositions.size()
            );
        }

        return
            SabotageBossPositions.size() >=
            SABOTAGE_TARGET_COUNT;
    }

    GameObject* SpawnSabotageTargetObject(
        Player* anchor,
        uint32 targetEntry,
        SabotageBossPosition const& bossPosition
    )
    {
        if (!anchor ||
            !anchor->IsInWorld() ||
            anchor->GetMapId() != MAP_STRATHOLME)
        {
            return nullptr;
        }

        Map* map = anchor->GetMap();

        if (!map)
            return nullptr;

        Position position(
            bossPosition.x,
            bossPosition.y,
            bossPosition.z + 0.20f,
            bossPosition.orientation
        );

        QuaternionData rotation =
            QuaternionData::fromEulerAnglesZYX(
                bossPosition.orientation,
                0.0f,
                0.0f
            );

        GameObject* targetObject =
            new GameObject();

        if (!targetObject->Create(
            map->GenerateLowGuid<
                HighGuid::GameObject
            >(),
            targetEntry,
            map,
            SCOURGE_PHASE_MASK,
            position,
            rotation,
            255,
            GO_STATE_READY
        ))
        {
            delete targetObject;
            return nullptr;
        }

        targetObject->SetSpawnedByDefault(
            false
        );

        targetObject->SetRespawnTime(
            DAY
        );

        if (!map->AddToMap(targetObject))
        {
            delete targetObject;
            return nullptr;
        }

        return targetObject;
    }

    void DespawnSabotageBombTargets(
        SabotagePreparedMatch& match
    )
    {
        Map* map =
            sMapMgr->FindMap(
                MAP_STRATHOLME,
                match.instanceId
            );

        if (map)
        {
            for (ObjectGuid const& guid :
                match.bombTargetGuids)
            {
                if (guid.IsEmpty())
                    continue;

                if (GameObject* target =
                    map->GetGameObject(guid))
                {
                    target->Delete();
                }
            }
        }

        match.bombTargetGuids.fill(
            ObjectGuid::Empty
        );

        match.bombTargetInstalled.fill(
            false
        );

        match.bombTargetPositionIndices.fill(
            0
        );

        match.bombTargetsSpawned = false;
    }

    bool EnsureSabotageBombTargetsSpawned(
        Player* anchor,
        SabotagePreparedMatch& match
    )
    {
        if (!anchor ||
            !anchor->IsInWorld() ||
            anchor->GetMapId() != MAP_STRATHOLME ||
            anchor->GetInstanceId() !=
                match.instanceId)
        {
            return false;
        }

        if (match.bombTargetsSpawned)
            return true;

        if (!LoadSabotageBossPositions())
        {
            SendSabotageMatchMessage(
                match,
                "보스 위치를 불러오지 못하여 폭파 지점을 생성하지 못했습니다."
            );

            return false;
        }

        std::vector<uint32> indices;
        indices.reserve(
            SabotageBossPositions.size()
        );

        for (uint32 index = 0;
            index < SabotageBossPositions.size();
            ++index)
        {
            if (IsSabotageBossPositionUsedByStart(
                    match,
                    index
                ))
            {
                continue;
            }

            indices.push_back(index);
        }

        if (indices.size() <
            SABOTAGE_TARGET_COUNT)
        {
            indices.clear();

            for (uint32 index = 0;
                 index <
                    SabotageBossPositions.size();
                 ++index)
            {
                indices.push_back(index);
            }
        }

        for (uint32 index = 0;
            index < SABOTAGE_TARGET_COUNT;
            ++index)
        {
            uint32 randomIndex =
                urand(
                    index,
                    uint32(indices.size() - 1)
                );

            std::swap(
                indices[index],
                indices[randomIndex]
            );
        }

        static constexpr std::array<
            uint32,
            SABOTAGE_TARGET_COUNT
        > targetEntries =
        {
            SABOTAGE_TARGET_A_GO,
            SABOTAGE_TARGET_B_GO,
            SABOTAGE_TARGET_C_GO
        };

        std::array<ObjectGuid, SABOTAGE_TARGET_COUNT>
            createdGuids{};

        std::ostringstream targetLocationText;
        targetLocationText
            << "폭파 지점 생성 위치: ";

        for (uint32 index = 0;
            index < SABOTAGE_TARGET_COUNT;
            ++index)
        {
            SabotageBossPosition const& position =
                SabotageBossPositions[
                    indices[index]
                ];

            GameObject* targetObject =
                SpawnSabotageTargetObject(
                    anchor,
                    targetEntries[index],
                    position
                );

            if (!targetObject)
            {
                for (ObjectGuid const& guid :
                    createdGuids)
                {
                    if (guid.IsEmpty())
                        continue;

                    if (GameObject* created =
                        anchor->GetMap()->GetGameObject(
                            guid
                        ))
                    {
                        created->Delete();
                    }
                }

                TC_LOG_ERROR(
                    "custom.scourge",
                    "Failed to spawn sabotage target: match={}, instance={}, targetEntry={}, bossEntry={}, x={}, y={}, z={}",
                    match.matchId,
                    match.instanceId,
                    targetEntries[index],
                    position.creatureEntry,
                    position.x,
                    position.y,
                    position.z
                );

                return false;
            }

            createdGuids[index] =
                targetObject->GetGUID();

            match.bombTargetPositionIndices[index] =
                indices[index];

            match.bombTargetInstalled[index] =
                false;

            if (index != 0)
                targetLocationText << " / ";

            targetLocationText
                << GetSabotageTargetName(
                    targetEntries[index]
                )
                << ": "
                << position.locationName;

            TC_LOG_INFO(
                "custom.scourge",
                "Sabotage target selected: match={}, instance={}, target={}, targetEntry={}, bossEntry={}, goGuid={}, x={}, y={}, z={}, o={}",
                match.matchId,
                match.instanceId,
                GetSabotageTargetName(
                    targetEntries[index]
                ),
                targetEntries[index],
                position.creatureEntry,
                targetObject->GetGUID().GetCounter(),
                position.x,
                position.y,
                position.z,
                position.orientation
            );
        }

        match.bombTargetGuids =
            createdGuids;
        match.bombTargetsSpawned = true;

        SendSabotageMatchMessage(
            match,
            targetLocationText.str()
        );

        return true;
    }

    SabotageCompetitorSide GetSabotageCompetitorSideForTeam(
        SabotagePreparedMatch const& match,
        SabotageAssignedTeam team
    )
    {
        if (team == SABOTAGE_TEAM_A)
        {
            return match.sideOneAttacking
                ? SABOTAGE_COMPETITOR_ONE
                : SABOTAGE_COMPETITOR_TWO;
        }

        if (team == SABOTAGE_TEAM_B)
        {
            return match.sideOneAttacking
                ? SABOTAGE_COMPETITOR_TWO
                : SABOTAGE_COMPETITOR_ONE;
        }

        return SABOTAGE_COMPETITOR_NONE;
    }

    SabotageAssignedTeam GetSabotageAssignedTeamForCompetitor(
        SabotagePreparedMatch const& match,
        SabotageCompetitorSide side
    )
    {
        if (side == SABOTAGE_COMPETITOR_ONE)
        {
            return match.sideOneAttacking
                ? SABOTAGE_TEAM_A
                : SABOTAGE_TEAM_B;
        }

        if (side == SABOTAGE_COMPETITOR_TWO)
        {
            return match.sideOneAttacking
                ? SABOTAGE_TEAM_B
                : SABOTAGE_TEAM_A;
        }

        return SABOTAGE_TEAM_NONE;
    }

    uint32 GetSabotageHudLabelCode(
        SabotagePreparedMatch const& match,
        SabotageCompetitorSide side
    )
    {
        if (match.queueType ==
            SABOTAGE_QUEUE_FACTION_WAR)
        {
            return side == SABOTAGE_COMPETITOR_ONE
                ? 1
                : 2;
        }

        if (match.faction == ALLIANCE)
        {
            return side == SABOTAGE_COMPETITOR_ONE
                ? 3
                : 4;
        }

        return side == SABOTAGE_COMPETITOR_ONE
            ? 5
            : 6;
    }

    char const* GetSabotageRoundPhaseToken(
        SabotageRoundPhase phase
    )
    {
        switch (phase)
        {
            case SABOTAGE_ROUND_PHASE_WAITING:
                return "PREPARE";

            case SABOTAGE_ROUND_PHASE_ACTIVE:
                return "ROUND";

            case SABOTAGE_ROUND_PHASE_BOMB_ARMED:
                return "BOMB";

            case SABOTAGE_ROUND_PHASE_BREAK:
                return "BREAK";

            case SABOTAGE_ROUND_PHASE_FINISHED:
                return "END";

            default:
                return "PREPARE";
        }
    }

    uint32 GetSabotageRemainingSeconds(
        SabotagePreparedMatch const& match
    )
    {
        uint32 remainingMs = 0;

        switch (match.roundPhase)
        {
            case SABOTAGE_ROUND_PHASE_WAITING:
            case SABOTAGE_ROUND_PHASE_BREAK:
            case SABOTAGE_ROUND_PHASE_FINISHED:
                remainingMs = match.roundBreakTimerMs;
                break;

            case SABOTAGE_ROUND_PHASE_ACTIVE:
                remainingMs = match.roundTimerMs;
                break;

            case SABOTAGE_ROUND_PHASE_BOMB_ARMED:
                remainingMs =
                    match.bombExplosionTimerMs;
                break;

            default:
                break;
        }

        return remainingMs
            ? (remainingMs + 999) / 1000
            : 0;
    }

    void SendSabotageTargetStateToPlayer(
        Player* player,
        SabotagePreparedMatch const& match
    )
    {
        if (!player)
            return;

        if (!match.bombTargetsSpawned)
        {
            player->WhisperAddon(
                SCOURGE_MODE_ADDON_PREFIX +
                    "\tSCOURGE_MODE:SABOTAGE:TARGETS:CLEAR",
                player
            );

            return;
        }

        std::ostringstream payload;
        payload
            << "SCOURGE_MODE:SABOTAGE:TARGETS:";

        bool wroteTarget = false;

        for (uint32 index = 0;
             index < SABOTAGE_TARGET_COUNT;
             ++index)
        {
            uint32 positionIndex =
                match.bombTargetPositionIndices[index];

            if (positionIndex >=
                SabotageBossPositions.size())
            {
                continue;
            }

            SabotageBossPosition const& position =
                SabotageBossPositions[positionIndex];

            if (wroteTarget)
                payload << ";";

            wroteTarget = true;

            payload
                << (index + 1)
                << "|"
                << position.x
                << "|"
                << position.y
                << "|"
                << (match.bombTargetInstalled[index]
                    ? 1
                    : 0)
                << "|"
                << position.locationName;
        }

        if (!wroteTarget)
        {
            player->WhisperAddon(
                SCOURGE_MODE_ADDON_PREFIX +
                    "\tSCOURGE_MODE:SABOTAGE:TARGETS:CLEAR",
                player
            );

            return;
        }

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\t" + payload.str(),
            player
        );
    }

    void SendSabotagePlayerPositionToPlayer(
        Player* player,
        SabotagePreparedMatch const& match
    )
    {
        if (!player ||
            !player->IsInWorld() ||
            player->GetMapId() != MAP_STRATHOLME ||
            player->GetInstanceId() !=
                match.instanceId)
        {
            return;
        }

        std::ostringstream payload;
        payload
            << "SCOURGE_MODE:SABOTAGE:POSITION:"
            << player->GetPositionX()
            << ":"
            << player->GetPositionY()
            << ":"
            << player->GetOrientation();

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\t" + payload.str(),
            player
        );
    }

    float ClampSabotageMapCoordinate(
        float value
    )
    {
        if (value < 0.0f)
            return 0.0f;

        if (value > 1.0f)
            return 1.0f;

        return value;
    }

    bool ConvertSabotageWorldToFullMap(
        float worldX,
        float worldY,
        float& mapX,
        float& mapY
    )
    {
        /*
         * Current Stratholme.tga calibration.
         *
         * The source image is a perspective-style full instance map rather
         * than the live Minimap texture. The homography below was calibrated
         * against the verified boss rooms and both entrance positions.
         *
         * Output:
         * mapX 0.0 = left, 1.0 = right
         * mapY 0.0 = top,  1.0 = bottom
         */
        float denominator =
            0.000101552537f * worldX +
            0.000287121000f * worldY +
            1.0f;

        if (std::fabs(denominator) < 0.000001f)
            return false;

        float pixelX =
            (
                0.0756480181f * worldX -
                0.2099766800f * worldY -
                775.9604300f
            ) /
            denominator;

        float pixelY =
            (
                -0.3599036320f * worldX +
                0.1526406850f * worldY +
                2075.8287100f
            ) /
            denominator;

        mapX =
            ClampSabotageMapCoordinate(
                pixelX / 1024.0f
            );

        mapY =
            ClampSabotageMapCoordinate(
                pixelY / 1024.0f
            );

        return true;
    }

    void SendSabotageFullMapPlayerToPlayer(
        Player* player,
        SabotagePreparedMatch const& match
    )
    {
        if (!player ||
            !player->IsInWorld() ||
            player->GetMapId() != MAP_STRATHOLME ||
            player->GetInstanceId() != match.instanceId)
        {
            return;
        }

        float mapX = 0.0f;
        float mapY = 0.0f;

        if (!ConvertSabotageWorldToFullMap(
                player->GetPositionX(),
                player->GetPositionY(),
                mapX,
                mapY
            ))
        {
            return;
        }

        std::ostringstream payload;
        payload
            << "SCOURGE_MODE:SABOTAGE:MAP:PLAYER:"
            << mapX
            << ":"
            << mapY
            << ":"
            << player->GetOrientation();

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\t" + payload.str(),
            player
        );
    }

    bool CanPlayerSeeSabotageBombOnFullMap(
        Player* player,
        SabotagePreparedMatch const& match
    )
    {
        if (!player)
            return false;

        /*
         * Once the bomb is installed, both teams may see the installed
         * bomb position.
         */
        if (match.roundPhase ==
            SABOTAGE_ROUND_PHASE_BOMB_ARMED)
        {
            return true;
        }

        /*
         * Before installation, only the current attacking team may see
         * the carried or dropped bomb.
         *
         * teamA is reassigned every round by AssignSabotageRoundRoles,
         * so this follows the current attack/defence role rather than
         * the player's permanent faction.
         */
        ObjectGuid playerGuid =
            player->GetGUID();

        return std::find(
                match.teamA.begin(),
                match.teamA.end(),
                playerGuid
            ) != match.teamA.end();
    }

    bool GetSabotageBombWorldPosition(
        SabotagePreparedMatch const& match,
        float& worldX,
        float& worldY
    )
    {
        if (!match.bombCarrierGuid.IsEmpty())
        {
            Player* carrier =
                ObjectAccessor::FindConnectedPlayer(
                    match.bombCarrierGuid
                );

            if (carrier &&
                carrier->IsInWorld() &&
                carrier->GetMapId() ==
                    MAP_STRATHOLME &&
                carrier->GetInstanceId() ==
                    match.instanceId)
            {
                worldX =
                    carrier->GetPositionX();

                worldY =
                    carrier->GetPositionY();

                return true;
            }
        }

        if (!match.bombObjectGuid.IsEmpty())
        {
            Map* map =
                sMapMgr->FindMap(
                    MAP_STRATHOLME,
                    match.instanceId
                );

            if (map)
            {
                GameObject* bombObject =
                    map->GetGameObject(
                        match.bombObjectGuid
                    );

                if (bombObject &&
                    bombObject->IsInWorld())
                {
                    worldX =
                        bombObject->GetPositionX();

                    worldY =
                        bombObject->GetPositionY();

                    return true;
                }
            }
        }

        if (match.roundPhase ==
                SABOTAGE_ROUND_PHASE_BOMB_ARMED &&
            match.installedTargetIndex <
                SABOTAGE_TARGET_COUNT)
        {
            uint32 positionIndex =
                match.bombTargetPositionIndices[
                    match.installedTargetIndex
                ];

            if (positionIndex <
                SabotageBossPositions.size())
            {
                SabotageBossPosition const& position =
                    SabotageBossPositions[
                        positionIndex
                    ];

                worldX = position.x;
                worldY = position.y;
                return true;
            }
        }

        return false;
    }

    void SendSabotageFullMapBombToPlayer(
        Player* player,
        SabotagePreparedMatch const& match
    )
    {
        if (!player)
            return;

        if (!CanPlayerSeeSabotageBombOnFullMap(
                player,
                match
            ))
        {
            player->WhisperAddon(
                SCOURGE_MODE_ADDON_PREFIX +
                    "\tSCOURGE_MODE:SABOTAGE:MAP:BOMB:0:0:0:폭탄",
                player
            );

            return;
        }

        float worldX = 0.0f;
        float worldY = 0.0f;
        float mapX = 0.0f;
        float mapY = 0.0f;

        if (!GetSabotageBombWorldPosition(
                match,
                worldX,
                worldY
            ) ||
            !ConvertSabotageWorldToFullMap(
                worldX,
                worldY,
                mapX,
                mapY
            ))
        {
            player->WhisperAddon(
                SCOURGE_MODE_ADDON_PREFIX +
                    "\tSCOURGE_MODE:SABOTAGE:MAP:BOMB:0:0:0:폭탄",
                player
            );

            return;
        }

        std::ostringstream payload;
        payload
            << "SCOURGE_MODE:SABOTAGE:MAP:BOMB:1:"
            << mapX
            << ":"
            << mapY
            << ":폭탄";

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\t" + payload.str(),
            player
        );
    }

    void SendSabotageFullMapTargetsToPlayer(
        Player* player,
        SabotagePreparedMatch const& match
    )
    {
        if (!player ||
            !match.bombTargetsSpawned)
        {
            return;
        }

        std::ostringstream payload;
        payload
            << "SCOURGE_MODE:SABOTAGE:MAP:TARGETS:";

        bool wroteTarget = false;

        for (uint32 index = 0;
             index < SABOTAGE_TARGET_COUNT;
             ++index)
        {
            uint32 positionIndex =
                match.bombTargetPositionIndices[
                    index
                ];

            if (positionIndex >=
                SabotageBossPositions.size())
            {
                continue;
            }

            SabotageBossPosition const& position =
                SabotageBossPositions[
                    positionIndex
                ];

            float mapX = 0.0f;
            float mapY = 0.0f;

            if (!ConvertSabotageWorldToFullMap(
                    position.x,
                    position.y,
                    mapX,
                    mapY
                ))
            {
                continue;
            }

            if (wroteTarget)
                payload << ";";

            wroteTarget = true;

            payload
                << (index + 1)
                << "|"
                << mapX
                << "|"
                << mapY
                << "|"
                << (
                    match.bombTargetInstalled[index]
                        ? 1
                        : 0
                )
                << "|"
                << position.locationName;
        }

        if (!wroteTarget)
            return;

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\t" + payload.str(),
            player
        );
    }

    void SendSabotageFullMapRoutesToPlayer(
        Player* player,
        SabotagePreparedMatch const& match
    )
    {
        if (!player)
            return;

        if (match.routePortalDefinitions.empty())
        {
            player->WhisperAddon(
                SCOURGE_MODE_ADDON_PREFIX +
                    "\tSCOURGE_MODE:SABOTAGE:MAP:ROUTES:CLEAR",
                player
            );

            return;
        }

        std::ostringstream payload;
        payload
            << "SCOURGE_MODE:SABOTAGE:MAP:ROUTES:";

        bool wroteRoute = false;

        for (
            SabotageRoutePortalDefinition const&
                definition :
            match.routePortalDefinitions
        )
        {
            float startMapX = 0.0f;
            float startMapY = 0.0f;
            float endMapX = 0.0f;
            float endMapY = 0.0f;

            if (!ConvertSabotageWorldToFullMap(
                    definition.startX,
                    definition.startY,
                    startMapX,
                    startMapY
                ) ||
                !ConvertSabotageWorldToFullMap(
                    definition.endX,
                    definition.endY,
                    endMapX,
                    endMapY
                ))
            {
                continue;
            }

            std::ostringstream entryStream;
            entryStream
                << definition.routeId
                << "|"
                << startMapX
                << "|"
                << startMapY
                << "|"
                << endMapX
                << "|"
                << endMapY
                << "|"
                << definition.routeName;

            std::string entry =
                entryStream.str();

            std::size_t requiredSize =
                payload.str().size() +
                entry.size() +
                (wroteRoute ? 1 : 0);

            if (requiredSize > 235)
                break;

            if (wroteRoute)
                payload << ";";

            payload << entry;
            wroteRoute = true;
        }

        if (!wroteRoute)
        {
            player->WhisperAddon(
                SCOURGE_MODE_ADDON_PREFIX +
                    "\tSCOURGE_MODE:SABOTAGE:MAP:ROUTES:CLEAR",
                player
            );

            return;
        }

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\t" + payload.str(),
            player
        );
    }

    std::vector<ObjectGuid> const*
        GetSabotageFullMapAllyList(
            Player* player,
            SabotagePreparedMatch const& match
        )
    {
        if (!player)
            return nullptr;

        ObjectGuid playerGuid =
            player->GetGUID();

        if (std::find(
                match.sideOne.begin(),
                match.sideOne.end(),
                playerGuid
            ) != match.sideOne.end())
        {
            return &match.sideOne;
        }

        if (std::find(
                match.sideTwo.begin(),
                match.sideTwo.end(),
                playerGuid
            ) != match.sideTwo.end())
        {
            return &match.sideTwo;
        }

        return nullptr;
    }

    void SendSabotageFullMapAlliesToPlayer(
        Player* player,
        SabotagePreparedMatch const& match
    )
    {
        if (!player)
            return;

        std::vector<ObjectGuid> const* allyList =
            GetSabotageFullMapAllyList(
                player,
                match
            );

        if (!allyList)
            return;

        std::ostringstream payload;
        payload
            << "SCOURGE_MODE:SABOTAGE:MAP:ALLIES:";

        bool wroteAlly = false;

        for (ObjectGuid const& guid :
            *allyList)
        {
            if (guid == player->GetGUID())
                continue;

            Player* ally =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!ally ||
                !ally->IsInWorld() ||
                ally->GetMapId() !=
                    MAP_STRATHOLME ||
                ally->GetInstanceId() !=
                    match.instanceId)
            {
                continue;
            }

            float mapX = 0.0f;
            float mapY = 0.0f;

            if (!ConvertSabotageWorldToFullMap(
                    ally->GetPositionX(),
                    ally->GetPositionY(),
                    mapX,
                    mapY
                ))
            {
                continue;
            }

            std::ostringstream allyEntry;
            allyEntry
                << ally->GetName()
                << "|"
                << mapX
                << "|"
                << mapY
                << "|"
                << (ally->IsAlive() ? 1 : 0);

            std::string entry =
                allyEntry.str();

            /*
             * Keep the addon whisper below the 3.3.5 client payload limit.
             * Small matches normally fit in one packet.
             */
            if (
                payload.str().size() +
                entry.size() +
                1 >
                235
            )
            {
                break;
            }

            if (wroteAlly)
                payload << ";";

            payload << entry;
            wroteAlly = true;
        }

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\t" + payload.str(),
            player
        );
    }

    void SendSabotageFullMapStateToPlayer(
        Player* player,
        SabotagePreparedMatch const& match
    )
    {
        SendSabotageFullMapPlayerToPlayer(
            player,
            match
        );

        SendSabotageFullMapBombToPlayer(
            player,
            match
        );

        SendSabotageFullMapTargetsToPlayer(
            player,
            match
        );

        SendSabotageFullMapRoutesToPlayer(
            player,
            match
        );

        SendSabotageFullMapAlliesToPlayer(
            player,
            match
        );
    }

    void SendSabotageHudStateToPlayer(
        Player* player,
        SabotagePreparedMatch const& match
    )
    {
        if (!player)
            return;

        /*
         * This packet is the authoritative client unlock.
         *
         * The HUD function is called from every valid active-match path:
         * OnMapChanged, OnUpdateZone, periodic updates and addon GET.
         * Always send SABOTAGE:1 before HUD:1 so an old EXIT state can
         * never suppress the first HUD of a new match.
         */
        SetScourgeSabotageParticipant(
            player,
            true
        );

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SABOTAGE:1",
            player
        );

        std::ostringstream payload;
        payload
            << "SCOURGE_MODE:SABOTAGE:HUD:1:"
            << match.currentRound
            << ":"
            << SABOTAGE_TOTAL_ROUNDS
            << ":"
            << match.sideOneScore
            << ":"
            << match.sideTwoScore
            << ":"
            << GetSabotageRoundPhaseToken(
                match.roundPhase
            )
            << ":"
            << GetSabotageRemainingSeconds(
                match
            )
            << ":"
            << GetSabotageHudLabelCode(
                match,
                SABOTAGE_COMPETITOR_ONE
            )
            << ":"
            << GetSabotageHudLabelCode(
                match,
                SABOTAGE_COMPETITOR_TWO
            );

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\t" + payload.str(),
            player
        );

        SendSabotageTargetStateToPlayer(
            player,
            match
        );

        SendSabotagePlayerPositionToPlayer(
            player,
            match
        );

        SendSabotageFullMapStateToPlayer(
            player,
            match
        );
    }

    void SendSabotageHudState(
        SabotagePreparedMatch const& match
    )
    {
        for (ObjectGuid const& guid : match.sideOne)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                SendSabotageHudStateToPlayer(
                    player,
                    match
                );
            }
        }

        for (ObjectGuid const& guid : match.sideTwo)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                SendSabotageHudStateToPlayer(
                    player,
                    match
                );
            }
        }
    }

    void HideSabotageHud(Player* player)
    {
        if (!player)
            return;

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SABOTAGE:HUD:0:0:3:0:0:END:0:0:0",
            player
        );

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SABOTAGE:TARGETS:CLEAR",
            player
        );

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SABOTAGE:MAP:CLEAR",
            player
        );
    }

    std::vector<ObjectGuid> GetSabotageAllPlayers(
        SabotagePreparedMatch const& match
    )
    {
        std::vector<ObjectGuid> players;
        players.reserve(
            match.sideOne.size() +
            match.sideTwo.size()
        );

        players.insert(
            players.end(),
            match.sideOne.begin(),
            match.sideOne.end()
        );

        players.insert(
            players.end(),
            match.sideTwo.begin(),
            match.sideTwo.end()
        );

        return players;
    }

    void ClearSabotageSmokeGrenadeItems(
        SabotagePreparedMatch const& match
    )
    {
        for (ObjectGuid const& guid :
            GetSabotageAllPlayers(
                match
            ))
        {
            Player* player =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!player)
                continue;

            uint32 itemCount =
                player->GetItemCount(
                    SABOTAGE_SMOKE_GRENADE_ITEM,
                    true
                );

            if (itemCount)
            {
                player->DestroyItemCount(
                    SABOTAGE_SMOKE_GRENADE_ITEM,
                    itemCount,
                    true
                );
            }
        }
    }

    void GiveSabotageSmokeGrenadeItems(
        SabotagePreparedMatch& match
    )
    {
        ClearSabotageSmokeGrenadeItems(
            match
        );

        for (ObjectGuid const& guid :
            match.teamA)
        {
            Player* attacker =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!attacker)
                continue;

            if (!attacker->AddItem(
                    SABOTAGE_SMOKE_GRENADE_ITEM,
                    1
                ))
            {
                SendSabotageMessage(
                    attacker,
                    "가방 공간이 부족하여 연막탄을 받지 못했습니다."
                );

                continue;
            }

            SendSabotageMessage(
                attacker,
                "공격팀 연막탄을 받았습니다. "
                "A팀 전체에서 이번 라운드에 한 번만 사용할 수 있습니다."
            );
        }
    }

    void DeleteSabotageSmokeObject(
        SabotagePreparedMatch& match
    )
    {
        if (match.smokeObjectGuid.IsEmpty())
        {
            match.smokeObjectRemainingMs = 0;
            return;
        }

        Map* map =
            sMapMgr->FindMap(
                MAP_STRATHOLME,
                match.instanceId
            );

        if (map)
        {
            if (GameObject* smokeObject =
                map->GetGameObject(
                    match.smokeObjectGuid
                ))
            {
                smokeObject->Delete();
            }
        }

        match.smokeObjectGuid.Clear();
        match.smokeObjectRemainingMs = 0;
    }

    void ClearSabotageSmokeAreaTargets(
        GameObject* smokeObject
    )
    {
        if (!smokeObject ||
            !smokeObject->IsInWorld())
        {
            return;
        }

        Map* map =
            smokeObject->GetMap();

        if (!map)
            return;

        Map::PlayerList const& players =
            map->GetPlayers();

        if (players.isEmpty())
            return;

        for (Map::PlayerList::const_iterator itr =
                 players.begin();
             itr != players.end();
             ++itr)
        {
            Player* player =
                itr->GetSource();

            if (!player ||
                !player->IsInWorld() ||
                !player->IsWithinDistInMap(
                    smokeObject,
                    SABOTAGE_SMOKE_TARGET_CLEAR_RADIUS
                ))
            {
                continue;
            }

            ObjectGuid selectedTarget =
                player->GetTarget();

            if (selectedTarget.IsEmpty())
                continue;

            /*
             * SetSelection only changes UNIT_FIELD_TARGET on the server.
             * The 3.3.5 client keeps its visible target frame until it
             * receives SMSG_CLEAR_TARGET containing the selected GUID.
             */
            player->SetSelection(
                ObjectGuid::Empty
            );

            WorldPacket clearTargetPacket(
                SMSG_CLEAR_TARGET,
                8
            );

            clearTargetPacket
                << selectedTarget;

            player->SendDirectMessage(
                &clearTargetPacket
            );
        }
    }

    GameObject* SpawnSabotageSmokeObject(
        Player* player,
        SabotagePreparedMatch& match
    )
    {
        if (!player ||
            !player->IsInWorld() ||
            player->GetMapId() != MAP_STRATHOLME ||
            player->GetInstanceId() != match.instanceId)
        {
            return nullptr;
        }

        Map* map =
            player->GetMap();

        if (!map)
            return nullptr;

        DeleteSabotageSmokeObject(
            match
        );

        Position position(
            player->GetPositionX(),
            player->GetPositionY(),
            player->GetPositionZ() + 0.05f,
            player->GetOrientation()
        );

        QuaternionData rotation =
            QuaternionData::fromEulerAnglesZYX(
                player->GetOrientation(),
                0.0f,
                0.0f
            );

        GameObject* smokeObject =
            new GameObject();

        if (!smokeObject->Create(
            map->GenerateLowGuid<
                HighGuid::GameObject
            >(),
            SABOTAGE_SMOKE_GAMEOBJECT,
            map,
            player->GetPhaseMask(),
            position,
            rotation,
            255,
            GO_STATE_READY
        ))
        {
            delete smokeObject;
            return nullptr;
        }

        smokeObject->SetSpawnedByDefault(
            false
        );

        smokeObject->SetRespawnTime(
            DAY
        );

        if (!map->AddToMap(
                smokeObject
            ))
        {
            delete smokeObject;
            return nullptr;
        }

        match.smokeObjectGuid =
            smokeObject->GetGUID();

        match.smokeObjectRemainingMs =
            SABOTAGE_SMOKE_GAMEOBJECT_DURATION_MS;

        ClearSabotageSmokeAreaTargets(
            smokeObject
        );

        return smokeObject;
    }

    void UpdateSabotageSmokeObject(
        SabotagePreparedMatch& match,
        uint32 diff
    )
    {
        if (match.smokeObjectGuid.IsEmpty())
            return;

        if (match.smokeObjectRemainingMs > diff)
        {
            match.smokeObjectRemainingMs -= diff;
            return;
        }

        DeleteSabotageSmokeObject(
            match
        );
    }

    void RefreshSabotageBombCarrierFlag(
        SabotagePreparedMatch const& match
    )
    {
        if (match.bombCarrierGuid.IsEmpty())
            return;

        Player* carrier =
            ObjectAccessor::FindConnectedPlayer(
                match.bombCarrierGuid
            );

        if (!carrier ||
            !carrier->IsAlive() ||
            !IsScourgeSabotageParticipant(carrier) ||
            carrier->GetMapId() != MAP_STRATHOLME ||
            carrier->GetInstanceId() != match.instanceId ||
            !carrier->HasItemCount(
                SABOTAGE_BOMB_ITEM,
                1,
                true
            ))
        {
            return;
        }

        uint32 expectedFlagSpell =
            carrier->GetTeam() == ALLIANCE
                ? SABOTAGE_WARSONG_FLAG_SPELL
                : SABOTAGE_SILVERWING_FLAG_SPELL;

        if (!carrier->HasAura(expectedFlagSpell))
        {
            DropSabotageBombAtCarrierPosition(
                carrier,
                true
            );
        }

    }

    void DeleteSabotageBombObject(
        SabotagePreparedMatch& match
    )
    {
        if (!match.bombObjectGuid.IsEmpty())
        {
            Map* map =
                sMapMgr->FindMap(
                    MAP_STRATHOLME,
                    match.instanceId
                );

            if (map)
            {
                if (GameObject* bombObject =
                    map->GetGameObject(
                        match.bombObjectGuid
                    ))
                {
                    bombObject->Delete();
                }
            }
        }

        match.bombObjectGuid.Clear();
    }

    void ClearSabotageRoundBombItems(
        SabotagePreparedMatch const& match
    )
    {
        for (ObjectGuid const& guid :
            GetSabotageAllPlayers(match))
        {
            ClearSabotagePendingBombAction(guid);

            Player* player =
                ObjectAccessor::FindConnectedPlayer(guid);

            if (!player)
                continue;

            RemoveSabotageBombCarrierFlag(
                player
            );

            player->InterruptNonMeleeSpells(true);

            uint32 bombCount =
                player->GetItemCount(
                    SABOTAGE_BOMB_ITEM,
                    true
                );

            if (bombCount)
            {
                player->DestroyItemCount(
                    SABOTAGE_BOMB_ITEM,
                    bombCount,
                    true
                );
            }
        }
    }

    void ResetSabotageTargetStates(
        SabotagePreparedMatch& match
    )
    {
        Map* map =
            sMapMgr->FindMap(
                MAP_STRATHOLME,
                match.instanceId
            );

        for (uint32 index = 0;
             index < SABOTAGE_TARGET_COUNT;
             ++index)
        {
            match.bombTargetInstalled[index] =
                false;

            if (!map ||
                match.bombTargetGuids[index].IsEmpty())
            {
                continue;
            }

            if (GameObject* targetObject =
                map->GetGameObject(
                    match.bombTargetGuids[index]
                ))
            {
                targetObject->SetGoState(
                    GO_STATE_READY
                );
            }
        }

        match.installedTargetIndex =
            SABOTAGE_TARGET_COUNT;
    }

    Player* FindSabotageRoundAnchor(
        SabotagePreparedMatch const& match
    )
    {
        for (ObjectGuid const& guid : match.teamA)
        {
            Player* player =
                ObjectAccessor::FindConnectedPlayer(guid);

            if (player &&
                player->IsInWorld() &&
                player->GetMapId() ==
                    MAP_STRATHOLME &&
                player->GetInstanceId() ==
                    match.instanceId)
            {
                return player;
            }
        }

        for (ObjectGuid const& guid : match.teamB)
        {
            Player* player =
                ObjectAccessor::FindConnectedPlayer(guid);

            if (player &&
                player->IsInWorld() &&
                player->GetMapId() ==
                    MAP_STRATHOLME &&
                player->GetInstanceId() ==
                    match.instanceId)
            {
                return player;
            }
        }

        return nullptr;
    }

    Player* FindSabotageRoundBombAnchor(
        SabotagePreparedMatch const& match
    )
    {
        SabotageStartPosition const& start =
            GetSabotageStartPosition(
                match,
                SABOTAGE_TEAM_A
            );

        static constexpr float
            SABOTAGE_BOMB_ANCHOR_MAX_DISTANCE_SQ =
                40.0f * 40.0f;

        for (ObjectGuid const& guid : match.teamA)
        {
            Player* player =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!player ||
                !player->IsInWorld() ||
                player->GetMapId() !=
                    MAP_STRATHOLME ||
                player->GetInstanceId() !=
                    match.instanceId)
            {
                continue;
            }

            float const deltaX =
                player->GetPositionX() -
                start.x;

            float const deltaY =
                player->GetPositionY() -
                start.y;

            float const distanceSq =
                deltaX * deltaX +
                deltaY * deltaY;

            if (distanceSq >
                SABOTAGE_BOMB_ANCHOR_MAX_DISTANCE_SQ)
            {
                continue;
            }

            return player;
        }

        return nullptr;
    }

    void UpdateSabotagePlayerRoundRole(
        ObjectGuid guid,
        SabotagePreparedMatch const& match,
        SabotageCompetitorSide side
    )
    {
        auto stateItr =
            SabotageQueueStates.find(
                uint32(guid.GetCounter())
            );

        if (stateItr ==
            SabotageQueueStates.end())
        {
            return;
        }

        stateItr->second.competitorSide = side;
        stateItr->second.assignedTeam =
            GetSabotageAssignedTeamForCompetitor(
                match,
                side
            );
    }

    void AssignSabotageRoundRoles(
        SabotagePreparedMatch& match,
        bool randomize
    )
    {
        if (randomize)
        {
            match.sideOneAttacking =
                urand(0, 1) != 0;
        }

        if (match.sideOneAttacking)
        {
            match.teamA = match.sideOne;
            match.teamB = match.sideTwo;
            match.teamAGroupGuid =
                match.sideOneGroupGuid;
            match.teamBGroupGuid =
                match.sideTwoGroupGuid;
        }
        else
        {
            match.teamA = match.sideTwo;
            match.teamB = match.sideOne;
            match.teamAGroupGuid =
                match.sideTwoGroupGuid;
            match.teamBGroupGuid =
                match.sideOneGroupGuid;
        }

        for (ObjectGuid const& guid : match.sideOne)
        {
            UpdateSabotagePlayerRoundRole(
                guid,
                match,
                SABOTAGE_COMPETITOR_ONE
            );
        }

        for (ObjectGuid const& guid : match.sideTwo)
        {
            UpdateSabotagePlayerRoundRole(
                guid,
                match,
                SABOTAGE_COMPETITOR_TWO
            );
        }
    }

    bool ForceReviveSabotagePlayer(
        Player* player,
        char const* reason
    )
    {
        if (!player)
            return false;

        bool requiresRevive =
            !player->IsAlive() ||
            player->HasFlag(
                PLAYER_FLAGS,
                PLAYER_FLAGS_GHOST
            );

        if (!requiresRevive)
        {
            player->SetHealth(
                player->GetMaxHealth()
            );

            return true;
        }

        /*
         * The core resurrection restrictions use the Player-side sabotage
         * participant flag. That flag intentionally blocks every normal
         * resurrection while a round is active.
         *
         * The script-local GUID participant cache remains active, so only
         * the synchronous core resurrection call is bypassed here. Restore
         * the Player flag immediately after the GM-command style revive.
         */
        player->SetScourgeSabotageParticipant(
            false
        );

        player->ClearResurrectRequestData();

        player->ResurrectPlayer(
            1.0f
        );

        player->SpawnCorpseBones();

        player->SetScourgeSabotageParticipant(
            true
        );

        bool revived =
            player->IsAlive() &&
            !player->HasFlag(
                PLAYER_FLAGS,
                PLAYER_FLAGS_GHOST
            );

        if (revived)
        {
            player->SetHealth(
                player->GetMaxHealth()
            );

            player->SaveToDB();
        }

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage forced revive result: player={}, guid={}, alive={}, ghost={}, deathState={}, reason={}",
            player->GetName(),
            player->GetGUID().GetCounter(),
            player->IsAlive() ? 1 : 0,
            player->HasFlag(
                PLAYER_FLAGS,
                PLAYER_FLAGS_GHOST
            ) ? 1 : 0,
            uint32(player->getDeathState()),
            reason ? reason : ""
        );

        return revived;
    }

    void ReviveAndFreezeSabotageRoundPlayer(
        Player* player
    )
    {
        if (!player)
            return;

        ClearSabotagePendingBombAction(
            player->GetGUID()
        );

        RemoveSabotageBombCarrierFlag(
            player
        );

        DisableSabotageDefenderScout(
            player,
            false
        );

        RestoreSabotageTemporaryMovementSpeed(
            player
        );

        player->CombatStopWithPets();
        player->AttackStop();
        player->InterruptNonMeleeSpells(true);
        player->SetSelection(
            ObjectGuid::Empty
        );

        /*
         * Revive at the exact round-end position using the same online
         * sequence as the GM revive command.
         */
        ForceReviveSabotagePlayer(
            player,
            "round end"
        );

        /*
         * Spectator mode expands the ghost detection mask so a ghost can
         * see living competitors. Restore the normal living-player mask
         * immediately after the round-end resurrection.
         */
        player->m_serverSideVisibilityDetect.SetValue(
            SERVERSIDE_VISIBILITY_GHOST,
            GHOST_VISIBILITY_ALIVE
        );

        SetScourgeSabotageParticipant(
            player,
            true
        );

        player->SetPhaseMask(
            SCOURGE_PHASE_MASK,
            true
        );

        player->m_InstanceValid = true;
        player->UpdateHomebindTime(0);

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SPECTATOR:0",
            player
        );

        player->UpdateObjectVisibility(
            true
        );

        player->StopMoving();
        player->SetControlled(
            true,
            UNIT_STATE_ROOT
        );
    }

    void ReviveAndFreezeSabotageRoundPlayers(
        SabotagePreparedMatch const& match
    )
    {
        std::vector<ObjectGuid> players =
            GetSabotageAllPlayers(match);

        for (ObjectGuid const& guid : players)
        {
            ReviveAndFreezeSabotageRoundPlayer(
                ObjectAccessor::FindConnectedPlayer(
                    guid
                )
            );
        }
    }

    void EnsureSabotageRoundPlayersRevived(
        SabotagePreparedMatch const& match
    )
    {
        std::vector<ObjectGuid> players =
            GetSabotageAllPlayers(match);

        for (ObjectGuid const& guid : players)
        {
            Player* player =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!player)
                continue;

            /*
             * EndSabotageRound can run in the same update window in which
             * TrinityCore is still completing KillPlayer or spirit release.
             * The first revive can therefore be overwritten by a delayed
             * death callback. Reapply only when the player is dead or still
             * carries the ghost flag.
             */
            if (!player->IsAlive() ||
                player->HasFlag(
                    PLAYER_FLAGS,
                    PLAYER_FLAGS_GHOST
                ))
            {
                ReviveAndFreezeSabotageRoundPlayer(
                    player
                );
            }
        }
    }

    void PrepareSabotageRoundPlayer(
        Player* player,
        SabotagePreparedMatch const& match,
        SabotageAssignedTeam team
    )
    {
        if (!player)
            return;

        ClearSabotagePendingBombAction(
            player->GetGUID()
        );

        RemoveSabotageBombCarrierFlag(
            player
        );

        DisableSabotageDefenderScout(
            player,
            false
        );

        RestoreSabotageTemporaryMovementSpeed(
            player
        );

        player->DestroyItemCount(
            SABOTAGE_BOMB_ITEM,
            player->GetItemCount(
                SABOTAGE_BOMB_ITEM,
                true
            ),
            true
        );

        player->DestroyItemCount(
            SABOTAGE_SMOKE_GRENADE_ITEM,
            player->GetItemCount(
                SABOTAGE_SMOKE_GRENADE_ITEM,
                true
            ),
            true
        );

        player->RemoveAurasDueToSpell(
            SABOTAGE_MOUNT_SPELL
        );

        player->SetControlled(
            false,
            UNIT_STATE_ROOT
        );

        player->CombatStopWithPets();

        ForceReviveSabotagePlayer(
            player,
            "next round preparation"
        );

        SetScourgeSabotageParticipant(
            player,
            true
        );

        player->SetPhaseMask(
            SCOURGE_PHASE_MASK,
            true
        );

        player->m_InstanceValid = true;
        player->UpdateHomebindTime(0);

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SPECTATOR:0",
            player
        );

        SabotageStartPosition const& start =
            GetSabotageStartPosition(
                match,
                team
            );

        float x = start.x;
        float y = start.y;
        float z = start.z;
        float orientation =
            start.orientation;

        player->TeleportTo(
            MAP_STRATHOLME,
            x,
            y,
            z,
            orientation
        );

        GiveSabotageMountItem(player);

        RefreshSabotageFreeForAll(
            player
        );
    }

    bool SpawnSabotageRoundBomb(
        SabotagePreparedMatch& match
    )
    {
        if (!match.bombObjectGuid.IsEmpty() ||
            !match.bombCarrierGuid.IsEmpty() ||
            match.installedTargetIndex <
                SABOTAGE_TARGET_COUNT)
        {
            return true;
        }

        Player* anchor =
            FindSabotageRoundBombAnchor(match);

        if (!anchor)
        {
            return false;
        }

        SabotageStartPosition const& start =
            GetSabotageStartPosition(
                match,
                SABOTAGE_TEAM_A
            );

        static constexpr float
            SABOTAGE_BOMB_PLAYER_Z_OFFSET =
                0.50f;

        float const bombX =
            anchor->GetPositionX();

        float const bombY =
            anchor->GetPositionY();

        float const bombZ =
            anchor->GetPositionZ() +
            SABOTAGE_BOMB_PLAYER_Z_OFFSET;

        float const bombOrientation =
            anchor->GetOrientation();

        GameObject* bombObject =
            SpawnSabotageBombObject(
                anchor,
                bombX,
                bombY,
                bombZ,
                bombOrientation
            );

        if (!bombObject)
            return false;

        match.bombObjectGuid =
            bombObject->GetGUID();

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage round bomb object created: match={}, round={}, teamAStart={}({}), playerPosition=({}, {}, {}), bombPosition=({}, {}, {}, {}), guid={}",
            match.matchId,
            match.currentRound,
            match.teamAStartPositionIndex,
            start.locationName,
            anchor->GetPositionX(),
            anchor->GetPositionY(),
            anchor->GetPositionZ(),
            bombX,
            bombY,
            bombZ,
            bombOrientation,
            match.bombObjectGuid.GetCounter()
        );

        return true;
    }

    void StartSabotageRound(
        SabotagePreparedMatch& match
    )
    {
        if (match.currentRound >=
            SABOTAGE_TOTAL_ROUNDS)
        {
            return;
        }

        ++match.currentRound;

        AssignSabotageRoundRoles(
            match,
            match.currentRound > 1
        );

        if (match.currentRound > 1)
        {
            SelectSabotageRoundStartPositions(
                match,
                true
            );
        }

        DeleteSabotageBombObject(match);
        DeleteSabotageDefenderScoutObject(
            match
        );
        ClearSabotageDefenderScoutStates(
            match
        );
        ClearSabotageSmokeGrenadeItems(
            match
        );
        ClearSabotageRoundBombItems(match);
        ResetSabotageTargetStates(match);

        match.bombCarrierGuid.Clear();
        match.smokeUsedThisRound = false;
        DeleteSabotageSmokeObject(
            match
        );
        match.roundPhase =
            SABOTAGE_ROUND_PHASE_ACTIVE;
        match.roundTimerMs =
            SABOTAGE_ROUND_LIMIT_MS;
        match.bombExplosionTimerMs = 0;
        match.roundBreakTimerMs = 0;
        match.hudUpdateTimerMs = 0;

        for (ObjectGuid const& guid : match.teamA)
        {
            PrepareSabotageRoundPlayer(
                ObjectAccessor::FindConnectedPlayer(guid),
                match,
                SABOTAGE_TEAM_A
            );
        }

        for (ObjectGuid const& guid : match.teamB)
        {
            PrepareSabotageRoundPlayer(
                ObjectAccessor::FindConnectedPlayer(guid),
                match,
                SABOTAGE_TEAM_B
            );
        }

        if (!EnsureSabotageRoutePortalObjectsSpawned(
                match
            ))
        {
            SendSabotageMatchMessage(
                match,
                "추가 이동 경로 생성에 실패했습니다. 기존 경로로 경기를 계속합니다."
            );
        }

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage round bomb spawn scheduled after teleport: match={}, round={}, teamAStart={}({})",
            match.matchId,
            match.currentRound,
            match.teamAStartPositionIndex,
            GetSabotageStartPosition(
                match,
                SABOTAGE_TEAM_A
            ).locationName
        );

        GiveSabotageSmokeGrenadeItems(
            match
        );

        if (!SpawnSabotageDefenderScoutObject(
                match
            ))
        {
            SendSabotageMatchMessage(
                match,
                "방어팀 정찰 장치 생성에 실패했습니다."
            );
        }
        else
        {
            for (ObjectGuid const& guid : match.teamB)
            {
                if (Player* defender =
                    ObjectAccessor::FindConnectedPlayer(
                        guid
                    ))
                {
                    SendSabotageMessage(
                        defender,
                        "B팀 시작 지점에 정찰 장치가 생성되었습니다. "
                        "클릭하면 투명 상태와 이동속도 2배를 전환합니다."
                    );
                }
            }
        }

        std::ostringstream text;
        text
            << match.currentRound
            << "라운드가 시작되었습니다. "
            << "A팀은 10분 안에 폭발물을 설치해야 합니다. "
            << "설치 완료 후 1분이 지나면 폭발합니다.";

        SendSabotageMatchMessage(
            match,
            text.str()
        );

        for (ObjectGuid const& guid : match.teamA)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                SendSabotageMessage(
                    player,
                    "이번 라운드는 공격팀 A입니다."
                );
            }
        }

        for (ObjectGuid const& guid : match.teamB)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                SendSabotageMessage(
                    player,
                    "이번 라운드는 방어팀 B입니다."
                );
            }
        }

        SendSabotageHudState(match);
    }

    bool IsSabotageTeamEliminated(
        SabotagePreparedMatch const& match,
        std::vector<ObjectGuid> const& teamPlayers
    )
    {
        if (teamPlayers.empty())
            return true;

        bool foundConnectedParticipant = false;

        for (ObjectGuid const& guid : teamPlayers)
        {
            Player* player =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!player)
                continue;

            /*
             * A dead player can be moved by the core during release or
             * ghost processing. The player still belongs to this round
             * and must count as a dead participant even when the current
             * map or instance no longer matches.
             */
            foundConnectedParticipant = true;

            if (player->IsAlive() &&
                player->GetMapId() ==
                    MAP_STRATHOLME &&
                player->GetInstanceId() ==
                    match.instanceId)
            {
                return false;
            }
        }

        return foundConnectedParticipant;
    }

    bool IsSabotageAttackingTeamEliminated(
        SabotagePreparedMatch const& match
    )
    {
        return IsSabotageTeamEliminated(
            match,
            match.teamA
        );
    }

    bool IsSabotageDefendingTeamEliminated(
        SabotagePreparedMatch const& match
    )
    {
        return IsSabotageTeamEliminated(
            match,
            match.teamB
        );
    }

    void EndSabotageRound(
        SabotagePreparedMatch& match,
        SabotageAssignedTeam winnerTeam,
        std::string const& reason
    )
    {
        if (match.roundPhase !=
                SABOTAGE_ROUND_PHASE_ACTIVE &&
            match.roundPhase !=
                SABOTAGE_ROUND_PHASE_BOMB_ARMED)
        {
            return;
        }

        /*
         * Custom Update - 2026-08-02
         *
         * The first round-end action is to revive every participant and
         * hold each character at the exact position where the round ended.
         */
        DeleteSabotageDefenderScoutObject(
            match
        );

        ClearSabotageDefenderScoutStates(
            match
        );

        ClearSabotageSmokeGrenadeItems(
            match
        );

        DeleteSabotageSmokeObject(
            match
        );

        ReviveAndFreezeSabotageRoundPlayers(
            match
        );

        SabotageCompetitorSide winnerSide =
            GetSabotageCompetitorSideForTeam(
                match,
                winnerTeam
            );

        if (winnerSide ==
            SABOTAGE_COMPETITOR_ONE)
        {
            ++match.sideOneScore;
        }
        else if (winnerSide ==
            SABOTAGE_COMPETITOR_TWO)
        {
            ++match.sideTwoScore;
        }
        else
        {
            return;
        }

        DeleteSabotageBombObject(match);
        ClearSabotageRoundBombItems(match);
        ResetSabotageTargetStates(match);
        match.bombCarrierGuid.Clear();
        match.roundTimerMs = 0;
        match.bombExplosionTimerMs = 0;
        match.roundBreakTimerMs =
            SABOTAGE_ROUND_BREAK_MS;
        match.hudUpdateTimerMs = 0;

        bool matchFinished =
            match.sideOneScore >=
                SABOTAGE_ROUNDS_TO_WIN ||
            match.sideTwoScore >=
                SABOTAGE_ROUNDS_TO_WIN ||
            match.currentRound >=
                SABOTAGE_TOTAL_ROUNDS;

        if (matchFinished)
        {
            match.roundPhase =
                SABOTAGE_ROUND_PHASE_FINISHED;
            match.matchWinnerSide = winnerSide;
        }
        else
        {
            match.roundPhase =
                SABOTAGE_ROUND_PHASE_BREAK;
        }

        std::ostringstream text;
        text
            << match.currentRound
            << "라운드 "
            << GetSabotageTeamName(winnerTeam)
            << " 승리: "
            << reason
            << " 현재 점수 "
            << match.sideOneScore
            << " : "
            << match.sideTwoScore
            << ".";

        SendSabotageMatchMessage(
            match,
            text.str()
        );

        if (matchFinished)
        {
            SendSabotageMatchMessage(
                match,
                "2승을 먼저 달성한 팀이 최종 우승했습니다. 10초 후 경기가 종료됩니다."
            );
        }
        else
        {
            SendSabotageMatchMessage(
                match,
                "10초 후 다음 라운드의 공격팀과 방어팀을 무작위로 다시 선정합니다."
            );
        }

        SendSabotageHudState(match);
    }

    SabotageCompetitorSide
        GetSabotageCompetitorSideForPlayer(
            SabotagePreparedMatch const& match,
            ObjectGuid playerGuid
        )
    {
        if (std::find(
                match.sideOne.begin(),
                match.sideOne.end(),
                playerGuid
            ) != match.sideOne.end())
        {
            return SABOTAGE_COMPETITOR_ONE;
        }

        if (std::find(
                match.sideTwo.begin(),
                match.sideTwo.end(),
                playerGuid
            ) != match.sideTwo.end())
        {
            return SABOTAGE_COMPETITOR_TWO;
        }

        return SABOTAGE_COMPETITOR_NONE;
    }

    bool GiveSabotageRewardItem(
        Player* player,
        uint32 itemEntry,
        uint32 itemCount,
        char const* itemName
    )
    {
        if (!player ||
            !itemEntry ||
            !itemCount)
        {
            return false;
        }

        if (player->AddItem(
                itemEntry,
                itemCount
            ))
        {
            return true;
        }

        SendSabotageMessage(
            player,
            Trinity::StringFormat(
                "가방 공간이 부족하여 {} {}개를 지급하지 못했습니다.",
                itemName,
                itemCount
            )
        );

        TC_LOG_ERROR(
            "custom.scourge",
            "Sabotage reward item failed: player={}, guid={}, account={}, item={}, count={}",
            player->GetName(),
            player->GetGUID().GetCounter(),
            player->GetSession()
                ? player->GetSession()->GetAccountId()
                : 0,
            itemEntry,
            itemCount
        );

        return false;
    }

    bool HasSabotageExecutionFragmentRewardToday(
        uint32 accountId
    )
    {
        if (!accountId)
            return true;

        QueryResult result =
            CharacterDatabase.PQuery(
                "SELECT 1 "
                "FROM `custom_scourge_sabotage_account_reward` "
                "WHERE `account_id` = {} "
                "AND `reward_date` = CURRENT_DATE "
                "LIMIT 1",
                accountId
            );

        return result != nullptr;
    }

    void RecordSabotageExecutionFragmentReward(
        Player* player,
        SabotagePreparedMatch const& match,
        uint32 itemEntry
    )
    {
        if (!player ||
            !player->GetSession())
        {
            return;
        }

        CharacterDatabase.PExecute(
            "INSERT IGNORE INTO "
            "`custom_scourge_sabotage_account_reward` "
            "(`account_id`, `reward_date`, `character_guid`, "
            "`match_id`, `item_entry`, `rewarded_at`) "
            "VALUES ({}, CURRENT_DATE, {}, {}, {}, CURRENT_TIMESTAMP)",
            player->GetSession()->GetAccountId(),
            player->GetGUID().GetCounter(),
            match.matchId,
            itemEntry
        );
    }

    uint32 SelectSabotageExecutionFragmentReward()
    {
        return
            SABOTAGE_REWARD_EXECUTION_FRAGMENT_ITEMS[
                urand(
                    0,
                    uint32(
                        SABOTAGE_REWARD_EXECUTION_FRAGMENT_ITEMS.size() - 1
                    )
                )
            ];
    }

    void GrantSabotageMatchRewardToPlayer(
        Player* player,
        SabotagePreparedMatch const& match
    )
    {
        if (!player ||
            !player->GetSession() ||
            match.matchWinnerSide ==
                SABOTAGE_COMPETITOR_NONE)
        {
            return;
        }

        SabotageCompetitorSide playerSide =
            GetSabotageCompetitorSideForPlayer(
                match,
                player->GetGUID()
            );

        if (playerSide ==
            SABOTAGE_COMPETITOR_NONE)
        {
            return;
        }

        bool winner =
            playerSide ==
            match.matchWinnerSide;

        uint32 baseRewardCount =
            winner
                ? SABOTAGE_REWARD_WIN_ITEM_COUNT
                : SABOTAGE_REWARD_LOSS_ITEM_COUNT;

        bool invasionRewarded =
            GiveSabotageRewardItem(
                player,
                SABOTAGE_REWARD_INVASION_FRAGMENT_ITEM,
                baseRewardCount,
                "침공 파편"
            );

        bool deathRuneRewarded =
            GiveSabotageRewardItem(
                player,
                SABOTAGE_REWARD_DEATH_RUNE_ITEM,
                baseRewardCount,
                "죽음의 룬"
            );

        if (invasionRewarded ||
            deathRuneRewarded)
        {
            SendSabotageMessage(
                player,
                Trinity::StringFormat(
                    "폭파전 {} 보상: 침공 파편 {}개, 죽음의 룬 {}개 지급을 처리했습니다.",
                    winner
                        ? "승리"
                        : "패배",
                    baseRewardCount,
                    baseRewardCount
                )
            );
        }

        if (!winner)
            return;

        uint32 accountId =
            player->GetSession()->GetAccountId();

        if (HasSabotageExecutionFragmentRewardToday(
                accountId
            ))
        {
            SendSabotageMessage(
                player,
                "오늘의 처형의 파편 계정 1회 승리 보상은 이미 지급되었습니다."
            );

            return;
        }

        uint32 executionFragmentEntry =
            SelectSabotageExecutionFragmentReward();

        if (!GiveSabotageRewardItem(
                player,
                executionFragmentEntry,
                1,
                "처형의 파편"
            ))
        {
            return;
        }

        RecordSabotageExecutionFragmentReward(
            player,
            match,
            executionFragmentEntry
        );

        SendSabotageMessage(
            player,
            Trinity::StringFormat(
                "오늘 계정 첫 승리 보상으로 처형의 파편 1개가 지급되었습니다. 아이템 ID: {}",
                executionFragmentEntry
            )
        );

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage daily execution fragment rewarded: match={}, player={}, guid={}, account={}, item={}",
            match.matchId,
            player->GetName(),
            player->GetGUID().GetCounter(),
            accountId,
            executionFragmentEntry
        );
    }

    void GrantSabotageMatchRewards(
        SabotagePreparedMatch const& match
    )
    {
        std::vector<ObjectGuid> players =
            GetSabotageAllPlayers(
                match
            );

        for (ObjectGuid const& guid : players)
        {
            Player* player =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!player)
                continue;

            GrantSabotageMatchRewardToPlayer(
                player,
                match
            );
        }
    }

    void ReviveSabotagePlayerBeforeMatchReturn(
        Player* player
    )
    {
        if (!player)
            return;

        ClearSabotagePendingBombAction(
            player->GetGUID()
        );

        player->CombatStopWithPets();
        player->AttackStop();
        player->InterruptNonMeleeSpells(
            true
        );
        player->SetSelection(
            ObjectGuid::Empty
        );

        player->SetControlled(
            false,
            UNIT_STATE_ROOT
        );

        /*
         * Repeat the GM-command style revive immediately before restoring
         * the queue position. This also handles a late death during the
         * final ten-second countdown.
         */
        ForceReviveSabotagePlayer(
            player,
            "match return"
        );

        player->m_serverSideVisibilityDetect.SetValue(
            SERVERSIDE_VISIBILITY_GHOST,
            GHOST_VISIBILITY_ALIVE
        );

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SPECTATOR:0",
            player
        );

        player->UpdateObjectVisibility(
            true
        );
    }

    void FinishSabotageMatch(uint32 matchId)
    {
        auto matchItr =
            SabotagePreparedMatches.find(matchId);

        if (matchItr ==
            SabotagePreparedMatches.end())
        {
            return;
        }

        SabotagePreparedMatch match =
            matchItr->second;

        GrantSabotageMatchRewards(
            match
        );

        ClearActiveSabotageMatch(matchId);
        DeleteSabotageBombObject(
            matchItr->second
        );
        DeleteSabotageDefenderScoutObject(
            matchItr->second
        );
        ClearSabotageDefenderScoutStates(
            matchItr->second
        );
        ClearSabotageSmokeGrenadeItems(
            matchItr->second
        );
        DeleteSabotageSmokeObject(
            matchItr->second
        );
        DeleteSabotageRoutePortalObjects(
            matchItr->second
        );
        DespawnSabotageBombTargets(
            matchItr->second
        );

        std::vector<ObjectGuid> players =
            GetSabotageAllPlayers(match);

        SabotagePreparedMatches.erase(
            matchItr
        );

        for (ObjectGuid const& guid : players)
        {
            uint32 guidLow =
                uint32(
                    guid.GetCounter()
                );

            SabotagePlayerQueueState returnState;

            auto stateItr =
                SabotageQueueStates.find(
                    guidLow
                );

            if (stateItr !=
                SabotageQueueStates.end())
            {
                returnState =
                    stateItr->second;

                SabotageQueueStates.erase(
                    stateItr
                );
            }

            Player* player =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!player)
                continue;

            /*
             * The player must be alive before the match state is removed
             * and before the saved queue position is restored.
             */
            ReviveSabotagePlayerBeforeMatchReturn(
                player
            );

            HideSabotageHud(
                player
            );

            RestoreSabotageCombatState(
                player
            );

            SabotagePendingPostTeleportResetGuids.insert(
                guidLow
            );

            bool teleported =
                TeleportSabotagePlayerToQueueReturnPosition(
                    player,
                    returnState
                );

            if (!teleported)
            {
                SabotagePendingPostTeleportResetGuids.erase(
                    guidLow
                );
            }
        }

        TryCreateSabotageMatches();
    }

    void UpdateSabotageMatches(uint32 diff)
    {
        std::vector<uint32> finishedMatches;

        for (auto& pair : SabotagePreparedMatches)
        {
            SabotagePreparedMatch& match =
                pair.second;

            if (!match.launched)
                continue;

            RefreshSabotageBombCarrierFlag(
                match
            );

            UpdateSabotageSmokeObject(
                match,
                diff
            );

            if (match.hudUpdateTimerMs <= diff)
            {
                match.hudUpdateTimerMs =
                    SABOTAGE_HUD_UPDATE_MS;

                if (match.roundPhase ==
                        SABOTAGE_ROUND_PHASE_ACTIVE &&
                    match.bombObjectGuid.IsEmpty() &&
                    match.bombCarrierGuid.IsEmpty() &&
                    match.installedTargetIndex >=
                        SABOTAGE_TARGET_COUNT)
                {
                    if (SpawnSabotageRoundBomb(match))
                    {
                        TC_LOG_INFO(
                            "custom.scourge",
                            "Sabotage round bomb spawned by retry: match={}, round={}, teamAStart={}({}), bombGuid={}",
                            match.matchId,
                            match.currentRound,
                            match.teamAStartPositionIndex,
                            GetSabotageStartPosition(
                                match,
                                SABOTAGE_TEAM_A
                            ).locationName,
                            match.bombObjectGuid.GetCounter()
                        );
                    }
                }

                SendSabotageHudState(match);
            }
            else
            {
                match.hudUpdateTimerMs -= diff;
            }

            if (match.roundPhase ==
                SABOTAGE_ROUND_PHASE_WAITING)
            {
                if (match.roundBreakTimerMs > diff)
                {
                    match.roundBreakTimerMs -= diff;
                    continue;
                }

                match.roundBreakTimerMs = 0;

                Player* anchor =
                    FindSabotageRoundAnchor(match);

                if (!anchor ||
                    !EnsureSabotageBombTargetsSpawned(
                        anchor,
                        match
                    ))
                {
                    match.roundBreakTimerMs = 1000;
                    continue;
                }

                StartSabotageRound(match);
                continue;
            }

            if (match.roundPhase ==
                    SABOTAGE_ROUND_PHASE_ACTIVE ||
                match.roundPhase ==
                    SABOTAGE_ROUND_PHASE_BOMB_ARMED)
            {
                if (IsSabotageAttackingTeamEliminated(
                    match
                ))
                {
                    EndSabotageRound(
                        match,
                        SABOTAGE_TEAM_B,
                        "A팀이 전멸했습니다"
                    );
                    continue;
                }

                if (IsSabotageDefendingTeamEliminated(
                    match
                ))
                {
                    EndSabotageRound(
                        match,
                        SABOTAGE_TEAM_A,
                        "B팀이 전멸했습니다"
                    );
                    continue;
                }
            }

            if (match.roundPhase ==
                SABOTAGE_ROUND_PHASE_ACTIVE)
            {
                if (match.roundTimerMs > diff)
                {
                    match.roundTimerMs -= diff;
                    continue;
                }

                match.roundTimerMs = 0;

                EndSabotageRound(
                    match,
                    SABOTAGE_TEAM_B,
                    "A팀이 10분 안에 폭발물을 설치하지 못했습니다"
                );

                continue;
            }

            if (match.roundPhase ==
                SABOTAGE_ROUND_PHASE_BOMB_ARMED)
            {
                if (match.bombExplosionTimerMs > diff)
                {
                    match.bombExplosionTimerMs -= diff;
                    continue;
                }

                match.bombExplosionTimerMs = 0;

                EndSabotageRound(
                    match,
                    SABOTAGE_TEAM_A,
                    "설치된 폭발물이 폭발했습니다"
                );

                continue;
            }

            if (match.roundPhase ==
                    SABOTAGE_ROUND_PHASE_BREAK ||
                match.roundPhase ==
                    SABOTAGE_ROUND_PHASE_FINISHED)
            {
                /*
                 * Keep checking throughout the full break/end countdown.
                 * This closes the race where a delayed core death update
                 * runs immediately after EndSabotageRound revived the player.
                 */
                EnsureSabotageRoundPlayersRevived(
                    match
                );

                if (match.roundBreakTimerMs > diff)
                {
                    match.roundBreakTimerMs -= diff;
                    continue;
                }

                match.roundBreakTimerMs = 0;

                if (match.roundPhase ==
                    SABOTAGE_ROUND_PHASE_FINISHED)
                {
                    finishedMatches.push_back(
                        match.matchId
                    );
                }
                else
                {
                    StartSabotageRound(match);
                }
            }
        }

        for (uint32 matchId : finishedMatches)
        {
            FinishSabotageMatch(matchId);
        }
    }

    bool DropSabotageBombAtCarrierPosition(
        Player* carrier,
        bool manualDrop
    )
    {
        if (!carrier)
            return false;

        SabotagePreparedMatch* match = nullptr;
        SabotagePlayerQueueState* state = nullptr;

        if (!GetSabotagePlayerMatchContext(
            carrier,
            match,
            state
        ))
        {
            return false;
        }

        if (state->assignedTeam !=
                SABOTAGE_TEAM_A ||
            match->bombCarrierGuid !=
                carrier->GetGUID())
        {
            return false;
        }

        float x = carrier->GetPositionX();
        float y = carrier->GetPositionY();
        float z = carrier->GetPositionZ();
        float orientation =
            carrier->GetOrientation();

        RemoveSabotageBombCarrierFlag(
            carrier
        );

        carrier->DestroyItemCount(
            SABOTAGE_BOMB_ITEM,
            1,
            true
        );

        /*
         * Clear the carrier first.
         * OnPVPKill and OnPlayerRepop may both run for the same death.
         * The second callback will therefore do nothing.
         */
        match->bombCarrierGuid.Clear();
        match->bombObjectGuid.Clear();

        GameObject* bombObject =
            SpawnSabotageBombObject(
                carrier,
                x,
                y,
                z,
                orientation
            );

        if (!bombObject)
        {
            SendSabotageMatchMessage(
                *match,
                manualDrop
                    ? "폭탄을 내려놓았지만 폭탄 오브젝트 생성에 실패했습니다."
                    : "폭탄 운반자가 사망했지만 폭탄 오브젝트 생성에 실패했습니다."
            );

            TC_LOG_ERROR(
                "custom.scourge",
                "Sabotage bomb drop failed: player={}, guid={}, match={}, instance={}, x={}, y={}, z={}",
                carrier->GetName(),
                carrier->GetGUID().GetCounter(),
                match->matchId,
                match->instanceId,
                x,
                y,
                z
            );

            return false;
        }

        match->bombObjectGuid =
            bombObject->GetGUID();

        std::ostringstream text;
        text
            << carrier->GetName();

        if (manualDrop)
        {
            text
                << "님이 깃발을 내려놓아 폭탄이 "
                << "그 자리에 놓였습니다.";
        }
        else
        {
            text
                << "님이 사망하여 폭탄이 "
                << "그 자리에 떨어졌습니다.";
        }

        SendSabotageMatchMessage(
            *match,
            text.str()
        );

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage bomb dropped: player={}, guid={}, match={}, instance={}, manual={}, goGuid={}, x={}, y={}, z={}",
            carrier->GetName(),
            carrier->GetGUID().GetCounter(),
            match->matchId,
            match->instanceId,
            manualDrop ? 1 : 0,
            bombObject->GetGUID().GetCounter(),
            x,
            y,
            z
        );

        return true;
    }

    bool ReturnSabotageBombToTeamAStart(
        Player* carrier,
        SabotagePreparedMatch& match
    )
    {
        if (!carrier)
            return false;

        RemoveSabotageBombCarrierFlag(
            carrier
        );

        carrier->DestroyItemCount(
            SABOTAGE_BOMB_ITEM,
            1,
            true
        );

        match.bombCarrierGuid.Clear();
        match.bombObjectGuid.Clear();

        SabotageStartPosition const& start =
            GetSabotageStartPosition(
                match,
                SABOTAGE_TEAM_A
            );

        float bombX = 0.0f;
        float bombY = 0.0f;
        float bombZ = 0.0f;
        float bombOrientation = 0.0f;

        GetSabotageStartOffsetPosition(
            start,
            3.50f,
            1.50f,
            bombX,
            bombY,
            bombZ,
            bombOrientation
        );

        GameObject* bombObject =
            SpawnSabotageBombObject(
                carrier,
                bombX,
                bombY,
                bombZ,
                bombOrientation
            );

        if (!bombObject)
            return false;

        match.bombObjectGuid =
            bombObject->GetGUID();

        return true;
    }

    void ClearInvalidSabotageBombCarrier(
        SabotagePreparedMatch& match
    )
    {
        if (match.bombCarrierGuid.IsEmpty())
            return;

        Player* carrier =
            ObjectAccessor::FindConnectedPlayer(
                match.bombCarrierGuid
            );

        bool valid = false;

        if (carrier &&
            carrier->IsAlive() &&
            carrier->GetMapId() ==
                MAP_STRATHOLME &&
            carrier->GetInstanceId() ==
                match.instanceId &&
            carrier->HasItemCount(
                SABOTAGE_BOMB_ITEM,
                1,
                false
            ))
        {
            uint32 carrierGuidLow =
                GetCharacterGuid(carrier);

            auto stateItr =
                SabotageQueueStates.find(
                    carrierGuidLow
                );

            valid =
                stateItr !=
                    SabotageQueueStates.end() &&
                stateItr->second.matched &&
                stateItr->second.matchId ==
                    match.matchId &&
                stateItr->second.assignedTeam ==
                    SABOTAGE_TEAM_A;
        }

        if (valid)
            return;

        if (carrier &&
            carrier->GetMapId() ==
                MAP_STRATHOLME &&
            carrier->GetInstanceId() ==
                match.instanceId)
        {
            DropSabotageBombAtCarrierPosition(
                carrier
            );

            return;
        }

        if (carrier)
        {
            ReturnSabotageBombToTeamAStart(
                carrier,
                match
            );
        }
        else
        {
            match.bombCarrierGuid.Clear();
            match.bombObjectGuid.Clear();
        }

        SendSabotageMatchMessage(
            match,
            "폭탄 운반 상태가 초기화되었습니다."
        );
    }

    char const* GetSabotageTargetName(
        uint32 gameObjectEntry
    )
    {
        switch (gameObjectEntry)
        {
            case SABOTAGE_TARGET_A_GO:
                return "A";

            case SABOTAGE_TARGET_B_GO:
                return "B";

            case SABOTAGE_TARGET_C_GO:
                return "C";

            default:
                return "?";
        }
    }

    int32 GetSabotageTargetIndex(
        uint32 gameObjectEntry
    )
    {
        switch (gameObjectEntry)
        {
            case SABOTAGE_TARGET_A_GO:
                return 0;

            case SABOTAGE_TARGET_B_GO:
                return 1;

            case SABOTAGE_TARGET_C_GO:
                return 2;

            default:
                return -1;
        }
    }

    char const* GetSabotageTargetLocationName(
        SabotagePreparedMatch const& match,
        uint32 targetIndex
    )
    {
        if (targetIndex >=
            SABOTAGE_TARGET_COUNT)
        {
            return "알 수 없는 위치";
        }

        uint32 positionIndex =
            match.bombTargetPositionIndices[
                targetIndex
            ];

        if (positionIndex >=
            SabotageBossPositions.size())
        {
            return "알 수 없는 위치";
        }

        return
            SabotageBossPositions[
                positionIndex
            ].locationName;
    }

    bool StartSabotageBombAction(
        Player* player,
        GameObject* targetObject,
        SabotagePreparedMatch& match,
        SabotagePlayerQueueState const& state
    )
    {
        if (!player ||
            !targetObject ||
            !player->IsAlive())
        {
            return false;
        }

        if (IsSabotageDefenderScoutActive(
                player
            ))
        {
            SendSabotageMessage(
                player,
                "정찰 장치에서 모습을 복구한 뒤 "
                "폭파 지점을 사용할 수 있습니다."
            );

            return false;
        }

        int32 targetIndexValue =
            GetSabotageTargetIndex(
                targetObject->GetEntry()
            );

        if (targetIndexValue < 0)
            return false;

        uint32 targetIndex =
            uint32(targetIndexValue);

        if (targetIndex >=
                SABOTAGE_TARGET_COUNT ||
            match.bombTargetGuids[
                targetIndex
            ] != targetObject->GetGUID())
        {
            SendSabotageMessage(
                player,
                "현재 경기의 폭파 지점이 아닙니다."
            );

            return false;
        }

        if (!player->IsWithinDistInMap(
                targetObject,
                SABOTAGE_BOMB_ACTION_DISTANCE
            ))
        {
            SendSabotageMessage(
                player,
                "폭파 지점에 더 가까이 이동해야 합니다."
            );

            return false;
        }

        if (player->IsNonMeleeSpellCast(false))
        {
            SendSabotageMessage(
                player,
                "다른 주문 시전이 끝난 뒤 다시 시도하세요."
            );

            return false;
        }

        SabotagePendingBombAction pending;
        pending.matchId = match.matchId;
        pending.targetIndex = targetIndex;
        pending.targetGuid =
            targetObject->GetGUID();

        char const* targetName =
            GetSabotageTargetName(
                targetObject->GetEntry()
            );

        char const* locationName =
            GetSabotageTargetLocationName(
                match,
                targetIndex
            );

        if (state.assignedTeam ==
            SABOTAGE_TEAM_A)
        {
            if (match.roundPhase !=
                SABOTAGE_ROUND_PHASE_ACTIVE)
            {
                SendSabotageMessage(
                    player,
                    "현재는 폭발물을 설치할 수 있는 단계가 아닙니다."
                );

                return false;
            }

            if (match.bombTargetInstalled[
                targetIndex
            ])
            {
                SendSabotageMessage(
                    player,
                    "이 폭파 지점에는 이미 폭탄이 설치되어 있습니다."
                );

                return false;
            }

            if (match.bombCarrierGuid !=
                    player->GetGUID() ||
                !player->HasItemCount(
                    SABOTAGE_BOMB_ITEM,
                    1,
                    false
                ))
            {
                SendSabotageMessage(
                    player,
                    "폭탄 운반자만 이 지점에 폭탄을 설치할 수 있습니다."
                );

                return false;
            }

            pending.actionType =
                SABOTAGE_BOMB_ACTION_INSTALL;

            pending.spellId =
                SABOTAGE_BOMB_INSTALL_SPELL;

            SabotagePendingBombActions[
                GetCharacterGuid(player)
            ] = pending;

            std::ostringstream text;
            text
                << "폭파 지점 "
                << targetName
                << "("
                << locationName
                << ")에 폭탄 설치를 시작합니다.";

            SendSabotageMatchMessage(
                match,
                text.str()
            );

            /*
             * Custom Update - 2026-08-02
             *
             * Cast as a normal, non-triggered spell so the 60-second
             * CastingTimeIndex from spell 900206 is applied.
             */
            SpellCastResult castResult =
                player->CastSpell(
                    targetObject,
                    SABOTAGE_BOMB_INSTALL_SPELL,
                    false
                );

            if (castResult != SPELL_CAST_OK)
            {
                ClearSabotagePendingBombAction(
                    player->GetGUID()
                );

                SendSabotageMessage(
                    player,
                    "폭탄 설치 주문 시전에 실패했습니다."
                );

                return false;
            }

            return true;
        }

        if (state.assignedTeam ==
            SABOTAGE_TEAM_B)
        {
            if (match.roundPhase !=
                SABOTAGE_ROUND_PHASE_BOMB_ARMED)
            {
                SendSabotageMessage(
                    player,
                    "현재는 해체할 수 있는 폭발물이 없습니다."
                );

                return false;
            }

            if (!match.bombTargetInstalled[
                targetIndex
            ])
            {
                SendSabotageMessage(
                    player,
                    "이 폭파 지점에는 해체할 폭탄이 없습니다."
                );

                return false;
            }

            pending.actionType =
                SABOTAGE_BOMB_ACTION_DISARM;

            pending.spellId =
                SABOTAGE_BOMB_DISARM_SPELL;

            SabotagePendingBombActions[
                GetCharacterGuid(player)
            ] = pending;

            std::ostringstream text;
            text
                << "폭파 지점 "
                << targetName
                << "("
                << locationName
                << ")의 폭탄 해체를 시작합니다.";

            SendSabotageMatchMessage(
                match,
                text.str()
            );

            /*
             * Custom Update - 2026-08-02
             *
             * Cast as a normal, non-triggered spell so the 30-second
             * CastingTimeIndex from spell 900207 is applied.
             */
            SpellCastResult castResult =
                player->CastSpell(
                    targetObject,
                    SABOTAGE_BOMB_DISARM_SPELL,
                    false
                );

            if (castResult != SPELL_CAST_OK)
            {
                ClearSabotagePendingBombAction(
                    player->GetGUID()
                );

                SendSabotageMessage(
                    player,
                    "폭탄 해체 주문 시전에 실패했습니다."
                );

                return false;
            }

            return true;
        }

        SendSabotageMessage(
            player,
            "팀 정보를 확인할 수 없습니다."
        );

        return false;
    }

    void CompleteSabotageBombAction(
        Player* player,
        Spell* spell
    )
    {
        if (!player || !spell)
            return;

        uint32 guidLow =
            GetCharacterGuid(player);

        auto pendingItr =
            SabotagePendingBombActions.find(
                guidLow
            );

        if (pendingItr ==
            SabotagePendingBombActions.end())
        {
            return;
        }

        SabotagePendingBombAction pending =
            pendingItr->second;

        if (!spell->GetSpellInfo() ||
            spell->GetSpellInfo()->Id !=
                pending.spellId)
        {
            return;
        }

        SabotagePendingBombActions.erase(
            pendingItr
        );

        SabotagePreparedMatch* match = nullptr;
        SabotagePlayerQueueState* state = nullptr;

        if (!GetSabotagePlayerMatchContext(
                player,
                match,
                state
            ) ||
            !match ||
            !state ||
            match->matchId != pending.matchId ||
            pending.targetIndex >=
                SABOTAGE_TARGET_COUNT)
        {
            SendSabotageMessage(
                player,
                "폭파전 상태가 변경되어 작업이 취소되었습니다."
            );

            return;
        }

        Map* map = player->GetMap();

        GameObject* targetObject =
            map
                ? map->GetGameObject(
                    pending.targetGuid
                )
                : nullptr;

        if (!targetObject ||
            match->bombTargetGuids[
                pending.targetIndex
            ] != pending.targetGuid ||
            !player->IsWithinDistInMap(
                targetObject,
                SABOTAGE_BOMB_ACTION_DISTANCE
            ))
        {
            SendSabotageMessage(
                player,
                "폭파 지점에서 멀어져 작업이 취소되었습니다."
            );

            return;
        }

        char const* targetName =
            GetSabotageTargetName(
                targetObject->GetEntry()
            );

        char const* locationName =
            GetSabotageTargetLocationName(
                *match,
                pending.targetIndex
            );

        if (pending.actionType ==
            SABOTAGE_BOMB_ACTION_INSTALL)
        {
            if (match->roundPhase !=
                    SABOTAGE_ROUND_PHASE_ACTIVE ||
                state->assignedTeam !=
                    SABOTAGE_TEAM_A ||
                match->bombCarrierGuid !=
                    player->GetGUID() ||
                match->bombTargetInstalled[
                    pending.targetIndex
                ] ||
                !player->HasItemCount(
                    SABOTAGE_BOMB_ITEM,
                    1,
                    false
                ))
            {
                SendSabotageMessage(
                    player,
                    "폭탄 설치 조건이 더 이상 유효하지 않습니다."
                );

                return;
            }

            RemoveSabotageBombCarrierFlag(
                player
            );

            player->DestroyItemCount(
                SABOTAGE_BOMB_ITEM,
                1,
                true
            );

            match->bombCarrierGuid.Clear();
            match->bombObjectGuid.Clear();

            match->bombTargetInstalled[
                pending.targetIndex
            ] = true;

            targetObject->SetGoState(
                GO_STATE_ACTIVE
            );

            match->installedTargetIndex =
                pending.targetIndex;
            match->roundPhase =
                SABOTAGE_ROUND_PHASE_BOMB_ARMED;
            match->roundTimerMs = 0;
            match->bombExplosionTimerMs =
                SABOTAGE_BOMB_EXPLOSION_MS;
            match->hudUpdateTimerMs = 0;

            std::ostringstream text;
            text
                << player->GetName()
                << "님이 폭파 지점 "
                << targetName
                << "("
                << locationName
                << ")에 폭탄을 설치했습니다. "
                << "1분 후 폭발합니다.";

            SendSabotageMatchMessage(
                *match,
                text.str()
            );

            SendSabotageHudState(*match);

            TC_LOG_INFO(
                "custom.scourge",
                "Sabotage bomb installed: player={}, guid={}, match={}, instance={}, target={}, location={}, spell={}",
                player->GetName(),
                player->GetGUID().GetCounter(),
                match->matchId,
                match->instanceId,
                targetName,
                locationName,
                pending.spellId
            );

            return;
        }

        if (pending.actionType ==
            SABOTAGE_BOMB_ACTION_DISARM)
        {
            if (match->roundPhase !=
                    SABOTAGE_ROUND_PHASE_BOMB_ARMED ||
                state->assignedTeam !=
                    SABOTAGE_TEAM_B ||
                !match->bombTargetInstalled[
                    pending.targetIndex
                ])
            {
                SendSabotageMessage(
                    player,
                    "폭탄 해체 조건이 더 이상 유효하지 않습니다."
                );

                return;
            }

            match->bombTargetInstalled[
                pending.targetIndex
            ] = false;

            targetObject->SetGoState(
                GO_STATE_READY
            );

            std::ostringstream text;
            text
                << player->GetName()
                << "님이 폭파 지점 "
                << targetName
                << "("
                << locationName
                << ")의 폭탄을 해체했습니다.";

            SendSabotageMatchMessage(
                *match,
                text.str()
            );

            TC_LOG_INFO(
                "custom.scourge",
                "Sabotage bomb disarmed: player={}, guid={}, match={}, instance={}, target={}, location={}, spell={}",
                player->GetName(),
                player->GetGUID().GetCounter(),
                match->matchId,
                match->instanceId,
                targetName,
                locationName,
                pending.spellId
            );

            EndSabotageRound(
                *match,
                SABOTAGE_TEAM_B,
                "B팀이 설치된 폭발물을 해체했습니다"
            );
        }
    }

    ObjectGuid GetSabotageTeamGroupGuid(
        SabotagePreparedMatch const& match,
        SabotageAssignedTeam team
    )
    {
        if (team == SABOTAGE_TEAM_A)
            return match.teamAGroupGuid;

        if (team == SABOTAGE_TEAM_B)
            return match.teamBGroupGuid;

        return ObjectGuid::Empty;
    }

    bool ApplySabotageFreeForAll(
        Player* player,
        SabotageAssignedTeam team,
        ObjectGuid sabotageGroupGuid
    )
    {
        if (!player ||
            (team != SABOTAGE_TEAM_A &&
             team != SABOTAGE_TEAM_B) ||
            sabotageGroupGuid.IsEmpty())
        {
            return false;
        }

        uint32 guid =
            GetCharacterGuid(player);

        if (!guid)
            return false;

        Group* group = player->GetGroup();

        if (!group ||
            group->GetGUID() !=
                sabotageGroupGuid)
        {
            TC_LOG_ERROR(
                "custom.scourge",
                "Sabotage team group mismatch: player={}, guid={}, team={}",
                player->GetName(),
                guid,
                uint32(team)
            );

            return false;
        }

        SetScourgeSabotageParticipant(
            player,
            true
        );

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SABOTAGE:1",
            player
        );

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SPECTATOR:0",
            player
        );

        auto restoreItr =
            SabotageCombatRestoreStates.find(guid);

        bool firstApply =
            restoreItr ==
                SabotageCombatRestoreStates.end();

        if (firstApply)
        {
            SabotageCombatRestoreState state;
            state.originalPvpFlags =
                uint8(player->GetPvpFlags());

            state.sabotageGroupGuid =
                sabotageGroupGuid;

            SabotageCombatRestoreStates[
                guid
            ] = state;
        }
        else
        {
            restoreItr->second.sabotageGroupGuid =
                sabotageGroupGuid;
        }

        /*
         * Player.cpp treats all of map 329 as an FFA area.
         * Refresh the normal PvP and FFA flags after team assignment.
         */
        player->RemovePvpFlag(
            UNIT_BYTE2_FLAG_SANCTUARY
        );

        player->UpdatePvP(
            true,
            true
        );

        player->UpdatePvPState(true);

        for (Unit* controlled :
             player->m_Controlled)
        {
            if (!controlled)
                continue;

            controlled->RemovePvpFlag(
                UNIT_BYTE2_FLAG_SANCTUARY
            );

            controlled->SetPvpFlag(
                UNIT_BYTE2_FLAG_PVP
            );

            controlled->SetPvpFlag(
                UNIT_BYTE2_FLAG_FFA_PVP
            );
        }

        player->UpdateObjectVisibility(true);

        if (firstApply)
        {
            SendSabotageMessage(
                player,
                "프리포올 전투 상태가 적용되었습니다. "
                "같은 팀 파티원만 우호 대상입니다."
            );
        }

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage FFA applied: player={}, guid={}, team={}, group={}, pvp={}, ffa={}",
            player->GetName(),
            guid,
            uint32(team),
            sabotageGroupGuid.GetCounter(),
            player->IsPvP() ? 1 : 0,
            player->IsFFAPvP() ? 1 : 0
        );

        return player->IsFFAPvP();
    }

    bool RefreshSabotageFreeForAll(
        Player* player
    )
    {
        if (!player ||
            player->GetMapId() != MAP_STRATHOLME)
        {
            return false;
        }

        uint32 guidLow =
            GetCharacterGuid(player);

        if (!guidLow)
            return false;

        auto stateItr =
            SabotageQueueStates.find(guidLow);

        if (stateItr ==
            SabotageQueueStates.end())
        {
            return false;
        }

        SabotagePlayerQueueState const& state =
            stateItr->second;

        if (!state.matched ||
            !state.matchId ||
            state.assignedTeam ==
                SABOTAGE_TEAM_NONE)
        {
            return false;
        }

        auto matchItr =
            SabotagePreparedMatches.find(
                state.matchId
            );

        if (matchItr ==
            SabotagePreparedMatches.end())
        {
            return false;
        }

        SabotagePreparedMatch const& match =
            matchItr->second;

        if (!match.launched ||
            !match.instanceId ||
            player->GetInstanceId() !=
                match.instanceId)
        {
            return false;
        }

        ObjectGuid groupGuid =
            GetSabotageTeamGroupGuid(
                match,
                state.assignedTeam
            );

        return ApplySabotageFreeForAll(
            player,
            state.assignedTeam,
            groupGuid
        );
    }

    void SendSabotageMessage(
        Player* player,
        std::string const& text
    )
    {
        if (!player || !player->GetSession())
            return;

        std::string message =
            "|cffd84cff[폭파전]|r " + text;

        ChatHandler(player->GetSession()).SendSysMessage(
            message.c_str()
        );
    }

    char const* GetSabotageFactionName(uint32 faction)
    {
        if (faction == ALLIANCE)
            return "얼라이언스";

        if (faction == HORDE)
            return "호드";

        return "알 수 없음";
    }

    char const* GetSabotageQueueName(
        SabotageQueueType queueType
    )
    {
        if (queueType == SABOTAGE_QUEUE_FACTION_WAR)
            return "진영전";

        if (queueType == SABOTAGE_QUEUE_SAME_FACTION)
            return "같은 진영 팀전";

        return "미신청";
    }

    char const* GetSabotageTeamName(
        SabotageAssignedTeam team
    )
    {
        if (team == SABOTAGE_TEAM_A)
            return "A팀";

        if (team == SABOTAGE_TEAM_B)
            return "B팀";

        return "미배정";
    }

    std::deque<ObjectGuid>* GetSabotageQueueContainer(
        SabotageQueueType queueType,
        uint32 faction
    )
    {
        if (queueType == SABOTAGE_QUEUE_FACTION_WAR)
        {
            if (faction == ALLIANCE)
                return &SabotageFactionAllianceQueue;

            if (faction == HORDE)
                return &SabotageFactionHordeQueue;

            return nullptr;
        }

        if (queueType == SABOTAGE_QUEUE_SAME_FACTION)
        {
            if (faction == ALLIANCE)
                return &SabotageSameAllianceQueue;

            if (faction == HORDE)
                return &SabotageSameHordeQueue;

            return nullptr;
        }

        return nullptr;
    }

    uint32 GetSabotageWaitingCount(
        SabotageQueueType queueType,
        uint32 faction
    )
    {
        std::deque<ObjectGuid>* queue =
            GetSabotageQueueContainer(
                queueType,
                faction
            );

        if (!queue)
            return 0;

        return uint32(queue->size());
    }

    void SendSabotageQueueApplicationNotification(
        Player* applicant,
        SabotageQueueType queueType,
        uint32 faction,
        bool joinedActiveMatch
    )
    {
        if (!applicant ||
            !applicant->GetSession())
        {
            return;
        }

        std::ostringstream text;

        if (queueType ==
            SABOTAGE_QUEUE_FACTION_WAR)
        {
            text
                << applicant->GetName()
                << "님이 "
                << GetSabotageFactionName(faction)
                << " 진영으로 폭파전 진영전에 신청했습니다.";

            if (joinedActiveMatch)
            {
                text
                    << " 진행 중인 경기에 즉시 합류했습니다.";
            }
            else
            {
                text
                    << " 현재 대기 인원: 얼라이언스 "
                    << GetSabotageWaitingCount(
                        SABOTAGE_QUEUE_FACTION_WAR,
                        ALLIANCE
                    )
                    << "명 / 호드 "
                    << GetSabotageWaitingCount(
                        SABOTAGE_QUEUE_FACTION_WAR,
                        HORDE
                    )
                    << "명.";
            }
        }
        else if (queueType ==
            SABOTAGE_QUEUE_SAME_FACTION)
        {
            text
                << applicant->GetName()
                << "님이 "
                << GetSabotageFactionName(faction)
                << " 같은 진영 폭파전에 신청했습니다.";

            if (joinedActiveMatch)
            {
                text
                    << " 진행 중인 경기에 즉시 합류했습니다.";
            }
            else
            {
                text
                    << " 현재 같은 진영 대기 인원: "
                    << GetSabotageWaitingCount(
                        SABOTAGE_QUEUE_SAME_FACTION,
                        faction
                    )
                    << "명.";
            }
        }
        else
        {
            return;
        }

        for (
            auto const& sessionEntry :
            sWorld->GetAllSessions()
        )
        {
            WorldSession* session =
                sessionEntry.second;

            if (!session)
                continue;

            Player* target =
                session->GetPlayer();

            if (!target ||
                !target->IsInWorld())
            {
                continue;
            }

            if (queueType ==
                    SABOTAGE_QUEUE_SAME_FACTION &&
                target->GetTeam() != faction)
            {
                continue;
            }

            SendSabotageMessage(
                target,
                text.str()
            );
        }
    }

    void RemoveGuidFromSabotageQueue(
        std::deque<ObjectGuid>& queue,
        ObjectGuid guid
    )
    {
        queue.erase(
            std::remove(
                queue.begin(),
                queue.end(),
                guid
            ),
            queue.end()
        );
    }

    void RemoveGuidFromAllSabotageQueues(
        ObjectGuid guid
    )
    {
        RemoveGuidFromSabotageQueue(
            SabotageFactionAllianceQueue,
            guid
        );

        RemoveGuidFromSabotageQueue(
            SabotageFactionHordeQueue,
            guid
        );

        RemoveGuidFromSabotageQueue(
            SabotageSameAllianceQueue,
            guid
        );

        RemoveGuidFromSabotageQueue(
            SabotageSameHordeQueue,
            guid
        );
    }

    void CleanSabotageQueue(
        std::deque<ObjectGuid>& queue
    )
    {
        for (auto itr = queue.begin();
             itr != queue.end();)
        {
            ObjectGuid guid = *itr;
            uint32 guidLow =
                uint32(guid.GetCounter());

            Player* player =
                ObjectAccessor::FindConnectedPlayer(guid);

            auto stateItr =
                SabotageQueueStates.find(guidLow);

            bool valid =
                player &&
                stateItr != SabotageQueueStates.end() &&
                !stateItr->second.matched;

            if (valid)
            {
                ++itr;
                continue;
            }

            if (stateItr != SabotageQueueStates.end() &&
                !stateItr->second.matched)
            {
                SabotageQueueStates.erase(stateItr);
            }

            itr = queue.erase(itr);
        }
    }

    void CleanAllSabotageQueues()
    {
        CleanSabotageQueue(
            SabotageFactionAllianceQueue
        );

        CleanSabotageQueue(
            SabotageFactionHordeQueue
        );

        CleanSabotageQueue(
            SabotageSameAllianceQueue
        );

        CleanSabotageQueue(
            SabotageSameHordeQueue
        );
    }

    void SetSabotageMatchedState(
        ObjectGuid guid,
        SabotagePreparedMatch const& match,
        SabotageAssignedTeam team,
        SabotageCompetitorSide competitorSide
    )
    {
        uint32 guidLow =
            uint32(guid.GetCounter());

        auto itr =
            SabotageQueueStates.find(guidLow);

        if (itr == SabotageQueueStates.end())
            return;

        itr->second.matched = true;
        itr->second.matchId = match.matchId;
        itr->second.assignedTeam = team;
        itr->second.competitorSide =
            competitorSide;
    }

    void NotifySabotageMatch(
        SabotagePreparedMatch const& match
    )
    {
        std::ostringstream common;

        common
            << "경기 #" << match.matchId
            << " 팀 구성이 완료되었습니다. "
            << "A팀 " << match.teamA.size()
            << "명 / B팀 " << match.teamB.size()
            << "명. 같은 스트라솔름 경기장으로 입장합니다.";

        for (ObjectGuid const& guid : match.teamA)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                std::ostringstream text;
                text
                    << common.str()
                    << " 당신은 A팀입니다.";

                SendSabotageMessage(
                    player,
                    text.str()
                );
            }
        }

        for (ObjectGuid const& guid : match.teamB)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                std::ostringstream text;
                text
                    << common.str()
                    << " 당신은 B팀입니다.";

                SendSabotageMessage(
                    player,
                    text.str()
                );
            }
        }

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage queue matched: match={}, mode={}, faction={}, teamA={}, teamB={}",
            match.matchId,
            uint32(match.queueType),
            match.faction,
            match.teamA.size(),
            match.teamB.size()
        );
    }

    struct SabotageCreatedTeamGroups
    {
        Group* teamA;
        Group* teamB;

        SabotageCreatedTeamGroups()
            : teamA(nullptr),
              teamB(nullptr)
        {
        }
    };

    void RemovePlayersFromCurrentGroups(
        SabotagePreparedMatch const& match
    )
    {
        std::vector<ObjectGuid> members;
        members.insert(
            members.end(),
            match.teamA.begin(),
            match.teamA.end()
        );

        members.insert(
            members.end(),
            match.teamB.begin(),
            match.teamB.end()
        );

        for (ObjectGuid const& guid : members)
        {
            Player* player =
                ObjectAccessor::FindConnectedPlayer(guid);

            if (!player)
                continue;

            if (player->GetGroup())
            {
                SendSabotageMessage(
                    player,
                    "폭파전 전용 공격대를 구성하기 위해 "
                    "현재 파티 또는 공격대에서 분리됩니다."
                );

                player->RemoveFromGroup();
            }

            player->UnbindInstance(
                MAP_STRATHOLME,
                DUNGEON_DIFFICULTY_NORMAL
            );
        }
    }

    Group* CreateSabotageTeamGroup(
        std::vector<ObjectGuid> const& members
    )
    {
        if (members.empty() ||
            members.size() > MAX_RAID_SIZE)
        {
            return nullptr;
        }

        Player* leader =
            ObjectAccessor::FindConnectedPlayer(
                members.front()
            );

        if (!leader ||
            leader->GetGroup())
        {
            return nullptr;
        }

        Group* group = new Group();

        if (!group->Create(leader))
        {
            delete group;
            return nullptr;
        }

        /*
         * Match TrinityCore's normal group lifecycle:
         * register the group first, then convert and broadcast the raid state.
         */
        sGroupMgr->AddGroup(group);

        group->ConvertToRaid();

        for (std::size_t index = 1;
             index < members.size();
             ++index)
        {
            Player* member =
                ObjectAccessor::FindConnectedPlayer(
                    members[index]
                );

            if (!member ||
                member->GetGroup() ||
                !group->AddMember(member))
            {
                group->Disband();
                return nullptr;
            }
        }

        group->SetDungeonDifficulty(
            DUNGEON_DIFFICULTY_NORMAL
        );

        if (!group->isRaidGroup())
        {
            group->ConvertToRaid();
        }

        group->SendUpdate();

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage team raid ready: raid={}, leader={}, members={}, expectedMembers={}, isRaid={}",
            group->GetGUID().GetCounter(),
            leader->GetName(),
            group->GetMembersCount(),
            members.size(),
            group->isRaidGroup() ? 1 : 0
        );

        for (ObjectGuid const& memberGuid : members)
        {
            Player* raidMember =
                ObjectAccessor::FindConnectedPlayer(
                    memberGuid
                );

            TC_LOG_INFO(
                "custom.scourge",
                "Sabotage raid member state: raid={}, player={}, guid={}, actualRaid={}",
                group->GetGUID().GetCounter(),
                raidMember
                    ? raidMember->GetName()
                    : std::string("offline"),
                memberGuid.GetCounter(),
                raidMember && raidMember->GetGroup()
                    ? raidMember->GetGroup()->GetGUID().GetCounter()
                    : 0
            );
        }

        return group;
    }

    void DisbandSabotageTeamGroups(
        SabotageCreatedTeamGroups& groups
    )
    {
        if (groups.teamA)
        {
            groups.teamA->Disband();
            groups.teamA = nullptr;
        }

        if (groups.teamB)
        {
            groups.teamB->Disband();
            groups.teamB = nullptr;
        }
    }

    bool CreateSabotageTeamGroups(
        SabotagePreparedMatch& match,
        SabotageCreatedTeamGroups& groups
    )
    {
        RemovePlayersFromCurrentGroups(match);

        groups.teamA =
            CreateSabotageTeamGroup(
                match.teamA
            );

        if (!groups.teamA)
            return false;

        groups.teamB =
            CreateSabotageTeamGroup(
                match.teamB
            );

        if (!groups.teamB)
        {
            DisbandSabotageTeamGroups(
                groups
            );

            return false;
        }

        match.teamAGroupGuid =
            groups.teamA->GetGUID();

        match.teamBGroupGuid =
            groups.teamB->GetGUID();

        match.sideOneGroupGuid =
            match.teamAGroupGuid;

        match.sideTwoGroupGuid =
            match.teamBGroupGuid;

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage team raids created: match={}, teamARaid={}, teamBRaid={}",
            match.matchId,
            match.teamAGroupGuid.GetCounter(),
            match.teamBGroupGuid.GetCounter()
        );

        return true;
    }

    bool CreateSabotageSharedInstance(
        SabotagePreparedMatch& match,
        InstanceSave*& save
    )
    {
        save = nullptr;

        uint32 instanceId =
            sMapMgr->GenerateInstanceId();

        if (!instanceId)
            return false;

        save = sInstanceSaveMgr->AddInstanceSave(
            MAP_STRATHOLME,
            instanceId,
            DUNGEON_DIFFICULTY_NORMAL,
            0,
            true
        );

        if (!save)
        {
            sMapMgr->FreeInstanceId(instanceId);

            TC_LOG_ERROR(
                "custom.scourge",
                "Failed to create sabotage instance save: match={}, map={}, instance={}",
                match.matchId,
                MAP_STRATHOLME,
                instanceId
            );

            return false;
        }

        match.instanceId = instanceId;
        match.launched = true;
        match.instanceSave = save;

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage shared instance created: match={}, map={}, instance={}",
            match.matchId,
            MAP_STRATHOLME,
            instanceId
        );

        return true;
    }

    bool TeleportSabotagePlayer(
        Player* player,
        InstanceSave* save,
        SabotagePreparedMatch const& match,
        SabotageAssignedTeam team
    )
    {
        if (!player || !save)
            return false;

        ObjectGuid expectedGroupGuid =
            GetSabotageTeamGroupGuid(
                match,
                team
            );

        Group* teamGroup =
            player->GetGroup();

        if (!teamGroup ||
            expectedGroupGuid.IsEmpty() ||
            teamGroup->GetGUID() !=
                expectedGroupGuid)
        {
            SendSabotageMessage(
                player,
                "폭파전 팀 파티를 확인할 수 없습니다."
            );

            TC_LOG_ERROR(
                "custom.scourge",
                "Sabotage team group missing before teleport: player={}, match={}, team={}",
                player->GetName(),
                match.matchId,
                uint32(team)
            );

            return false;
        }

        player->SetDungeonDifficulty(
            DUNGEON_DIFFICULTY_NORMAL
        );

        player->UnbindInstance(
            MAP_STRATHOLME,
            DUNGEON_DIFFICULTY_NORMAL
        );

        if (!player->BindToInstance(
            save,
            false
        ))
        {
            TC_LOG_ERROR(
                "custom.scourge",
                "Failed to bind sabotage player: player={}, match={}, instance={}",
                player->GetName(),
                match.matchId,
                match.instanceId
            );

            SendSabotageMessage(
                player,
                "경기 인스턴스 귀속에 실패했습니다."
            );

            return false;
        }

        player->DestroyItemCount(
            SABOTAGE_BOMB_ITEM,
            1,
            true
        );

        player->SaveRecallPosition();

        player->m_InstanceValid = true;
        player->UpdateHomebindTime(0);

        player->SetPhaseMask(
            SCOURGE_PHASE_MASK,
            true
        );

        ScourgeModePhasePlayers.insert(
            GetCharacterGuid(player)
        );

        ApplySabotageFreeForAll(
            player,
            team,
            expectedGroupGuid
        );

        SabotageStartPosition const& start =
            GetSabotageStartPosition(
                match,
                team
            );

        float x = start.x;
        float y = start.y;
        float z = start.z;
        float o = start.orientation;

        /*
         * The team raid must still exist immediately before world transfer.
         * This log makes any later removal visible in the server log.
         */
        Group* raidBeforeTeleport =
            player->GetGroup();

        if (!raidBeforeTeleport ||
            raidBeforeTeleport->GetGUID() !=
                expectedGroupGuid)
        {
            TC_LOG_ERROR(
                "custom.scourge",
                "Sabotage raid lost before teleport: player={}, match={}, expectedRaid={}, actualRaid={}",
                player->GetName(),
                match.matchId,
                expectedGroupGuid.GetCounter(),
                raidBeforeTeleport
                    ? raidBeforeTeleport->GetGUID().GetCounter()
                    : 0
            );

            return false;
        }

        if (!raidBeforeTeleport->isRaidGroup())
        {
            raidBeforeTeleport->ConvertToRaid();
        }

        raidBeforeTeleport->SendUpdate();

        uint32 guidLow =
            GetCharacterGuid(
                player
            );

        if (guidLow)
        {
            SabotagePendingEntryTransferMs[
                guidLow
            ] = SABOTAGE_ENTRY_TRANSFER_GRACE_MS;
        }

        bool teleported = player->TeleportTo(
            MAP_STRATHOLME,
            x,
            y,
            z,
            o
        );

        if (!teleported)
        {
            SabotagePendingEntryTransferMs.erase(
                guidLow
            );

            TC_LOG_ERROR(
                "custom.scourge",
                "Failed to teleport sabotage player: player={}, match={}, instance={}, team={}",
                player->GetName(),
                match.matchId,
                match.instanceId,
                uint32(team)
            );

            SendSabotageMessage(
                player,
                "스트라솔름 경기장 이동에 실패했습니다."
            );

            return false;
        }

        GiveSabotageMountItem(
            player
        );

        std::ostringstream text;
        text
            << "경기 #" << match.matchId
            << " "
            << GetSabotageTeamName(team)
            << " 시작 위치로 이동합니다. "
            << "공용 인스턴스 ID: "
            << match.instanceId;

        SendSabotageMessage(
            player,
            text.str()
        );

        return true;
    }

    void LaunchSabotageMatch(
        SabotagePreparedMatch const& match,
        InstanceSave* save
    )
    {
        uint32 teleportedCount = 0;

        for (ObjectGuid const& guid : match.teamA)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                if (TeleportSabotagePlayer(
                    player,
                    save,
                    match,
                    SABOTAGE_TEAM_A
                ))
                {
                    ++teleportedCount;
                }
            }
        }

        for (ObjectGuid const& guid : match.teamB)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                if (TeleportSabotagePlayer(
                    player,
                    save,
                    match,
                    SABOTAGE_TEAM_B
                ))
                {
                    ++teleportedCount;
                }
            }
        }

        for (ObjectGuid const& guid : match.teamA)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                player->UpdateObjectVisibility(true);
            }
        }

        for (ObjectGuid const& guid : match.teamB)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                player->UpdateObjectVisibility(true);
            }
        }

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage match launched: match={}, instance={}, teleported={}, expected={}",
            match.matchId,
            match.instanceId,
            teleportedCount,
            match.teamA.size() + match.teamB.size()
        );
    }

    Group* ResolveSabotageTeamGroup(
        SabotagePreparedMatch const& match,
        SabotageAssignedTeam team
    )
    {
        std::vector<ObjectGuid> const* members =
            nullptr;

        ObjectGuid expectedGroupGuid =
            ObjectGuid::Empty;

        if (team == SABOTAGE_TEAM_A)
        {
            members = &match.teamA;
            expectedGroupGuid =
                match.teamAGroupGuid;
        }
        else if (team == SABOTAGE_TEAM_B)
        {
            members = &match.teamB;
            expectedGroupGuid =
                match.teamBGroupGuid;
        }

        if (!members ||
            expectedGroupGuid.IsEmpty())
        {
            return nullptr;
        }

        if (Group* storedGroup =
            sGroupMgr->GetGroupByGUID(
                expectedGroupGuid
            ))
        {
            return storedGroup;
        }

        for (ObjectGuid const& guid : *members)
        {
            Player* member =
                ObjectAccessor::FindConnectedPlayer(guid);

            if (!member)
                continue;

            Group* group = member->GetGroup();

            if (group &&
                group->GetGUID() ==
                    expectedGroupGuid)
            {
                return group;
            }
        }

        return nullptr;
    }

    SabotageAssignedTeam SelectActiveSabotageTeam(
        SabotagePreparedMatch const& match,
        uint32 faction
    )
    {
        if (match.queueType ==
            SABOTAGE_QUEUE_FACTION_WAR)
        {
            if (faction == ALLIANCE &&
                match.sideOne.size() <
                    SABOTAGE_MAX_TEAM_SIZE)
            {
                return
                    GetSabotageAssignedTeamForCompetitor(
                        match,
                        SABOTAGE_COMPETITOR_ONE
                    );
            }

            if (faction == HORDE &&
                match.sideTwo.size() <
                    SABOTAGE_MAX_TEAM_SIZE)
            {
                return
                    GetSabotageAssignedTeamForCompetitor(
                        match,
                        SABOTAGE_COMPETITOR_TWO
                    );
            }

            return SABOTAGE_TEAM_NONE;
        }

        if (match.queueType !=
                SABOTAGE_QUEUE_SAME_FACTION ||
            match.faction != faction)
        {
            return SABOTAGE_TEAM_NONE;
        }

        bool sideOneHasRoom =
            match.sideOne.size() <
                SABOTAGE_MAX_TEAM_SIZE;

        bool sideTwoHasRoom =
            match.sideTwo.size() <
                SABOTAGE_MAX_TEAM_SIZE;

        if (!sideOneHasRoom && !sideTwoHasRoom)
            return SABOTAGE_TEAM_NONE;

        SabotageCompetitorSide side =
            SABOTAGE_COMPETITOR_ONE;

        if (!sideOneHasRoom)
        {
            side = SABOTAGE_COMPETITOR_TWO;
        }
        else if (sideTwoHasRoom &&
            match.sideTwo.size() <
                match.sideOne.size())
        {
            side = SABOTAGE_COMPETITOR_TWO;
        }

        return GetSabotageAssignedTeamForCompetitor(
            match,
            side
        );
    }

    std::vector<ObjectGuid> BuildConnectedSabotageTeamMembers(
        SabotagePreparedMatch const& match,
        SabotageAssignedTeam team,
        ObjectGuid additionalGuid =
            ObjectGuid::Empty
    )
    {
        std::vector<ObjectGuid> result;

        std::vector<ObjectGuid> const* source =
            nullptr;

        if (team == SABOTAGE_TEAM_A)
            source = &match.teamA;
        else if (team == SABOTAGE_TEAM_B)
            source = &match.teamB;

        if (source)
        {
            for (ObjectGuid const& guid : *source)
            {
                if (guid.IsEmpty())
                    continue;

                if (std::find(
                        result.begin(),
                        result.end(),
                        guid
                    ) != result.end())
                {
                    continue;
                }

                Player* member =
                    ObjectAccessor::FindConnectedPlayer(
                        guid
                    );

                if (!member)
                    continue;

                result.push_back(
                    guid
                );
            }
        }

        if (!additionalGuid.IsEmpty() &&
            std::find(
                result.begin(),
                result.end(),
                additionalGuid
            ) == result.end())
        {
            Player* additionalPlayer =
                ObjectAccessor::FindConnectedPlayer(
                    additionalGuid
                );

            if (additionalPlayer)
            {
                result.push_back(
                    additionalGuid
                );
            }
        }

        return result;
    }

    void StoreSabotageTeamGroupGuid(
        SabotagePreparedMatch& match,
        SabotageAssignedTeam team,
        SabotageCompetitorSide competitorSide,
        ObjectGuid groupGuid
    )
    {
        if (team == SABOTAGE_TEAM_A)
            match.teamAGroupGuid = groupGuid;
        else if (team == SABOTAGE_TEAM_B)
            match.teamBGroupGuid = groupGuid;

        if (competitorSide ==
            SABOTAGE_COMPETITOR_ONE)
        {
            match.sideOneGroupGuid =
                groupGuid;
        }
        else if (competitorSide ==
            SABOTAGE_COMPETITOR_TWO)
        {
            match.sideTwoGroupGuid =
                groupGuid;
        }
    }

    void RefreshSabotageTeamRaidMembers(
        Group* group,
        SabotagePreparedMatch const& match,
        SabotageAssignedTeam team
    )
    {
        if (!group)
            return;

        if (!group->isRaidGroup())
        {
            group->ConvertToRaid();
        }

        group->SetDungeonDifficulty(
            DUNGEON_DIFFICULTY_NORMAL
        );

        if (match.instanceSave)
        {
            InstanceGroupBind* bind =
                group->GetBoundInstance(
                    DUNGEON_DIFFICULTY_NORMAL,
                    MAP_STRATHOLME
                );

            if (!bind ||
                !bind->save ||
                bind->save->GetInstanceId() !=
                    match.instanceId)
            {
                group->UnbindInstance(
                    MAP_STRATHOLME,
                    DUNGEON_DIFFICULTY_NORMAL
                );

                group->BindToInstance(
                    match.instanceSave,
                    false
                );
            }
        }

        std::vector<ObjectGuid> const* members =
            nullptr;

        if (team == SABOTAGE_TEAM_A)
            members = &match.teamA;
        else if (team == SABOTAGE_TEAM_B)
            members = &match.teamB;

        if (members)
        {
            for (ObjectGuid const& guid : *members)
            {
                Player* member =
                    ObjectAccessor::FindConnectedPlayer(
                        guid
                    );

                if (!member)
                    continue;

                if (member->GetGroup() == group &&
                    group->IsMember(guid))
                {
                    member->m_InstanceValid = true;
                    member->UpdateHomebindTime(0);
                    member->SetGroupUpdateFlag(
                        GROUP_UPDATE_FULL
                    );
                }
            }
        }

        group->SendUpdate();
    }

    Group* RebuildSabotageTeamRaid(
        SabotagePreparedMatch& match,
        SabotageAssignedTeam team,
        SabotageCompetitorSide competitorSide,
        ObjectGuid additionalGuid
    )
    {
        std::vector<ObjectGuid> connectedMembers =
            BuildConnectedSabotageTeamMembers(
                match,
                team,
                additionalGuid
            );

        if (connectedMembers.empty())
            return nullptr;

        /*
         * A previously disbanded two-member raid leaves every remaining
         * member without a group. Rebuild the raid with all currently
         * connected team members, not only the newly joining player.
         */
        for (ObjectGuid const& guid : connectedMembers)
        {
            Player* member =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!member)
                continue;

            if (member->GetGroup())
            {
                TC_LOG_ERROR(
                    "custom.scourge",
                    "Cannot rebuild sabotage team raid because member still has another group: player={}, guid={}, group={}",
                    member->GetName(),
                    guid.GetCounter(),
                    member->GetGroup()->GetGUID().GetCounter()
                );

                return nullptr;
            }
        }

        Group* group =
            CreateSabotageTeamGroup(
                connectedMembers
            );

        if (!group)
            return nullptr;

        StoreSabotageTeamGroupGuid(
            match,
            team,
            competitorSide,
            group->GetGUID()
        );

        RefreshSabotageTeamRaidMembers(
            group,
            match,
            team
        );

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage team raid rebuilt: match={}, team={}, raid={}, members={}",
            match.matchId,
            uint32(team),
            group->GetGUID().GetCounter(),
            group->GetMembersCount()
        );

        return group;
    }

    Group* EnsureSabotageActiveJoinRaid(
        Player* player,
        SabotagePreparedMatch& match,
        SabotageAssignedTeam team,
        SabotageCompetitorSide competitorSide
    )
    {
        if (!player)
            return nullptr;

        ObjectGuid playerGuid =
            player->GetGUID();

        Group* group =
            ResolveSabotageTeamGroup(
                match,
                team
            );

        if (!group)
        {
            return RebuildSabotageTeamRaid(
                match,
                team,
                competitorSide,
                playerGuid
            );
        }

        if (!group->isRaidGroup())
        {
            group->ConvertToRaid();
        }

        if (!group->IsMember(
                playerGuid
            ))
        {
            if (group->IsFull() ||
                !group->AddMember(
                    player
                ))
            {
                TC_LOG_ERROR(
                    "custom.scourge",
                    "Failed to add active joiner to sabotage raid: player={}, match={}, team={}, raid={}, members={}",
                    player->GetName(),
                    match.matchId,
                    uint32(team),
                    group->GetGUID().GetCounter(),
                    group->GetMembersCount()
                );

                return nullptr;
            }
        }

        if (player->GetGroup() != group ||
            !group->IsMember(
                playerGuid
            ))
        {
            TC_LOG_ERROR(
                "custom.scourge",
                "Sabotage active join raid verification failed: player={}, match={}, expectedRaid={}, actualRaid={}, isMember={}",
                player->GetName(),
                match.matchId,
                group->GetGUID().GetCounter(),
                player->GetGroup()
                    ? player->GetGroup()->GetGUID().GetCounter()
                    : 0,
                group->IsMember(playerGuid)
                    ? 1
                    : 0
            );

            return nullptr;
        }

        StoreSabotageTeamGroupGuid(
            match,
            team,
            competitorSide,
            group->GetGUID()
        );

        RefreshSabotageTeamRaidMembers(
            group,
            match,
            team
        );

        player->m_InstanceValid = true;
        player->UpdateHomebindTime(0);
        player->SetGroupUpdateFlag(
            GROUP_UPDATE_FULL
        );

        group->SendUpdateToPlayer(
            player
        );

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage active joiner raid confirmed: player={}, match={}, team={}, raid={}, members={}",
            player->GetName(),
            match.matchId,
            uint32(team),
            group->GetGUID().GetCounter(),
            group->GetMembersCount()
        );

        return group;
    }

    Group* FindConnectedSabotageTeamGroup(
        SabotagePreparedMatch const& match,
        SabotageAssignedTeam team
    )
    {
        Group* group =
            ResolveSabotageTeamGroup(
                match,
                team
            );

        if (group)
            return group;

        std::vector<ObjectGuid> const* members =
            nullptr;

        if (team == SABOTAGE_TEAM_A)
            members = &match.teamA;
        else if (team == SABOTAGE_TEAM_B)
            members = &match.teamB;

        if (!members)
            return nullptr;

        for (ObjectGuid const& guid : *members)
        {
            Player* member =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!member)
                continue;

            Group* memberGroup =
                member->GetGroup();

            if (memberGroup &&
                memberGroup->IsMember(
                    guid
                ))
            {
                return memberGroup;
            }
        }

        return nullptr;
    }

    bool IsSabotageTeamRaidBoundToMatch(
        Group* group,
        SabotagePreparedMatch const& match
    )
    {
        if (!group ||
            !match.instanceSave)
        {
            return false;
        }

        InstanceGroupBind* bind =
            group->GetBoundInstance(
                DUNGEON_DIFFICULTY_NORMAL,
                MAP_STRATHOLME
            );

        return bind &&
            bind->save &&
            bind->save->GetInstanceId() ==
                match.instanceId;
    }

    bool IsSabotageTeamRaidHealthy(
        SabotagePreparedMatch const& match,
        SabotageAssignedTeam team
    )
    {
        std::vector<ObjectGuid> connectedMembers =
            BuildConnectedSabotageTeamMembers(
                match,
                team
            );

        if (connectedMembers.empty())
            return true;

        Group* group =
            FindConnectedSabotageTeamGroup(
                match,
                team
            );

        if (!group ||
            !group->isRaidGroup() ||
            !IsSabotageTeamRaidBoundToMatch(
                group,
                match
            ))
        {
            return false;
        }

        for (ObjectGuid const& guid :
             connectedMembers)
        {
            Player* member =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!member ||
                member->GetGroup() != group ||
                !group->IsMember(
                    guid
                ))
            {
                return false;
            }
        }

        return true;
    }

    void ValidateSabotageTeamInstanceMembers(
        Group* group,
        SabotagePreparedMatch const& match,
        SabotageAssignedTeam team
    )
    {
        if (!group)
            return;

        std::vector<ObjectGuid> const* members =
            nullptr;

        if (team == SABOTAGE_TEAM_A)
            members = &match.teamA;
        else if (team == SABOTAGE_TEAM_B)
            members = &match.teamB;

        if (!members)
            return;

        for (ObjectGuid const& guid : *members)
        {
            Player* member =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!member ||
                member->GetGroup() != group ||
                !group->IsMember(
                    guid
                ))
            {
                continue;
            }

            member->m_InstanceValid = true;
            member->UpdateHomebindTime(0);
            member->SetGroupUpdateFlag(
                GROUP_UPDATE_FULL
            );
        }
    }

    Group* RepairSabotageTeamRaid(
        SabotagePreparedMatch& match,
        SabotageAssignedTeam team
    )
    {
        std::vector<ObjectGuid> connectedMembers =
            BuildConnectedSabotageTeamMembers(
                match,
                team
            );

        if (connectedMembers.empty())
            return nullptr;

        SabotageCompetitorSide competitorSide =
            GetSabotageCompetitorSideForTeam(
                match,
                team
            );

        Group* group =
            FindConnectedSabotageTeamGroup(
                match,
                team
            );

        if (!group)
        {
            group =
                RebuildSabotageTeamRaid(
                    match,
                    team,
                    competitorSide,
                    ObjectGuid::Empty
                );

            if (!group)
            {
                TC_LOG_ERROR(
                    "custom.scourge",
                    "Failed to recreate missing sabotage team raid: match={}, team={}, connectedMembers={}",
                    match.matchId,
                    uint32(team),
                    connectedMembers.size()
                );

                return nullptr;
            }
        }
        else
        {
            if (!group->isRaidGroup())
            {
                group->ConvertToRaid();
            }

            for (ObjectGuid const& guid :
                 connectedMembers)
            {
                Player* member =
                    ObjectAccessor::FindConnectedPlayer(
                        guid
                    );

                if (!member)
                    continue;

                if (member->GetGroup() == group &&
                    group->IsMember(
                        guid
                    ))
                {
                    continue;
                }

                Group* oldGroup =
                    member->GetGroup();

                if (oldGroup &&
                    oldGroup != group)
                {
                    /*
                     * A previous failed recovery can leave team members
                     * in separate one-member raids. Remove the stale raid
                     * membership before adding the member to the selected
                     * active team raid.
                     *
                     * GROUP_REMOVEMETHOD_DEFAULT does not trigger the
                     * manual sabotage-leave path.
                     */
                    member->RemoveFromGroup(
                        GROUP_REMOVEMETHOD_DEFAULT
                    );
                }

                if (member->GetGroup() ||
                    group->IsFull() ||
                    !group->AddMember(
                        member
                    ))
                {
                    TC_LOG_ERROR(
                        "custom.scourge",
                        "Failed to merge sabotage team raid member: match={}, team={}, player={}, guid={}, raid={}",
                        match.matchId,
                        uint32(team),
                        member->GetName(),
                        guid.GetCounter(),
                        group->GetGUID().GetCounter()
                    );

                    continue;
                }
            }

            StoreSabotageTeamGroupGuid(
                match,
                team,
                competitorSide,
                group->GetGUID()
            );

            RefreshSabotageTeamRaidMembers(
                group,
                match,
                team
            );
        }

        ValidateSabotageTeamInstanceMembers(
            group,
            match,
            team
        );

        group->SendUpdate();

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage team raid repaired: match={}, team={}, raid={}, members={}, instance={}",
            match.matchId,
            uint32(team),
            group->GetGUID().GetCounter(),
            group->GetMembersCount(),
            match.instanceId
        );

        return group;
    }

    void MaintainSabotageTeamRaids(
        uint32 diff
    )
    {
        static uint32 maintenanceTimerMs = 0;

        if (maintenanceTimerMs > diff)
        {
            maintenanceTimerMs -= diff;
            return;
        }

        maintenanceTimerMs = 250;

        for (auto& pair :
             SabotagePreparedMatches)
        {
            SabotagePreparedMatch& match =
                pair.second;

            if (!match.launched ||
                !match.instanceId ||
                !match.instanceSave)
            {
                continue;
            }

            SabotageAssignedTeam const teams[] =
            {
                SABOTAGE_TEAM_A,
                SABOTAGE_TEAM_B
            };

            for (SabotageAssignedTeam team :
                 teams)
            {
                if (!IsSabotageTeamRaidHealthy(
                        match,
                        team
                    ))
                {
                    RepairSabotageTeamRaid(
                        match,
                        team
                    );

                    continue;
                }

                Group* group =
                    FindConnectedSabotageTeamGroup(
                        match,
                        team
                    );

                ValidateSabotageTeamInstanceMembers(
                    group,
                    match,
                    team
                );
            }
        }
    }

    bool AddPlayerToActiveSabotageMatch(
        Player* player,
        SabotagePreparedMatch& match,
        SabotageAssignedTeam team
    )
    {
        if (!player ||
            !match.launched ||
            !match.instanceSave ||
            match.roundPhase ==
                SABOTAGE_ROUND_PHASE_FINISHED ||
            team == SABOTAGE_TEAM_NONE)
        {
            return false;
        }

        ObjectGuid playerGuid =
            player->GetGUID();

        uint32 guidLow =
            GetCharacterGuid(player);

        if (!guidLow)
            return false;

        SabotageCompetitorSide competitorSide =
            GetSabotageCompetitorSideForTeam(
                match,
                team
            );

        if (competitorSide ==
            SABOTAGE_COMPETITOR_NONE)
        {
            return false;
        }

        if (player->GetGroup())
        {
            SendSabotageMessage(
                player,
                "진행 중인 폭파전 공격대에 합류하기 위해 "
                "현재 파티 또는 공격대에서 분리됩니다."
            );

            player->RemoveFromGroup();
        }

        player->UnbindInstance(
            MAP_STRATHOLME,
            DUNGEON_DIFFICULTY_NORMAL
        );

        Group* group =
            EnsureSabotageActiveJoinRaid(
                player,
                match,
                team,
                competitorSide
            );

        if (!group)
        {
            SendSabotageMessage(
                player,
                "진행 중인 폭파전 공격대 구성에 실패했습니다."
            );

            return false;
        }

        SabotagePlayerQueueState state;

        auto existingStateItr =
            SabotageQueueStates.find(
                guidLow
            );

        if (existingStateItr !=
            SabotageQueueStates.end())
        {
            state =
                existingStateItr->second;
        }

        StoreSabotageQueueReturnPosition(
            player,
            state
        );

        state.queueType = match.queueType;
        state.matched = true;
        state.matchId = match.matchId;
        state.assignedTeam = team;
        state.competitorSide = competitorSide;
        state.faction = player->GetTeam();

        SabotageQueueStates[guidLow] = state;

        std::vector<ObjectGuid>& members =
            team == SABOTAGE_TEAM_A
                ? match.teamA
                : match.teamB;

        members.push_back(playerGuid);

        std::vector<ObjectGuid>& competitorMembers =
            competitorSide == SABOTAGE_COMPETITOR_ONE
                ? match.sideOne
                : match.sideTwo;

        competitorMembers.push_back(playerGuid);

        RefreshSabotageTeamRaidMembers(
            group,
            match,
            team
        );

        bool teleported =
            TeleportSabotagePlayer(
                player,
                match.instanceSave,
                match,
                team
            );

        if (!teleported)
        {
            members.erase(
                std::remove(
                    members.begin(),
                    members.end(),
                    playerGuid
                ),
                members.end()
            );

            competitorMembers.erase(
                std::remove(
                    competitorMembers.begin(),
                    competitorMembers.end(),
                    playerGuid
                ),
                competitorMembers.end()
            );

            SabotageQueueStates.erase(guidLow);

            RestoreSabotageCombatState(
                player
            );

            if (player->GetGroup() == group &&
                group->IsMember(
                    playerGuid
                ))
            {
                group->RemoveMember(
                    playerGuid,
                    GROUP_REMOVEMETHOD_DEFAULT
                );
            }

            return false;
        }

        for (ObjectGuid const& guid : match.teamA)
        {
            if (Player* member =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                RefreshSabotageFreeForAll(member);
                member->UpdateObjectVisibility(true);
            }
        }

        for (ObjectGuid const& guid : match.teamB)
        {
            if (Player* member =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                RefreshSabotageFreeForAll(member);
                member->UpdateObjectVisibility(true);
            }
        }

        RefreshSabotageTeamRaidMembers(
            group,
            match,
            team
        );

        SendSabotageHudStateToPlayer(
            player,
            match
        );

        std::ostringstream text;
        text
            << "진행 중인 경기 #"
            << match.matchId
            << " "
            << GetSabotageTeamName(team)
            << "에 즉시 합류했습니다.";

        SendSabotageMessage(
            player,
            text.str()
        );

        TC_LOG_INFO(
            "custom.scourge",
            "Player joined active sabotage match: player={}, match={}, team={}, instance={}",
            player->GetName(),
            match.matchId,
            uint32(team),
            match.instanceId
        );

        return true;
    }

    bool TryJoinActiveSabotageMatch(
        Player* player,
        SabotageQueueType queueType,
        uint32 faction
    )
    {
        SabotagePreparedMatch* match =
            FindActiveSabotageMatch(
                queueType,
                faction
            );

        if (!match)
            return false;

        SabotageAssignedTeam team =
            SelectActiveSabotageTeam(
                *match,
                faction
            );

        if (team == SABOTAGE_TEAM_NONE)
            return false;

        RemoveGuidFromAllSabotageQueues(
            player->GetGUID()
        );

        return AddPlayerToActiveSabotageMatch(
            player,
            *match,
            team
        );
    }

    void FillActiveSabotageMatchFromQueue(
        SabotageQueueType queueType,
        uint32 faction
    )
    {
        SabotagePreparedMatch* match =
            FindActiveSabotageMatch(
                queueType,
                faction
            );

        if (!match)
            return;

        if (queueType ==
            SABOTAGE_QUEUE_FACTION_WAR)
        {
            CleanSabotageQueue(
                SabotageFactionAllianceQueue
            );

            CleanSabotageQueue(
                SabotageFactionHordeQueue
            );

            while (
                match->sideOne.size() <
                    SABOTAGE_MAX_TEAM_SIZE &&
                !SabotageFactionAllianceQueue.empty()
            )
            {
                ObjectGuid guid =
                    SabotageFactionAllianceQueue.front();

                SabotageFactionAllianceQueue.pop_front();

                Player* player =
                    ObjectAccessor::FindConnectedPlayer(guid);

                SabotageAssignedTeam team =
                    GetSabotageAssignedTeamForCompetitor(
                        *match,
                        SABOTAGE_COMPETITOR_ONE
                    );

                if (!player ||
                    !AddPlayerToActiveSabotageMatch(
                        player,
                        *match,
                        team
                    ))
                {
                    SabotageQueueStates.erase(
                        uint32(guid.GetCounter())
                    );

                    break;
                }
            }

            while (
                match->sideTwo.size() <
                    SABOTAGE_MAX_TEAM_SIZE &&
                !SabotageFactionHordeQueue.empty()
            )
            {
                ObjectGuid guid =
                    SabotageFactionHordeQueue.front();

                SabotageFactionHordeQueue.pop_front();

                Player* player =
                    ObjectAccessor::FindConnectedPlayer(guid);

                SabotageAssignedTeam team =
                    GetSabotageAssignedTeamForCompetitor(
                        *match,
                        SABOTAGE_COMPETITOR_TWO
                    );

                if (!player ||
                    !AddPlayerToActiveSabotageMatch(
                        player,
                        *match,
                        team
                    ))
                {
                    SabotageQueueStates.erase(
                        uint32(guid.GetCounter())
                    );

                    break;
                }
            }

            return;
        }

        std::deque<ObjectGuid>* queue =
            GetSabotageQueueContainer(
                queueType,
                faction
            );

        if (!queue)
            return;

        CleanSabotageQueue(*queue);

        while (!queue->empty())
        {
            SabotageAssignedTeam team =
                SelectActiveSabotageTeam(
                    *match,
                    faction
                );

            if (team == SABOTAGE_TEAM_NONE)
                break;

            ObjectGuid guid = queue->front();
            queue->pop_front();

            Player* player =
                ObjectAccessor::FindConnectedPlayer(guid);

            if (!player ||
                !AddPlayerToActiveSabotageMatch(
                    player,
                    *match,
                    team
                ))
            {
                SabotageQueueStates.erase(
                    uint32(guid.GetCounter())
                );

                break;
            }
        }
    }

    void RestoreFailedSabotageMatchToQueue(
        SabotagePreparedMatch const& match
    )
    {
        for (ObjectGuid const& guid : match.teamA)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                if (player->GetGroup() &&
                    player->GetGroup()->GetGUID() ==
                        match.teamAGroupGuid)
                {
                    player->RemoveFromGroup();
                }
            }
        }

        for (ObjectGuid const& guid : match.teamB)
        {
            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                if (player->GetGroup() &&
                    player->GetGroup()->GetGUID() ==
                        match.teamBGroupGuid)
                {
                    player->RemoveFromGroup();
                }
            }
        }

        std::vector<ObjectGuid> members;
        members.insert(
            members.end(),
            match.teamA.begin(),
            match.teamA.end()
        );

        members.insert(
            members.end(),
            match.teamB.begin(),
            match.teamB.end()
        );

        for (ObjectGuid const& guid : members)
        {
            uint32 guidLow =
                uint32(guid.GetCounter());

            auto stateItr =
                SabotageQueueStates.find(guidLow);

            if (stateItr ==
                SabotageQueueStates.end())
            {
                continue;
            }

            SabotagePlayerQueueState& state =
                stateItr->second;

            state.matched = false;
            state.matchId = 0;
            state.assignedTeam =
                SABOTAGE_TEAM_NONE;
            state.competitorSide =
                SABOTAGE_COMPETITOR_NONE;

            std::deque<ObjectGuid>* queue =
                GetSabotageQueueContainer(
                    state.queueType,
                    state.faction
                );

            if (queue)
                queue->push_front(guid);

            if (Player* player =
                ObjectAccessor::FindConnectedPlayer(guid))
            {
                SendSabotageMessage(
                    player,
                    "경기 인스턴스 생성에 실패하여 "
                    "대기열로 복귀했습니다."
                );
            }
        }
    }

    bool StoreSabotagePreparedMatch(
        SabotagePreparedMatch& match
    )
    {
        match.sideOne = match.teamA;
        match.sideTwo = match.teamB;
        match.sideOneAttacking = true;
        match.currentRound = 0;
        match.sideOneScore = 0;
        match.sideTwoScore = 0;
        match.roundPhase =
            SABOTAGE_ROUND_PHASE_WAITING;
        match.roundBreakTimerMs =
            SABOTAGE_FIRST_ROUND_WAIT_MS;
        match.hudUpdateTimerMs = 0;

        SelectSabotageRoundStartPositions(
            match,
            false
        );

        SabotageCreatedTeamGroups groups;

        if (!CreateSabotageTeamGroups(
            match,
            groups
        ))
        {
            RestoreFailedSabotageMatchToQueue(
                match
            );

            return false;
        }

        InstanceSave* save = nullptr;

        if (!CreateSabotageSharedInstance(
            match,
            save
        ))
        {
            DisbandSabotageTeamGroups(
                groups
            );

            RestoreFailedSabotageMatchToQueue(
                match
            );

            return false;
        }

        groups.teamA->BindToInstance(
            save,
            false
        );

        groups.teamB->BindToInstance(
            save,
            false
        );

        for (ObjectGuid const& guid : match.teamA)
        {
            SetSabotageMatchedState(
                guid,
                match,
                SABOTAGE_TEAM_A,
                SABOTAGE_COMPETITOR_ONE
            );
        }

        for (ObjectGuid const& guid : match.teamB)
        {
            SetSabotageMatchedState(
                guid,
                match,
                SABOTAGE_TEAM_B,
                SABOTAGE_COMPETITOR_TWO
            );
        }

        SabotagePreparedMatches[
            match.matchId
        ] = match;

        RegisterActiveSabotageMatch(
            match
        );

        NotifySabotageMatch(match);
        LaunchSabotageMatch(match, save);

        FillActiveSabotageMatchFromQueue(
            match.queueType,
            match.faction
        );

        return true;
    }

    bool TryCreateFactionWarMatch()
    {
        if (FindActiveSabotageMatch(
            SABOTAGE_QUEUE_FACTION_WAR,
            0
        ))
        {
            return false;
        }

        CleanSabotageQueue(
            SabotageFactionAllianceQueue
        );

        CleanSabotageQueue(
            SabotageFactionHordeQueue
        );

        uint32 teamSize = std::min(
            uint32(SABOTAGE_MAX_TEAM_SIZE),
            uint32(std::min(
                SabotageFactionAllianceQueue.size(),
                SabotageFactionHordeQueue.size()
            ))
        );

        if (teamSize < SABOTAGE_MIN_TEAM_SIZE)
            return false;

        SabotagePreparedMatch match;
        match.matchId = NextSabotageMatchId++;
        match.queueType = SABOTAGE_QUEUE_FACTION_WAR;
        match.faction = 0;

        for (uint32 index = 0;
             index < teamSize;
             ++index)
        {
            match.teamA.push_back(
                SabotageFactionAllianceQueue.front()
            );

            SabotageFactionAllianceQueue.pop_front();

            match.teamB.push_back(
                SabotageFactionHordeQueue.front()
            );

            SabotageFactionHordeQueue.pop_front();
        }

        return StoreSabotagePreparedMatch(
            match
        );
    }

    bool TryCreateSameFactionMatch(uint32 faction)
    {
        if (FindActiveSabotageMatch(
            SABOTAGE_QUEUE_SAME_FACTION,
            faction
        ))
        {
            return false;
        }

        std::deque<ObjectGuid>* queue =
            GetSabotageQueueContainer(
                SABOTAGE_QUEUE_SAME_FACTION,
                faction
            );

        if (!queue)
            return false;

        CleanSabotageQueue(*queue);

        uint32 totalPlayers = std::min(
            uint32(queue->size()),
            SABOTAGE_MAX_TEAM_SIZE * 2
        );

        if ((totalPlayers % 2) != 0)
            --totalPlayers;

        if (totalPlayers <
            SABOTAGE_MIN_TEAM_SIZE * 2)
        {
            return false;
        }

        SabotagePreparedMatch match;
        match.matchId = NextSabotageMatchId++;
        match.queueType =
            SABOTAGE_QUEUE_SAME_FACTION;
        match.faction = faction;

        for (uint32 index = 0;
             index < totalPlayers;
             ++index)
        {
            ObjectGuid guid = queue->front();
            queue->pop_front();

            if ((index % 2) == 0)
                match.teamA.push_back(guid);
            else
                match.teamB.push_back(guid);
        }

        return StoreSabotagePreparedMatch(
            match
        );
    }

    void TryCreateSabotageMatches()
    {
        while (TryCreateFactionWarMatch())
        {
        }

        while (TryCreateSameFactionMatch(
            ALLIANCE
        ))
        {
        }

        while (TryCreateSameFactionMatch(
            HORDE
        ))
        {
        }
    }

    void RequeueSabotagePlayer(
        ObjectGuid guid,
        SabotageQueueType queueType,
        uint32 faction
    )
    {
        Player* player =
            ObjectAccessor::FindConnectedPlayer(guid);

        if (!player)
        {
            SabotageQueueStates.erase(
                uint32(guid.GetCounter())
            );

            return;
        }

        std::deque<ObjectGuid>* queue =
            GetSabotageQueueContainer(
                queueType,
                faction
            );

        if (!queue)
        {
            SabotageQueueStates.erase(
                uint32(guid.GetCounter())
            );

            return;
        }

        uint32 guidLow =
            uint32(
                guid.GetCounter()
            );

        SabotagePlayerQueueState state;

        auto existingStateItr =
            SabotageQueueStates.find(
                guidLow
            );

        if (existingStateItr !=
            SabotageQueueStates.end())
        {
            state =
                existingStateItr->second;
        }

        StoreSabotageQueueReturnPosition(
            player,
            state
        );

        state.queueType = queueType;
        state.matched = false;
        state.matchId = 0;
        state.assignedTeam =
            SABOTAGE_TEAM_NONE;
        state.competitorSide =
            SABOTAGE_COMPETITOR_NONE;
        state.faction = faction;

        SabotageQueueStates[
            guidLow
        ] = state;

        queue->push_back(guid);

        SendSabotageMessage(
            player,
            "팀 구성이 취소되어 기존 대기열로 복귀했습니다."
        );
    }

    void DissolveSabotagePreparedMatch(
        uint32 matchId,
        ObjectGuid removedGuid,
        bool notifyRemoved
    )
    {
        auto matchItr =
            SabotagePreparedMatches.find(matchId);

        if (matchItr ==
            SabotagePreparedMatches.end())
        {
            SabotageQueueStates.erase(
                uint32(removedGuid.GetCounter())
            );

            return;
        }

        SabotagePreparedMatch match =
            matchItr->second;

        ClearActiveSabotageMatch(
            matchId
        );

        DeleteSabotageDefenderScoutObject(
            matchItr->second
        );

        ClearSabotageDefenderScoutStates(
            matchItr->second
        );

        ClearSabotageSmokeGrenadeItems(
            matchItr->second
        );

        DeleteSabotageSmokeObject(
            matchItr->second
        );

        DeleteSabotageRoutePortalObjects(
            matchItr->second
        );

        DespawnSabotageBombTargets(
            matchItr->second
        );

        SabotagePreparedMatches.erase(
            matchItr
        );

        std::vector<ObjectGuid> members;
        members.insert(
            members.end(),
            match.teamA.begin(),
            match.teamA.end()
        );

        members.insert(
            members.end(),
            match.teamB.begin(),
            match.teamB.end()
        );

        for (ObjectGuid const& guid : members)
        {
            if (guid == removedGuid)
            {
                SabotageQueueStates.erase(
                    uint32(guid.GetCounter())
                );

                if (notifyRemoved)
                {
                    if (Player* player =
                        ObjectAccessor::FindConnectedPlayer(
                            guid
                        ))
                    {
                        SendSabotageMessage(
                            player,
                            "폭파전 신청을 취소했습니다."
                        );
                    }
                }

                continue;
            }

            RequeueSabotagePlayer(
                guid,
                match.queueType,
                match.faction
            );
        }

        TryCreateSabotageMatches();
    }

    bool JoinSabotageQueue(
        Player* player,
        SabotageQueueType queueType
    )
    {
        if (!player)
            return false;

        uint32 guidLow =
            GetCharacterGuid(player);

        if (!guidLow)
            return false;

        if (!CustomScourgeDungeonMode::IsEnabled(
            player
        ))
        {
            SendSabotageMessage(
                player,
                "폭파전 룬을 활성화한 뒤 신청해야 합니다."
            );

            return false;
        }

        auto stateItr =
            SabotageQueueStates.find(guidLow);

        if (stateItr !=
            SabotageQueueStates.end())
        {
            std::ostringstream text;

            if (stateItr->second.matched)
            {
                text
                    << "이미 경기 #"
                    << stateItr->second.matchId
                    << " "
                    << GetSabotageTeamName(
                        stateItr->second.assignedTeam
                    )
                    << "으로 구성되었습니다.";
            }
            else
            {
                text
                    << "이미 "
                    << GetSabotageQueueName(
                        stateItr->second.queueType
                    )
                    << " 대기열에 참가 중입니다.";
            }

            SendSabotageMessage(
                player,
                text.str()
            );

            return false;
        }

        uint32 faction = player->GetTeam();

        if (faction != ALLIANCE &&
            faction != HORDE)
        {
            SendSabotageMessage(
                player,
                "진영 정보를 확인할 수 없습니다."
            );

            return false;
        }

        if (TryJoinActiveSabotageMatch(
            player,
            queueType,
            faction
        ))
        {
            SendSabotageQueueApplicationNotification(
                player,
                queueType,
                faction,
                true
            );

            return true;
        }

        std::deque<ObjectGuid>* queue =
            GetSabotageQueueContainer(
                queueType,
                faction
            );

        if (!queue)
            return false;

        SabotagePlayerQueueState state;

        StoreSabotageQueueReturnPosition(
            player,
            state
        );

        state.queueType = queueType;
        state.faction = faction;

        SabotageQueueStates[guidLow] = state;
        queue->push_back(player->GetGUID());

        std::ostringstream text;

        SabotagePreparedMatch* activeMatch =
            FindActiveSabotageMatch(
                queueType,
                faction
            );

        if (activeMatch)
        {
            text
                << GetSabotageQueueName(queueType)
                << " 경기가 진행 중이지만 현재 팀이 가득 찼습니다. "
                << "대기 인원: "
                << queue->size()
                << "명.";
        }
        else
        {
            text
                << GetSabotageQueueName(queueType)
                << " 신청이 완료되었습니다. "
                << GetSabotageFactionName(faction)
                << " 대기 인원: "
                << queue->size()
                << "명.";
        }

        SendSabotageMessage(
            player,
            text.str()
        );

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage queue joined: player={}, guid={}, mode={}, faction={}",
            player->GetName(),
            guidLow,
            uint32(queueType),
            faction
        );

        SendSabotageQueueApplicationNotification(
            player,
            queueType,
            faction,
            false
        );

        TryCreateSabotageMatches();
        return true;
    }

    void CancelSabotageQueue(
        Player* player,
        bool notify
    )
    {
        if (!player)
            return;

        uint32 guidLow =
            GetCharacterGuid(player);

        auto stateItr =
            SabotageQueueStates.find(guidLow);

        if (stateItr ==
            SabotageQueueStates.end())
        {
            if (notify)
            {
                SendSabotageMessage(
                    player,
                    "현재 참가 중인 대기열이 없습니다."
                );
            }

            return;
        }

        SabotagePlayerQueueState state =
            stateItr->second;

        if (state.matched)
        {
            auto matchItr =
                SabotagePreparedMatches.find(
                    state.matchId
                );

            if (matchItr !=
                    SabotagePreparedMatches.end() &&
                matchItr->second.launched)
            {
                if (notify)
                {
                    SendSabotageMessage(
                        player,
                        "이미 경기 인스턴스에 입장하여 "
                        "NPC에서 신청을 취소할 수 없습니다."
                    );
                }

                return;
            }

            DissolveSabotagePreparedMatch(
                state.matchId,
                player->GetGUID(),
                notify
            );

            return;
        }

        RemoveGuidFromAllSabotageQueues(
            player->GetGUID()
        );

        SabotageQueueStates.erase(
            stateItr
        );

        if (notify)
        {
            SendSabotageMessage(
                player,
                "폭파전 신청을 취소했습니다."
            );
        }

        TryCreateSabotageMatches();
    }

    void RemoveSabotageQueueGuid(
        ObjectGuid guid
    )
    {
        ClearSabotagePendingBombAction(
            guid
        );

        if (Player* player =
            ObjectAccessor::FindConnectedPlayer(guid))
        {
            HideSabotageHud(player);

            RemoveSabotageMountSupport(
                player
            );
        }

        uint32 guidLow =
            uint32(guid.GetCounter());

        SabotagePendingPartyLeaveGuids.erase(
            guidLow
        );

        SabotagePendingEntryTransferMs.erase(
            guidLow
        );

        SabotagePendingHearthstoneExitMs.erase(
            guidLow
        );

        auto stateItr =
            SabotageQueueStates.find(guidLow);

        if (stateItr ==
            SabotageQueueStates.end())
        {
            RemoveGuidFromAllSabotageQueues(guid);
            return;
        }

        SabotagePlayerQueueState state =
            stateItr->second;

        if (state.matched)
        {
            auto matchItr =
                SabotagePreparedMatches.find(
                    state.matchId
                );

            if (matchItr !=
                    SabotagePreparedMatches.end() &&
                matchItr->second.launched)
            {
                SabotagePreparedMatch& match =
                    matchItr->second;

                if (match.bombCarrierGuid == guid)
                {
                    Player* carrier =
                        ObjectAccessor::FindConnectedPlayer(
                            guid
                        );

                    bool returned =
                        ReturnSabotageBombToTeamAStart(
                            carrier,
                            match
                        );

                    SendSabotageMatchMessage(
                        match,
                        returned
                            ? "폭탄 운반자가 경기에서 이탈하여 "
                              "폭탄이 A팀 시작점으로 반환되었습니다."
                            : "폭탄 운반자가 경기에서 이탈하여 "
                              "폭탄 상태가 초기화되었습니다."
                    );
                }

                match.teamA.erase(
                    std::remove(
                        match.teamA.begin(),
                        match.teamA.end(),
                        guid
                    ),
                    match.teamA.end()
                );

                match.teamB.erase(
                    std::remove(
                        match.teamB.begin(),
                        match.teamB.end(),
                        guid
                    ),
                    match.teamB.end()
                );

                match.sideOne.erase(
                    std::remove(
                        match.sideOne.begin(),
                        match.sideOne.end(),
                        guid
                    ),
                    match.sideOne.end()
                );

                match.sideTwo.erase(
                    std::remove(
                        match.sideTwo.begin(),
                        match.sideTwo.end(),
                        guid
                    ),
                    match.sideTwo.end()
                );

                SabotageQueueStates.erase(
                    stateItr
                );

                SabotageQueueType queueType =
                    match.queueType;

                uint32 matchFaction =
                    match.faction;

                if (match.sideOne.empty() &&
                    match.sideTwo.empty())
                {
                    ClearActiveSabotageMatch(
                        match.matchId
                    );

                    DeleteSabotageDefenderScoutObject(
                        match
                    );

                    ClearSabotageDefenderScoutStates(
                        match
                    );

                    ClearSabotageSmokeGrenadeItems(
                        match
                    );

                    DeleteSabotageSmokeObject(
                        match
                    );

                    DeleteSabotageRoutePortalObjects(
                        match
                    );

                    DespawnSabotageBombTargets(
                        match
                    );

                    SabotagePreparedMatches.erase(
                        matchItr
                    );
                }
                else
                {
                    FillActiveSabotageMatchFromQueue(
                        queueType,
                        matchFaction
                    );
                }

                return;
            }

            DissolveSabotagePreparedMatch(
                state.matchId,
                guid,
                false
            );

            return;
        }

        RemoveGuidFromAllSabotageQueues(guid);
        SabotageQueueStates.erase(stateItr);

        TryCreateSabotageMatches();
    }

    void SendSabotageTerminalExitState(
        Player* player
    )
    {
        if (!player)
            return;

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SABOTAGE:EXIT",
            player
        );

        HideSabotageHud(
            player
        );

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SPECTATOR:0",
            player
        );
    }

    void ResetSabotagePlayerAfterManualLeave(
        Player* player
    )
    {
        if (!player)
            return;

        uint32 guidLow =
            GetCharacterGuid(player);

        DisableSabotageDefenderScout(
            player,
            false
        );

        RestoreSabotageTemporaryMovementSpeed(
            player
        );

        ClearSabotagePendingBombAction(
            player->GetGUID()
        );

        player->CombatStopWithPets();
        player->AttackStop();
        player->InterruptNonMeleeSpells(
            true
        );
        player->SetSelection(
            ObjectGuid::Empty
        );

        player->SetControlled(
            false,
            UNIT_STATE_ROOT
        );

        /*
         * Spell 900208 can trigger a mounted aura.
         * Removing only 900208 does not guarantee that the actual
         * mounted state disappears, so clear both forms explicitly.
         */
        player->RemoveAurasByType(
            SPELL_AURA_MOUNTED
        );
        player->Dismount();

        RemoveSabotageBombCarrierFlag(
            player
        );

        RemoveSabotageMountSupport(
            player
        );

        uint32 bombCount =
            player->GetItemCount(
                SABOTAGE_BOMB_ITEM,
                true
            );

        if (bombCount)
        {
            player->DestroyItemCount(
                SABOTAGE_BOMB_ITEM,
                bombCount,
                true
            );
        }

        uint32 smokeGrenadeCount =
            player->GetItemCount(
                SABOTAGE_SMOKE_GRENADE_ITEM,
                true
            );

        if (smokeGrenadeCount)
        {
            player->DestroyItemCount(
                SABOTAGE_SMOKE_GRENADE_ITEM,
                smokeGrenadeCount,
                true
            );
        }

        bool combatStateRestored =
            RestoreSabotageCombatState(
                player
            );

        if (!combatStateRestored)
        {
            player->RemovePvpFlag(
                UNIT_BYTE2_FLAG_FFA_PVP
            );

            player->RestoreFaction();

            for (Unit* controlled :
                 player->m_Controlled)
            {
                if (!controlled)
                    continue;

                controlled->RestoreFaction();
                controlled->RemovePvpFlag(
                    UNIT_BYTE2_FLAG_FFA_PVP
                );
            }
        }

        SetScourgeSabotageParticipant(
            player,
            false
        );

        /*
         * A dead player leaving sabotage must return to TrinityCore's
         * normal ghost visibility. Otherwise the expanded spectator
         * detection mask would remain active outside the match.
         */
        if (!player->IsAlive())
        {
            player->m_serverSideVisibilityDetect.SetValue(
                SERVERSIDE_VISIBILITY_GHOST,
                GHOST_VISIBILITY_GHOST
            );
        }

        player->SetPhaseMask(
            NORMAL_PHASE_MASK,
            true
        );

        if (guidLow)
        {
            ScourgeModePhasePlayers.erase(
                guidLow
            );
        }

        player->m_InstanceValid = true;
        player->UpdateHomebindTime(0);
        player->UpdateObjectVisibility(
            true
        );

        SendSabotageTerminalExitState(
            player
        );
    }

    bool ExitSabotagePlayerAfterExternalTransfer(
        Player* player,
        char const* reason
    )
    {
        if (!player)
            return false;

        uint32 guidLow =
            GetCharacterGuid(
                player
            );

        if (!guidLow)
            return false;

        auto stateItr =
            SabotageQueueStates.find(
                guidLow
            );

        bool hasQueueState =
            stateItr !=
                SabotageQueueStates.end();

        SabotagePlayerQueueState returnState;

        if (hasQueueState)
        {
            returnState =
                stateItr->second;
        }

        bool hearthstoneExit = false;

        auto hearthstoneItr =
            SabotagePendingHearthstoneExitMs.find(
                guidLow
            );

        if (hearthstoneItr !=
                SabotagePendingHearthstoneExitMs.end() &&
            player->GetMapId() ==
                player->m_homebindMapId)
        {
            hearthstoneExit = true;
        }

        SabotagePendingHearthstoneExitMs.erase(
            guidLow
        );

        bool wasParticipant =
            IsScourgeSabotageParticipant(
                player
            );

        if (!hasQueueState &&
            !wasParticipant)
        {
            return false;
        }

        ObjectGuid expectedGroupGuid =
            ObjectGuid::Empty;

        if (hasQueueState &&
            stateItr->second.matched &&
            stateItr->second.matchId)
        {
            auto matchItr =
                SabotagePreparedMatches.find(
                    stateItr->second.matchId
                );

            if (matchItr !=
                SabotagePreparedMatches.end())
            {
                expectedGroupGuid =
                    GetSabotageTeamGroupGuid(
                        matchItr->second,
                        stateItr->second.assignedTeam
                    );
            }
        }

        if (expectedGroupGuid.IsEmpty())
        {
            auto restoreItr =
                SabotageCombatRestoreStates.find(
                    guidLow
                );

            if (restoreItr !=
                SabotageCombatRestoreStates.end())
            {
                expectedGroupGuid =
                    restoreItr->second.sabotageGroupGuid;
            }
        }

        SabotagePendingEntryTransferMs.erase(
            guidLow
        );

        SabotagePendingPostTeleportResetGuids.erase(
            guidLow
        );

        RemoveSabotageQueueGuid(
            player->GetGUID()
        );

        ResetSabotagePlayerAfterManualLeave(
            player
        );

        Group* group =
            player->GetGroup();

        if (group &&
            !expectedGroupGuid.IsEmpty() &&
            group->GetGUID() ==
                expectedGroupGuid)
        {
            player->RemoveFromGroup(
                GROUP_REMOVEMETHOD_DEFAULT
            );
        }

        player->m_InstanceValid = true;
        player->UpdateHomebindTime(0);
        player->UpdateObjectVisibility(
            true
        );

        bool returned = true;

        if (hearthstoneExit)
        {
            /*
             * The Hearthstone already placed the player at the homebind.
             * Only clear the sabotage state. Do not teleport again.
             */
            SabotagePendingPostTeleportResetGuids.erase(
                guidLow
            );

            TC_LOG_INFO(
                "custom.scourge",
                "Sabotage Hearthstone destination preserved: player={}, guid={}, map={}, instance={}",
                player->GetName(),
                guidLow,
                player->GetMapId(),
                player->GetInstanceId()
            );
        }
        else
        {
            SabotagePendingPostTeleportResetGuids.insert(
                guidLow
            );

            returned =
                TeleportSabotagePlayerToQueueReturnPosition(
                    player,
                    returnState
                );

            if (!returned)
            {
                SabotagePendingPostTeleportResetGuids.erase(
                    guidLow
                );
            }
        }

        if (reason &&
            reason[0] != '\0')
        {
            SendSabotageMessage(
                player,
                reason
            );
        }

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage external transfer exit completed: player={}, guid={}, map={}, instance={}, groupRemoved={}, returned={}, hearthstone={}, reason={}",
            player->GetName(),
            guidLow,
            player->GetMapId(),
            player->GetInstanceId(),
            player->GetGroup() ? 0 : 1,
            returned ? 1 : 0,
            hearthstoneExit ? 1 : 0,
            reason ? reason : ""
        );

        TryCreateSabotageMatches();
        return true;
    }

    void UpdateSabotageHearthstoneExitMarkers(
        uint32 diff
    )
    {
        for (auto itr =
                 SabotagePendingHearthstoneExitMs.begin();
             itr !=
                 SabotagePendingHearthstoneExitMs.end();)
        {
            if (itr->second > diff)
            {
                itr->second -= diff;
                ++itr;
                continue;
            }

            itr =
                SabotagePendingHearthstoneExitMs.erase(
                    itr
                );
        }
    }

    void ProcessSabotageExternalTransfers(
        uint32 diff
    )
    {
        std::vector<uint32> expiredTransfers;

        for (auto itr =
                 SabotagePendingEntryTransferMs.begin();
             itr !=
                 SabotagePendingEntryTransferMs.end();)
        {
            uint32 guidLow =
                itr->first;

            ObjectGuid guid =
                ObjectGuid::Create<
                    HighGuid::Player
                >(guidLow);

            Player* player =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            uint32 expectedInstanceId = 0;

            if (!player ||
                !GetSabotageMatchInstanceId(
                    player,
                    expectedInstanceId
                ))
            {
                itr =
                    SabotagePendingEntryTransferMs.erase(
                        itr
                    );

                continue;
            }

            if (player->GetMapId() ==
                    MAP_STRATHOLME &&
                player->GetInstanceId() ==
                    expectedInstanceId)
            {
                itr =
                    SabotagePendingEntryTransferMs.erase(
                        itr
                    );

                continue;
            }

            if (itr->second > diff)
            {
                itr->second -= diff;
                ++itr;
                continue;
            }

            expiredTransfers.push_back(
                guidLow
            );

            itr =
                SabotagePendingEntryTransferMs.erase(
                    itr
                );
        }

        std::vector<uint32> exitGuids =
            expiredTransfers;

        for (auto const& pair :
             SabotageQueueStates)
        {
            uint32 guidLow =
                pair.first;

            SabotagePlayerQueueState const& state =
                pair.second;

            if (!state.matched ||
                !state.matchId ||
                state.assignedTeam ==
                    SABOTAGE_TEAM_NONE)
            {
                continue;
            }

            if (SabotagePendingEntryTransferMs.find(
                    guidLow
                ) !=
                SabotagePendingEntryTransferMs.end())
            {
                continue;
            }

            auto matchItr =
                SabotagePreparedMatches.find(
                    state.matchId
                );

            if (matchItr ==
                    SabotagePreparedMatches.end() ||
                !matchItr->second.launched ||
                !matchItr->second.instanceId)
            {
                continue;
            }

            ObjectGuid guid =
                ObjectGuid::Create<
                    HighGuid::Player
                >(guidLow);

            Player* player =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!player)
                continue;

            if (player->GetMapId() ==
                    MAP_STRATHOLME &&
                player->GetInstanceId() ==
                    matchItr->second.instanceId)
            {
                continue;
            }

            if (std::find(
                    exitGuids.begin(),
                    exitGuids.end(),
                    guidLow
                ) == exitGuids.end())
            {
                exitGuids.push_back(
                    guidLow
                );
            }
        }

        for (uint32 guidLow :
             exitGuids)
        {
            ObjectGuid guid =
                ObjectGuid::Create<
                    HighGuid::Player
                >(guidLow);

            Player* player =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!player)
                continue;

            ExitSabotagePlayerAfterExternalTransfer(
                player,
                "폭파전 지역을 벗어나 경기에서 이탈했습니다."
            );
        }
    }

    void QueueSabotageManualPartyLeave(
        ObjectGuid guid,
        RemoveMethod method
    )
    {
        if (method !=
            GROUP_REMOVEMETHOD_LEAVE)
        {
            return;
        }

        Player* player =
            ObjectAccessor::FindConnectedPlayer(
                guid
            );

        if (!player ||
            !IsScourgeSabotageParticipant(
                player
            ))
        {
            return;
        }

        uint32 guidLow =
            GetCharacterGuid(player);

        if (!guidLow)
            return;

        if (!SabotagePendingPartyLeaveGuids.insert(
                guidLow
            ).second)
        {
            return;
        }

        /*
         * GroupScript::OnRemoveMember runs before TrinityCore finishes
         * removing the member from the group.
         *
         * Hide the UI immediately, but defer queue removal,
         * state restoration and teleport until the next world update.
         */
        HideSabotageHud(
            player
        );

        SendSabotageMessage(
            player,
            "파티를 탈퇴하여 폭파전에서 이탈합니다."
        );

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage manual party leave queued: player={}, guid={}, map={}, instance={}",
            player->GetName(),
            guidLow,
            player->GetMapId(),
            player->GetInstanceId()
        );
    }

    void ProcessSabotagePendingPartyLeaves()
    {
        if (SabotagePendingPartyLeaveGuids.empty())
            return;

        std::vector<uint32> pendingGuids;
        pendingGuids.reserve(
            SabotagePendingPartyLeaveGuids.size()
        );

        for (uint32 guidLow :
             SabotagePendingPartyLeaveGuids)
        {
            pendingGuids.push_back(
                guidLow
            );
        }

        SabotagePendingPartyLeaveGuids.clear();

        for (uint32 guidLow : pendingGuids)
        {
            ObjectGuid guid =
                ObjectGuid::Create<
                    HighGuid::Player
                >(guidLow);

            Player* player =
                ObjectAccessor::FindConnectedPlayer(
                    guid
                );

            if (!player)
            {
                RemoveSabotageQueueGuid(
                    guid
                );

                continue;
            }

            bool hadSabotageState =
                IsScourgeSabotageParticipant(
                    player
                ) ||
                SabotageQueueStates.find(
                    guidLow
                ) !=
                    SabotageQueueStates.end();

            if (!hadSabotageState)
                continue;

            SabotagePlayerQueueState returnState;

            auto returnStateItr =
                SabotageQueueStates.find(
                    guidLow
                );

            if (returnStateItr !=
                SabotageQueueStates.end())
            {
                returnState =
                    returnStateItr->second;
            }

            /*
             * Remove the player while still inside the sabotage map.
             * A carried bomb can therefore be returned correctly.
             */
            RemoveSabotageQueueGuid(
                guid
            );

            ResetSabotagePlayerAfterManualLeave(
                player
            );

            /*
             * Packets sent immediately before TeleportTo can be lost
             * during the world transfer. Repeat the hard reset and
             * terminal EXIT state from OnMapChanged at the destination.
             */
            SabotagePendingPostTeleportResetGuids.insert(
                guidLow
            );

            bool teleported =
                TeleportSabotagePlayerToQueueReturnPosition(
                    player,
                    returnState
                );

            if (!teleported)
            {
                SabotagePendingPostTeleportResetGuids.erase(
                    guidLow
                );

                ResetSabotagePlayerAfterManualLeave(
                    player
                );
            }

            TC_LOG_INFO(
                "custom.scourge",
                "Sabotage manual party leave completed: player={}, guid={}, teleported={}",
                player->GetName(),
                guidLow,
                teleported ? 1 : 0
            );
        }

        TryCreateSabotageMatches();
    }

    std::string GetSabotageQueueStatusText()
    {
        CleanAllSabotageQueues();

        SabotagePreparedMatch* factionWar =
            FindActiveSabotageMatch(
                SABOTAGE_QUEUE_FACTION_WAR,
                0
            );

        SabotagePreparedMatch* allianceWar =
            FindActiveSabotageMatch(
                SABOTAGE_QUEUE_SAME_FACTION,
                ALLIANCE
            );

        SabotagePreparedMatch* hordeWar =
            FindActiveSabotageMatch(
                SABOTAGE_QUEUE_SAME_FACTION,
                HORDE
            );

        std::ostringstream text;

        text
            << "얼라이언스 대 호드: ";

        if (factionWar)
        {
            text
                << "진행 중 A "
                << factionWar->teamA.size()
                << "명 / B "
                << factionWar->teamB.size()
                << "명";
        }
        else
        {
            text << "대기 중";
        }

        text
            << " / 얼라 대기 "
            << SabotageFactionAllianceQueue.size()
            << "명 / 호드 대기 "
            << SabotageFactionHordeQueue.size()
            << "명\n"
            << "얼라이언스 대 얼라이언스: ";

        if (allianceWar)
        {
            text
                << "진행 중 A "
                << allianceWar->teamA.size()
                << "명 / B "
                << allianceWar->teamB.size()
                << "명";
        }
        else
        {
            text << "대기 중";
        }

        text
            << " / 대기 "
            << SabotageSameAllianceQueue.size()
            << "명\n"
            << "호드 대 호드: ";

        if (hordeWar)
        {
            text
                << "진행 중 A "
                << hordeWar->teamA.size()
                << "명 / B "
                << hordeWar->teamB.size()
                << "명";
        }
        else
        {
            text << "대기 중";
        }

        text
            << " / 대기 "
            << SabotageSameHordeQueue.size()
            << "명\n"
            << "최소 1 대 1, 각 팀 최대 40명 공격대";

        return text.str();
    }

    std::string GetSabotagePlayerStateText(
        Player const* player
    )
    {
        if (!player)
            return "";

        uint32 guidLow =
            GetCharacterGuid(player);

        auto itr =
            SabotageQueueStates.find(guidLow);

        if (itr == SabotageQueueStates.end())
            return "현재 신청 상태: 없음";

        std::ostringstream text;

        if (itr->second.matched)
        {
            text
                << "현재 신청 상태: 경기 #"
                << itr->second.matchId
                << " "
                << GetSabotageTeamName(
                    itr->second.assignedTeam
                );

            auto matchItr =
                SabotagePreparedMatches.find(
                    itr->second.matchId
                );

            if (matchItr !=
                    SabotagePreparedMatches.end() &&
                matchItr->second.launched)
            {
                text
                    << " 입장 완료 / 인스턴스 "
                    << matchItr->second.instanceId;
            }
            else
            {
                text << " 구성 완료";
            }
        }
        else
        {
            text
                << "현재 신청 상태: "
                << GetSabotageQueueName(
                    itr->second.queueType
                )
                << " 대기 중";
        }

        return text.str();
    }

    void SendScourgeModePayload(
        Player* player,
        std::string const& payload
    )
    {
        if (!player || payload.empty())
            return;

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX + "\t" + payload,
            player
        );
    }

    void SendScourgeModeState(Player* player, bool enabled)
    {
        SendScourgeModePayload(
            player,
            std::string("SCOURGE_MODE:STATE:") +
                (enabled ? "1" : "0")
        );
    }


    void SendCustomWarsongState(Player* player)
    {
        if (!player)
            return;

        bool queued = false;
        Battleground* ws = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_WS);
        if (ws && !player->InBattleground())
        {
            if (PvPDifficultyEntry const* bracket = GetBattlegroundBracketByLevel(ws->GetMapId(), player->GetLevel()))
            {
                BattlegroundQueueTypeId id = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_WS, bracket->GetBracketId(), 0);
                BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(id);
                GroupQueueInfo info{};
                queued = queue.GetPlayerGroupInfoData(player->GetGUID(), &info) && info.IsCustomWarsong;
            }
        }

        SendScourgeModePayload(player, std::string("SCOURGE_MODE:WSG:STATE:") + (queued ? "1" : "0"));
    }

    void JoinCustomWarsongQueue(Player* player)
    {
        if (!player)
            return;

        if (!CustomScourgeDungeonMode::IsEnabled(player))
        {
            SendScourgeModePayload(player, "SCOURGE_MODE:FAIL:침공룬을 먼저 켜야 합니다.");
            return;
        }

        Battleground* ws = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_WS);
        if (!ws || player->InBattleground() || player->InBattlegroundQueue() ||
            player->isUsingLfg() || player->IsDeserter() || player->HasAura(9454) ||
            !player->CanJoinToBattleground(ws) || !player->HasFreeBattlegroundQueueId())
        {
            SendScourgeModePayload(player, "SCOURGE_MODE:FAIL:전장 참가 조건을 확인하거나 기존 대기열을 취소해 주세요.");
            return;
        }

        PvPDifficultyEntry const* bracket = GetBattlegroundBracketByLevel(ws->GetMapId(), player->GetLevel());
        if (!bracket)
        {
            SendScourgeModePayload(player, "SCOURGE_MODE:FAIL:현재 레벨에서는 전장에 신청할 수 없습니다.");
            return;
        }

        BattlegroundQueueTypeId queueId = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_WS, bracket->GetBracketId(), 0);
        BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(queueId);
        GroupQueueInfo* info = queue.AddGroup(player, nullptr, bracket, false, false, 0, 0, 0, 0, true);
        uint32 averageWait = queue.GetAverageQueueWaitTime(info);
        uint32 slot = player->AddBattlegroundQueueId(queueId);

        WorldPackets::Battleground::BattlefieldStatusQueued status;
        BattlegroundMgr::BuildBattlegroundStatusQueued(&status, ws, slot, info->JoinTime, queueId, averageWait);
        player->SendDirectMessage(status.Write());

        sBattlegroundMgr->ScheduleQueueUpdate(0, queueId);
        SendCustomWarsongState(player);
    }

    void CancelCustomWarsongQueue(Player* player)
    {
        if (!player || player->InBattleground())
            return;

        Battleground* ws = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_WS);
        if (!ws)
            return;

        PvPDifficultyEntry const* bracket = GetBattlegroundBracketByLevel(ws->GetMapId(), player->GetLevel());
        if (!bracket)
            return;

        BattlegroundQueueTypeId queueId = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_WS, bracket->GetBracketId(), 0);
        uint32 slot = player->GetBattlegroundQueueIndex(queueId);
        if (slot >= PLAYER_MAX_BATTLEGROUND_QUEUES)
            return;

        BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(queueId);
        GroupQueueInfo info{};
        if (!queue.GetPlayerGroupInfoData(player->GetGUID(), &info) || !info.IsCustomWarsong)
            return;

        queue.RemovePlayer(player->GetGUID(), true);
        player->RemoveBattlegroundQueueId(queueId);

        WorldPackets::Battleground::BattlefieldStatusNone status;
        BattlegroundMgr::BuildBattlegroundStatusNone(&status, slot);
        player->SendDirectMessage(status.Write());
        sBattlegroundMgr->ScheduleQueueUpdate(0, queueId);
    }

    void SendSabotageQueueMenuState(
        Player* player
    )
    {
        if (!player)
            return;

        bool enabled =
            CustomScourgeDungeonMode::IsEnabled(
                player
            );

        bool queued = false;
        uint32 queueType =
            uint32(SABOTAGE_QUEUE_NONE);
        bool matched = false;
        uint32 matchId = 0;
        uint32 assignedTeam =
            uint32(SABOTAGE_TEAM_NONE);
        bool launched = false;

        uint32 guidLow =
            GetCharacterGuid(
                player
            );

        auto stateItr =
            SabotageQueueStates.find(
                guidLow
            );

        if (stateItr !=
            SabotageQueueStates.end())
        {
            queued = true;
            queueType =
                uint32(
                    stateItr->second.queueType
                );
            matched =
                stateItr->second.matched;
            matchId =
                stateItr->second.matchId;
            assignedTeam =
                uint32(
                    stateItr->second.assignedTeam
                );

            if (matched &&
                matchId)
            {
                auto matchItr =
                    SabotagePreparedMatches.find(
                        matchId
                    );

                if (matchItr !=
                    SabotagePreparedMatches.end())
                {
                    launched =
                        matchItr->second.launched;
                }
            }
        }

        std::ostringstream payload;
        payload
            << "SCOURGE_MODE:MENU:"
            << (enabled ? 1 : 0)
            << ":"
            << (queued ? 1 : 0)
            << ":"
            << queueType
            << ":"
            << (matched ? 1 : 0)
            << ":"
            << matchId
            << ":"
            << assignedTeam
            << ":"
            << (launched ? 1 : 0);

        SendScourgeModePayload(
            player,
            payload.str()
        );
    }

    void SendSabotageQueueRules(
        Player* player
    )
    {
        SendSabotageMessage(
            player,
            "진영전은 얼라이언스 대 호드로 구성됩니다. "
            "같은 진영 팀전은 같은 진영 신청자를 "
            "A팀과 B팀으로 균등 분할합니다. "
            "최소 1 대 1이며 각 편은 자동 공격대로 구성되고 "
            "최대 40 대 40까지 입장합니다. "
            "초과 인원은 다음 경기를 기다립니다."
        );
    }

    bool IsSabotageLaunchedForPlayer(
        Player const* player
    )
    {
        if (!player)
            return false;

        uint32 guidLow =
            GetCharacterGuid(
                player
            );

        auto stateItr =
            SabotageQueueStates.find(
                guidLow
            );

        if (stateItr ==
                SabotageQueueStates.end() ||
            !stateItr->second.matched ||
            !stateItr->second.matchId)
        {
            return false;
        }

        auto matchItr =
            SabotagePreparedMatches.find(
                stateItr->second.matchId
            );

        return matchItr !=
                SabotagePreparedMatches.end() &&
            matchItr->second.launched;
    }

    bool IsScourgeModePayload(std::string const& payload)
    {
        return payload.compare(
            0,
            SCOURGE_MODE_PAYLOAD_PREFIX.size(),
            SCOURGE_MODE_PAYLOAD_PREFIX
        ) == 0;
    }

    bool GetScourgeModePayload(
        std::string const& message,
        std::string& payload
    )
    {
        payload.clear();

        if (IsScourgeModePayload(message))
        {
            payload = message;
            return true;
        }

        if (message.compare(
            0,
            SCOURGE_MODE_ADDON_PREFIX.size(),
            SCOURGE_MODE_ADDON_PREFIX
        ) != 0)
        {
            return false;
        }

        if (message.size() <= SCOURGE_MODE_ADDON_PREFIX.size())
            return false;

        char separator =
            message[SCOURGE_MODE_ADDON_PREFIX.size()];

        if (separator != '\t' &&
            separator != ' ' &&
            separator != ':')
        {
            return false;
        }

        std::string innerPayload =
            message.substr(
                SCOURGE_MODE_ADDON_PREFIX.size() + 1
            );

        if (!IsScourgeModePayload(innerPayload))
            return false;

        payload = innerPayload;
        return true;
    }

    bool ParseSetCommand(
        std::string const& payload,
        bool& enabled
    )
    {
        if (payload.compare(
            0,
            SCOURGE_MODE_SET_PREFIX.size(),
            SCOURGE_MODE_SET_PREFIX
        ) != 0)
        {
            return false;
        }

        std::string value =
            payload.substr(SCOURGE_MODE_SET_PREFIX.size());

        if (value == "1")
        {
            enabled = true;
            return true;
        }

        if (value == "0")
        {
            enabled = false;
            return true;
        }

        return false;
    }

    void HandleScourgeModeAddonMessage(
        Player* player,
        std::string const& message
    )
    {
        if (!player)
            return;

        std::string payload;
        if (!GetScourgeModePayload(message, payload))
            return;

        if (payload == SCOURGE_MODE_GET_COMMAND)
        {
            bool enabled =
                CustomScourgeDungeonMode::IsEnabled(player);

            SendScourgeModeState(player, enabled);

            SabotagePreparedMatch* match = nullptr;
            SabotagePlayerQueueState* state = nullptr;

            if (GetSabotagePlayerMatchContext(
                    player,
                    match,
                    state
                ) &&
                match)
            {
                SendSabotageHudStateToPlayer(
                    player,
                    *match
                );
            }
            else
            {
                player->WhisperAddon(
                    SCOURGE_MODE_ADDON_PREFIX +
                        "\tSCOURGE_MODE:SABOTAGE:0",
                    player
                );

                HideSabotageHud(player);
            }

            SendSabotageQueueMenuState(
                player
            );
            SendCustomWarsongState(player);

            TC_LOG_DEBUG(
                "custom.scourge",
                "Scourge dungeon mode requested: player={}, guid={}, enabled={}",
                player->GetName(),
                GetCharacterGuid(player),
                enabled ? 1 : 0
            );

            return;
        }

        if (payload ==
            SCOURGE_MODE_MENU_GET_COMMAND)
        {
            SendScourgeModeState(
                player,
                CustomScourgeDungeonMode::IsEnabled(
                    player
                )
            );

            SendSabotageQueueMenuState(
                player
            );
            SendCustomWarsongState(player);

            return;
        }

        if (payload == SCOURGE_MODE_WSG_JOIN)
        {
            JoinCustomWarsongQueue(player);
            SendCustomWarsongState(player);
            return;
        }

        if (payload == SCOURGE_MODE_WSG_CANCEL)
        {
            CancelCustomWarsongQueue(player);
            SendCustomWarsongState(player);
            return;
        }

        if (payload ==
            SCOURGE_MODE_QUEUE_JOIN_FACTION_COMMAND)
        {
            JoinSabotageQueue(
                player,
                SABOTAGE_QUEUE_FACTION_WAR
            );

            SendSabotageQueueMenuState(
                player
            );

            return;
        }

        if (payload ==
            SCOURGE_MODE_QUEUE_JOIN_SAME_COMMAND)
        {
            JoinSabotageQueue(
                player,
                SABOTAGE_QUEUE_SAME_FACTION
            );

            SendSabotageQueueMenuState(
                player
            );

            return;
        }

        if (payload ==
            SCOURGE_MODE_QUEUE_CANCEL_COMMAND)
        {
            CancelSabotageQueue(
                player,
                true
            );

            SendSabotageQueueMenuState(
                player
            );

            return;
        }

        if (payload ==
            SCOURGE_MODE_QUEUE_STATUS_COMMAND)
        {
            SendSabotageMessage(
                player,
                GetSabotageQueueStatusText()
            );

            SendSabotageMessage(
                player,
                GetSabotagePlayerStateText(
                    player
                )
            );

            SendSabotageQueueMenuState(
                player
            );

            return;
        }

        if (payload ==
            SCOURGE_MODE_QUEUE_RULES_COMMAND)
        {
            SendSabotageQueueRules(
                player
            );

            SendSabotageQueueMenuState(
                player
            );

            return;
        }

        bool enabled = false;
        if (!ParseSetCommand(payload, enabled))
            return;

        if (!enabled)
        {
            if (IsSabotageLaunchedForPlayer(
                    player
                ))
            {
                SendScourgeModePayload(
                    player,
                    "SCOURGE_MODE:FAIL:"
                    "진행 중인 폭파전에서는 침공룬을 끌 수 없습니다."
                );

                SendScourgeModeState(
                    player,
                    true
                );

                SendSabotageQueueMenuState(
                    player
                );

                return;
            }

            CancelSabotageQueue(
                player,
                false
            );
            CancelCustomWarsongQueue(player);
        }

        CacheScourgeModeState(
            player,
            enabled
        );

        SaveScourgeModeState(
            player,
            enabled
        );

        ApplyScourgeDungeonPhase(
            player
        );

        SendScourgeModeState(
            player,
            enabled
        );

        SendSabotageQueueMenuState(
            player
        );
        SendCustomWarsongState(player);
    }
}

namespace CustomScourgeDungeonMode
{
    bool IsEnabled(Player const* player)
    {
        uint32 guid = GetCharacterGuid(player);
        if (!guid)
            return false;

        auto itr = ScourgeModeStates.find(guid);
        if (itr != ScourgeModeStates.end())
            return itr->second;

        bool enabled = LoadScourgeModeState(guid);
        ScourgeModeStates[guid] = enabled;
        return enabled;
    }

    void SendState(Player* player)
    {
        if (!player)
            return;

        SendScourgeModeState(
            player,
            IsEnabled(player)
        );
    }
}



class go_scourge_sabotage_route_portal :
    public GameObjectScript
{
public:
    go_scourge_sabotage_route_portal()
        : GameObjectScript(
            "go_scourge_sabotage_route_portal"
        )
    {
    }

    struct go_scourge_sabotage_route_portalAI :
        public GameObjectAI
    {
        explicit go_scourge_sabotage_route_portalAI(
            GameObject* gameObject
        )
            : GameObjectAI(gameObject)
        {
        }

        bool OnGossipHello(
            Player* player
        ) override
        {
            SabotagePreparedMatch* match = nullptr;
            SabotagePlayerQueueState* state = nullptr;

            if (!GetSabotagePlayerMatchContext(
                    player,
                    match,
                    state
                ) ||
                !match ||
                !state)
            {
                SendSabotageMessage(
                    player,
                    "진행 중인 폭파전 참가자만 "
                    "연결 포탈을 사용할 수 있습니다."
                );

                return true;
            }

            if (!player->IsAlive())
            {
                SendSabotageMessage(
                    player,
                    "생존 상태에서만 연결 포탈을 "
                    "사용할 수 있습니다."
                );

                return true;
            }

            if (match->roundPhase !=
                    SABOTAGE_ROUND_PHASE_ACTIVE &&
                match->roundPhase !=
                    SABOTAGE_ROUND_PHASE_BOMB_ARMED)
            {
                SendSabotageMessage(
                    player,
                    "라운드 진행 중에만 연결 포탈을 "
                    "사용할 수 있습니다."
                );

                return true;
            }

            bool clickedStartPortal = false;

            SabotageRoutePortalDefinition const*
                definition =
                    FindSabotageRoutePortalDefinition(
                        *match,
                        me->GetGUID(),
                        clickedStartPortal
                    );

            if (!definition)
            {
                SendSabotageMessage(
                    player,
                    "현재 경기의 연결 포탈이 아닙니다."
                );

                return true;
            }

            float destinationX = 0.0f;
            float destinationY = 0.0f;
            float destinationZ = 0.0f;
            float destinationO = 0.0f;

            GetSabotageRoutePortalArrivalPosition(
                *definition,
                clickedStartPortal,
                destinationX,
                destinationY,
                destinationZ,
                destinationO
            );

            uint32 preservedMountSpell = 0;

            if (player->HasAura(
                    SABOTAGE_BOMB_CARRIER_MOUNT_SPELL
                ))
            {
                preservedMountSpell =
                    SABOTAGE_BOMB_CARRIER_MOUNT_SPELL;
            }
            else if (player->HasAura(
                    SABOTAGE_MOUNT_SPELL
                ))
            {
                preservedMountSpell =
                    SABOTAGE_MOUNT_SPELL;
            }

            player->StopMoving();

            bool teleported =
                player->TeleportTo(
                    MAP_STRATHOLME,
                    destinationX,
                    destinationY,
                    destinationZ,
                    destinationO
                );

            if (!teleported)
            {
                SendSabotageMessage(
                    player,
                    "연결 포탈 이동에 실패했습니다."
                );

                TC_LOG_ERROR(
                    "custom.scourge",
                    "Sabotage route portal teleport failed: match={}, player={}, guid={}, route={}({}), clickedStart={}, destination=({}, {}, {}, {})",
                    match->matchId,
                    player->GetName(),
                    player->GetGUID().GetCounter(),
                    definition->routeId,
                    definition->routeName,
                    clickedStartPortal ? 1 : 0,
                    destinationX,
                    destinationY,
                    destinationZ,
                    destinationO
                );

                return true;
            }

            if (preservedMountSpell &&
                !player->HasAura(
                    preservedMountSpell
                ))
            {
                SpellCastResult mountRestoreResult =
                    player->CastSpell(
                        player,
                        preservedMountSpell,
                        true
                    );

                if (mountRestoreResult !=
                    SPELL_CAST_OK)
                {
                    TC_LOG_ERROR(
                        "custom.scourge",
                        "Sabotage route portal mount restore failed: match={}, player={}, guid={}, route={}({}), spell={}, result={}",
                        match->matchId,
                        player->GetName(),
                        player->GetGUID().GetCounter(),
                        definition->routeId,
                        definition->routeName,
                        preservedMountSpell,
                        uint32(mountRestoreResult)
                    );
                }
            }

            TC_LOG_INFO(
                "custom.scourge",
                "Sabotage route portal used: match={}, player={}, guid={}, route={}({}), from={}, destination=({}, {}, {}, {}), preservedMountSpell={}",
                match->matchId,
                player->GetName(),
                player->GetGUID().GetCounter(),
                definition->routeId,
                definition->routeName,
                clickedStartPortal
                    ? "start"
                    : "end",
                destinationX,
                destinationY,
                destinationZ,
                destinationO,
                preservedMountSpell
            );

            return true;
        }
    };

    GameObjectAI* GetAI(
        GameObject* gameObject
    ) const override
    {
        return new
            go_scourge_sabotage_route_portalAI(
                gameObject
            );
    }
};

class go_scourge_sabotage_bomb_supply :
    public GameObjectScript
{
public:
    go_scourge_sabotage_bomb_supply()
        : GameObjectScript(
            "go_scourge_sabotage_bomb_supply"
        )
    {
    }

    struct go_scourge_sabotage_bomb_supplyAI :
        public GameObjectAI
    {
        explicit go_scourge_sabotage_bomb_supplyAI(
            GameObject* gameObject
        )
            : GameObjectAI(gameObject)
        {
        }

        bool OnGossipHello(
            Player* player
        ) override
        {
            SabotagePreparedMatch* match = nullptr;
            SabotagePlayerQueueState* state = nullptr;

            if (!GetSabotagePlayerMatchContext(
                player,
                match,
                state
            ))
            {
                SendSabotageMessage(
                    player,
                    "진행 중인 폭파전 참가자만 "
                    "폭발물 보급함을 사용할 수 있습니다."
                );

                return true;
            }

            if (state->assignedTeam !=
                SABOTAGE_TEAM_A)
            {
                SendSabotageMessage(
                    player,
                    "폭발물은 공격팀인 A팀만 "
                    "획득할 수 있습니다."
                );

                return true;
            }

            if (!player->IsAlive())
            {
                SendSabotageMessage(
                    player,
                    "생존 상태에서만 폭발물을 "
                    "획득할 수 있습니다."
                );

                return true;
            }

            if (match->roundPhase !=
                SABOTAGE_ROUND_PHASE_ACTIVE)
            {
                SendSabotageMessage(
                    player,
                    "현재 라운드에서는 폭발물을 획득할 수 없습니다."
                );

                return true;
            }

            ClearInvalidSabotageBombCarrier(
                *match
            );

            if (!match->bombCarrierGuid.IsEmpty())
            {
                if (match->bombCarrierGuid ==
                    player->GetGUID())
                {
                    SendSabotageMessage(
                        player,
                        "이미 폭발물을 보유하고 있습니다."
                    );
                }
                else
                {
                    SendSabotageMessage(
                        player,
                        "현재 다른 A팀원이 폭발물을 "
                        "운반하고 있습니다."
                    );
                }

                return true;
            }

            if (player->HasItemCount(
                SABOTAGE_BOMB_ITEM,
                1,
                false
            ))
            {
                match->bombCarrierGuid =
                    player->GetGUID();

                match->bombObjectGuid.Clear();

                ApplySabotageBombCarrierFlag(
                    player
                );

                me->DespawnOrUnsummon();

                SendSabotageMessage(
                    player,
                    "보유 중인 폭발물이 현재 경기의 "
                    "폭발물로 등록되었습니다."
                );

                return true;
            }

            if (!player->AddItem(
                SABOTAGE_BOMB_ITEM,
                1
            ))
            {
                SendSabotageMessage(
                    player,
                    "가방 공간이 부족하여 폭발물을 "
                    "획득하지 못했습니다."
                );

                return true;
            }

            match->bombCarrierGuid =
                player->GetGUID();

            match->bombObjectGuid.Clear();

            ApplySabotageBombCarrierFlag(
                player
            );

            /*
             * The same script is used by the initial Team-A bomb
             * and by a bomb dropped at a dead carrier's position.
             */
            me->DespawnOrUnsummon();

            std::ostringstream text;
            text
                << player->GetName()
                << "님이 폭발물을 획득했습니다.";

            SendSabotageMatchMessage(
                *match,
                text.str()
            );

            TC_LOG_INFO(
                "custom.scourge",
                "Sabotage bomb acquired: player={}, guid={}, match={}, instance={}, item={}",
                player->GetName(),
                player->GetGUID().GetCounter(),
                match->matchId,
                match->instanceId,
                SABOTAGE_BOMB_ITEM
            );

            return true;
        }
    };

    GameObjectAI* GetAI(
        GameObject* gameObject
    ) const override
    {
        return new
            go_scourge_sabotage_bomb_supplyAI(
                gameObject
            );
    }
};

class go_scourge_sabotage_defender_scout :
    public GameObjectScript
{
public:
    go_scourge_sabotage_defender_scout()
        : GameObjectScript(
            "go_scourge_sabotage_defender_scout"
        )
    {
    }

    struct go_scourge_sabotage_defender_scoutAI :
        public GameObjectAI
    {
        explicit go_scourge_sabotage_defender_scoutAI(
            GameObject* gameObject
        )
            : GameObjectAI(gameObject)
        {
        }

        bool OnGossipHello(
            Player* player
        ) override
        {
            SabotagePreparedMatch* match = nullptr;
            SabotagePlayerQueueState* state = nullptr;

            if (!GetSabotagePlayerMatchContext(
                    player,
                    match,
                    state
                ) ||
                !match ||
                !state)
            {
                SendSabotageMessage(
                    player,
                    "진행 중인 폭파전 참가자만 "
                    "정찰 장치를 사용할 수 있습니다."
                );

                return true;
            }

            if (match->defenderScoutObjectGuid !=
                me->GetGUID())
            {
                SendSabotageMessage(
                    player,
                    "현재 라운드의 정찰 장치가 아닙니다."
                );

                return true;
            }

            if (state->assignedTeam !=
                SABOTAGE_TEAM_B)
            {
                SendSabotageMessage(
                    player,
                    "현재 라운드의 방어팀만 "
                    "정찰 장치를 사용할 수 있습니다."
                );

                return true;
            }

            if (!player->IsAlive())
            {
                SendSabotageMessage(
                    player,
                    "생존 상태에서만 정찰 장치를 "
                    "사용할 수 있습니다."
                );

                return true;
            }

            if (match->roundPhase !=
                    SABOTAGE_ROUND_PHASE_ACTIVE &&
                match->roundPhase !=
                    SABOTAGE_ROUND_PHASE_BOMB_ARMED)
            {
                SendSabotageMessage(
                    player,
                    "라운드 진행 중에만 정찰 장치를 "
                    "사용할 수 있습니다."
                );

                return true;
            }

            if (IsSabotageDefenderScoutActive(
                    player
                ))
            {
                DisableSabotageDefenderScout(
                    player,
                    true
                );

                return true;
            }

            EnableSabotageDefenderScout(
                player
            );

            return true;
        }
    };

    GameObjectAI* GetAI(
        GameObject* gameObject
    ) const override
    {
        return new
            go_scourge_sabotage_defender_scoutAI(
                gameObject
            );
    }
};

class go_scourge_sabotage_target :
    public GameObjectScript
{
public:
    go_scourge_sabotage_target()
        : GameObjectScript(
            "go_scourge_sabotage_target"
        )
    {
    }

    struct go_scourge_sabotage_targetAI :
        public GameObjectAI
    {
        explicit go_scourge_sabotage_targetAI(
            GameObject* gameObject
        )
            : GameObjectAI(gameObject)
        {
        }

        bool OnGossipHello(
            Player* player
        ) override
        {
            SabotagePreparedMatch* match = nullptr;
            SabotagePlayerQueueState* state = nullptr;

            if (!GetSabotagePlayerMatchContext(
                player,
                match,
                state
            ))
            {
                SendSabotageMessage(
                    player,
                    "현재 경기 참가자만 폭파 지점을 "
                    "사용할 수 있습니다."
                );

                return true;
            }

            StartSabotageBombAction(
                player,
                me,
                *match,
                *state
            );

            return true;
        }
    };

    GameObjectAI* GetAI(
        GameObject* gameObject
    ) const override
    {
        return new
            go_scourge_sabotage_targetAI(
                gameObject
            );
    }
};

class npc_scourge_sabotage_queue_manager :
    public CreatureScript
{
public:
    npc_scourge_sabotage_queue_manager()
        : CreatureScript(
            "npc_scourge_sabotage_queue_manager"
        )
    {
    }

    struct npc_scourge_sabotage_queue_managerAI :
        public ScriptedAI
    {
        explicit npc_scourge_sabotage_queue_managerAI(
            Creature* creature
        )
            : ScriptedAI(creature)
        {
        }

        enum GossipActions : uint32
        {
            ACTION_JOIN_FACTION_WAR =
                GOSSIP_ACTION_INFO_DEF + 1,
            ACTION_JOIN_SAME_FACTION =
                GOSSIP_ACTION_INFO_DEF + 2,
            ACTION_CANCEL_QUEUE =
                GOSSIP_ACTION_INFO_DEF + 3,
            ACTION_SHOW_STATUS =
                GOSSIP_ACTION_INFO_DEF + 4,
            ACTION_SHOW_RULES =
                GOSSIP_ACTION_INFO_DEF + 5
        };

        void ShowMainMenu(Player* player)
        {
            if (!player)
                return;

            ClearGossipMenuFor(player);

            uint32 guidLow =
                GetCharacterGuid(player);

            auto stateItr =
                SabotageQueueStates.find(guidLow);

            if (stateItr ==
                SabotageQueueStates.end())
            {
                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    "진영전 신청",
                    GOSSIP_SENDER_MAIN,
                    ACTION_JOIN_FACTION_WAR
                );

                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    "같은 진영 팀전 신청",
                    GOSSIP_SENDER_MAIN,
                    ACTION_JOIN_SAME_FACTION
                );
            }
            else
            {
                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    "현재 신청 또는 팀 구성 취소",
                    GOSSIP_SENDER_MAIN,
                    ACTION_CANCEL_QUEUE
                );
            }

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "대기 현황 확인",
                GOSSIP_SENDER_MAIN,
                ACTION_SHOW_STATUS
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "경기 방식 안내",
                GOSSIP_SENDER_MAIN,
                ACTION_SHOW_RULES
            );

            SendGossipMenuFor(
                player,
                1,
                me
            );
        }

        bool OnGossipHello(
            Player* player
        ) override
        {
            ShowMainMenu(player);
            return true;
        }

        bool OnGossipSelect(
            Player* player,
            uint32 /*menuId*/,
            uint32 gossipListId
        ) override
        {
            if (!player)
                return false;

            uint32 action =
                GetGossipActionFor(
                    player,
                    gossipListId
                );

            ClearGossipMenuFor(player);

            switch (action)
            {
                case ACTION_JOIN_FACTION_WAR:
                    JoinSabotageQueue(
                        player,
                        SABOTAGE_QUEUE_FACTION_WAR
                    );
                    break;

                case ACTION_JOIN_SAME_FACTION:
                    JoinSabotageQueue(
                        player,
                        SABOTAGE_QUEUE_SAME_FACTION
                    );
                    break;

                case ACTION_CANCEL_QUEUE:
                    CancelSabotageQueue(
                        player,
                        true
                    );
                    break;

                case ACTION_SHOW_STATUS:
                {
                    SendSabotageMessage(
                        player,
                        GetSabotageQueueStatusText()
                    );

                    SendSabotageMessage(
                        player,
                        GetSabotagePlayerStateText(
                            player
                        )
                    );
                    break;
                }

                case ACTION_SHOW_RULES:
                    SendSabotageMessage(
                        player,
                        "진영전은 얼라이언스 대 호드로 구성됩니다. 같은 진영 팀전은 같은 진영 신청자를 A팀과 B팀으로 균등 분할합니다. 최소 1 대 1이며 각 편은 자동 공격대로 구성되고 최대 40 대 40까지 입장합니다. 초과 인원은 다음 경기를 기다립니다."
                    );
                    break;

                default:
                    break;
            }

            CloseGossipMenuFor(player);
            return true;
        }

        void UpdateAI(
            uint32 /*diff*/
        ) override
        {
        }
    };

    CreatureAI* GetAI(
        Creature* creature
    ) const override
    {
        return new
            npc_scourge_sabotage_queue_managerAI(
                creature
            );
    }
};

class item_scourge_sabotage_mount_scroll :
    public ItemScript
{
public:
    item_scourge_sabotage_mount_scroll()
        : ItemScript(
            "item_scourge_sabotage_mount_scroll"
        )
    {
    }

    bool OnUse(
        Player* player,
        Item* item,
        SpellCastTargets const& /*targets*/
    ) override
    {
        if (!player ||
            !item ||
            item->GetEntry() !=
                SABOTAGE_MOUNT_ITEM)
        {
            return true;
        }

        SabotagePreparedMatch* match = nullptr;
        SabotagePlayerQueueState* state = nullptr;

        if (!GetSabotagePlayerMatchContext(
                player,
                match,
                state
            ) ||
            !IsScourgeSabotageParticipant(
                player
            ))
        {
            RemoveSabotageMountSupport(
                player
            );

            SendSabotageMessage(
                player,
                "폭파전 진행 중에만 사용할 수 있습니다."
            );

            return true;
        }

        if (IsSabotageDefenderScoutActive(
                player
            ))
        {
            SendSabotageMessage(
                player,
                "정찰 상태에서는 탈것을 사용할 수 없습니다. "
                "정찰 장치를 다시 클릭해 정상 상태로 돌아오세요."
            );

            return true;
        }

        bool carryingBomb =
            match &&
            match->bombCarrierGuid ==
                player->GetGUID() &&
            player->HasItemCount(
                SABOTAGE_BOMB_ITEM,
                1,
                false
            );

        uint32 mountSpell =
            carryingBomb
                ? SABOTAGE_BOMB_CARRIER_MOUNT_SPELL
                : SABOTAGE_MOUNT_SPELL;

        /*
         * The item decides the mount spell at the exact use time.
         * Bomb pickup and periodic match updates never force a mount cast.
         */
        player->RemoveAurasDueToSpell(
            SABOTAGE_MOUNT_SPELL
        );

        player->RemoveAurasDueToSpell(
            SABOTAGE_BOMB_CARRIER_MOUNT_SPELL
        );

        player->RemoveAurasByType(
            SPELL_AURA_MOUNTED
        );

        player->Dismount();

        SpellCastResult castResult =
            player->CastSpell(
                player,
                mountSpell,
                false
            );

        if (castResult != SPELL_CAST_OK)
        {
            SendSabotageMessage(
                player,
                carryingBomb
                    ? "폭탄 운반 전용 해골마를 사용할 수 없습니다."
                    : "폭파전 전용 탈것을 사용할 수 없습니다."
            );

            TC_LOG_ERROR(
                "custom.scourge",
                "Sabotage mount scroll cast failed: player={}, guid={}, carryingBomb={}, spell={}, result={}",
                player->GetName(),
                player->GetGUID().GetCounter(),
                carryingBomb ? 1 : 0,
                mountSpell,
                uint32(castResult)
            );

            return true;
        }

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage mount scroll used: player={}, guid={}, carryingBomb={}, spell={}",
            player->GetName(),
            player->GetGUID().GetCounter(),
            carryingBomb ? 1 : 0,
            mountSpell
        );

        return true;
    }
};

class item_scourge_sabotage_smoke_grenade :
    public ItemScript
{
public:
    item_scourge_sabotage_smoke_grenade()
        : ItemScript(
            "item_scourge_sabotage_smoke_grenade"
        )
    {
    }

    bool OnUse(
        Player* player,
        Item* item,
        SpellCastTargets const& /*targets*/
    ) override
    {
        if (!player ||
            !item ||
            item->GetEntry() !=
                SABOTAGE_SMOKE_GRENADE_ITEM)
        {
            return true;
        }

        SabotagePreparedMatch* match = nullptr;
        SabotagePlayerQueueState* state = nullptr;

        if (!GetSabotagePlayerMatchContext(
                player,
                match,
                state
            ) ||
            !match ||
            !state)
        {
            SendSabotageMessage(
                player,
                "폭파전 진행 중에만 연막탄을 사용할 수 있습니다."
            );

            return true;
        }

        if (!player->IsAlive())
        {
            SendSabotageMessage(
                player,
                "생존 상태에서만 연막탄을 사용할 수 있습니다."
            );

            return true;
        }

        if (state->assignedTeam !=
            SABOTAGE_TEAM_A)
        {
            SendSabotageMessage(
                player,
                "현재 라운드의 공격팀만 연막탄을 사용할 수 있습니다."
            );

            return true;
        }

        if (match->roundPhase !=
                SABOTAGE_ROUND_PHASE_ACTIVE &&
            match->roundPhase !=
                SABOTAGE_ROUND_PHASE_BOMB_ARMED)
        {
            SendSabotageMessage(
                player,
                "라운드 진행 중에만 연막탄을 사용할 수 있습니다."
            );

            return true;
        }

        if (match->smokeUsedThisRound)
        {
            SendSabotageMessage(
                player,
                "이번 라운드의 연막탄은 이미 사용되었습니다."
            );

            return true;
        }

        GameObject* smokeObject =
            SpawnSabotageSmokeObject(
                player,
                *match
            );

        if (!smokeObject)
        {
            SendSabotageMessage(
                player,
                "현재 위치에 연막 오브젝트를 생성하지 못했습니다."
            );

            return true;
        }

        match->smokeUsedThisRound = true;

        player->DestroyItemCount(
            SABOTAGE_SMOKE_GRENADE_ITEM,
            1,
            true
        );

        std::ostringstream text;
        text
            << player->GetName()
            << "님의 위치에 연막이 생성되었습니다. "
            << "13초 후 사라집니다.";

        SendSabotageMatchMessage(
            *match,
            text.str()
        );

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage smoke object spawned: player={}, guid={}, match={}, instance={}, item={}, gameObject={}, objectGuid={}, durationMs={}, x={}, y={}, z={}",
            player->GetName(),
            player->GetGUID().GetCounter(),
            match->matchId,
            match->instanceId,
            SABOTAGE_SMOKE_GRENADE_ITEM,
            SABOTAGE_SMOKE_GAMEOBJECT,
            smokeObject->GetGUID().GetCounter(),
            SABOTAGE_SMOKE_GAMEOBJECT_DURATION_MS,
            player->GetPositionX(),
            player->GetPositionY(),
            player->GetPositionZ()
        );

        /*
         * Block the item's native spell. The smoke feature consists only
         * of GameObject 961205.
         */
        return true;
    }
};

class custom_scourge_sabotage_group :
    public GroupScript
{
public:
    custom_scourge_sabotage_group()
        : GroupScript(
            "custom_scourge_sabotage_group"
        )
    {
    }

    void OnRemoveMember(
        Group* /*group*/,
        ObjectGuid guid,
        RemoveMethod method,
        ObjectGuid /*kicker*/,
        char const* /*reason*/
    ) override
    {
        QueueSabotageManualPartyLeave(
            guid,
            method
        );
    }
};

class custom_scourge_sabotage_world :
    public WorldScript
{
public:
    custom_scourge_sabotage_world()
        : WorldScript(
            "custom_scourge_sabotage_world"
        )
    {
    }

    void OnUpdate(uint32 diff) override
    {
        ProcessSabotagePendingPartyLeaves();
        UpdateSabotageHearthstoneExitMarkers(diff);
        ProcessSabotageExternalTransfers(diff);
        MaintainSabotageTeamRaids(diff);
        UpdateSabotageMatches(diff);
    }
};

class custom_scourge_dungeon_mode_player :
    public PlayerScript
{
public:
    custom_scourge_dungeon_mode_player()
        : PlayerScript(
            "custom_scourge_dungeon_mode_player"
        )
    {
    }

    void OnChat(
        Player* player,
        uint32 type,
        uint32 lang,
        std::string& msg,
        Player* receiver
    ) override
    {
        if (!player ||
            type != CHAT_MSG_WHISPER ||
            lang != LANG_ADDON)
        {
            return;
        }

        if (receiver && receiver != player)
            return;

        HandleScourgeModeAddonMessage(
            player,
            msg
        );
    }

    void OnLogin(
        Player* player,
        bool /*firstLogin*/
    ) override
    {
        if (!player)
            return;

        DisableSabotageDefenderScout(
            player,
            false
        );

        RestoreSabotageTemporaryMovementSpeed(
            player
        );

        player->m_serverSideVisibility.SetValue(
            SERVERSIDE_VISIBILITY_GM,
            SEC_PLAYER
        );

        SetScourgeSabotageParticipant(
            player,
            false
        );

        // Do not send an unconditional sabotage-hide packet here.
        // The first world entry can complete after OnLogin, so hiding here
        // races with the first valid sabotage HUD state.

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SPECTATOR:0",
            player
        );

        EnsureScourgeModeRow(player);

        bool enabled =
            LoadScourgeModeState(GetCharacterGuid(player));

        CacheScourgeModeState(player, enabled);
        ApplyScourgeDungeonPhase(player);
        SendScourgeModeState(player, enabled);

        TC_LOG_INFO(
            "custom.scourge",
            "Scourge dungeon mode loaded: player={}, guid={}, enabled={}",
            player->GetName(),
            GetCharacterGuid(player),
            enabled ? 1 : 0
        );
    }

    void OnSpellCast(
        Player* player,
        Spell* spell,
        bool /*skipCheck*/
    ) override
    {
        if (player &&
            spell &&
            IsSabotageDefenderScoutActive(
                player
            ))
        {
            bool triggered =
                spell->IsTriggered();

            spell->cancel(
                SPELL_FAILED_SILENCED
            );

            if (!triggered)
            {
                SendSabotageMessage(
                    player,
                    "정찰 상태에서는 스펠을 사용할 수 없습니다. "
                    "이동과 채팅만 가능합니다."
                );
            }

            TC_LOG_DEBUG(
                "custom.scourge",
                "Sabotage defender scout spell blocked: player={}, guid={}, spell={}, triggered={}",
                player->GetName(),
                player->GetGUID().GetCounter(),
                spell->GetSpellInfo()
                    ? spell->GetSpellInfo()->Id
                    : 0,
                triggered ? 1 : 0
            );

            return;
        }

        if (player &&
            spell &&
            spell->GetSpellInfo() &&
            spell->GetSpellInfo()->Id ==
                SABOTAGE_HEARTHSTONE_SPELL &&
            IsScourgeSabotageParticipant(
                player
            ))
        {
            uint32 guidLow =
                GetCharacterGuid(
                    player
                );

            if (guidLow)
            {
                SabotagePendingHearthstoneExitMs[
                    guidLow
                ] =
                    SABOTAGE_HEARTHSTONE_EXIT_GRACE_MS;

                TC_LOG_INFO(
                    "custom.scourge",
                    "Sabotage Hearthstone exit marked: player={}, guid={}, map={}, instance={}",
                    player->GetName(),
                    guidLow,
                    player->GetMapId(),
                    player->GetInstanceId()
                );
            }
        }

        CompleteSabotageBombAction(
            player,
            spell
        );
    }

    void OnMapChanged(Player* player) override
    {
        if (!player)
            return;

        uint32 guidLow =
            GetCharacterGuid(player);

        if (guidLow &&
            SabotagePendingPostTeleportResetGuids.erase(
                guidLow
            ) != 0)
        {
            ResetSabotagePlayerAfterManualLeave(
                player
            );

            TC_LOG_INFO(
                "custom.scourge",
                "Sabotage post-teleport reset completed: player={}, guid={}, map={}, instance={}",
                player->GetName(),
                guidLow,
                player->GetMapId(),
                player->GetInstanceId()
            );

            return;
        }

        ApplyScourgeDungeonPhase(player);

        SabotagePreparedMatch* match = nullptr;
        SabotagePlayerQueueState* state = nullptr;

        if (GetSabotagePlayerMatchContext(
                player,
                match,
                state
            ))
        {
            EnsureSabotageBombTargetsSpawned(
                player,
                *match
            );

            SendSabotageHudStateToPlayer(
                player,
                *match
            );
        }
    }

    void OnUpdateZone(
        Player* player,
        uint32 /*newZone*/,
        uint32 /*newArea*/
    ) override
    {
        /*
         * HandleMoveWorldportAck performs the normal instance validity
         * check before UpdateZone. Restore the authorized sabotage
         * exception here, after that core check has completed.
         */
        RestoreSabotageInstanceValidity(
            player
        );

        RefreshSabotageFreeForAll(
            player
        );

        SabotagePreparedMatch* match = nullptr;
        SabotagePlayerQueueState* state = nullptr;

        if (GetSabotagePlayerMatchContext(
                player,
                match,
                state
            ) &&
            match)
        {
            SendSabotageHudStateToPlayer(
                player,
                *match
            );
        }
    }

    void OnPVPKill(
        Player* /*killer*/,
        Player* killed
    ) override
    {
        DropSabotageBombAtCarrierPosition(
            killed
        );
    }

    void OnPlayerKilledByCreature(
        Creature* /*killer*/,
        Player* killed
    ) override
    {
        DropSabotageBombAtCarrierPosition(
            killed
        );
    }

    void OnPlayerRepop(
        Player* player
    ) override
    {
        /*
         * Fallback for environmental deaths and for custom kill paths
         * that do not call the two direct kill hooks above.
         */
        DropSabotageBombAtCarrierPosition(
            player
        );

        SabotagePreparedMatch* match = nullptr;
        SabotagePlayerQueueState* state = nullptr;

        if (!GetSabotagePlayerMatchContext(
            player,
            match,
            state
        ))
        {
            return;
        }

        if (match->roundPhase ==
                SABOTAGE_ROUND_PHASE_BREAK ||
            match->roundPhase ==
                SABOTAGE_ROUND_PHASE_FINISHED)
        {
            ReviveAndFreezeSabotageRoundPlayer(
                player
            );

            SendSabotageHudStateToPlayer(
                player,
                *match
            );

            return;
        }

        DisableSabotageDefenderScout(
            player,
            false
        );

        RestoreSabotageTemporaryMovementSpeed(
            player
        );

        SetScourgeSabotageParticipant(
            player,
            true
        );

        player->SetPhaseMask(
            SCOURGE_PHASE_MASK,
            true
        );

        player->m_InstanceValid = true;
        player->UpdateHomebindTime(0);

        player->CombatStopWithPets();
        player->SetSelection(
            ObjectGuid::Empty
        );

        player->WhisperAddon(
            SCOURGE_MODE_ADDON_PREFIX +
                "\tSCOURGE_MODE:SPECTATOR:1",
            player
        );

        /*
         * The normal ghost aura changes the detection mask to
         * GHOST_VISIBILITY_GHOST only. A sabotage spectator must also
         * detect living competitors so the match can actually be watched.
         *
         * Keep the spectator itself as a ghost. Only expand what this
         * player can detect.
         */
        player->m_serverSideVisibilityDetect.SetValue(
            SERVERSIDE_VISIBILITY_GHOST,
            GHOST_VISIBILITY_ALIVE |
                GHOST_VISIBILITY_GHOST
        );

        ApplySabotageTemporaryMovementSpeed(
            player,
            SABOTAGE_SPECTATOR_SPEED_RATE
        );

        player->UpdateObjectVisibility(
            true
        );

        SendSabotageMessage(
            player,
            "관전 모드로 전환되었습니다. "
            "유령 상태에서 이동속도가 3배가 되며 "
            "스트라솔름 내부를 자유롭게 관전할 수 있습니다."
        );

        TC_LOG_INFO(
            "custom.scourge",
            "Sabotage spectator mode entered: player={}, guid={}, match={}, instance={}, team={}",
            player->GetName(),
            player->GetGUID().GetCounter(),
            match->matchId,
            match->instanceId,
            uint32(state->assignedTeam)
        );
    }

    void OnLogout(Player* player) override
    {
        if (player)
        {
            uint32 guidLow =
                GetCharacterGuid(player);

            DisableSabotageDefenderScout(
                player,
                false
            );

            RestoreSabotageTemporaryMovementSpeed(
                player
            );

            player->m_serverSideVisibility.SetValue(
                SERVERSIDE_VISIBILITY_GM,
                SEC_PLAYER
            );

            SabotagePendingPostTeleportResetGuids.erase(
                guidLow
            );

            SabotagePendingEntryTransferMs.erase(
                guidLow
            );

            SabotagePendingHearthstoneExitMs.erase(
                guidLow
            );

            RestoreSabotageCombatState(
                player
            );

            RemoveSabotageQueueGuid(
                player->GetGUID()
            );
        }

        RemoveCachedScourgeModeState(player);
    }

    void OnDelete(
        ObjectGuid guid,
        uint32 /*accountId*/
    ) override
    {
        uint32 guidLow = uint32(guid.GetCounter());
        if (!guidLow)
            return;

        RemoveSabotageQueueGuid(guid);

        ScourgeModeStates.erase(guidLow);
        ScourgeModePhasePlayers.erase(guidLow);
        SabotageCombatRestoreStates.erase(guidLow);
        SabotagePendingPartyLeaveGuids.erase(guidLow);
        SabotagePendingPostTeleportResetGuids.erase(guidLow);
        SabotagePendingEntryTransferMs.erase(guidLow);
        SabotagePendingHearthstoneExitMs.erase(guidLow);
        SabotageMovementRestoreStates.erase(guidLow);
        SabotageDefenderScoutGuids.erase(guidLow);
        SabotageDefenderScoutRestoreStates.erase(
            guidLow
        );

        CharacterDatabase.PExecute(
            "DELETE FROM {} "
            "WHERE `guid` = {}",
            SCOURGE_MODE_TABLE,
            guidLow
        );
    }
};

void AddSC_custom_scourge_dungeon_mode()
{
    new custom_scourge_sabotage_group();
    new custom_scourge_sabotage_world();
    new item_scourge_sabotage_mount_scroll();
    new item_scourge_sabotage_smoke_grenade();
    new go_scourge_sabotage_route_portal();
    new go_scourge_sabotage_bomb_supply();
    new go_scourge_sabotage_defender_scout();
    new go_scourge_sabotage_target();
    new npc_scourge_sabotage_queue_manager();
    new custom_scourge_dungeon_mode_player();
}