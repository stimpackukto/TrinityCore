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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "WorldSession.h"
#include "Battleground.h"
#include "Common.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GossipDef.h"
#include "Group.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "QuestPackets.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldPacket.h"

namespace
{
    bool QuestRequiresItem(Quest const* quest, uint32 itemId)
    {
        if (!quest || !itemId)
            return false;

        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            if (quest->RequiredItemId[i] == itemId)
                return true;
        }

        return false;
    }

    bool QuestUsesItem(Quest const* quest, uint32 itemId)
    {
        if (!quest || !itemId)
            return false;

        if (quest->GetSrcItemId() == itemId)
            return true;

        return QuestRequiresItem(quest, itemId);
    }

    bool IsCleanupTargetQuestItem(uint32 itemId)
    {
        if (!itemId)
            return false;

        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
        if (!itemTemplate)
            return false;

        return itemTemplate->Class == ITEM_CLASS_QUEST;
    }

    bool PlayerHasOtherActiveQuestThatUsesItem(Player* player, uint32 itemId, uint32 currentQuestId)
    {
        if (!player || !itemId)
            return false;

        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 activeQuestId = player->GetQuestSlotQuestId(slot);

            if (!activeQuestId || activeQuestId == currentQuestId)
                continue;

            Quest const* activeQuest = sObjectMgr->GetQuestTemplate(activeQuestId);
            if (QuestUsesItem(activeQuest, itemId))
                return true;
        }

        return false;
    }

    void CleanupQuestRequiredItems(Player* player, Quest const* quest, uint32 questId, Quest const* nextQuestToKeep = nullptr)
    {
        if (!player || !quest)
            return;

        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            uint32 itemId = quest->RequiredItemId[i];

            if (!itemId)
                continue;

            if (!IsCleanupTargetQuestItem(itemId))
                continue;

            bool alreadyChecked = false;
            for (uint8 j = 0; j < i; ++j)
            {
                if (quest->RequiredItemId[j] == itemId)
                {
                    alreadyChecked = true;
                    break;
                }
            }

            if (alreadyChecked)
                continue;

            if (PlayerHasOtherActiveQuestThatUsesItem(player, itemId, questId))
                continue;

            if (QuestUsesItem(nextQuestToKeep, itemId))
                continue;

            uint32 count = player->GetItemCount(itemId, true);

            if (!count)
                continue;

            // 퀘스트 완료 후 더 이상 필요 없는 RequiredItemId 퀘스트 아이템은 보유 수량 전체 삭제
            // 다른 진행 중 퀘스트 또는 다음 연계 퀘스트가 RequiredItem/SourceItem으로 쓰면 유지
            player->DestroyItemCount(itemId, count, true, false);
        }
    }
}

void WorldSession::HandleQuestgiverStatusQueryOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;
    uint32 questStatus = DIALOG_STATUS_NONE;

    Object* questGiver = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT);
    if (!questGiver)
    {
        TC_LOG_INFO("network", "Error in CMSG_QUESTGIVER_STATUS_QUERY, called for non-existing questgiver ({})", guid.ToString());
        return;
    }

    switch (questGiver->GetTypeId())
    {
        case TYPEID_UNIT:
        {
            TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_STATUS_QUERY for npc {}", questGiver->GetGUID().ToString());
            if (!questGiver->ToCreature()->IsHostileTo(_player))
                questStatus = _player->GetQuestDialogStatus(questGiver);
            break;
        }
        case TYPEID_GAMEOBJECT:
        {
            TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_STATUS_QUERY for GameObject {}", questGiver->GetGUID().ToString());
            questStatus = _player->GetQuestDialogStatus(questGiver);
            break;
        }
        default:
            TC_LOG_ERROR("network", "QuestGiver called for unexpected type {}", questGiver->GetTypeId());
            break;
    }

    _player->PlayerTalkClass->SendQuestGiverStatus(uint8(questStatus), guid);
}

void WorldSession::HandleQuestgiverHelloOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_HELLO {}", guid.ToString());

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_QUESTGIVER);
    if (!creature)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleQuestgiverHelloOpcode - {} not found or you can't interact with him.", guid.ToString());
        return;
    }

    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    if (uint32 pause = creature->GetMovementTemplate().GetInteractionPauseTimer())
        creature->PauseMovement(pause);

    creature->SetHomePosition(creature->GetPosition());

    _player->PlayerTalkClass->ClearMenus();

    if (creature->AI()->OnGossipHello(_player))
        return;

    _player->PrepareGossipMenu(creature, creature->GetCreatureTemplate()->GossipMenuId, true);
    _player->SendPreparedGossip(creature);
}

