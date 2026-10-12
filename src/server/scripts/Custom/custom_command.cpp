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

#include "ScriptMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Language.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"          // [ADDED] FindPlayerByName 사용
#include "Pet.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "DBCStores.h"
#include "World.h"
#include "InstanceSaveMgr.h"   // InstanceSave 정의(complete type)
#include "GameTime.h"          // GameTime::GetGameTime()

#include "ScriptMgr.h"
#include "Chat.h"
#include "DBCStores.h"
#include "Language.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "RBAC.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"

#include "ScriptMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Group.h"
#include "Language.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

#include "Guild.h"
#include "GuildMgr.h"

#include "Bag.h"


#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

// ============================================================================
// Custom Update - 2026-07-13
// appdata.character_archive_log 등록 캐릭터 영구 삭제 명령어 (1개월/6개월 유형 구분)
// ============================================================================

using namespace Trinity::ChatCommands;

class custom_commandscript : public CommandScript
{
public:
    custom_commandscript() : CommandScript("custom_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable sendCommandTable =
        {
            // rbac 권한은 서버 환경에 맞춰 조정하세요.  HandleTeleCommand
            // RBAC_PERM_COMMAND_SEND_MESSAGE가 없으면 MOD 권한 등으로 대체할 수 있습니다.
            { "appmessage", HandleSendAppMessageCommand, rbac::RBAC_PERM_COMMAND_SEND_MESSAGE, Console::Yes }, // [CHANGED] 설명 주석 정리   HandleDeliveryGiftCommand
			{ "sendhome",		  HandleSendHomeCommand, rbac::RBAC_PERM_COMMAND_SEND_MESSAGE, Console::Yes },
			{ "appannounce",      HandleAppAnnounceCommand, rbac::RBAC_PERM_COMMAND_SEND_MESSAGE, Console::Yes },
			{ "owargift",      HandleOwarGiftCommand, rbac::RBAC_PERM_COMMAND_SEND_MESSAGE, Console::Yes },
			{ "unbind",       HandleInstanceUnbindCommand,       rbac::RBAC_PERM_COMMAND_SEND_MESSAGE,         Console::Yes },
			{ "customlearn",       HandleInstanceCustomlearnCommand,       rbac::RBAC_PERM_COMMAND_SEND_MESSAGE,         Console::Yes },
			{ "delivery",      HandleDeliveryGiftCommand, rbac::RBAC_PERM_COMMAND_SEND_MESSAGE, Console::Yes },
			{ "guildsay",      HandleSendGuildSayCommand, rbac::RBAC_PERM_COMMAND_SEND_MESSAGE, Console::Yes },
			{ "custommail",      HandleSendCustomMailCommand, rbac::RBAC_PERM_COMMAND_SEND_MESSAGE, Console::Yes },
			{ "pshopmail", HandleSendPersonalShopItemMailCommand, rbac::RBAC_PERM_COMMAND_SEND_MESSAGE, Console::Yes },
        };

		static ChatCommandTable titleCommandTable =
		{
			{ "customadd",    HandleCustomTitleAddCommand,    rbac::RBAC_PERM_COMMAND_SEND_MESSAGE,    Console::Yes },
			{ "customremove", HandleCustomTitleRemoveCommand, rbac::RBAC_PERM_COMMAND_SEND_MESSAGE, Console::Yes },
		};

        static ChatCommandTable archiveDeleteCommandTable =
        {
            { "1month", HandleArchiveDelete1MonthCommand, rbac::RBAC_PERM_COMMAND_SEND_MESSAGE, Console::Yes },
            { "6months", HandleArchiveDelete6MonthsCommand, rbac::RBAC_PERM_COMMAND_SEND_MESSAGE, Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "send", sendCommandTable },
			{ "title", titleCommandTable },
			{ "archivedelete", archiveDeleteCommandTable },
        };
        return commandTable;
    }

