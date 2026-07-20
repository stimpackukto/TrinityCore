#include "ScriptMgr.h"
#include "ArenaTeamMgr.h"
#include "Common.h"
#include "DisableMgr.h"
#include "BattlegroundMgr.h"
#include "Battleground.h"
#include "BattlegroundPackets.h"
#include "ArenaTeam.h"
#include "Language.h"
#include "npc_arena1v1.h"
#include "World.h"
#include "Player.h"
#include "Chat.h"
#include "WorldSession.h"
#include "Log.h"
#include "ScriptedGossip.h"
#include "ScriptedCreature.h"
#include "CreatureAI.h"
#include "Config.h"
#include "CustomRP.h"

#include <sstream>
#include <string>

namespace
{
    static BattlegroundQueueTypeId GetArena1v1QueueTypeId(Player* player)
    {
        if (!player)
            return BATTLEGROUND_QUEUE_NONE;

        Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_AA);
        if (!bg)
            return BATTLEGROUND_QUEUE_NONE;

        PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), player->GetLevel());
        if (!bracketEntry)
            return BATTLEGROUND_QUEUE_NONE;

        return BattlegroundMgr::BGQueueTypeId(bg->GetTypeID(), bracketEntry->GetBracketId(), ARENA_TYPE_5v5);
    }

    static void SendArena1v1Message(Player* player, std::string const& text)
    {
        if (!player || !player->GetSession())
            return;

        std::string message = "|cffFF4500[1v1 투기장]:|r " + text;
        ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
    }
}

class npc_1v1arena : public CreatureScript
{
public:
    npc_1v1arena() : CreatureScript("npc_1v1arena") { }

    struct npc_1v1arenaAI : public ScriptedAI
    {
        npc_1v1arenaAI(Creature* creature) : ScriptedAI(creature) { }

        bool JoinQueueArena(Player* player, Creature* creature, bool isRated)
        {
            if (!player || !creature)
                return false;

            if (config_minLevel > player->GetLevel())
                return false;

            uint8 arenaslot = ArenaTeam::GetSlotByType(ARENA_TEAM_5v5);
            uint8 arenatype = ARENA_TYPE_5v5;

            uint32 arenaRating = 0;
            uint32 matchmakerRating = 0;

            if (player->InBattleground())
                return false;

            Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_AA);
            if (!bg)
            {
                TC_LOG_ERROR("bg.arena", "1v1 Arena: BATTLEGROUND_AA template not found");
                return false;
            }

            if (DisableMgr::IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, BATTLEGROUND_AA, nullptr))
            {
                ChatHandler(player->GetSession()).PSendSysMessage(LANG_ARENA_DISABLED);
                return false;
            }

            PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), player->GetLevel());
            if (!bracketEntry)
                return false;

            BattlegroundTypeId bgTypeId = bg->GetTypeID();
            BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(bgTypeId, bracketEntry->GetBracketId(), arenatype);

            if (player->GetBattlegroundQueueIndex(bgQueueTypeId) < PLAYER_MAX_BATTLEGROUND_QUEUES)
                return false;

            if (!player->HasFreeBattlegroundQueueId())
                return false;

            uint32 ateamId = 0;

            if (isRated)
            {
                ateamId = player->GetArenaTeamId(arenaslot);
                ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(ateamId);

                if (!at)
                {
                    player->GetSession()->SendNotInArenaTeamPacket(arenatype);
                    return false;
                }

                arenaRating = at->GetRating();
                matchmakerRating = arenaRating;

                if (arenaRating <= 0)
                    arenaRating = 1;
            }

            BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
            bg->SetRated(isRated);

            GroupQueueInfo* ginfo = bgQueue.AddGroup(
                player,
                nullptr,
                bracketEntry,
                isRated,
                false,
                arenaRating,
                matchmakerRating,
                ateamId
            );

            if (!ginfo)
                return false;

            uint32 avgTime = bgQueue.GetAverageQueueWaitTime(ginfo);
            uint32 queueSlot = player->AddBattlegroundQueueId(bgQueueTypeId);

            WorldPackets::Battleground::BattlefieldStatusQueued battlefieldStatus;
            sBattlegroundMgr->BuildBattlegroundStatusQueued(
                &battlefieldStatus,
                bg,
                queueSlot,
                ginfo->JoinTime,
                bgQueueTypeId,
                avgTime
            );

            player->SendDirectMessage(battlefieldStatus.Write());

            sBattlegroundMgr->ScheduleQueueUpdate(matchmakerRating, bgQueueTypeId);

            return true;
        }

        bool CreateArenateam(Player* player, Creature* creature)
        {
            if (!player || !creature)
                return false;

            uint8 slot = ArenaTeam::GetSlotByType(ARENA_TEAM_5v5);
            if (slot >= MAX_ARENA_SLOT)
                return false;

            if (player->GetArenaTeamId(slot))
            {
                player->GetSession()->SendArenaTeamCommandResult(
                    ERR_ARENA_TEAM_CREATE_S,
                    player->GetName(),
                    "",
                    ERR_ALREADY_IN_ARENA_TEAM
                );

                return false;
            }

            int32 index = 1;
            std::stringstream teamName;
            teamName << player->GetName();

            do
            {
                if (sArenaTeamMgr->GetArenaTeamByName(teamName.str()) != nullptr)
                {
                    teamName.str(std::string());
                    teamName.clear();
                    teamName << player->GetName() << index;
                    ++index;
                }
                else
                    break;
            } while (index < 100);

            ArenaTeam* arenaTeam = new ArenaTeam();

            if (!arenaTeam->Create(
                player->GetGUID(),
                ARENA_TEAM_5v5,
                teamName.str(),
                4283124816u,
                45,
                4294242303u,
                5,
                4294705149u
            ))
            {
                delete arenaTeam;
                return false;
            }

            sArenaTeamMgr->AddArenaTeam(arenaTeam);
            arenaTeam->AddMember(player->GetGUID());

            SendArena1v1Message(player, "1v1 투기장 팀이 생성되었습니다.");

            return true;
        }

        bool OnGossipHello(Player* player) override
        {
            if (!player || !me)
                return true;

            if (config_Enable == false)
            {
                SendArena1v1Message(player, "현재 1v1 투기장은 비활성화 상태입니다.");
                return true;
            }

            BattlegroundQueueTypeId bgQueueTypeId = GetArena1v1QueueTypeId(player);
            bool inQueue = player->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeId);
            uint8 slot = ArenaTeam::GetSlotByType(ARENA_TEAM_5v5);

            if (inQueue)
            {
                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    "1v1 투기장 대기열 나가기",
                    GOSSIP_SENDER_MAIN,
                    3,
                    "1v1 투기장 대기열에서 나가시겠습니까?",
                    0,
                    false
                );
            }
            else
            {
                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    "1v1 투기장 신청 - 연습전",
                    GOSSIP_SENDER_MAIN,
                    20
                );
            }

            if (player->GetArenaTeamId(slot) == 0)
            {
                AddGossipItemFor(
					player,
					GOSSIP_ICON_CHAT,
					"1v1 투기장 팀 생성 (RP 1,000)",
					GOSSIP_SENDER_MAIN,
					1,
					"1v1 투기장 팀을 생성하시겠습니까?\n\n필요 RP: 1,000",
					0,
					false
				);
            }
            else
            {
                if (!inQueue)
                {
                    AddGossipItemFor(
                        player,
                        GOSSIP_ICON_CHAT,
                        "1v1 투기장 신청 - 평점전",
                        GOSSIP_SENDER_MAIN,
                        2
                    );

                    AddGossipItemFor(
                        player,
                        GOSSIP_ICON_CHAT,
                        "1v1 투기장 팀 해산",
                        GOSSIP_SENDER_MAIN,
                        5,
                        "1v1 투기장 팀을 해산하시겠습니까?",
                        0,
                        false
                    );
                }

                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    "내 1v1 투기장 전적 확인",
                    GOSSIP_SENDER_MAIN,
                    4
                );
            }

            SendGossipMenuFor(player, 68, me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            if (!player || !me)
                return true;

            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

            ClearGossipMenuFor(player);

            switch (action)
            {
                case 1:
                {
                    if (config_minLevel > player->GetLevel())
                    {
                        std::ostringstream msg;
                        msg << "1v1 투기장 팀 생성에는 "
                            << uint32(config_minLevel)
                            << "레벨이 필요합니다.";

                        SendArena1v1Message(player, msg.str());
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    uint32 accountId = player->GetSession()->GetAccountId();
					uint32 needRP = 1000;

					if (!HasEnoughRP(accountId, needRP))
					{
						std::ostringstream msg;
						msg << "1v1 투기장 팀 생성에는 RP "
							<< needRP
							<< "이 필요합니다. 현재 보유 RP: "
							<< GetRP(accountId);

						SendArena1v1Message(player, msg.str());
						CloseGossipMenuFor(player);
						return true;
					}

					if (!SubtractRP(accountId, needRP))
					{
						SendArena1v1Message(player, "RP 차감에 실패했습니다. 잠시 후 다시 시도해주세요.");
						CloseGossipMenuFor(player);
						return true;
					}

					if (!CreateArenateam(player, me))
					{
						AddRP(accountId, needRP);
						SendArena1v1Message(player, "1v1 투기장 팀 생성에 실패하여 RP가 반환되었습니다.");
						CloseGossipMenuFor(player);
						return true;
					}

					CloseGossipMenuFor(player);
					return true;
                }

                case 2:
                {
                    if (Arena1v1CheckTalents(player) && !JoinQueueArena(player, me, true))
                        SendArena1v1Message(player, "1v1 평점전 대기열 신청에 실패했습니다.");

                    CloseGossipMenuFor(player);
                    return true;
                }

                case 20:
                {
                    if (Arena1v1CheckTalents(player) && !JoinQueueArena(player, me, false))
                        SendArena1v1Message(player, "1v1 연습전 대기열 신청에 실패했습니다.");

                    CloseGossipMenuFor(player);
                    return true;
                }

                case 3:
                {
                    BattlegroundQueueTypeId bgQueueTypeId = GetArena1v1QueueTypeId(player);
                    uint32 queueSlot = player->GetBattlegroundQueueIndex(bgQueueTypeId);

                    if (queueSlot < PLAYER_MAX_BATTLEGROUND_QUEUES)
                    {
                        sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId).RemovePlayer(player->GetGUID(), true);
                        player->RemoveBattlegroundQueueId(bgQueueTypeId);

                        WorldPackets::Battleground::BattlefieldStatusNone battlefieldStatus;
                        sBattlegroundMgr->BuildBattlegroundStatusNone(&battlefieldStatus, queueSlot);
                        player->SendDirectMessage(battlefieldStatus.Write());

                        SendArena1v1Message(player, "1v1 투기장 대기열에서 나갔습니다.");
                    }

                    CloseGossipMenuFor(player);
                    return true;
                }

                case 4:
                {
                    ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(
                        player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_5v5))
                    );

                    if (at)
                    {
                        ArenaTeamStats const& stats = at->GetStats();

                        std::ostringstream msg;
                        msg << "1v1 투기장 전적"
                            << "\n평점: " << stats.Rating
                            << "\n순위: " << stats.Rank
                            << "\n시즌 경기: " << stats.SeasonGames
                            << "\n시즌 승리: " << stats.SeasonWins
                            << "\n이번 주 경기: " << stats.WeekGames
                            << "\n이번 주 승리: " << stats.WeekWins;

                        SendArena1v1Message(player, msg.str());
                    }
                    else
                        SendArena1v1Message(player, "1v1 투기장 팀이 없습니다.");

                    CloseGossipMenuFor(player);
                    return true;
                }

                case 5:
                {
                    ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(
                        player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_5v5))
                    );

                    if (!at)
                    {
                        SendArena1v1Message(player, "해산할 1v1 투기장 팀이 없습니다.");
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    if (at->IsFighting())
                    {
                        SendArena1v1Message(player, "전투 중인 1v1 투기장 팀은 해산할 수 없습니다.");
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    at->Disband(player->GetSession());

                    SendArena1v1Message(player, "1v1 투기장 팀이 해산되었습니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                default:
                    break;
            }

            OnGossipHello(player);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_1v1arenaAI(creature);
    }
};

class npc_arena1v1_world : public WorldScript
{
public:
    npc_arena1v1_world() : WorldScript("npc_arena1v1_world") { }

    void OnConfigLoad(bool reload) override
    {
        MindsearIsMyHero();

        if (reload)
            TC_LOG_INFO("server.loading", "Loaded 1v1 Arena config reload");
        else
            TC_LOG_INFO("server.loading", "Loaded 1v1 Arena script");
    }
};

void AddSC_npc_1v1arena()
{
    new npc_1v1arena();
    new npc_arena1v1_world();
}