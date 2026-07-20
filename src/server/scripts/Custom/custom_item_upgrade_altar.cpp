#include "ScriptMgr.h"
#include "Player.h"
#include "WorldSession.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Chat.h"
#include "ScriptedGossip.h"
#include "GossipDef.h"
#include "Util.h"
#include "World.h"

#include <array>
#include <string>
#include <vector>

namespace
{
    constexpr uint32 ITEM_OLD_BATTLE_MARK = 3438;          // 낡은 전장의 훈장
    constexpr uint32 ITEM_APPRECIATION_CERTIFICATE = 45924; // 감사장

    constexpr uint32 ACTION_BACK         = 10;
    constexpr uint32 ACTION_ITEM_BASE    = 1000;
    constexpr uint32 ACTION_ADD_BASE     = 5000;
    constexpr uint32 ACTION_REPLACE_BASE = 10000;

    constexpr uint32 ACTION_REPLACE_DIVISOR = 10;

    struct CustomEnchantInfo
    {
        uint32 EnchantId = 0;
        std::string Description;
    };

    struct UpgradeCostInfo
    {
        uint32 Rp = 0;
        uint32 OldBattleMark = 0;
        uint32 AppreciationCertificate = 0;
    };

    // 영구 저장 확인된 슬롯만 사용
    // 메뉴상 1번째 강화 = PROP_ENCHANTMENT_SLOT_2
    // 메뉴상 2번째 강화 = PROP_ENCHANTMENT_SLOT_3
    // 메뉴상 3번째 강화 = PROP_ENCHANTMENT_SLOT_4
    std::array<EnchantmentSlot, 3> const CustomUpgradeSlots =
    {
        PROP_ENCHANTMENT_SLOT_2,
        PROP_ENCHANTMENT_SLOT_3,
        PROP_ENCHANTMENT_SLOT_4
    };

    void SendMsg(Player* player, std::string const& text)
    {
        if (!player)
            return;

        ChatHandler(player->GetSession()).SendSysMessage(text.c_str());
    }

    std::string RpText(uint32 value)
    {
        return Trinity::StringFormat("{} RP", value);
    }

    uint32 GetUpgradeSuccessChance(uint8 customSlotIndex)
    {
        switch (customSlotIndex)
        {
            case 0:
                return 100; // 1번째 강화
            case 1:
                return 60;  // 2번째 강화
            case 2:
                return 20;  // 3번째 강화
            default:
                return 0;
        }
    }

    UpgradeCostInfo GetUpgradeCost(uint8 customSlotIndex)
    {
		// rp 훈장 감사장
        switch (customSlotIndex)
        {
            case 0:
                return { 2000, 0, 0 };

            case 1:
                return { 3000, 0, 3 };

            case 2:
                return { 5000, 0, 5 };

            default:
                return { 0, 0, 0 };
        }
    }

    std::string GetUpgradeCostText(uint8 customSlotIndex)
    {
        UpgradeCostInfo cost = GetUpgradeCost(customSlotIndex);

        std::string text = RpText(cost.Rp);

        if (cost.OldBattleMark > 0)
            text += Trinity::StringFormat(" / 낡은 전장의 훈장 {}개", cost.OldBattleMark);

        if (cost.AppreciationCertificate > 0)
            text += Trinity::StringFormat(" / 감사장 {}개", cost.AppreciationCertificate);

        return text;
    }

    bool RollUpgradeSuccess(uint8 customSlotIndex)
    {
        uint32 chance = GetUpgradeSuccessChance(customSlotIndex);

        if (chance >= 100)
            return true;

        if (chance == 0)
            return false;

        return urand(1, 100) <= chance;
    }

    std::vector<CustomEnchantInfo> LoadCustomUpgradeEnchants()
    {
        std::vector<CustomEnchantInfo> list;

        QueryResult result = LoginDatabase.Query(
            "SELECT id, description "
            "FROM appdata.data_wotlk_spellitemenchantment "
            "WHERE custom_upgrade_enabled = 1 "
            "ORDER BY id ASC"
        );

        if (!result)
            return list;

        do
        {
            Field* fields = result->Fetch();

            CustomEnchantInfo info;
            info.EnchantId = fields[0].GetUInt32();
            info.Description = fields[1].GetString();

            if (info.EnchantId == 0)
                continue;

            if (!sSpellItemEnchantmentStore.LookupEntry(info.EnchantId))
                continue;

            list.push_back(info);
        }
        while (result->NextRow());

        return list;
    }

