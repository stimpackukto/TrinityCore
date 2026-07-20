/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "WorldSession.h"
#include "AccountMgr.h"
#include "CellImpl.h"
#include "Common.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "ChatPackets.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "Language.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "Util.h"
#include "Warden.h"
#include "World.h"
#include "WorldPacket.h"
#include <algorithm>
#include <array>
#include <vector>

#include "TicketMgr.h"
#include "DatabaseEnv.h"

inline bool isNasty(uint8 c)
{
    if (c == '\t')
        return false;
    if (c <= '\037') // ASCII control block
        return true;
    return false;
}

namespace
{
    std::array<EnchantmentSlot, 5> const CustomUpgradeChatSlots =
    {
        PROP_ENCHANTMENT_SLOT_0,
        PROP_ENCHANTMENT_SLOT_1,
        PROP_ENCHANTMENT_SLOT_2,
        PROP_ENCHANTMENT_SLOT_3,
        PROP_ENCHANTMENT_SLOT_4
    };

    uint32 ParseCustomItemLinkEntry(std::string const& msg, size_t hitemPos)
    {
        size_t entryStart = hitemPos + 7; // |Hitem:
        size_t entryEnd = msg.find(':', entryStart);

        if (entryEnd == std::string::npos)
            return 0;

        if (entryEnd <= entryStart)
            return 0;

        uint32 entry = 0;

        for (size_t i = entryStart; i < entryEnd; ++i)
        {
            char c = msg[i];

            if (c < '0' || c > '9')
                return 0;

            entry = entry * 10 + uint32(c - '0');
        }

        return entry;
    }

    std::string CleanCustomUpgradeDescription(std::string text)
    {
        std::string result;
        result.reserve(text.size());

        for (char c : text)
        {
            if (c == '|' || c == '[' || c == ']' || c == '{' || c == '}' || c == '\n' || c == '\r')
                continue;

            result += c;
        }

        return result;
    }

    std::string GetCustomUpgradeDescriptionForChat(uint32 enchantId)
    {
        if (enchantId == 0)
            return "";

        std::string sql = Trinity::StringFormat(
            "SELECT description "
            "FROM appdata.data_wotlk_spellitemenchantment "
            "WHERE id = {} "
            "AND custom_upgrade_enabled = 1 "
            "LIMIT 1",
            enchantId
        );

        QueryResult result = LoginDatabase.Query(sql.c_str());

        if (!result)
            return "";

        Field* fields = result->Fetch();
        std::string description = fields[0].GetString();

        if (description.empty())
            return "";

        return CleanCustomUpgradeDescription(description);
    }

    std::string BuildCustomUpgradeTextForChat(Item* item)
    {
        if (!item)
            return "";

        std::vector<std::string> descriptions;

        for (EnchantmentSlot slot : CustomUpgradeChatSlots)
        {
            uint32 enchantId = item->GetEnchantmentId(slot);

            if (enchantId == 0)
                continue;

            std::string description = GetCustomUpgradeDescriptionForChat(enchantId);

            if (description.empty())
                continue;

            descriptions.push_back(description);
        }

        if (descriptions.empty())
            return "";

        std::string text = " {";

        for (size_t i = 0; i < descriptions.size(); ++i)
        {
            if (i != 0)
                text += " / ";

            text += descriptions[i];
        }

        text += "}";

        return text;
    }

    std::string GetCustomUpgradeTextForLinkedItem(Player* player, uint32 itemEntry)
    {
        if (!player || itemEntry == 0)
            return "";

        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

            if (!item)
                continue;

            if (item->GetEntry() != itemEntry)
                continue;

            std::string text = BuildCustomUpgradeTextForChat(item);

            if (!text.empty())
                return text;
        }