	// 사용법: .archivedelete 1month / .archivedelete 6months
	// appdata.character_archive_log의 archive_type별로 현재 존재하는 캐릭터를 영구 삭제한다.
	static bool HandleArchiveDeleteByType(ChatHandler* handler, char const* archiveType, char const* archiveLabel)
	{
		if (!handler)
			return false;

		QueryResult result = CharacterDatabase.PQuery(
			"SELECT DISTINCT c.guid, c.account "
			"FROM appdata.character_archive_log AS archive_log "
			"INNER JOIN characters.characters AS c ON c.guid = archive_log.guid "
			"WHERE archive_log.archive_type = '{}'", archiveType);

		if (!result)
		{
			handler->SendSysMessage(
				Trinity::StringFormat("{} 유형에 삭제할 캐릭터가 없습니다.", archiveLabel).c_str()
			);
			return true;
		}

		uint32 deletedCount = 0;
		do
		{
			Field* fields = result->Fetch();
			uint32 guidLow = fields[0].GetUInt32();
			uint32 accountId = fields[1].GetUInt32();
			ObjectGuid playerGuid = ObjectGuid::Create<HighGuid::Player>(guidLow);

			if (Player* player = ObjectAccessor::FindConnectedPlayer(playerGuid))
				if (WorldSession* session = player->GetSession())
					session->KickPlayer("archivedelete command deleting archived character");

			Player::DeleteFromDB(playerGuid, accountId, true, true);
			++deletedCount;
		} while (result->NextRow());

		handler->SendSysMessage(
			Trinity::StringFormat("{} 유형의 캐릭터 {}명을 삭제했습니다.", archiveLabel, deletedCount).c_str()
		);

		return true;
	}

	static bool HandleArchiveDelete1MonthCommand(ChatHandler* handler)
	{
		return HandleArchiveDeleteByType(handler, "under40_1month", "1개월 미접속 (40레벨 미만)");
	}

	static bool HandleArchiveDelete6MonthsCommand(ChatHandler* handler)
	{
		return HandleArchiveDeleteByType(handler, "inactive_6months", "6개월 미접속");
	}

	// global announce
	static bool HandleAppAnnounceCommand(ChatHandler* handler, Tail message)
	{
		if (message.empty())
			return false;

		// 1) 첫 토큰이 A/H 인지 확인
		std::string_view msg = message;
		while (!msg.empty() && std::isspace(static_cast<unsigned char>(msg.front())))
			msg.remove_prefix(1);

		char target = '\0'; // '\0' = all, 'A' = alliance, 'H' = horde
		if (!msg.empty())
		{
			// 첫 글자만 보고 판단 (A/H 뒤는 공백이거나 문자열 끝이어야 함)
			char c = static_cast<char>(std::toupper(static_cast<unsigned char>(msg.front())));
			if ((c == 'A' || c == 'H') && (msg.size() == 1 || std::isspace(static_cast<unsigned char>(msg[1]))))
			{
				target = c;

				// "A" 또는 "H" 토큰 제거
				msg.remove_prefix(1);
				while (!msg.empty() && std::isspace(static_cast<unsigned char>(msg.front())))
					msg.remove_prefix(1);

				if (msg.empty())
					return false; // A/H만 있고 내용이 없음
			}
		}

		// 시스템 메시지 포맷(기존 유지)
		std::string text = handler->PGetParseString(LANG_SYSTEMMESSAGE, msg.data());

		// 2) 대상에 따라 전송
		if (target == '\0')
		{
			// 전체 공지
			sWorld->SendServerMessage(SERVER_MSG_STRING, text);
			return true;
		}

		// 얼라/호드만 공지: 세션 순회로 직접 전송
		SessionMap const& sessions = sWorld->GetAllSessions();
		for (auto const& [_, session] : sessions)
		{
			if (!session)
				continue;

			Player* plr = session->GetPlayer();
			if (!plr)
				continue;

			// ALLIANCE=0, HORDE=1 (TeamId)
			if ((target == 'A' && plr->GetTeamId() == TEAM_ALLIANCE) ||
				(target == 'H' && plr->GetTeamId() == TEAM_HORDE))
			{
				session->SendNotification("%s", text.c_str()); // 서버 메시지 느낌으로 팝업
				// 원하면 채팅 시스템 메시지로 바꾸고 싶을 때는 다른 패킷/헬퍼로 교체 가능
			}
		}

		return true;
	}