    CustomEnchantInfo const* GetCustomEnchantInfo(uint32 enchantId, std::vector<CustomEnchantInfo> const& enchants)
    {
        if (enchantId == 0)
            return nullptr;

        for (CustomEnchantInfo const& info : enchants)
        {
            if (info.EnchantId == enchantId)
                return &info;
        }

        return nullptr;
    }

    std::string GetCustomEnchantDescription(uint32 enchantId, std::vector<CustomEnchantInfo> const& enchants)
    {
        CustomEnchantInfo const* info = GetCustomEnchantInfo(enchantId, enchants);

        if (!info)
            return Trinity::StringFormat("기존 새김 {}", enchantId);

        return info->Description;
    }

    bool HasCustomUpgrade(Item* item)
    {
        if (!item)
            return false;

        for (EnchantmentSlot slot : CustomUpgradeSlots)
        {
            if (item->GetEnchantmentId(slot) != 0)
                return true;
        }

        return false;
    }

    bool FindFreeCustomSlot(Item* item, EnchantmentSlot& outSlot)
    {
        if (!item)
            return false;

        for (EnchantmentSlot slot : CustomUpgradeSlots)
        {
            if (item->GetEnchantmentId(slot) == 0)
            {
                outSlot = slot;
                return true;
            }
        }

        return false;
    }

    uint8 GetCustomSlotIndex(EnchantmentSlot targetSlot)
    {
        for (uint8 i = 0; i < static_cast<uint8>(CustomUpgradeSlots.size()); ++i)
        {
            if (CustomUpgradeSlots[i] == targetSlot)
                return i;
        }

        return 0;
    }

    uint32 MakeReplaceAction(uint8 itemSlot, uint8 customSlotIndex)
    {
        return ACTION_REPLACE_BASE + uint32(itemSlot) * ACTION_REPLACE_DIVISOR + customSlotIndex;
    }

    bool DecodeReplaceAction(uint32 action, uint8& itemSlot, uint8& customSlotIndex)
    {
        if (action < ACTION_REPLACE_BASE)
            return false;

        uint32 value = action - ACTION_REPLACE_BASE;

        itemSlot = uint8(value / ACTION_REPLACE_DIVISOR);
        customSlotIndex = uint8(value % ACTION_REPLACE_DIVISOR);

        if (customSlotIndex >= static_cast<uint8>(CustomUpgradeSlots.size()))
            return false;

        return true;
    }

    bool IsWeaponItem(Item* item)
    {
        if (!item)
            return false;

        ItemTemplate const* proto = item->GetTemplate();

        if (!proto)
            return false;

        if (proto->Class != ITEM_CLASS_WEAPON)
            return false;

        if (proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE)
            return false;

        return true;
    }

    std::string GetItemName(Item* item)
    {
        if (!item)
            return "알 수 없는 아이템";

        ItemTemplate const* proto = item->GetTemplate();

        if (!proto)
            return Trinity::StringFormat("아이템 {}", item->GetEntry());

        if (!proto->Name1.empty())
            return proto->Name1;

        return Trinity::StringFormat("아이템 {}", item->GetEntry());
    }

	void SendUpgradeSuccessWorldMessage(Player* player, Item* item, uint8 slotIndex, std::string const& enchantDescription, bool isReplace)
	{
		if (!player || !item)
			return;

		std::string message = Trinity::StringFormat(
			"|cff00ff00[빛의 새김]|r {} 님이 {}의 {}번째 {}에 성공했습니다. 결과: {}",
			player->GetName(),
			GetItemName(item),
			uint32(slotIndex + 1),
			isReplace ? "새김 교환" : "슬롯 새김",
			enchantDescription
		);

		//sWorld->SendServerMessage(SERVER_MSG_STRING, message.c_str());
		ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
		
	}

