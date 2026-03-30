#ifndef DEF_TRANSMOGRIFICATION_DEFINES_H
#define DEF_TRANSMOGRIFICATION_DEFINES_H

#include "Define.h"
#include <limits>

enum AppearanceType : uint32
{
    TRANSMOG_TYPE_ITEM,
    TRANSMOG_TYPE_ENCHANT,
    TRANSMOG_TYPE_COUNT,
};

enum TransmogResult
{
    TransmogResult_Ok = 0, // expected to be 0 while others are expected not to be 0
    TransmogResult_InvalidItemType,
    TransmogResult_ItemBlocked,
    TransmogResult_FishingPoleBlocked,
    TransmogResult_InvalidItemQuality,
    TransmogResult_RequiredEventNotActive,
    TransmogResult_ItemMustHaveStats,
    TransmogResult_InvalidFaction,
    TransmogResult_InvalidClass,
    TransmogResult_InvalidRace,
    TransmogResult_MissingProfiency,
    TransmogResult_MissingSkill,
    TransmogResult_TooLowSkill,
    TransmogResult_MissingSpell,
    TransmogResult_TooLowLevelPlayer,
    TransmogResult_TooLowLevelItem,
    TransmogResult_TooHighLevelItem,
    TransmogResult_ItemTypesDontMatch,
    TransmogResult_ArmorTypesDontMatch,
    TransmogResult_WeaponTypesDontMatch,
    TransmogResult_EquipSlotsDontMatch,
    TransmogResult_InvalidSlot,
    TransmogResult_NonexistantTransmog,
    TransmogResult_EmptySlot,
    TransmogResult_NoPendingTarnsmogs,
    TransmogResult_CostChangedDuringTransaction,
    TransmogResult_NotEnoughMoney,
    TransmogResult_TooLongSetName,
    TransmogResult_NoTransmogrifications,
    TransmogResult_AtMaxSets,
    TransmogResult_NonexistantSet,
    TransmogResult_ItemNotFitForEnchantRequirements,
};

inline const char* CanTransmogrifyResultMessage(TransmogResult result)
{
    switch (result)
    {
        case TransmogResult_Ok:
            return "아이템의 외형이 변경되었습니다";

        case TransmogResult_InvalidItemType:
            return "이 유형의 아이템은 형상변환할 수 없습니다";

        case TransmogResult_ItemBlocked:
            return "이 아이템은 형상변환할 수 없습니다";

        case TransmogResult_FishingPoleBlocked:
            return "낚싯대는 형상변환할 수 없습니다";

        case TransmogResult_InvalidItemQuality:
            return "이 아이템 등급은 형상변환할 수 없습니다";

        case TransmogResult_RequiredEventNotActive:
            return "이 아이템을 사용하기 위한 이벤트가 활성화되어 있지 않습니다";

        case TransmogResult_ItemMustHaveStats:
            return "형상변환 가능한 아이템에는 능력치가 있어야 합니다";

        case TransmogResult_InvalidFaction:
            return "현재 진영에서는 이 아이템을 사용할 수 없습니다";

        case TransmogResult_InvalidClass:
            return "현재 직업으로는 이 아이템을 사용할 수 없습니다";

        case TransmogResult_InvalidRace:
            return "현재 종족으로는 이 아이템을 사용할 수 없습니다";

        case TransmogResult_MissingProfiency:
            return "이 아이템을 사용할 수 있는 숙련이 없습니다";

        case TransmogResult_MissingSkill:
            return "이 아이템을 사용하기 위한 기술이 없습니다";

        case TransmogResult_TooLowSkill:
            return "이 아이템을 사용하기 위한 기술 숙련도가 부족합니다";

        case TransmogResult_MissingSpell:
            return "이 아이템을 사용하기 위한 주문이 없습니다";

        case TransmogResult_TooLowLevelPlayer:
            return "레벨이 부족하여 이 아이템을 사용할 수 없습니다";

        case TransmogResult_TooLowLevelItem:
            return "아이템 레벨이 너무 낮습니다";

        case TransmogResult_TooHighLevelItem:
            return "아이템 레벨이 너무 높습니다";

        case TransmogResult_ItemTypesDontMatch:
            return "아이템 유형이 서로 맞지 않습니다";

        case TransmogResult_ArmorTypesDontMatch:
            return "방어구 유형이 서로 맞지 않습니다";

        case TransmogResult_WeaponTypesDontMatch:
            return "무기 유형이 서로 맞지 않습니다";

        case TransmogResult_EquipSlotsDontMatch:
            return "장착 슬롯이 서로 맞지 않아 형상변환할 수 없습니다";

        case TransmogResult_InvalidSlot:
            return "이 슬롯에 장착된 아이템은 형상변환할 수 없습니다";

        case TransmogResult_NonexistantTransmog:
            return "선택한 형상변환 외형이 존재하지 않습니다";

        case TransmogResult_EmptySlot:
            return "해당 슬롯에 장착된 아이템이 없습니다";

        case TransmogResult_NoPendingTarnsmogs:
            return "보류 중인 형상변환이 없습니다";

        case TransmogResult_CostChangedDuringTransaction:
            return "거래 중 비용이 변경되었습니다";

        case TransmogResult_NotEnoughMoney:
            return "소지 금액이 부족합니다";

        case TransmogResult_TooLongSetName:
            return "세트 이름의 길이가 올바르지 않습니다";

        case TransmogResult_NoTransmogrifications:
            return "형상변환된 아이템이 없습니다";

        case TransmogResult_AtMaxSets:
            return "더 이상 세트를 저장할 수 없습니다";

        case TransmogResult_NonexistantSet:
            return "해당 세트가 존재하지 않습니다";

        case TransmogResult_ItemNotFitForEnchantRequirements:
            return "이 아이템에는 해당 마법부여를 적용할 수 없습니다";
    }
    return nullptr;
};
const char* const TransmogResult_Ok_PendingMessage = "형상변환이 보류 상태로 추가되었습니다";
const char* const AddToCollectionMessageFmt = "%s 외형이 외형 도감에 추가되었습니다.";
const uint32 InvisibleEntry = std::numeric_limits<uint32>::max();
const uint32 NormalEntry = InvisibleEntry - 1;
const uint32 RemovePending = NormalEntry - 1;
const uint32 AbsoluteMaxSets = 25;

class Item;
typedef std::vector<std::tuple<Item* /*item*/, uint32 /*transmog*/, AppearanceType /*transmog type*/>> PendingTransmogs;
typedef std::vector<std::tuple<uint8 /*slot*/, uint32 /*transmog*/, AppearanceType /*transmog type*/>> SetTransmogs;

#endif
