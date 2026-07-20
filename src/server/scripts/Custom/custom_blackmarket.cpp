/*
 * Custom BlackMarket System
 *
 * Custom Update - 2026-06-24
 * - 입찰 성공 기준을 blackmarket_auction.current_bid / bidder_guid / bid_count 로 고정
 * - appdata.blackmarket_rp_bids 는 입찰 기록 / 유찰자 환급 추적용으로만 사용
 * - 경매 종료 시 낙찰품은 blackmarket_auction 기준으로 우편 발송
 * - 유찰자는 appdata.blackmarket_rp_bids 기준으로 RP 환급 토큰 발송
 *
 * Custom Update - 2026-07-03
 * - 테스트 서버(RealmID=2)는 서버 시작 2분 후 경매 시작
 * - 테스트 서버(RealmID=2)는 기존 active 경매 복구 없이 새 테스트 경매 예약
 * - 테스트 서버(RealmID=2)는 경매 영업/종료 시간을 2분으로 단축
 * - 테스트 서버(RealmID=2)는 앱 FCM 알림 발송 제외
 *
 * Custom Update - 2026-07-04
 * - 만료된 active 경매가 암시장 목록에 표시되지 않도록 end_time 조건 추가
 * - 유찰자 RP 환급 토큰 우편도 온라인 캐릭터에게 MailReceiver(Player*, guid) 방식으로 발송
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Config.h"
#include "Containers.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "Player.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Chat.h"
#include "Group.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Mail.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Pet.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "MovementGenerator.h"
#include "DBCStores.h"
#include "AchievementMgr.h"
#include "World.h"
#include "Log.h"
#include "Random.h"
#include "StringFormat.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "CustomRP.h"

#include <unordered_map>
#include <unordered_set>
#include <array>
#include <string>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>

//암시장
// 암시장 (RP 기반, 단일 입찰로그 테이블 관리)

// ===== Config / Consts =====
static constexpr uint32 MIN_START_RP             = 10000;   // 최초 입찰 최소 RP
static constexpr uint32 MIN_INCREMENT_RP         = 100;     // 최소 인상 RP
static constexpr uint32 EXTEND_THRESHOLD_SEC     = 300;     // 종료 5분 이내
static constexpr uint32 EXTEND_ADD_SEC           = 300;     // 5분 연장
static constexpr uint8  EXTEND_MAX_COUNT         = 12;
static constexpr uint32 AUCTION_VISIBLE          = 12;
static constexpr uint32 SPAWN_DURATION_SEC       = 3600;    // 운영서버 영업시간 1시간
static constexpr uint32 BM_TEST_REALM_ID         = 2;       // 테스트 서버 RealmID
static constexpr uint32 BM_TEST_START_DELAY_SEC  = 120;     // 테스트 서버: 서버 시작 2분 후 경매 시작
static constexpr uint32 BM_TEST_DURATION_SEC     = 120;     // 테스트 서버: 경매 2분 후 종료
static constexpr uint32 BLACKMARKET_NPC_ENTRY    = 5000064;
static constexpr uint32 BM_BID_FEE_GOLD          = 100;     // 입찰 수수료(고정 100골드)
static constexpr uint64 BM_BID_FEE_COPPER        = uint64(BM_BID_FEE_GOLD) * GOLD;
static constexpr uint32 BM_RP_REFUND_ITEM_ENTRY  = 823;     // "RP 환급 토큰" 템플릿(직접 DB에 생성)
static constexpr uint32 BM_GADGETZAN_PORTAL_ENTRY = 244606;
static constexpr uint32 BM_GADGETZAN_PORTAL_GUID  = 167115;
static constexpr uint32 BM_GADGETZAN_PORTAL_PHASE_ACTIVE = 1;
static constexpr uint32 BM_GADGETZAN_PORTAL_PHASE_HIDDEN = 2;

// 앱 아이템 샵 카테고리 ID
static constexpr std::array<uint32, 1> BM_CATEGORY_IDS = {
    7
};

// ===== Globals =====
static ObjectGuid s_BlackMarketNpcGuid;
static uint32     s_BlackMarketNpcMapId = 0;
static time_t     s_BlackMarketActiveUntil = 0;
static uint32     s_GadgetzanPortalPhase = 0;

static bool IsBlackMarketTestRealm()
{
    return sConfigMgr->GetIntDefault("RealmID", 0) == BM_TEST_REALM_ID;
}

static uint32 GetBlackMarketDurationSec()
{
    return IsBlackMarketTestRealm() ? BM_TEST_DURATION_SEC : SPAWN_DURATION_SEC;
}

static char const* GetBlackMarketDurationText()
{
    return IsBlackMarketTestRealm() ? "2분" : "1시간";
}

static bool IsBlackMarketActive()
{
    return s_BlackMarketActiveUntil > GameTime::GetGameTime();
}

static void SetGadgetzanPortalPhase(uint32 phase, bool force = false)
{
    if (!force && s_GadgetzanPortalPhase == phase)
        return;

    s_GadgetzanPortalPhase = phase;

    GameObjectData const* data = sObjectMgr->GetGameObjectData(BM_GADGETZAN_PORTAL_GUID);
    if (!data)
    {
        TC_LOG_ERROR("custom", "[BM] 가젯잔 포털 GameObjectData 없음: guid={}, entry={}",
                     BM_GADGETZAN_PORTAL_GUID, BM_GADGETZAN_PORTAL_ENTRY);
        return;
    }

    const_cast<GameObjectData*>(data)->phaseMask = phase;

    sMapMgr->DoForAllMapsWithMapId(
        data->mapId,
        [phase](Map* map) -> void
        {
            if (!map)
                return;

            auto bounds = Trinity::Containers::MapEqualRange(
                map->GetGameObjectBySpawnIdStore(),
                BM_GADGETZAN_PORTAL_GUID
            );

            for (auto const& pair : bounds)
            {
                GameObject* go = pair.second;
                if (!go)
                    continue;

                if (go->GetSpawnId() != BM_GADGETZAN_PORTAL_GUID)
                    continue;

                if (go->GetEntry() != BM_GADGETZAN_PORTAL_ENTRY)
                    continue;

                go->SetPhaseMask(phase, true);
                go->UpdateObjectVisibility();
            }
        }
    );

    TC_LOG_INFO("custom", "[BM] 가젯잔 포털 페이즈 변경: guid={}, entry={}, phase={}",
                BM_GADGETZAN_PORTAL_GUID, BM_GADGETZAN_PORTAL_ENTRY, phase);
}

static void SetBlackMarketActiveUntil(time_t activeUntil)
{
    s_BlackMarketActiveUntil = activeUntil;

    if (IsBlackMarketActive())
        SetGadgetzanPortalPhase(BM_GADGETZAN_PORTAL_PHASE_ACTIVE);
    else
        SetGadgetzanPortalPhase(BM_GADGETZAN_PORTAL_PHASE_HIDDEN);
}

static void UpdateBlackMarketPortalPhase()
{
    if (IsBlackMarketActive())
        SetGadgetzanPortalPhase(BM_GADGETZAN_PORTAL_PHASE_ACTIVE);
    else
        SetGadgetzanPortalPhase(BM_GADGETZAN_PORTAL_PHASE_HIDDEN);
}

static void DespawnBlackMarketNpc()
{
    // 암거상은 DB에 고정 스폰되어 있으므로 스크립트에서 소환/삭제하지 않는다.
    s_BlackMarketNpcGuid = ObjectGuid::Empty;
    s_BlackMarketNpcMapId = 0;
}

// ===== 유틸 =====
static std::string EscapeShell(std::string s)
{
    for (char& c : s)
        if (c == '\'' || c == '\"')
            c = ' ';

    return s;
}

static void WritePartyChatToFile(uint32, std::string const&, uint32, uint32, std::string const&)
{
    // no-op
}

static void RefreshBlackMarketMailIfOnline(uint32 receiverGuidLow, char const* reason)
{
    if (receiverGuidLow == 0)
        return;

    ObjectGuid receiverGuid = ObjectGuid::Create<HighGuid::Player>(receiverGuidLow);

    if (Player* receiver = ObjectAccessor::FindConnectedPlayer(receiverGuid))
    {
        receiver->UpdateNextMailTimeAndUnreads();
        receiver->SendNewMail();

        TC_LOG_INFO(
            "custom",
            "[BM] 접속 중 우편함 리프레시 패킷 발송: receiverGuid={}, reason={}",
            receiverGuidLow,
            reason ? reason : "unknown"
        );
    }
}

static bool RestoreAuctionBidStateIfOwned(
    uint32 auctionId,
    uint64 expectedBidRP,
    uint32 expectedBidderGuid,
    uint64 oldBidRP,
    uint32 oldBidderGuid,
    uint32 oldBidCount,
    uint32 oldEndTime,
    uint32 oldExtendCount)
{
    std::string restoreAuction = Trinity::StringFormat(
        "UPDATE blackmarket_auction "
        "SET current_bid={}, bidder_guid={}, bid_count={}, end_time={}, extend_count={} "
        "WHERE auction_id={} "
        "AND auction_status='active' "
        "AND current_bid={} "
        "AND bidder_guid={}",
        static_cast<uint64_t>(oldBidRP),
        oldBidderGuid,
        oldBidCount,
        oldEndTime,
        oldExtendCount,
        auctionId,
        static_cast<uint64_t>(expectedBidRP),
        expectedBidderGuid
    );

    WorldDatabase.DirectExecute(restoreAuction.c_str());

    std::string checkAuction = Trinity::StringFormat(
        "SELECT current_bid, bidder_guid "
        "FROM blackmarket_auction "
        "WHERE auction_id={} "
        "AND current_bid={} "
        "AND bidder_guid={} "
        "LIMIT 1",
        auctionId,
        static_cast<uint64_t>(oldBidRP),
        oldBidderGuid
    );

    if (QueryResult rCheck = WorldDatabase.Query(checkAuction.c_str()))
        return true;

    return false;
}

// ============================================================================
//                       NPC: Auctioneer (Gossip & Bidding)
// ============================================================================
class npc_black_market_auctioneer : public CreatureScript
{
public:
    npc_black_market_auctioneer() : CreatureScript("npc_black_market_auctioneer") {}

    struct npc_black_market_auctioneerAI : public ScriptedAI
    {
        using GuidKey = uint64;
        std::unordered_map<GuidKey, uint32> g_AuctionSelections;

        static constexpr uint32 ACTION_BID_INPUT        = 999;
        static constexpr uint32 ACTION_EXCHANGE_MENU    = 900000001;
        static constexpr uint32 ACTION_EXCHANGE_INPUT   = 900000002;
        static constexpr uint32 GOLD_PER_RP             = 500;
        static constexpr uint32 COPPER_PER_GOLD         = 10000;

        npc_black_market_auctioneerAI(Creature* c) : ScriptedAI(c) {}

        static void SendEndTimeWhisper(Player* player, time_t endTime)
        {
            if (!player)
                return;

            tm* ti = localtime(&endTime);
            if (!ti)
                return;

            std::ostringstream timeStr;
            timeStr << (ti->tm_year + 1900) << "년 "
                    << (ti->tm_mon + 1)    << "월 "
                    << ti->tm_mday         << "일 "
                    << ti->tm_hour         << "시 "
                    << ti->tm_min          << "분";

            std::ostringstream msg;
            msg << "|cff00ff00[암시장알림]|r " << timeStr.str() << " 종료 예정입니다.";
            ChatHandler(player->GetSession()).SendSysMessage(msg.str().c_str());
        }

        bool OnGossipHello(Player* player) override
        {
            if (!player)
                return false;

            ClearGossipMenuFor(player);

            if (player->GetLevel() != 80)
            {
                player->GetSession()->SendNotification("80레벨만 입찰할 수 있습니다.");
                return true;
            }

            {
                std::string sql = Trinity::StringFormat(
                    "SELECT a.auction_id, a.item_id, i.name, i.ItemLevel, "
                    "       a.current_bid, IFNULL(ci.iconname,''), a.end_time, a.bid_count "
                    "FROM blackmarket_auction a "
                    "JOIN item_template i ON a.item_id = i.entry "
                    "LEFT JOIN appdata.data_wotlk_itemdisplayinfo ci ON i.displayid = ci.id "
                    "WHERE a.auction_status='active' "
                    "AND a.end_time > UNIX_TIMESTAMP() "
                    "ORDER BY a.auction_id ASC LIMIT {}",
                    AUCTION_VISIBLE
                );

                QueryResult result = WorldDatabase.Query(sql.c_str());
                if (!result)
                {
                    player->GetSession()->SendNotification("현재 진행 중인 경매가 없습니다.");
                }
                else
                {
                    {
                        Field* f = result->Fetch();
                        time_t endTime = time_t(f[6].GetUInt32());
                        SendEndTimeWhisper(player, endTime);
                    }

                    do
                    {
                        Field* f = result->Fetch();
                        uint32 auctionId    = f[0].GetUInt32();
                        uint32 itemID       = f[1].GetUInt32();
                        std::string name    = f[2].GetString();
                        uint32 itemLevel    = f[3].GetUInt32();
                        uint64 currentBidRP = f[4].GetUInt64();
                        std::string icon    = f[5].GetString();
                        uint32 bidCount     = f[7].GetUInt32();

                        std::ostringstream label;
                        label << "|TInterface\\Icons\\" << icon << ":30:30:-18:0|t "
                              << "[경매ID " << auctionId << "] [아이템ID " << itemID << "]\n"
                              << name << " (Lv" << itemLevel << ")\n현재 |cffff0000"
                              << currentBidRP << "|r RP, 입찰자 " << bidCount;

                        AddGossipItemFor(player, GOSSIP_ICON_DOT, label.str(), GOSSIP_SENDER_MAIN, auctionId);
                    } while (result->NextRow());
                }
            }

            {
                std::ostringstream exLabel;
                exLabel << "|TInterface\\Icons\\INV_Misc_Coin_01:30:30:-18:0|t "
                        << "골드를 RP로 환전하기 (1 RP = " << GOLD_PER_RP << "골드)";
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, exLabel.str(), GOSSIP_SENDER_MAIN, ACTION_EXCHANGE_MENU);
            }

            AddGossipItemFor(player, GOSSIP_ICON_DOT, "닫기", GOSSIP_SENDER_MAIN, 0);

            player->TalkedToCreature(me->GetEntry(), me->GetGUID());
            SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            if (!player)
                return false;

            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            if (action == 0)
                return OnGossipHello(player);

            if (action == ACTION_EXCHANGE_MENU)
            {
                std::ostringstream label;
                label << "환전할 RP 수량을 입력해주세요.\n"
                      << "1 RP당 " << GOLD_PER_RP << " 골드가 차감됩니다.\n"
                      << "(예: 10 입력 시 " << (GOLD_PER_RP * 10) << "골드 차감)";

                AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, label.str(),
                                 GOSSIP_SENDER_MAIN, ACTION_EXCHANGE_INPUT, "", 0, true);
                AddGossipItemFor(player, GOSSIP_ICON_DOT, "뒤로가기", GOSSIP_SENDER_MAIN, 0);
                SendGossipMenuFor(player, 1, me->GetGUID());
                return true;
            }

            g_AuctionSelections[player->GetGUID().GetRawValue()] = action;

            std::ostringstream label;
            label << "[ID:" << action << "] 입찰 RP를 입력해주세요.\n"
                  << "시작금액은 " << MIN_START_RP << " RP 입니다.\n"
                  << "입찰 시 수수료로 골드 100이 차감됩니다.";

            AddGossipItemFor(player, GOSSIP_ICON_DOT, label.str(), GOSSIP_SENDER_MAIN, ACTION_BID_INPUT, "", 0, true);
            AddGossipItemFor(player, GOSSIP_ICON_DOT, "뒤로가기", GOSSIP_SENDER_MAIN, 0);
            SendGossipMenuFor(player, 1, me->GetGUID());
            return true;
        }

        bool OnGossipSelectCode(Player* player, uint32 /*menuId*/, uint32 gossipListId, char const* args) override
        {
            if (!player)
                return false;

            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            if (action == ACTION_BID_INPUT)
            {
                GuidKey key = player->GetGUID().GetRawValue();
                if (!g_AuctionSelections.count(key))
                {
                    player->GetSession()->SendNotification("세션이 만료되었거나 잘못된 접근입니다.");
                    return true;
                }

                uint64 bidRP = uint64(std::max<long long>(atoll(args ? args : "0"), 0));
                if (bidRP < MIN_START_RP)
                {
                    player->GetSession()->SendNotification("최소 10000 RP 이상만 입찰 가능합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                if (bidRP > uint64(std::numeric_limits<uint32>::max()))
                {
                    player->GetSession()->SendNotification("한 번에 너무 많은 RP를 입찰하려고 합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                uint32 auctionId = g_AuctionSelections[key];
                uint32 accountId = player->GetSession()->GetAccountId();
                uint32 guidLo    = player->GetGUID().GetCounter();

                uint32 oldBidRP = 0;
                {
                    std::string qOld = Trinity::StringFormat(
                        "SELECT bid_rp FROM appdata.blackmarket_rp_bids "
                        "WHERE auction_id={} AND account_id={}",
                        auctionId, accountId
                    );

                    if (QueryResult rOld = CharacterDatabase.Query(qOld.c_str()))
                        oldBidRP = rOld->Fetch()[0].GetUInt32();
                }

                if (bidRP <= oldBidRP)
                {
                    player->GetSession()->SendNotification("이전 입찰 RP보다 더 높게 입력해야 합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                uint32 needRP = uint32(bidRP - oldBidRP);

                uint32 curAccountRP = 0;
                {
                    std::string qRP = Trinity::StringFormat(
                        "SELECT reg_point FROM appdata.account_data WHERE id={}",
                        accountId
                    );

                    if (QueryResult rRP = CharacterDatabase.Query(qRP.c_str()))
                    {
                        curAccountRP = rRP->Fetch()[0].GetUInt32();
                        if (curAccountRP < needRP)
                        {
                            player->GetSession()->SendNotification("보유 RP가 부족합니다.");
                            CloseGossipMenuFor(player);
                            return true;
                        }
                    }
                    else
                    {
                        player->GetSession()->SendNotification("RP 정보를 조회할 수 없습니다.");
                        CloseGossipMenuFor(player);
                        return true;
                    }
                }

                if (player->GetMoney() < BM_BID_FEE_COPPER)
                {
                    player->GetSession()->SendNotification("입찰 수수료 100골드가 부족합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                uint64 curBid = 0;
                uint64 startRP = MIN_START_RP;
                uint64 minIncRP = MIN_INCREMENT_RP;
                uint32 oldBidderGuid = 0;
                uint32 oldBidCount = 0;
                uint32 oldEndTime = 0;
                uint32 oldExtendCount = 0;
                uint32 oldBidderAccountId = 0;

                {
                    std::string sqlInfo = Trinity::StringFormat(
                        "SELECT current_bid, start_price, IFNULL(min_increment,0), "
                        "       IFNULL(bidder_guid,0), IFNULL(bid_count,0), end_time, IFNULL(extend_count,0) "
                        "FROM blackmarket_auction "
                        "WHERE auction_id={} AND auction_status='active' AND end_time > UNIX_TIMESTAMP()",
                        auctionId
                    );

                    QueryResult rInfo = WorldDatabase.Query(sqlInfo.c_str());
                    if (!rInfo)
                    {
                        player->GetSession()->SendNotification("유효하지 않은 경매입니다.");
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    Field* fi = rInfo->Fetch();
                    curBid         = fi[0].GetUInt64();
                    startRP        = fi[1].GetUInt64();
                    minIncRP       = fi[2].GetUInt64();
                    oldBidderGuid  = fi[3].GetUInt32();
                    oldBidCount    = fi[4].GetUInt32();
                    oldEndTime     = fi[5].GetUInt32();
                    oldExtendCount = fi[6].GetUInt32();

                    if (startRP == 0)
                        startRP = MIN_START_RP;

                    if (minIncRP == 0)
                        minIncRP = MIN_INCREMENT_RP;
                }

                if (bidRP < startRP)
                {
                    player->GetSession()->SendNotification("시작 RP 이상만 입찰 가능합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                if (bidRP <= curBid)
                {
                    player->GetSession()->SendNotification("현재 입찰 RP보다 높아야 합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                if (bidRP < curBid + minIncRP)
                {
                    player->GetSession()->SendNotification("현재가보다 최소 100 RP 이상 입찰하셔야 합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                if (curBid > 0 && oldBidderGuid != 0 && oldBidderGuid != guidLo)
                {
                    std::string qOldBidderAccount = Trinity::StringFormat(
                        "SELECT account FROM characters WHERE guid={} LIMIT 1",
                        oldBidderGuid
                    );

                    if (QueryResult rOldBidderAccount = CharacterDatabase.Query(qOldBidderAccount.c_str()))
                        oldBidderAccountId = rOldBidderAccount->Fetch()[0].GetUInt32();
                }

                uint32 nowTs = uint32(GameTime::GetGameTime());

                uint32 newEndTime = oldEndTime;
                uint32 newExtendCount = oldExtendCount;
                if (oldEndTime > nowTs &&
                    (oldEndTime - nowTs) <= EXTEND_THRESHOLD_SEC &&
                    oldExtendCount < EXTEND_MAX_COUNT)
                {
                    newEndTime = oldEndTime + EXTEND_ADD_SEC;
                    newExtendCount = oldExtendCount + 1;
                }

                // 1) 실제 경매 본체를 먼저 갱신한다.
                {
                    std::string sqlUpd = Trinity::StringFormat(
                        "UPDATE blackmarket_auction "
                        "SET current_bid={}, bidder_guid={}, bid_count=IFNULL(bid_count,0)+1, "
                        "    end_time={}, extend_count={} "
                        "WHERE auction_id={} "
                        "AND auction_status='active' "
                        "AND end_time > UNIX_TIMESTAMP() "
                        "AND current_bid={} "
                        "AND IFNULL(bidder_guid,0)={} "
                        "AND IFNULL(bid_count,0)={} "
                        "AND end_time={} "
                        "AND IFNULL(extend_count,0)={} "
                        "AND current_bid < {}",
                        static_cast<uint64_t>(bidRP),
                        guidLo,
                        newEndTime,
                        newExtendCount,
                        auctionId,
                        static_cast<uint64_t>(curBid),
                        oldBidderGuid,
                        oldBidCount,
                        oldEndTime,
                        oldExtendCount,
                        static_cast<uint64_t>(bidRP)
                    );

                    WorldDatabase.DirectExecute(sqlUpd.c_str());
                }

                // 2) blackmarket_auction 갱신이 실제로 되었는지 확인한다.
                bool auctionSaved = false;
                {
                    std::string checkAuction = Trinity::StringFormat(
                        "SELECT current_bid, bidder_guid "
                        "FROM blackmarket_auction "
                        "WHERE auction_id={} "
                        "AND auction_status='active' "
                        "AND current_bid={} "
                        "AND bidder_guid={} "
                        "LIMIT 1",
                        auctionId,
                        static_cast<uint64_t>(bidRP),
                        guidLo
                    );

                    if (QueryResult rCheck = WorldDatabase.Query(checkAuction.c_str()))
                        auctionSaved = true;
                }

                if (!auctionSaved)
                {
                    player->GetSession()->SendNotification("입찰 처리에 실패했습니다. 다시 시도해주세요.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                // 3) 경매 본체 갱신이 확인된 뒤 RP를 차감한다.
                bool rpSaved = false;
                {
                    std::string updRP = Trinity::StringFormat(
                        "UPDATE appdata.account_data "
                        "SET reg_point = reg_point - {} "
                        "WHERE id={} AND reg_point={} AND reg_point >= {}",
                        needRP,
                        accountId,
                        curAccountRP,
                        needRP
                    );

                    CharacterDatabase.DirectExecute(updRP.c_str());

                    std::string checkRP = Trinity::StringFormat(
                        "SELECT reg_point "
                        "FROM appdata.account_data "
                        "WHERE id={} AND reg_point={} "
                        "LIMIT 1",
                        accountId,
                        curAccountRP - needRP
                    );

                    if (QueryResult rCheck = CharacterDatabase.Query(checkRP.c_str()))
                        rpSaved = true;
                }

                if (!rpSaved)
                {
                    RestoreAuctionBidStateIfOwned(
                        auctionId,
                        bidRP,
                        guidLo,
                        curBid,
                        oldBidderGuid,
                        oldBidCount,
                        oldEndTime,
                        oldExtendCount
                    );

                    TC_LOG_ERROR(
                        "custom",
                        "[BM] RP 차감 실패로 입찰 롤백: auctionId={}, accountId={}, guidLo={}, bidRP={}, needRP={}",
                        auctionId,
                        accountId,
                        guidLo,
                        static_cast<uint64_t>(bidRP),
                        needRP
                    );

                    player->GetSession()->SendNotification("RP 차감 처리에 실패했습니다. 다시 시도해주세요.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                // 4) 입찰 기록은 환급 추적용으로 저장한다.
                {
                    std::string upsert = Trinity::StringFormat(
                        "INSERT INTO appdata.blackmarket_rp_bids "
                        "(auction_id, account_id, character_guid, bid_rp, created_at, updated_at, status, token_item_guid) "
                        "VALUES ({}, {}, {}, {}, {}, {}, 0, 0) "
                        "ON DUPLICATE KEY UPDATE "
                        "  bid_rp=VALUES(bid_rp), "
                        "  character_guid=VALUES(character_guid), "
                        "  updated_at=VALUES(updated_at), "
                        "  status=0, "
                        "  token_item_guid=0",
                        auctionId,
                        accountId,
                        guidLo,
                        static_cast<uint32>(bidRP),
                        nowTs,
                        nowTs
                    );

                    CharacterDatabase.DirectExecute(upsert.c_str());
                }

                // 5) 보조 기록 저장 확인. 실패 시 RP와 경매 본체를 되돌린다.
                bool bidLogSaved = false;
                {
                    std::string checkBidLog = Trinity::StringFormat(
                        "SELECT bid_rp "
                        "FROM appdata.blackmarket_rp_bids "
                        "WHERE auction_id={} AND account_id={} AND bid_rp={} "
                        "LIMIT 1",
                        auctionId,
                        accountId,
                        static_cast<uint32>(bidRP)
                    );

                    if (QueryResult rCheck = CharacterDatabase.Query(checkBidLog.c_str()))
                        bidLogSaved = true;
                }

                if (!bidLogSaved)
                {
                    {
                        std::string refundRP = Trinity::StringFormat(
                            "UPDATE appdata.account_data "
                            "SET reg_point = reg_point + {} "
                            "WHERE id={}",
                            needRP,
                            accountId
                        );

                        CharacterDatabase.DirectExecute(refundRP.c_str());
                    }

                    RestoreAuctionBidStateIfOwned(
                        auctionId,
                        bidRP,
                        guidLo,
                        curBid,
                        oldBidderGuid,
                        oldBidCount,
                        oldEndTime,
                        oldExtendCount
                    );

                    TC_LOG_ERROR(
                        "custom",
                        "[BM] 입찰 로그 저장 실패로 롤백: auctionId={}, accountId={}, guidLo={}, bidRP={}",
                        auctionId,
                        accountId,
                        guidLo,
                        static_cast<uint64_t>(bidRP)
                    );

                    player->GetSession()->SendNotification("입찰 기록 저장에 실패했습니다. RP는 환급되었습니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                if (newEndTime > oldEndTime && time_t(newEndTime) > s_BlackMarketActiveUntil)
                {
                    SetBlackMarketActiveUntil(time_t(newEndTime));
                }

                if (!IsBlackMarketTestRealm() && oldBidderAccountId != 0 && oldBidderAccountId != accountId)
                {
                    SendFcmCategory(
                        9,
                        "blackmarket",
                        "[암시장] 상위 입찰 발생",
                        Trinity::StringFormat(
                            "입찰 중인 암시장 경매에 더 높은 입찰이 들어왔습니다.\n경매ID: {}\n기존 입찰: {} RP\n현재 입찰: {} RP",
                            auctionId,
                            static_cast<uint64_t>(curBid),
                            static_cast<uint64_t>(bidRP)
                        ),
                        oldBidderAccountId
                    );
                }

                player->ModifyMoney(-int32(BM_BID_FEE_COPPER));

                ChatHandler(player->GetSession()).SendSysMessage("입찰 수수료 100골드가 지불되었습니다.");

                std::string announce = Trinity::StringFormat("|cffABD473[암시장알림]|r {} RP 입찰!", bidRP);
                sWorld->SendServerMessage(SERVER_MSG_STRING, announce.c_str(), 0);

                CloseGossipMenuFor(player);
                return true;
            }

            if (action == ACTION_EXCHANGE_INPUT)
            {
                uint32 accountId = player->GetSession()->GetAccountId();

                uint64 wantRP = uint64(std::max<long long>(atoll(args ? args : "0"), 0));
                if (wantRP == 0)
                {
                    player->GetSession()->SendNotification("1 이상의 RP를 입력해주세요.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                uint64 needGold = wantRP * GOLD_PER_RP;
                uint64 needCopper = needGold * COPPER_PER_GOLD;

                if (needCopper > uint64(std::numeric_limits<int32>::max()))
                {
                    player->GetSession()->SendNotification("한 번에 너무 많은 RP를 환전하려고 합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                uint64 curCopper = player->GetMoney();
                if (curCopper < needCopper)
                {
                    player->GetSession()->SendNotification("환전에 필요한 골드가 부족합니다.");
                    CloseGossipMenuFor(player);
                    return true;
                }

                player->ModifyMoney(-int32(needCopper));

                {
                    std::string updRP = Trinity::StringFormat(
                        "UPDATE appdata.account_data SET reg_point = reg_point + {} WHERE id={}",
                        static_cast<uint32>(wantRP),
                        accountId
                    );

                    CharacterDatabase.Execute(updRP.c_str());
                }

                std::ostringstream msg;
                msg << "골드 " << needGold << "을(를) 사용하여 "
                    << wantRP << " RP로 환전되었습니다.";
                ChatHandler(player->GetSession()).SendSysMessage(msg.str().c_str());

                CloseGossipMenuFor(player);
                return true;
            }

            return false;
        }
    };

    CreatureAI* GetAI(Creature* c) const override
    {
        return new npc_black_market_auctioneerAI(c);
    }
};

// ============================================================================
//                           Scheduler: Close auctions
// ============================================================================
static constexpr uint32 BM_AUCTION_MAIL_SENDER_ID = 7;
static constexpr uint32 BM_AUCTION_MAIL_OUTBIDDED = 0;
static constexpr uint32 BM_AUCTION_MAIL_WON       = 1;

static Player* FindBlackMarketMailReceiver(uint32 receiverGuidLow)
{
    if (receiverGuidLow == 0)
        return nullptr;

    ObjectGuid receiverGuid = ObjectGuid::Create<HighGuid::Player>(receiverGuidLow);
    return ObjectAccessor::FindConnectedPlayer(receiverGuid);
}

static std::string BuildBlackMarketAuctionMailSubject(
    uint32 itemEntry,
    int32 randomPropertyId,
    uint32 response,
    uint32 auctionId,
    uint32 itemCount
)
{
    return Trinity::StringFormat(
        "{}:{}:{}:{}:{}",
        itemEntry,
        randomPropertyId,
        response,
        auctionId,
        itemCount
    );
}

static std::string BuildBlackMarketAuctionWonMailBody(uint64 bidRP)
{
    return Trinity::StringFormat(
        "{}:{}:{}",
        uint64(0),
        static_cast<uint64_t>(bidRP),
        uint32(0)
    );
}

class BlackMarketAuctionScheduler : public WorldScript
{
public:
    BlackMarketAuctionScheduler() : WorldScript("BlackMarketAuctionScheduler") {}

    void OnUpdate(uint32 /*diff*/) override
    {
        static time_t nextCheck = 0;
        time_t now = GameTime::GetGameTime();

        if (now < nextCheck)
            return;

        nextCheck = now + 5;

        QueryResult result = WorldDatabase.Query(
            "SELECT auction_id, item_id, current_bid, bidder_guid, end_time "
            "FROM blackmarket_auction "
            "WHERE auction_status='active' AND end_time <= UNIX_TIMESTAMP()"
        );

        if (!result)
            return;

        do
        {
            Field* f           = result->Fetch();
            uint32 auctionId   = f[0].GetUInt32();
            uint32 itemId      = f[1].GetUInt32();
            uint64 bidAmountRP = f[2].GetUInt64();
            uint32 bidderLow   = f[3].GetUInt32();
            (void)f[4];

            {
                std::string sqlDone = Trinity::StringFormat(
                    "UPDATE blackmarket_auction "
                    "SET auction_status='completed' "
                    "WHERE auction_id={} AND auction_status='active' AND end_time <= UNIX_TIMESTAMP()",
                    auctionId
                );

                WorldDatabase.DirectExecute(sqlDone.c_str());
            }

            {
                std::string qDone = Trinity::StringFormat(
                    "SELECT auction_id "
                    "FROM blackmarket_auction "
                    "WHERE auction_id={} AND auction_status='completed' "
                    "LIMIT 1",
                    auctionId
                );

                if (!WorldDatabase.Query(qDone.c_str()))
                {
                    TC_LOG_ERROR(
                        "custom",
                        "[BM] 경매 완료 상태 변경 확인 실패로 낙찰 처리 중단: auctionId={}",
                        auctionId
                    );
                    continue;
                }
            }

            if (bidderLow == 0 || bidAmountRP == 0)
                continue;

            ObjectGuid bidderGuid = ObjectGuid::Create<HighGuid::Player>(bidderLow);

            uint32 accountIdWinner = 0;
            {
                std::string qAcc = Trinity::StringFormat(
                    "SELECT account FROM characters WHERE guid={}",
                    bidderGuid.GetCounter()
                );

                if (QueryResult q = CharacterDatabase.Query(qAcc.c_str()))
                    accountIdWinner = q->Fetch()[0].GetUInt32();
            }

            bool winnerDelivered = false;
            if (accountIdWinner != 0)
			{
				Player* bidder = FindBlackMarketMailReceiver(bidderLow);
				CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

				if (Item* item = Item::CreateItem(itemId, 1, nullptr))
				{
					if (const ItemTemplate* proto = item->GetTemplate())
					{
						item->SetBinding(false);
						item->SaveToDB(trans);

						MailDraft(
							BuildBlackMarketAuctionMailSubject(
								item->GetEntry(),
								item->GetItemRandomPropertyId(),
								BM_AUCTION_MAIL_WON,
								auctionId,
								item->GetCount()
							),
							BuildBlackMarketAuctionWonMailBody(bidAmountRP)
						)
							.AddItem(item)
							.SendMailTo(
								trans,
								MailReceiver(bidder, bidderGuid.GetCounter()),
								MailSender(MAIL_AUCTION, BM_AUCTION_MAIL_SENDER_ID),
								MAIL_CHECK_MASK_COPIED
							);

						CharacterDatabase.CommitTransaction(trans);
						winnerDelivered = true;

						if (bidder)
							bidder->SendNewMail();

						std::string msg = Trinity::StringFormat(
							"|cffABD473[암시장알림]|r [{}] {} RP 낙찰.",
							proto->Name1.c_str(),
							bidAmountRP
						);

						sWorld->SendServerMessage(SERVER_MSG_STRING, msg.c_str(), 0);
					}
				}
			}
            else
            {
                TC_LOG_ERROR(
                    "custom",
                    "[BM] 낙찰 캐릭터 계정 조회 실패: auctionId={}, bidderGuid={}",
                    auctionId,
                    bidderLow
                );
            }

            {
                if (accountIdWinner != 0 && winnerDelivered)
                {
                    std::string updWin = Trinity::StringFormat(
                        "UPDATE appdata.blackmarket_rp_bids "
                        "SET status=1, updated_at={} "
                        "WHERE auction_id={} AND account_id={} AND status=0",
                        uint32(now),
                        auctionId,
                        accountIdWinner
                    );

                    CharacterDatabase.Execute(updWin.c_str());
                }

                std::string qLosers = Trinity::StringFormat(
                    "SELECT account_id, character_guid, bid_rp "
                    "FROM appdata.blackmarket_rp_bids "
                    "WHERE auction_id={} "
                    "AND account_id<>{} "
                    "AND status=0",
                    auctionId,
                    accountIdWinner == 0 ? 0 : accountIdWinner
                );

                if (QueryResult rLoser = CharacterDatabase.Query(qLosers.c_str()))
                {
                    do
                    {
                        Field* fl       = rLoser->Fetch();
                        uint32 accId    = fl[0].GetUInt32();
                        uint32 chGuidLo = fl[1].GetUInt32();
                        uint32 bidRP    = fl[2].GetUInt32();

                        if (bidRP == 0)
                            continue;

                        uint64 tokenItemGuidCounter = 0;

                        {
                            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

                            if (Item* token = Item::CreateItem(BM_RP_REFUND_ITEM_ENTRY, 1, nullptr))
                            {
                                Player* receiver = FindBlackMarketMailReceiver(chGuidLo);

                                token->SaveToDB(trans);
                                tokenItemGuidCounter = token->GetGUID().GetCounter();

                                MailDraft(
                                    BuildBlackMarketAuctionMailSubject(
                                        token->GetEntry(),
                                        token->GetItemRandomPropertyId(),
                                        BM_AUCTION_MAIL_OUTBIDDED,
                                        auctionId,
                                        token->GetCount()
                                    ),
                                    ""
                                )
                                    .AddItem(token)
                                    .SendMailTo(
                                        trans,
                                        MailReceiver(receiver, chGuidLo),
                                        MailSender(MAIL_AUCTION, BM_AUCTION_MAIL_SENDER_ID),
                                        MAIL_CHECK_MASK_COPIED
                                    );

                                CharacterDatabase.CommitTransaction(trans);

                                if (receiver)
                                    receiver->SendNewMail();
                            }
                        }

                        if (tokenItemGuidCounter != 0)
                        {
                            std::string updLoser = Trinity::StringFormat(
                                "UPDATE appdata.blackmarket_rp_bids "
                                "SET status=2, token_item_guid={}, updated_at={} "
                                "WHERE auction_id={} AND account_id={} AND status=0",
                                static_cast<uint64_t>(tokenItemGuidCounter),
                                uint32(now),
                                auctionId,
                                accId
                            );

                            CharacterDatabase.Execute(updLoser.c_str());
                        }
                        else
                        {
                            std::string directRefund = Trinity::StringFormat(
                                "UPDATE appdata.account_data "
                                "SET reg_point = reg_point + {} "
                                "WHERE id={}",
                                bidRP,
                                accId
                            );

                            CharacterDatabase.Execute(directRefund.c_str());

                            std::string updRefunded = Trinity::StringFormat(
                                "UPDATE appdata.blackmarket_rp_bids "
                                "SET status=3, token_item_guid=0, updated_at={} "
                                "WHERE auction_id={} AND account_id={} AND status=0",
                                uint32(now),
                                auctionId,
                                accId
                            );

                            CharacterDatabase.Execute(updRefunded.c_str());

                            TC_LOG_ERROR(
                                "custom",
                                "[BM] RP 환급 토큰 생성 실패로 직접 환급 처리: auctionId={}, accountId={}, bidRP={}",
                                auctionId,
                                accId,
                                bidRP
                            );
                        }

                    } while (rLoser->NextRow());
                }
            }

        } while (result->NextRow());
    }
};

// ============================================================================
//                             Spawner
// ============================================================================
class BlackMarketSpawner : public WorldScript
{
public:
    BlackMarketSpawner() : WorldScript("BlackMarketSpawner") {}

    enum BMSpawnMode
    {
        BM_MODE_TEST     = 0,
        BM_MODE_MONTHLY  = 1,
        BM_MODE_INTERVAL = 2
    };

    static constexpr BMSpawnMode BM_MODE = BM_MODE_INTERVAL;
    static constexpr uint32 BM_TEST_DELAY_SEC = BM_TEST_START_DELAY_SEC;
    static constexpr uint32 BM_INTERVAL_MIN_DAYS = 3;
    static constexpr uint32 BM_INTERVAL_MAX_DAYS = 4;
    static constexpr uint32 BM_START_HOUR_MIN = 20;
    static constexpr uint32 BM_START_HOUR_MAX = 22;

    static bool IsRealm2()
    {
        return IsBlackMarketTestRealm();
    }

    static uint32 GenerateMonthlySpawnTime()
    {
        time_t now = GameTime::GetGameTime();
        tm lt = *localtime(&now);

        int year  = lt.tm_year + 1900;
        int month = lt.tm_mon + 1;

        int day  = urand(1, 28);
        int hour = urand(BM_START_HOUR_MIN, BM_START_HOUR_MAX);
        int min  = urand(0, 59);

        tm when {};
        when.tm_year = year - 1900;
        when.tm_mon  = month - 1;
        when.tm_mday = day;
        when.tm_hour = hour;
        when.tm_min  = min;
        when.tm_sec  = 0;

        time_t ts = mktime(&when);
        if (ts <= now)
        {
            month += 1;
            if (month > 12)
            {
                month = 1;
                year += 1;
            }

            day  = urand(1, 28);
            hour = urand(BM_START_HOUR_MIN, BM_START_HOUR_MAX);
            min  = urand(0, 59);

            when = {};
            when.tm_year = year - 1900;
            when.tm_mon  = month - 1;
            when.tm_mday = day;
            when.tm_hour = hour;
            when.tm_min  = min;
            when.tm_sec  = 0;
            ts = mktime(&when);
        }

        return static_cast<uint32>(ts);
    }

    static uint32 GenerateIntervalSpawnTime()
    {
        time_t now = GameTime::GetGameTime();
        tm lt = *localtime(&now);

        int addDays = urand(BM_INTERVAL_MIN_DAYS, BM_INTERVAL_MAX_DAYS);
        lt.tm_mday += addDays;
        lt.tm_hour  = urand(BM_START_HOUR_MIN, BM_START_HOUR_MAX);
        lt.tm_min   = urand(0, 59);
        lt.tm_sec   = 0;

        time_t ts = mktime(&lt);

        if (ts <= now)
        {
            lt.tm_mday += BM_INTERVAL_MIN_DAYS;
            ts = mktime(&lt);
        }

        return static_cast<uint32>(ts);
    }

    void ScheduleNextSpawn(time_t now)
    {
        uint32 nextTs = 0;

        if (BM_MODE == BM_MODE_TEST)
        {
            nextTs = static_cast<uint32>(now) + BM_TEST_DELAY_SEC;
            TC_LOG_INFO("custom", "[BM][TEST] QuickNext: {}초 후({}) 스폰 예정",
                        BM_TEST_DELAY_SEC, nextTs);
        }
        else if (BM_MODE == BM_MODE_MONTHLY)
        {
            nextTs = GenerateMonthlySpawnTime();
            TC_LOG_INFO("custom", "[BM] 다음 월간 시작 스케줄 생성: next_spawn_time={}", nextTs);
        }
        else if (BM_MODE == BM_MODE_INTERVAL)
        {
            nextTs = GenerateIntervalSpawnTime();
            TC_LOG_INFO("custom", "[BM] 다음 3~4일 랜덤 시작 스케줄 생성: next_spawn_time={}", nextTs);
        }
        else
        {
            nextTs = GenerateMonthlySpawnTime();
            TC_LOG_INFO("custom", "[BM] BM_MODE 알 수 없음, 월간 스케줄로 대체 생성: next_spawn_time={}", nextTs);
        }

        std::string upsert = Trinity::StringFormat(
            "REPLACE INTO blackmarket_schedule (id, next_spawn_time, status) VALUES (1, {}, 1)",
            nextTs
        );

        WorldDatabase.Execute(upsert.c_str());

        m_spawnTime   = time_t(nextTs);
        m_warningSent = false;
        m_spawned     = false;
    }

    void OnStartup() override
    {
        time_t now = GameTime::GetGameTime();

        SetBlackMarketActiveUntil(0);

        if (IsRealm2())
        {
            WorldDatabase.DirectExecute(
                "UPDATE blackmarket_auction "
                "SET auction_status='completed' "
                "WHERE auction_status='active'"
            );

            m_spawnTime   = now + BM_TEST_DELAY_SEC;
            m_warningSent = true;
            m_spawned     = false;

            TC_LOG_INFO("custom", "[BM][REALM2] 서버 시작 {}초 후 암시장 시작 예정: next_spawn_time={}",
                        BM_TEST_DELAY_SEC, uint32(m_spawnTime));
            return;
        }

        bool activeMarketRestored = RestoreActiveBlackMarket(now);
        (void)activeMarketRestored;

        QueryResult r = WorldDatabase.Query("SELECT next_spawn_time, status FROM blackmarket_schedule WHERE id=1");
        if (!r)
        {
            ScheduleNextSpawn(now);
            return;
        }

        Field* f = r->Fetch();
        uint32 nextSpawn = f[0].GetUInt32();
        uint32 status    = f[1].GetUInt32();

        m_spawnTime   = time_t(nextSpawn);
        m_warningSent = false;
        m_spawned     = false;

        if (status == 0 || m_spawnTime <= now)
        {
            TC_LOG_INFO("custom", "[BM] 기존 스케줄이 비활성/과거(next_spawn_time={}, status={}), 새 스케줄 생성",
                        nextSpawn, status);
            ScheduleNextSpawn(now);
        }
    }

    void OnUpdate(uint32 /*diff*/) override
    {
        time_t now = GameTime::GetGameTime();

        if (s_BlackMarketActiveUntil != 0 && now >= s_BlackMarketActiveUntil)
        {
            SetBlackMarketActiveUntil(0);
            DespawnBlackMarketNpc();
        }

        if (m_restoreActiveMarket && IsBlackMarketActive() && now >= m_restoreRetryAt)
        {
            if (RestoreActiveBlackMarketNpc(s_BlackMarketActiveUntil))
                m_restoreActiveMarket = false;
            else
                m_restoreRetryAt = now + 60;
        }

        if (m_spawnTime == 0)
        {
            UpdateBlackMarketPortalPhase();
            return;
        }

        UpdateBlackMarketPortalPhase();

        if (!m_warningSent && now >= (m_spawnTime - 3600))
        {
            // (void)std::system("php /home/esotar/fcm/send_fcm_BM.php 'alert' '암거상 노사장이 1시간뒤 방문예정입니다.' > /dev/null 2>&1 &");
            if (!IsBlackMarketTestRealm())
            {
                SendFcmCategory(
                    9,
                    "blackmarket",
                    "[암시장] 암시장 예정",
                    "암거상이 1시간후에 방문 예정입니다.",
                    0
                );
            }

            m_warningSent = true;
        }

        if (!m_spawned && now >= m_spawnTime)
        {
            if (SpawnNPC())
            {
                m_spawned = true;

                if (IsRealm2())
                    m_spawnTime = 0;
                else
                    ScheduleNextSpawn(now);
            }
            else
            {
                m_spawnTime = now + 60;
                m_warningSent = true;
                TC_LOG_ERROR("custom", "[BM] 암시장 시작 실패. 60초 후 재시도합니다.");
            }
        }
    }

private:
    time_t m_spawnTime = 0;
    time_t m_restoreRetryAt = 0;
    bool   m_warningSent = false;
    bool   m_spawned = false;
    bool   m_restoreActiveMarket = false;

    bool RestoreActiveBlackMarket(time_t now)
    {
        QueryResult active = WorldDatabase.Query(
            "SELECT IFNULL(MAX(end_time),0) "
            "FROM blackmarket_auction "
            "WHERE auction_status='active' AND end_time > UNIX_TIMESTAMP()"
        );

        if (!active)
            return false;

        uint32 activeUntil = active->Fetch()[0].GetUInt32();
        if (activeUntil <= uint32(now))
            return false;

        SetBlackMarketActiveUntil(time_t(activeUntil));

        if (RestoreActiveBlackMarketNpc(time_t(activeUntil)))
            m_restoreActiveMarket = false;
        else
        {
            m_restoreActiveMarket = true;
            m_restoreRetryAt = now + 60;
            TC_LOG_ERROR("custom", "[BM] 운영 중 암시장 NPC 복구 실패. 60초 후 재시도합니다: active_until={}", activeUntil);
        }

        TC_LOG_INFO("custom", "[BM] 운영 중 암시장 복구: active_until={}, remaining_sec={}",
                    activeUntil, uint32(activeUntil - uint32(now)));

        return true;
    }

    bool RestoreActiveBlackMarketNpc(time_t activeUntil)
    {
        // 암거상은 필드에 고정 배치되어 있으므로 복구 시에도 소환하지 않는다.
        time_t now = GameTime::GetGameTime();
        uint32 remainingSec = activeUntil > now ? uint32(activeUntil - now) : 1;

        TC_LOG_INFO("custom", "[BM] 운영 중 암시장 복구 완료: 고정 스폰 NPC 사용, remaining_sec={}", remainingSec);
        return true;
    }

    void BroadcastSpawn(Creature* /*npc*/)
    {
        uint32 durationSec = GetBlackMarketDurationSec();
        char const* durationText = GetBlackMarketDurationText();

        SetBlackMarketActiveUntil(GameTime::GetGameTime() + durationSec);

        std::string worldMsg = Trinity::StringFormat(
            "|cffFF0000[암시장 알림]|r 암시장 상인이 왔습니다. {}동안 거래가 가능합니다.",
            durationText
        );

        sWorld->SendServerMessage(
            SERVER_MSG_STRING,
            worldMsg.c_str(),
            0
        );

        if (!IsBlackMarketTestRealm())
        {
            SendFcmCategory(
                3,
                "blackmarket",
                "[암시장] 암시장 시작",
                Trinity::StringFormat("암거상 방문 {}동안 영업합니다.", durationText),
                0
            );
        }
    }

    void BroadcastSpawnGold(Creature* /*npc*/)
    {
        // 암거상 관련 NPC는 필드에 고정 배치되어 있으므로 스크립트에서 삭제하지 않는다.
    }

    bool SpawnNPC()
    {
        // 암거상은 이미 필드에 고정 배치되어 있으므로 여기서는 NPC를 소환하지 않는다.
        // 스케줄 시간이 되면 경매 데이터만 생성하고 포털/공지는 활성화한다.
        // 앱 FCM 알림은 RealmID=2 테스트 서버에서는 발송하지 않는다.
        s_BlackMarketNpcGuid = ObjectGuid::Empty;
        s_BlackMarketNpcMapId = 0;

        uint32 groupId = static_cast<uint32>(GameTime::GetGameTime());

        uint32 created  = 0;
        uint32 attempts = 0;
        const uint32 kMaxAttempts = AUCTION_VISIBLE * 20;
        std::unordered_set<uint32> selectedItems;

        while (created < AUCTION_VISIBLE && attempts < kMaxAttempts)
        {
            ++attempts;

            uint32 catId = BM_CATEGORY_IDS[urand(0, BM_CATEGORY_IDS.size() - 1)];

            std::string qItem = Trinity::StringFormat(
                "SELECT s.game_item_id "
                "FROM appdata.shop_items s "
                "WHERE s.category_id = {} "
                "ORDER BY RAND() LIMIT 1",
                catId
            );

            QueryResult itemR = CharacterDatabase.Query(qItem.c_str());
            if (!itemR)
                continue;

            uint32 item = itemR->Fetch()[0].GetUInt32();
            if (selectedItems.count(item))
                continue;

            {
                std::string qWorldItem = Trinity::StringFormat(
                    "SELECT entry FROM item_template WHERE entry={} LIMIT 1",
                    item
                );

                if (!WorldDatabase.Query(qWorldItem.c_str()))
                    continue;
            }

            selectedItems.insert(item);
            time_t end = GameTime::GetGameTime() + GetBlackMarketDurationSec();

            uint64 minIncRP = MIN_INCREMENT_RP;

            std::string ins = Trinity::StringFormat(
                "INSERT INTO blackmarket_auction "
                "(item_id, start_price, current_bid, bidder_guid, end_time, auction_status, group_id, extend_count, min_increment, bid_count) "
                "VALUES ({}, {}, 0, 0, {}, 'active', {}, 0, {}, 0)",
                item,
                static_cast<uint64_t>(MIN_START_RP),
                static_cast<uint32_t>(end),
                groupId,
                static_cast<uint64_t>(minIncRP)
            );

            WorldDatabase.Execute(ins.c_str());
            ++created;
        }

        if (created < AUCTION_VISIBLE)
        {
            TC_LOG_WARN("custom", "[BM] 경매 아이템을 충분히 채우지 못했습니다: {}/{} (시도횟수 {})",
                        created, AUCTION_VISIBLE, attempts);
        }
        else
        {
            TC_LOG_INFO("custom", "[BM] 경매 아이템 {}개 정상 등록 (시도횟수 {})",
                        created, attempts);
        }

        BroadcastSpawn(nullptr);
        return created > 0;
    }
};

//                            RP Refund Item
// ============================================================================
class item_blackmarket_rp_refund : public ItemScript
{
public:
    item_blackmarket_rp_refund() : ItemScript("item_blackmarket_rp_refund") {}

    bool OnUse(Player* player, Item* item, SpellCastTargets const&) override
    {
        if (!player || !item)
            return false;

        uint32 accountId = player->GetSession()->GetAccountId();
        uint64 itemGuid = item->GetGUID().GetCounter();

        std::string q = Trinity::StringFormat(
            "SELECT auction_id, account_id, bid_rp, status "
            "FROM appdata.blackmarket_rp_bids "
            "WHERE token_item_guid={} LIMIT 1",
            static_cast<uint64_t>(itemGuid)
        );

        uint32 auctionId = 0;
        uint32 logAccId  = 0;
        uint32 bidRP     = 0;
        uint8  status    = 0;

        if (QueryResult r = CharacterDatabase.Query(q.c_str()))
        {
            Field* f = r->Fetch();
            auctionId = f[0].GetUInt32();
            logAccId  = f[1].GetUInt32();
            bidRP     = f[2].GetUInt32();
            status    = f[3].GetUInt8();
        }
        else
        {
            player->GetSession()->SendNotification("이 토큰과 연결된 RP 입찰 기록을 찾을 수 없습니다.");
            return false;
        }

        if (logAccId != accountId)
        {
            player->GetSession()->SendNotification("이 토큰은 다른 계정의 RP 입찰과 연결되어 있습니다.");
            return false;
        }

        if (status != 2)
        {
            player->GetSession()->SendNotification("이미 환급되었거나 사용할 수 없는 토큰입니다.");
            return false;
        }

        if (bidRP == 0)
        {
            player->GetSession()->SendNotification("환급할 RP가 없습니다.");
            return false;
        }

        {
            uint32 nowTs = uint32(GameTime::GetGameTime());

            std::string updLog = Trinity::StringFormat(
                "UPDATE appdata.blackmarket_rp_bids "
                "SET status=3, updated_at={} "
                "WHERE auction_id={} AND account_id={} AND token_item_guid={} AND status=2",
                nowTs,
                auctionId,
                accountId,
                static_cast<uint64_t>(itemGuid)
            );

            CharacterDatabase.Execute(updLog.c_str());

            std::string checkLog = Trinity::StringFormat(
                "SELECT status "
                "FROM appdata.blackmarket_rp_bids "
                "WHERE auction_id={} AND account_id={} AND token_item_guid={} AND status=3 "
                "LIMIT 1",
                auctionId,
                accountId,
                static_cast<uint64_t>(itemGuid)
            );

            if (!CharacterDatabase.Query(checkLog.c_str()))
            {
                player->GetSession()->SendNotification("환급 토큰 처리에 실패했습니다. 다시 시도해주세요.");
                return false;
            }
        }

        {
            std::string updRP = Trinity::StringFormat(
                "UPDATE appdata.account_data SET reg_point = reg_point + {} WHERE id={}",
                bidRP,
                accountId
            );

            CharacterDatabase.Execute(updRP.c_str());
        }

        player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        player->GetSession()->SendNotification("입찰에 사용된 RP가 환급되었습니다.");

        return true;
    }
};

// ============================================================================
//                            Register
// ============================================================================
void AddSC_custom_blackmarket()
{
    new npc_black_market_auctioneer();
    new BlackMarketAuctionScheduler();
    new BlackMarketSpawner();
    new item_blackmarket_rp_refund();
}