    uint32 GetAccountRp(Player* player)
    {
        if (!player || !player->GetSession())
            return 0;

        uint32 accountId = player->GetSession()->GetAccountId();

        std::string sql = Trinity::StringFormat(
            "SELECT reg_point "
            "FROM appdata.account_data "
            "WHERE id = {} "
            "LIMIT 1",
            accountId
        );

        QueryResult result = LoginDatabase.Query(sql.c_str());

        if (!result)
            return 0;

        Field* fields = result->Fetch();
        return fields[0].GetUInt32();
    }

    bool AddAccountRp(Player* player, uint32 amount)
    {
        if (!player || !player->GetSession() || amount == 0)
            return false;

        uint32 accountId = player->GetSession()->GetAccountId();

        std::string sql = Trinity::StringFormat(
            "UPDATE appdata.account_data "
            "SET reg_point = reg_point + {} "
            "WHERE id = {}",
            amount,
            accountId
        );

        LoginDatabase.Execute(sql.c_str());
        return true;
    }

    bool TakeAccountRp(Player* player, uint32 amount)
    {
        if (!player || !player->GetSession())
            return false;

        if (amount == 0)
            return true;

        uint32 accountId = player->GetSession()->GetAccountId();

        if (GetAccountRp(player) < amount)
            return false;

        std::string sql = Trinity::StringFormat(
            "UPDATE appdata.account_data "
            "SET reg_point = reg_point - {} "
            "WHERE id = {} "
            "AND reg_point >= {}",
            amount,
            accountId,
            amount
        );

        LoginDatabase.Execute(sql.c_str());
        return true;
    }

    bool HasUpgradeCost(Player* player, uint8 customSlotIndex)
    {
        if (!player)
            return false;

        UpgradeCostInfo cost = GetUpgradeCost(customSlotIndex);

        if (GetAccountRp(player) < cost.Rp)
            return false;

        if (cost.OldBattleMark > 0 && !player->HasItemCount(ITEM_OLD_BATTLE_MARK, cost.OldBattleMark, false))
            return false;

        if (cost.AppreciationCertificate > 0 && !player->HasItemCount(ITEM_APPRECIATION_CERTIFICATE, cost.AppreciationCertificate, false))
            return false;

        return true;
    }

    bool TakeUpgradeCost(Player* player, uint8 customSlotIndex)
    {
        if (!player)
            return false;

        UpgradeCostInfo cost = GetUpgradeCost(customSlotIndex);

        if (!HasUpgradeCost(player, customSlotIndex))
            return false;

        if (!TakeAccountRp(player, cost.Rp))
            return false;

        if (cost.OldBattleMark > 0)
        {
            if (!player->DestroyItemCount(ITEM_OLD_BATTLE_MARK, cost.OldBattleMark, true))
            {
                AddAccountRp(player, cost.Rp);
                return false;
            }
        }

        if (cost.AppreciationCertificate > 0)
        {
            if (!player->DestroyItemCount(ITEM_APPRECIATION_CERTIFICATE, cost.AppreciationCertificate, true))
            {
                if (cost.OldBattleMark > 0)
                    player->AddItem(ITEM_OLD_BATTLE_MARK, cost.OldBattleMark);

                AddAccountRp(player, cost.Rp);
                return false;
            }
        }

        return true;
    }

    CustomEnchantInfo const* PickRandomEnchant(std::vector<CustomEnchantInfo> const& enchants, uint32 exceptEnchantId = 0)
    {
        if (enchants.empty())
            return nullptr;

        if (exceptEnchantId != 0 && enchants.size() > 1)
        {
            std::vector<CustomEnchantInfo const*> candidates;

            for (CustomEnchantInfo const& info : enchants)
            {
                if (info.EnchantId == exceptEnchantId)
                    continue;

                candidates.push_back(&info);
            }

            if (!candidates.empty())
            {
                uint32 index = urand(0, uint32(candidates.size() - 1));
                return candidates[index];
            }
        }

        uint32 index = urand(0, uint32(enchants.size() - 1));
        return &enchants[index];
    }
}