    // 사용법: .send appmessage <playerName> <message...>
    static bool HandleSendAppMessageCommand(ChatHandler* handler, char const* args)
    {
        // [CHANGED] 인자 검증 보강 및 안전한 파싱
        if (!args || !*args)
        {
            handler->SendSysMessage("Usage: .send appmessage <playerName> <message>");
            handler->SetSentErrorMessage(true);
            return false;
        }

        // [CHANGED] 첫 공백 기준으로 <플레이어명> / <메시지> 분리
        std::string full(args);
        // 앞/뒤 공백 제거
        auto ltrim = [](std::string& s){ s.erase(0, s.find_first_not_of(" \t\r\n")); };
        auto rtrim = [](std::string& s){ s.erase(s.find_last_not_of(" \t\r\n")+1); };
        ltrim(full); rtrim(full);

        size_t sp = full.find(' ');
        std::string targetName = sp == std::string::npos ? full : full.substr(0, sp);
        std::string msg        = sp == std::string::npos ? std::string() : full.substr(sp + 1);
        ltrim(targetName); rtrim(targetName);
        ltrim(msg); rtrim(msg);

        if (targetName.empty() || msg.empty())
        {
            handler->SendSysMessage("Usage: .send appmessage <playerName> <message>");
            handler->SetSentErrorMessage(true);
            return false;
        }

        // [CHANGED] 플레이어 탐색: 이름으로 온라인 플레이어 검색
        // (선택 대상/가까운 대상 추출 대신 명시적 이름 기반으로 단정)
        Player* player = ObjectAccessor::FindPlayerByName(targetName);
        if (!player || !player->GetSession())
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // [CHANGED] 로그아웃 중인 세션 방어
        if (player->GetSession()->isLogingOut())
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // [CHANGED] 메시지 1회만 전송 (프리픽스 포함) + 색상 코드 정리
        // 기존: 메시지 전송 후 "[APP]"를 또 한 줄 전송 → 한 줄에 묶어서 표시
   //     player->GetSession()->SendAreaTriggerMessage("|cffff3030[APP]|r %s", msg.c_str());

		// 메시지 전송
		std::ostringstream ss;
		ss << "|cffff80ff[APP Whisper]:|r " << msg;
		ChatHandler(player->GetSession()).SendSysMessage(ss.str().c_str());


        // [CHANGED] 확인 응답(로컬): 다국어 상수 대신 직관 메시지
        handler->PSendSysMessage("Sent APP message to %s: %s", player->GetName().c_str(), msg.c_str());

        return true;
    }

	static bool HandleSendHomeCommand(ChatHandler* handler, char const* args)
	{
		if (!handler || !args || !*args)
			return false;

		// 캐릭터 이름 파싱
		std::string playerName = args;

		Player* target = ObjectAccessor::FindConnectedPlayerByName(playerName.c_str());
		if (!target)
		{
			handler->SendSysMessage(
				Trinity::StringFormat("대상이 온라인이 아닙니다: {}", playerName).c_str()
			);
			return true;
		}

		// 대상 캐릭의 귀환지(Homebind)로 이동
		target->TeleportTo(
			target->m_homebindMapId,
			target->m_homebindX,
			target->m_homebindY,
			target->m_homebindZ,
			target->GetOrientation()
		);

		handler->SendSysMessage(
			Trinity::StringFormat("귀환지로 이동 완료: {}", playerName).c_str()
		);

		return true;
	}

