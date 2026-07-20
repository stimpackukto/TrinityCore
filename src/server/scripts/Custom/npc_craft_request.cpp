#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "Creature.h"
#include "GossipDef.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Mail.h"
#include "WorldSession.h"
#include "StringFormat.h"

#include <ctime>
#include <limits>
#include <string>
#include <vector>

namespace
{
    enum CraftRequestGossip
    {
        CRAFT_REQUEST_GOSSIP_MENU = 900700,

        CRAFT_ACTION_BACK = GOSSIP_ACTION_INFO_DEF + 100,
        CRAFT_ACTION_CLOSE = GOSSIP_ACTION_INFO_DEF + 101,

        CRAFT_ACTION_CATEGORY_BASE = GOSSIP_ACTION_INFO_DEF + 1000,
        CRAFT_ACTION_ITEM_BASE = GOSSIP_ACTION_INFO_DEF + 100000,
        CRAFT_ACTION_CONFIRM_BASE = GOSSIP_ACTION_INFO_DEF + 1000000
    };

    constexpr uint32 ORDER_PROCESS_INTERVAL_MS = 60 * 1000;
    constexpr uint64 COPPER_PER_GOLD = 10000;
    constexpr uint64 DAY_SECONDS = 86400;

    struct CraftRequestCategory
    {
        uint32 MenuIndex = 0;
        std::string Category;
        uint32 ItemCount = 0;
    };

    struct CraftRequestItem
    {
        uint32 ItemEntry = 0;
        std::string Category;
        std::string ItemName;
        uint32 ItemCount = 1;
        uint64 GoldCost = 0;
        uint32 DelayDays = 7;
    };

    struct PendingCraftOrder
    {
        bool HasOrder = false;
        uint32 ItemEntry = 0;
        uint32 ItemCount = 0;
        uint64 GoldCost = 0;
        uint32 DeliverTime = 0;
    };

    std::string MoneyToText(uint64 copper)
    {
        uint64 gold = copper / COPPER_PER_GOLD;
        return Trinity::StringFormat("{}골", gold);
    }

    std::string SecondsToText(uint32 seconds)
    {
        if (seconds == 0)
            return "곧 발송 예정";

        uint32 days = seconds / uint32(DAY_SECONDS);
        uint32 hours = (seconds % uint32(DAY_SECONDS)) / 3600;
        uint32 minutes = (seconds % 3600) / 60;

        if (days > 0)
            return Trinity::StringFormat("{}일 {}시간", days, hours);

        if (hours > 0)
            return Trinity::StringFormat("{}시간 {}분", hours, minutes);

        if (minutes > 0)
            return Trinity::StringFormat("{}분", minutes);

        return "1분 미만";
    }

    std::string GetDisplayItemName(uint32 itemEntry, std::string const& fixedName)
    {
        if (!fixedName.empty())
            return fixedName;

        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemEntry);

        if (itemTemplate && !itemTemplate->Name1.empty())
            return itemTemplate->Name1;