class go_custom_item_upgrade_altar : public GameObjectScript
{
public:
    go_custom_item_upgrade_altar() : GameObjectScript("go_custom_item_upgrade_altar") { }

    struct go_custom_item_upgrade_altarAI : public GameObjectAI
    {
        explicit go_custom_item_upgrade_altarAI(GameObject* go) : GameObjectAI(go) { }

        bool OnGossipHello(Player* player) override
        {
            ShowItemMenu(player);
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            if (!player)
                return true;

            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

            ClearGossipMenuFor(player);

            if (action == ACTION_BACK)
            {
                ShowItemMenu(player);
                return true;
            }

            if (action >= ACTION_REPLACE_BASE)
            {
                uint8 itemSlot = 0;
                uint8 customSlotIndex = 0;

                if (!DecodeReplaceAction(action, itemSlot, customSlotIndex))
                {
                    SendMsg(player, "빛의 새김 교환 정보를 읽을 수 없습니다.");
                    ShowItemMenu(player);
                    return true;
                }

                ApplyRandomUpgrade(player, itemSlot, int32(customSlotIndex));
                return true;
            }

            if (action >= ACTION_ADD_BASE)
            {
                uint8 itemSlot = uint8(action - ACTION_ADD_BASE);
                ApplyRandomUpgrade(player, itemSlot, -1);
                return true;
            }

            if (action >= ACTION_ITEM_BASE)
            {
                uint8 itemSlot = uint8(action - ACTION_ITEM_BASE);
                ShowUpgradeMenu(player, itemSlot);
                return true;
            }

            ShowItemMenu(player);
            return true;
        }

        void ShowItemMenu(Player* player)
        {
            if (!player || !me)
                return;

            std::vector<CustomEnchantInfo> enchants = LoadCustomUpgradeEnchants();

            ClearGossipMenuFor(player);

            if (enchants.empty())
            {
                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    "사용 가능한 랜덤 빛의 새김 인첸이 없습니다.",
                    GOSSIP_SENDER_MAIN,
                    ACTION_BACK
                );

                SendGossipMenuFor(player, 1, me->GetGUID());
                return;
            }

            uint32 currentRp = GetAccountRp(player);

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                Trinity::StringFormat("현재 보유 RP: {}", currentRp),
                GOSSIP_SENDER_MAIN,
                ACTION_BACK
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                Trinity::StringFormat("1번째 비용: {}", GetUpgradeCostText(0)),
                GOSSIP_SENDER_MAIN,
                ACTION_BACK
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                Trinity::StringFormat("2번째 비용: {}", GetUpgradeCostText(1)),
                GOSSIP_SENDER_MAIN,
                ACTION_BACK
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                Trinity::StringFormat("3번째 비용: {}", GetUpgradeCostText(2)),
                GOSSIP_SENDER_MAIN,
                ACTION_BACK
            );

            bool hasWeapon = false;

            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

                if (!item)
                    continue;

                if (!IsWeaponItem(item))
                    continue;

                EnchantmentSlot freeSlot = PROP_ENCHANTMENT_SLOT_2;
                bool hasFreeSlot = FindFreeCustomSlot(item, freeSlot);
                bool hasCustomUpgrade = HasCustomUpgrade(item);

                if (!hasFreeSlot && !hasCustomUpgrade)
                    continue;

