#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Creature.h"
#include "Player.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"   // SendAreaTriggerMessage 용
#include "Group.h"
#include "Config.h"
#include "Map.h"
#include "GameTime.h"
#include "Log.h"
#include "World.h"
#include "Random.h"
#include "Channel.h"
#include "Item.h"
#include "StringFormat.h"    // Trinity::StringFormat
#include "Creature.h"
#include "CreatureAI.h"
#include "Chat.h"
#include "ObjectAccessor.h"
#include "MapManager.h"
#include "Mail.h"
#include "ObjectGuid.h"
#include "ScriptedCreature.h"
#include "SharedDefines.h"      // GOLD 등 통화 상수
#include "ItemTemplate.h"       // ItemTemplate 접근
#include "ObjectMgr.h"    // sObjectMgr
#include "QuestDef.h"     // Quest

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
#include "Object.h"      // PHASEMASK_NORMAL
#include "Position.h"

#include "ScriptMgr.h"
#include "DBCStructure.h"
#include "GameEventMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GameTime.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "WorldSession.h"


// ─────────────────────────────────────────────
// RP 유틸
// ─────────────────────────────────────────────
namespace RP
{
    // 행 보장 (없으면 reg_point=0으로 생성)
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

    // RP amount 소모 시도: 성공하면 true, outAfter에 "소모 후 RP" 반환
    static inline bool TrySpendOne(uint32 accountId, uint32 amount, long long& outAfter)
    {
        EnsureRow(accountId);

        // (1) 현재값
        long long before = Get(accountId);

        if (before < static_cast<long long>(amount))
        {
            outAfter = before;      // 부족하면 현재 RP 그대로 돌려줌
            return false;
        }

        // (2) 차감
        CharacterDatabase.PExecute(
            "UPDATE appdata.account_data SET reg_point = reg_point - {} "
            "WHERE id = {} AND reg_point >= {}",
            amount, accountId, amount);

        // (3) after = before - amount
        outAfter = before - static_cast<long long>(amount);
        return true;
    }
} // namespace RP

// ─────────────────────────────────────────────
// 포탈 오브젝트: go_om_point
//  - GameObjectAI의 GossipHello/GossipSelect 사용
//  - 사용 시 RP 100 필요, 부족하면 안내 후 취소
// ─────────────────────────────────────────────
class go_om_point : public GameObjectScript
{
public:
    go_om_point() : GameObjectScript("go_om_point") { }

    struct go_om_pointAI : public GameObjectAI
    {
        go_om_pointAI(GameObject* go) : GameObjectAI(go) { }

        bool OnGossipHello(Player* player) override
        {
            if (!player)
                return false;

            ClearGossipMenuFor(player);

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "오닉시아로 이동...",     GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "낙스라마스로 이동...",   GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "용의 안식처로 이동...", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 3);

            // 현재 RP 안내 + 100 필요 알림
            uint32 accId   = player->GetSession()->GetAccountId();
            long long curr = RP::Get(accId);

            {
                std::string msg = Trinity::StringFormat("이 포탈을 이용하려면 100 RP가 필요합니다. 현재 RP: {}", curr);
                ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
            }

            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, me->GetGUID());
            return true;
        }

        // sender / action 은 AddGossipItemFor 에서 준 값
		bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
			uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

			// 포탈 사용 전 RP 100 차감 시도
            uint32 accId   = player->GetSession()->GetAccountId();
            long long after = 0;
            constexpr uint32 kCost = 100;

            if (!RP::TrySpendOne(accId, kCost, after))
            {
                // RP 부족
                std::string msg = Trinity::StringFormat("RP 부족: 현재 {} RP (필요: {})", after, kCost);
                ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());

                CloseGossipMenuFor(player);
                return true;
            }

            // 차감 성공 안내
            {
                std::string msg = Trinity::StringFormat("RP {} 소모됨. 잔여 RP: {}", kCost, after);
                ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
            }

            // 액션에 따라 텔레포트
            switch (action)
            {
                case GOSSIP_ACTION_INFO_DEF + 1:
                {
                    // 오닉시아
                    player->TeleportTo(1, -4744.27f, -3659.65f, 49.627f, 1.328935f);
                    break;
                }
                case GOSSIP_ACTION_INFO_DEF + 2:
                {
                    // 낙스라마스
                    player->TeleportTo(571, 3669.118f, -1255.324f, 244.084f, 4.745410f);
                    break;
                }
                case GOSSIP_ACTION_INFO_DEF + 3:
                {
                    // 용의 안식처
                    player->TeleportTo(571, 3561.363f, 275.11f, -115.55f, 3.172924f);
                    break;
                }
                default:
                    break;
            }

            CloseGossipMenuFor(player);
            player->SaveToDB();
            return true;
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_om_pointAI(go);
    }
};










void AddSC_objects_special_custom()
{
	new go_om_point();
}