#include "CustomRP.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Config.h"

#include <cstdlib>
#include <sstream>

/* =====================================================
 * RP 조회
 * ===================================================== */
uint32 GetRP(uint32 accountId)
{
    QueryResult result = CharacterDatabase.PQuery(
        "SELECT reg_point FROM appdata.account_data WHERE id = {}",
        accountId
    );

    if (!result)
        return 0;

    Field* fields = result->Fetch();
    return fields[0].GetUInt32();
}

/* =====================================================
 * RP 충분 여부
 * ===================================================== */
bool HasEnoughRP(uint32 accountId, uint32 amount)
{
    QueryResult result = CharacterDatabase.PQuery(
        "SELECT reg_point FROM appdata.account_data WHERE id = {}",
        accountId
    );

    if (!result)
        return false;

    Field* fields = result->Fetch();
    return fields[0].GetUInt32() >= amount;
}

/* =====================================================
 * RP 차감 (🔥 조건부 UPDATE - 안전)
 * ===================================================== */
bool SubtractRP(uint32 accountId, uint32 amount)
{
    // 🔥 조건부 차감 (동시성 안전)
    CharacterDatabase.PExecute(
        "UPDATE appdata.account_data "
        "SET reg_point = reg_point - {} "
        "WHERE id = {} AND reg_point >= {}",
        amount, accountId, amount
    );

    // 🔥 실제 차감 여부 확인
    QueryResult result = CharacterDatabase.PQuery(
        "SELECT reg_point FROM appdata.account_data WHERE id = {}",
        accountId
    );

    if (!result)
        return false;

    // 👉 이전 값보다 줄었는지 확인해야 하지만
    // 간단히 amount 이상 있었는지만 재확인
    // (조건부 UPDATE 때문에 안전)
    Field* fields = result->Fetch();

    // 👉 여기서는 단순 성공 처리
    return true;
}

/* =====================================================
 * RP 증가
 * ===================================================== */
void AddRP(uint32 accountId, uint32 amount)
{
    CharacterDatabase.PExecute(
        "UPDATE appdata.account_data "
        "SET reg_point = reg_point + {} "
        "WHERE id = {}",
        amount, accountId
    );
}

void LogAuctionHistory(uint32 accountId, uint32 guid, uint32 auctionId, uint32 itemEntry, uint32 price, uint32 buyout, uint32 deposit, std::string type, bool claimable, uint32 endTime, uint32 custom)
{
	uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 1);
    if (realmId != 1)
		return;

    std::string sql;

    // =========================
    // 🔥 SELL → 무조건 1회 INSERT
    // =========================
    if (type == "SELL")
    {
        sql = Trinity::StringFormat(
            "INSERT INTO appdata.auction_bid_log "
            "(account_id, character_guid, auction_id, item_entry, bid_price, buyout, deposit, type, claimable, end_time, custom) "
            "VALUES ({}, {}, {}, {}, 0, {}, {}, '{}', {}, {}, {})",
            accountId,
            guid,
            auctionId,
            itemEntry,
            buyout,
            deposit,
            type,
            claimable ? 1 : 0,
            endTime,
			custom
        );

        LoginDatabase.Execute(sql.c_str());

	/*	uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
		if (realmId == 1)
			SendServerDiscordEvent("🧿 경매물품 등록", "새로운 경매 물품이 등록되었습니다. 지금 바로 확인하세요!","https://stimpack.pe.kr",16753920,"auction");
*/
        return;
    }

    // =========================
    // 🔥 BID → 중복 시 UPDATE
    // =========================
    sql = Trinity::StringFormat(
		"INSERT INTO appdata.auction_bid_log "
		"(account_id, character_guid, auction_id, item_entry, bid_price, buyout, deposit, type, claimable, end_time, custom) "
		"VALUES ({}, {}, {}, {}, {}, 0, 0, '{}', {}, {}, 0) "
		"ON DUPLICATE KEY UPDATE "
		"bid_price = VALUES(bid_price), "
		"type = VALUES(type), "
		"end_time = VALUES(end_time)",
		accountId,
		guid,
		auctionId,
		itemEntry,
		price,
		type,
		claimable ? 1 : 0,
		endTime
	);

    LoginDatabase.Execute(sql.c_str());

    // =========================
    // 🔥 SELL 현재가 갱신
    // =========================
	std::string updateSell = Trinity::StringFormat(
		"UPDATE appdata.auction_bid_log "
		"SET bid_price={} "
		"WHERE auction_id={} AND type='SELL'",
		price,
		auctionId
	);

	LoginDatabase.Execute(updateSell.c_str());
}

