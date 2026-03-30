// Custom/bg_wrathful_set_npc.cpp
// 분노한 검투사 PvP 전장 세트 지급 NPC
// - DPS / HEAL 세트 지급
// - 이미 보유한 아이템 제외
// - 지급 시 메모리 + DB 기록
// - 전장 이탈 / 로그아웃 시 회수

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "Chat.h"
#include "ObjectMgr.h"
#include "Item.h"
#include "DatabaseEnv.h"

#include <map>
#include <unordered_map>
#include <vector>

/* =====================================================
 * 메모리 구조
 * ===================================================== */
struct WrathfulItemMem
{
    uint32     entry;
    ObjectGuid itemGuid;
};

// charGuid → 지급된 아이템 목록
static std::unordered_map<uint32, std::vector<WrathfulItemMem>> g_WrathfulItemMap;

/* =====================================================
 * 세트 정의
 * ===================================================== */
static const std::map<uint8, std::vector<uint32>> PVP_DPS_SET_MAP =
{
    { CLASS_WARRIOR, { 51543,51331,51545,51541,51359,51544,51360,51361,51542,51336,51358,51330,51393 }},
    { CLASS_PALADIN, { 51470,51331,51473,51468,51359,51471,51360,51361,51469,51336,51358,51330,51393 }},
    { CLASS_HUNTER,  { 51460,51331,51462,51350,51461,51351,51352,51459,51336,51358,51330,51503,51395 }},
    { CLASS_ROGUE,   { 51494,51331,51496,51340,51495,51341,51342,51493,51336,51358,51330,51442,51492 }},
    { CLASS_PRIEST,  { 51484,51331,51486,51482,51327,51485,51328,51329,51483,51336,51358,51330,51457 }},
    { CLASS_DEATH_KNIGHT,{ 51415,51331,51418,51413,51362,51416,51363,51364,51414,51336,51358,51330,51393 }},
    { CLASS_SHAMAN,  { 51499,51331,51502,51497,51350,51500,51351,51352,51498,51336,51358,51330,51448 }},
    { CLASS_MAGE,    { 51465,51331,51467,51463,51327,51466,51328,51329,51464,51336,51358,51330,51457 }},
    { CLASS_WARLOCK, { 51538,51331,51540,51536,51327,51539,51328,51329,51537,51336,51358,51330,51457 }},
    { CLASS_DRUID,   { 51421,51331,51424,51419,51340,51422,51341,51342,51420,51336,51358,51330,51432 }},
};

static const std::map<uint8, std::vector<uint32>> PVP_HEAL_SET_MAP =
{
    { CLASS_PALADIN, { 51470,51331,51473,51468,51359,51471,51360,51361,51469,51336,51358,51330,51453 }},
    { CLASS_PRIEST,  { 51484,51331,51486,51482,51327,51485,51328,51329,51483,51336,51358,51330,51405 }},
    { CLASS_SHAMAN,  { 51499,51331,51502,51497,51350,51500,51351,51352,51498,51336,51358,51330,51405 }},
    { CLASS_DRUID,   { 51421,51331,51424,51419,51340,51422,51341,51342,51420,51336,51358,51330,51405 }},
};

/* =====================================================
 * DB 기록
 * ===================================================== */
static void RecordWrathfulItem(uint32 charGuid, WrathfulItemMem const& info)
{
    std::string sql = Trinity::StringFormat(
		"INSERT INTO appdata.wrathful_given_items "
		"(char_guid, item_guid, item_entry) "
		"VALUES ({}, {}, {})",
		charGuid,
		info.itemGuid.GetRawValue(),
		info.entry
	);
	CharacterDatabase.Execute(sql.c_str());
}

static void RemoveWrathfulItem(uint32 charGuid, ObjectGuid itemGuid)
{
	 std::string sql = Trinity::StringFormat(
        "DELETE FROM appdata.wrathful_given_items "
        "WHERE char_guid={} AND item_guid={}",
        charGuid,
        itemGuid.GetRawValue()
    );
	CharacterDatabase.Execute(sql.c_str());
}

/* =====================================================
 * 지급 헬퍼
 * ===================================================== */
static bool GiveItemIfNotOwned(Player* player, uint32 entry)
{
    if (!player || player->HasItemCount(entry, 1, true))
        return false;

    ItemPosCountVec dest;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, 1) != EQUIP_ERR_OK)
        return false;

    Item* item = player->StoreNewItem(dest, entry, true);
    if (!item)
        return false;

    player->SendNewItem(item, 1, true, false);

    WrathfulItemMem info{ entry, item->GetGUID() };
    uint32 charGuid = player->GetGUID().GetCounter();

    g_WrathfulItemMap[charGuid].push_back(info);
    RecordWrathfulItem(charGuid, info);

    return true;
}

/* =====================================================
 * NPC AI
 * ===================================================== */
class npc_wrathful_set_giver : public CreatureScript
{
public:
    npc_wrathful_set_giver() : CreatureScript("npc_wrathful_set_giver") {}

    struct npc_wrathful_set_giverAI : public ScriptedAI
    {
        npc_wrathful_set_giverAI(Creature* c) : ScriptedAI(c) {}

        bool OnGossipHello(Player* player) override
        {
            ClearGossipMenuFor(player);

            if (player->GetLevel() != 80)
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "80레벨만 이용 가능합니다.", GOSSIP_SENDER_MAIN, 1);
                SendGossipMenuFor(player, 1, me->GetGUID());
                return true;
            }

            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "분노한 검투사 DPS 세트 지급", GOSSIP_SENDER_MAIN, 100);
            if (PVP_HEAL_SET_MAP.count(player->GetClass()))
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "분노한 검투사 HEAL 세트 지급", GOSSIP_SENDER_MAIN, 101);

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "닫기", GOSSIP_SENDER_MAIN, 1);
            SendGossipMenuFor(player, 1, me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32, uint32 gossipListId) override
        {
			uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            auto const* map =
                (action == 100) ? &PVP_DPS_SET_MAP :
                (action == 101) ? &PVP_HEAL_SET_MAP : nullptr;

			std::string msg = Trinity::StringFormat("분노한 검투사 이전 {}", action);
            ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());

            if (!map || !map->count(player->GetClass()))
            {
                CloseGossipMenuFor(player);
                return true;
            }

            uint32 count = 0;
            for (uint32 entry : map->at(player->GetClass()))
                if (GiveItemIfNotOwned(player, entry))
                    ++count;

            ChatHandler(player->GetSession()).PSendSysMessage(
                "분노한 검투사 세트 지급 완료: {}개", count);

            CloseGossipMenuFor(player);
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_wrathful_set_giverAI(creature);
    }
};

/* =====================================================
 * 등록
 * ===================================================== */
void AddSC_bg_wrathful_set_npc()
{
    new npc_wrathful_set_giver();
}