        return "";
    }

    void AppendCustomUpgradeTextInsideItemLinks(Player* player, std::string& msg)
    {
        if (!player)
            return;

        size_t pos = 0;

        while ((pos = msg.find("|Hitem:", pos)) != std::string::npos)
        {
            uint32 itemEntry = ParseCustomItemLinkEntry(msg, pos);

            size_t textStart = msg.find("|h[", pos);

            if (textStart == std::string::npos)
            {
                pos += 7;
                continue;
            }

            textStart += 3;

            size_t textEnd = msg.find("]|h|r", textStart);

            if (textEnd == std::string::npos)
            {
                pos = textStart;
                continue;
            }

            if (itemEntry == 0)
            {
                pos = textEnd + 5;
                continue;
            }

            std::string upgradeText = GetCustomUpgradeTextForLinkedItem(player, itemEntry);

            if (upgradeText.empty())
            {
                pos = textEnd + 5;
                continue;
            }

            if (textEnd >= upgradeText.size() &&
                msg.compare(textEnd - upgradeText.size(), upgradeText.size(), upgradeText) == 0)
            {
                pos = textEnd + 5;
                continue;
            }

            if (msg.size() + upgradeText.size() > 255)
            {
                pos = textEnd + 5;
                continue;
            }

            msg.insert(textEnd, upgradeText);
            pos = textEnd + upgradeText.size() + 5;
        }
    }
}