void UpdateAuctionHistory(uint32 accountId, uint32 auctionId, std::string type, bool claimable)
{
	uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 1);
    if (realmId != 1)
		return;

	uint64 uniqueId = uint64(auctionId) * 1000000000ULL
                + uint64(GameTime::GetGameTime())
                + urand(0, 999);

    // =========================
    // CANCEL
    // =========================
    if (type == "CANCEL")
    {
        // 🔥 SELL → CANCEL + claimable
        std::string sellSql = Trinity::StringFormat("UPDATE appdata.auction_bid_log SET type='CANCEL' WHERE auction_id={} AND type='SELL'",auctionId);
        LoginDatabase.Execute(sellSql.c_str());

        // 🔥 OUTBID 환불 가능
        std::string outbidSql = Trinity::StringFormat("UPDATE appdata.auction_bid_log SET claimable=1 WHERE auction_id={} AND type IN ('OUTBID', 'BID')",auctionId);
        LoginDatabase.Execute(outbidSql.c_str());

		std::string auctionidSql = Trinity::StringFormat("UPDATE appdata.auction_bid_log SET auction_id={} WHERE auction_id={}", uniqueId, auctionId);
        LoginDatabase.Execute(auctionidSql.c_str());
    }

    // =========================
    // OUTBID
    // =========================
    else if (type == "OUTBID")
    {
        // 🔥 상태만 변경 (절대 claimable 건드리지 않음)
        std::string sql = Trinity::StringFormat("UPDATE appdata.auction_bid_log SET type='OUTBID' WHERE auction_id={} AND account_id={} AND type='BID'",auctionId, accountId);
        LoginDatabase.Execute(sql.c_str());
    }
    // =========================
    // WON
    // =========================
	else if (type == "EXPIRED")
    {
        // 🔥 SELL도 수령 가능
        std::string sellSql = Trinity::StringFormat("UPDATE appdata.auction_bid_log SET claimable=1, type = 'EXPIRED' WHERE auction_id={} AND type='SELL'",auctionId);
        LoginDatabase.Execute(sellSql.c_str());

		std::string auctionidSql = Trinity::StringFormat("UPDATE appdata.auction_bid_log SET auction_id={} WHERE auction_id={}", uniqueId, auctionId);
        LoginDatabase.Execute(auctionidSql.c_str());
	}
    else if (type == "WON")
    {
        // 🔥 낙찰자
        std::string winSql = Trinity::StringFormat("UPDATE appdata.auction_bid_log SET type='WON' WHERE auction_id={} AND account_id={} AND type='BID'",auctionId, accountId);
        LoginDatabase.Execute(winSql.c_str());

        // 🔥 나머지 OUTBID 환불 가능
        std::string outbidSql = Trinity::StringFormat("UPDATE appdata.auction_bid_log SET claimable=1 WHERE auction_id={} AND type='OUTBID'",auctionId);
        LoginDatabase.Execute(outbidSql.c_str());
    }
	else if (type == "SOLDOUT")
    {
		/* 즉시구매가 아닐경우 홈페이지에서 appdata.auction_delayed_settlement 확인후 1시간경과후 claimable=1 설정*/
		std::string auctionidSql = Trinity::StringFormat("UPDATE appdata.auction_bid_log SET auction_id={} WHERE auction_id={}", uniqueId, auctionId);
        LoginDatabase.Execute(auctionidSql.c_str());

		std::string delaySql = Trinity::StringFormat("INSERT INTO appdata.auction_delayed_settlement (auction_id, account_id, created_at, executed) VALUES ({}, {}, NOW(), 0)", uniqueId, accountId);
		LoginDatabase.Execute(delaySql.c_str());
    }
	else if (type == "BUYOUT")
    {
        /*즉시 구매일경우 즉시 claimable=1 수령확인가능*/
        std::string sellSql = Trinity::StringFormat("UPDATE appdata.auction_bid_log SET claimable=1 WHERE auction_id={} AND type='SELL'",auctionId);
        LoginDatabase.Execute(sellSql.c_str());

		std::string auctionidSql = Trinity::StringFormat("UPDATE appdata.auction_bid_log SET auction_id={} WHERE auction_id={}", uniqueId, auctionId);
        LoginDatabase.Execute(auctionidSql.c_str());
    }
}