        return Trinity::StringFormat("아이템 #{}", itemEntry);
    }

    std::vector<CraftRequestCategory> LoadCraftRequestCategories()
    {
        std::vector<CraftRequestCategory> categories;

        QueryResult result = LoginDatabase.Query(
            "SELECT "
            "IFNULL(NULLIF(category, ''), '기타') AS category_name, "
            "COUNT(*) AS item_count, "
            "MIN(display_order) AS sort_order "
            "FROM appdata.craft_request_items "
            "WHERE enabled = 1 "
            "GROUP BY IFNULL(NULLIF(category, ''), '기타') "
            "ORDER BY sort_order ASC, category_name ASC"
        );

        if (!result)
            return categories;

        uint32 index = 1;

        do
        {
            Field* fields = result->Fetch();

            CraftRequestCategory category;
            category.MenuIndex = index++;
            category.Category = fields[0].GetString();
            category.ItemCount = fields[1].GetUInt32();

            if (category.Category.empty())
                category.Category = "기타";

            categories.push_back(category);
        }
        while (result->NextRow());

        return categories;
    }

    bool FindCraftRequestCategory(uint32 menuIndex, std::string& category)
    {
        std::vector<CraftRequestCategory> categories = LoadCraftRequestCategories();

        for (CraftRequestCategory const& row : categories)
        {
            if (row.MenuIndex == menuIndex)
            {
                category = row.Category;
                return !category.empty();
            }
        }

        return false;
    }

    std::vector<CraftRequestItem> LoadCraftRequestItemsByCategory(std::string const& category)
    {
        std::vector<CraftRequestItem> items;

        std::string escapedCategory = category;
        LoginDatabase.EscapeString(escapedCategory);

        QueryResult result = LoginDatabase.PQuery(
            "SELECT item_entry, IFNULL(NULLIF(category, ''), '기타'), item_name, item_count, gold_cost, delay_days "
            "FROM appdata.craft_request_items "
            "WHERE enabled = 1 "
            "AND IFNULL(NULLIF(category, ''), '기타') = '{}' "
            "ORDER BY display_order ASC, item_entry ASC",
            escapedCategory
        );

        if (!result)
            return items;

        do
        {
            Field* fields = result->Fetch();

            CraftRequestItem item;
            item.ItemEntry = fields[0].GetUInt32();
            item.Category = fields[1].GetString();
            item.ItemName = fields[2].GetString();
            item.ItemCount = fields[3].GetUInt32();
            item.GoldCost = fields[4].GetUInt64();
            item.DelayDays = fields[5].GetUInt32();

            if (item.ItemEntry == 0)
                continue;

            if (item.ItemCount == 0)
                item.ItemCount = 1;

            if (item.DelayDays == 0)
                item.DelayDays = 7;

            if (!sObjectMgr->GetItemTemplate(item.ItemEntry))
                continue;

            items.push_back(item);
        }
        while (result->NextRow());

        return items;
    }

    bool FindCraftRequestItem(uint32 itemEntry, CraftRequestItem& item, bool enabledOnly = true)
    {
        std::string enabledSql = enabledOnly ? "AND enabled = 1 " : "";

        QueryResult result = LoginDatabase.PQuery(
            "SELECT item_entry, IFNULL(NULLIF(category, ''), '기타'), item_name, item_count, gold_cost, delay_days "
            "FROM appdata.craft_request_items "
            "WHERE item_entry = {} "
            "{}"
            "LIMIT 1",
            itemEntry,
            enabledSql
        );

        if (!result)
            return false;

        Field* fields = result->Fetch();

        item.ItemEntry = fields[0].GetUInt32();
        item.Category = fields[1].GetString();
        item.ItemName = fields[2].GetString();
        item.ItemCount = fields[3].GetUInt32();
        item.GoldCost = fields[4].GetUInt64();
        item.DelayDays = fields[5].GetUInt32();

        if (item.ItemCount == 0)
            item.ItemCount = 1;

        if (item.DelayDays == 0)
            item.DelayDays = 7;

        return item.ItemEntry > 0 && sObjectMgr->GetItemTemplate(item.ItemEntry);
    }

    PendingCraftOrder GetPendingOrderForCharacter(uint32 characterGuid)
    {
        PendingCraftOrder order;

        QueryResult result = LoginDatabase.PQuery(
            "SELECT item_entry, item_count, gold_cost, deliver_time "
            "FROM appdata.craft_request_orders "
            "WHERE character_guid = {} "
            "AND delivered = 0 "
            "ORDER BY deliver_time ASC, id ASC "
            "LIMIT 1",
            characterGuid
        );

        if (!result)
            return order;

        Field* fields = result->Fetch();

        order.HasOrder = true;
        order.ItemEntry = fields[0].GetUInt32();
        order.ItemCount = fields[1].GetUInt32();
        order.GoldCost = fields[2].GetUInt64();
        order.DeliverTime = fields[3].GetUInt32();

        if (order.ItemCount == 0)
            order.ItemCount = 1;

        return order;
    }

    void ShowPendingOrderMenu(Player* player, Creature* creature, PendingCraftOrder const& order)
    {
        CraftRequestItem requestItem;
        std::string itemName;

        if (FindCraftRequestItem(order.ItemEntry, requestItem, false))
            itemName = GetDisplayItemName(order.ItemEntry, requestItem.ItemName);
        else
            itemName = GetDisplayItemName(order.ItemEntry, "");

        uint32 now = uint32(std::time(nullptr));
        uint32 remainSeconds = 0;

        if (order.DeliverTime > now)
            remainSeconds = order.DeliverTime - now;

        InitGossipMenuFor(player, CRAFT_REQUEST_GOSSIP_MENU);

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            Trinity::StringFormat("현재 이 캐릭터가 제작 중: {} x{}", itemName, order.ItemCount),
            GOSSIP_SENDER_MAIN,
            CRAFT_ACTION_CLOSE
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            Trinity::StringFormat("남은 시간: {}", SecondsToText(remainSeconds)),
            GOSSIP_SENDER_MAIN,
            CRAFT_ACTION_CLOSE
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "제작 완료 후 우편 발송이 끝나야 이 캐릭터로 다음 의뢰가 가능합니다.",
            GOSSIP_SENDER_MAIN,
            CRAFT_ACTION_CLOSE
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "닫기",
            GOSSIP_SENDER_MAIN,
            CRAFT_ACTION_CLOSE
        );

        player->TalkedToCreature(creature->GetEntry(), creature->GetGUID());
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    bool HasPendingOrderAndShow(Player* player, Creature* creature)
    {
        uint32 characterGuid = player->GetGUID().GetCounter();
        PendingCraftOrder pendingOrder = GetPendingOrderForCharacter(characterGuid);

        if (!pendingOrder.HasOrder)
            return false;

        ShowPendingOrderMenu(player, creature, pendingOrder);
        return true;
    }

    void ShowCategoryMenu(Player* player, Creature* creature)
    {
        if (HasPendingOrderAndShow(player, creature))
            return;

        std::vector<CraftRequestCategory> categories = LoadCraftRequestCategories();

        InitGossipMenuFor(player, CRAFT_REQUEST_GOSSIP_MENU);

        if (categories.empty())
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "현재 의뢰 가능한 제작품이 없습니다.",
                GOSSIP_SENDER_MAIN,
                CRAFT_ACTION_CLOSE
            );
        }
        else
        {
            for (CraftRequestCategory const& category : categories)
            {
                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    Trinity::StringFormat("{}", category.Category),
                    GOSSIP_SENDER_MAIN,
                    CRAFT_ACTION_CATEGORY_BASE + category.MenuIndex
                );
            }
        }

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "닫기",
            GOSSIP_SENDER_MAIN,
            CRAFT_ACTION_CLOSE
        );

        player->TalkedToCreature(creature->GetEntry(), creature->GetGUID());
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowItemMenu(Player* player, Creature* creature, std::string const& category)
    {
        if (HasPendingOrderAndShow(player, creature))
            return;

        std::vector<CraftRequestItem> items = LoadCraftRequestItemsByCategory(category);

        InitGossipMenuFor(player, CRAFT_REQUEST_GOSSIP_MENU);

        if (items.empty())
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "이 분류에는 현재 의뢰 가능한 제작품이 없습니다.",
                GOSSIP_SENDER_MAIN,
                CRAFT_ACTION_BACK
            );
        }
        else
        {
            for (CraftRequestItem const& item : items)
            {
                std::string itemName = GetDisplayItemName(item.ItemEntry, item.ItemName);

                std::string text = Trinity::StringFormat(
                    "{} x{} / {}",
                    itemName,
                    item.ItemCount,
                    MoneyToText(item.GoldCost),
                    item.DelayDays
                );

                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_VENDOR,
                    text,
                    GOSSIP_SENDER_MAIN,
                    CRAFT_ACTION_ITEM_BASE + item.ItemEntry
                );
            }
        }

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "분류 목록으로 돌아가기",
            GOSSIP_SENDER_MAIN,
            CRAFT_ACTION_BACK
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "닫기",
            GOSSIP_SENDER_MAIN,
            CRAFT_ACTION_CLOSE
        );

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowConfirmMenu(Player* player, Creature* creature, uint32 itemEntry)
    {
        if (HasPendingOrderAndShow(player, creature))
            return;

        CraftRequestItem item;

        if (!FindCraftRequestItem(itemEntry, item, true))
        {
            InitGossipMenuFor(player, CRAFT_REQUEST_GOSSIP_MENU);

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                "의뢰할 수 없는 제작품입니다.",
                GOSSIP_SENDER_MAIN,
                CRAFT_ACTION_BACK
            );

            SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
            return;
        }

        std::string itemName = GetDisplayItemName(item.ItemEntry, item.ItemName);

        InitGossipMenuFor(player, CRAFT_REQUEST_GOSSIP_MENU);

        AddGossipItemFor(
            player,
            GOSSIP_ICON_VENDOR,
            Trinity::StringFormat("의뢰 확정: [{}] {} x{} / {}", item.Category, itemName, item.ItemCount, MoneyToText(item.GoldCost)),
            GOSSIP_SENDER_MAIN,
            CRAFT_ACTION_CONFIRM_BASE + item.ItemEntry
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            Trinity::StringFormat("{}일 뒤 우편으로 발송됩니다.", item.DelayDays),
            GOSSIP_SENDER_MAIN,
            CRAFT_ACTION_BACK
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "분류 목록으로 돌아가기",
            GOSSIP_SENDER_MAIN,
            CRAFT_ACTION_BACK
        );

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void ShowResult(Player* player, Creature* creature, std::string const& message)
    {
        InitGossipMenuFor(player, CRAFT_REQUEST_GOSSIP_MENU);

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            message,
            GOSSIP_SENDER_MAIN,
            CRAFT_ACTION_BACK
        );

        AddGossipItemFor(
            player,
            GOSSIP_ICON_CHAT,
            "분류 목록으로 돌아가기",
            GOSSIP_SENDER_MAIN,
            CRAFT_ACTION_BACK
        );

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
    }

    void CreateCraftRequestOrder(Player* player, Creature* creature, uint32 itemEntry)
    {
        if (HasPendingOrderAndShow(player, creature))
            return;

        CraftRequestItem item;

        if (!FindCraftRequestItem(itemEntry, item, true))
        {
            ShowResult(player, creature, "의뢰할 수 없는 제작품입니다.");
            return;
        }

        if (item.GoldCost > uint64(std::numeric_limits<int32>::max()))
        {
            ShowResult(player, creature, "의뢰 가격이 너무 높게 설정되어 있습니다.");
            return;
        }

        if (uint64(player->GetMoney()) < item.GoldCost)
        {
            ShowResult(player, creature, Trinity::StringFormat("골드가 부족합니다. 필요 금액: {}", MoneyToText(item.GoldCost)));
            return;
        }

        uint32 accountId = player->GetSession()->GetAccountId();
        uint32 characterGuid = player->GetGUID().GetCounter();

        uint32 now = uint32(std::time(nullptr));
        uint32 deliverTime = now + uint32(uint64(item.DelayDays) * DAY_SECONDS);

        player->ModifyMoney(-int32(item.GoldCost));

        LoginDatabase.PExecute(
            "INSERT INTO appdata.craft_request_orders "
            "(account_id, character_guid, item_entry, item_count, gold_cost, request_time, deliver_time, delivered, delivered_time) "
            "VALUES "
            "({}, {}, {}, {}, {}, {}, {}, 0, 0)",
            accountId,
            characterGuid,
            item.ItemEntry,
            item.ItemCount,
            item.GoldCost,
            now,
            deliverTime
        );

        std::string itemName = GetDisplayItemName(item.ItemEntry, item.ItemName);

        ShowResult(
            player,
            creature,
            Trinity::StringFormat(
                "[{}] {} x{} 제작 의뢰가 접수되었습니다. {}일 뒤 우편으로 발송됩니다.",
                item.Category,
                itemName,
                item.ItemCount,
                item.DelayDays
            )
        );
    }

    bool SendCraftRequestMail(uint32 characterGuid, uint32 itemEntry, uint32 itemCount)
    {
        if (characterGuid == 0 || itemEntry == 0 || itemCount == 0)
            return false;

        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemEntry);

        if (!itemTemplate)
            return false;

        CraftRequestItem requestItem;

        if (!FindCraftRequestItem(itemEntry, requestItem, false))
        {
            requestItem.ItemEntry = itemEntry;
            requestItem.Category = "기타";
            requestItem.ItemName = "";
            requestItem.ItemCount = itemCount;
            requestItem.GoldCost = 0;
            requestItem.DelayDays = 7;
        }

        ObjectGuid receiverGuid = ObjectGuid::Create<HighGuid::Player>(characterGuid);

        Item* mailItem = Item::CreateItem(itemEntry, itemCount, nullptr);

        if (!mailItem)
            return false;

        mailItem->SetOwnerGUID(receiverGuid);

        std::string itemName = GetDisplayItemName(itemEntry, requestItem.ItemName);

        std::string subject = Trinity::StringFormat("[공방 의뢰] {} 제작 완료", itemName);

        std::string body =
            "달라란 공방에서 의뢰하신 제작품이 완성되었습니다.\n\n"
            "전문기술 성장에 필요한 제작품을 동봉합니다.\n"
            "다음 의뢰도 공방 의뢰관을 통해 신청할 수 있습니다.";

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        mailItem->SaveToDB(trans);

        MailDraft draft(subject, body);
        draft.AddItem(mailItem);

        draft.SendMailTo(
            trans,
            MailReceiver(nullptr, characterGuid),
            MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM)
        );

        CharacterDatabase.CommitTransaction(trans);

        return true;
    }

    void ProcessReadyOrders()
    {
        uint32 now = uint32(std::time(nullptr));

        QueryResult result = LoginDatabase.PQuery(
            "SELECT id, character_guid, item_entry, item_count "
            "FROM appdata.craft_request_orders "
            "WHERE delivered = 0 "
            "AND deliver_time <= {} "
            "ORDER BY id ASC "
            "LIMIT 20",
            now
        );

        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();

            uint64 orderId = fields[0].GetUInt64();
            uint32 characterGuid = fields[1].GetUInt32();
            uint32 itemEntry = fields[2].GetUInt32();
            uint32 itemCount = fields[3].GetUInt32();

            bool sent = SendCraftRequestMail(characterGuid, itemEntry, itemCount);

            LoginDatabase.PExecute(
                "UPDATE appdata.craft_request_orders "
                "SET delivered = {}, delivered_time = {} "
                "WHERE id = {} "
                "AND delivered = 0 "
                "LIMIT 1",
                sent ? 1 : 2,
                now,
                orderId
            );
        }
        while (result->NextRow());
    }
}