void WorldSession::HandleQuestgiverAcceptQuestOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    uint32 questId;
    uint32 startCheat;
    recvData >> guid >> questId >> startCheat;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_ACCEPT_QUEST {}, quest = {}, startCheat = {}", guid.ToString(), questId, startCheat);

    Object* object;
    if (!guid.IsPlayer())
        object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT | TYPEMASK_ITEM);
    else
        object = ObjectAccessor::FindPlayer(guid);

    auto CLOSE_GOSSIP_CLEAR_SHARING_INFO = ([this]()
    {
        _player->PlayerTalkClass->SendCloseGossip();
        _player->ClearQuestSharingInfo();
    });

    if (!object)
    {
        CLOSE_GOSSIP_CLEAR_SHARING_INFO();
        return;
    }

    if (Player* playerQuestObject = object->ToPlayer())
    {
        if ((!_player->GetPlayerSharingQuest().IsEmpty() && _player->GetPlayerSharingQuest() != guid) || !playerQuestObject->CanShareQuest(questId))
        {
            CLOSE_GOSSIP_CLEAR_SHARING_INFO();
            return;
        }

        if (!_player->IsInSameRaidWith(playerQuestObject))
        {
            CLOSE_GOSSIP_CLEAR_SHARING_INFO();
            return;
        }
    }
    else
    {
        if (!object->hasQuest(questId))
        {
            CLOSE_GOSSIP_CLEAR_SHARING_INFO();
            return;
        }
    }

    if (!_player->CanInteractWithQuestGiver(object))
    {
        CLOSE_GOSSIP_CLEAR_SHARING_INFO();
        return;
    }

    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        if (!GetPlayer()->CanTakeQuest(quest, true))
        {
            CLOSE_GOSSIP_CLEAR_SHARING_INFO();
            return;
        }

        if (!_player->GetPlayerSharingQuest().IsEmpty())
        {
            Player* player = ObjectAccessor::FindPlayer(_player->GetPlayerSharingQuest());
            if (player)
            {
                player->SendPushToPartyResponse(_player, QUEST_PARTY_MSG_ACCEPT_QUEST);
                _player->ClearQuestSharingInfo();
            }
        }

        if (_player->CanAddQuest(quest, true))
        {
            _player->AddQuestAndCheckCompletion(quest, object);

            if (quest->HasFlag(QUEST_FLAGS_PARTY_ACCEPT))
            {
                if (Group* group = _player->GetGroup())
                {
                    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
                    {
                        Player* player = itr->GetSource();

                        if (!player || player == _player || !player->IsInMap(_player))
                            continue;

                        if (player->CanTakeQuest(quest, true))
                        {
                            player->SetQuestSharingInfo(_player->GetGUID(), questId);
                            player->PlayerTalkClass->SendCloseGossip();
                            _player->SendQuestConfirmAccept(quest, player);
                        }
                    }
                }
            }

            _player->PlayerTalkClass->SendCloseGossip();

            if (quest->GetSrcSpell() > 0)
                _player->CastSpell(_player, quest->GetSrcSpell(), true);

            return;
        }
    }

    CLOSE_GOSSIP_CLEAR_SHARING_INFO();
}

void WorldSession::HandleQuestgiverQueryQuestOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    uint32 questId;
    uint8 unk1;
    recvData >> guid >> questId >> unk1;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_QUERY_QUEST npc = {}, quest = {}, unk1 = {}", guid.ToString(), questId, unk1);

    Object* object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT | TYPEMASK_ITEM);
    if (!object || (!object->hasQuest(questId) && !object->hasInvolvedQuest(questId)))
    {
        _player->PlayerTalkClass->SendCloseGossip();
        return;
    }

    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        if (!_player->CanTakeQuest(quest, true))
            return;

        if (quest->IsAutoAccept() && _player->CanAddQuest(quest, true))
            _player->AddQuestAndCheckCompletion(quest, object);

        if (quest->IsAutoComplete())
            _player->PlayerTalkClass->SendQuestGiverRequestItems(quest, object->GetGUID(), _player->CanCompleteQuest(quest->GetQuestId()), true);
        else
            _player->PlayerTalkClass->SendQuestGiverQuestDetails(quest, object->GetGUID(), true);
    }
}

void WorldSession::HandleQuestQueryOpcode(WorldPackets::Quest::QueryQuestInfo& query)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUEST_QUERY quest = {}", query.QuestID);

    if (Quest const* quest = sObjectMgr->GetQuestTemplate(query.QuestID))
        _player->PlayerTalkClass->SendQuestQueryResponse(quest);
}

