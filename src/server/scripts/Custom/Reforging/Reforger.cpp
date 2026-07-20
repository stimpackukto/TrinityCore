#include "Reforging.h"
#include <sstream>
#include <string>
#include <vector>

#include "Creature.h"
#include "Define.h"
#include "EventProcessor.h"
#include "GossipDef.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "Transaction.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"
#include "Channel.h"
#include "Chat.h"

#include "CustomRP.h"

/*
Reforging by Rochet2
http://rochet2.github.io/
*/

// ── 이 파일 내부 전용 RP 유틸 ────────────────────────────────────────────────
namespace RP
{
    static inline long long Get(uint32 accountId)
    {
        if (!accountId)
            return 0;

        return static_cast<long long>(::GetRP(accountId));
    }

    // CustomRP.h 기준 RP 차감
    static inline bool TrySpend(uint32 accountId, uint32 amount, long long& outAfter)
    {
        if (!accountId)
        {
            outAfter = 0;
            return false;
        }

        if (!::HasEnoughRP(accountId, amount))
        {
            outAfter = Get(accountId);
            return false;
        }

        if (!::SubtractRP(accountId, amount))
        {
            outAfter = Get(accountId);
            return false;
        }

        outAfter = Get(accountId);
        return true;
    }
} // namespace RP

// ── 비용 ────────────────────────────────────────────────────────────────────
static constexpr uint32 REFORGE_COST_RP = 1000; // 연마
static constexpr uint32 RESTORE_COST_RP = 500;  // 삭제

static inline uint32 GetAccountId(Player* player)
{
    return (player && player->GetSession()) ? player->GetSession()->GetAccountId() : 0;
}

static inline uint32 GetPlayerGuidLow(Player* player)
{
    return player ? static_cast<uint32>(player->GetGUID().GetCounter()) : 0;
}

static inline uint32 GetGuildTechDiscountedCost(Player* player, uint32 baseCost, uint32& discount)
{
    uint32 accountId = GetAccountId(player);
    uint32 guid = GetPlayerGuidLow(player);

    return ApplyGuildTechDiscount(
        accountId,
        guid,
        "r2c",
        baseCost,
        20,
        discount
    );
}

static inline std::string MakeCostText(Player* player, uint32 baseCost)
{
    uint32 discount = 0;
    uint32 finalCost = GetGuildTechDiscountedCost(player, baseCost, discount);

    if (discount > 0)
    {
        return Trinity::StringFormat(
            "{} RP → {} RP |cFF3ECB3C(길드 테크니크 20% 할인)|r",
            baseCost,
            finalCost
        );
    }

    return Trinity::StringFormat("{} RP", baseCost);
}

// Remember to add to GetStatName too
static const ItemModType statTypes[] =
{
    ITEM_MOD_SPIRIT,
    ITEM_MOD_DODGE_RATING,
    ITEM_MOD_PARRY_RATING,
    ITEM_MOD_HIT_RATING,
    ITEM_MOD_CRIT_RATING,
    ITEM_MOD_HASTE_RATING,
    ITEM_MOD_EXPERTISE_RATING,
	ITEM_MOD_ARMOR_PENETRATION_RATING
};
static const uint8 stat_type_max = sizeof(statTypes) / sizeof(*statTypes);

static const char* GetStatName(uint32 ItemStatType)
{
    switch (ItemStatType)
    {
        case ITEM_MOD_SPIRIT:             return "정신력";
        case ITEM_MOD_DODGE_RATING:       return "회피 숙련";
        case ITEM_MOD_PARRY_RATING:       return "무기 막기 숙련";
        case ITEM_MOD_HIT_RATING:         return "적중 숙련";
        case ITEM_MOD_CRIT_RATING:        return "치명타 숙련";
        case ITEM_MOD_HASTE_RATING:       return "가속 숙련";
        case ITEM_MOD_EXPERTISE_RATING:   return "숙련";
		case ITEM_MOD_ARMOR_PENETRATION_RATING:     return "방어구 관통력";
        default:                          return nullptr;
    }
}

