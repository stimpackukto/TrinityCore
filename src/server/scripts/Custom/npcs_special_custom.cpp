#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Creature.h"
#include "Player.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"
#include "Group.h"
#include "Config.h"
#include "Map.h"
#include "GameTime.h"
#include "Log.h"
#include "World.h"
#include "Random.h"
#include "Channel.h"
#include "Item.h"
#include "StringFormat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Chat.h"
#include "ObjectAccessor.h"
#include "MapManager.h"
#include "Mail.h"
#include "ObjectGuid.h"
#include "ScriptedCreature.h"
#include "SharedDefines.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Vehicle.h"

#include <inttypes.h>
#include <unordered_map>
#include <array>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <string>
#include <cstdlib>
#include <iomanip>
#include <cctype>
#include <curl/curl.h>

#include "ScriptMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"
#include "ObjectGuid.h"
#include "StringFormat.h"
#include "GameObject.h"

#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <filesystem>

#include "ScriptMgr.h"
#include "World.h"
#include "MapManager.h"
#include "Map.h"
#include "GameObject.h"
#include "ObjectMgr.h"
#include "ObjectGuid.h"
#include "Object.h"
#include "Position.h"

// ─────────────────────────────────────────────
// RP 유틸
// ─────────────────────────────────────────────
namespace RP
{
    static inline void EnsureRow(uint32 accountId)
    {
        CharacterDatabase.PExecute(
            "INSERT INTO appdata.account_data (id, reg_point) VALUES ({}, 0) "
            "ON DUPLICATE KEY UPDATE reg_point = reg_point",
            accountId);
    }

    static inline long long Get(uint32 accountId)
    {
        EnsureRow(accountId);
        if (QueryResult r = CharacterDatabase.PQuery(
                "SELECT IFNULL(reg_point,0) FROM appdata.account_data WHERE id={}",
                accountId))
        {
            return r->Fetch()[0].GetInt64();
        }
        return 0;
    }

    static inline bool TrySpendOne(uint32 accountId, uint32 amount, long long& outAfter)
    {
        EnsureRow(accountId);

        long long before = Get(accountId);

        if (before < static_cast<long long>(amount))
        {
            outAfter = before;
            return false;
        }

        CharacterDatabase.PExecute(
            "UPDATE appdata.account_data SET reg_point = reg_point - {} "
            "WHERE id = {} AND reg_point >= {}",
            amount, accountId, amount);

        outAfter = before - static_cast<long long>(amount);
        return true;
    }

    // [ADDED] RP 지급(증가)
    static inline void Add(uint32 accountId, uint32 amount)
    {
        if (amount == 0)
            return;

        EnsureRow(accountId);
        CharacterDatabase.PExecute(
            "UPDATE appdata.account_data SET reg_point = reg_point + {} WHERE id={}",
            amount, accountId);
    }
} // namespace RP

// ─────────────────────────────────────────────
// 계정 골드 은행 NPC
// ─────────────────────────────────────────────
namespace
{
    constexpr uint64 MAX_SAFE_GOLD   = 214000;
    constexpr uint64 MAX_SAFE_COPPER = MAX_SAFE_GOLD * GOLD;

    // [ADDED] 골드 -> RP 환율 (운영값)
    // 추천: 10,000G = 1RP
    constexpr uint64 GOLD_PER_RP = 10000;

    // [ADDED] 1회 교환 상한 (원하면 조절/삭제)
    constexpr uint32 MAX_RP_PER_TX = 200;

    enum BankActions : uint32
    {
        ACTION_SHOW_BALANCE  = 1,
        ACTION_DEPOSIT       = 2,
        ACTION_WITHDRAW      = 3,
        ACTION_HELP          = 4,

        // [ADDED] 골드 -> RP 교환
        ACTION_EXCHANGE_GOLD_TO_RP = 5
    };

    uint64 GetBankGold(uint32 accountId)
    {
        std::string sql = "SELECT `gold` FROM `account_gold_bank` WHERE `account` = " + std::to_string(accountId);
        QueryResult result = CharacterDatabase.Query(sql.c_str());
        if (!result)
            return 0;

        Field* fields = result->Fetch();
        return fields[0].GetUInt64();
    }

