/*
 * Custom Update - 2026-06-29
 *
 * Execution Meta Gem System
 *
 * 3918 : 처형의 얼개보석 : 절단
 * 3919 : 처형의 얼개보석 : 관통
 * 3920 : 처형의 얼개보석 : 붕괴
 * 3921 : 처형의 얼개보석 : 압도
 * 3922 : 처형의 얼개보석 : 구원
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Unit.h"
#include "Creature.h"
#include "Item.h"
#include "Map.h"
#include "SpellAuraEffects.h"
#include "ThreatManager.h"
#include "Util.h"

#include <array>
#include <unordered_map>

namespace
{
    enum ExecutionMetaGemEnchant
    {
        ENCHANT_EXECUTION_CUT       = 3918,
        ENCHANT_EXECUTION_PIERCE    = 3919,
        ENCHANT_EXECUTION_COLLAPSE  = 3920,
        ENCHANT_EXECUTION_OVERWHELM = 3921,
        ENCHANT_EXECUTION_SALVATION = 3922
    };

    enum ExecutionMetaGemIndex
    {
        EXECUTION_INDEX_CUT = 0,
        EXECUTION_INDEX_PIERCE,
        EXECUTION_INDEX_COLLAPSE,
        EXECUTION_INDEX_OVERWHELM,
        EXECUTION_INDEX_SALVATION,
        EXECUTION_INDEX_MAX
    };

    static constexpr uint32 EXECUTION_INTERNAL_COOLDOWN_MS = 8 * IN_MILLISECONDS;
    static constexpr uint32 SPELL_EXECUTION_GEM_PROC_VISUAL = 45935;

    static constexpr uint32 EXECUTION_PROC_CHANCE_NORMAL = 35;
    static constexpr uint32 EXECUTION_PROC_CHANCE_JEWELCRAFTING = 50;
    static constexpr uint32 SKILL_CUSTOM_JEWELCRAFTING = 755;

    using CooldownArray = std::array<uint32, EXECUTION_INDEX_MAX>;

    std::unordered_map<uint64, CooldownArray>& GetExecutionCooldownStore()
    {
        static std::unordered_map<uint64, CooldownArray> cooldowns;
        return cooldowns;
    }

    bool IsJewelcrafter(Player* player)
    {
        if (!player)
            return false;

        return player->HasSkill(SKILL_CUSTOM_JEWELCRAFTING);
    }

    uint32 GetExecutionProcChance(Player* player)
    {
        if (IsJewelcrafter(player))
            return EXECUTION_PROC_CHANCE_JEWELCRAFTING;

        return EXECUTION_PROC_CHANCE_NORMAL;
    }

    bool RollExecutionProc(Player* player)
    {
        return urand(1, 100) <= GetExecutionProcChance(player);
    }

    void PlayExecutionGemProcVisual(Player* player)
    {
        if (!player)
            return;

        player->CastSpell(player, SPELL_EXECUTION_GEM_PROC_VISUAL, true);
    }

    uint8 GetExecutionGemIndex(uint32 enchantId)
    {
        switch (enchantId)
        {
            case ENCHANT_EXECUTION_CUT:
                return EXECUTION_INDEX_CUT;
            case ENCHANT_EXECUTION_PIERCE:
                return EXECUTION_INDEX_PIERCE;
            case ENCHANT_EXECUTION_COLLAPSE:
                return EXECUTION_INDEX_COLLAPSE;
            case ENCHANT_EXECUTION_OVERWHELM:
                return EXECUTION_INDEX_OVERWHELM;
            case ENCHANT_EXECUTION_SALVATION:
                return EXECUTION_INDEX_SALVATION;
            default:
                return EXECUTION_INDEX_MAX;
        }
    }

    bool IsExecutionEnchant(uint32 enchantId)
    {
        return GetExecutionGemIndex(enchantId) != EXECUTION_INDEX_MAX;
    }

    uint32 GetEquippedExecutionMetaEnchant(Player* player)
    {
        if (!player)
            return 0;

        Item* head = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);

        if (!head)
            return 0;

        for (uint8 slot = PERM_ENCHANTMENT_SLOT; slot < MAX_INSPECTED_ENCHANTMENT_SLOT; ++slot)
        {
            uint32 enchantId = head->GetEnchantmentId(EnchantmentSlot(slot));

            if (IsExecutionEnchant(enchantId))
                return enchantId;
        }

        return 0;
    }

    bool IsCooldownReady(Player* player, uint32 enchantId)
    {
        if (!player)
            return false;

        uint8 index = GetExecutionGemIndex(enchantId);

        if (index >= EXECUTION_INDEX_MAX)
            return false;

        uint64 guid = player->GetGUID().GetCounter();
        uint32 now = getMSTime();

        CooldownArray& cooldowns = GetExecutionCooldownStore()[guid];

        if (cooldowns[index] != 0 && getMSTimeDiff(cooldowns[index], now) < EXECUTION_INTERNAL_COOLDOWN_MS)
            return false;

        cooldowns[index] = now;
        return true;
    }

    bool IsPlayerControlledUnit(Unit* unit)
    {
        if (!unit)
            return false;

        if (unit->ToPlayer())
            return true;

        if (unit->GetCharmerOrOwnerPlayerOrPlayerItself())
            return true;

        return false;
    }

    bool IsPvPBlocked(Player* player)
    {
        if (!player)
            return true;

        Map* map = player->GetMap();

        if (map && map->IsBattlegroundOrArena())
            return true;

        return false;
    }

    bool IsValidPvEExecutionTarget(Player* player, Unit* victim)
    {
        if (!player || !victim)
            return false;

        if (!victim->IsAlive())
            return false;

        if (IsPvPBlocked(player))
            return false;

        if (IsPlayerControlledUnit(victim))
            return false;

        if (victim->GetHealthPct() > 20.0f)
            return false;

        return true;
    }

    bool IsBossTarget(Unit* victim)
    {
        if (!victim)
            return false;

        Creature* creature = victim->ToCreature();

        if (!creature)
            return false;

        if (creature->isWorldBoss())
            return true;

        return false;
    }

    uint32 ApplyBossReduction(Unit* victim, uint32 amount)
    {
        if (IsBossTarget(victim))
            return amount / 2;

        return amount;
    }

    bool IsMeleePhysicalClass(Player* player)
    {
        if (!player)
            return false;

        switch (player->GetClass())
        {
            case CLASS_WARRIOR:
            case CLASS_ROGUE:
            case CLASS_DEATH_KNIGHT:
            case CLASS_PALADIN:
            case CLASS_DRUID:
            case CLASS_SHAMAN:
                return true;
            default:
                return false;
        }
    }

    bool IsRangedPhysicalClass(Player* player)
    {
        if (!player)
            return false;

        return player->GetClass() == CLASS_HUNTER;
    }

    bool IsCasterDamageClass(Player* player)
    {
        if (!player)
            return false;

        switch (player->GetClass())
        {
            case CLASS_MAGE:
            case CLASS_WARLOCK:
            case CLASS_PRIEST:
            case CLASS_DRUID:
            case CLASS_SHAMAN:
                return true;
            default:
                return false;
        }
    }

    bool IsTankClass(Player* player)
    {
        if (!player)
            return false;

        switch (player->GetClass())
        {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_DEATH_KNIGHT:
            case CLASS_DRUID:
                return true;
            default:
                return false;
        }
    }

    bool IsHealerClass(Player* player)
    {
        if (!player)
            return false;

        switch (player->GetClass())
        {
            case CLASS_PALADIN:
            case CLASS_PRIEST:
            case CLASS_DRUID:
            case CLASS_SHAMAN:
                return true;
            default:
                return false;
        }
    }

    uint8 CountCasterPeriodicDamageAuras(Player* player, Unit* victim)
    {
        if (!player || !victim)
            return 0;

        uint8 count = 0;

        auto const& periodicDamageAuras = victim->GetAuraEffectsByType(SPELL_AURA_PERIODIC_DAMAGE);

        for (auto const* auraEffect : periodicDamageAuras)
        {
            if (!auraEffect)
                continue;

            if (auraEffect->GetCasterGUID() != player->GetGUID())
                continue;

            ++count;

            if (count >= 3)
                break;
        }

        return count;
    }

    uint32 BuildCutDamage(Player* player, uint32 originalDamage)
    {
        uint32 bonus = 600;
        bonus += uint32(player->GetLevel()) * 18;
        bonus += originalDamage * 12 / 100;

        return bonus;
    }

    uint32 BuildPierceDamage(Player* player, Unit* victim, uint32 originalDamage)
    {
        uint32 bonus = 550;
        bonus += uint32(player->GetLevel()) * 18;
        bonus += originalDamage * 12 / 100;

        if (player->GetDistance(victim) >= 30.0f)
            bonus += bonus / 10;

        return bonus;
    }

    uint32 BuildCollapseDamage(Player* player, Unit* victim, uint32 originalDamage)
    {
        uint32 bonus = 650;
        bonus += uint32(player->GetLevel()) * 20;
        bonus += originalDamage * 10 / 100;

        uint8 dotCount = CountCasterPeriodicDamageAuras(player, victim);

        if (dotCount > 0)
            bonus += bonus * dotCount * 5 / 100;

        return bonus;
    }

    uint32 BuildOverwhelmDamage(Player* player, uint32 originalDamage)
    {
        uint32 bonus = 300;
        bonus += uint32(player->GetLevel()) * 12;
        bonus += originalDamage * 6 / 100;

        return bonus;
    }

    uint32 BuildSalvationHeal(Player* player, uint32 originalHeal)
    {
        uint32 bonus = 500;
        bonus += uint32(player->GetLevel()) * 18;
        bonus += originalHeal * 12 / 100;

        return bonus;
    }

    void AddTankThreat(Player* player, Unit* victim, uint32 bonusDamage)
    {
        if (!player || !victim)
            return;

        if (!victim->CanHaveThreatList())
            return;

        victim->GetThreatManager().AddThreat(player, float(bonusDamage) * 1.5f);
    }
}

class custom_execution_meta_gem_unit_script : public UnitScript
{
public:
    custom_execution_meta_gem_unit_script() : UnitScript("custom_execution_meta_gem_unit_script") { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!attacker || !victim || damage == 0)
            return;

        Player* player = attacker->ToPlayer();

        if (!player)
            player = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();

        if (!player)
            return;

        uint32 enchantId = GetEquippedExecutionMetaEnchant(player);

        if (!enchantId)
            return;

        if (!IsValidPvEExecutionTarget(player, victim))
            return;

        switch (enchantId)
        {
            case ENCHANT_EXECUTION_CUT:
            {
                if (!IsMeleePhysicalClass(player))
                    return;

                if (!RollExecutionProc(player))
                    return;

                if (!IsCooldownReady(player, enchantId))
                    return;

                uint32 bonus = BuildCutDamage(player, damage);
                bonus = ApplyBossReduction(victim, bonus);

                damage += bonus;
                PlayExecutionGemProcVisual(player);
                break;
            }

            case ENCHANT_EXECUTION_PIERCE:
            {
                if (!IsRangedPhysicalClass(player))
                    return;

                if (!RollExecutionProc(player))
                    return;

                if (!IsCooldownReady(player, enchantId))
                    return;

                uint32 bonus = BuildPierceDamage(player, victim, damage);
                bonus = ApplyBossReduction(victim, bonus);

                damage += bonus;
                PlayExecutionGemProcVisual(player);
                break;
            }

            case ENCHANT_EXECUTION_COLLAPSE:
            {
                if (!IsCasterDamageClass(player))
                    return;

                if (!RollExecutionProc(player))
                    return;

                if (!IsCooldownReady(player, enchantId))
                    return;

                uint32 bonus = BuildCollapseDamage(player, victim, damage);
                bonus = ApplyBossReduction(victim, bonus);

                damage += bonus;
                PlayExecutionGemProcVisual(player);
                break;
            }

            case ENCHANT_EXECUTION_OVERWHELM:
            {
                if (!IsTankClass(player))
                    return;

                if (!RollExecutionProc(player))
                    return;

                if (!IsCooldownReady(player, enchantId))
                    return;

                uint32 bonus = BuildOverwhelmDamage(player, damage);
                bonus = ApplyBossReduction(victim, bonus);

                damage += bonus;
                AddTankThreat(player, victim, bonus);
                PlayExecutionGemProcVisual(player);
                break;
            }

            default:
                break;
        }
    }

    void OnHeal(Unit* healer, Unit* receiver, uint32& gain) override
    {
        if (!healer || !receiver || gain == 0)
            return;

        Player* player = healer->ToPlayer();

        if (!player)
            player = healer->GetCharmerOrOwnerPlayerOrPlayerItself();

        if (!player)
            return;

        if (IsPvPBlocked(player))
            return;

        if (!IsHealerClass(player))
            return;

        uint32 enchantId = GetEquippedExecutionMetaEnchant(player);

        if (enchantId != ENCHANT_EXECUTION_SALVATION)
            return;

        if (receiver->GetHealthPct() > 35.0f)
            return;

        if (!RollExecutionProc(player))
            return;

        if (!IsCooldownReady(player, enchantId))
            return;

        uint32 bonus = BuildSalvationHeal(player, gain);

        gain += bonus;
        PlayExecutionGemProcVisual(player);
    }
};

class custom_execution_meta_gem_player_script : public PlayerScript
{
public:
    custom_execution_meta_gem_player_script() : PlayerScript("custom_execution_meta_gem_player_script") { }

    void OnLogout(Player* player) override
    {
        if (!player)
            return;

        GetExecutionCooldownStore().erase(player->GetGUID().GetCounter());
    }
};


//파편
/*
 * Custom Update - 2026-06-29
 *
 * Execution Gem Fragment Spell Combine System
 */