static const char* GetSlotName(uint8 slot, WorldSession* /*session*/)
{
    switch (slot)
    {
        case EQUIPMENT_SLOT_HEAD:        return "머리";
        case EQUIPMENT_SLOT_NECK:        return "목";
        case EQUIPMENT_SLOT_SHOULDERS:   return "어깨";
        case EQUIPMENT_SLOT_BODY:        return "셔츠";
        case EQUIPMENT_SLOT_CHEST:       return "가슴";
        case EQUIPMENT_SLOT_WAIST:       return "허리";
        case EQUIPMENT_SLOT_LEGS:        return "다리";
        case EQUIPMENT_SLOT_FEET:        return "발";
        case EQUIPMENT_SLOT_WRISTS:      return "손목";
        case EQUIPMENT_SLOT_HANDS:       return "손";
        case EQUIPMENT_SLOT_FINGER1:     return "오른쪽 반지";
        case EQUIPMENT_SLOT_FINGER2:     return "왼쪽 반지";
        case EQUIPMENT_SLOT_TRINKET1:    return "오른쪽 장신구";
        case EQUIPMENT_SLOT_TRINKET2:    return "왼쪽 장신구";
        case EQUIPMENT_SLOT_BACK:        return "등";
        case EQUIPMENT_SLOT_MAINHAND:    return "주무기";
        case EQUIPMENT_SLOT_OFFHAND:     return "보조무기";
        case EQUIPMENT_SLOT_TABARD:      return "겉옷";
        case EQUIPMENT_SLOT_RANGED:      return "원거리 무기";
        default:                         return nullptr;
    }
}

static uint32 Melt(uint8 i, uint8 j)
{
    return (i << 8) + j;
}

static void Unmelt(uint32 melt, uint8& i, uint8& j)
{
    i = melt >> 8;
    j = melt & 0xFF;
}