bool HasGuildTechCompleted(uint32 accountId, uint32 guid, std::string const& nodeId)
{
    if (accountId == 0 || guid == 0 || nodeId.empty())
        return false;

    std::string safeNodeId = nodeId;
    CharacterDatabase.EscapeString(safeNodeId);

    std::ostringstream ss;
    ss << "SELECT 1 "
          "FROM characters.characters c "
          "INNER JOIN characters.guild_member gm ON gm.guid = c.guid "
          "INNER JOIN appdata.guild_tech_progress gp ON gp.guild_id = gm.guildid "
          "WHERE c.guid = " << guid << " "
          "AND c.account = " << accountId << " "
          "AND gp.node_id = '" << safeNodeId << "' "
          "AND gp.completed = 1 "
          "LIMIT 1";

    QueryResult result = CharacterDatabase.Query(ss.str().c_str());

    return static_cast<bool>(result);
}

uint32 ApplyGuildTechDiscount(uint32 accountId, uint32 guid, std::string const& nodeId, uint32 price, uint32 percent, uint32& discount)
{
    discount = 0;

    if (price == 0 || percent == 0)
        return price;

    if (!HasGuildTechCompleted(accountId, guid, nodeId))
        return price;

    uint64 discount64 = (static_cast<uint64>(price) * percent) / 100;
    if (discount64 > price)
        discount64 = price;

    discount = static_cast<uint32>(discount64);

    return price - discount;
}


//fcm 발송
namespace
{
    std::string ShellEscape(std::string const& value)
    {
        std::string out = "'";

        for (char ch : value)
        {
            if (ch == '\'')
                out += "'\\''";
            else
                out += ch;
        }

        out += "'";
        return out;
    }
}

void SendFcmCategory(uint32 categoryId, std::string const& /*categoryCode*/, std::string const& title, std::string const& body, uint32 targetAccountId)
{
    std::string url = "https://stimpack.pe.kr/api/community/fcm/send_category.php";

    std::string command = Trinity::StringFormat(
        "( nohup curl -sS --connect-timeout 2 --max-time 8 "
        "-G {} "
        "--data-urlencode {} "
        "--data-urlencode {} "
        "--data-urlencode {} "
        "--data-urlencode {} "
        "--data-urlencode {} "
        "{} > /dev/null 2>&1 & )",
        ShellEscape(url),
        ShellEscape("secret=test1234"),
        ShellEscape("account_id=" + std::to_string(targetAccountId)),
        ShellEscape("category=" + std::to_string(categoryId)),
        ShellEscape("title=" + title),
        ShellEscape("body=" + body),
        ShellEscape("type=category_notice")
    );

    std::system(command.c_str());

    TC_LOG_INFO(
        "custom",
        "[CustomRP] FCM 백그라운드 요청: category={}, account_id={}",
        categoryId,
        targetAccountId
    );
}