#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "Item.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace PersonalShop
{
    static constexpr uint32 DEFAULT_SLOT_LIMIT = 10;
    static constexpr uint32 SLOT_EXPAND_AMOUNT = 10;
    static constexpr uint32 SLOT_EXPAND_PRICE_RP = 1000;

    static constexpr uint32 ACTION_REGISTER_ITEM_PRICE_BASE = 10000;

    enum GossipAction : uint32
    {
        ACTION_CREATE_SHOP_CODE       = 100,
        ACTION_ITEM_MANAGE            = 200,
        ACTION_REGISTER_ITEM_LIST     = 210,

        ACTION_CLOSE_SHOP             = 300,
        ACTION_CONFIRM_CLOSE_SHOP     = 301,

        ACTION_EXPAND_SLOT            = 400,
        ACTION_CONFIRM_EXPAND_SLOT    = 401,

        ACTION_CONFIRM_REGISTER_ITEM  = 500,
        ACTION_CANCEL_REGISTER_ITEM   = 501,

        ACTION_BACK_MAIN              = 900,
        ACTION_BACK_ITEM_MANAGE       = 901
    };

    struct ShopState
    {
        uint64 Id = 0;
        bool IsOpen = false;
        uint32 SlotLimit = 0;
        uint32 ReopenCooldownSeconds = 0;
    };

    struct PendingRegisterItem
    {
        uint8 Bag = INVENTORY_SLOT_BAG_0;
        uint8 Slot = 0;
        uint64 ItemGuid = 0;
        uint32 PriceRegPoint = 0;
    };

    static std::unordered_map<uint64, PendingRegisterItem> PendingRegisterItems;

    static uint64 GetOwnerGuid(Player* player)
    {
        return player->GetGUID().GetCounter();
    }

    static uint32 GetAccountId(Player* player)
    {
        return player->GetSession()->GetAccountId();
    }

    static uint64 GetPendingKey(Player* player)
    {
        return GetOwnerGuid(player);
    }

    static void Notify(Player* player, std::string const& message)
    {
        player->GetSession()->SendNotification(message.c_str());
    }

    static bool IsPriceInputAction(uint32 action)
    {
        return action >= ACTION_REGISTER_ITEM_PRICE_BASE &&
            action < ACTION_REGISTER_ITEM_PRICE_BASE + 200;
    }

    static uint8 GetSlotFromPriceInputAction(uint32 action)
    {
        return uint8(action - ACTION_REGISTER_ITEM_PRICE_BASE);
    }

    static void TrimText(std::string& value)
    {
        value.erase(
            value.begin(),
            std::find_if(value.begin(), value.end(), [](unsigned char ch)
            {
                return !std::isspace(ch);
            })
        );

        value.erase(
            std::find_if(value.rbegin(), value.rend(), [](unsigned char ch)
            {
                return !std::isspace(ch);
            }).base(),
            value.end()
        );
    }

    static bool GetUtf8Length(std::string const& text, size_t& length)
    {
        length = 0;

        for (size_t i = 0; i < text.size();)
        {
            unsigned char ch = static_cast<unsigned char>(text[i]);

            if ((ch & 0x80) == 0)
            {
                i += 1;
            }
            else if ((ch & 0xE0) == 0xC0)
            {
                if (i + 1 >= text.size())
                    return false;

                i += 2;
            }
            else if ((ch & 0xF0) == 0xE0)
            {
                if (i + 2 >= text.size())
                    return false;

                i += 3;
            }
            else if ((ch & 0xF8) == 0xF0)
            {
                if (i + 3 >= text.size())
                    return false;

                i += 4;
            }
            else
            {
                return false;
            }

            ++length;
        }

        return true;
    }

    static bool IsValidShopName(std::string const& shopName)
    {
        if (shopName.empty())
            return false;

        for (char ch : shopName)
        {
            if (ch == '\n' || ch == '\r' || ch == '\t')
                return false;
        }

        size_t length = 0;

        if (!GetUtf8Length(shopName, length))
            return false;

        if (length > 5)
            return false;

        return true;
    }

    static bool ParseRegPointPrice(std::string const& text, uint32& price)
    {
        if (text.empty())
            return false;

        uint64 value = 0;

        for (char ch : text)
        {
            if (!std::isdigit(static_cast<unsigned char>(ch)))
                return false;

            value = (value * 10) + uint64(ch - '0');

            if (value > 4000000000ULL)
                return false;
        }

        if (value == 0)
            return false;

        price = uint32(value);
        return true;
    }

    static std::string FormatCooldown(uint32 seconds)
    {
        uint32 days = seconds / 86400;
        uint32 hours = (seconds % 86400) / 3600;

        if (days > 0)
            return std::to_string(days) + "일 " + std::to_string(hours) + "시간";

        if (hours > 0)
            return std::to_string(hours) + "시간";

        return "1시간 미만";
    }

    static std::string GetItemDisplayName(Item* item)
    {
        if (!item || !item->GetTemplate())
            return "알 수 없는 아이템";

        std::string name = item->GetTemplate()->Name1;

        if (name.empty())
            name = "알 수 없는 아이템";

        if (item->GetCount() > 1)
            name += " x" + std::to_string(item->GetCount());

        return name;
    }

    static bool LoadShopState(Player* player, ShopState& state)
    {
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT id, is_open, slot_limit, "
            "IF(reopen_available_at IS NULL OR reopen_available_at <= NOW(), 0, TIMESTAMPDIFF(SECOND, NOW(), reopen_available_at)) "
            "FROM appdata.personal_shops "
            "WHERE account_id = {} "
            "LIMIT 1",
            GetAccountId(player)
        );

        if (!result)
            return false;

        Field* fields = result->Fetch();

        state.Id = fields[0].GetUInt64();
        state.IsOpen = fields[1].GetUInt8() != 0;
        state.SlotLimit = fields[2].GetUInt32();
        state.ReopenCooldownSeconds = fields[3].GetUInt32();

        return true;
    }

    static bool HasOpenShop(Player* player)
    {
        ShopState state;
        return LoadShopState(player, state) && state.IsOpen;
    }

    static uint32 GetListedItemCount(Player* player)
    {
        ShopState state;

        if (!LoadShopState(player, state))
            return 0;

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT COUNT(*) "
            "FROM appdata.personal_shop_items "
            "WHERE shop_id = {}",
            state.Id
        );

        if (!result)
            return 0;

        return result->Fetch()[0].GetUInt32();
    }

    static bool HasActiveShopItems(Player* player)
    {
        return GetListedItemCount(player) > 0;
    }

    static bool IsItemAlreadyListed(uint64 itemGuid)
	{
		QueryResult result = CharacterDatabase.PQuery(
			"SELECT id "
			"FROM appdata.personal_shop_items "
			"WHERE item_guid = {} "
			"AND sold_out = 0 "
			"LIMIT 1",
			itemGuid
		);

		return result != nullptr;
	}

    static uint32 GetAccountRegPoint(Player* player)
    {
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT reg_point "
            "FROM appdata.account_data "
            "WHERE id = {} "
            "LIMIT 1",
            GetAccountId(player)
        );

        if (!result)
            return 0;

        return result->Fetch()[0].GetUInt32();
    }

    static bool ConsumeAccountRegPoint(Player* player, uint32 amount)
    {
        uint32 accountId = GetAccountId(player);
        uint32 currentRp = GetAccountRegPoint(player);

        if (currentRp < amount)
            return false;

        CharacterDatabase.PExecute(
            "UPDATE appdata.account_data "
            "SET reg_point = reg_point - {} "
            "WHERE id = {} "
            "AND reg_point >= {} "
            "LIMIT 1",
            amount,
            accountId,
            amount
        );

        return true;
    }

    static bool CreateOrReopenShop(Player* player, std::string shopName)
    {
        TrimText(shopName);

        if (!IsValidShopName(shopName))
        {
            Notify(player, "상점 이름은 5글자 이하로 입력해야 합니다.");
            return false;
        }

        uint32 accountId = GetAccountId(player);
        uint64 ownerGuid = GetOwnerGuid(player);

        CharacterDatabase.EscapeString(shopName);

        ShopState state;

        if (LoadShopState(player, state))
        {
            if (state.IsOpen)
            {
                Notify(player, "이미 개인상점이 개설되어 있습니다.");
                return false;
            }

            if (state.ReopenCooldownSeconds > 0)
            {
                Notify(player, "첫 개설 후 15일 이내에는 재개설할 수 없습니다. 남은 시간: " + FormatCooldown(state.ReopenCooldownSeconds));
                return false;
            }

            CharacterDatabase.PExecute(
                "UPDATE appdata.personal_shops "
                "SET owner_guid = {}, "
                "shop_name = '{}', "
                "is_open = 1, "
                "updated_at = NOW() "
                "WHERE id = {} "
                "AND account_id = {} "
                "LIMIT 1",
                ownerGuid,
                shopName,
                state.Id,
                accountId
            );

            Notify(player, "개인상점이 재개설되었습니다.");
            return true;
        }

        CharacterDatabase.PExecute(
            "INSERT INTO appdata.personal_shops "
            "(account_id, owner_guid, shop_name, shop_description, slot_limit, is_open, reopen_available_at, created_at, updated_at) "
            "VALUES "
            "({}, {}, '{}', '아직 상점 소개가 등록되지 않았습니다.', {}, 1, DATE_ADD(NOW(), INTERVAL 15 DAY), NOW(), NOW())",
            accountId,
            ownerGuid,
            shopName,
            DEFAULT_SLOT_LIMIT
        );

        Notify(player, "개인상점이 개설되었습니다. 기본 판매 슬롯 10개가 지급되었습니다.");
        return true;
    }

    static bool CloseShop(Player* player)
    {
        ShopState state;

        if (!LoadShopState(player, state) || !state.IsOpen)
        {
            Notify(player, "폐쇄할 개인상점이 없습니다.");
            return false;
        }

        if (HasActiveShopItems(player))
        {
            Notify(player, "등록된 아이템이 있어 상점을 폐쇄할 수 없습니다. 홈페이지에서 먼저 정리해 주세요.");
            return false;
        }

        CharacterDatabase.PExecute(
            "UPDATE appdata.personal_shops "
            "SET is_open = 0, "
            "closed_at = NOW(), "
            "updated_at = NOW() "
            "WHERE id = {} "
            "AND account_id = {} "
            "LIMIT 1",
            state.Id,
            GetAccountId(player)
        );

        Notify(player, "개인상점이 폐쇄되었습니다.");
        return true;
    }

    static bool ExpandSlot(Player* player)
    {
        ShopState state;

        if (!LoadShopState(player, state) || !state.IsOpen)
        {
            Notify(player, "개인상점을 먼저 개설해야 합니다.");
            return false;
        }

        if (!ConsumeAccountRegPoint(player, SLOT_EXPAND_PRICE_RP))
        {
            Notify(player, "RP가 부족합니다.");
            return false;
        }

        CharacterDatabase.PExecute(
            "UPDATE appdata.personal_shops "
            "SET slot_limit = slot_limit + {}, "
            "updated_at = NOW() "
            "WHERE id = {} "
            "AND account_id = {} "
            "AND is_open = 1 "
            "LIMIT 1",
            SLOT_EXPAND_AMOUNT,
            state.Id,
            GetAccountId(player)
        );

        Notify(player, "개인상점 판매 슬롯이 10개 확장되었습니다.");
        return true;
    }

    static bool RegisterPendingItem(Player* player)
    {
        uint64 pendingKey = GetPendingKey(player);

        auto itr = PendingRegisterItems.find(pendingKey);

        if (itr == PendingRegisterItems.end())
        {
            Notify(player, "등록 대기중인 아이템이 없습니다.");
            return false;
        }

        PendingRegisterItem pending = itr->second;

        ShopState state;

        if (!LoadShopState(player, state) || !state.IsOpen)
        {
            PendingRegisterItems.erase(pendingKey);
            Notify(player, "개인상점을 먼저 개설해야 합니다.");
            return false;
        }

        uint32 listedCount = GetListedItemCount(player);

        if (listedCount >= state.SlotLimit)
        {
            PendingRegisterItems.erase(pendingKey);
            Notify(player, "판매 슬롯이 부족합니다.");
            return false;
        }

        Item* item = player->GetItemByPos(pending.Bag, pending.Slot);

        if (!item)
        {
            PendingRegisterItems.erase(pendingKey);
            Notify(player, "아이템을 찾을 수 없습니다.");
            return false;
        }

        uint64 itemGuid = item->GetGUID().GetCounter();
        uint32 itemEntry = item->GetEntry();

        if (itemGuid != pending.ItemGuid)
        {
            PendingRegisterItems.erase(pendingKey);
            Notify(player, "선택한 아이템 정보가 변경되었습니다. 다시 등록해 주세요.");
            return false;
        }

        if (IsItemAlreadyListed(itemGuid))
        {
            PendingRegisterItems.erase(pendingKey);
            Notify(player, "이미 개인상점에 등록된 아이템입니다.");
            return false;
        }

        CharacterDatabase.PExecute(
            "INSERT INTO appdata.personal_shop_items "
            "(shop_id, seller_account_id, owner_guid, item_guid, item_entry, price_reg_point) "
            "VALUES "
            "({}, {}, {}, {}, {}, {})",
            state.Id,
            GetAccountId(player),
            GetOwnerGuid(player),
            itemGuid,
            itemEntry,
            pending.PriceRegPoint
        );

        /*
         * 개인상점 등록 기준
         * - item_instance 는 그대로 둔다.
         * - character_inventory 에서만 제거한다.
         * - personal_shop_items 에 item_guid 만 기록한다.
         */
        player->MoveItemFromInventory(pending.Bag, pending.Slot, true);

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        player->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);

        std::string deleteSql =
            "DELETE FROM character_inventory "
            "WHERE item = " + std::to_string(itemGuid) + " "
            "LIMIT 1";

        CharacterDatabase.DirectExecute(deleteSql.c_str());

        PendingRegisterItems.erase(pendingKey);

        Notify(player, "아이템이 개인상점에 등록되었습니다.");
        return true;
    }

    static void SendMainMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        ShopState state;

        if (!LoadShopState(player, state) || !state.IsOpen)
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "개인상점 개설",
                GOSSIP_SENDER_MAIN,
                ACTION_CREATE_SHOP_CODE,
                "개설할 개인상점 이름을 입력하세요. 최대 5글자입니다.",
                0,
                true
            );
        }
        else
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "아이템 등록",
                GOSSIP_SENDER_MAIN,
                ACTION_ITEM_MANAGE
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "상점 폐쇄",
                GOSSIP_SENDER_MAIN,
                ACTION_CLOSE_SHOP
            );
        }

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    static void SendItemManageMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        ShopState state;
        LoadShopState(player, state);

        uint32 listedCount = GetListedItemCount(player);

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "판매 슬롯: " + std::to_string(listedCount) + " / " + std::to_string(state.SlotLimit),
            GOSSIP_SENDER_MAIN,
            ACTION_BACK_ITEM_MANAGE
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "아이템 등록",
            GOSSIP_SENDER_MAIN,
            ACTION_REGISTER_ITEM_LIST
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "판매 슬롯 10개 확장 - " + std::to_string(SLOT_EXPAND_PRICE_RP) + " RP",
            GOSSIP_SENDER_MAIN,
            ACTION_EXPAND_SLOT
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "돌아가기",
            GOSSIP_SENDER_MAIN,
            ACTION_BACK_MAIN
        );

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    static void SendRegisterItemList(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        ShopState state;

        if (!LoadShopState(player, state) || !state.IsOpen)
        {
            Notify(player, "개인상점을 먼저 개설해야 합니다.");
            SendMainMenu(player, creature);
            return;
        }

        uint32 listedCount = GetListedItemCount(player);

        if (listedCount >= state.SlotLimit)
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "판매 슬롯이 부족합니다.",
                GOSSIP_SENDER_MAIN,
                ACTION_BACK_ITEM_MANAGE
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "돌아가기",
                GOSSIP_SENDER_MAIN,
                ACTION_BACK_ITEM_MANAGE
            );

            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
            return;
        }

        bool hasItem = false;

        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        {
            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

            if (!item)
                continue;

            uint64 itemGuid = item->GetGUID().GetCounter();

            if (IsItemAlreadyListed(itemGuid))
                continue;

            hasItem = true;

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                GetItemDisplayName(item),
                GOSSIP_SENDER_MAIN,
                ACTION_REGISTER_ITEM_PRICE_BASE + uint32(slot),
                "판매 가격을 RP로 입력하세요.",
                0,
                true
            );
        }

        if (!hasItem)
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "기본 인벤토리에 등록 가능한 아이템이 없습니다.",
                GOSSIP_SENDER_MAIN,
                ACTION_BACK_ITEM_MANAGE
            );
        }

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "돌아가기",
            GOSSIP_SENDER_MAIN,
            ACTION_BACK_ITEM_MANAGE
        );

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    static void SendRegisterConfirmMenu(Player* player, Creature* creature, uint8 slot, uint32 price)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);

        if (!item)
        {
            Notify(player, "아이템을 찾을 수 없습니다.");
            SendRegisterItemList(player, creature);
            return;
        }

        PendingRegisterItem pending;
        pending.Bag = INVENTORY_SLOT_BAG_0;
        pending.Slot = slot;
        pending.ItemGuid = item->GetGUID().GetCounter();
        pending.PriceRegPoint = price;

        PendingRegisterItems[GetPendingKey(player)] = pending;

        ClearGossipMenuFor(player);

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "아이템: " + GetItemDisplayName(item),
            GOSSIP_SENDER_MAIN,
            ACTION_CANCEL_REGISTER_ITEM
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "가격: " + std::to_string(price) + " RP",
            GOSSIP_SENDER_MAIN,
            ACTION_CANCEL_REGISTER_ITEM
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "이 아이템을 개인상점에 등록합니다.",
            GOSSIP_SENDER_MAIN,
            ACTION_CONFIRM_REGISTER_ITEM
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "취소",
            GOSSIP_SENDER_MAIN,
            ACTION_CANCEL_REGISTER_ITEM
        );

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    static void SendCloseConfirmMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "정말 개인상점을 폐쇄합니다.",
            GOSSIP_SENDER_MAIN,
            ACTION_CONFIRM_CLOSE_SHOP
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "돌아가기",
            GOSSIP_SENDER_MAIN,
            ACTION_BACK_MAIN
        );

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    static void SendExpandConfirmMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "판매 슬롯 10개를 " + std::to_string(SLOT_EXPAND_PRICE_RP) + " RP로 확장합니다.",
            GOSSIP_SENDER_MAIN,
            ACTION_CONFIRM_EXPAND_SLOT
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "돌아가기",
            GOSSIP_SENDER_MAIN,
            ACTION_BACK_ITEM_MANAGE
        );

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }
}