static Item* GetEquippedItem(Player* player, uint32 guidlow)
{
    for (uint8 i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        if (Item* pItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->GetGUID().GetCounter() == guidlow)
                return pItem;
    return NULL;
}

static bool IsReforgable(Item* invItem, Player* player)
{
    if (invItem->GetOwnerGUID() != player->GetGUID())
        return false;

    const ItemTemplate* pProto = invItem->GetTemplate();

    if (!pProto->StatsCount || pProto->StatsCount >= MAX_ITEM_PROTO_STATS) // Mandatory! Do NOT remove or edit
        return false;

    if (!player->reforgeMap.empty() && player->reforgeMap.find(invItem->GetGUID().GetCounter()) != player->reforgeMap.end()) // Mandatory! Do NOT remove or edit
        return false;

    for (uint32 i = 0; i < pProto->StatsCount; ++i)
    {
        if (!GetStatName(pProto->ItemStat[i].ItemStatType))
            continue;

        if (((int32)floorf((float)pProto->ItemStat[i].ItemStatValue * 0.4f)) > 1)
            return true;
    }
    return false;
}

// [CHANGED] 결제는 여기서 하지 않고(중복 차감 방지), 확정 시점에서 1회 결제 후 호출
static void UpdatePlayerReforgeStats(Item* invItem, Player* player, uint32 decrease, uint32 increase) // stat types
{
    const ItemTemplate* pProto = invItem->GetTemplate();

    int32 stat_diff = 0;
    for (uint32 i = 0; i < pProto->StatsCount; ++i)
    {
        if (pProto->ItemStat[i].ItemStatType == increase)
            return; // Should not have the increased stat already
        if (pProto->ItemStat[i].ItemStatType == decrease)
            stat_diff = (int32)floorf((float)pProto->ItemStat[i].ItemStatValue * 0.4f);
    }
    if (stat_diff <= 0)
        return; // Should have some kind of diff

    // Update player stats
    if (invItem->IsEquipped())
        player->_ApplyItemMods(invItem, invItem->GetSlot(), false);

    uint32 guidlow = invItem->GetGUID().GetCounter();
    ReforgeData& data = player->reforgeMap[guidlow];
    data.increase = increase;
    data.decrease = decrease;
    data.stat_value = stat_diff;

    if (invItem->IsEquipped())
        player->_ApplyItemMods(invItem, invItem->GetSlot(), true);

    SendReforgePacket(player, invItem->GetEntry(), 0, &data);
}

class REFORGE_PLAYER : public PlayerScript
{
public:
    REFORGE_PLAYER() : PlayerScript("REFORGE_PLAYER")
    {
        CharacterDatabase.DirectExecute(
            "DELETE FROM `custom_reforging` "
            "WHERE NOT EXISTS (SELECT 1 FROM `item_instance` WHERE `item_instance`.`guid` = `custom_reforging`.`GUID`)");
    }

    class SendRefPackLogin : public BasicEvent
    {
    public:
        SendRefPackLogin(Player* _player) : player(_player)
        {
            _player->m_Events.AddEvent(this, _player->m_Events.CalculateTime(Milliseconds(1000)));
        }

        bool Execute(uint64, uint32) override
        {
            SendReforgePackets(player);
            return true;
        }
        Player* player;
    };

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        uint32 playerGUID = player->GetGUID().GetCounter();
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT `GUID`, `increase`, `decrease`, `stat_value` FROM `custom_reforging` WHERE `Owner` = {}",
            playerGUID);

        if (result)
        {
            do
            {
                uint32 lowGUID = (*result)[0].GetUInt32();

                ObjectGuid itemGuid = ObjectGuid::Create<HighGuid::Item>(lowGUID);
                Item* invItem = player->GetItemByGuid(itemGuid);

                if (invItem && invItem->IsEquipped())
                    player->_ApplyItemMods(invItem, invItem->GetSlot(), false);

                ReforgeData& data = player->reforgeMap[lowGUID];
                data.increase = (*result)[1].GetUInt32();
                data.decrease = (*result)[2].GetUInt32();
                data.stat_value = (*result)[3].GetInt32();

                if (invItem && invItem->IsEquipped())
                    player->_ApplyItemMods(invItem, invItem->GetSlot(), true);

            } while (result->NextRow());

            new SendRefPackLogin(player);
        }
    }

    void OnSave(Player* player) override
    {
        uint32 lowguid = player->GetGUID().GetCounter();
        auto trans = CharacterDatabase.BeginTransaction();
        trans->PAppend("DELETE FROM `custom_reforging` WHERE `Owner` = {}", lowguid);

        if (!player->reforgeMap.empty())
        {
            std::vector<Item*> items = GetItemList(player);
            for (std::vector<Item*>::const_iterator it = items.begin(); it != items.end(); ++it)
            {
                ReforgeMapType::const_iterator it2 = player->reforgeMap.find((*it)->GetGUID().GetCounter());
                if (it2 == player->reforgeMap.end())
                    continue;

                const ReforgeData& data = it2->second;
                trans->PAppend(
                    "REPLACE INTO `custom_reforging` (`GUID`, `increase`, `decrease`, `stat_value`, `Owner`) VALUES ({}, {}, {}, {}, {})",
                    it2->first, data.increase, data.decrease, data.stat_value, lowguid);
            }
        }

        if (trans->GetSize())
            CharacterDatabase.CommitTransaction(trans);
    }
};

class REFORGER_NPC : public CreatureScript
{
public:
    REFORGER_NPC() : CreatureScript("REFORGER_NPC") { }

    class ReforgeAI : public ScriptedAI
    {
    public:
        ReforgeAI(Creature* creature) : ScriptedAI(creature) { }