	static bool HandleOwarGiftCommand(ChatHandler* handler, char const* args)
	{
		if (!handler || !args || !*args)
			return false;

		char side = static_cast<char>(std::tolower(*args));

		TeamId teamId;
		if (side == 'a')
			teamId = TEAM_ALLIANCE;
		else if (side == 'h')
			teamId = TEAM_HORDE;
		else
		{
			handler->SendSysMessage("사용법: .app a  또는  .app h");
			return true;
		}

		constexpr uint32 kWintergraspZoneId = 4197;

		auto IsOwarGiftExcludedPlayer = [](Player* plr) -> bool
		{
			if (!plr)
				return true;

			if (!plr->IsInWorld())
				return true;

			// 겨울손아귀 호수 제외
			if (plr->GetZoneId() == 4197)
				return true;

			// 일반 전장 , 투기장은 제외
			if (plr->InBattleground() && plr->InArena())
				return true;

			return false;
		};

		// ─────────────────────────────────────────────────────────
		// 3가지 랜덤:
		//   1) 버프 A (확률 높음)
		//   2) 버프 B (확률 높음)
		//   3) 소환물 경호 이벤트 (희귀, 지금은 TODO)
		//
		// 확률 예시:
		//   - 1~45 : 버프 A (45%)
		//   - 46~90: 버프 B (45%)
		//   - 91~100: 소환물(10%)
		// ─────────────────────────────────────────────────────────
		constexpr uint32 kBuffSpellA = 29235;
		constexpr uint32 kBuffSpellB = 34410;

		uint32 roll = urand(1, 100);

		enum class GiftKind : uint8
		{
			BuffA = 1,
			BuffB = 2,
			SummonEvent = 3
		};

		GiftKind kind = GiftKind::BuffA;
		if (roll <= 45)
			kind = GiftKind::BuffA;
		else if (roll <= 90)
			kind = GiftKind::BuffB;
		else
			kind = GiftKind::SummonEvent;

		uint32 hit = 0;
		uint32 appliedSpell = 0;

		// ─────────────────────────────────────────────────────────
		// 1) / 2) 버프 처리
		// ─────────────────────────────────────────────────────────
		if (kind == GiftKind::BuffA || kind == GiftKind::BuffB)
		{
			appliedSpell = (kind == GiftKind::BuffA) ? kBuffSpellA : kBuffSpellB;

			HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();
			for (auto const& kv : players)
			{
				Player* plr = kv.second;
				if (!plr) continue;
				if (!plr->IsInWorld()) continue;
				if (plr->GetTeamId() != teamId) continue;

				plr->CastSpell(plr, appliedSpell, true);

				++hit;
			}
		}
		else
		{
			appliedSpell = (kind == GiftKind::BuffA) ? kBuffSpellA : kBuffSpellB;

			HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();
			for (auto const& kv : players)
			{
				Player* plr = kv.second;
				if (!plr) continue;
				if (!plr->IsInWorld()) continue;
				if (plr->GetTeamId() != teamId) continue;

				plr->CastSpell(plr, appliedSpell, true);

				++hit;
			}
		}

		std::string teamName = (teamId == TEAM_ALLIANCE ? "얼라이언스" : "호드");
		std::string msg = "";
		msg += "[오늘의 버킷 리스트 달성] ";
/*		msg += teamName;
		msg += "가 일일 할일을  달성하였습니다.!";*/
		sWorld->SendServerMessage(SERVER_MSG_STRING, msg.c_str(), 0);

		return true;
	}

