/*
 * Character Trade V7.3
 * TrinityCore 3.3.5a
 *
 * Custom Update - 2026-08-17
 *
 * 기능
 * - 전승룬 AddOn 메뉴에서 판매 / 구매 진입
 * - 판매: 현재 계정 캐릭터 목록, 현재 접속 캐릭터 등록 차단
 * - 판매 등록 시 character_trade_listing 기록 + World::BanCharacter 잠금
 * - 판매 취소 시 status = 2 처리 + World::RemoveBanCharacter 잠금 해제
 * - 구매: 활성 판매 캐릭터 목록 및 캐릭터/가방/은행/업적/소환수/평판/숙련/화폐 상세보기
 * - 소환수 탭: 배운 친구 소환수 + 탈것 표시 및 3D 모델 정보 전송
 * - 판매 등록/구매 결제는 Point만 사용
 * - 구매 완료 시 구매자는 판매가 전액 결제, 판매자는 3% 수수료 차감 후 Point 지급
 * - appdata.character_archive_log 등록 캐릭터는 판매/구매 불가
 * - 구매 목록은 양 진영 모두 표시, 현재 접속 캐릭터와 같은 진영만 구매 가능
 * - 구매 완료 시 characters.account 소유권 이전, 거래 잠금 해제
 * - 구매 완료 시 World::RemoveBanCharacter로 거래 잠금 해제
 *
 * POINT / RPOINT
 * - LoginDatabase: appdata.account_data.point
 * - LoginDatabase: appdata.account_data.reg_point
 */