        struct Timed : public BasicEvent
        {
            Timed(Player* player, Creature* creature) : guid(creature->GetGUID()), player(player), triggered(false)
            {
                CloseGossipMenuFor(player);
                player->m_Events.AddEvent(this, player->m_Events.CalculateTime(Milliseconds(1)));
            }

            bool Execute(uint64, uint32) override
            {
                if (!triggered)
                {
                    triggered = true;
                    player->m_Events.AddEvent(this, player->m_Events.CalculateTime(Milliseconds(1)));
                    return false;
                }
                if (Creature* creature = player->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_GOSSIP))
                    OnGossipHello(player, creature);
                return true;
            }

            ObjectGuid guid;
            Player* player;
            bool triggered;
        };

        bool OnGossipHello(Player* player) override
        {
            return OnGossipHello(player, me);
        }

        static bool OnGossipHello(Player* player, Creature* creature)
        {
            std::string reforgeCostText = Trinity::StringFormat(
				"재연마할 아이템의 슬롯을 선택하세요({}):",
				MakeCostText(player, REFORGE_COST_RP)
			);

			AddGossipItemFor(player, GOSSIP_ICON_BATTLE, reforgeCostText, 0, Melt(MAIN_MENU, 0));

            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                if (Item* invItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    if (IsReforgable(invItem, player))
                        if (const char* slotname = GetSlotName(slot, player->GetSession()))
                            AddGossipItemFor(player, GOSSIP_ICON_TRAINER, slotname, 0, Melt(SELECT_STAT_REDUCE, slot));
            }

            // [CHANGED] 삭제 비용 안내 문구
            std::string restoreCostText = Trinity::StringFormat(
				"재연마 제거 ({})",
				MakeCostText(player, RESTORE_COST_RP)
			);

			AddGossipItemFor(player, GOSSIP_ICON_TRAINER, restoreCostText, 0, Melt(SELECT_RESTORE, 0));

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "메뉴 새로고침", 0, Melt(MAIN_MENU, 0));
            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menu_id*/, uint32 gossipListId) override
        {
            uint32 sender = player->PlayerTalkClass->GetGossipOptionSender(gossipListId);
            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            return OnGossipSelect(player, me, sender, action);
        }

        static bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 melt)
        {
            ClearGossipMenuFor(player);

            uint8 menu, action;
            Unmelt(melt, menu, action);

            switch (menu)
            {
                case MAIN_MENU:
                    OnGossipHello(player, creature);
                    break;

                case SELECT_STAT_REDUCE:
                    // action = slot
                    if (Item* invItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, action))
                    {
                        if (IsReforgable(invItem, player))
                        {
                            uint32 guidlow = invItem->GetGUID().GetCounter();
                            const ItemTemplate* pProto = invItem->GetTemplate();

                            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "감소시킬 능력치를 선택하세요:", sender, melt);

                            for (uint32 i = 0; i < pProto->StatsCount; ++i)
                            {
                                int32 stat_diff = ((int32)floorf((float)pProto->ItemStat[i].ItemStatValue * 0.4f));
                                if (stat_diff > 1)
                                    if (const char* stat_name = GetStatName(pProto->ItemStat[i].ItemStatType))
                                    {
                                        std::ostringstream oss;
                                        oss << stat_name << " (" << pProto->ItemStat[i].ItemStatValue << " |cFFDB2222-" << stat_diff << "|r)";
                                        AddGossipItemFor(player, GOSSIP_ICON_TRAINER, oss.str(), guidlow, Melt(SELECT_STAT_INCREASE, i));
                                    }
                            }
                            AddGossipItemFor(player, GOSSIP_ICON_TALK, "뒤로..", 0, Melt(MAIN_MENU, 0));
                            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
                        }
                        else
                        {
                            player->GetSession()->SendNotification("잘못된 아이템이 선택되었습니다");
                            OnGossipHello(player, creature);
                        }
                    }
                    else
                    {
                        player->GetSession()->SendNotification("잘못된 아이템이 선택되었습니다");
                        OnGossipHello(player, creature);
                    }
                    break;

                case SELECT_STAT_INCREASE:
                    // sender = item guidlow
                    // action = StatsCount id
                    {
                        Item* invItem = GetEquippedItem(player, sender);
                        if (invItem)
                        {
                            const ItemTemplate* pProto = invItem->GetTemplate();
                            int32 stat_diff = ((int32)floorf((float)pProto->ItemStat[action].ItemStatValue * 0.4f));

                            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "증가시킬 능력치를 선택하세요:", sender, melt);

                            for (uint8 i = 0; i < stat_type_max; ++i)
                            {
                                bool cont = false;
                                for (uint32 j = 0; j < pProto->StatsCount; ++j)
                                {
                                    if (statTypes[i] == pProto->ItemStat[j].ItemStatType)
                                    {
                                        cont = true;
                                        break;
                                    }
                                }
                                if (cont)
                                    continue;

                                if (const char* stat_name = GetStatName(statTypes[i]))
                                {
                                    std::ostringstream oss;
                                    oss << stat_name << " |cFF3ECB3C+" << stat_diff << "|r";

                                    // [CHANGED] 확인창: 비용 1000 RP 표시, gold cost 파라미터는 0
                                    std::string confirm =
									"정말로 재연마하시겠습니까?\n\n" +
									pProto->Name1 +
									"\n\n비용: " +
									MakeCostText(player, REFORGE_COST_RP);

                                    AddGossipItemFor(
                                        player,
                                        GOSSIP_ICON_INTERACT_1,
                                        oss.str(),
                                        sender,
                                        Melt(i, (uint8)pProto->ItemStat[action].ItemStatType),
                                        confirm,
                                        0,
                                        false);
                                }
                            }

                            AddGossipItemFor(player, GOSSIP_ICON_TALK, "뒤로..", 0, Melt(SELECT_STAT_REDUCE, invItem->GetSlot()));
                            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
                        }
                        else
                        {
                            player->GetSession()->SendNotification("잘못된 아이템이 선택되었습니다");
                            OnGossipHello(player, creature);
                        }
                    }
                    break;

                case SELECT_RESTORE:
                    {
                        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "재연마를 제거할 슬롯을 선택하세요:", sender, melt);
                        if (!player->reforgeMap.empty())
                        {
                            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
                            {
                                if (Item* invItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                                    if (player->reforgeMap.find(invItem->GetGUID().GetCounter()) != player->reforgeMap.end())
                                        if (const char* slotname = GetSlotName(slot, player->GetSession()))
                                        {
                                            // [CHANGED] 확인창 비용 안내
                                            std::string confirm =
											"다음 아이템의 재연마를 제거하시겠습니까?\n\n" +
											invItem->GetTemplate()->Name1 +
											"\n\n비용: " +
											MakeCostText(player, RESTORE_COST_RP);

                                            AddGossipItemFor(
                                                player,
                                                GOSSIP_ICON_INTERACT_1,
                                                slotname,
                                                invItem->GetGUID().GetCounter(),
                                                Melt(RESTORE, 0),
                                                confirm,
                                                0,
                                                false);
                                        }
                            }
                        }
                        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "메뉴 새로고침", sender, melt);
                        AddGossipItemFor(player, GOSSIP_ICON_TALK, "뒤로..", 0, Melt(MAIN_MENU, 0));
                        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
                    }
                    break;

                case RESTORE:
                    // sender = item guidlow
                    {
                        ObjectGuid itemGuid = ObjectGuid::Create<HighGuid::Item>(sender);
                        if (player->GetItemByGuid(ObjectGuid(itemGuid)))
                        {
                            if (!player->reforgeMap.empty() && player->reforgeMap.find(sender) != player->reforgeMap.end())
                            {
                                // [CHANGED] 삭제 비용 500 RP 차감
                                uint32 accountId = GetAccountId(player);
                                long long after = 0;
                                Item* invItem = GetEquippedItem(player, sender);
                                if (!invItem)
                                {
                                    player->GetSession()->SendNotification("잘못된 아이템이 선택되었습니다");
                                    OnGossipHello(player, creature);
                                    break;
                                }

                                const ItemTemplate* proto = invItem->GetTemplate();

                                uint32 guildDiscount = 0;
								uint32 finalCost = GetGuildTechDiscountedCost(player, RESTORE_COST_RP, guildDiscount);

								if (accountId && RP::TrySpend(accountId, finalCost, after))
								{
									RemoveReforge(player, sender, true);

									if (guildDiscount > 0)
									{
										std::string msg = Trinity::StringFormat(
											"재연마 [|cFFA335EE{}|r] 제거되었습니다 ({} RP 소모, 길드 테크니크 {} RP 할인), 잔여 RP: {}",
											proto->Name1,
											finalCost,
											guildDiscount,
											after
										);
										ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
									}
									else
									{
										std::string msg = Trinity::StringFormat(
											"재연마 [|cFFA335EE{}|r] 제거되었습니다 ({} RP 소모), 잔여 RP: {}",
											proto->Name1,
											finalCost,
											after
										);
										ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
									}
								}
								else
								{
									long long currentRp = RP::Get(accountId);
									std::string msg = Trinity::StringFormat(
										"RP가 부족합니다 (필요: {} RP), 보유 RP: {}",
										finalCost,
										currentRp
									);
									ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
								}
                            }
                        }
                        OnGossipHello(player, creature);
                    }
                    break;

                default: // Reforge
                    // sender = item guidlow
                    // menu = stat type to increase index to statTypes[]
                    // action = stat type to decrease
                    {
                        if (menu < stat_type_max)
                        {
                            Item* invItem = GetEquippedItem(player, sender);
                            if (invItem && IsReforgable(invItem, player))
                            {
                                // [CHANGED] 연마 비용 1000 RP 차감
                                uint32 accountId = GetAccountId(player);
                                long long after = 0;
								const ItemTemplate* proto = invItem->GetTemplate();

                                uint32 guildDiscount = 0;
								uint32 finalCost = GetGuildTechDiscountedCost(player, REFORGE_COST_RP, guildDiscount);

								if (accountId && RP::TrySpend(accountId, finalCost, after))
								{
									UpdatePlayerReforgeStats(invItem, player, action, statTypes[menu]);

									if (guildDiscount > 0)
									{
										std::string msg = Trinity::StringFormat(
											"재연마 [|cFFA335EE{}|r] 완료되었습니다 ({} RP 소모, 길드 테크니크 {} RP 할인), 잔여 RP: {}",
											proto->Name1,
											finalCost,
											guildDiscount,
											after
										);
										ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
									}
									else
									{
										std::string msg = Trinity::StringFormat(
											"재연마 [|cFFA335EE{}|r] 완료되었습니다 ({} RP 소모), 잔여 RP: {}",
											proto->Name1,
											finalCost,
											after
										);
										ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
									}
								}
								else
								{
									long long currentRp = RP::Get(accountId);
									std::string msg = Trinity::StringFormat(
										"RP가 부족합니다 (필요: {} RP), 보유 RP: {}",
										finalCost,
										currentRp
									);
									ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
								}
                            }
                            else
                            {
                                player->GetSession()->SendNotification("잘못된 아이템이 선택되었습니다");
                            }
                        }
                        new Timed(player, creature);
                    }
            }
            return true;
        }

        enum Menus
        {
            MAIN_MENU = 200, // stat_type_max
            SELECT_ITEM,
            SELECT_STAT_REDUCE,
            SELECT_STAT_INCREASE,
            SELECT_RESTORE,
            RESTORE,
            REFORGE,
        };
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new ReforgeAI(creature);
    }
};

void AddSC_REFORGER_NPC()
{
    new REFORGER_NPC;
    new REFORGE_PLAYER;
}