void WorldSession::HandleQuestgiverChooseRewardOpcode(WorldPacket& recvData)
{
    uint32 questId;
    uint32 reward;
    ObjectGuid guid;
    recvData >> guid >> questId >> reward;

    if (reward >= QUEST_REWARD_CHOICES_COUNT)
    {
        TC_LOG_ERROR("entities.player.cheat", "Error in CMSG_QUESTGIVER_CHOOSE_REWARD: player {} {} tried to get invalid reward ({}) (possible packet-hacking detected)", _player->GetName(), _player->GetGUID().ToString(), reward);
        return;
    }

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_CHOOSE_REWARD npc = {}, quest = {}, reward = {}", guid.ToString(), questId, reward);

    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return;

    Object* object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT);
    if (!object || !object->hasInvolvedQuest(questId))
        return;

    if (!_player->CanInteractWithQuestGiver(object))
        return;

    if ((!_player->CanSeeStartQuest(quest) && _player->GetQuestStatus(questId) == QUEST_STATUS_NONE) ||
        (_player->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE && !quest->IsAutoComplete()))
    {
        TC_LOG_ERROR("entities.player.cheat", "Error in QUEST_STATUS_COMPLETE: player {} {} tried to complete quest {}, but is not allowed to do so (possible packet-hacking or high latency)",
            _player->GetName(), _player->GetGUID().ToString(), questId);
        return;
    }

    if (_player->CanRewardQuest(quest, true))
    {
        if (_player->CanRewardQuest(quest, reward, true))
        {
            _player->RewardQuest(quest, reward, object);

            Quest const* nextQuestToKeep = nullptr;

            switch (object->GetTypeId())
            {
                case TYPEID_UNIT:
                {
                    Creature* questgiver = object->ToCreature();

                    if (Quest const* nextQuest = _player->GetNextQuest(questgiver, quest))
                    {
                        nextQuestToKeep = nextQuest;

                        if (_player->CanTakeQuest(nextQuest, false))
                        {
                            if (nextQuest->IsAutoAccept() && _player->CanAddQuest(nextQuest, true))
                                _player->AddQuestAndCheckCompletion(nextQuest, object);

                            _player->PlayerTalkClass->SendQuestGiverQuestDetails(nextQuest, guid, true);
                        }
                    }

                    _player->PlayerTalkClass->ClearMenus();
                    questgiver->AI()->OnQuestReward(_player, quest, reward);
                    break;
                }
                case TYPEID_GAMEOBJECT:
                {
                    GameObject* questGiver = object->ToGameObject();

                    if (Quest const* nextQuest = _player->GetNextQuest(questGiver, quest))
                    {
                        nextQuestToKeep = nextQuest;

                        if (_player->CanTakeQuest(nextQuest, false))
                        {
                            if (nextQuest->IsAutoAccept() && _player->CanAddQuest(nextQuest, true))
                                _player->AddQuestAndCheckCompletion(nextQuest, object);

                            _player->PlayerTalkClass->SendQuestGiverQuestDetails(nextQuest, guid, true);
                        }
                    }

                    _player->PlayerTalkClass->ClearMenus();
                    questGiver->AI()->OnQuestReward(_player, quest, reward);
                    break;
                }
                default:
                    break;
            }

            CleanupQuestRequiredItems(_player, quest, questId, nextQuestToKeep);
        }
    }
    else
        _player->PlayerTalkClass->SendQuestGiverOfferReward(quest, guid, true);
}

void WorldSession::HandleQuestgiverRequestRewardOpcode(WorldPacket& recvData)
{
    uint32 questId;
    ObjectGuid guid;
    recvData >> guid >> questId;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_REQUEST_REWARD npc = {}, quest = {}", guid.ToString(), questId);

    Object* object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT);
    if (!object || !object->hasInvolvedQuest(questId))
        return;

    if (!_player->CanInteractWithQuestGiver(object))
        return;

    if (_player->CanCompleteQuest(questId))
        _player->CompleteQuest(questId);

    if (_player->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
        return;

    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
        _player->PlayerTalkClass->SendQuestGiverOfferReward(quest, guid, true);
}

void WorldSession::HandleQuestgiverCancel(WorldPacket& /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_CANCEL");

    _player->PlayerTalkClass->SendCloseGossip();
}

