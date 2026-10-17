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
			AddGossipItemFor(player, GOSSIP_ICON_CHAT, "검은 바위산으로 이동...", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 4);

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
				case GOSSIP_ACTION_INFO_DEF + 4:
                {
                    // 검은 바위산
                    player->TeleportTo(0, -7805.8627f, -1134.1936f, 215.8477f, 0.146750f);
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


//가젯잔 이동포털
[[maybe_unused]] static constexpr uint32 GO_GADGETZAN_CRYSTAL_ENTRY = 244606;
static constexpr uint32 SPELL_GADGETZAN_TELEPORT = 23441;

static constexpr float GADGETZAN_CRYSTAL_RADIUS = 1.8f;
static constexpr float GADGETZAN_CRYSTAL_Z_RADIUS = 3.0f;
static constexpr uint32 GADGETZAN_CRYSTAL_CHECK_MS = 500;
static constexpr uint32 GADGETZAN_CRYSTAL_COOLDOWN_SEC = 5;

class go_gadgetzan_crystal : public GameObjectScript
{
public:
    go_gadgetzan_crystal() : GameObjectScript("go_gadgetzan_crystal") { }

    struct go_gadgetzan_crystalAI : public GameObjectAI
    {
        go_gadgetzan_crystalAI(GameObject* go) : GameObjectAI(go) { }

        void UpdateAI(uint32 diff) override
        {
            if (m_checkTimer > diff)
            {
                m_checkTimer -= diff;
                return;
            }

            m_checkTimer = GADGETZAN_CRYSTAL_CHECK_MS;

            if (!me || !me->IsInWorld())
                return;

            Map* map = me->GetMap();
            if (!map)
                return;

            Map::PlayerList const& players = map->GetPlayers();
            if (players.isEmpty())
                return;

            time_t now = GameTime::GetGameTime();

            for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
            {
                Player* player = itr->GetSource();
                if (!player)
                    continue;

                if (!player->IsInWorld())
                    continue;

                if (player->IsBeingTeleported())
                    continue;

                if (!player->IsWithinDistInMap(me, GADGETZAN_CRYSTAL_RADIUS))
                    continue;

                if (std::fabs(player->GetPositionZ() - me->GetPositionZ()) > GADGETZAN_CRYSTAL_Z_RADIUS)
                    continue;

                uint64 guid = player->GetGUID().GetCounter();

                auto cdItr = m_cooldown.find(guid);
                if (cdItr != m_cooldown.end() && cdItr->second > now)
                    continue;

                m_cooldown[guid] = now + GADGETZAN_CRYSTAL_COOLDOWN_SEC;

                player->CastSpell(player, SPELL_GADGETZAN_TELEPORT, true);

                TC_LOG_INFO("custom", "[GadgetzanCrystal] 가젯잔 수정 발동: player={}, spell={}",
                            player->GetName(), SPELL_GADGETZAN_TELEPORT);
            }
        }

        bool OnGossipHello(Player* player) override
        {
            if (!player)
                return true;

            player->CastSpell(player, SPELL_GADGETZAN_TELEPORT, true);
            return true;
        }

    private:
        uint32 m_checkTimer = 0;
        std::unordered_map<uint64, time_t> m_cooldown;
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_gadgetzan_crystalAI(go);
    }
};

//실리시스트
enum SilithystMoundData
{
    SPELL_SILITHYST_GEYSER = 29519
};

class go_silithyst_mound_181597 : public GameObjectScript
{
public:
    go_silithyst_mound_181597() : GameObjectScript("go_silithyst_mound_181597") { }

    struct go_silithyst_mound_181597AI : public GameObjectAI
    {
        go_silithyst_mound_181597AI(GameObject* go) : GameObjectAI(go) { }

        bool OnGossipHello(Player* player) override
        {
            if (!player)
                return true;

            player->CastSpell(player, SPELL_SILITHYST_GEYSER, true);

            me->SetLootState(GO_JUST_DEACTIVATED);
            me->SetRespawnTime(0);
            me->Delete();

            return true;
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_silithyst_mound_181597AI(go);
    }
};

//커스텀 이동 포털
#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Player.h"
#include "GameTime.h"
#include "Map.h"

#include <cmath>
#include <unordered_map>

static constexpr float ENTRY_TELEPORT_RADIUS = 1.8f;
static constexpr float ENTRY_TELEPORT_Z_RADIUS = 3.0f;
static constexpr uint32 ENTRY_TELEPORT_CHECK_MS = 500;
static constexpr uint32 ENTRY_TELEPORT_COOLDOWN_SEC = 5;

struct EntryTeleportPosition
{
    uint32 MapId;
    float X;
    float Y;
    float Z;
    float O;
};

struct EntryFactionTeleportDestination
{
    uint32 GameObjectEntry;
    EntryTeleportPosition Horde;
    EntryTeleportPosition Alliance;
};

// 같은 Entry를 사용하고, 진영에 따라 좌표만 다르게 처리
static EntryFactionTeleportDestination const kEntryTeleportDestinations[] =
{
    {
        244608,

        // 호드 → 실리더스 호드 좌표
        { 1, -7513.760254f, 764.175842f, -14.156381f, 3.683960f },

        // 얼라이언스 → 실리더스 얼라이언스 좌표
        { 1, -721.356934f, 1411.592529f, 4.941202f, 6.026802f }
    },

    {
        244609,

        // 호드 → 지옥불 반도 호드 좌표
        { 530, 2.125769f, 2646.063232f, 73.112503f, 0.811844f },

        // 얼라이언스 → 지옥불 반도 얼라이언스 좌표
        { 530, -634.270350f, 2563.131836f, 78.342232f, 2.148068f }
    },

    {
        244610,

        // 호드 → 장가르 습지대 호드 좌표
        { 530, 294.596985f, 7850.740234f, 2.438700f, 1.343900f },

        // 얼라이언스 → 장가르 습지대 얼라이언스 좌표
        { 530, 377.113770f, 6240.718910f, 22.510540f, 0.979136f }
    },

    {
        244611,

        // 호드 → 나그란드 호드 좌표
        { 530, -1208.005493f, 7408.603027f, 28.196314f, 4.355800f },

        // 얼라이언스 → 나그란드 얼라이언스 좌표
        { 530, -508.327393f, 7308.223633f, 1.653407f, 3.519342f }
    },
};

static EntryFactionTeleportDestination const* GetTeleportDestinationByEntry(uint32 entry)
{
    for (EntryFactionTeleportDestination const& destination : kEntryTeleportDestinations)
    {
        if (destination.GameObjectEntry == entry)
            return &destination;
    }

    return nullptr;
}

static EntryTeleportPosition const* GetTeleportPositionForPlayer(GameObject* go, Player* player)
{
    if (!go || !player)
        return nullptr;

    EntryFactionTeleportDestination const* destination = GetTeleportDestinationByEntry(go->GetEntry());
    if (!destination)
        return nullptr;

    if (player->GetTeamId() == TEAM_HORDE)
        return &destination->Horde;

    return &destination->Alliance;
}

class go_entry_based_auto_teleporter : public GameObjectScript
{
public:
    go_entry_based_auto_teleporter() : GameObjectScript("go_entry_based_auto_teleporter") { }

    struct go_entry_based_auto_teleporterAI : public GameObjectAI
    {
        go_entry_based_auto_teleporterAI(GameObject* go) : GameObjectAI(go) { }

        void UpdateAI(uint32 diff) override
        {
            if (m_checkTimer > diff)
            {
                m_checkTimer -= diff;
                return;
            }

            m_checkTimer = ENTRY_TELEPORT_CHECK_MS;

            if (!me || !me->IsInWorld())
                return;

            if (!GetTeleportDestinationByEntry(me->GetEntry()))
                return;

            Map* map = me->GetMap();
            if (!map)
                return;

            Map::PlayerList const& players = map->GetPlayers();
            if (players.isEmpty())
                return;

            time_t now = GameTime::GetGameTime();

            for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
            {
                Player* player = itr->GetSource();
                if (!player)
                    continue;

                if (!player->IsInWorld())
                    continue;

                if (player->IsBeingTeleported())
                    continue;

                if (!player->IsAlive())
                    continue;

                if (!player->IsWithinDistInMap(me, ENTRY_TELEPORT_RADIUS))
                    continue;

                if (std::fabs(player->GetPositionZ() - me->GetPositionZ()) > ENTRY_TELEPORT_Z_RADIUS)
                    continue;

                EntryTeleportPosition const* position = GetTeleportPositionForPlayer(me, player);
                if (!position)
                    continue;

                uint64 guid = player->GetGUID().GetCounter();

                auto cdItr = m_cooldown.find(guid);
                if (cdItr != m_cooldown.end() && cdItr->second > now)
                    continue;

                m_cooldown[guid] = now + ENTRY_TELEPORT_COOLDOWN_SEC;

                player->TeleportTo(
                    position->MapId,
                    position->X,
                    position->Y,
                    position->Z,
                    position->O
                );

                TC_LOG_INFO("custom",
                    "[EntryAutoTeleporter] 진영 분기 텔레포트: player={}, team={}, goEntry={}, map={}, x={}, y={}, z={}, o={}",
                    player->GetName(),
                    player->GetTeamId(),
                    me->GetEntry(),
                    position->MapId,
                    position->X,
                    position->Y,
                    position->Z,
                    position->O
                );
            }
        }

        bool OnGossipHello(Player* player) override
        {
            if (!player)
                return true;

            EntryTeleportPosition const* position = GetTeleportPositionForPlayer(me, player);
            if (!position)
                return true;

            uint64 guid = player->GetGUID().GetCounter();
            time_t now = GameTime::GetGameTime();

            auto cdItr = m_cooldown.find(guid);
            if (cdItr != m_cooldown.end() && cdItr->second > now)
                return true;

            m_cooldown[guid] = now + ENTRY_TELEPORT_COOLDOWN_SEC;

            player->TeleportTo(
                position->MapId,
                position->X,
                position->Y,
                position->Z,
                position->O
            );

            TC_LOG_INFO("custom",
                "[EntryAutoTeleporter] 클릭 진영 분기 텔레포트: player={}, team={}, goEntry={}, map={}, x={}, y={}, z={}, o={}",
                player->GetName(),
                player->GetTeamId(),
                me->GetEntry(),
                position->MapId,
                position->X,
                position->Y,
                position->Z,
                position->O
            );

            return true;
        }

    private:
        uint32 m_checkTimer = 0;
        std::unordered_map<uint64, time_t> m_cooldown;
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_entry_based_auto_teleporterAI(go);
    }
};

/*
 * Orb of Command corpse resurrection fix
 *
 * - 살아있는 플레이어의 기존 Orb 작동은 건드리지 않는다.
 * - 죽은 상태/유령 상태 플레이어가 Orb 근처에 왔을 때만 검사한다.
 * - Blackhand's Command 입장퀘 완료자만 부활시킨다.
 * - 자동 텔레포트는 하지 않는다.
 *
 * Custom Update - 2026-07-15
 * - 살아있는 입장퀘 완료자의 보주 클릭 이동 처리 추가
 */

#include "GameObject.h"
#include "GameObjectAI.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"

namespace
{
    constexpr uint32 GO_ORB_OF_COMMAND = 179879;
    constexpr uint32 QUEST_BLACKHANDS_COMMAND = 7761;

    constexpr float ORB_CORPSE_RESURRECT_DISTANCE = 7.0f;
    constexpr uint32 ORB_CORPSE_CHECK_INTERVAL = 1000;
}

class go_orb_of_command_corpse_resurrect : public GameObjectScript
{
public:
    go_orb_of_command_corpse_resurrect() : GameObjectScript("go_orb_of_command_corpse_resurrect") { }

    struct go_orb_of_command_corpse_resurrectAI : public GameObjectAI
    {
        explicit go_orb_of_command_corpse_resurrectAI(GameObject* go) : GameObjectAI(go) { }

        uint32 _checkTimer = ORB_CORPSE_CHECK_INTERVAL;

        bool OnGossipHello(Player* player) override
        {
            if (!player || !player->IsAlive())
                return false;

            if (!player->GetQuestRewardStatus(QUEST_BLACKHANDS_COMMAND))
                return false;

            player->CastSpell(player, 23460, true);
            return true;
        }

        void UpdateAI(uint32 diff) override
        {
            if (_checkTimer <= diff)
                _checkTimer = ORB_CORPSE_CHECK_INTERVAL;
            else
            {
                _checkTimer -= diff;
                return;
            }

            if (!me || me->GetEntry() != GO_ORB_OF_COMMAND)
                return;

            Map* map = me->GetMap();
            if (!map)
                return;

            Map::PlayerList const& players = map->GetPlayers();
            if (players.isEmpty())
                return;

            for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
            {
                Player* player = itr->GetSource();
                if (!player)
                    continue;

                // 살아있는 유저는 보주 클릭으로 이동한다.
                if (player->IsAlive())
                    continue;

                if (player->IsBeingTeleported())
                    continue;

                if (!player->IsInWorld())
                    continue;

                if (!player->IsWithinDistInMap(me, ORB_CORPSE_RESURRECT_DISTANCE))
                    continue;

                // 입장퀘 완료자만 부활
                if (!player->GetQuestRewardStatus(QUEST_BLACKHANDS_COMMAND))
                    continue;

                // 부활만 처리한다. 텔레포트는 하지 않는다.
                player->ResurrectPlayer(1.0f, false);
                player->SpawnCorpseBones();
                player->CastSpell(player, 23460, true);
            }
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_orb_of_command_corpse_resurrectAI(go);
    }
};












void AddSC_objects_special_custom()
{
	new go_om_point();
	new go_gadgetzan_crystal();
	new go_silithyst_mound_181597();
	new go_entry_based_auto_teleporter();
	new go_orb_of_command_corpse_resurrect();
}