void WorldSession::HandleMessagechatOpcode(WorldPacket& recvData)
{
    uint32 type;
    uint32 lang;

    recvData >> type;
    recvData >> lang;

    if (type >= MAX_CHAT_MSG_TYPE)
    {
        TC_LOG_ERROR("network", "CHAT: Wrong message type received: {}", type);
        recvData.rfinish();
        return;
    }

    if (lang == LANG_UNIVERSAL && type != CHAT_MSG_AFK && type != CHAT_MSG_DND)
    {
        TC_LOG_ERROR("entities.player.cheat", "CMSG_MESSAGECHAT: Possible hacking-attempt: {} tried to send a message in universal language", GetPlayerInfo());
        SendNotification(LANG_UNKNOWN_LANGUAGE);
        recvData.rfinish();
        return;
    }

    Player* sender = GetPlayer();

    //TC_LOG_DEBUG("CHAT: packet received. type {}, lang {}", type, lang);

    // prevent talking at unknown language (cheating)
    LanguageDesc const* langDesc = GetLanguageDescByID(lang);
    if (!langDesc)
    {
        SendNotification(LANG_UNKNOWN_LANGUAGE);
        recvData.rfinish();
        return;
    }

    if (langDesc->skill_id != 0 && !sender->HasSkill(langDesc->skill_id))
    {
        // also check SPELL_AURA_COMPREHEND_LANGUAGE (client offers option to speak in that language)
        Unit::AuraEffectList const& langAuras = sender->GetAuraEffectsByType(SPELL_AURA_COMPREHEND_LANGUAGE);
        bool foundAura = false;
        for (Unit::AuraEffectList::const_iterator i = langAuras.begin(); i != langAuras.end(); ++i)
        {
            if ((*i)->GetMiscValue() == int32(lang))
            {
                foundAura = true;
                break;
            }
        }
        if (!foundAura)
        {
            SendNotification(LANG_NOT_LEARNED_LANGUAGE);
            recvData.rfinish();
            return;
        }
    }

    if (lang == LANG_ADDON)
    {
        // LANG_ADDON is only valid for the following message types
        switch (type)
        {
            case CHAT_MSG_PARTY:
            case CHAT_MSG_RAID:
            case CHAT_MSG_GUILD:
            case CHAT_MSG_BATTLEGROUND:
            case CHAT_MSG_WHISPER:
                // check if addon messages are disabled
                if (!sWorld->getBoolConfig(CONFIG_ADDON_CHANNEL))
                {
                    recvData.rfinish();
                    return;
                }
                break;
            default:
                TC_LOG_ERROR("network", "Player {} {} sent a chatmessage with an invalid language/message type combination",
                    GetPlayer()->GetName(), GetPlayer()->GetGUID().ToString());

                recvData.rfinish();
                return;
        }
    }
    else
    {
        // send in universal language if player in .gm on mode (ignore spell effects)
        if (sender->IsGameMaster())
            lang = LANG_UNIVERSAL;
        else
        {
            Unit::AuraEffectList const& ModLangAuras = sender->GetAuraEffectsByType(SPELL_AURA_MOD_LANGUAGE);
            if (!ModLangAuras.empty())
                lang = ModLangAuras.front()->GetMiscValue();
            else if (HasPermission(rbac::RBAC_PERM_TWO_SIDE_INTERACTION_CHAT))
                lang = LANG_UNIVERSAL;
            else
            {
                switch (type)
                {
                    case CHAT_MSG_PARTY:
                    case CHAT_MSG_PARTY_LEADER:
                    case CHAT_MSG_RAID:
                    case CHAT_MSG_RAID_LEADER:
                    case CHAT_MSG_RAID_WARNING:
                        // allow two side chat at group channel if two side group allowed
                        if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP))
                            lang = LANG_UNIVERSAL;
                        break;
                    case CHAT_MSG_GUILD:
                    case CHAT_MSG_OFFICER:
                        // allow two side chat at guild channel if two side guild allowed
                        if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD))
                            lang = LANG_UNIVERSAL;
                        break;
                }
            }
        }
    }

    if (!CanSpeak())
    {
        std::string timeStr = secsToTimeString(m_muteTime - GameTime::GetGameTime());
        SendNotification(GetTrinityString(LANG_WAIT_BEFORE_SPEAKING), timeStr.c_str());
        recvData.rfinish(); // Prevent warnings
        return;
    }

    if (type != CHAT_MSG_AFK && type != CHAT_MSG_DND)
        sender->UpdateSpeakTime(lang == LANG_ADDON ? Player::ChatFloodThrottle::ADDON : Player::ChatFloodThrottle::REGULAR);

    if (sender->HasAura(1852) && type != CHAT_MSG_WHISPER)
    {
        SendNotification(GetTrinityString(LANG_GM_SILENCE), sender->GetName().c_str());
        recvData.rfinish();
        return;
    }

    std::string to, channel, msg;
    switch (type)
    {
        case CHAT_MSG_SAY:
        case CHAT_MSG_EMOTE:
        case CHAT_MSG_YELL:
        case CHAT_MSG_PARTY:
        case CHAT_MSG_PARTY_LEADER:
        case CHAT_MSG_GUILD:
        case CHAT_MSG_OFFICER:
        case CHAT_MSG_RAID:
        case CHAT_MSG_RAID_LEADER:
        case CHAT_MSG_RAID_WARNING:
        case CHAT_MSG_BATTLEGROUND:
        case CHAT_MSG_BATTLEGROUND_LEADER:
        case CHAT_MSG_AFK:
        case CHAT_MSG_DND:
            msg = recvData.ReadCString(lang != LANG_ADDON);
            break;
        case CHAT_MSG_WHISPER:
            recvData >> to;
            msg = recvData.ReadCString(lang != LANG_ADDON);
            break;
        case CHAT_MSG_CHANNEL:
            recvData >> channel;
            msg = recvData.ReadCString(lang != LANG_ADDON);
            break;
    }

    if (msg.size() > 255)
        return;

	/// 🔥 GM 인터셉트 + 티켓 조건
	if (msg.rfind("gm ", 0) == 0)
	{
		Player* player = GetPlayer();
		if (!player)
			return;

		/// 🔥 티켓 체크 (핵심)
		GmTicket* ticket = sTicketMgr->GetTicketByPlayer(player->GetGUID());
		if (!ticket)
			return; // ❗ 티켓 없으면 gm 무시 (일반 채팅으로 진행됨)

		std::string content = msg.substr(3);
		if (content.empty())
			return;

		/// 🔥 DB 저장 (핵심)
		CharacterDatabase.PExecute(
			"INSERT INTO appdata.gm_chat_log (account_id, player_guid, message) VALUES ({}, {}, '{}')",
			player->GetSession()->GetAccountId(),
			player->GetGUID().GetCounter(),
			content
		);

		std::string gmMsg = "[GM에게보냄] " + content;

		WorldPacket data;
		ChatHandler::BuildChatPacket(
			data,
			CHAT_MSG_MONSTER_WHISPER,
			LANG_UNIVERSAL,
			player,
			player,
			gmMsg,
			0,
			"GM"
		);

		SendPacket(&data);

		return; // 🔥 기존 채팅 완전 차단
	}

    // Our Warden module also uses SendAddonMessage as a way to communicate Lua check results to the server, see if this is that
    if ((type == CHAT_MSG_GUILD) && (lang == LANG_ADDON))
    {
        if (_warden && _warden->ProcessLuaCheckResponse(msg))
            return;
    }

    // no chat commands in AFK/DND autoreply, and it can be empty
    if (!(type == CHAT_MSG_AFK || type == CHAT_MSG_DND))
    {
        if (msg.empty())
            return;
        if (lang == LANG_ADDON)
        {
            if (AddonChannelCommandHandler(this).ParseCommands(msg.c_str()))
                return;
        }
        else
        {
            if (ChatHandler(this).ParseCommands(msg.c_str()))
                return;
        }
    }

    // do message validity checks
    if (lang != LANG_ADDON)
    {
        // cut at the first newline or carriage return
        std::string::size_type pos = msg.find_first_of("\n\r");
        if (pos == 0)
            return;
        else if (pos != std::string::npos)
            msg.erase(pos);

        // abort on any sort of nasty character
        for (uint8 c : msg)
            if (isNasty(c))
            {
                TC_LOG_ERROR("network", "Player {} {} sent a message containing invalid character {} - blocked", GetPlayer()->GetName(),
                    GetPlayer()->GetGUID().ToString(), uint8(c));
                return;
            }

        // collapse multiple spaces into one
        if (sWorld->getBoolConfig(CONFIG_CHAT_FAKE_MESSAGE_PREVENTING))
        {
            auto end = std::unique(msg.begin(), msg.end(), [](char c1, char c2) { return (c1 == ' ') && (c2 == ' '); });
            msg.erase(end, msg.end());
        }

        // validate hyperlinks
        if (!ValidateHyperlinksAndMaybeKick(msg))
            return;

        // 커스텀 강화는 PROP_ENCHANTMENT_SLOT_0~4에 저장되어 기본 아이템 링크 툴팁에 표시되지 않는다.
        // 그래서 채팅으로 나가기 전 링크 표시명 안에 강화 설명을 붙인다.
        // 예: [어둠한 {힘 +30 / 치명 +20}]
        AppendCustomUpgradeTextInsideItemLinks(sender, msg);
    }

    switch (type)
    {
        case CHAT_MSG_SAY:
        {
            // Prevent cheating
            if (!sender->IsAlive())
                return;

            if (sender->GetLevel() < sWorld->getIntConfig(CONFIG_CHAT_SAY_LEVEL_REQ))
            {
                SendNotification(GetTrinityString(LANG_SAY_REQ), sWorld->getIntConfig(CONFIG_CHAT_SAY_LEVEL_REQ));
                return;
            }

            sender->Say(msg, Language(lang));
            break;
        }
        case CHAT_MSG_EMOTE:
        {
            // Prevent cheating
            if (!sender->IsAlive())
                return;

            if (sender->GetLevel() < sWorld->getIntConfig(CONFIG_CHAT_EMOTE_LEVEL_REQ))
            {
                SendNotification(GetTrinityString(LANG_SAY_REQ), sWorld->getIntConfig(CONFIG_CHAT_EMOTE_LEVEL_REQ));
                return;
            }

            sender->TextEmote(msg);
            break;
        }
        case CHAT_MSG_YELL:
        {
            // Prevent cheating
            if (!sender->IsAlive())
                return;

            if (sender->GetLevel() < sWorld->getIntConfig(CONFIG_CHAT_YELL_LEVEL_REQ))
            {
                SendNotification(GetTrinityString(LANG_SAY_REQ), sWorld->getIntConfig(CONFIG_CHAT_YELL_LEVEL_REQ));
                return;
            }

            sender->Yell(msg, Language(lang));
            break;
        }
        case CHAT_MSG_WHISPER:
        {
            if (!normalizePlayerName(to))
            {
                SendPlayerNotFoundNotice(to);
                break;
            }

            Player* receiver = ObjectAccessor::FindConnectedPlayerByName(to);
            if (!receiver || (lang != LANG_ADDON && !receiver->isAcceptWhispers() && receiver->GetSession()->HasPermission(rbac::RBAC_PERM_CAN_FILTER_WHISPERS) && !receiver->IsInWhisperWhiteList(sender->GetGUID())))
            {
                SendPlayerNotFoundNotice(to);
                return;
            }

            // Apply checks only if receiver is not already in whitelist and if receiver is not a GM with ".whisper on"
            if (!receiver->IsInWhisperWhiteList(sender->GetGUID()) && !receiver->IsGameMasterAcceptingWhispers())
            {
                if (!sender->IsGameMaster() && sender->GetLevel() < sWorld->getIntConfig(CONFIG_CHAT_WHISPER_LEVEL_REQ))
                {
                    SendNotification(GetTrinityString(LANG_WHISPER_REQ), sWorld->getIntConfig(CONFIG_CHAT_WHISPER_LEVEL_REQ));
                    return;
                }

                if (GetPlayer()->GetTeam() != receiver->GetTeam() && !HasPermission(rbac::RBAC_PERM_TWO_SIDE_INTERACTION_CHAT))
                {
                    SendWrongFactionNotice();
                    return;
                }
            }

            if (GetPlayer()->HasAura(1852) && !receiver->IsGameMaster())
            {
                SendNotification(GetTrinityString(LANG_GM_SILENCE), GetPlayer()->GetName().c_str());
                return;
            }

            // If player is a Gamemaster and doesn't accept whisper, we auto-whitelist every player that the Gamemaster is talking to
            // We also do that if a player is under the required level for whispers.
            if (receiver->GetLevel() < sWorld->getIntConfig(CONFIG_CHAT_WHISPER_LEVEL_REQ) ||
                (HasPermission(rbac::RBAC_PERM_CAN_FILTER_WHISPERS) && !sender->isAcceptWhispers() && !sender->IsInWhisperWhiteList(receiver->GetGUID())))
                sender->AddWhisperWhiteList(receiver->GetGUID());

            if (lang == LANG_ADDON)
                GetPlayer()->WhisperAddon(msg, receiver);
            else
                GetPlayer()->Whisper(msg, Language(lang), receiver);
            break;
        }
        case CHAT_MSG_PARTY:
        case CHAT_MSG_PARTY_LEADER:
        {
            // if player is in battleground, he cannot say to battleground members by /p
            Group* group = GetPlayer()->GetOriginalGroup();
            if (!group)
            {
                group = sender->GetGroup();
                if (!group || group->isBGGroup())
                    return;
            }

            // control LEADER messages on the server
            // in a scenario where player has both Group and OriginalGroup,
            // client will incorrectly send LEADER type when sending message to OriginalGroup while being a leader in regular group
            type = group->IsLeader(sender->GetGUID()) ? CHAT_MSG_PARTY_LEADER : CHAT_MSG_PARTY;

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, ChatMsg(type), Language(lang), sender, nullptr, msg);
            group->BroadcastPacket(&data, false, group->GetMemberGroup(GetPlayer()->GetGUID()));
            break;
        }
        case CHAT_MSG_GUILD:
		{
			if (GetPlayer()->GetGuildId())
			{
				if (Guild* guild = sGuildMgr->GetGuildById(GetPlayer()->GetGuildId()))
				{
					sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, guild);

					// 웹 길드 채팅룸 기록
					// LANG_ADDON은 애드온 통신일 수 있으므로 기록 제외
					if (lang != LANG_ADDON)
					{
						std::string dbName = GetPlayer()->GetName();
						std::string dbMsg = msg;

						CharacterDatabase.EscapeString(dbName);
						CharacterDatabase.EscapeString(dbMsg);

						CharacterDatabase.PExecute(
							"INSERT INTO appdata.guild_tech_chat "
							"(guild_id, account_id, character_guid, character_name, message) "
							"VALUES ({}, {}, {}, '{}', '{}')",
							GetPlayer()->GetGuildId(),
							GetAccountId(),
							GetPlayer()->GetGUID().GetCounter(),
							dbName,
							dbMsg
						);

							CharacterDatabase.PExecute(
								"DELETE FROM appdata.guild_tech_chat "
								"WHERE guild_id = {} "
								"AND id NOT IN ("
								"    SELECT id FROM ("
								"        SELECT id "
								"        FROM appdata.guild_tech_chat "
								"        WHERE guild_id = {} "
								"        ORDER BY id DESC "
								"        LIMIT 30"
								"    ) keep_rows"
								")",
								GetPlayer()->GetGuildId(),
								GetPlayer()->GetGuildId()
							);
					}

					guild->BroadcastToGuild(this, false, msg, lang == LANG_ADDON ? LANG_ADDON : LANG_UNIVERSAL);
				}
			}
			break;
		}
        case CHAT_MSG_OFFICER:
        {
            if (GetPlayer()->GetGuildId())
            {
                if (Guild* guild = sGuildMgr->GetGuildById(GetPlayer()->GetGuildId()))
                {
                    sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, guild);

                    guild->BroadcastToGuild(this, true, msg, lang == LANG_ADDON ? LANG_ADDON : LANG_UNIVERSAL);
                }
            }
            break;
        }
        case CHAT_MSG_RAID:
        {
            // if player is in battleground, he cannot say to battleground members by /ra
            Group* group = GetPlayer()->GetOriginalGroup();
            if (!group)
            {
                group = GetPlayer()->GetGroup();
                if (!group || group->isBGGroup() || !group->isRaidGroup())
                    return;
            }

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID, Language(lang), sender, nullptr, msg);
            group->BroadcastPacket(&data, false);
            break;
        }
        case CHAT_MSG_RAID_LEADER:
        {
            // if player is in battleground, he cannot say to battleground members by /ra
            Group* group = GetPlayer()->GetOriginalGroup();
            if (!group)
            {
                group = GetPlayer()->GetGroup();
                if (!group || group->isBGGroup() || !group->isRaidGroup() || !group->IsLeader(sender->GetGUID()))
                    return;
            }

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID_LEADER, Language(lang), sender, nullptr, msg);
            group->BroadcastPacket(&data, false);
            break;
        }
        case CHAT_MSG_RAID_WARNING:
        {
            Group* group = GetPlayer()->GetGroup();
            if (!group || !(group->isRaidGroup() || sWorld->getBoolConfig(CONFIG_CHAT_PARTY_RAID_WARNINGS)) || !(group->IsLeader(GetPlayer()->GetGUID()) || group->IsAssistant(GetPlayer()->GetGUID())) || group->isBGGroup())
                return;

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            //in battleground, raid warning is sent only to players in battleground - code is ok
            ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID_WARNING, Language(lang), sender, nullptr, msg);
            group->BroadcastPacket(&data, false);
            break;
        }
        case CHAT_MSG_BATTLEGROUND:
        {
            //battleground raid is always in Player->GetGroup(), never in GetOriginalGroup()
            Group* group = GetPlayer()->GetGroup();
            if (!group || !group->isBGGroup())
                return;

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_BATTLEGROUND, Language(lang), sender, nullptr, msg);
            group->BroadcastPacket(&data, false);
            break;
        }
        case CHAT_MSG_BATTLEGROUND_LEADER:
        {
            // battleground raid is always in Player->GetGroup(), never in GetOriginalGroup()
            Group* group = GetPlayer()->GetGroup();
            if (!group || !group->isBGGroup() || !group->IsLeader(GetPlayer()->GetGUID()))
                return;

            sScriptMgr->OnPlayerChat(GetPlayer(), type, lang, msg, group);

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_BATTLEGROUND_LEADER, Language(lang), sender, nullptr, msg);;
            group->BroadcastPacket(&data, false);
            break;
        }
        case CHAT_MSG_CHANNEL:
        {
            if (!HasPermission(rbac::RBAC_PERM_SKIP_CHECK_CHAT_CHANNEL_REQ))
            {
                if (sender->GetLevel() < sWorld->getIntConfig(CONFIG_CHAT_CHANNEL_LEVEL_REQ))
                {
                    SendNotification(GetTrinityString(LANG_CHANNEL_REQ), sWorld->getIntConfig(CONFIG_CHAT_CHANNEL_LEVEL_REQ));
                    return;
                }
            }

            if (Channel* chn = ChannelMgr::GetChannelForPlayerByNamePart(channel, sender))
            {
                sScriptMgr->OnPlayerChat(sender, type, lang, msg, chn);
                chn->Say(sender->GetGUID(), msg, lang);
            }
            break;
        }
        case CHAT_MSG_AFK:
        {
            if (!sender->IsInCombat())
            {
                if (sender->isAFK())                       // Already AFK
                {
                    if (msg.empty())
                        sender->ToggleAFK();               // Remove AFK
                    else
                        sender->autoReplyMsg = msg;        // Update message
                }
                else                                        // New AFK mode
                {
                    sender->autoReplyMsg = msg.empty() ? GetTrinityString(LANG_PLAYER_AFK_DEFAULT) : msg;

                    if (sender->isDND())
                        sender->ToggleDND();

                    sender->ToggleAFK();
                }

                sScriptMgr->OnPlayerChat(sender, type, lang, msg);
            }
            break;
        }
        case CHAT_MSG_DND:
        {
            if (sender->isDND())                           // Already DND
            {
                if (msg.empty())
                    sender->ToggleDND();                   // Remove DND
                else
                    sender->autoReplyMsg = msg;            // Update message
            }
            else                                            // New DND mode
            {
                sender->autoReplyMsg = msg.empty() ? GetTrinityString(LANG_PLAYER_DND_DEFAULT) : msg;

                if (sender->isAFK())
                    sender->ToggleAFK();

                sender->ToggleDND();
            }

            sScriptMgr->OnPlayerChat(sender, type, lang, msg);
            break;
        }
        default:
            TC_LOG_ERROR("network", "CHAT: unknown message type {}, lang: {}", type, lang);
            break;
    }
}