#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <cstdlib>
#include <ctime>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
    static std::string const CHARACTER_TRADE_ADDON_PREFIX = "CTRADE";
    static std::string const CHARACTER_TRADE_SELL_LIST_COMMAND = "SELL:LIST";
    static std::string const CHARACTER_TRADE_SELL_REGISTER_PREFIX = "SELL:REGISTER:";
    static std::string const CHARACTER_TRADE_SELL_CANCEL_PREFIX = "SELL:CANCEL:";
    static std::string const CHARACTER_TRADE_BUY_LIST_COMMAND = "BUY:LIST";
    static std::string const CHARACTER_TRADE_BUY_PREFIX = "BUY:";
    static std::string const CHARACTER_TRADE_DETAIL_PREFIX = "DETAIL:";
    static std::string const CHARACTER_TRADE_SELF_COMMAND = "SELF";

    static constexpr uint32 CHARACTER_TRADE_MAX_CHARACTERS_PER_ACCOUNT = 10;
    static constexpr uint64 CHARACTER_TRADE_MIN_PRICE = 1;
    static constexpr uint64 CHARACTER_TRADE_MAX_PRICE = 4294967295ULL;
    static constexpr uint64 CHARACTER_TRADE_FEE_PERCENT = 3;
    static constexpr uint64 CHARACTER_TRADE_CANCEL_FEE = 5;

    enum CharacterTradeCurrency : uint8
    {
        CHARACTER_TRADE_CURRENCY_POINT = 1
    };

    struct CharacterTradeBagInfo
    {
        uint8 bagIndex = 0;
        uint32 itemEntry = 0;
        uint32 capacity = 0;
    };

    static std::unordered_set<uint64> CharacterTradeProcessingListings;

    static void RefreshCharacterTradeRealmCounts(uint32 sellerAccount, uint32 buyerAccount)
    {
        if (sellerAccount)
            sWorld->UpdateRealmCharCount(sellerAccount);

        if (buyerAccount && buyerAccount != sellerAccount)
            sWorld->UpdateRealmCharCount(buyerAccount);
    }

    static void SendCharacterTradeAddonMessage(Player* player, std::string const& payload)
    {
        if (!player)
            return;

        player->WhisperAddon(CHARACTER_TRADE_ADDON_PREFIX + "\t" + payload, player);
    }

    static void SendCharacterTradeError(Player* player, std::string const& text)
    {
        SendCharacterTradeAddonMessage(player, "ERROR:" + text);
    }

    static bool ParseCharacterTradeUInt32(std::string const& text, uint32& value)
    {
        value = 0;

        if (text.empty())
            return false;

        char* end = nullptr;
        unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
        if (!end || *end != '\0' || parsed > std::numeric_limits<uint32>::max())
            return false;

        value = uint32(parsed);
        return true;
    }

    static bool ParseCharacterTradeUInt64(std::string const& text, uint64& value)
    {
        value = 0;

        if (text.empty())
            return false;

        char* end = nullptr;
        unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
        if (!end || *end != '\0')
            return false;

        value = uint64(parsed);
        return true;
    }

    static bool GetCharacterTradeAddonPayload(std::string const& message, std::string& payload)
    {
        payload = message;

        if (payload.compare(0, CHARACTER_TRADE_ADDON_PREFIX.size(), CHARACTER_TRADE_ADDON_PREFIX) == 0)
        {
            if (payload.size() <= CHARACTER_TRADE_ADDON_PREFIX.size())
                return false;

            char separator = payload[CHARACTER_TRADE_ADDON_PREFIX.size()];
            if (separator != '\t' && separator != ' ' && separator != ':')
                return false;

            payload = payload.substr(CHARACTER_TRADE_ADDON_PREFIX.size() + 1);
        }

        return !payload.empty();
    }

    static bool ReadCharacterTradePair(std::string const& text, std::string& first, std::string& second)
    {
        size_t split = text.find(':');
        if (split == std::string::npos)
            return false;

        first = text.substr(0, split);
        second = text.substr(split + 1);
        return !first.empty() && !second.empty();
    }

    static bool ReadCharacterTradeTriple(
        std::string const& text,
        std::string& first,
        std::string& second,
        std::string& third)
    {
        size_t firstSplit = text.find(':');
        if (firstSplit == std::string::npos)
            return false;

        size_t secondSplit = text.find(':', firstSplit + 1);
        if (secondSplit == std::string::npos)
            return false;

        first = text.substr(0, firstSplit);
        second = text.substr(firstSplit + 1, secondSplit - firstSplit - 1);
        third = text.substr(secondSplit + 1);

        return !first.empty() && !second.empty() && !third.empty();
    }

    static bool IsCharacterTradeCurrency(uint8 currencyType)
    {
        return currencyType == CHARACTER_TRADE_CURRENCY_POINT;
    }

    static char const* GetCharacterTradeCurrencyName(uint8 currencyType)
    {
        return "Point";
    }

    static std::string SanitizeCharacterTradeField(std::string value)
    {
        for (char& c : value)
        {
            if (c == ':' || c == '\t' || c == '\r' || c == '\n')
                c = ' ';
        }

        return value;
    }

    static uint32 GetCharacterTradeLocale(Player* player)
    {
        if (!player || !player->GetSession())
            return 0;

        uint32 locale = uint32(player->GetSession()->GetSessionDbcLocale());
        return locale < 16 ? locale : 0;
    }

    static std::string GetCharacterTradeFactionName(Player* player, uint32 factionId)
    {
        FactionEntry const* entry = sFactionStore.LookupEntry(factionId);
        if (!entry)
            return "Faction " + std::to_string(factionId);

        uint32 locale = GetCharacterTradeLocale(player);
        char const* name = entry->Name[locale];
        if (!name || !*name)
            name = entry->Name[0];

        return SanitizeCharacterTradeField(
            name && *name ? std::string(name) : "Faction " + std::to_string(factionId));
    }

    static std::string GetCharacterTradeSkillName(Player* player, uint32 skillId)
    {
        SkillLineEntry const* entry = sSkillLineStore.LookupEntry(skillId);
        if (!entry)
            return "Skill " + std::to_string(skillId);

        uint32 locale = GetCharacterTradeLocale(player);
        char const* name = entry->DisplayName[locale];
        if (!name || !*name)
            name = entry->DisplayName[0];

        return SanitizeCharacterTradeField(
            name && *name ? std::string(name) : "Skill " + std::to_string(skillId));
    }

    static uint32 GetCharacterTradeDisplayId(uint8 race, uint8 playerClass, uint8 gender)
    {
        PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race, playerClass);
        if (!info)
            return 0;

        if (gender == GENDER_FEMALE)
            return info->displayId_f;

        if (gender == GENDER_MALE)
            return info->displayId_m;

        return 0;
    }


    static bool IsCharacterTradeListed(uint32 guid)
    {
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT 1 "
            "FROM character_trade_listing "
            "WHERE guid = {} AND status = 0 "
            "LIMIT 1",
            guid);

        return result != nullptr;
    }

    static bool GetCharacterTradeBalances(uint32 accountId, uint64& point, uint64& rpoint)
    {
        point = 0;
        rpoint = 0;

        QueryResult result = LoginDatabase.PQuery(
            "SELECT point, reg_point "
            "FROM appdata.account_data "
            "WHERE id = {} "
            "LIMIT 1",
            accountId);

        if (!result)
            return false;

        Field* fields = result->Fetch();
        point = fields[0].GetUInt64();
        rpoint = fields[1].GetUInt64();
        return true;
    }

    static uint64 GetCharacterTradeSelectedBalance(uint8 currencyType, uint64 point, uint64 rpoint)
    {
        return point;
    }

    static uint64 GetCharacterTradeFee(uint64 price)
    {
        return (price * CHARACTER_TRADE_FEE_PERCENT) / 100;
    }

    static uint64 GetCharacterTradeSellerPayout(uint64 price)
    {
        uint64 const fee = GetCharacterTradeFee(price);
        return price >= fee ? price - fee : 0;
    }

    static void CreditCharacterTradeCurrency(uint32 accountId, uint8 currencyType, uint64 amount)
    {
        LoginDatabase.DirectPExecute(
            "UPDATE appdata.account_data SET point = point + {} WHERE id = {}",
            amount,
            accountId);
    }

    static void DebitCharacterTradeCurrency(uint32 accountId, uint8 currencyType, uint64 amount)
    {
        LoginDatabase.DirectPExecute(
            "UPDATE appdata.account_data "
            "SET point = point - {} "
            "WHERE id = {} AND point >= {}",
            amount,
            accountId,
            amount);
    }

    static void SendCharacterTradeBalance(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        uint64 point = 0;
        uint64 rpoint = 0;
        if (!GetCharacterTradeBalances(player->GetSession()->GetAccountId(), point, rpoint))
        {
            SendCharacterTradeAddonMessage(player, "BALANCE:0:0");
            return;
        }

        SendCharacterTradeAddonMessage(
            player,
            "BALANCE:" + std::to_string(point) + ":0");
    }

    static void SendCharacterTradeSellList(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        uint32 const accountId = player->GetSession()->GetAccountId();
        uint32 const currentGuid = player->GetGUID().GetCounter();

        SendCharacterTradeBalance(player);
        SendCharacterTradeAddonMessage(player, "SELL:CLEAR");

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT c.guid, c.name, c.race, c.class, c.gender, c.level, c.online, "
            "COALESCE(l.id, 0), COALESCE(l.price, 0), COALESCE(l.currency_type, 1), "
            "CASE WHEN archive_log.guid IS NULL THEN 0 ELSE 1 END "
            "FROM characters c "
            "LEFT JOIN character_trade_listing l ON l.guid = c.guid AND l.status = 0 "
            "LEFT JOIN appdata.character_archive_log archive_log ON archive_log.guid = c.guid "
            "WHERE c.account = {} "
            "GROUP BY c.guid, c.name, c.race, c.class, c.gender, c.level, c.online, "
            "l.id, l.price, l.currency_type, archive_log.guid "
            "ORDER BY c.name",
            accountId);

        uint32 count = 0;
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 guid = fields[0].GetUInt32();
                std::string name = fields[1].GetString();
                uint8 race = fields[2].GetUInt8();
                uint8 playerClass = fields[3].GetUInt8();
                uint8 gender = fields[4].GetUInt8();
                uint8 level = fields[5].GetUInt8();
                uint8 online = fields[6].GetUInt8();
                uint64 listingId = fields[7].GetUInt64();
                uint64 price = fields[8].GetUInt64();
                uint8 currencyType = fields[9].GetUInt8();
                bool const archived = fields[10].GetUInt8() != 0;

                uint8 state = 0;
                if (guid == currentGuid)
                    state = 1; // 현재 접속 캐릭터
                else if (listingId != 0)
                    state = 2; // 이미 판매 등록됨 - 취소는 가능
                else if (online != 0)
                    state = 3; // 다른 세션 등에서 접속 중
                else if (archived)
                    state = 4; // character_archive_log 등록 - 판매 불가

                SendCharacterTradeAddonMessage(
                    player,
                    "SELL:ITEM:" + std::to_string(guid) + ":" +
                    name + ":" +
                    std::to_string(race) + ":" +
                    std::to_string(playerClass) + ":" +
                    std::to_string(gender) + ":" +
                    std::to_string(level) + ":" +
                    std::to_string(state) + ":" +
                    std::to_string(currencyType) + ":" +
                    std::to_string(price));

                ++count;
            }
            while (result->NextRow());
        }

        SendCharacterTradeAddonMessage(player, "SELL:END:" + std::to_string(count));
    }

    static bool IsCharacterTradeArchived(uint32 guid)
    {
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT 1 "
            "FROM appdata.character_archive_log "
            "WHERE guid = {} "
            "LIMIT 1",
            guid);

        return result != nullptr;
    }

    static bool HasCharacterTradeBan(uint32 guid)
    {
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT 1 "
            "FROM character_banned "
            "WHERE guid = {} AND active = 1 AND bannedby = 'CharacterTrade' "
            "LIMIT 1",
            guid);

        return result != nullptr;
    }

    static bool HasOtherActiveCharacterBan(uint32 guid)
    {
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT 1 "
            "FROM character_banned "
            "WHERE guid = {} AND active = 1 AND bannedby <> 'CharacterTrade' "
            "LIMIT 1",
            guid);

        return result != nullptr;
    }

    static bool CreateCharacterTradeBan(std::string const& characterName)
    {
        // TrinityCore 기본 .ban character 명령과 동일하게 이름을 정규화한 뒤
        // World::BanCharacter를 사용한다.
        std::string name = characterName;
        if (!normalizePlayerName(name))
            return false;

        BanReturn const result = sWorld->BanCharacter(
            name,
            "3650d",
            "Character trade sale registration",
            "CharacterTrade");

        return result == BAN_SUCCESS;
    }

    static bool RemoveCharacterTradeBan(std::string const& characterName)
    {
        // TrinityCore 기본 HandleUnBanCharacterCommand와 동일한 순서:
        // normalizePlayerName -> sWorld->RemoveBanCharacter
        std::string name = characterName;
        if (!normalizePlayerName(name))
            return false;

        if (!sWorld->RemoveBanCharacter(name))
            return false;

        return true;
    }

    static void HandleCharacterTradeSellRegister(Player* player, uint32 guid, uint8 currencyType, uint64 price)
    {
        if (!player || !player->GetSession())
            return;

        uint32 const sellerAccount = player->GetSession()->GetAccountId();
        uint32 const currentGuid = player->GetGUID().GetCounter();

        if (!guid || currencyType != CHARACTER_TRADE_CURRENCY_POINT
            || price < CHARACTER_TRADE_MIN_PRICE || price > CHARACTER_TRADE_MAX_PRICE)
        {
            SendCharacterTradeError(player, "캐릭터 판매는 Point로만 등록할 수 있습니다.");
            return;
        }

        if (guid == currentGuid)
        {
            SendCharacterTradeError(player, "현재 접속 중인 캐릭터는 판매 등록할 수 없습니다.");
            return;
        }

        QueryResult characterResult = CharacterDatabase.PQuery(
            "SELECT account, name, online "
            "FROM characters "
            "WHERE guid = {} "
            "LIMIT 1",
            guid);

        if (!characterResult)
        {
            SendCharacterTradeError(player, "판매할 캐릭터를 찾을 수 없습니다.");
            return;
        }

        Field* fields = characterResult->Fetch();
        uint32 ownerAccount = fields[0].GetUInt32();
        std::string characterName = fields[1].GetString();
        uint8 online = fields[2].GetUInt8();

        if (ownerAccount != sellerAccount)
        {
            SendCharacterTradeError(player, "현재 계정의 캐릭터만 판매할 수 있습니다.");
            return;
        }

        if (online != 0)
        {
            SendCharacterTradeError(player, "접속 중인 캐릭터는 판매 등록할 수 없습니다.");
            return;
        }

        if (IsCharacterTradeArchived(guid))
        {
            SendCharacterTradeError(
                player,
                "보관 삭제 대상 캐릭터는 판매 등록할 수 없습니다.");
            SendCharacterTradeSellList(player);
            return;
        }

        if (IsCharacterTradeListed(guid))
        {
            SendCharacterTradeError(player, "이미 판매 등록된 캐릭터입니다.");
            SendCharacterTradeSellList(player);
            return;
        }

        if (HasOtherActiveCharacterBan(guid))
        {
            SendCharacterTradeError(player, "현재 제재 중인 캐릭터는 판매 등록할 수 없습니다.");
            return;
        }

        // 이전 테스트 등으로 CharacterTrade 밴만 남아 있고 판매 등록 정보가 없는 경우 정리한다.
        if (HasCharacterTradeBan(guid))
            RemoveCharacterTradeBan(characterName);

        if (!CreateCharacterTradeBan(characterName))
        {
            SendCharacterTradeError(player, "캐릭터 거래 잠금 처리에 실패했습니다.");
            return;
        }

        CharacterDatabase.DirectPExecute(
            "INSERT INTO character_trade_listing "
            "(guid, seller_account, buyer_account, price, currency_type, status, ban_bandate, created_at, sold_at) "
            "VALUES ({}, {}, 0, {}, {}, 0, {}, CURRENT_TIMESTAMP, NULL) "
            "ON DUPLICATE KEY UPDATE "
            "seller_account = VALUES(seller_account), "
            "buyer_account = 0, "
            "price = VALUES(price), "
            "currency_type = VALUES(currency_type), "
            "status = 0, "
            "ban_bandate = VALUES(ban_bandate), "
            "created_at = CURRENT_TIMESTAMP, "
            "sold_at = NULL",
            guid,
            sellerAccount,
            price,
            currencyType,
            0);

        QueryResult listingCheck = CharacterDatabase.PQuery(
            "SELECT 1 "
            "FROM character_trade_listing "
            "WHERE guid = {} AND seller_account = {} AND price = {} AND currency_type = {} AND status = 0 "
            "LIMIT 1",
            guid,
            sellerAccount,
            price,
            currencyType);

        if (!listingCheck)
        {
            RemoveCharacterTradeBan(characterName);
            SendCharacterTradeError(player, "판매 등록 정보를 저장하지 못했습니다.");
            return;
        }

        SendCharacterTradeAddonMessage(
            player,
            "SELL:REGISTER:OK:" + std::to_string(guid) + ":" +
            std::to_string(currencyType) + ":" +
            std::to_string(price) + ":" + characterName);

        SendCharacterTradeSellList(player);
    }

    static void HandleCharacterTradeSellCancel(Player* player, uint32 guid)
    {
        if (!player || !player->GetSession() || !guid)
            return;

        uint32 const sellerAccount = player->GetSession()->GetAccountId();

        QueryResult listingResult = CharacterDatabase.PQuery(
            "SELECT l.id, c.account, c.online, c.name "
            "FROM character_trade_listing l "
            "INNER JOIN characters c ON c.guid = l.guid "
            "WHERE l.guid = {} AND l.seller_account = {} AND l.status = 0 "
            "LIMIT 1",
            guid,
            sellerAccount);

        if (!listingResult)
        {
            SendCharacterTradeError(player, "현재 판매 중인 캐릭터를 찾을 수 없습니다.");
            SendCharacterTradeSellList(player);
            return;
        }

        Field* fields = listingResult->Fetch();
        uint64 listingId = fields[0].GetUInt64();
        uint32 characterAccount = fields[1].GetUInt32();
        uint8 online = fields[2].GetUInt8();
        std::string characterName = fields[3].GetString();

        if (characterAccount != sellerAccount)
        {
            SendCharacterTradeError(player, "현재 계정이 소유한 판매 캐릭터만 취소할 수 있습니다.");
            return;
        }

        if (online != 0)
        {
            SendCharacterTradeError(player, "접속 중인 캐릭터는 판매 취소 처리할 수 없습니다.");
            return;
        }

        uint64 cancelPoint = 0;
        uint64 cancelRPoint = 0;
        if (!GetCharacterTradeBalances(sellerAccount, cancelPoint, cancelRPoint)
            || cancelPoint < CHARACTER_TRADE_CANCEL_FEE)
        {
            SendCharacterTradeError(
                player,
                "판매 취소에는 5 Point가 필요합니다.");
            SendCharacterTradeBalance(player);
            SendCharacterTradeSellList(player);
            return;
        }

        // 구매와 판매 취소가 동시에 진행되지 않도록 동일한 처리 잠금을 사용한다.
        if (!CharacterTradeProcessingListings.insert(listingId).second)
        {
            SendCharacterTradeError(player, "현재 해당 캐릭터의 거래 처리가 진행 중입니다.");
            return;
        }

        // 먼저 status = 4로 예약하여 구매(status = 0 대상)를 차단한다.
        CharacterDatabase.DirectPExecute(
            "UPDATE character_trade_listing "
            "SET status = 4 "
            "WHERE id = {} AND guid = {} AND seller_account = {} AND status = 0",
            listingId,
            guid,
            sellerAccount);

        QueryResult reserveCheck = CharacterDatabase.PQuery(
            "SELECT 1 "
            "FROM character_trade_listing "
            "WHERE id = {} AND guid = {} AND seller_account = {} AND status = 4 "
            "LIMIT 1",
            listingId,
            guid,
            sellerAccount);

        if (!reserveCheck)
        {
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "이미 구매 처리 중이거나 판매가 종료된 캐릭터입니다.");
            SendCharacterTradeSellList(player);
            return;
        }

        // 거래 밴 이외의 관리자 밴이 추가된 경우 임의로 언밴하지 않는다.
        if (HasOtherActiveCharacterBan(guid))
        {
            CharacterDatabase.DirectPExecute(
                "UPDATE character_trade_listing SET status = 0 WHERE id = {} AND status = 4",
                listingId);
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "거래 외 제재가 적용된 캐릭터는 판매 취소로 잠금을 해제할 수 없습니다.");
            return;
        }

        // status = 4 예약 이후 Point가 변했을 수도 있으므로
        // 실제 차감 직전에 다시 확인한다.
        cancelPoint = 0;
        cancelRPoint = 0;
        if (!GetCharacterTradeBalances(sellerAccount, cancelPoint, cancelRPoint)
            || cancelPoint < CHARACTER_TRADE_CANCEL_FEE)
        {
            CharacterDatabase.DirectPExecute(
                "UPDATE character_trade_listing SET status = 0 WHERE id = {} AND status = 4",
                listingId);
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(
                player,
                "판매 취소에는 5 Point가 필요합니다.");
            SendCharacterTradeBalance(player);
            SendCharacterTradeSellList(player);
            return;
        }

        // 판매 등록은 무료지만 판매 취소 시 5 Point를 차감한다.
        DebitCharacterTradeCurrency(
            sellerAccount,
            CHARACTER_TRADE_CURRENCY_POINT,
            CHARACTER_TRADE_CANCEL_FEE);

        bool cancelFeeCharged = true;

        if (HasCharacterTradeBan(guid))
        {
            if (!RemoveCharacterTradeBan(characterName))
            {
                if (cancelFeeCharged)
                {
                    CreditCharacterTradeCurrency(
                        sellerAccount,
                        CHARACTER_TRADE_CURRENCY_POINT,
                        CHARACTER_TRADE_CANCEL_FEE);
                }

                CharacterDatabase.DirectPExecute(
                    "UPDATE character_trade_listing SET status = 0 WHERE id = {} AND status = 4",
                    listingId);
                CharacterTradeProcessingListings.erase(listingId);
                SendCharacterTradeError(player, "캐릭터 거래 잠금 해제에 실패했습니다.");
                SendCharacterTradeBalance(player);
                return;
            }
        }

        // status = 2 : 판매자에 의한 판매 취소
        CharacterDatabase.DirectPExecute(
            "UPDATE character_trade_listing "
            "SET status = 2, buyer_account = 0, sold_at = NULL "
            "WHERE id = {} AND status = 4",
            listingId);

        QueryResult cancelCheck = CharacterDatabase.PQuery(
            "SELECT 1 "
            "FROM character_trade_listing "
            "WHERE id = {} AND guid = {} AND seller_account = {} AND status = 2 "
            "LIMIT 1",
            listingId,
            guid,
            sellerAccount);

        if (!cancelCheck)
        {
            // DB 취소 기록 실패 시 거래 잠금과 취소 수수료를 원상복구한다.
            CreateCharacterTradeBan(characterName);

            if (cancelFeeCharged)
            {
                CreditCharacterTradeCurrency(
                    sellerAccount,
                    CHARACTER_TRADE_CURRENCY_POINT,
                    CHARACTER_TRADE_CANCEL_FEE);
            }

            CharacterDatabase.DirectPExecute(
                "UPDATE character_trade_listing SET status = 0 WHERE id = {} AND status = 4",
                listingId);
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "판매 취소 정보를 저장하지 못해 기존 판매 상태로 복구했습니다.");
            SendCharacterTradeBalance(player);
            return;
        }

        CharacterTradeProcessingListings.erase(listingId);

        SendCharacterTradeAddonMessage(
            player,
            "SELL:CANCEL:OK:" + std::to_string(guid) + ":" + characterName);

        SendCharacterTradeBalance(player);
        SendCharacterTradeSellList(player);
    }

    static void SendCharacterTradeBuyList(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        uint32 const buyerAccount = player->GetSession()->GetAccountId();
        uint32 const buyerTeam = player->GetTeam();

        SendCharacterTradeBalance(player);
        SendCharacterTradeAddonMessage(player, "BUY:CLEAR");

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT l.id, l.guid, l.price, l.currency_type, l.seller_account, "
            "c.name, c.race, c.class, c.gender, c.level "
            "FROM character_trade_listing l "
            "INNER JOIN characters c ON c.guid = l.guid "
            "WHERE l.status = 0 AND l.currency_type = 1 "
            "AND NOT EXISTS ("
                "SELECT 1 "
                "FROM appdata.character_archive_log archive_log "
                "WHERE archive_log.guid = l.guid"
            ") "
            "ORDER BY l.id DESC "
            "LIMIT 100");

        uint32 count = 0;
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint64 listingId = fields[0].GetUInt64();
                uint32 guid = fields[1].GetUInt32();
                uint64 price = fields[2].GetUInt64();
                uint8 currencyType = fields[3].GetUInt8();
                uint32 sellerAccount = fields[4].GetUInt32();
                std::string name = fields[5].GetString();
                uint8 race = fields[6].GetUInt8();
                uint8 playerClass = fields[7].GetUInt8();
                uint8 gender = fields[8].GetUInt8();
                uint8 level = fields[9].GetUInt8();
                uint8 isOwnListing = sellerAccount == buyerAccount ? 1 : 0;
                uint8 sameFaction =
                    Player::TeamForRace(race) == buyerTeam ? 1 : 0;

                SendCharacterTradeAddonMessage(
                    player,
                    "BUY:ITEM:" + std::to_string(listingId) + ":" +
                    std::to_string(guid) + ":" +
                    std::to_string(currencyType) + ":" +
                    std::to_string(price) + ":" +
                    std::to_string(isOwnListing) + ":" +
                    name + ":" +
                    std::to_string(race) + ":" +
                    std::to_string(playerClass) + ":" +
                    std::to_string(gender) + ":" +
                    std::to_string(level) + ":" +
                    std::to_string(sameFaction));

                ++count;
            }
            while (result->NextRow());
        }

        SendCharacterTradeAddonMessage(player, "BUY:END:" + std::to_string(count));
    }

    static void SendCharacterTradeEquipment(Player* player, uint32 guid)
    {
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT ci.slot, ii.itemEntry "
            "FROM character_inventory ci "
            "INNER JOIN item_instance ii ON ii.guid = ci.item "
            "WHERE ci.guid = {} "
            "AND ci.bag = 0 "
            "AND ci.slot < {} "
            "ORDER BY ci.slot",
            guid,
            uint32(EQUIPMENT_SLOT_END));

        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            uint32 slot = fields[0].GetUInt32();
            uint32 itemEntry = fields[1].GetUInt32();

            SendCharacterTradeAddonMessage(
                player,
                "DETAIL:EQUIP:" + std::to_string(slot) + ":" + std::to_string(itemEntry));
        }
        while (result->NextRow());
    }

    static void SendCharacterTradeAmmo(Player* player, uint32 guid)
    {
        uint32 ammoId = 0;
        uint64 ammoCount = 0;

        QueryResult ammoResult = CharacterDatabase.PQuery(
            "SELECT ammoId "
            "FROM characters "
            "WHERE guid = {} "
            "LIMIT 1",
            guid);

        if (ammoResult)
            ammoId = ammoResult->Fetch()[0].GetUInt32();

        if (ammoId)
        {
            // 현재 캐릭터가 실제로 휴대 중인 탄약만 합산한다.
            // 1) 기본 가방(backpack)
            // 2) 현재 장착된 4개 가방 내부
            QueryResult countResult = CharacterDatabase.PQuery(
                "SELECT COALESCE(SUM(ii.count), 0) "
                "FROM character_inventory ci "
                "INNER JOIN item_instance ii ON ii.guid = ci.item "
                "LEFT JOIN character_inventory bagSlot "
                "ON bagSlot.guid = ci.guid "
                "AND bagSlot.bag = 0 "
                "AND bagSlot.item = ci.bag "
                "AND bagSlot.slot >= {} "
                "AND bagSlot.slot < {} "
                "WHERE ci.guid = {} "
                "AND ii.itemEntry = {} "
                "AND ("
                    "(ci.bag = 0 AND ci.slot >= {} AND ci.slot < {}) "
                    "OR bagSlot.item IS NOT NULL"
                ")",
                uint32(INVENTORY_SLOT_BAG_START),
                uint32(INVENTORY_SLOT_BAG_END),
                guid,
                ammoId,
                uint32(INVENTORY_SLOT_ITEM_START),
                uint32(INVENTORY_SLOT_ITEM_END));

            if (countResult)
                ammoCount = countResult->Fetch()[0].GetUInt64();
        }

        SendCharacterTradeAddonMessage(
            player,
            "DETAIL:AMMO:" +
            std::to_string(ammoId) + ":" +
            std::to_string(ammoCount));
    }

    static void SendCharacterTradeBags(Player* player, uint32 guid)
    {
        uint32 backpackCapacity = uint32(INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START);
        SendCharacterTradeAddonMessage(
            player,
            "DETAIL:BAG:0:" + std::to_string(backpackCapacity) + ":0");

        std::unordered_map<uint64, CharacterTradeBagInfo> bagByItemGuid;

        QueryResult bagResult = CharacterDatabase.PQuery(
            "SELECT ci.slot, ii.guid, ii.itemEntry "
            "FROM character_inventory ci "
            "INNER JOIN item_instance ii ON ii.guid = ci.item "
            "WHERE ci.guid = {} "
            "AND ci.bag = 0 "
            "AND ci.slot >= {} "
            "AND ci.slot < {} "
            "ORDER BY ci.slot",
            guid,
            uint32(INVENTORY_SLOT_BAG_START),
            uint32(INVENTORY_SLOT_BAG_END));

        if (bagResult)
        {
            do
            {
                Field* fields = bagResult->Fetch();
                uint32 inventorySlot = fields[0].GetUInt32();
                uint64 bagItemGuid = fields[1].GetUInt64();
                uint32 bagItemEntry = fields[2].GetUInt32();
                uint8 bagIndex = uint8(inventorySlot - uint32(INVENTORY_SLOT_BAG_START) + 1);

                uint32 capacity = 0;
                if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(bagItemEntry))
                    capacity = proto->ContainerSlots;

                CharacterTradeBagInfo info;
                info.bagIndex = bagIndex;
                info.itemEntry = bagItemEntry;
                info.capacity = capacity;
                bagByItemGuid[bagItemGuid] = info;

                SendCharacterTradeAddonMessage(
                    player,
                    "DETAIL:BAG:" + std::to_string(bagIndex) + ":" +
                    std::to_string(capacity) + ":" +
                    std::to_string(bagItemEntry));
            }
            while (bagResult->NextRow());
        }

        QueryResult backpackResult = CharacterDatabase.PQuery(
            "SELECT ci.slot, ii.itemEntry, ii.count, ii.durability "
            "FROM character_inventory ci "
            "INNER JOIN item_instance ii ON ii.guid = ci.item "
            "WHERE ci.guid = {} "
            "AND ci.bag = 0 "
            "AND ci.slot >= {} "
            "AND ci.slot < {} "
            "ORDER BY ci.slot",
            guid,
            uint32(INVENTORY_SLOT_ITEM_START),
            uint32(INVENTORY_SLOT_ITEM_END));

        if (backpackResult)
        {
            do
            {
                Field* fields = backpackResult->Fetch();
                uint32 inventorySlot = fields[0].GetUInt32();
                uint32 itemEntry = fields[1].GetUInt32();
                uint32 count = fields[2].GetUInt32();
                uint32 durability = fields[3].GetUInt32();
                uint32 maxDurability = 0;

                if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry))
                    maxDurability = proto->MaxDurability;

                uint32 logicalSlot = inventorySlot - uint32(INVENTORY_SLOT_ITEM_START) + 1;

                SendCharacterTradeAddonMessage(
                    player,
                    "DETAIL:ITEM:0:" + std::to_string(logicalSlot) + ":" +
                    std::to_string(itemEntry) + ":" +
                    std::to_string(count) + ":" +
                    std::to_string(durability) + ":" +
                    std::to_string(maxDurability));
            }
            while (backpackResult->NextRow());
        }

        QueryResult contentResult = CharacterDatabase.PQuery(
            "SELECT ci.bag, ci.slot, ii.itemEntry, ii.count, ii.durability "
            "FROM character_inventory ci "
            "INNER JOIN item_instance ii ON ii.guid = ci.item "
            "WHERE ci.guid = {} "
            "AND ci.bag <> 0 "
            "ORDER BY ci.bag, ci.slot",
            guid);

        if (!contentResult)
            return;

        do
        {
            Field* fields = contentResult->Fetch();
            uint64 bagItemGuid = fields[0].GetUInt64();
            uint32 bagSlot = fields[1].GetUInt32();
            uint32 itemEntry = fields[2].GetUInt32();
            uint32 count = fields[3].GetUInt32();
            uint32 durability = fields[4].GetUInt32();

            auto bagItr = bagByItemGuid.find(bagItemGuid);
            if (bagItr == bagByItemGuid.end())
                continue;

            uint32 maxDurability = 0;
            if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry))
                maxDurability = proto->MaxDurability;

            uint32 logicalSlot = bagSlot + 1;

            SendCharacterTradeAddonMessage(
                player,
                "DETAIL:ITEM:" + std::to_string(bagItr->second.bagIndex) + ":" +
                std::to_string(logicalSlot) + ":" +
                std::to_string(itemEntry) + ":" +
                std::to_string(count) + ":" +
                std::to_string(durability) + ":" +
                std::to_string(maxDurability));
        }
        while (contentResult->NextRow());
    }

    static void SendCharacterTradeBank(Player* player, uint32 guid)
    {
        uint32 const bankCapacity =
            uint32(BANK_SLOT_ITEM_END - BANK_SLOT_ITEM_START);

        SendCharacterTradeAddonMessage(
            player,
            "DETAIL:BANK:0:" +
            std::to_string(bankCapacity) +
            ":0");

        std::unordered_map<uint64, CharacterTradeBagInfo> bankBagByItemGuid;

        QueryResult bankBagResult = CharacterDatabase.PQuery(
            "SELECT ci.slot, ii.guid, ii.itemEntry "
            "FROM character_inventory ci "
            "INNER JOIN item_instance ii ON ii.guid = ci.item "
            "WHERE ci.guid = {} "
            "AND ci.bag = 0 "
            "AND ci.slot >= {} "
            "AND ci.slot < {} "
            "ORDER BY ci.slot",
            guid,
            uint32(BANK_SLOT_BAG_START),
            uint32(BANK_SLOT_BAG_END));

        if (bankBagResult)
        {
            do
            {
                Field* fields = bankBagResult->Fetch();
                uint32 bankBagSlot = fields[0].GetUInt32();
                uint64 bagItemGuid = fields[1].GetUInt64();
                uint32 bagItemEntry = fields[2].GetUInt32();

                uint8 bankBagIndex = uint8(
                    bankBagSlot - uint32(BANK_SLOT_BAG_START) + 1);

                uint32 capacity = 0;
                if (ItemTemplate const* proto =
                        sObjectMgr->GetItemTemplate(bagItemEntry))
                {
                    capacity = proto->ContainerSlots;
                }

                CharacterTradeBagInfo info;
                info.bagIndex = bankBagIndex;
                info.itemEntry = bagItemEntry;
                info.capacity = capacity;
                bankBagByItemGuid[bagItemGuid] = info;

                SendCharacterTradeAddonMessage(
                    player,
                    "DETAIL:BANK:" +
                    std::to_string(bankBagIndex) + ":" +
                    std::to_string(capacity) + ":" +
                    std::to_string(bagItemEntry));
            }
            while (bankBagResult->NextRow());
        }

        QueryResult bankItemResult = CharacterDatabase.PQuery(
            "SELECT ci.slot, ii.itemEntry, ii.count, ii.durability "
            "FROM character_inventory ci "
            "INNER JOIN item_instance ii ON ii.guid = ci.item "
            "WHERE ci.guid = {} "
            "AND ci.bag = 0 "
            "AND ci.slot >= {} "
            "AND ci.slot < {} "
            "ORDER BY ci.slot",
            guid,
            uint32(BANK_SLOT_ITEM_START),
            uint32(BANK_SLOT_ITEM_END));

        if (bankItemResult)
        {
            do
            {
                Field* fields = bankItemResult->Fetch();
                uint32 bankSlot = fields[0].GetUInt32();
                uint32 itemEntry = fields[1].GetUInt32();
                uint32 count = fields[2].GetUInt32();
                uint32 durability = fields[3].GetUInt32();

                uint32 maxDurability = 0;
                if (ItemTemplate const* proto =
                        sObjectMgr->GetItemTemplate(itemEntry))
                {
                    maxDurability = proto->MaxDurability;
                }

                uint32 logicalSlot =
                    bankSlot - uint32(BANK_SLOT_ITEM_START) + 1;

                SendCharacterTradeAddonMessage(
                    player,
                    "DETAIL:BANKITEM:0:" +
                    std::to_string(logicalSlot) + ":" +
                    std::to_string(itemEntry) + ":" +
                    std::to_string(count) + ":" +
                    std::to_string(durability) + ":" +
                    std::to_string(maxDurability));
            }
            while (bankItemResult->NextRow());
        }

        QueryResult bankContentResult = CharacterDatabase.PQuery(
            "SELECT ci.bag, ci.slot, ii.itemEntry, ii.count, ii.durability "
            "FROM character_inventory ci "
            "INNER JOIN item_instance ii ON ii.guid = ci.item "
            "WHERE ci.guid = {} "
            "AND ci.bag <> 0 "
            "ORDER BY ci.bag, ci.slot",
            guid);

        if (!bankContentResult)
            return;

        do
        {
            Field* fields = bankContentResult->Fetch();
            uint64 bagItemGuid = fields[0].GetUInt64();
            uint32 bagSlot = fields[1].GetUInt32();
            uint32 itemEntry = fields[2].GetUInt32();
            uint32 count = fields[3].GetUInt32();
            uint32 durability = fields[4].GetUInt32();

            auto bagItr = bankBagByItemGuid.find(bagItemGuid);
            if (bagItr == bankBagByItemGuid.end())
                continue;

            uint32 maxDurability = 0;
            if (ItemTemplate const* proto =
                    sObjectMgr->GetItemTemplate(itemEntry))
            {
                maxDurability = proto->MaxDurability;
            }

            uint32 logicalSlot = bagSlot + 1;

            SendCharacterTradeAddonMessage(
                player,
                "DETAIL:BANKITEM:" +
                std::to_string(bagItr->second.bagIndex) + ":" +
                std::to_string(logicalSlot) + ":" +
                std::to_string(itemEntry) + ":" +
                std::to_string(count) + ":" +
                std::to_string(durability) + ":" +
                std::to_string(maxDurability));
        }
        while (bankContentResult->NextRow());
    }

    static void SendCharacterTradeStats(Player* player, uint32 guid)
    {
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT maxhealth, maxpower1, strength, agility, stamina, intellect, spirit, armor, "
            "blockPct, dodgePct, parryPct, critPct, rangedCritPct, spellCritPct, "
            "attackPower, rangedAttackPower, spellPower, resilience "
            "FROM character_stats "
            "WHERE guid = {} "
            "LIMIT 1",
            guid);

        if (!result)
            return;

        Field* fields = result->Fetch();

        SendCharacterTradeAddonMessage(
            player,
            "DETAIL:STAT:" +
            std::to_string(fields[0].GetUInt32()) + ":" +
            std::to_string(fields[1].GetUInt32()) + ":" +
            std::to_string(fields[2].GetUInt32()) + ":" +
            std::to_string(fields[3].GetUInt32()) + ":" +
            std::to_string(fields[4].GetUInt32()) + ":" +
            std::to_string(fields[5].GetUInt32()) + ":" +
            std::to_string(fields[6].GetUInt32()) + ":" +
            std::to_string(fields[7].GetUInt32()) + ":" +
            std::to_string(fields[8].GetFloat()) + ":" +
            std::to_string(fields[9].GetFloat()) + ":" +
            std::to_string(fields[10].GetFloat()) + ":" +
            std::to_string(fields[11].GetFloat()) + ":" +
            std::to_string(fields[12].GetFloat()) + ":" +
            std::to_string(fields[13].GetFloat()) + ":" +
            std::to_string(fields[14].GetUInt32()) + ":" +
            std::to_string(fields[15].GetUInt32()) + ":" +
            std::to_string(fields[16].GetUInt32()) + ":" +
            std::to_string(fields[17].GetUInt32()));
    }

    static uint32 GetCharacterTradeCreatureDisplayId(
        uint32 creatureEntry,
        int32 modelSelector = 0)
    {
        if (!creatureEntry)
            return 0;

        QueryResult result = WorldDatabase.PQuery(
            "SELECT modelid1, modelid2, modelid3, modelid4 "
            "FROM creature_template "
            "WHERE entry = {} "
            "LIMIT 1",
            creatureEntry);

        if (!result)
            return 0;

        Field* fields = result->Fetch();

        uint32 model1 = fields[0].GetUInt32();
        uint32 model2 = fields[1].GetUInt32();
        uint32 model3 = fields[2].GetUInt32();
        uint32 model4 = fields[3].GetUInt32();

        // TrinityCore의 SPELL_AURA_MOUNTED 처리와 동일하게
        // MiscValueB == 2이면 modelid2를 우선 사용한다.
        if (modelSelector == 2 && model2)
            return model2;

        if (model1)
            return model1;
        if (model2)
            return model2;
        if (model3)
            return model3;
        if (model4)
            return model4;

        return 0;
    }

    static uint32 GetCharacterTradeCompanionCreatureEntry(
        SpellInfo const* spellInfo)
    {
        if (!spellInfo)
            return 0;

        for (SpellEffectInfo const& effect : spellInfo->GetEffects())
        {
            if (!effect.IsEffect(SPELL_EFFECT_SUMMON))
                continue;

            if (effect.MiscValue <= 0)
                continue;

            uint32 const creatureEntry = uint32(effect.MiscValue);

            if (sObjectMgr->GetCreatureTemplate(creatureEntry))
                return creatureEntry;
        }

        // 일부 커스텀 친구 소환수 대응:
        // 양수 MiscValue가 실제 creature_template entry인 경우 사용.
        for (SpellEffectInfo const& effect : spellInfo->GetEffects())
        {
            if (effect.MiscValue <= 0)
                continue;

            uint32 const creatureEntry = uint32(effect.MiscValue);

            if (sObjectMgr->GetCreatureTemplate(creatureEntry))
                return creatureEntry;
        }

        return 0;
    }

    static uint32 GetCharacterTradeMountCreatureEntry(
        SpellInfo const* spellInfo)
    {
        if (!spellInfo)
            return 0;

        for (SpellEffectInfo const& effect : spellInfo->GetEffects())
        {
            if (!effect.IsAura(SPELL_AURA_MOUNTED))
                continue;

            if (effect.MiscValue <= 0)
                continue;

            uint32 const creatureEntry = uint32(effect.MiscValue);

            if (sObjectMgr->GetCreatureTemplate(creatureEntry))
                return creatureEntry;
        }

        return 0;
    }

    static uint32 GetCharacterTradeCompanionDisplayId(
        SpellInfo const* spellInfo)
    {
        if (!spellInfo)
            return 0;

        // 친구 소환수 주문은 보통 SPELL_EFFECT_SUMMON의 MiscValue가
        // creature_template.entry를 가리킨다.
        for (SpellEffectInfo const& effect : spellInfo->GetEffects())
        {
            if (!effect.IsEffect(SPELL_EFFECT_SUMMON))
                continue;

            if (effect.MiscValue <= 0)
                continue;

            uint32 displayId = GetCharacterTradeCreatureDisplayId(
                uint32(effect.MiscValue));

            if (displayId)
                return displayId;
        }

        // 커스텀/특수 주문 대응:
        // SUMMON 효과가 아닌 경우에도 양수 MiscValue가 실제
        // creature_template entry라면 모델을 사용할 수 있게 한다.
        for (SpellEffectInfo const& effect : spellInfo->GetEffects())
        {
            if (effect.MiscValue <= 0)
                continue;

            uint32 displayId = GetCharacterTradeCreatureDisplayId(
                uint32(effect.MiscValue));

            if (displayId)
                return displayId;
        }

        return 0;
    }

    static uint32 GetCharacterTradeMountDisplayId(
        SpellInfo const* spellInfo)
    {
        if (!spellInfo)
            return 0;

        // TrinityCore 3.3.5의 HandleAuraMounted와 동일한 구조:
        // SPELL_AURA_MOUNTED의 MiscValue = creature_template.entry
        // MiscValueB는 일부 탈것의 modelid 선택에 사용된다.
        for (SpellEffectInfo const& effect : spellInfo->GetEffects())
        {
            if (!effect.IsAura(SPELL_AURA_MOUNTED))
                continue;

            if (effect.MiscValue <= 0)
                continue;

            uint32 displayId = GetCharacterTradeCreatureDisplayId(
                uint32(effect.MiscValue),
                effect.MiscValueB);

            if (displayId)
                return displayId;
        }

        return 0;
    }

    static void SendCharacterTradeSummons(Player* player, uint32 guid)
    {
        // WoW 3.3.5 DBC 기준
        // SkillLine 777 = Mounts
        // SkillLine 778 = Companions
        //
        // 이전처럼 주문 효과와 소환 대상 CreatureType을 추측하지 않고,
        // SkillLineAbility.dbc의 실제 분류를 그대로 사용한다.
        std::unordered_set<uint32> companionSpells;
        std::unordered_set<uint32> mountSpells;

        for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
        {
            SkillLineAbilityEntry const* skillLine =
                sSkillLineAbilityStore.LookupEntry(i);

            if (!skillLine || !skillLine->Spell)
                continue;

            if (skillLine->SkillLine == 778)
                companionSpells.insert(skillLine->Spell);
            else if (skillLine->SkillLine == 777)
                mountSpells.insert(skillLine->Spell);
        }

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT spell "
            "FROM character_spell "
            "WHERE guid = {} "
            "AND disabled = 0 "
            "ORDER BY spell",
            guid);

        if (!result)
            return;

        do
        {
            uint32 spellId = result->Fetch()[0].GetUInt32();

            SpellInfo const* spellInfo =
                sSpellMgr->GetSpellInfo(spellId);

            if (companionSpells.find(spellId) != companionSpells.end())
            {
                uint32 const creatureEntry =
                    GetCharacterTradeCompanionCreatureEntry(spellInfo);

                uint32 const displayId =
                    GetCharacterTradeCompanionDisplayId(spellInfo);

                SendCharacterTradeAddonMessage(
                    player,
                    "DETAIL:COMPANION:" +
                    std::to_string(spellId) + ":" +
                    std::to_string(creatureEntry) + ":" +
                    std::to_string(displayId));
                continue;
            }

            if (mountSpells.find(spellId) != mountSpells.end())
            {
                uint32 const creatureEntry =
                    GetCharacterTradeMountCreatureEntry(spellInfo);

                uint32 const displayId =
                    GetCharacterTradeMountDisplayId(spellInfo);

                SendCharacterTradeAddonMessage(
                    player,
                    "DETAIL:MOUNT:" +
                    std::to_string(spellId) + ":" +
                    std::to_string(creatureEntry) + ":" +
                    std::to_string(displayId));
            }
        }
        while (result->NextRow());
    }

    static void SendCharacterTradeReputation(Player* player, uint32 guid)
    {
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT faction, standing, flags "
            "FROM character_reputation "
            "WHERE guid = {} "
            "ORDER BY faction",
            guid);

        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            uint32 factionId = fields[0].GetUInt32();
            int32 standing = fields[1].GetInt32();
            uint32 flags = fields[2].GetUInt32();

            SendCharacterTradeAddonMessage(
                player,
                "DETAIL:REP:" +
                std::to_string(factionId) + ":" +
                std::to_string(standing) + ":" +
                std::to_string(flags) + ":" +
                GetCharacterTradeFactionName(player, factionId));
        }
        while (result->NextRow());
    }

    static void SendCharacterTradeSkills(Player* player, uint32 guid)
    {
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT skill, value, max "
            "FROM character_skills "
            "WHERE guid = {} "
            "ORDER BY skill",
            guid);

        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            uint32 skillId = fields[0].GetUInt32();
            uint32 value = fields[1].GetUInt32();
            uint32 maxValue = fields[2].GetUInt32();

            SendCharacterTradeAddonMessage(
                player,
                "DETAIL:SKILL:" +
                std::to_string(skillId) + ":" +
                std::to_string(value) + ":" +
                std::to_string(maxValue) + ":" +
                GetCharacterTradeSkillName(player, skillId));
        }
        while (result->NextRow());
    }

    static void SendCharacterTradeCurrencies(Player* player, uint32 guid)
    {
        QueryResult characterResult = CharacterDatabase.PQuery(
            "SELECT totalHonorPoints, arenaPoints "
            "FROM characters "
            "WHERE guid = {} "
            "LIMIT 1",
            guid);

        if (characterResult)
        {
            Field* fields = characterResult->Fetch();

            SendCharacterTradeAddonMessage(
                player,
                "DETAIL:CUR:H:" + std::to_string(fields[0].GetUInt32()));

            SendCharacterTradeAddonMessage(
                player,
                "DETAIL:CUR:A:" + std::to_string(fields[1].GetUInt32()));
        }

        std::unordered_set<uint32> currencyItems;
        for (uint32 i = 0; i < sCurrencyTypesStore.GetNumRows(); ++i)
        {
            CurrencyTypesEntry const* currency = sCurrencyTypesStore.LookupEntry(i);
            if (currency && currency->ItemID)
                currencyItems.insert(currency->ItemID);
        }

        if (currencyItems.empty())
            return;

        QueryResult itemResult = CharacterDatabase.PQuery(
            "SELECT ii.itemEntry, SUM(ii.count) "
            "FROM character_inventory ci "
            "INNER JOIN item_instance ii ON ii.guid = ci.item "
            "WHERE ci.guid = {} "
            "GROUP BY ii.itemEntry "
            "ORDER BY ii.itemEntry",
            guid);

        if (!itemResult)
            return;

        do
        {
            Field* fields = itemResult->Fetch();
            uint32 itemEntry = fields[0].GetUInt32();

            if (currencyItems.find(itemEntry) == currencyItems.end())
                continue;

            uint64 count = fields[1].GetUInt64();
            if (!count)
                continue;

            SendCharacterTradeAddonMessage(
                player,
                "DETAIL:CUR:I:" +
                std::to_string(itemEntry) + ":" +
                std::to_string(count));
        }
        while (itemResult->NextRow());
    }


    static void FlushCharacterTradeAchievementBatch(Player* player, std::string& batch)
    {
        if (!player || batch.empty())
            return;

        SendCharacterTradeAddonMessage(player, "DETAIL:ACHB:" + batch);
        batch.clear();
    }

    static void SendCharacterTradeAchievements(Player* player, uint32 guid)
    {
        QueryResult result = CharacterDatabase.PQuery(
            "SELECT achievement, date "
            "FROM character_achievement "
            "WHERE guid = {} "
            "ORDER BY date DESC, achievement",
            guid);

        if (!result)
            return;

        std::string batch;
        do
        {
            Field* fields = result->Fetch();
            uint32 achievementId = fields[0].GetUInt32();
            uint32 completedAt = fields[1].GetUInt32();

            std::string pair = std::to_string(achievementId) + "," + std::to_string(completedAt);
            if (!batch.empty() && batch.size() + pair.size() + 1 > 180)
                FlushCharacterTradeAchievementBatch(player, batch);

            if (!batch.empty())
                batch += ";";

            batch += pair;
        }
        while (result->NextRow());

        FlushCharacterTradeAchievementBatch(player, batch);
    }


    static void SendCharacterTradeDetail(Player* player, uint32 guid, bool allowUnlisted)
    {
        if (!player || !guid)
            return;

        if (!allowUnlisted && !IsCharacterTradeListed(guid))
        {
            SendCharacterTradeError(player, "판매 중인 캐릭터가 아닙니다.");
            return;
        }

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT name, race, class, gender, level, money "
            "FROM characters "
            "WHERE guid = {} "
            "LIMIT 1",
            guid);

        if (!result)
        {
            SendCharacterTradeError(player, "캐릭터 정보를 찾을 수 없습니다.");
            return;
        }

        Field* fields = result->Fetch();
        std::string name = fields[0].GetString();
        uint8 race = fields[1].GetUInt8();
        uint8 playerClass = fields[2].GetUInt8();
        uint8 gender = fields[3].GetUInt8();
        uint8 level = fields[4].GetUInt8();
        uint64 money = fields[5].GetUInt64();
        uint32 displayId = GetCharacterTradeDisplayId(race, playerClass, gender);

        SendCharacterTradeAddonMessage(player, "DETAIL:BEGIN:" + std::to_string(guid));
        SendCharacterTradeAddonMessage(
            player,
            "DETAIL:CHAR:" + std::to_string(guid) + ":" +
            name + ":" +
            std::to_string(race) + ":" +
            std::to_string(playerClass) + ":" +
            std::to_string(gender) + ":" +
            std::to_string(level) + ":" +
            std::to_string(money) + ":" +
            std::to_string(displayId));

        SendCharacterTradeEquipment(player, guid);
        SendCharacterTradeAmmo(player, guid);
        SendCharacterTradeBags(player, guid);
        SendCharacterTradeBank(player, guid);
        SendCharacterTradeAchievements(player, guid);
        SendCharacterTradeStats(player, guid);
        SendCharacterTradeSummons(player, guid);
        SendCharacterTradeReputation(player, guid);
        SendCharacterTradeSkills(player, guid);
        SendCharacterTradeCurrencies(player, guid);

        SendCharacterTradeAddonMessage(player, "DETAIL:END:" + std::to_string(guid));
    }

    static void RestoreCharacterTradeListing(uint64 listingId)
    {
        CharacterDatabase.DirectPExecute(
            "UPDATE character_trade_listing "
            "SET status = 0, buyer_account = 0, sold_at = NULL "
            "WHERE id = {} AND status = 3",
            listingId);
    }

    static void HandleCharacterTradeBuy(Player* player, uint64 listingId)
    {
        if (!player || !player->GetSession() || !listingId)
            return;

        if (!CharacterTradeProcessingListings.insert(listingId).second)
        {
            SendCharacterTradeError(player, "현재 다른 구매 처리가 진행 중입니다.");
            return;
        }

        uint32 const buyerAccount = player->GetSession()->GetAccountId();

        QueryResult listingResult = CharacterDatabase.PQuery(
            "SELECT l.guid, l.seller_account, l.price, l.currency_type, "
            "c.account, c.online, c.name, c.race "
            "FROM character_trade_listing l "
            "INNER JOIN characters c ON c.guid = l.guid "
            "WHERE l.id = {} AND l.status = 0 AND l.currency_type = 1 "
            "AND NOT EXISTS ("
                "SELECT 1 "
                "FROM appdata.character_archive_log archive_log "
                "WHERE archive_log.guid = l.guid"
            ") "
            "LIMIT 1",
            listingId);

        if (!listingResult)
        {
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "판매가 종료되었거나 판매할 수 없는 캐릭터입니다.");
            return;
        }

        Field* fields = listingResult->Fetch();
        uint32 guid = fields[0].GetUInt32();
        uint32 sellerAccount = fields[1].GetUInt32();
        uint64 price = fields[2].GetUInt64();
        uint64 const tradeFee = GetCharacterTradeFee(price);
        uint64 const sellerPayout = GetCharacterTradeSellerPayout(price);
        uint8 currencyType = fields[3].GetUInt8();
        uint32 characterAccount = fields[4].GetUInt32();
        uint8 online = fields[5].GetUInt8();
        std::string characterName = fields[6].GetString();
        uint8 characterRace = fields[7].GetUInt8();

        if (sellerAccount == buyerAccount)
        {
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "자신의 계정에 등록된 캐릭터는 구매할 수 없습니다.");
            return;
        }

        if (characterAccount != sellerAccount || online != 0)
        {
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "현재 캐릭터 소유권 또는 접속 상태가 변경되어 구매할 수 없습니다.");
            return;
        }

        // 구매 목록은 양 진영 모두 보여주되 실제 구매는
        // 현재 접속 중인 캐릭터와 같은 진영만 허용한다.
        if (Player::TeamForRace(characterRace) != player->GetTeam())
        {
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(
                player,
                "현재 접속 캐릭터와 다른 진영의 캐릭터는 구매할 수 없습니다.");
            return;
        }

        if (HasOtherActiveCharacterBan(guid))
        {
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "판매 캐릭터에 거래 외 제재가 적용되어 구매할 수 없습니다.");
            return;
        }

        if (!HasCharacterTradeBan(guid))
        {
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "판매 캐릭터의 거래 잠금 상태가 확인되지 않습니다.");
            return;
        }

        QueryResult countResult = CharacterDatabase.PQuery(
            "SELECT COUNT(*) FROM characters WHERE account = {}",
            buyerAccount);

        uint32 buyerCharacterCount = countResult ? countResult->Fetch()[0].GetUInt32() : CHARACTER_TRADE_MAX_CHARACTERS_PER_ACCOUNT;
        if (buyerCharacterCount >= CHARACTER_TRADE_MAX_CHARACTERS_PER_ACCOUNT)
        {
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "구매 계정의 캐릭터 슬롯이 가득 찼습니다.");
            return;
        }

        if (!IsCharacterTradeCurrency(currencyType))
        {
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "현재 캐릭터 거래는 Point 판매만 지원합니다.");
            return;
        }

        uint64 buyerPoint = 0;
        uint64 buyerRPoint = 0;
        uint64 sellerPoint = 0;
        uint64 sellerRPoint = 0;

        if (!GetCharacterTradeBalances(buyerAccount, buyerPoint, buyerRPoint))
        {
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "구매 계정의 포인트 정보를 찾을 수 없습니다.");
            return;
        }

        if (!GetCharacterTradeBalances(sellerAccount, sellerPoint, sellerRPoint))
        {
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "판매 계정의 포인트 정보를 찾을 수 없습니다.");
            return;
        }

        uint64 buyerBalance = GetCharacterTradeSelectedBalance(currencyType, buyerPoint, buyerRPoint);

        if (price < CHARACTER_TRADE_MIN_PRICE || buyerBalance < price)
        {
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(
                player,
                std::string(GetCharacterTradeCurrencyName(currencyType)) + "가 부족합니다.");
            SendCharacterTradeBalance(player);
            return;
        }

        CharacterDatabase.DirectPExecute(
            "UPDATE character_trade_listing "
            "SET status = 3, buyer_account = {} "
            "WHERE id = {} AND status = 0",
            buyerAccount,
            listingId);

        QueryResult reserveCheck = CharacterDatabase.PQuery(
            "SELECT 1 FROM character_trade_listing "
            "WHERE id = {} AND status = 3 AND buyer_account = {} "
            "LIMIT 1",
            listingId,
            buyerAccount);

        if (!reserveCheck)
        {
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "다른 사용자가 먼저 구매 처리 중입니다.");
            return;
        }

        DebitCharacterTradeCurrency(buyerAccount, currencyType, price);

        uint64 buyerPointAfterDebit = 0;
        uint64 buyerRPointAfterDebit = 0;
        if (!GetCharacterTradeBalances(buyerAccount, buyerPointAfterDebit, buyerRPointAfterDebit)
            || GetCharacterTradeSelectedBalance(currencyType, buyerPointAfterDebit, buyerRPointAfterDebit) != buyerBalance - price)
        {
            RestoreCharacterTradeListing(listingId);
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(
                player,
                std::string(GetCharacterTradeCurrencyName(currencyType)) + " 결제 처리에 실패했습니다.");
            SendCharacterTradeBalance(player);
            return;
        }

        CharacterDatabase.DirectPExecute(
            "UPDATE characters "
            "SET account = {} "
            "WHERE guid = {} AND account = {} AND online = 0",
            buyerAccount,
            guid,
            sellerAccount);

        QueryResult transferCheck = CharacterDatabase.PQuery(
            "SELECT 1 FROM characters WHERE guid = {} AND account = {} LIMIT 1",
            guid,
            buyerAccount);

        if (!transferCheck)
        {
            CreditCharacterTradeCurrency(buyerAccount, currencyType, price);
            RestoreCharacterTradeListing(listingId);
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "캐릭터 소유권 이전에 실패하여 결제를 취소했습니다.");
            SendCharacterTradeBalance(player);
            return;
        }

        CreditCharacterTradeCurrency(sellerAccount, currencyType, sellerPayout);

        CharacterDatabase.DirectPExecute(
            "UPDATE character_trade_listing "
            "SET status = 1, buyer_account = {}, sold_at = CURRENT_TIMESTAMP "
            "WHERE id = {} AND status = 3",
            buyerAccount,
            listingId);

        QueryResult soldCheck = CharacterDatabase.PQuery(
            "SELECT 1 FROM character_trade_listing "
            "WHERE id = {} AND status = 1 AND buyer_account = {} "
            "LIMIT 1",
            listingId,
            buyerAccount);

        if (!soldCheck)
        {
            CharacterDatabase.DirectPExecute(
                "UPDATE characters SET account = {} WHERE guid = {} AND account = {} AND online = 0",
                sellerAccount,
                guid,
                buyerAccount);
            CreditCharacterTradeCurrency(buyerAccount, currencyType, price);
            DebitCharacterTradeCurrency(sellerAccount, currencyType, sellerPayout);
            RestoreCharacterTradeListing(listingId);
            RefreshCharacterTradeRealmCounts(sellerAccount, buyerAccount);
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "거래 완료 기록에 실패하여 구매를 취소했습니다.");
            SendCharacterTradeBalance(player);
            return;
        }

        if (!RemoveCharacterTradeBan(characterName))
        {
            CharacterDatabase.DirectPExecute(
                "UPDATE characters SET account = {} WHERE guid = {} AND account = {} AND online = 0",
                sellerAccount,
                guid,
                buyerAccount);
            CreditCharacterTradeCurrency(buyerAccount, currencyType, price);
            DebitCharacterTradeCurrency(sellerAccount, currencyType, sellerPayout);
            CharacterDatabase.DirectPExecute(
                "UPDATE character_trade_listing SET status = 0, buyer_account = 0, sold_at = NULL WHERE id = {}",
                listingId);
            RefreshCharacterTradeRealmCounts(sellerAccount, buyerAccount);
            CharacterTradeProcessingListings.erase(listingId);
            SendCharacterTradeError(player, "캐릭터 거래 잠금 해제에 실패하여 구매를 취소했습니다.");
            SendCharacterTradeBalance(player);
            return;
        }

        // 거래 로그의 price는 구매자가 실제 결제한 총 판매가를 유지한다.
        // 판매자 실지급액 = price - tradeFee.
        (void)tradeFee;

        CharacterDatabase.DirectPExecute(
            "INSERT INTO character_trade_log "
            "(listing_id, guid, seller_account, buyer_account, price, currency_type, completed_at) "
            "VALUES ({}, {}, {}, {}, {}, {}, CURRENT_TIMESTAMP)",
            listingId,
            guid,
            sellerAccount,
            buyerAccount,
            price,
            currencyType);

        RefreshCharacterTradeRealmCounts(sellerAccount, buyerAccount);

        uint64 remainingPoint = 0;
        uint64 remainingRPoint = 0;
        GetCharacterTradeBalances(buyerAccount, remainingPoint, remainingRPoint);

        SendCharacterTradeAddonMessage(
            player,
            "BUY:OK:" + std::to_string(listingId) + ":" +
            std::to_string(guid) + ":" +
            characterName + ":" +
            std::to_string(remainingPoint) + ":" +
            std::to_string(remainingRPoint));

        SendCharacterTradeBalance(player);
        CharacterTradeProcessingListings.erase(listingId);
    }

    static bool TryHandleCharacterTradeAddonPacket(WorldSession* session, WorldPacket& packet)
    {
        if (!session || packet.GetOpcode() != CMSG_MESSAGECHAT)
            return false;

        Player* player = session->GetPlayer();
        if (!player)
            return false;

        WorldPacket data(packet);
        data.rpos(0);

        uint32 chatType = 0;
        uint32 language = 0;
        data >> chatType;
        data >> language;

        if (language != LANG_ADDON || chatType != CHAT_MSG_WHISPER)
            return false;

        std::string target;
        data >> target;

        if (!target.empty() && target != player->GetName())
            return false;

        std::string message;
        data >> message;

        std::string payload;
        if (!GetCharacterTradeAddonPayload(message, payload))
            return false;

        if (payload == CHARACTER_TRADE_SELL_LIST_COMMAND)
        {
            SendCharacterTradeSellList(player);
            return true;
        }

        if (payload.compare(0, CHARACTER_TRADE_SELL_REGISTER_PREFIX.size(), CHARACTER_TRADE_SELL_REGISTER_PREFIX) == 0)
        {
            std::string guidText;
            std::string currencyText;
            std::string priceText;
            uint32 guid = 0;
            uint32 currencyValue = 0;
            uint64 price = 0;

            if (!ReadCharacterTradeTriple(
                    payload.substr(CHARACTER_TRADE_SELL_REGISTER_PREFIX.size()),
                    guidText,
                    currencyText,
                    priceText)
                || !ParseCharacterTradeUInt32(guidText, guid)
                || !ParseCharacterTradeUInt32(currencyText, currencyValue)
                || currencyValue > std::numeric_limits<uint8>::max()
                || !ParseCharacterTradeUInt64(priceText, price))
            {
                SendCharacterTradeError(player, "잘못된 판매 등록 요청입니다.");
                return true;
            }

            HandleCharacterTradeSellRegister(player, guid, uint8(currencyValue), price);
            return true;
        }

        if (payload.compare(0, CHARACTER_TRADE_SELL_CANCEL_PREFIX.size(), CHARACTER_TRADE_SELL_CANCEL_PREFIX) == 0)
        {
            uint32 guid = 0;
            if (!ParseCharacterTradeUInt32(
                    payload.substr(CHARACTER_TRADE_SELL_CANCEL_PREFIX.size()),
                    guid))
            {
                SendCharacterTradeError(player, "잘못된 판매 취소 요청입니다.");
                return true;
            }

            HandleCharacterTradeSellCancel(player, guid);
            return true;
        }

        if (payload == CHARACTER_TRADE_BUY_LIST_COMMAND)
        {
            SendCharacterTradeBuyList(player);
            return true;
        }

        if (payload == CHARACTER_TRADE_SELF_COMMAND)
        {
            SendCharacterTradeDetail(player, player->GetGUID().GetCounter(), true);
            return true;
        }

        if (payload.compare(0, CHARACTER_TRADE_DETAIL_PREFIX.size(), CHARACTER_TRADE_DETAIL_PREFIX) == 0)
        {
            uint32 guid = 0;
            if (!ParseCharacterTradeUInt32(payload.substr(CHARACTER_TRADE_DETAIL_PREFIX.size()), guid))
            {
                SendCharacterTradeError(player, "잘못된 캐릭터 상세보기 요청입니다.");
                return true;
            }

            SendCharacterTradeDetail(player, guid, false);
            return true;
        }

        if (payload.compare(0, CHARACTER_TRADE_BUY_PREFIX.size(), CHARACTER_TRADE_BUY_PREFIX) == 0)
        {
            uint64 listingId = 0;
            if (!ParseCharacterTradeUInt64(payload.substr(CHARACTER_TRADE_BUY_PREFIX.size()), listingId))
            {
                SendCharacterTradeError(player, "잘못된 구매 요청입니다.");
                return true;
            }

            HandleCharacterTradeBuy(player, listingId);
            return true;
        }

        return false;
    }
}

class character_trade_serverscript : public ServerScript
{
public:
    character_trade_serverscript() : ServerScript("character_trade_serverscript") { }

    void OnPacketReceive(WorldSession* session, WorldPacket& packet) override
    {
        TryHandleCharacterTradeAddonPacket(session, packet);
    }
};

void AddSC_character_trade()
{
    new character_trade_serverscript();
}