   //인던 촉초기화
	static bool HandleInstanceUnbindCommand(
    ChatHandler* handler,
    Variant<uint16, EXACT_SEQUENCE("all")> mapArg,
    Optional<PlayerIdentifier> playerArg,            // [NEW] 캐릭터명 인자
    Optional<uint8> difficultyArg)
{
    Player* player = nullptr;

    // [NEW] 캐릭터명 인자가 있으면 그 플레이어를 우선 사용 (온라인 캐릭만)
    if (playerArg)
        player = playerArg->GetConnectedPlayer();

    // 기존 동작 유지: 캐릭명 인자가 없으면 선택된 대상, 없으면 자기 자신
    if (!player)
    {
        player = handler->getSelectedPlayer();
        if (!player)
            player = handler->GetSession()->GetPlayer();
    }

    if (!player)
        return false;

    uint16 counter = 0;
    uint16 mapId = 0;

    if (mapArg.holds_alternative<uint16>())
    {
        mapId = mapArg.get<uint16>();
        if (!mapId)
            return false;
    }

    for (uint8 i = 0; i < MAX_DIFFICULTY; ++i)
    {
        Player::BoundInstancesMap& binds = player->GetBoundInstances(Difficulty(i));

        for (Player::BoundInstancesMap::iterator itr = binds.begin(); itr != binds.end();)
        {
            InstanceSave const* save = itr->second.save;

            if (itr->first != player->GetMapId()
                && (!mapId || mapId == itr->first)
                && (!difficultyArg || difficultyArg.value() == save->GetDifficulty()))
            {
                std::string timeleft =
                    secsToTimeString(save->GetResetTime() - GameTime::GetGameTime(), TimeFormat::ShortText);

                handler->PSendSysMessage(
                    LANG_COMMAND_INST_UNBIND_UNBINDING,
                    itr->first,
                    save->GetInstanceId(),
                    itr->second.perm ? "yes" : "no",
                    save->GetDifficulty(),
                    save->CanReset() ? "yes" : "no",
                    timeleft.c_str());

                player->UnbindInstance(itr, Difficulty(i));
                ++counter;
            }
            else
                ++itr;
        }
    }

    handler->PSendSysMessage(LANG_COMMAND_INST_UNBIND_UNBOUND, counter);
    return true;
}

static bool HandleInstanceCustomlearnCommand(ChatHandler* /*handler*/, char const* args)
{
    if (!args || !*args)
        return false;

    char* spellStr = strtok((char*)args, " ");
    char* nameStr  = strtok(nullptr, " ");

    if (!spellStr || !nameStr)
        return false;

    uint32 spellId = static_cast<uint32>(atoi(spellStr));

    Player* target = ObjectAccessor::FindConnectedPlayerByName(nameStr);
    if (!target)
        return false;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return false;

    // ─────────────────────────────────────
    // 1) 주문 습득
    // ─────────────────────────────────────
    if (!target->HasSpell(spellId))
		target->LearnSpell(spellId, false);

    // ─────────────────────────────────────
    // 2) character_flags UPSERT (taxi_cheat 활성화)
    // ─────────────────────────────────────
    uint32 guidLow = target->GetGUID().GetCounter();

    CharacterDatabase.PExecute(
        "INSERT INTO appdata.character_flags (guid, taxi_cheat) "
        "VALUES ({}, 1) "
        "ON DUPLICATE KEY UPDATE "
        "  taxi_cheat = 1, "
        "  updated_at = CURRENT_TIMESTAMP",
        guidLow
    );

	return true;
}

static bool HandleDeliveryGiftCommand(ChatHandler* handler)
{
    if (!handler)
        return false;

    constexpr uint32 kBuffSpellA = 16609;
    constexpr uint32 kBuffSpellB = 24425;
    constexpr uint32 kBuffSpellC = 22888;
    constexpr uint32 kBuffSpellD = 34410;
    constexpr uint32 kBuffSpellE = 29235;

    std::array<uint32, 5> buffs =
    {
        kBuffSpellA,
        kBuffSpellB,
        kBuffSpellC,
        kBuffSpellD,
        kBuffSpellE
    };

    uint32 hit = 0;

    HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();
    for (auto const& kv : players)
    {
		uint32 index = urand(0, buffs.size() - 1);
		uint32 selectedSpell = buffs[index];

        Player* plr = kv.second;
        if (!plr) continue;
        if (!plr->IsInWorld()) continue;

   //     plr->AddAura(selectedSpell, plr);
        ++hit;
    }

    std::string msg = "오늘의 버킷 리스트 달성! 서버 버프 적용";
    sWorld->SendServerMessage(SERVER_MSG_STRING, msg.c_str(), 0);

    return true;
}

static bool HandleSendGuildSayCommand(ChatHandler* handler, char const* args)
{
    if (!handler || !args || !*args)
        return false;

    std::string full(args);

    auto trim = [](std::string& s)
    {
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            s.clear();
            return;
        }

        size_t last = s.find_last_not_of(" \t\r\n");
        s = s.substr(first, last - first + 1);
    };