#include "ScriptMgr.h"
#include "SpellScript.h"
#include "Player.h"
#include "Item.h"
#include "Chat.h"
#include "StringFormat.h"

#include <array>

namespace
{
    static constexpr uint32 SPELL_EXECUTION_GEM_FRAGMENT_COMBINE = 900201;

    static constexpr uint32 ITEM_THANK_YOU_CERTIFICATE = 45924; // 실제 감사장 아이템 ID로 변경
    static constexpr uint32 REQUIRED_CERTIFICATE_COUNT = 10;

    static constexpr uint32 REQUIRED_FRAGMENT_COUNT_NORMAL = 30;
    static constexpr uint32 REQUIRED_FRAGMENT_COUNT_JEWELCRAFTING = 25;

    struct ExecutionGemFragmentRecipe
    {
        uint32 fragmentItem;
        uint32 completeGem;
        char const* gemName;
    };

    static constexpr std::array<ExecutionGemFragmentRecipe, 5> EXECUTION_GEM_FRAGMENT_RECIPES =
    {{
        { 56924, 56919, "처형의 얼개보석 : 절단" },
        { 56925, 56920, "처형의 얼개보석 : 관통" },
        { 56926, 56921, "처형의 얼개보석 : 붕괴" },
        { 56927, 56922, "처형의 얼개보석 : 압도" },
        { 56928, 56923, "처형의 얼개보석 : 구원" }
    }};