void WorldSession::HandleQuestLogSwapQuest(WorldPacket& recvData)
{
    uint8 slot1;
    uint8 slot2;
    recvData >> slot1 >> slot2;

    if (slot1 == slot2 || slot1 >= MAX_QUEST_LOG_SIZE || slot2 >= MAX_QUEST_LOG_SIZE)
        return;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTLOG_SWAP_QUEST slot 1 = {}, slot 2 = {}", slot1, slot2);

    GetPlayer()->SwapQuestSlot(slot1, slot2);
}

void WorldSession::HandleQuestLogRemoveQuest(WorldPacket& recvData)
{
    uint8 slot;
    recvData >> slot;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTLOG_REMOVE_QUEST slot = {}", slot);

    if (slot < MAX_QUEST_LOG_SIZE)
    {
        if (uint32 questId = _player->GetQuestSlotQuestId(slot))
        {
            if (!_player->TakeQuestSourceItem(questId, true))
                return;

            Quest const* abandonedQuest = sObjectMgr->GetQuestTemplate(questId);

            if (abandonedQuest)
            {
                if (abandonedQuest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_TIMED))
                    _player->RemoveTimedQuest(questId);

                if (abandonedQuest->HasFlag(QUEST_FLAGS_FLAGS_PVP))
                {
                    _player->pvpInfo.IsHostile = _player->pvpInfo.IsInHostileArea || _player->HasPvPForcingQuest();
                    _player->UpdatePvPState();
                }
            }

            _player->TakeQuestSourceItem(questId, true);
            _player->AbandonQuest(questId);

            // RequiredItemId 퀘스트 아이템은 포기 시 삭제하지 않음
            // 포기 후 재수락/재진행 가능성을 위해 유저 인벤에 그대로 유지
            _player->RemoveActiveQuest(questId);
            _player->RemoveTimedAchievement(ACHIEVEMENT_TIMED_TYPE_QUEST, questId);

            TC_LOG_INFO("network", "Player {} abandoned quest {}", _player->GetGUID().ToString(), questId);

            if (sWorld->getBoolConfig(CONFIG_QUEST_ENABLE_QUEST_TRACKER))
            {
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_QUEST_TRACK_ABANDON_TIME);
                stmt->setUInt32(0, questId);
                stmt->setUInt32(1, _player->GetGUID().GetCounter());

                CharacterDatabase.Execute(stmt);
            }

            sScriptMgr->OnQuestStatusChange(_player, questId);
        }

        _player->SetQuestSlot(slot, 0);

        _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_QUEST_ABANDONED, 1);
    }
}

void WorldSession::HandleQuestConfirmAccept(WorldPacket& recvData)
{
    uint32 questId;
    recvData >> questId;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUEST_CONFIRM_ACCEPT quest = {}", questId);

    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        if (!quest->HasFlag(QUEST_FLAGS_PARTY_ACCEPT))
            return;

        Player* originalPlayer = ObjectAccessor::FindPlayer(_player->GetPlayerSharingQuest());
        if (!originalPlayer)
            return;

        if (!_player->IsInSameRaidWith(originalPlayer))
            return;

        if (!originalPlayer->IsActiveQuest(questId))
            return;

        if (!_player->CanTakeQuest(quest, true))
            return;

        if (_player->CanAddQuest(quest, true))
        {
            _player->AddQuestAndCheckCompletion(quest, nullptr);

            if (quest->GetSrcSpell() > 0)
                _player->CastSpell(_player, quest->GetSrcSpell(), true);
        }
    }

    _player->ClearQuestSharingInfo();
}

void WorldSession::HandleQuestgiverCompleteQuest(WorldPacket& recvData)
{
    uint32 questId;
    ObjectGuid guid;

    recvData >> guid >> questId;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_COMPLETE_QUEST npc = {}, quest = {}", guid.ToString(), questId);

    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return;

    Object* object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT);

    if (!object)
        return;

    if (!quest->HasFlag(QUEST_FLAGS_AUTOCOMPLETE))
    {
        if (!object->hasInvolvedQuest(questId))
            return;

        if (!_player->CanInteractWithQuestGiver(object))
            return;
    }
    else
    {
        if (guid != _player->GetGUID())
            return;
    }

    if (!_player->CanSeeStartQuest(quest) && _player->GetQuestStatus(questId) == QUEST_STATUS_NONE)
    {
        TC_LOG_ERROR("entities.player.cheat", "Possible hacking attempt: Player {} [{}] tried to complete quest [entry: {}] without being in possession of the quest!",
            _player->GetName(), _player->GetGUID().ToString(), questId);
        return;
    }

    if (Battleground* bg = _player->GetBattleground())
        bg->HandleQuestComplete(questId, _player);

    if (_player->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
    {
        if (quest->IsRepeatable())
            _player->PlayerTalkClass->SendQuestGiverRequestItems(quest, guid, _player->CanCompleteRepeatableQuest(quest), false);
        else
            _player->PlayerTalkClass->SendQuestGiverRequestItems(quest, guid, _player->CanRewardQuest(quest, false), false);
    }
    else
    {
        if (quest->GetReqItemsCount())
            _player->PlayerTalkClass->SendQuestGiverRequestItems(quest, guid, _player->CanRewardQuest(quest, false), false);
        else
            _player->PlayerTalkClass->SendQuestGiverOfferReward(quest, guid, true);
    }
}

