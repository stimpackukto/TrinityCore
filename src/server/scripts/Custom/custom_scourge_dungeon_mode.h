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
 * Custom Update - 2026-08-05
 *
 * Scourge dungeon mode public declarations
 *
 * - Declares the public state helpers implemented in
 *   custom_scourge_dungeon_mode.cpp.
 * - Declares the script registration function.
 * - Keeps internal sabotage match structures and helpers private to the
 *   implementation file.
 */

#ifndef CUSTOM_SCOURGE_DUNGEON_MODE_H
#define CUSTOM_SCOURGE_DUNGEON_MODE_H

class Player;

namespace CustomScourgeDungeonMode
{
    bool IsEnabled(Player const* player);
    void SendState(Player* player);
}

void AddSC_custom_scourge_dungeon_mode();

#endif // CUSTOM_SCOURGE_DUNGEON_MODE_H