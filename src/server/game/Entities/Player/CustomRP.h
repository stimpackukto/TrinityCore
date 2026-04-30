#pragma once

#include "Define.h"
#include <string>

// RP 조회
uint32 GetRP(uint32 accountId);

// RP 충분 여부
bool HasEnoughRP(uint32 accountId, uint32 amount);
bool SubtractRP(uint32 accountId, uint32 amount);
void AddRP(uint32 accountId, uint32 amount);

// 🔥 길드 테크니크 확인
bool HasGuildTechCompleted(uint32 accountId, uint32 guid, std::string const& nodeId);

// 🔥 길드 테크니크 할인 적용
uint32 ApplyGuildTechDiscount(uint32 accountId, uint32 guid, std::string const& nodeId, uint32 price, uint32 percent, uint32& discount);

// 🔥 경매 로그                 계정아이디              케릭터guid          옥션아이디    아이템아이디        가격             즉구              보증금             타입              수령              종료시간
void LogAuctionHistory(uint32 accountId, uint32 guid, uint32 auctionId, uint32 itemEntry, uint32 price, uint32 buyout, uint32 deposit, std::string type, bool claimable, uint32 endTime, uint32 custom);

                                            //계정아이디          옥션아이디                  타입            환불
void UpdateAuctionHistory(uint32 accountId, uint32 auctionId, std::string type, bool claimable);