                hasWeapon = true;

                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_VENDOR,
                    Trinity::StringFormat("{} 빛의 새김 관리", GetItemName(item)),
                    GOSSIP_SENDER_MAIN,
                    ACTION_ITEM_BASE + slot
                );
            }

            if (!hasWeapon)
            {
                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    "빛의 새김 가능한 장착 무기가 없습니다.",
                    GOSSIP_SENDER_MAIN,
                    ACTION_BACK
                );
            }

            SendGossipMenuFor(player, 1, me->GetGUID());
        }

        void ShowUpgradeMenu(Player* player, uint8 itemSlot)
        {
            if (!player || !me)
                return;

            std::vector<CustomEnchantInfo> enchants = LoadCustomUpgradeEnchants();

            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, itemSlot);

            ClearGossipMenuFor(player);

            if (enchants.empty())
            {
                SendMsg(player, "사용 가능한 빛의 새김 인첸이 없습니다.");
                ShowItemMenu(player);
                return;
            }

            if (!item)
            {
                SendMsg(player, "선택한 무기를 찾을 수 없습니다.");
                ShowItemMenu(player);
                return;
            }

            if (!IsWeaponItem(item))
            {
                SendMsg(player, "무기만 빛의 새김할 수 있습니다.");
                ShowItemMenu(player);
                return;
            }

            uint32 currentRp = GetAccountRp(player);

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                Trinity::StringFormat("선택 무기: {}", GetItemName(item)),
                GOSSIP_SENDER_MAIN,
                ACTION_BACK
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                Trinity::StringFormat("현재 보유 RP: {}", currentRp),
                GOSSIP_SENDER_MAIN,
                ACTION_BACK
            );

            EnchantmentSlot freeSlot = PROP_ENCHANTMENT_SLOT_2;
            bool hasFreeSlot = FindFreeCustomSlot(item, freeSlot);
            bool hasMenu = false;

            if (hasFreeSlot)
            {
                uint8 freeIndex = GetCustomSlotIndex(freeSlot);
                uint32 successChance = GetUpgradeSuccessChance(freeIndex);

                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_VENDOR,
                    Trinity::StringFormat(
                        "빛의 새김 - {}번째 슬롯 신규 새김",
                        uint32(freeIndex + 1)
                    ),
                    GOSSIP_SENDER_MAIN,
                    ACTION_ADD_BASE + itemSlot
                );

                hasMenu = true;
            }

            for (uint8 i = 0; i < static_cast<uint8>(CustomUpgradeSlots.size()); ++i)
            {
                EnchantmentSlot slot = CustomUpgradeSlots[i];
                uint32 enchantId = item->GetEnchantmentId(slot);

                if (enchantId == 0)
                    continue;

                uint32 successChance = GetUpgradeSuccessChance(i);

                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_VENDOR,
                    Trinity::StringFormat(
                        "{}번째 빛의 새김 교환하기 - {}",
                        uint32(i + 1),
                        GetCustomEnchantDescription(enchantId, enchants)
                    ),
                    GOSSIP_SENDER_MAIN,
                    MakeReplaceAction(itemSlot, i)
                );

                hasMenu = true;
            }

            if (!hasMenu)
            {
                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    "빛의 새김 가능한 빈 슬롯 또는 교환 가능한 기존 새김가 없습니다.",
                    GOSSIP_SENDER_MAIN,
                    ACTION_BACK
                );
            }

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "뒤로가기",
                GOSSIP_SENDER_MAIN,
                ACTION_BACK
            );

            SendGossipMenuFor(player, 1, me->GetGUID());
        }

        void ApplyRandomUpgrade(Player* player, uint8 itemSlot, int32 replaceCustomSlotIndex)
        {
            if (!player || !me)
                return;

            if (player->IsInCombat())
            {
                SendMsg(player, "전투 중에는 빛의 새김을 할 수 없습니다.");
                CloseGossipMenuFor(player);
                return;
            }

            std::vector<CustomEnchantInfo> enchants = LoadCustomUpgradeEnchants();

            if (enchants.empty())
            {
                SendMsg(player, "사용 가능한 빛의 새김 인첸이 없습니다.");
                ShowItemMenu(player);
                return;
            }

            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, itemSlot);

            if (!item)
            {
                SendMsg(player, "선택한 무기를 찾을 수 없습니다.");
                ShowItemMenu(player);
                return;
            }

            if (!IsWeaponItem(item))
            {
                SendMsg(player, "무기만 빛의 새김할 수 있습니다.");
                ShowItemMenu(player);
                return;
            }

            EnchantmentSlot targetSlot = PROP_ENCHANTMENT_SLOT_2;
            uint32 oldEnchantId = 0;
            bool isReplace = replaceCustomSlotIndex >= 0;

            if (isReplace)
            {
                if (replaceCustomSlotIndex >= int32(CustomUpgradeSlots.size()))
                {
                    SendMsg(player, "잘못된 빛의 새김 슬롯입니다.");
                    ShowUpgradeMenu(player, itemSlot);
                    return;
                }

                targetSlot = CustomUpgradeSlots[replaceCustomSlotIndex];
                oldEnchantId = item->GetEnchantmentId(targetSlot);

                if (oldEnchantId == 0)
                {
                    SendMsg(player, "선택한 슬롯에는 교환 가능한빛의 새김가 없습니다.");
                    ShowUpgradeMenu(player, itemSlot);
                    return;
                }
            }
            else
            {
                if (!FindFreeCustomSlot(item, targetSlot))
                {
                    SendMsg(player, "남은 빛의 새김 슬롯이 없습니다. 기존 새김 교환만 가능합니다.");
                    ShowUpgradeMenu(player, itemSlot);
                    return;
                }
            }

            uint8 slotIndex = GetCustomSlotIndex(targetSlot);
            uint32 successChance = GetUpgradeSuccessChance(slotIndex);

            if (!HasUpgradeCost(player, slotIndex))
            {
                SendMsg(player,
                    Trinity::StringFormat(
                        "빛의 새김 재료가 부족합니다. 필요 비용: {}",
                        GetUpgradeCostText(slotIndex)
                    )
                );

                ShowUpgradeMenu(player, itemSlot);
                return;
            }

            CustomEnchantInfo const* enchantInfo = PickRandomEnchant(enchants, oldEnchantId);

            if (!enchantInfo)
            {
                SendMsg(player, "빛의 새김 인첸 선택에 실패했습니다.");
                ShowUpgradeMenu(player, itemSlot);
                return;
            }

            if (!TakeUpgradeCost(player, slotIndex))
            {
                SendMsg(player, "빛의 새김 비용 차감에 실패했습니다.");
                ShowUpgradeMenu(player, itemSlot);
                return;
            }

            if (!RollUpgradeSuccess(slotIndex))
            {
                if (isReplace)
                {
                    SendMsg(
                        player,
                        Trinity::StringFormat(
                            "{}의 {}번째 새김 교환에 실패했습니다. 기존 새김는 유지됩니다. ",
                            GetItemName(item),
                            uint32(slotIndex + 1)
                        )
                    );
                }
                else
                {
                    SendMsg(
                        player,
                        Trinity::StringFormat(
                            "{}의 {}번째 슬롯 새김에 실패했습니다. 빈 슬롯은 유지됩니다.",
                            GetItemName(item),
                            uint32(slotIndex + 1)
                        )
                    );
                }

                CloseGossipMenuFor(player);
                return;
            }

            player->ApplyEnchantment(item, targetSlot, false);

            item->SetEnchantment(targetSlot, enchantInfo->EnchantId, 0, 0, player->GetGUID());
            item->SetState(ITEM_CHANGED, player);

            player->ApplyEnchantment(item, targetSlot, true);

            if (isReplace)
            {
                SendMsg(
                    player,
                    Trinity::StringFormat(
                        "{}의 {}번째 새김이 교환되었습니다. 기존: {} / 결과: {}",
                        GetItemName(item),
                        uint32(slotIndex + 1),
                        GetCustomEnchantDescription(oldEnchantId, enchants),
                        enchantInfo->Description
                    )
                );
            }
            else
            {
                SendMsg(
                    player,
                    Trinity::StringFormat(
                        "{}의 {}번째 슬롯에 빛의 새김이 적용되었습니다. 결과: {}",
                        GetItemName(item),
                        uint32(slotIndex + 1),
                        enchantInfo->Description
                    )
                );
            }

			SendUpgradeSuccessWorldMessage(player, item, slotIndex, enchantInfo->Description, isReplace);

            CloseGossipMenuFor(player);
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override
    {
        return new go_custom_item_upgrade_altarAI(go);
    }
};

void AddSC_custom_item_upgrade_altar()
{
    new go_custom_item_upgrade_altar();
}