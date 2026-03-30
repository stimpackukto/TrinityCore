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


#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

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
        };

        static ChatCommandTable commandTable =
        {
            { "send", sendCommandTable },
        };
        return commandTable;
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

	// ─────────────────────────────────────
    // 3) 델레포터  5804.149 624.77 647.767 1.64
    // ─────────────────────────────────────
	target->TeleportTo(571, 5804.149f, 624.77f, 647.767f, 1.64f); 

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

        plr->AddAura(selectedSpell, plr);
        ++hit;
    }

    std::string msg = "오늘의 버킷 리스트 달성! 서버 버프 적용";
    sWorld->SendServerMessage(SERVER_MSG_STRING, msg.c_str(), 0);

    return true;
}













};

void AddSC_custom_commandscript()
{
    new custom_commandscript();
}