    trim(full);

    size_t firstSpace = full.find(' ');
    if (firstSpace == std::string::npos)
        return false;

    std::string guildIdText = full.substr(0, firstSpace);
    std::string remain = full.substr(firstSpace + 1);

    trim(guildIdText);
    trim(remain);

    size_t secondSpace = remain.find(' ');
    if (secondSpace == std::string::npos)
        return false;

    std::string senderName = remain.substr(0, secondSpace);
    std::string message = remain.substr(secondSpace + 1);

    trim(senderName);
    trim(message);

    uint32 guildId = uint32(strtoul(guildIdText.c_str(), nullptr, 10));

    if (guildId == 0 || senderName.empty() || message.empty())
        return false;

    if (message.size() >= 2 && message.front() == '"' && message.back() == '"')
        message = message.substr(1, message.size() - 2);

    trim(message);

    if (message.empty())
        return false;

    Guild* guild = sGuildMgr->GetGuildById(guildId);
    if (!guild)
        return false;

    std::string safeSenderName = senderName;
    CharacterDatabase.EscapeString(safeSenderName);

    QueryResult result = CharacterDatabase.PQuery(
        "SELECT c.guid "
        "FROM characters.characters c "
        "JOIN characters.guild_member gm ON gm.guid = c.guid "
        "WHERE gm.guildid = {} "
        "AND c.name = '{}' "
        "LIMIT 1",
        guildId,
        safeSenderName
    );

    if (!result)
        return false;

    Field* fields = result->Fetch();
    uint32 senderGuidLow = fields[0].GetUInt32();

    ObjectGuid senderGuid = ObjectGuid::Create<HighGuid::Player>(senderGuidLow);

    WorldPacket data;
    ChatHandler::BuildChatPacket(
        data,
        CHAT_MSG_GUILD,
        LANG_UNIVERSAL,
        senderGuid,
        ObjectGuid::Empty,
        message,
        CHAT_TAG_NONE,
        senderName
    );

    guild->BroadcastPacket(&data);

    return true;
}

static bool HandleSendCustomMailCommand(ChatHandler* /*handler*/, char const* args)
{
    if (!args || !*args)
        return true;

    std::string full(args);

    auto trim = [](std::string& s)
    {
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            s.clear();
            return;
        }

        size_t last = s.find_last_not_of(" \t\r\n");
        s = s.substr(first, last - first + 1);
    };

    trim(full);

    size_t pos1 = full.find_first_of(" \t\r\n");
    if (pos1 == std::string::npos)
        return true;

    std::string senderName = full.substr(0, pos1);
    std::string rest = full.substr(pos1 + 1);

    trim(senderName);
    trim(rest);

    if (senderName.empty() || rest.empty())
        return true;

    size_t pos2 = rest.find_first_of(" \t\r\n");
    if (pos2 == std::string::npos)
        return true;

    std::string receiverName = rest.substr(0, pos2);
    std::string body = rest.substr(pos2 + 1);

    trim(receiverName);
    trim(body);

    if (receiverName.empty() || body.empty())
        return true;

    if (body.size() >= 2 && body.front() == '"' && body.back() == '"')
        body = body.substr(1, body.size() - 2);

    trim(body);

    if (body.empty())
        return true;

    normalizePlayerName(senderName);
    normalizePlayerName(receiverName);

    std::string senderSqlName = senderName;
    std::string receiverSqlName = receiverName;

    CharacterDatabase.EscapeString(senderSqlName);
    CharacterDatabase.EscapeString(receiverSqlName);

    std::string senderSql =
        "SELECT guid FROM characters.characters WHERE name = '" +
        senderSqlName +
        "' LIMIT 1";

    QueryResult senderResult = CharacterDatabase.Query(senderSql.c_str());

    if (!senderResult)
        return true;

    Field* senderFields = senderResult->Fetch();
    uint64 senderGuid = senderFields[0].GetUInt64();

    std::string receiverSql =
        "SELECT guid FROM characters.characters WHERE name = '" +
        receiverSqlName +
        "' LIMIT 1";

    QueryResult receiverResult = CharacterDatabase.Query(receiverSql.c_str());

    if (!receiverResult)
        return true;

    Field* receiverFields = receiverResult->Fetch();
    uint64 receiverGuid = receiverFields[0].GetUInt64();

    MailDraft draft(
        "[도움 우편함] 답변이 도착했습니다",
        body
    );

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    draft.SendMailTo(
        trans,
        MailReceiver(nullptr, receiverGuid),
        MailSender(MAIL_NORMAL, senderGuid, MAIL_STATIONERY_DEFAULT)
    );

    CharacterDatabase.CommitTransaction(trans);

    return true;
}