void WorldSession::HandleEmoteOpcode(WorldPackets::Chat::EmoteClient& packet)
{
    Emote emoteId = static_cast<Emote>(packet.EmoteID);

    // restrict to the only emotes hardcoded in client
    if (emoteId != EMOTE_ONESHOT_NONE && emoteId != EMOTE_ONESHOT_WAVE)
        return;

    if (!_player->IsAlive() || _player->HasUnitState(UNIT_STATE_DIED))
        return;

    sScriptMgr->OnPlayerEmote(_player, emoteId);
    _player->HandleEmoteCommand(emoteId);
}

namespace Trinity
{
    class EmoteChatBuilder
    {
        public:
            EmoteChatBuilder(Player const& player, uint32 text_emote, uint32 emote_num, Unit const* target)
                : i_player(player), i_text_emote(text_emote), i_emote_num(emote_num), i_target(target) { }

            void operator()(WorldPacket& data, LocaleConstant loc_idx)
            {
                std::string const name(i_target ? i_target->GetNameForLocaleIdx(loc_idx) : "");
                uint32 namlen = name.size();

                data.Initialize(SMSG_TEXT_EMOTE, 20 + namlen);
                data << i_player.GetGUID();
                data << uint32(i_text_emote);
                data << uint32(i_emote_num);
                data << uint32(namlen);
                if (namlen > 1)
                    data << name;
                else
                    data << uint8(0x00);
            }