void WorldSession::HandleQuestgiverQuestAutoLaunch(WorldPacket& /*recvPacket*/)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_QUEST_AUTOLAUNCH");
}

void WorldSession::HandlePushQuestToParty(WorldPacket& recvPacket)
{
    uint32 questId;
    recvPacket >> questId;

    if (!_player->CanShareQuest(questId))
        return;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_PUSHQUESTTOPARTY questId = {}", questId);

    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return;

    Player* const sender = GetPlayer();

    Group* group = sender->GetGroup();
    if (!group)
    {
        sender->SendPushToPartyResponse(sender, QUEST_PARTY_MSG_NOT_IN_PARTY);
        return;
    }

    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* receiver = itr->GetSource();

        if (!receiver || receiver == sender)
            continue;

        if (!receiver->GetPlayerSharingQuest().IsEmpty())
        {
            sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_BUSY);
            continue;
        }

        switch (receiver->GetQuestStatus(questId))
        {
            case QUEST_STATUS_REWARDED:
            {
                sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_FINISH_QUEST);
                continue;
            }
            case QUEST_STATUS_INCOMPLETE:
            case QUEST_STATUS_COMPLETE:
            {
                sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_HAVE_QUEST);
                continue;
            }
            default:
                break;
        }

        if (!receiver->SatisfyQuestLog(false))
        {
            sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_LOG_FULL);
            continue;
        }

        if (!receiver->SatisfyQuestDay(quest, false))
        {
            sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_NOT_ELIGIBLE_TODAY);
            continue;
        }

        if (!receiver->CanTakeQuest(quest, false))
        {
            sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_CANT_TAKE_QUEST);
            continue;
        }

        sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_SHARING_QUEST);

        if (quest->IsAutoComplete() && quest->IsRepeatable() && !quest->IsDailyOrWeekly())
            receiver->PlayerTalkClass->SendQuestGiverRequestItems(quest, sender->GetGUID(), receiver->CanCompleteRepeatableQuest(quest), true);
        else
        {
            receiver->SetQuestSharingInfo(sender->GetGUID(), questId);
            receiver->PlayerTalkClass->SendQuestGiverQuestDetails(quest, receiver->GetGUID(), true);

            if (quest->IsAutoAccept() && receiver->CanAddQuest(quest, true) && receiver->CanTakeQuest(quest, true))
            {
                receiver->AddQuestAndCheckCompletion(quest, sender);
                sender->SendPushToPartyResponse(receiver, QUEST_PARTY_MSG_ACCEPT_QUEST);
                receiver->ClearQuestSharingInfo();
            }
        }
    }
}

void WorldSession::HandleQuestPushResult(WorldPacket& recvPacket)
{
    ObjectGuid guid;
    uint32 questId;
    uint8 msg;
    recvPacket >> guid >> questId >> msg;

    TC_LOG_DEBUG("network", "WORLD: Received MSG_QUEST_PUSH_RESULT");

    if (!_player->GetPlayerSharingQuest())
        return;

    if (_player->GetPlayerSharingQuest() == guid)
    {
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (player)
            player->SendPushToPartyResponse(_player, static_cast<QuestShareMessages>(msg));
    }

    _player->ClearQuestSharingInfo();
}

void WorldSession::HandleQuestgiverStatusMultipleQuery(WorldPacket& /*recvPacket*/)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_STATUS_MULTIPLE_QUERY");

    _player->SendQuestGiverStatusMultiple();
}

void WorldSession::HandleQueryQuestsCompleted(WorldPacket& /*recvData*/)
{
    size_t rew_count = _player->GetRewardedQuestCount();

    WorldPacket data(SMSG_QUERY_QUESTS_COMPLETED_RESPONSE, 4 + 4 * rew_count);
    data << uint32(rew_count);

    RewardedQuestSet const& rewQuests = _player->getRewardedQuests();
    for (RewardedQuestSet::const_iterator itr = rewQuests.begin(); itr != rewQuests.end(); ++itr)
        data << uint32(*itr);

    SendPacket(&data);
}