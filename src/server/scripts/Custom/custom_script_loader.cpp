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

// This is where scripts' loading functions should be declared:
void AddSC_Transmogrification();
void AddSC_REFORGER_NPC();

// The name of this function should match:
// void Add${NameOfDirectory}Scripts()
void AddSC_item_enchant_visuals();
void AddSC_custom_scripts();
void AddSC_custom_commandscript();
void AddSC_npcs_special_custom();
void AddSC_custom_quest_scripts();
void AddSC_objects_special_custom();
void AddSC_monthly_random_npc_spawner();
void AddSC_daily_continent_boss_quests();
void AddSC_instance_damage_multiplier();
void AddSC_bg_wrathful_set_npc();
void AddSC_npc_item_exchange();
void AddSC_npc_delivery_depot();
void AddSC_icc_rental_mgr();
void AddSC_icc_rental_npc();
void AddSC_weekly_delivery_vendor();
void AddSC_discord_daily_event();

void AddCustomScripts()
{
	AddSC_item_enchant_visuals();
	AddSC_custom_scripts();
	AddSC_custom_commandscript();
	AddSC_npcs_special_custom();
	AddSC_custom_quest_scripts();
	AddSC_objects_special_custom();
    AddSC_Transmogrification();
    AddSC_REFORGER_NPC();
	AddSC_monthly_random_npc_spawner();
	AddSC_daily_continent_boss_quests();
	AddSC_instance_damage_multiplier();
	AddSC_bg_wrathful_set_npc();
	AddSC_npc_item_exchange();
	AddSC_npc_delivery_depot();
	AddSC_icc_rental_mgr();
	AddSC_icc_rental_npc();
	AddSC_weekly_delivery_vendor();
	AddSC_discord_daily_event();
}