    ExecutionGemFragmentRecipe const* GetExecutionGemFragmentRecipe(uint32 itemId)
    {
        for (ExecutionGemFragmentRecipe const& recipe : EXECUTION_GEM_FRAGMENT_RECIPES)
            if (recipe.fragmentItem == itemId)
                return &recipe;

        return nullptr;
    }

    uint32 GetRequiredFragmentCount(Player* player)
    {
        if (IsJewelcrafter(player))
            return REQUIRED_FRAGMENT_COUNT_JEWELCRAFTING;

        return REQUIRED_FRAGMENT_COUNT_NORMAL;
    }

    void SendExecutionGemMessage(Player* player, std::string const& message)
    {
        if (!player || !player->GetSession())
            return;

        ChatHandler(player->GetSession()).SendSysMessage(Trinity::StringFormat("[처형보석] {}", message).c_str());
    }
}

class spell_execution_gem_fragment_combine : public SpellScriptLoader
{
public:
    spell_execution_gem_fragment_combine() : SpellScriptLoader("spell_execution_gem_fragment_combine") { }

    class spell_execution_gem_fragment_combine_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_execution_gem_fragment_combine_SpellScript);

        SpellCastResult CheckCast()
        {
            Unit* caster = GetCaster();

            if (!caster)
                return SPELL_FAILED_DONT_REPORT;

            Player* player = caster->ToPlayer();

            if (!player)
                return SPELL_FAILED_DONT_REPORT;

            Item* castItem = GetCastItem();

            if (!castItem)
            {
                SendExecutionGemMessage(player, "사용한 파편을 확인할 수 없습니다.");
                return SPELL_FAILED_DONT_REPORT;
            }

            ExecutionGemFragmentRecipe const* recipe = GetExecutionGemFragmentRecipe(castItem->GetEntry());

            if (!recipe)
            {
                SendExecutionGemMessage(player, "이 파편은 처형의 얼개보석 제작에 사용할 수 없습니다.");
                return SPELL_FAILED_DONT_REPORT;
            }

            uint32 requiredFragmentCount = GetRequiredFragmentCount(player);

            if (!player->HasItemCount(recipe->fragmentItem, requiredFragmentCount, false))
            {
                SendExecutionGemMessage(player, Trinity::StringFormat("{} 제작에는 해당 파편이 {}개 필요합니다.", recipe->gemName, requiredFragmentCount));
                return SPELL_FAILED_DONT_REPORT;
            }

            if (!player->HasItemCount(ITEM_THANK_YOU_CERTIFICATE, REQUIRED_CERTIFICATE_COUNT, false))
            {
                SendExecutionGemMessage(player, Trinity::StringFormat("{} 제작에는 감사장이 {}장 필요합니다.", recipe->gemName, REQUIRED_CERTIFICATE_COUNT));
                return SPELL_FAILED_DONT_REPORT;
            }

            ItemPosCountVec dest;
            InventoryResult result = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, recipe->completeGem, 1);

            if (result != EQUIP_ERR_OK)
            {
                player->SendEquipError(result, nullptr, nullptr, recipe->completeGem);
                return SPELL_FAILED_DONT_REPORT;
            }

            return SPELL_CAST_OK;
        }

        void HandleAfterCast()
        {
            Unit* caster = GetCaster();

            if (!caster)
                return;

            Player* player = caster->ToPlayer();

            if (!player)
                return;

            Item* castItem = GetCastItem();

            if (!castItem)
                return;

            ExecutionGemFragmentRecipe const* recipe = GetExecutionGemFragmentRecipe(castItem->GetEntry());

            if (!recipe)
                return;

            uint32 requiredFragmentCount = GetRequiredFragmentCount(player);

            if (!player->HasItemCount(recipe->fragmentItem, requiredFragmentCount, false))
                return;

            if (!player->HasItemCount(ITEM_THANK_YOU_CERTIFICATE, REQUIRED_CERTIFICATE_COUNT, false))
                return;

            ItemPosCountVec dest;
            InventoryResult result = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, recipe->completeGem, 1);

            if (result != EQUIP_ERR_OK)
            {
                player->SendEquipError(result, nullptr, nullptr, recipe->completeGem);
                return;
            }

            player->DestroyItemCount(recipe->fragmentItem, requiredFragmentCount, true);
            player->DestroyItemCount(ITEM_THANK_YOU_CERTIFICATE, REQUIRED_CERTIFICATE_COUNT, true);

            Item* completeGem = player->StoreNewItem(dest, recipe->completeGem, true);

            if (completeGem)
                player->SendNewItem(completeGem, 1, true, false);

            SendExecutionGemMessage(player, Trinity::StringFormat("{} 제작이 완료되었습니다.", recipe->gemName));
        }

        void Register() override
        {
            OnCheckCast += SpellCheckCastFn(spell_execution_gem_fragment_combine_SpellScript::CheckCast);
            AfterCast += SpellCastFn(spell_execution_gem_fragment_combine_SpellScript::HandleAfterCast);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_execution_gem_fragment_combine_SpellScript();
    }
};










void AddSC_custom_execution_meta_gem()
{
    new custom_execution_meta_gem_unit_script();
    new custom_execution_meta_gem_player_script();
	new spell_execution_gem_fragment_combine();
}