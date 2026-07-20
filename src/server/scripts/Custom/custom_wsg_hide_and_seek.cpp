/*
 * Custom Update - 2026-07-10
 *
 * 전쟁노래 은폐전 1차 구현
 *
 * - 기존 전장 신청/전장 큐는 건드리지 않는다.
 * - 새 NPC로 전쟁노래 은폐전 전용 대기열을 만든다.
 * - 진영 상관없이 신청자를 모은다.
 * - 대기열이 차면 레드팀/블루팀을 자동 분배한다.
 * - 레드팀은 전쟁노래협곡 얼라이언스 시작지점으로 이동한다.
 * - 블루팀은 전쟁노래협곡 호드 시작지점으로 이동한다.
 * - 은폐전 참가 중에는 일반/귓말/파티/길드/채널 대화를 차단한다.
 * - 현재 단계에서는 은폐/수색/보상은 구현하지 않는다.
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Chat.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"
#include "StringFormat.h"

#include <algorithm>
#include <random>
#include <string>
#include <vector>

namespace
{
    enum WsgHideData
    {
        NPC_WSG_HIDE_MASTER = 900800,

        MAP_WARSONG_GULCH = 489,

        WSG_HIDE_TEAM_RED = 1,
        WSG_HIDE_TEAM_BLUE = 2,

        // 1차 테스트용: 2명 신청 시 바로 시작.
        // 운영 시 4 또는 6으로 올리면 된다.
        WSG_HIDE_START_COUNT = 2,
        WSG_HIDE_MAX_PLAYERS = 10,

        // 1차 테스트용: 10분 후 자동 종료 및 원위치 복귀.
        WSG_HIDE_TEST_DURATION = 10 * 60 * 1000
    };

    enum WsgHideGossipAction
    {
        WSG_HIDE_GOSSIP_JOIN = 1,
        WSG_HIDE_GOSSIP_CANCEL = 2,
        WSG_HIDE_GOSSIP_COUNT = 3
    };

    struct WsgHidePlayerState
    {
        ObjectGuid Guid;
        uint32 Team = 0;

        uint32 OldMapId = 0;
        float OldX = 0.0f;
        float OldY = 0.0f;
        float OldZ = 0.0f;
        float OldO = 0.0f;
    };

    static std::vector<ObjectGuid> sWsgHideQueue;
    static std::vector<WsgHidePlayerState> sWsgHidePlayers;
    static bool sWsgHideActive = false;
    static uint32 sWsgHideTimer = 0;

    static void SendMsg(Player* player, char const* text)
    {
        if (!player || !player->GetSession())
            return;

        ChatHandler(player->GetSession()).SendSysMessage(text);
    }

    static bool IsInQueue(Player* player)
    {
        if (!player)
            return false;

        ObjectGuid guid = player->GetGUID();

        return std::find(sWsgHideQueue.begin(), sWsgHideQueue.end(), guid) != sWsgHideQueue.end();
    }

    static bool IsInHideGame(Player* player)
    {
        if (!player)
            return false;

        ObjectGuid guid = player->GetGUID();

        return std::find_if(
            sWsgHidePlayers.begin(),
            sWsgHidePlayers.end(),
            [guid](WsgHidePlayerState const& state)
            {
                return state.Guid == guid;
            }) != sWsgHidePlayers.end();
    }

    static void RemoveFromQueue(Player* player)
    {
        if (!player)
            return;

        ObjectGuid guid = player->GetGUID();

        sWsgHideQueue.erase(
            std::remove(sWsgHideQueue.begin(), sWsgHideQueue.end(), guid),
            sWsgHideQueue.end());
    }

    static void ClearOfflinePlayersFromQueue()
    {
        sWsgHideQueue.erase(
            std::remove_if(
                sWsgHideQueue.begin(),
                sWsgHideQueue.end(),
                [](ObjectGuid const& guid)
                {
                    return ObjectAccessor::FindConnectedPlayer(guid) == nullptr;
                }),
            sWsgHideQueue.end());
    }

    static void BroadcastToHidePlayers(char const* text)
    {
        for (WsgHidePlayerState const& state : sWsgHidePlayers)
        {
            if (Player* player = ObjectAccessor::FindConnectedPlayer(state.Guid))
                SendMsg(player, text);
        }
    }

    static void RestorePlayer(WsgHidePlayerState const& state)
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(state.Guid);
        if (!player)
            return;

        player->TeleportTo(state.OldMapId, state.OldX, state.OldY, state.OldZ, state.OldO);
    }

    static void EndWsgHideGame()
    {
        std::vector<WsgHidePlayerState> restoreList = sWsgHidePlayers;

        sWsgHidePlayers.clear();
        sWsgHideActive = false;
        sWsgHideTimer = 0;

        for (WsgHidePlayerState const& state : restoreList)
            RestorePlayer(state);
    }

    static bool TeleportToTeamStart(Player* player, uint32 team)
    {
        if (!player)
            return false;

        if (team == WSG_HIDE_TEAM_BLUE)
        {
            // 블루팀: 호드 시작지점
            return player->TeleportTo(MAP_WARSONG_GULCH, 916.797f, 1434.557f, 345.413f, 0.017f);
        }

        // 레드팀: 얼라이언스 시작지점
        return player->TeleportTo(MAP_WARSONG_GULCH, 1540.323f, 1481.556f, 352.640f, 3.124f);
    }

    static void StartWsgHideGame()
    {
        if (sWsgHideActive)
            return;

        ClearOfflinePlayersFromQueue();

        if (sWsgHideQueue.size() < WSG_HIDE_START_COUNT)
            return;

        std::vector<ObjectGuid> players;

        uint32 takeCount = std::min(uint32(sWsgHideQueue.size()), uint32(WSG_HIDE_MAX_PLAYERS));

        for (uint32 i = 0; i < takeCount; ++i)
            players.push_back(sWsgHideQueue[i]);

        sWsgHideQueue.erase(sWsgHideQueue.begin(), sWsgHideQueue.begin() + takeCount);

        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(players.begin(), players.end(), g);

        sWsgHidePlayers.clear();

        // 홀수 인원일 경우 블루팀이 1명 더 많게 분배한다.
        uint32 redCount = uint32(players.size() / 2);

        if (redCount == 0)
            redCount = 1;

        for (uint32 i = 0; i < players.size(); ++i)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(players[i]);
            if (!player)
                continue;

            uint32 team = i < redCount ? WSG_HIDE_TEAM_RED : WSG_HIDE_TEAM_BLUE;

            WsgHidePlayerState state;
            state.Guid = player->GetGUID();
            state.Team = team;
            state.OldMapId = player->GetMapId();
            state.OldX = player->GetPositionX();
            state.OldY = player->GetPositionY();
            state.OldZ = player->GetPositionZ();
            state.OldO = player->GetOrientation();

            player->CombatStop();
            player->Dismount();

            if (!TeleportToTeamStart(player, team))
            {
                SendMsg(player, "전쟁노래 은폐전 입장에 실패했습니다.");
                continue;
            }

            sWsgHidePlayers.push_back(state);

            if (team == WSG_HIDE_TEAM_BLUE)
                SendMsg(player, "당신은 블루팀입니다. 호드 시작지점으로 이동했습니다.");
            else
                SendMsg(player, "당신은 레드팀입니다. 얼라이언스 시작지점으로 이동했습니다.");
        }

        if (sWsgHidePlayers.empty())
        {
            sWsgHideActive = false;
            sWsgHideTimer = 0;
            return;
        }

        sWsgHideActive = true;
        sWsgHideTimer = WSG_HIDE_TEST_DURATION;

        BroadcastToHidePlayers("전쟁노래 은폐전 1차 테스트가 시작되었습니다.");
        BroadcastToHidePlayers("진행 중에는 대화가 차단됩니다.");
    }

    static bool CanJoinWsgHide(Player* player)
    {
        if (!player)
            return false;

        if (sWsgHideActive)
        {
            SendMsg(player, "이미 전쟁노래 은폐전이 진행 중입니다.");
            return false;
        }

        if (IsInQueue(player))
        {
            SendMsg(player, "이미 전쟁노래 은폐전 대기열에 등록되어 있습니다.");
            return false;
        }

        if (IsInHideGame(player))
        {
            SendMsg(player, "이미 전쟁노래 은폐전에 참가 중입니다.");
            return false;
        }

        if (player->InBattleground())
        {
            SendMsg(player, "전장 안에서는 전쟁노래 은폐전에 신청할 수 없습니다.");
            return false;
        }

        if (player->InBattlegroundQueue())
        {
            SendMsg(player, "기존 전장 대기열에 등록된 상태에서는 신청할 수 없습니다.");
            return false;
        }

        if (player->IsInCombat())
        {
            SendMsg(player, "전투 중에는 신청할 수 없습니다.");
            return false;
        }

        if (!player->IsAlive())
        {
            SendMsg(player, "죽은 상태에서는 신청할 수 없습니다.");
            return false;
        }

        return true;
    }

    static void JoinWsgHideQueue(Player* player)
    {
        if (!CanJoinWsgHide(player))
            return;

        sWsgHideQueue.push_back(player->GetGUID());

        std::string msg = Trinity::StringFormat(
            "전쟁노래 은폐전 대기열에 등록되었습니다. 현재 인원: {}/{}",
            uint32(sWsgHideQueue.size()),
            uint32(WSG_HIDE_START_COUNT));

        ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());

        StartWsgHideGame();
    }

    static void CancelWsgHideQueue(Player* player)
    {
        if (!player)
            return;

        if (!IsInQueue(player))
        {
            SendMsg(player, "전쟁노래 은폐전 대기열에 등록되어 있지 않습니다.");
            return;
        }

        RemoveFromQueue(player);
        SendMsg(player, "전쟁노래 은폐전 대기열에서 취소되었습니다.");
    }

    static void BlockChatIfNeeded(Player* player, std::string& msg)
    {
        if (!IsInHideGame(player))
            return;

        msg.clear();

        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage("전쟁노래 은폐전 진행 중에는 대화할 수 없습니다.");
    }
}

class npc_wsg_hide_master : public CreatureScript
{
public:
    npc_wsg_hide_master() : CreatureScript("npc_wsg_hide_master") { }

    struct npc_wsg_hide_masterAI : public ScriptedAI
    {
        npc_wsg_hide_masterAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "전쟁노래 은폐전에 참가한다.", GOSSIP_SENDER_MAIN, WSG_HIDE_GOSSIP_JOIN);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "전쟁노래 은폐전 대기열을 취소한다.", GOSSIP_SENDER_MAIN, WSG_HIDE_GOSSIP_CANCEL);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "현재 대기 인원을 확인한다.", GOSSIP_SENDER_MAIN, WSG_HIDE_GOSSIP_COUNT);
            SendGossipMenuFor(player, 1, me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

            ClearGossipMenuFor(player);

            switch (action)
            {
                case WSG_HIDE_GOSSIP_JOIN:
                    JoinWsgHideQueue(player);
                    break;

                case WSG_HIDE_GOSSIP_CANCEL:
                    CancelWsgHideQueue(player);
                    break;

                case WSG_HIDE_GOSSIP_COUNT:
                {
                    ClearOfflinePlayersFromQueue();

                    std::string msg = Trinity::StringFormat(
                        "현재 전쟁노래 은폐전 대기 인원: {}/{}",
                        uint32(sWsgHideQueue.size()),
                        uint32(WSG_HIDE_START_COUNT));

                    ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
                    break;
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
        return new npc_wsg_hide_masterAI(creature);
    }
};

class player_wsg_hide_chat_block : public PlayerScript
{
public:
    player_wsg_hide_chat_block() : PlayerScript("player_wsg_hide_chat_block") { }

    void OnChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg) override
    {
        BlockChatIfNeeded(player, msg);
    }

    void OnChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg, Player* /*receiver*/) override
    {
        BlockChatIfNeeded(player, msg);
    }

    void OnChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg, Group* /*group*/) override
    {
        BlockChatIfNeeded(player, msg);
    }

    void OnChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg, Guild* /*guild*/) override
    {
        BlockChatIfNeeded(player, msg);
    }

    void OnChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg, Channel* /*channel*/) override
    {
        BlockChatIfNeeded(player, msg);
    }

    void OnLogout(Player* player) override
    {
        if (!player)
            return;

        RemoveFromQueue(player);

        ObjectGuid guid = player->GetGUID();

        sWsgHidePlayers.erase(
            std::remove_if(
                sWsgHidePlayers.begin(),
                sWsgHidePlayers.end(),
                [guid](WsgHidePlayerState const& state)
                {
                    return state.Guid == guid;
                }),
            sWsgHidePlayers.end());

        if (sWsgHidePlayers.empty())
        {
            sWsgHideActive = false;
            sWsgHideTimer = 0;
        }
    }
};

class world_wsg_hide_update : public WorldScript
{
public:
    world_wsg_hide_update() : WorldScript("world_wsg_hide_update") { }

    void OnUpdate(uint32 diff) override
    {
        if (!sWsgHideActive)
            return;

        if (sWsgHideTimer <= diff)
        {
            BroadcastToHidePlayers("전쟁노래 은폐전 1차 테스트가 종료되었습니다. 원래 위치로 돌아갑니다.");
            EndWsgHideGame();
            return;
        }

        sWsgHideTimer -= diff;
    }
};

void AddSC_custom_wsg_hide_and_seek()
{
    new npc_wsg_hide_master();
    new player_wsg_hide_chat_block();
    new world_wsg_hide_update();
}