        private:
            Player const& i_player;
            uint32        i_text_emote;
            uint32        i_emote_num;
            Unit const*   i_target;
    };
}                                                           // namespace Trinity

void WorldSession::HandleTextEmoteOpcode(WorldPacket& recvData)
{
    if (!GetPlayer()->IsAlive())
        return;

    if (!CanSpeak())
    {
        std::string timeStr = secsToTimeString(m_muteTime - GameTime::GetGameTime());
        SendNotification(GetTrinityString(LANG_WAIT_BEFORE_SPEAKING), timeStr.c_str());
        return;
    }

    uint32 text_emote, emoteNum;
    ObjectGuid guid;

    recvData >> text_emote;
    recvData >> emoteNum;
    recvData >> guid;

    sScriptMgr->OnPlayerTextEmote(GetPlayer(), text_emote, emoteNum, guid);

    EmotesTextEntry const* em = sEmotesTextStore.LookupEntry(text_emote);
    if (!em)
        return;

    Emote emote = static_cast<Emote>(em->EmoteID);

    switch (emote)
    {
        case EMOTE_STATE_SLEEP:
        case EMOTE_STATE_SIT:
        case EMOTE_STATE_KNEEL:
        case EMOTE_ONESHOT_NONE:
            break;
        case EMOTE_STATE_DANCE:
            GetPlayer()->SetEmoteState(emote);
            break;
        default:
            // Only allow text-emotes for "dead" entities (feign death included)
            if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
                break;
            GetPlayer()->HandleEmoteCommand(emote);
            break;
    }

    Unit* unit = ObjectAccessor::GetUnit(*_player, guid);

    CellCoord p = Trinity::ComputeCellCoord(GetPlayer()->GetPositionX(), GetPlayer()->GetPositionY());

    Cell cell(p);
    cell.SetNoCreate();

    Trinity::EmoteChatBuilder emote_builder(*GetPlayer(), text_emote, emoteNum, unit);
    Trinity::LocalizedPacketDo<Trinity::EmoteChatBuilder > emote_do(emote_builder);
    Trinity::PlayerDistWorker<Trinity::LocalizedPacketDo<Trinity::EmoteChatBuilder > > emote_worker(GetPlayer(), sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_TEXTEMOTE), emote_do);
    TypeContainerVisitor<Trinity::PlayerDistWorker<Trinity::LocalizedPacketDo<Trinity::EmoteChatBuilder> >, WorldTypeMapContainer> message(emote_worker);
    cell.Visit(p, message, *GetPlayer()->GetMap(), *GetPlayer(), sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_TEXTEMOTE));

    GetPlayer()->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_DO_EMOTE, text_emote, 0, unit);

    //Send scripted event call
    if (unit && unit->GetTypeId() == TYPEID_UNIT && ((Creature*)unit)->AI())
        ((Creature*)unit)->AI()->ReceiveEmote(GetPlayer(), text_emote);
}