static Player* FindOnlinePlayerForCustomTitle(std::string playerName)
{
    if (playerName.empty())
        return nullptr;

    normalizePlayerName(playerName);

    return ObjectAccessor::FindConnectedPlayerByName(playerName.c_str());
}

static bool HandleCustomTitleAddCommand(ChatHandler* handler, uint16 titleId, std::string_view characterName)
{
    if (!handler)
        return false;

    std::string targetName(characterName);

    if (titleId == 0 || targetName.empty())
    {
        handler->SendSysMessage("사용법: .title add 칭호ID 캐릭터이름");
        return true;
    }

    CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(titleId);
    if (!titleInfo)
    {
        handler->SendSysMessage(
            Trinity::StringFormat("존재하지 않는 칭호 ID입니다: {}", titleId).c_str()
        );
        return true;
    }

    Player* target = FindOnlinePlayerForCustomTitle(targetName);
    if (!target || !target->GetSession())
    {
        handler->SendSysMessage(
            Trinity::StringFormat("캐릭터가 온라인이 아니거나 찾을 수 없습니다: {}", targetName).c_str()
        );
        return true;
    }

    if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
        return false;

    if (target->HasTitle(titleInfo))
    {
        handler->SendSysMessage(
            Trinity::StringFormat("{} 캐릭터는 이미 해당 칭호를 보유하고 있습니다. 칭호 ID: {}", target->GetName(), titleId).c_str()
        );
        return true;
    }

    target->SetTitle(titleInfo);

    handler->SendSysMessage(
        Trinity::StringFormat("{} 캐릭터에게 칭호를 지급했습니다. 칭호 ID: {}", target->GetName(), titleId).c_str()
    );

    return true;
}

static bool HandleCustomTitleRemoveCommand(ChatHandler* handler, uint16 titleId, std::string_view characterName)
{
    if (!handler)
        return false;

    std::string targetName(characterName);

    if (titleId == 0 || targetName.empty())
    {
        handler->SendSysMessage("사용법: .title remove 칭호ID 캐릭터이름");
        return true;
    }

    CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(titleId);
    if (!titleInfo)
    {
        handler->SendSysMessage(
            Trinity::StringFormat("존재하지 않는 칭호 ID입니다: {}", titleId).c_str()
        );
        return true;
    }

    Player* target = FindOnlinePlayerForCustomTitle(targetName);
    if (!target || !target->GetSession())
    {
        handler->SendSysMessage(
            Trinity::StringFormat("캐릭터가 온라인이 아니거나 찾을 수 없습니다: {}", targetName).c_str()
        );
        return true;
    }

    if (handler->HasLowerSecurity(target, ObjectGuid::Empty))
        return false;

    if (!target->HasTitle(titleInfo))
    {
        handler->SendSysMessage(
            Trinity::StringFormat("{} 캐릭터는 해당 칭호를 보유하고 있지 않습니다. 칭호 ID: {}", target->GetName(), titleId).c_str()
        );
        return true;
    }

    target->SetTitle(titleInfo, true);

    if (!target->HasTitle(target->GetInt32Value(PLAYER_CHOSEN_TITLE)))
        target->SetUInt32Value(PLAYER_CHOSEN_TITLE, 0);

    handler->SendSysMessage(
        Trinity::StringFormat("{} 캐릭터에게서 칭호를 제거했습니다. 칭호 ID: {}", target->GetName(), titleId).c_str()
    );

    return true;
}