class npc_personal_shop_manager : public CreatureScript
{
public:
    npc_personal_shop_manager() : CreatureScript("npc_personal_shop_manager") { }

    struct npc_personal_shop_managerAI : public ScriptedAI
    {
        npc_personal_shop_managerAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            PersonalShop::SendMainMenu(player, me);
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

            switch (action)
            {
                case PersonalShop::ACTION_ITEM_MANAGE:
                {
                    if (!PersonalShop::HasOpenShop(player))
                    {
                        PersonalShop::Notify(player, "개인상점을 먼저 개설해야 합니다.");
                        PersonalShop::SendMainMenu(player, me);
                        break;
                    }

                    PersonalShop::SendItemManageMenu(player, me);
                    break;
                }

                case PersonalShop::ACTION_REGISTER_ITEM_LIST:
                {
                    PersonalShop::SendRegisterItemList(player, me);
                    break;
                }

                case PersonalShop::ACTION_CONFIRM_REGISTER_ITEM:
                {
                    PersonalShop::RegisterPendingItem(player);
                    PersonalShop::SendItemManageMenu(player, me);
                    break;
                }

                case PersonalShop::ACTION_CANCEL_REGISTER_ITEM:
                {
                    PersonalShop::PendingRegisterItems.erase(PersonalShop::GetPendingKey(player));
                    PersonalShop::SendRegisterItemList(player, me);
                    break;
                }

                case PersonalShop::ACTION_CLOSE_SHOP:
                {
                    if (!PersonalShop::HasOpenShop(player))
                    {
                        PersonalShop::Notify(player, "폐쇄할 개인상점이 없습니다.");
                        PersonalShop::SendMainMenu(player, me);
                        break;
                    }

                    PersonalShop::SendCloseConfirmMenu(player, me);
                    break;
                }

                case PersonalShop::ACTION_CONFIRM_CLOSE_SHOP:
                {
                    PersonalShop::CloseShop(player);
                    PersonalShop::SendMainMenu(player, me);
                    break;
                }

                case PersonalShop::ACTION_EXPAND_SLOT:
                {
                    PersonalShop::SendExpandConfirmMenu(player, me);
                    break;
                }

                case PersonalShop::ACTION_CONFIRM_EXPAND_SLOT:
                {
                    PersonalShop::ExpandSlot(player);
                    PersonalShop::SendItemManageMenu(player, me);
                    break;
                }

                case PersonalShop::ACTION_BACK_ITEM_MANAGE:
                {
                    PersonalShop::SendItemManageMenu(player, me);
                    break;
                }

                case PersonalShop::ACTION_BACK_MAIN:
                default:
                {
                    PersonalShop::SendMainMenu(player, me);
                    break;
                }
            }

            return true;
        }

        bool OnGossipSelectCode(Player* player, uint32 /*menuId*/, uint32 gossipListId, char const* code) override
        {
            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

            if (action == PersonalShop::ACTION_CREATE_SHOP_CODE)
            {
                PersonalShop::CreateOrReopenShop(player, code ? std::string(code) : "");
                PersonalShop::SendMainMenu(player, me);
                return true;
            }

            if (PersonalShop::IsPriceInputAction(action))
            {
                uint32 price = 0;

                if (!PersonalShop::ParseRegPointPrice(code ? std::string(code) : "", price))
                {
                    PersonalShop::Notify(player, "가격은 숫자로만 입력해야 합니다.");
                    PersonalShop::SendRegisterItemList(player, me);
                    return true;
                }

                uint8 slot = PersonalShop::GetSlotFromPriceInputAction(action);
                PersonalShop::SendRegisterConfirmMenu(player, me, slot, price);
                return true;
            }

            PersonalShop::SendMainMenu(player, me);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_personal_shop_managerAI(creature);
    }
};

void AddSC_npc_personal_shop_manager()
{
    new npc_personal_shop_manager();
}