void WorldSession::HandleChatIgnoredOpcode(WorldPacket& recvData)
{
    ObjectGuid iguid;
    uint8 unk;
    //TC_LOG_DEBUG("network", "WORLD: Received CMSG_CHAT_IGNORED");

    recvData >> iguid;
    recvData >> unk;                                       // probably related to spam reporting

    Player* player = ObjectAccessor::FindConnectedPlayer(iguid);
    if (!player || !player->GetSession())
        return;

    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_IGNORED, LANG_UNIVERSAL, _player, _player, GetPlayer()->GetName());
    player->SendDirectMessage(&data);
}

void WorldSession::HandleChannelDeclineInvite(WorldPacket &recvPacket)
{
    TC_LOG_DEBUG("network", "Opcode {}", recvPacket.GetOpcode());
}

void WorldSession::SendPlayerNotFoundNotice(std::string const& name)
{
    WorldPacket data(SMSG_CHAT_PLAYER_NOT_FOUND, name.size()+1);
    data << name;
    SendPacket(&data);
}

void WorldSession::SendPlayerAmbiguousNotice(std::string const& name)
{
    WorldPacket data(SMSG_CHAT_PLAYER_AMBIGUOUS, name.size()+1);
    data << name;
    SendPacket(&data);
}

void WorldSession::SendWrongFactionNotice()
{
    WorldPacket data(SMSG_CHAT_WRONG_FACTION, 0);
    SendPacket(&data);
}

void WorldSession::SendChatRestrictedNotice(ChatRestrictionType restriction)
{
    WorldPacket data(SMSG_CHAT_RESTRICTED, 1);
    data << uint8(restriction);
    SendPacket(&data);
}