//개인상점
static Item* GetAItemFromDB(uint32 itemGuid, ChatHandler* handler)
{
    (void)handler;

    QueryResult result = CharacterDatabase.PQuery(
        "SELECT owner_guid, itemEntry, "
        "creatorGuid, giftCreatorGuid, count, duration, charges, flags, enchantments, randomPropertyId, durability, playedTime, text, "
        "0, 0, 0, 0, 0, transmog, enchant "
        "FROM item_instance "
        "WHERE guid = {} "
        "LIMIT 1",
        itemGuid
    );

    if (!result)
        return nullptr;

    Field* fields = result->Fetch();

    uint32 ownerGuidLow = fields[0].GetUInt32();
    uint32 itemEntry = fields[1].GetUInt32();

    if (ownerGuidLow == 0)
        return nullptr;

    ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemEntry);
	if (!itemTemplate)
		return nullptr;

	Item* item = NewItemOrBag(itemTemplate);
	if (!item)
		return nullptr;

    ObjectGuid ownerGuid = ObjectGuid::Create<HighGuid::Player>(ownerGuidLow);

    // fields + 2 이후 기준:
    // fields[0]  = creatorGuid
    // fields[1]  = giftCreatorGuid
    // fields[2]  = count
    // fields[3]  = duration
    // fields[4]  = charges
    // fields[5]  = flags
    // fields[6]  = enchantments
    // fields[7]  = randomPropertyId
    // fields[8]  = durability
    // fields[9]  = playedTime
    // fields[10] = text
    // fields[16] = transmog
    // fields[17] = enchant
    if (!item->LoadFromDB(itemGuid, ownerGuid, fields + 2, itemEntry))
    {
        delete item;
        return nullptr;
    }

    return item;
}

static bool HandleSendPersonalShopItemMailCommand(ChatHandler* handler, char const* args)
{
    if (!handler || !args || !*args)
        return true;

    std::istringstream iss(args);

    uint32 itemGuid = 0;
    uint32 buyerGuid = 0;

    if (!(iss >> itemGuid >> buyerGuid))
    {
        handler->SendSysMessage("사용법: .send pshopmail {item_guid} {buyer_guid}");
        return true;
    }

    if (itemGuid == 0 || buyerGuid == 0)
    {
        handler->SendSysMessage("개인상점 우편 발송 실패: 인자 오류");
        return true;
    }

    QueryResult buyerResult = CharacterDatabase.PQuery(
        "SELECT guid, name "
        "FROM characters "
        "WHERE guid = {} "
        "LIMIT 1",
        buyerGuid
    );

    if (!buyerResult)
    {
        handler->SendSysMessage(
            Trinity::StringFormat("개인상점 우편 발송 실패: 구매자 캐릭터 없음 buyer_guid={}", buyerGuid).c_str()
        );

        return true;
    }

    Item* pItem = GetAItemFromDB(itemGuid, handler);
    if (!pItem)
    {
        handler->SendSysMessage(
            Trinity::StringFormat("개인상점 우편 발송 실패: DB 아이템 로드 실패 item_guid={}", itemGuid).c_str()
        );
        return true;
    }

    ObjectGuid buyerObjectGuid = ObjectGuid::Create<HighGuid::Player>(buyerGuid);
    Player* buyer = ObjectAccessor::FindConnectedPlayer(buyerObjectGuid);

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    pItem->SetBinding(false);
    pItem->SetOwnerGUID(buyerObjectGuid);
    pItem->SetState(ITEM_CHANGED);

    pItem->SaveToDB(trans);

    MailDraft("개인상점 구매 물품", "개인상점에서 구매한 물품입니다.")
        .AddItem(pItem)
        .SendMailTo(
            trans,
            MailReceiver(buyer, buyerGuid),
            MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_DEFAULT),
            MAIL_CHECK_MASK_COPIED,
            0
        );

    CharacterDatabase.CommitTransaction(trans);

    return true;
}









};

void AddSC_custom_commandscript()
{
    new custom_commandscript();
}