class npc_craft_request : public CreatureScript
{
public:
    npc_craft_request() : CreatureScript("npc_craft_request") { }

    struct npc_craft_requestAI : public ScriptedAI
    {
        npc_craft_requestAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            ShowCategoryMenu(player, me);
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            if (action == CRAFT_ACTION_CLOSE)
            {
                CloseGossipMenuFor(player);
                return true;
            }

            if (action == CRAFT_ACTION_BACK)
            {
                ShowCategoryMenu(player, me);
                return true;
            }

            if (action >= CRAFT_ACTION_CONFIRM_BASE)
            {
                uint32 itemEntry = action - CRAFT_ACTION_CONFIRM_BASE;
                CreateCraftRequestOrder(player, me, itemEntry);
                return true;
            }

            if (action >= CRAFT_ACTION_ITEM_BASE)
            {
                uint32 itemEntry = action - CRAFT_ACTION_ITEM_BASE;
                ShowConfirmMenu(player, me, itemEntry);
                return true;
            }

            if (action >= CRAFT_ACTION_CATEGORY_BASE)
            {
                uint32 categoryIndex = action - CRAFT_ACTION_CATEGORY_BASE;
                std::string category;

                if (FindCraftRequestCategory(categoryIndex, category))
                    ShowItemMenu(player, me, category);
                else
                    ShowCategoryMenu(player, me);

                return true;
            }

            ShowCategoryMenu(player, me);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_craft_requestAI(creature);
    }
};

class npc_craft_request_world : public WorldScript
{
public:
    npc_craft_request_world() : WorldScript("npc_craft_request_world") { }

    void OnUpdate(uint32 diff) override
    {
        if (_timer <= diff)
        {
            ProcessReadyOrders();
            _timer = ORDER_PROCESS_INTERVAL_MS;
            return;
        }

        _timer -= diff;
    }

private:
    uint32 _timer = ORDER_PROCESS_INTERVAL_MS;
};

void AddSC_npc_craft_request()
{
    new npc_craft_request();
    new npc_craft_request_world();
}