    void SetBankGold(uint32 accountId, uint64 gold)
    {
        std::ostringstream ss;
        ss << "INSERT INTO `account_gold_bank` (`account`, `gold`) VALUES ("
           << accountId << ", " << gold << ") "
           << "ON DUPLICATE KEY UPDATE `gold` = VALUES(`gold`)";

        CharacterDatabase.Execute(ss.str().c_str());
    }

    uint64 ParsePositiveGold(char const* text)
    {
        if (!text)
            return 0;

        while (*text == ' ' || *text == '\t')
            ++text;

        if (*text == '\0')
            return 0;

        char* endPtr = nullptr;
        unsigned long long v = std::strtoull(text, &endPtr, 10);
        if (endPtr == text || v == 0)
            return 0;

        return static_cast<uint64>(v);
    }
}

class npc_gold_bank : public CreatureScript
{
public:
    npc_gold_bank() : CreatureScript("npc_gold_bank") { }

    struct npc_gold_bankAI : public ScriptedAI
    {
        npc_gold_bankAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            if (!player)
                return false;

            ClearGossipMenuFor(player);

            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                "내 은행 골드 확인", GOSSIP_SENDER_MAIN, ACTION_SHOW_BALANCE);

            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                "골드 예금하기", GOSSIP_SENDER_MAIN, ACTION_DEPOSIT,
                "예금할 골드 양을 입력하세요. (골드 단위, 정수)", 0, true);

            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                "골드 인출하기", GOSSIP_SENDER_MAIN, ACTION_WITHDRAW,
                "인출할 골드 양을 입력하세요. (골드 단위, 정수)", 0, true);

            // [ADDED] 골드 -> RP 교환 메뉴
            {
                std::ostringstream label;
                label << "골드 → RP 교환 (" << GOLD_PER_RP << "골드 = 1RP)";
                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG,
                    label.str(), GOSSIP_SENDER_MAIN, ACTION_EXCHANGE_GOLD_TO_RP,
                    "교환할 골드 양을 입력하세요. (골드 단위, 정수)", 0, true);
            }

            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                "설명 보기", GOSSIP_SENDER_MAIN, ACTION_HELP);

            SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            if (!player)
                return false;

            WorldSession* session = player->GetSession();
            if (!session)
                return false;

            ChatHandler ch(session);

            uint32 const sender = player->PlayerTalkClass->GetGossipOptionSender(gossipListId);
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

            ClearGossipMenuFor(player);

            if (sender != GOSSIP_SENDER_MAIN)
            {
                CloseGossipMenuFor(player);
                return true;
            }

            switch (action)
            {
                case ACTION_SHOW_BALANCE:
                {
                    uint32 accountId = session->GetAccountId();
                    uint64 gold = GetBankGold(accountId);

                    auto msg = Trinity::StringFormat("현재 계정 은행 잔액: {} 골드", gold);
                    ch.SendSysMessage(msg.c_str());
                    CloseGossipMenuFor(player);
                    return true;
                }
                case ACTION_HELP:
                {
                    ch.SendSysMessage("계정 골드 은행 안내:");
                    ch.SendSysMessage("- 이 NPC는 계정 단위 골드를 보관합니다.");
                    ch.SendSysMessage("- 예금: 현재 캐릭터의 소지 골드를 계정 은행에 넣습니다.");
                    ch.SendSysMessage("- 인출: 계정 은행의 골드를 현재 캐릭터가 꺼냅니다.");
                    ch.SendSysMessage("- 클라이언트 한도 때문에 한 번에 너무 많은 골드를 인출할 수 없습니다.");
                    ch.SendSysMessage("- 예금 / 인출이 실제로 완료되면 RP 1이 수수료로 소모됩니다.");

                    // [ADDED] 골드->RP 안내
                    {
                        std::ostringstream ss;
                        ss << "- 골드 → RP 교환: " << GOLD_PER_RP << "골드당 1RP 지급 (거래당 최대 " << MAX_RP_PER_TX << "RP)";
                        ch.SendSysMessage(ss.str().c_str());
                        ch.SendSysMessage("- 입력한 골드 중 교환 가능한 만큼만 차감되며, 나머지는 그대로 남습니다.");
                    }

                    CloseGossipMenuFor(player);
                    return true;
                }
                default:
                    break;
            }

            CloseGossipMenuFor(player);
            return true;
        }

        bool OnGossipSelectCode(Player* player, uint32 /*menuId*/, uint32 gossipListId, char const* code) override
        {
            if (!player)
                return false;

            WorldSession* session = player->GetSession();
            if (!session)
                return false;

            ChatHandler ch(session);

            uint32 const sender = player->PlayerTalkClass->GetGossipOptionSender(gossipListId);
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

            if (sender != GOSSIP_SENDER_MAIN)
            {
                CloseGossipMenuFor(player);
                return true;
            }

            uint32 accountId = session->GetAccountId();
            uint64 bankGold  = GetBankGold(accountId);
            uint64 inputGold = ParsePositiveGold(code);

            if (inputGold == 0)
            {
                ch.SendSysMessage("0 이하 값은 사용할 수 없습니다. 정수 골드만 입력해 주세요.");
                CloseGossipMenuFor(player);
                return true;
            }

            switch (action)
            {
                case ACTION_DEPOSIT:
                {
                    uint64 playerCopper = player->GetMoney();
                    uint64 needCopper   = inputGold * GOLD;

                    if (needCopper == 0 || needCopper > playerCopper)
                    {
                        ch.SendSysMessage("소지 골드가 부족합니다.");
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    long long rpAfter = 0;
                    if (!RP::TrySpendOne(accountId, 1, rpAfter))
                    {
                        auto msg = Trinity::StringFormat("RP 부족: 현재 {} RP (필요: 1)", rpAfter);
                        ch.SendSysMessage(msg.c_str());
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    player->ModifyMoney(-static_cast<int64>(needCopper));

                    uint64 newBankGold = bankGold + inputGold;
                    SetBankGold(accountId, newBankGold);

                    auto msg1 = Trinity::StringFormat("골드 예금 완료: {} 골드 (계정 은행 잔액: {} 골드)", inputGold, newBankGold);
                    ch.SendSysMessage(msg1.c_str());

                    auto msg2 = Trinity::StringFormat("RP 1 소모됨. 잔여 RP: {}", rpAfter);
                    ch.SendSysMessage(msg2.c_str());

                    CloseGossipMenuFor(player);
                    return true;
                }

                case ACTION_WITHDRAW:
                {
                    if (inputGold > bankGold)
                    {
                        ch.SendSysMessage("은행에 해당 금액만큼의 골드가 없습니다.");
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    uint64 playerCopper = player->GetMoney();

                    if (playerCopper >= MAX_SAFE_COPPER)
                    {
                        ch.SendSysMessage("이미 소지 골드가 너무 많습니다. 일부 사용 후 다시 시도해 주세요.");
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    uint64 maxCanTakeCopper = MAX_SAFE_COPPER - playerCopper;
                    uint64 maxCanTakeGold   = maxCanTakeCopper / GOLD;

                    if (maxCanTakeGold == 0)
                    {
                        ch.SendSysMessage("클라이언트 한도 때문에 더 이상 인출할 수 없습니다.");
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    uint64 realTakeGold = inputGold;
                    if (realTakeGold > maxCanTakeGold)
                        realTakeGold = maxCanTakeGold;
                    if (realTakeGold > bankGold)
                        realTakeGold = bankGold;

                    uint64 realTakeCopper = realTakeGold * GOLD;
                    if (realTakeCopper == 0)
                    {
                        ch.SendSysMessage("인출 가능한 금액이 없습니다.");
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    long long rpAfter = 0;
                    if (!RP::TrySpendOne(accountId, 1, rpAfter))
                    {
                        auto msg = Trinity::StringFormat("RP 부족: 현재 {} RP (필요: 1)", rpAfter);
                        ch.SendSysMessage(msg.c_str());
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    uint64 newBankGold = bankGold - realTakeGold;
                    SetBankGold(accountId, newBankGold);
                    player->ModifyMoney(static_cast<int64>(realTakeCopper));

                    auto msg1 = Trinity::StringFormat("골드 인출 완료: {} 골드 (남은 은행 골드: {} 골드)", realTakeGold, newBankGold);
                    ch.SendSysMessage(msg1.c_str());

                    if (realTakeGold < inputGold)
                        ch.SendSysMessage("클라이언트 한도 때문에 입력한 금액보다 적게 인출되었습니다.");

                    auto msg2 = Trinity::StringFormat("RP 1 소모됨. 잔여 RP: {}", rpAfter);
                    ch.SendSysMessage(msg2.c_str());

                    CloseGossipMenuFor(player);
                    return true;
                }

                // [ADDED] 골드 -> RP 교환
                case ACTION_EXCHANGE_GOLD_TO_RP:
                {
                    if (GOLD_PER_RP == 0)
                    {
                        ch.SendSysMessage("환율 설정 오류입니다. 운영자에게 문의하세요.");
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    // 받을 RP 계산 (버림)
                    uint64 rpGive64 = inputGold / GOLD_PER_RP;
                    if (rpGive64 == 0)
                    {
                        std::ostringstream ss;
                        ss << "입력한 골드가 부족합니다. 최소 " << GOLD_PER_RP << "골드 이상 입력하세요.";
                        ch.SendSysMessage(ss.str().c_str());
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    uint32 rpGive = (rpGive64 > 0xFFFFFFFFull) ? 0xFFFFFFFFu : static_cast<uint32>(rpGive64);

                    // 거래당 상한 적용
                    if (rpGive > MAX_RP_PER_TX)
                        rpGive = MAX_RP_PER_TX;

                    uint64 needGold = uint64(rpGive) * GOLD_PER_RP;
                    uint64 needCopper = needGold * GOLD;

                    uint64 playerCopper = player->GetMoney();
                    if (needCopper == 0 || needCopper > playerCopper)
                    {
                        ch.SendSysMessage("소지 골드가 부족합니다.");
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    // 실제 처리
                    player->ModifyMoney(-static_cast<int64>(needCopper));
                    RP::Add(accountId, rpGive);

                    long long afterRp = RP::Get(accountId);
					uint64 newp = afterRp + rpGive;

                    auto msg1 = Trinity::StringFormat("골드 → RP 교환 완료: {} 골드 → {} RP", needGold, rpGive);
                    ch.SendSysMessage(msg1.c_str());

                    auto msg2 = Trinity::StringFormat("현재 RP: {}", newp);
                    ch.SendSysMessage(msg2.c_str());

                    if (needGold < inputGold)
                        ch.SendSysMessage("입력한 골드 중 교환 가능한 만큼만 사용되었습니다. (남은 골드는 그대로 유지)");

                    CloseGossipMenuFor(player);
                    return true;
                }

                default:
                    break;
            }

            CloseGossipMenuFor(player);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_gold_bankAI(creature);
    }
};

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "Player.h"

class npc_sandbox_tiger_vehicle : public CreatureScript
{
public:
    npc_sandbox_tiger_vehicle() : CreatureScript("npc_sandbox_tiger_vehicle") { }

    struct npc_sandbox_tiger_vehicleAI : public ScriptedAI
    {
        npc_sandbox_tiger_vehicleAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override
        {
            // 움직임 금지 (고정형)
            me->SetReactState(REACT_PASSIVE);
            me->SetControlled(true, UNIT_STATE_ROOT);
        }

        // 🔥 핵심 함수
        void PassengerBoarded(Unit* passenger, int8 /*seatId*/, bool apply) override
		{
			if (!passenger || !passenger->IsPlayer())
				return;

			Player* player = passenger->ToPlayer();

			if (apply)
			{
				if (Vehicle* veh = me->GetVehicleKit())
				{
					// 💥 이게 핵심
					veh->InstallAllAccessories(true);

					player->SendOnCancelExpectedVehicleRideAura();
				}
			}
		}
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_sandbox_tiger_vehicleAI(creature);
    }
};


void AddSC_npcs_special_custom()
{
    new npc_gold_bank();
	new npc_sandbox_tiger_vehicle();
}
