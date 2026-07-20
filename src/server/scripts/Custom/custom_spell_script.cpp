#include "ScriptMgr.h"
#include "SpellScript.h"
#include "Spell.h"
#include "Player.h"
#include "WorldSession.h"
#include "CustomRP.h"
#include "SpellHistory.h"
#include "Item.h"
#include "Creature.h"
#include "CreatureData.h"
#include "Unit.h"
#include "CustomRP.h"
#include "GameTime.h"
#include "Random.h"
#include "Item.h"
#include "DatabaseEnv.h"
#include "Chat.h"
#include "ObjectMgr.h"
#include "WorldPacket.h"
#include "Opcodes.h"

#include <sstream>

#include <unordered_map>
#include <string>

static constexpr uint32 SPELL_HEARTHSTONE = 8690;
static constexpr uint32 ITEM_SECOND_HEARTHSTONE = 12585;
static constexpr uint8 SECOND_HOMEBIND_SLOT = 2;

struct SecondHomebindLocation
{
    uint32 MapId = 0;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float O = 0.0f;
};

static bool LoadSecondHomebindLocation(Player* player, SecondHomebindLocation& location)
{
    if (!player)
        return false;

    QueryResult result = CharacterDatabase.PQuery(
        "SELECT map_id, position_x, position_y, position_z, position_o "
        "FROM characters.custom_homebind_extra "
        "WHERE guid = {} "
        "AND slot = {} "
        "LIMIT 1",
        player->GetGUID().GetCounter(),
        uint32(SECOND_HOMEBIND_SLOT)
    );

    if (!result)
        return false;

    Field* fields = result->Fetch();

    location.MapId = fields[0].GetUInt32();
    location.X     = fields[1].GetFloat();
    location.Y     = fields[2].GetFloat();
    location.Z     = fields[3].GetFloat();
    location.O     = fields[4].GetFloat();

    return true;
}

class spell_guild_hearthstone : public SpellScriptLoader
{
public:
    spell_guild_hearthstone() : SpellScriptLoader("spell_guild_hearthstone") { }

    class spell_guild_hearthstone_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_guild_hearthstone_SpellScript);

        void HandleSecondHearthstoneTeleport(SpellEffIndex effIndex)
        {
            Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
            if (!player || !player->GetSession())
                return;

            Item* castItem = GetCastItem();
            if (!castItem)
                return;

            if (castItem->GetEntry() != ITEM_SECOND_HEARTHSTONE)
                return;

            PreventHitDefaultEffect(effIndex);

            SecondHomebindLocation location;
            if (!LoadSecondHomebindLocation(player, location))
            {
                player->GetSession()->SendNotification("두 번째 여관 위치가 저장되어 있지 않습니다.");
                return;
            }

            player->TeleportTo(
                location.MapId,
                location.X,
                location.Y,
                location.Z,
                location.O,
                TELE_TO_SPELL
            );
        }

        void HandleAfterCast()
        {
            Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
            if (!player || !player->GetSession())
                return;

            uint32 accountId = player->GetSession()->GetAccountId();
            uint32 guid = static_cast<uint32>(player->GetGUID().GetCounter());

            if (!HasGuildTechCompleted(accountId, guid, "r2b"))
                return;

            player->GetSpellHistory()->ResetCooldown(SPELL_HEARTHSTONE, true);

            player->GetSession()->SendNotification("길드 테크니크 귀환의 숙련 적용: 귀환석 재사용 시간이 초기화되었습니다.");
        }

        void Register() override
        {
            OnEffectHitTarget += SpellEffectFn(
                spell_guild_hearthstone_SpellScript::HandleSecondHearthstoneTeleport,
                EFFECT_0,
                SPELL_EFFECT_TELEPORT_UNITS
            );

            AfterCast += SpellCastFn(
                spell_guild_hearthstone_SpellScript::HandleAfterCast
            );
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_guild_hearthstone_SpellScript();
    }
};

static constexpr uint32 GUILD_TECH_BOSS_DAMAGE_BONUS_PERCENT = 5;
static constexpr uint32 GUILD_TECH_DAMAGE_REDUCE_PERCENT = 5;
static constexpr uint32 GUILD_TECH_HEAL_BONUS_PERCENT = 5;

// 테스트 후 2로 변경
static constexpr uint32 GUILD_TECH_FINAL_RESOLVE_CHANCE = 2;      // 테스트용 100%
static constexpr uint32 GUILD_TECH_FINAL_RESOLVE_COOLDOWN = 1800;   // 30분

static std::unordered_map<uint32, uint32> s_FinalResolveCooldown;

static bool IsBossTarget(Unit* target)
{
    if (!target)
        return false;

    Creature* creature = target->ToCreature();
    if (!creature)
        return false;

    CreatureTemplate const* cInfo = creature->GetCreatureTemplate();
    if (!cInfo)
        return false;

    return cInfo->rank == CREATURE_ELITE_WORLDBOSS;
}

static Player* GetPlayerFromUnit(Unit* unit)
{
    if (!unit)
        return nullptr;

    if (Player* player = unit->ToPlayer())
        return player;

    return unit->GetCharmerOrOwnerPlayerOrPlayerItself();
}

static bool HasGuildTech(Unit* unit, std::string const& nodeId)
{
    Player* player = GetPlayerFromUnit(unit);
    if (!player || !player->GetSession())
        return false;

    uint32 accountId = player->GetSession()->GetAccountId();
    uint32 guid = static_cast<uint32>(player->GetGUID().GetCounter());

    return HasGuildTechCompleted(accountId, guid, nodeId);
}

static uint32 AddPercentUInt32(uint32 value, uint32 percent)
{
    if (value == 0 || percent == 0)
        return value;

    uint64 bonus = (static_cast<uint64>(value) * percent) / 100;
    uint64 total = static_cast<uint64>(value) + bonus;

    if (total > 0xFFFFFFFFull)
        total = 0xFFFFFFFFull;

    return static_cast<uint32>(total);
}

static int32 AddPercentInt32(int32 value, uint32 percent)
{
    if (value <= 0 || percent == 0)
        return value;

    uint64 bonus = (static_cast<uint64>(value) * percent) / 100;
    uint64 total = static_cast<uint64>(value) + bonus;

    if (total > 0x7FFFFFFFull)
        total = 0x7FFFFFFFull;

    return static_cast<int32>(total);
}

static uint32 ReducePercentUInt32(uint32 value, uint32 percent)
{
    if (value == 0 || percent == 0)
        return value;

    uint64 reduce = (static_cast<uint64>(value) * percent) / 100;

    if (reduce >= value)
        return 0;

    return value - static_cast<uint32>(reduce);
}

static int32 ReducePercentInt32(int32 value, uint32 percent)
{
    if (value <= 0 || percent == 0)
        return value;

    uint64 reduce = (static_cast<uint64>(value) * percent) / 100;

    if (reduce >= static_cast<uint64>(value))
        return 0;

    return value - static_cast<int32>(reduce);
}

static bool TryFinalResolve(Unit* victim, uint32& damage)
{
    if (!victim || damage == 0)
        return false;

    Player* player = victim->ToPlayer();
    if (!player || !player->GetSession())
        return false;

    if (!player->IsAlive())
        return false;

    uint32 currentHealth = player->GetHealth();

    if (damage < currentHealth)
        return false;

    player->GetSession()->SendNotification("최후의 결의 사망 피해 감지");

    if (!HasGuildTech(player, "core"))
    {
        player->GetSession()->SendNotification("최후의 결의 실패: core 미완성");
        return false;
    }

    uint32 guid = static_cast<uint32>(player->GetGUID().GetCounter());
    uint32 now = GameTime::GetGameTime();

    auto itr = s_FinalResolveCooldown.find(guid);
    if (itr != s_FinalResolveCooldown.end())
    {
        if (itr->second > now)
        {
            player->GetSession()->SendNotification("최후의 결의 실패: 내부 쿨타임");
            return false;
        }
    }

    if (urand(1, 100) > GUILD_TECH_FINAL_RESOLVE_CHANCE)
    {
        player->GetSession()->SendNotification("최후의 결의 실패: 확률 미발동");
        return false;
    }

    s_FinalResolveCooldown[guid] = now + GUILD_TECH_FINAL_RESOLVE_COOLDOWN;

    damage = 0;

    player->SetHealth(player->GetMaxHealth());

    // 착용 장비 내구도 전체 복구
    player->DurabilityRepairAll(false, 0.0f, false);

    player->GetSession()->SendNotification("길드 테크니크 최후의 결의 발동! 생명력과 장비 내구도가 회복되었습니다.");

    return true;
}

class guild_tech_combat_bonus : public UnitScript
{
public:
    guild_tech_combat_bonus() : UnitScript("guild_tech_combat_bonus") { }

    // 근접 피해
    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
{
    if (IsBossTarget(target) && HasGuildTech(attacker, "r3a"))
    {
        uint32 before = damage;
        damage = AddPercentUInt32(damage, GUILD_TECH_BOSS_DAMAGE_BONUS_PERCENT);

        if (Player* player = GetPlayerFromUnit(attacker))
        {
            std::string msg = Trinity::StringFormat("파괴의 교리 적용: {} -> {}", before, damage);
            player->GetSession()->SendNotification("%s", msg.c_str());
        }
    }

    if (HasGuildTech(target, "r3b"))
    {
        uint32 before = damage;
        damage = ReducePercentUInt32(damage, GUILD_TECH_DAMAGE_REDUCE_PERCENT);

        if (Player* player = GetPlayerFromUnit(target))
        {
            std::string msg = Trinity::StringFormat("불굴의 수호 적용: {} -> {}", before, damage);
            player->GetSession()->SendNotification("%s", msg.c_str());
        }
    }
}

    // 주문 직접 피해
    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage) override
{
    if (damage <= 0)
        return;

    if (IsBossTarget(target) && HasGuildTech(attacker, "r3a"))
    {
        int32 before = damage;
        damage = AddPercentInt32(damage, GUILD_TECH_BOSS_DAMAGE_BONUS_PERCENT);

        if (Player* player = GetPlayerFromUnit(attacker))
        {
            std::string msg = Trinity::StringFormat("파괴의 교리 주문 적용: {} -> {}", before, damage);
            player->GetSession()->SendNotification("%s", msg.c_str());
        }
    }

    if (HasGuildTech(target, "r3b"))
    {
        int32 before = damage;
        damage = ReducePercentInt32(damage, GUILD_TECH_DAMAGE_REDUCE_PERCENT);

        if (Player* player = GetPlayerFromUnit(target))
        {
            std::string msg = Trinity::StringFormat("불굴의 수호 주문 적용: {} -> {}", before, damage);
            player->GetSession()->SendNotification("%s", msg.c_str());
        }
    }
}

    // 도트 피해
    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage) override
{
    if (IsBossTarget(target) && HasGuildTech(attacker, "r3a"))
    {
        uint32 before = damage;
        damage = AddPercentUInt32(damage, GUILD_TECH_BOSS_DAMAGE_BONUS_PERCENT);

        if (Player* player = GetPlayerFromUnit(attacker))
        {
            std::string msg = Trinity::StringFormat("파괴의 교리 도트 적용: {} -> {}", before, damage);
            player->GetSession()->SendNotification("%s", msg.c_str());
        }
    }

    if (HasGuildTech(target, "r3b"))
    {
        uint32 before = damage;
        damage = ReducePercentUInt32(damage, GUILD_TECH_DAMAGE_REDUCE_PERCENT);

        if (Player* player = GetPlayerFromUnit(target))
        {
            std::string msg = Trinity::StringFormat("불굴의 수호 도트 적용: {} -> {}", before, damage);
            player->GetSession()->SendNotification("%s", msg.c_str());
        }
    }
}

    // 치유 효과
    void OnHeal(Unit* healer, Unit* reciever, uint32& gain) override
	{
		if (HasGuildTech(reciever, "r3c"))
			gain = AddPercentUInt32(gain, GUILD_TECH_HEAL_BONUS_PERCENT);
	}

    // 최종 피해 처리
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        // core: 최후의 결의
        // 죽음에 이르는 피해를 받을 때 처리
        TryFinalResolve(victim, damage);
    }
};

#include "ScriptMgr.h"
#include "SpellScript.h"
#include "SpellInfo.h"
#include "Player.h"
#include "Creature.h"
#include "Unit.h"
#include "BattlefieldMgr.h"
#include "Battlefield/BattlefieldWG.h"

enum WgRocketPropelledGoblinGrenadeData
{
    SPELL_ROCKET_PROPELLED_GOBLIN_GRENADE = 49761,
    SPELL_ROCKET_PROPELLED_GOBLIN_MISSILE = 49769,

    WG_RPGG_MAX_RANGE = 70
};

class spell_wg_rocket_propelled_goblin_grenade : public SpellScript
{
    PrepareSpellScript(spell_wg_rocket_propelled_goblin_grenade);

private:
    static bool IsWintergraspVehicleEntry(uint32 entry)
    {
        switch (entry)
        {
            case NPC_WINTERGRASP_SIEGE_ENGINE_ALLIANCE:
            case NPC_WINTERGRASP_SIEGE_ENGINE_HORDE:
            case NPC_WINTERGRASP_CATAPULT:
            case NPC_WINTERGRASP_DEMOLISHER:
			case NPC_WINTERGRASP_TOWER_CANNON:
			case NPC_WG_CUSTOM_SIEGE_ELITE_DEMOLISHER:
			
                return true;
            default:
                break;
        }

        return false;
    }

    Unit* GetRocketTarget(Player* player)
    {
        if (!player)
            return nullptr;

        if (Unit* target = GetExplTargetUnit())
            return target;

        if (Unit* target = player->GetSelectedUnit())
            return target;

        ObjectGuid targetGuid = player->GetTarget();
        if (!targetGuid.IsEmpty())
            return ObjectAccessor::GetUnit(*player, targetGuid);

        return nullptr;
    }

    static bool IsSameTeamVehicle(Player* player, Creature* creature)
    {
        if (!player || !creature)
            return false;

        switch (creature->GetEntry())
        {
            case NPC_WINTERGRASP_SIEGE_ENGINE_ALLIANCE:
                return player->GetTeamId() == TEAM_ALLIANCE;

            case NPC_WINTERGRASP_SIEGE_ENGINE_HORDE:
                return player->GetTeamId() == TEAM_HORDE;

            case NPC_WINTERGRASP_CATAPULT:
            case NPC_WINTERGRASP_DEMOLISHER:
            {
                if (creature->HasAura(SPELL_ALLIANCE_FLAG))
                    return player->GetTeamId() == TEAM_ALLIANCE;

                if (creature->HasAura(SPELL_HORDE_FLAG))
                    return player->GetTeamId() == TEAM_HORDE;

                break;
            }
			case NPC_WINTERGRASP_TOWER_CANNON:
				return !player->IsValidAttackTarget(creature);

            default:
                break;
        }

        return false;
    }

    SpellCastResult CheckCast()
    {
        Unit* caster = GetCaster();
        if (!caster)
            return SPELL_FAILED_DONT_REPORT;

        Player* player = caster->ToPlayer();
        if (!player)
            return SPELL_FAILED_DONT_REPORT;

        if (player->GetMapId() != BATTLEFIELD_WG_MAPID || player->GetZoneId() != AREA_WINTERGRASP)
            return SPELL_FAILED_NOT_HERE;

        Battlefield* battlefield = sBattlefieldMgr->GetBattlefieldByBattleId(BATTLEFIELD_BATTLEID_WG);
        if (!battlefield || !battlefield->IsWarTime())
            return SPELL_FAILED_NOT_HERE;

        Unit* target = GetRocketTarget(player);
        if (!target)
            return SPELL_FAILED_BAD_TARGETS;

        Creature* creature = target->ToCreature();
        if (!creature)
            return SPELL_FAILED_BAD_TARGETS;

        if (!creature->IsAlive())
            return SPELL_FAILED_BAD_TARGETS;

        if (!IsWintergraspVehicleEntry(creature->GetEntry()))
            return SPELL_FAILED_BAD_TARGETS;

        if (!player->IsWithinDistInMap(creature, float(WG_RPGG_MAX_RANGE)))
            return SPELL_FAILED_OUT_OF_RANGE;

        if (IsSameTeamVehicle(player, creature))
            return SPELL_FAILED_BAD_TARGETS;

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

        Unit* target = GetRocketTarget(player);
        if (!target)
            return;

        Creature* creature = target->ToCreature();
        if (!creature)
            return;

        if (!creature->IsAlive())
            return;

        if (!IsWintergraspVehicleEntry(creature->GetEntry()))
            return;

        if (!player->IsWithinDistInMap(creature, float(WG_RPGG_MAX_RANGE)))
            return;

        if (IsSameTeamVehicle(player, creature))
            return;

        caster->CastSpell(creature, SPELL_ROCKET_PROPELLED_GOBLIN_MISSILE, true);
		if (Item* castItem = GetCastItem())
			player->DestroyItemCount(castItem->GetEntry(), 1, true);
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_wg_rocket_propelled_goblin_grenade::CheckCast);
        AfterCast += SpellCastFn(spell_wg_rocket_propelled_goblin_grenade::HandleAfterCast);
    }
};








#include "ScriptMgr.h"
#include "SpellScript.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Player.h"
#include "Item.h"
#include "DBCStores.h"

namespace
{
    enum CustomPreparationFirestoneData
    {
        // 우회 통로로 사용할 화염석 스펠
        SPELL_FIRESTONE_REDIRECT_BASE          = 55152,

        // 실제 아이템
        ITEM_ADAMANTITE_SHARPENING_STONE      = 23529,
        ITEM_ADAMANTITE_WEIGHTSTONE           = 28421,
        ITEM_SUPERIOR_WIZARD_OIL              = 22522,
        ITEM_SUPERIOR_MANA_OIL                = 22521,
        ITEM_FROST_OIL                        = 3829,

        // 원래 준비물 스펠
        SPELL_ADAMANTITE_SHARPENING_STONE     = 29453,
        SPELL_ADAMANTITE_WEIGHTSTONE          = 34340,
        SPELL_SUPERIOR_WIZARD_OIL             = 28017,
        SPELL_SUPERIOR_MANA_OIL               = 28013,
        SPELL_FROST_OIL                       = 3595,

        TEMP_ENCHANT_DURATION_1H              = 3600 * 1000,
        TEMP_ENCHANT_DURATION_30M             = 1800 * 1000
    };

    static bool IsCustomPreparationItem(uint32 itemEntry)
    {
        switch (itemEntry)
        {
            case ITEM_ADAMANTITE_SHARPENING_STONE:
            case ITEM_ADAMANTITE_WEIGHTSTONE:
            case ITEM_SUPERIOR_WIZARD_OIL:
            case ITEM_SUPERIOR_MANA_OIL:
            case ITEM_FROST_OIL:
                return true;
            default:
                return false;
        }
    }

	static bool IsStoneOrWeightstone(uint32 itemEntry)
	{
		switch (itemEntry)
		{
			case ITEM_ADAMANTITE_SHARPENING_STONE:
			case ITEM_ADAMANTITE_WEIGHTSTONE:
				return true;
			default:
				return false;
		}
	}

    static uint32 GetOriginalPreparationSpell(uint32 itemEntry)
    {
        switch (itemEntry)
        {
            case ITEM_ADAMANTITE_SHARPENING_STONE:
                return SPELL_ADAMANTITE_SHARPENING_STONE;
            case ITEM_ADAMANTITE_WEIGHTSTONE:
                return SPELL_ADAMANTITE_WEIGHTSTONE;
            case ITEM_SUPERIOR_WIZARD_OIL:
                return SPELL_SUPERIOR_WIZARD_OIL;
            case ITEM_SUPERIOR_MANA_OIL:
                return SPELL_SUPERIOR_MANA_OIL;
            case ITEM_FROST_OIL:
                return SPELL_FROST_OIL;
            default:
                return 0;
        }
    }

    static uint32 GetCustomEnchantDuration(uint32 itemEntry)
    {
        switch (itemEntry)
        {
            case ITEM_FROST_OIL:
                return TEMP_ENCHANT_DURATION_30M;
            default:
                return TEMP_ENCHANT_DURATION_1H;
        }
    }

    static bool IsWeapon(Item const* item)
    {
        if (!item || !item->GetTemplate())
            return false;

        return item->GetTemplate()->Class == ITEM_CLASS_WEAPON;
    }

    static bool IsBladedWeapon(Item const* item)
    {
        if (!item || !item->GetTemplate())
            return false;

        ItemTemplate const* proto = item->GetTemplate();

        if (proto->Class != ITEM_CLASS_WEAPON)
            return false;

        switch (proto->SubClass)
        {
            case ITEM_SUBCLASS_WEAPON_AXE:
            case ITEM_SUBCLASS_WEAPON_AXE2:
            case ITEM_SUBCLASS_WEAPON_SWORD:
            case ITEM_SUBCLASS_WEAPON_SWORD2:
            case ITEM_SUBCLASS_WEAPON_DAGGER:
			case ITEM_SUBCLASS_WEAPON_POLEARM:
                return true;
            default:
                return false;
        }
    }

    static bool IsBluntWeapon(Item const* item)
    {
        if (!item || !item->GetTemplate())
            return false;

        ItemTemplate const* proto = item->GetTemplate();

        if (proto->Class != ITEM_CLASS_WEAPON)
            return false;

        switch (proto->SubClass)
        {
            case ITEM_SUBCLASS_WEAPON_MACE:
            case ITEM_SUBCLASS_WEAPON_MACE2:
            case ITEM_SUBCLASS_WEAPON_STAFF:
                return true;
            default:
                return false;
        }
    }

    static bool IsValidTargetWeaponForItem(uint32 itemEntry, Item const* targetItem)
    {
        switch (itemEntry)
        {
            case ITEM_ADAMANTITE_SHARPENING_STONE:
                return IsBladedWeapon(targetItem);

            case ITEM_ADAMANTITE_WEIGHTSTONE:
                return IsBluntWeapon(targetItem);

            case ITEM_SUPERIOR_WIZARD_OIL:
            case ITEM_SUPERIOR_MANA_OIL:
            case ITEM_FROST_OIL:
                return IsWeapon(targetItem);

            default:
                return false;
        }
    }

    static uint32 GetEnchantIdFromOriginalSpell(uint32 spellId)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            return 0;

        SpellEffectInfo const& effect = spellInfo->GetEffect(EFFECT_0);
        return uint32(effect.MiscValue);
    }

    static bool ApplyPreparationEnchant(Player* player, Item* targetItem, uint32 itemEntry)
    {
        if (!player || !targetItem)
            return false;

        uint32 const originalSpellId = GetOriginalPreparationSpell(itemEntry);
        if (!originalSpellId)
            return false;

        uint32 const enchantId = GetEnchantIdFromOriginalSpell(originalSpellId);
        if (!enchantId)
            return false;

        if (!sSpellItemEnchantmentStore.LookupEntry(enchantId))
            return false;

        uint32 const duration = GetCustomEnchantDuration(itemEntry);

        player->ApplyEnchantment(targetItem, TEMP_ENCHANTMENT_SLOT, false);
        targetItem->SetEnchantment(TEMP_ENCHANTMENT_SLOT, enchantId, duration, 0, player->GetGUID());
        player->ApplyEnchantment(targetItem, TEMP_ENCHANTMENT_SLOT, true);
        player->AddEnchantmentDuration(targetItem, TEMP_ENCHANTMENT_SLOT, duration);

        return true;
    }
}

class spell_custom_firestone_preparation_redirect : public SpellScript
{
    PrepareSpellScript(spell_custom_firestone_preparation_redirect);

    SpellCastResult CheckCast()
	{
		Item* castItem = GetCastItem();
		if (!castItem)
			return SPELL_CAST_OK;

		uint32 const itemEntry = castItem->GetEntry();

		// 원래 화염석 아이템이면 기본 화염석 효과 그대로 진행
		if (!IsCustomPreparationItem(itemEntry))
			return SPELL_CAST_OK;

		Item* targetItem = GetExplTargetItem();
		if (!targetItem)
			return SPELL_FAILED_DONT_REPORT;

		if (!IsValidTargetWeaponForItem(itemEntry, targetItem))
		{
			if (IsStoneOrWeightstone(itemEntry))
			{
				if (Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr)
					player->GetSession()->SendAreaTriggerMessage("적용할 수 없는 아이템입니다.");
			}

			return SPELL_FAILED_DONT_REPORT;
		}

		return SPELL_CAST_OK;
	}

    void HandleEffect(SpellEffIndex effIndex)
    {
        Item* castItem = GetCastItem();
        if (!castItem)
            return;

        uint32 const itemEntry = castItem->GetEntry();

        // 원래 화염석 아이템이면 기본 효과 유지
        if (!IsCustomPreparationItem(itemEntry))
            return;

        PreventHitDefaultEffect(effIndex);

        Player* player = GetCaster() ? GetCaster()->ToPlayer() : nullptr;
        if (!player)
            return;

        Item* targetItem = GetHitItem();
        if (!targetItem)
            targetItem = GetExplTargetItem();

        if (!targetItem)
            return;

        if (!IsValidTargetWeaponForItem(itemEntry, targetItem))
            return;

        ApplyPreparationEnchant(player, targetItem, itemEntry);
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_custom_firestone_preparation_redirect::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_custom_firestone_preparation_redirect::HandleEffect, EFFECT_0, SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY);
    }
};

//보석업그렝드
/*
 * Custom Update - 2026-06-27
 *
 * 보석 결합 시스템
 *
 * 스펠:
 * - 900200 보석 결합
 *
 * 구조:
 * - 보석 결합 도구 사용
 * - 가방 안의 보석 아이템 선택
 * - appdata.gem_upgrade_recipe 기준으로 source_item -> target_item 처리
 *
 * 출력 메시지:
 * - 감사장 부족
 * - 쿨다운 남은 시간
 *
 * 결합 규칙:
 * - 같은 source_item 3개 필요
 * - 하 -> 중하: 쿨타임 없음
 * - 중하 -> 중: 쿨타임 없음
 * - 중 -> 중상: 보석세공사 쿨타임 없음 / 비세공사 4시간
 * - 중상 -> 상: 보석세공사 쿨타임 없음 / 비세공사 12시간
 * - 상 -> 최상급: 보석세공사만 가능 / 보석세공사 4시간 / 감사장 10장 필요
 */

#include "ScriptMgr.h"
#include "SpellScript.h"
#include "Player.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"
#include "StringFormat.h"
#include "SpellHistory.h"
#include "Duration.h"

#include <ctime>
#include <string>

namespace
{
    constexpr uint32 SPELL_GEM_COMBINE = 900200;

    constexpr char const* GEM_UPGRADE_RECIPE_TABLE = "`appdata`.`gem_upgrade_recipe`";

    struct GemUpgradeRecipe
    {
        uint32 TargetItem = 0;
        uint32 NeedCount = 3;
        std::string SourceStage;
        std::string TargetStage;
    };

    void SendGemCombineMessage(Player* player, char const* message)
    {
        if (!player || !player->GetSession())
            return;

        player->GetSession()->SendNotification("%s", message);
    }

    bool LoadGemUpgradeRecipe(uint32 sourceItem, GemUpgradeRecipe& recipe)
    {
        /*
         * required_skill 은 읽지 않는다.
         * 쿨타임 판별은 source_stage / target_stage 만 사용한다.
         */
        std::string sql = Trinity::StringFormat(
            "SELECT "
            "`target_item`, "
            "`need_count`, "
            "`source_stage`, "
            "`target_stage` "
            "FROM {} "
            "WHERE `source_item` = {} "
            "AND `enabled` = 1 "
            "LIMIT 1",
            GEM_UPGRADE_RECIPE_TABLE,
            sourceItem
        );

        QueryResult result = WorldDatabase.Query(sql.c_str());

        if (!result)
            return false;

        Field* fields = result->Fetch();

        recipe.TargetItem = fields[0].GetUInt32();
        recipe.NeedCount = fields[1].GetUInt32();
        recipe.SourceStage = fields[2].GetString();
        recipe.TargetStage = fields[3].GetString();

        if (recipe.NeedCount == 0)
            recipe.NeedCount = 3;

        return recipe.TargetItem > 0;
    }
}

class spell_custom_gem_combine : public SpellScriptLoader
{
public:
    spell_custom_gem_combine() : SpellScriptLoader("spell_custom_gem_combine") { }

    class spell_custom_gem_combine_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_custom_gem_combine_SpellScript);

    private:
        static constexpr uint32 GEM_TOP_COST_ITEM = 45924;
        static constexpr uint32 GEM_TOP_COST_COUNT = 10;

        uint32 _sourceItem = 0;
        uint32 _cooldownSeconds = 0;
        bool _hasRecipe = false;
        GemUpgradeRecipe _recipe;

        void ResetCachedRecipe()
        {
            _sourceItem = 0;
            _cooldownSeconds = 0;
            _hasRecipe = false;
            _recipe = GemUpgradeRecipe();
        }

        uint32 GetGemCombineSpellId() const
        {
            SpellInfo const* spellInfo = GetSpellInfo();

            if (spellInfo)
                return spellInfo->Id;

            return SPELL_GEM_COMBINE;
        }

        bool IsJewelcrafter(Player* player) const
        {
            if (!player)
                return false;

            /*
             * 보석세공사 판정은 SkillLine 755로 하지 않는다.
             * 실제 보석세공 전문기술 주문을 배운 캐릭터만 보석세공사로 판정한다.
             */
            return player->HasSpell(25229) ||
                   player->HasSpell(25230) ||
                   player->HasSpell(28894) ||
                   player->HasSpell(28895) ||
                   player->HasSpell(28897) ||
                   player->HasSpell(51311);
        }

        bool IsStageUpgrade(GemUpgradeRecipe const& recipe, char const* sourceStage, char const* targetStage) const
        {
            return recipe.SourceStage == sourceStage && recipe.TargetStage == targetStage;
        }

        bool IsHighToTopUpgrade(GemUpgradeRecipe const& recipe) const
        {
            return IsStageUpgrade(recipe, "high", "top");
        }

        bool CheckHighToTopJewelcrafter(Player* player, GemUpgradeRecipe const& recipe)
		{
			if (!IsHighToTopUpgrade(recipe))
				return true;

			if (!IsJewelcrafter(player))
			{
				SendGemCombineMessage(player, "최상급 보석 결합은 보석세공사만 가능합니다.");
				return false;
			}

			return true;
		}

        uint32 GetGemCombineCooldownSeconds(Player* player, GemUpgradeRecipe const& recipe) const
        {
            bool jewelcrafter = IsJewelcrafter(player);

            if (IsStageUpgrade(recipe, "low", "mid_low") ||
                IsStageUpgrade(recipe, "mid_low", "mid"))
                return 0;

            if (IsStageUpgrade(recipe, "mid", "mid_high"))
                return jewelcrafter ? 0 : 4 * 60 * 60;

            if (IsStageUpgrade(recipe, "mid_high", "high"))
                return jewelcrafter ? 0 : 12 * 60 * 60;

            if (IsStageUpgrade(recipe, "high", "top"))
                return 4 * 60 * 60;

            return 0;
        }

        std::string FormatGemCombineCooldown(uint32 seconds) const
        {
            uint32 hours = seconds / 3600;
            uint32 minutes = (seconds % 3600) / 60;

            if (hours > 0 && minutes > 0)
                return Trinity::StringFormat("{}시간 {}분", hours, minutes);

            if (hours > 0)
                return Trinity::StringFormat("{}시간", hours);

            if (minutes == 0)
                minutes = 1;

            return Trinity::StringFormat("{}분", minutes);
        }

        uint32 GetGemCombineCooldownRemaining(Player* player) const
        {
            if (!player)
                return 0;

            uint32 now = uint32(::time(nullptr));
            uint32 spellId = GetGemCombineSpellId();
            uint64 guid = player->GetGUID().GetCounter();

            std::string query = Trinity::StringFormat(
                "SELECT `time` "
                "FROM `character_spell_cooldown` "
                "WHERE `guid` = {} "
                "AND `spell` = {} "
                "AND `item` = 0",
                guid,
                spellId
            );

            QueryResult result = CharacterDatabase.Query(query.c_str());

            if (!result)
                return 0;

            Field* fields = result->Fetch();
            uint32 cooldownUntil = fields[0].GetUInt32();

            if (cooldownUntil <= now)
            {
                std::string deleteQuery = Trinity::StringFormat(
                    "DELETE FROM `character_spell_cooldown` "
                    "WHERE `guid` = {} "
                    "AND `spell` = {} "
                    "AND `item` = 0",
                    guid,
                    spellId
                );

                CharacterDatabase.DirectExecute(deleteQuery.c_str());
                return 0;
            }

            return cooldownUntil - now;
        }

        bool CheckGemCombineCooldown(Player* player, uint32 cooldownSeconds)
        {
            if (cooldownSeconds == 0)
                return true;

            uint32 remaining = GetGemCombineCooldownRemaining(player);

            if (remaining == 0)
                return true;

            std::string message = Trinity::StringFormat(
                "보석 결합 재사용 대기시간이 남아 있습니다. 남은 시간: {}",
                FormatGemCombineCooldown(remaining)
            );

            SendGemCombineMessage(player, message.c_str());
            return false;
        }

		void SendGemCombineCooldownPacket(Player* player, uint32 spellId, uint32 cooldownSeconds)
		{
			if (!player || cooldownSeconds == 0)
				return;

			WorldPacket data(SMSG_SPELL_COOLDOWN, 8 + 1 + 4 + 4);
			data << uint64(player->GetGUID().GetRawValue());
			data << uint8(0);
			data << uint32(spellId);
			data << uint32(cooldownSeconds * 1000);

			player->SendDirectMessage(&data);
		}

        void ApplyGemCombineCooldown(Player* player, uint32 cooldownSeconds)
        {
            if (!player)
                return;

            if (cooldownSeconds == 0)
                return;

            uint32 now = uint32(::time(nullptr));
            uint32 cooldownUntil = now + cooldownSeconds;
            uint32 spellId = GetGemCombineSpellId();
            uint64 guid = player->GetGUID().GetCounter();

            player->GetSpellHistory()->AddCooldown(spellId, 0, Seconds(cooldownSeconds));
			SendGemCombineCooldownPacket(player, spellId, cooldownSeconds);

            std::string query = Trinity::StringFormat(
                "REPLACE INTO `character_spell_cooldown` "
                "(`guid`, `spell`, `item`, `time`, `categoryId`, `categoryEnd`) "
                "VALUES "
                "({}, {}, 0, {}, 0, 0)",
                guid,
                spellId,
                cooldownUntil
            );

            CharacterDatabase.DirectExecute(query.c_str());
        }

        bool CheckHighToTopCost(Player* player, GemUpgradeRecipe const& recipe)
        {
            if (!IsHighToTopUpgrade(recipe))
                return true;

            if (!player->HasItemCount(GEM_TOP_COST_ITEM, GEM_TOP_COST_COUNT, false))
            {
                SendGemCombineMessage(player, "상급 보석을 최상급으로 결합하려면 감사장 10장이 필요합니다.");
                return false;
            }

            return true;
        }

        void ConsumeHighToTopCost(Player* player, GemUpgradeRecipe const& recipe)
        {
            if (!IsHighToTopUpgrade(recipe))
                return;

            player->DestroyItemCount(GEM_TOP_COST_ITEM, GEM_TOP_COST_COUNT, true);
        }

        SpellCastResult CheckCast()
		{
			ResetCachedRecipe();

			Unit* caster = GetCaster();

			if (!caster)
				return SPELL_FAILED_DONT_REPORT;

			Player* player = caster->ToPlayer();

			if (!player)
				return SPELL_FAILED_DONT_REPORT;

			Item* sourceGem = GetExplTargetItem();

			if (!sourceGem)
			{
				SendGemCombineMessage(player, "결합할 보석을 선택해야 합니다.");
				return SPELL_FAILED_DONT_REPORT;
			}

			ItemTemplate const* sourceTemplate = sourceGem->GetTemplate();

			if (!sourceTemplate)
			{
				SendGemCombineMessage(player, "선택한 아이템 정보를 확인할 수 없습니다.");
				return SPELL_FAILED_DONT_REPORT;
			}

			if (sourceTemplate->Class != ITEM_CLASS_GEM || sourceTemplate->GemProperties == 0)
			{
				SendGemCombineMessage(player, "보석 아이템만 결합할 수 있습니다.");
				return SPELL_FAILED_DONT_REPORT;
			}

			uint32 sourceItem = sourceGem->GetEntry();

			GemUpgradeRecipe recipe;

			if (!LoadGemUpgradeRecipe(sourceItem, recipe))
			{
				SendGemCombineMessage(player, "이 보석은 결합 가능한 목록에 없습니다.");
				return SPELL_FAILED_DONT_REPORT;
			}

			if (!sObjectMgr->GetItemTemplate(recipe.TargetItem))
			{
				SendGemCombineMessage(player, "결합 결과 아이템 정보를 찾을 수 없습니다.");
				return SPELL_FAILED_DONT_REPORT;
			}

			if (!player->HasItemCount(sourceItem, recipe.NeedCount, false))
			{
				std::string message = Trinity::StringFormat(
					"결합 재료가 부족합니다. 같은 보석이 {}개 필요합니다.",
					recipe.NeedCount
				);

				SendGemCombineMessage(player, message.c_str());
				return SPELL_FAILED_DONT_REPORT;
			}

			if (!CheckHighToTopJewelcrafter(player, recipe))
				return SPELL_FAILED_DONT_REPORT;

			if (!CheckHighToTopCost(player, recipe))
				return SPELL_FAILED_DONT_REPORT;

			uint32 cooldownSeconds = GetGemCombineCooldownSeconds(player, recipe);

			if (!CheckGemCombineCooldown(player, cooldownSeconds))
				return SPELL_FAILED_DONT_REPORT;

			ItemPosCountVec dest;
			InventoryResult canStore = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, recipe.TargetItem, 1);

			if (canStore != EQUIP_ERR_OK)
			{
				SendGemCombineMessage(player, "가방 공간이 부족하여 결합할 수 없습니다.");
				return SPELL_FAILED_DONT_REPORT;
			}

			_sourceItem = sourceItem;
			_cooldownSeconds = cooldownSeconds;
			_recipe = recipe;
			_hasRecipe = true;

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

            if (!_hasRecipe || !_sourceItem)
                return;

            uint32 sourceItem = _sourceItem;
            GemUpgradeRecipe const& recipe = _recipe;

            if (!sObjectMgr->GetItemTemplate(recipe.TargetItem))
                return;

            if (!player->HasItemCount(sourceItem, recipe.NeedCount, false))
                return;

            if (!CheckHighToTopJewelcrafter(player, recipe))
                return;

            if (!CheckHighToTopCost(player, recipe))
                return;

            uint32 afterCooldownSeconds = GetGemCombineCooldownSeconds(player, recipe);
            _cooldownSeconds = afterCooldownSeconds;

            if (!CheckGemCombineCooldown(player, _cooldownSeconds))
                return;

            ItemPosCountVec dest;
            InventoryResult canStore = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, recipe.TargetItem, 1);

            if (canStore != EQUIP_ERR_OK)
                return;

            player->DestroyItemCount(sourceItem, recipe.NeedCount, true);
            ConsumeHighToTopCost(player, recipe);

            Item* newItem = player->StoreNewItem(dest, recipe.TargetItem, true);

            if (!newItem)
                return;

            ApplyGemCombineCooldown(player, _cooldownSeconds);
            ResetCachedRecipe();
        }

        void Register() override
        {
            OnCheckCast += SpellCheckCastFn(spell_custom_gem_combine_SpellScript::CheckCast);
            AfterCast += SpellCastFn(spell_custom_gem_combine_SpellScript::HandleAfterCast);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_custom_gem_combine_SpellScript();
    }
};

// 수호자의 정화
enum GuardianPurifySpells
{
    SPELL_GUARDIAN_PURIFY = 900202
};

// 수호자의 정화로 제거할 중첩 디버프 스펠 ID를 여기에 직접 넣는다.
static constexpr uint32 GuardianPurifyAuraIds[] =
{
    72004 // 토라본 동상
};

static bool HasGuardianPurifyAura(Unit* target)
{
    if (!target)
        return false;

    for (uint32 auraId : GuardianPurifyAuraIds)
        if (auraId && target->HasAura(auraId))
            return true;

    return false;
}

class spell_guardian_purify : public SpellScript
{
    PrepareSpellScript(spell_guardian_purify);

    void SendCasterNotification(char const* message)
    {
        Unit* casterUnit = GetCaster();
        if (Player* caster = casterUnit ? casterUnit->ToPlayer() : nullptr)
            caster->GetSession()->SendNotification("%s", message);
    }

    SpellCastResult CheckCast()
    {
        Unit* casterUnit = GetCaster();
        Unit* targetUnit = GetExplTargetUnit();

        if (!casterUnit || !targetUnit)
            return SPELL_FAILED_BAD_TARGETS;

        Player* caster = casterUnit->ToPlayer();
        Player* target = targetUnit->ToPlayer();

        if (!caster || !target)
            return SPELL_FAILED_BAD_TARGETS;

        if (caster == target)
            return SPELL_FAILED_BAD_TARGETS;

        if (!caster->IsInSameRaidWith(target))
            return SPELL_FAILED_BAD_TARGETS;

        if (!HasGuardianPurifyAura(targetUnit))
        {
            SendCasterNotification("정화할 수 있는 중첩이 없습니다.");
            return SPELL_FAILED_DONT_REPORT;
        }

        return SPELL_CAST_OK;
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        Unit* targetUnit = GetHitUnit();

        if (!targetUnit)
            targetUnit = GetExplTargetUnit();

        if (!targetUnit)
            return;

        for (uint32 auraId : GuardianPurifyAuraIds)
        {
            if (!auraId || !targetUnit->HasAura(auraId))
                continue;

            targetUnit->RemoveAuraFromStack(auraId);
            return;
        }
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_guardian_purify::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_guardian_purify::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

//전승룬
#include "ScriptMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldSession.h"
#include "WorldPacket.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_map>

namespace
{
    static constexpr uint32 LEGACY_RUNE_ALLOWED_ITEM_CLASS = 17;
    static constexpr uint8 LEGACY_RUNE_SLOT_MAIN = 1;
    static std::string const LEGACY_RUNE_ADDON_PREFIX = "LRUNE";

    /*
     * Custom Update - 2026-07-18
     * 전승 룬 애드온 설치 및 버전 확인
     */
    static constexpr uint32 LEGACY_RUNE_ADDON_REQUIRED_VERSION = 1;
    static constexpr uint32 LEGACY_RUNE_ADDON_LOGIN_CHECK_DELAY_SECONDS = 8;
    static std::string const LEGACY_RUNE_ADDON_REQUIRED_VERSION_TEXT = "1";
    static std::string const LEGACY_RUNE_ADDON_VERSION_PREFIX = "VERSION:";

    /*
     * Custom Update - 2026-07-18
     * 패치4 Spell.dbc 표식 감지 및 미설치 안내
     */
    static std::string const LEGACY_RUNE_PATCH4_OK_COMMAND = "PATCH4:OK";
    static std::string const LEGACY_RUNE_PATCH4_MISSING_COMMAND = "PATCH4:MISSING";

    /*
     * Custom Update - 2026-07-18
     * 전승 룬 애드온에서 길드 테크 조회 및 RP 기부 처리
     */
    static std::string const LEGACY_RUNE_APPLY_PREFIX = "APPLY:";
    static std::string const LEGACY_RUNE_CHECK_PREFIX = "CHECK:";
    static std::string const LEGACY_RUNE_COMBINE_PREFIX = "COMBINE:";
    static std::string const LEGACY_RUNE_COMBINE_GET_PREFIX = "GETCOMBINE:";
    static std::string const LEGACY_RUNE_GET_COMMAND = "GET";
    static std::string const LEGACY_RUNE_ARMORY_OPEN_COMMAND = "ARMORY:OPEN";
    static std::string const LEGACY_RUNE_ARMORY_SEARCH_PREFIX = "ARMORY:SEARCH:";
    static std::string const LEGACY_RUNE_ARMORY_BUY_PREFIX = "ARMORY:BUY:";
    static std::string const LEGACY_RUNE_GUILD_TECH_OPEN_COMMAND = "GUILDTECH:OPEN";
    static std::string const LEGACY_RUNE_GUILD_TECH_DONATE_PREFIX = "GUILDTECH:DONATE:";

    /*
     * Custom Update - 2026-07-19
     * 마법 재단 오브젝트 클릭 시 전승 룬 애드온 조합 창 열기
     */
    static std::string const LEGACY_RUNE_MAGIC_ALTAR_OPEN_COMMAND = "MAGICALTAR:OPEN";
    static std::string const LEGACY_RUNE_MAGIC_ALTAR_COMBINE_COMMAND = "MAGICALTAR:COMBINE";
    static constexpr uint32 LEGACY_RUNE_MAGIC_ALTAR_RESULT_ITEM_ID = 23162;
    static constexpr uint32 LEGACY_RUNE_MAGIC_ALTAR_BAG_ITEM_ID = 21193;
    static constexpr uint32 LEGACY_RUNE_MAGIC_ALTAR_BAG_REQUIRED_COUNT = 1;
    static constexpr uint32 LEGACY_RUNE_MAGIC_ALTAR_CRYSTAL_ITEM_ID = 56941;
    static constexpr uint32 LEGACY_RUNE_MAGIC_ALTAR_CRYSTAL_REQUIRED_COUNT = 2;
    static constexpr uint32 LEGACY_RUNE_MAGIC_ALTAR_FROZEN_ORB_ITEM_ID = 43102;
    static constexpr uint32 LEGACY_RUNE_MAGIC_ALTAR_FROZEN_ORB_REQUIRED_COUNT = 20;
    static constexpr uint32 LEGACY_RUNE_MAGIC_ALTAR_ABYSS_CRYSTAL_ITEM_ID = 34057;
    static constexpr uint32 LEGACY_RUNE_MAGIC_ALTAR_ABYSS_CRYSTAL_REQUIRED_COUNT = 15;
    static constexpr uint32 LEGACY_RUNE_MAGIC_ALTAR_ACCESS_SECONDS = 300;
    static constexpr uint32 LEGACY_RUNE_ARMORY_SEARCH_LIMIT = 100;
    static constexpr uint32 LEGACY_RUNE_ARMORY_GUILD_TECH_DISCOUNT_PERCENT = 15;

    struct LegacyRuneSpellMap
    {
        uint32 applyItemEntry;
        uint32 removeItemEntry;
        uint32 spellId;
        uint32 skillId;
        uint8 blockedClass;
    };

    struct LegacyRuneItemRef
    {
        uint32 itemEntry;
        uint32 clientBag;
        uint32 clientSlot;
        uint64 itemGuid;
    };

    struct LegacyRuneCombineMap
    {
        uint32 reagent1;
        uint32 reagent2;
        uint32 reagent3;
        uint32 resultItemEntry;
    };

    struct LegacyRuneArmoryPriceInfo
    {
        uint32 pointPrice;
        uint32 originalRegPointPrice;
        uint32 regPointPrice;
        uint32 regPointDiscount;
        bool guildTechApplied;
    };

    struct LegacyRuneGuildTechNode
    {
        char const* nodeId;
        char const* name;
        uint32 targetAmount;
        uint8 tier;
    };

    struct LegacyRuneAddonLoginCheck
    {
        std::chrono::steady_clock::time_point timeoutAt;
        bool addonDetected;
    };

    static std::unordered_map<uint32, LegacyRuneAddonLoginCheck> LegacyRuneAddonLoginChecks;
    static std::unordered_map<uint32, std::chrono::steady_clock::time_point> LegacyRuneMagicAltarAccess;

    static constexpr LegacyRuneSpellMap LegacyRuneSpellMaps[] =
    {
        { 56930, 0, 1804, 633, 4 }, // 전승 룬 : 자물쇠 따기, 도적 착용 불가
        { 56931, 0, 1953, 0, 8 },   // 전승 룬 : 점멸, 마법사 착용 불가
        { 56932, 0, 5384, 0, 3 }    // 전승 룬 : 죽은척 하기, 사냥꾼 착용 불가
    };

    static constexpr uint32 LegacyRuneCombineReagents[] =
    {
        56919, // 처형의 얼개보석 : 절단
        56920, // 처형의 얼개보석 : 관통
        56921, // 처형의 얼개보석 : 붕괴
        56922, // 처형의 얼개보석 : 압도
        56923  // 처형의 얼개보석 : 구원
    };

    static constexpr LegacyRuneCombineMap LegacyRuneCombineMaps[] =
    {
        { 56919, 56920, 56921, 56930 },
        { 56919, 56920, 56922, 56931 },
        { 56919, 56920, 56923, 56932 },
        { 56919, 56921, 56922, 56933 },
        { 56919, 56921, 56923, 56934 },
        { 56919, 56922, 56923, 56935 },
        { 56920, 56921, 56922, 56936 },
        { 56920, 56921, 56923, 56937 },
        { 56920, 56922, 56923, 56938 },
        { 56921, 56922, 56923, 56939 }
    };

    static constexpr LegacyRuneGuildTechNode LegacyRuneGuildTechNodes[] =
    {
        { "r1a", "황금의 흐름", 200000, 1 },
        { "r1b", "상인의 교섭", 200000, 1 },
        { "r1c", "기여의 확장", 200000, 1 },
        { "r2a", "결속의 전달", 400000, 2 },
        { "r2b", "귀환의 숙련", 400000, 2 },
        { "r2c", "장비의 관리", 400000, 2 },
        { "r3a", "파괴의 교리", 600000, 3 },
        { "r3b", "불굴의 수호", 600000, 3 },
        { "r3c", "생명의 회복", 600000, 3 },
        { "core", "최후의 결의", 800000, 4 }
    };

    static LegacyRuneSpellMap const* FindLegacyRuneApplyMap(uint32 itemEntry)
    {
        for (LegacyRuneSpellMap const& runeMap : LegacyRuneSpellMaps)
            if (runeMap.applyItemEntry == itemEntry)
                return &runeMap;

        return nullptr;
    }

    static LegacyRuneSpellMap const* FindLegacyRuneRemoveMap(uint32 itemEntry)
    {
        for (LegacyRuneSpellMap const& runeMap : LegacyRuneSpellMaps)
            if (runeMap.removeItemEntry == itemEntry)
                return &runeMap;

        return nullptr;
    }

    static bool IsLegacyRuneCombineReagent(uint32 itemEntry)
    {
        for (uint32 reagentEntry : LegacyRuneCombineReagents)
            if (reagentEntry == itemEntry)
                return true;

        return false;
    }

    static uint32 FindLegacyRuneCombineResult(uint32 itemEntry1, uint32 itemEntry2, uint32 itemEntry3)
    {
        uint32 entries[3] = { itemEntry1, itemEntry2, itemEntry3 };
        std::sort(entries, entries + 3);

        for (LegacyRuneCombineMap const& combineMap : LegacyRuneCombineMaps)
        {
            if (combineMap.reagent1 == entries[0] && combineMap.reagent2 == entries[1] && combineMap.reagent3 == entries[2])
                return combineMap.resultItemEntry;
        }

        return 0;
    }

    static char const* GetLegacyRuneApplyBlockReason(Player* player, ItemTemplate const* proto)
    {
        if (!player || !proto)
            return "아이템 정보를 확인할 수 없습니다.";

        if (proto->Class != LEGACY_RUNE_ALLOWED_ITEM_CLASS)
            return "전승 룬 클래스 아이템만 착용할 수 있습니다.";

        LegacyRuneSpellMap const* currentRuneMap = FindLegacyRuneApplyMap(proto->ItemId);
        if (!currentRuneMap)
            return "등록되지 않은 전승 룬 아이템입니다.";

        if (currentRuneMap->blockedClass && player->GetClass() == currentRuneMap->blockedClass)
            return "해당 직업은 이 전승 룬을 착용할 수 없습니다.";

        return nullptr;
    }

    static bool ConvertLegacyRuneClientPos(uint32 clientBag, uint32 clientSlot, uint8& bag, uint8& slot)
    {
        if (!clientSlot)
            return false;

        if (clientBag == 0)
        {
            uint32 slotCount = INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START;
            if (clientSlot > slotCount)
                return false;

            bag = INVENTORY_SLOT_BAG_0;
            slot = uint8(INVENTORY_SLOT_ITEM_START + clientSlot - 1);
            return true;
        }

        if (clientBag >= 1 && clientBag <= (INVENTORY_SLOT_BAG_END - INVENTORY_SLOT_BAG_START))
        {
            bag = uint8(INVENTORY_SLOT_BAG_START + clientBag - 1);
            slot = uint8(clientSlot - 1);
            return true;
        }

        return false;
    }

    static Item* FindLegacyRuneItem(Player* player, LegacyRuneItemRef const& itemRef)
    {
        if (!player || !itemRef.itemEntry)
            return nullptr;

        uint8 bag = 0;
        uint8 slot = 0;
        if (!ConvertLegacyRuneClientPos(itemRef.clientBag, itemRef.clientSlot, bag, slot))
            return nullptr;

        Item* item = player->GetItemByPos(bag, slot);
        if (!item)
            return nullptr;

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || proto->ItemId != itemRef.itemEntry)
            return nullptr;

        if (itemRef.itemGuid && item->GetGUID().GetCounter() != itemRef.itemGuid)
            return nullptr;

        return item;
    }

    static uint64 GetSavedLegacyRuneItemGuid(Player* player)
    {
        if (!player)
            return 0;

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT item_guid "
            "FROM legacy_rune_character "
            "WHERE guid = {} AND rune_slot = {} "
            "LIMIT 1",
            player->GetGUID().GetCounter(),
            LEGACY_RUNE_SLOT_MAIN);

        if (!result)
            return 0;

        return result->Fetch()[0].GetUInt64();
    }

    static uint32 GetSavedLegacyRuneItemEntry(Player* player)
    {
        if (!player)
            return 0;

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT item_entry "
            "FROM legacy_rune_character "
            "WHERE guid = {} AND rune_slot = {} "
            "LIMIT 1",
            player->GetGUID().GetCounter(),
            LEGACY_RUNE_SLOT_MAIN);

        if (!result)
            return 0;

        return result->Fetch()[0].GetUInt32();
    }

    static bool StoreLegacyRuneItemOutsideInventory(Player* player, Item* item)
    {
        if (!player || !item)
            return false;

        uint64 itemGuidLow = item->GetGUID().GetCounter();

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        player->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
        item->SaveToDB(trans);

        CharacterDatabase.CommitTransaction(trans);
        CharacterDatabase.PExecute("DELETE FROM character_inventory WHERE item = {}", itemGuidLow);
        return true;
    }

    static void ConsumeLegacyRuneItem(Player* player, Item* item)
    {
        if (!player || !item)
            return;

        uint64 itemGuidLow = item->GetGUID().GetCounter();
        player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);

        CharacterDatabase.PExecute("DELETE FROM character_inventory WHERE item = {}", itemGuidLow);
        CharacterDatabase.PExecute("DELETE FROM item_instance WHERE guid = {}", itemGuidLow);
    }

    static void ConsumeLegacyRuneCombineReagent(Player* player, Item* item)
    {
        if (!player || !item)
            return;

        uint32 count = 1;
        player->DestroyItemCount(item, count, true);
    }

    static void DeleteLegacyRuneStoredItemByGuid(Player* player, uint64 itemGuidLow)
    {
        if (!itemGuidLow)
            return;

        if (player)
        {
            if (Item* item = player->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(itemGuidLow)))
            {
                player->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
                delete item;
            }
        }

        CharacterDatabase.PExecute("DELETE FROM character_inventory WHERE item = {}", itemGuidLow);
        CharacterDatabase.PExecute("DELETE FROM item_instance WHERE guid = {}", itemGuidLow);
    }

    static bool SaveLegacyRune(Player* player, Item* item)
    {
        if (!player || !item)
            return false;

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            return false;

        CharacterDatabase.PExecute(
            "INSERT INTO legacy_rune_character "
            "(guid, account_id, player_class, player_race, player_level, rune_slot, item_guid, item_entry, item_class, item_subclass) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}) "
            "ON DUPLICATE KEY UPDATE "
            "account_id = VALUES(account_id), "
            "player_class = VALUES(player_class), "
            "player_race = VALUES(player_race), "
            "player_level = VALUES(player_level), "
            "item_guid = VALUES(item_guid), "
            "item_entry = VALUES(item_entry), "
            "item_class = VALUES(item_class), "
            "item_subclass = VALUES(item_subclass), "
            "updated_at = CURRENT_TIMESTAMP",
            player->GetGUID().GetCounter(),
            player->GetSession()->GetAccountId(),
            player->GetClass(),
            player->GetRace(),
            player->GetLevel(),
            LEGACY_RUNE_SLOT_MAIN,
            item->GetGUID().GetCounter(),
            proto->ItemId,
            proto->Class,
            proto->SubClass);

        return true;
    }

    static void TeachLegacyRuneSpell(Player* player, LegacyRuneSpellMap const* runeMap)
    {
        if (!player || !runeMap)
            return;

        if (!player->HasSpell(runeMap->spellId))
            player->LearnSpell(runeMap->spellId, false);

        if (runeMap->skillId)
        {
            uint16 maxSkill = uint16(std::min<uint32>(player->GetLevel() * 5, 400));
            player->SetSkill(runeMap->skillId, 1, maxSkill, maxSkill);
        }
    }

    static void RemoveLegacyRuneSpell(Player* player, LegacyRuneSpellMap const* runeMap)
    {
        if (!player || !runeMap)
            return;

        if (player->HasSpell(runeMap->spellId))
            player->RemoveSpell(runeMap->spellId, false, false);

        if (runeMap->skillId && player->GetSkillValue(runeMap->skillId))
            player->SetSkill(runeMap->skillId, 0, 0, 0);
    }

    static bool DeleteLegacyRuneCharacterState(Player* player)
    {
        if (!player)
            return false;

        CharacterDatabase.PExecute(
            "DELETE FROM legacy_rune_character "
            "WHERE guid = {} AND rune_slot = {}",
            player->GetGUID().GetCounter(),
            LEGACY_RUNE_SLOT_MAIN);

        return true;
    }

    static void SendLegacyRuneAddonMessage(Player* player, std::string const& payload)
    {
        if (!player)
            return;

        player->WhisperAddon(LEGACY_RUNE_ADDON_PREFIX + "\t" + payload, player);
    }

    static std::string EscapeLegacyRuneSqlLike(std::string text)
    {
        if (text.size() > 40)
            text.resize(40);

        std::string escaped;
        escaped.reserve(text.size() * 2);

        for (char ch : text)
        {
            switch (ch)
            {
                case '\\':
                case '\'':
                case '%':
                case '_':
                    escaped.push_back('\\');
                    escaped.push_back(ch);
                    break;
                default:
                    escaped.push_back(ch);
                    break;
            }
        }

        return escaped;
    }

    static std::string CleanLegacyRuneAddonText(std::string text)
    {
        for (char& ch : text)
            if (ch == ':' || ch == '\t' || ch == '\r' || ch == '\n')
                ch = ' ';

        return text;
    }

    static bool HasLegacyRuneArmoryGuildTechDiscount(Player* player)
    {
        if (!player)
            return false;

        uint32 guildId = uint32(player->GetGuildId());
        if (!guildId)
            return false;

        QueryResult result = LoginDatabase.PQuery(
            "SELECT completed "
            "FROM appdata.guild_tech_progress "
            "WHERE guild_id = {} "
            "AND node_id = 'r1b' "
            "AND completed = 1 "
            "LIMIT 1",
            guildId);

        return bool(result);
    }

    static LegacyRuneArmoryPriceInfo GetLegacyRuneArmoryPriceInfo(Player* player, uint32 price, uint32 rpprice)
    {
        LegacyRuneArmoryPriceInfo priceInfo = { price, rpprice, rpprice, 0, false };

        if (rpprice > 0 && HasLegacyRuneArmoryGuildTechDiscount(player))
        {
            priceInfo.guildTechApplied = true;
            priceInfo.regPointDiscount = uint32((uint64(rpprice) * LEGACY_RUNE_ARMORY_GUILD_TECH_DISCOUNT_PERCENT) / 100);
            priceInfo.regPointPrice = rpprice > priceInfo.regPointDiscount ? rpprice - priceInfo.regPointDiscount : 0;
        }

        return priceInfo;
    }

    static bool GetLegacyRuneArmoryBalance(Player* player, uint32& point, uint32& regPoint)
    {
        point = 0;
        regPoint = 0;

        if (!player || !player->GetSession())
            return false;

        QueryResult result = LoginDatabase.PQuery(
            "SELECT point, reg_point "
            "FROM appdata.account_data "
            "WHERE id = {} "
            "LIMIT 1",
            player->GetSession()->GetAccountId());

        if (!result)
            return false;

        Field* fields = result->Fetch();
        point = fields[0].GetUInt32();
        regPoint = fields[1].GetUInt32();
        return true;
    }

    static void SendLegacyRuneArmoryBalance(Player* player)
    {
        uint32 point = 0;
        uint32 regPoint = 0;
        GetLegacyRuneArmoryBalance(player, point, regPoint);

        SendLegacyRuneAddonMessage(
            player,
            "ARMORY:BALANCE:" + std::to_string(point) + ":" + std::to_string(regPoint));
    }

    static void SendLegacyRuneArmoryFail(Player* player, char const* reason)
    {
        SendLegacyRuneAddonMessage(
            player,
            std::string("ARMORY:FAIL:") + (reason ? reason : "무기고 요청 처리에 실패했습니다."));
    }

    static void SendLegacyRuneArmoryBuyFail(Player* player, char const* reason)
    {
        SendLegacyRuneAddonMessage(
            player,
            std::string("ARMORY:BUY:FAIL:") + (reason ? reason : "구매에 실패했습니다."));
    }

    static void SendLegacyRuneArmoryBuyOk(Player* player, char const* message)
    {
        uint32 point = 0;
        uint32 regPoint = 0;
        GetLegacyRuneArmoryBalance(player, point, regPoint);

        SendLegacyRuneAddonMessage(
            player,
            std::string("ARMORY:BUY:OK:") + CleanLegacyRuneAddonText(message ? message : "구매가 완료되었습니다.") + ":" + std::to_string(point) + ":" + std::to_string(regPoint));
    }

    static LegacyRuneGuildTechNode const* FindLegacyRuneGuildTechNode(std::string const& nodeId)
    {
        for (LegacyRuneGuildTechNode const& node : LegacyRuneGuildTechNodes)
            if (nodeId == node.nodeId)
                return &node;

        return nullptr;
    }

    static bool EnsureLegacyRuneGuildTechRows(uint32 guildId)
    {
        if (!guildId)
            return false;

        for (LegacyRuneGuildTechNode const& node : LegacyRuneGuildTechNodes)
        {
            LoginDatabase.DirectPExecute(
                "INSERT IGNORE INTO appdata.guild_tech_progress "
                "(guild_id, node_id, current_amount, target_amount, completed) "
                "VALUES ({}, '{}', 0, {}, 0)",
                guildId,
                node.nodeId,
                node.targetAmount);
        }

        return true;
    }

    static bool GetLegacyRuneGuildTechNodeProgress(uint32 guildId, LegacyRuneGuildTechNode const* node, uint64& currentAmount, uint64& targetAmount, bool& completed)
    {
        currentAmount = 0;
        targetAmount = 0;
        completed = false;

        if (!guildId || !node)
            return false;

        QueryResult result = LoginDatabase.PQuery(
            "SELECT current_amount, target_amount, completed "
            "FROM appdata.guild_tech_progress "
            "WHERE guild_id = {} AND node_id = '{}' "
            "LIMIT 1",
            guildId,
            node->nodeId);

        if (!result)
            return false;

        Field* fields = result->Fetch();
        currentAmount = fields[0].GetUInt64();
        targetAmount = fields[1].GetUInt64();
        completed = fields[2].GetUInt8() == 1;
        return true;
    }

    static bool AreLegacyRuneGuildTechTierNodesCompleted(uint32 guildId, uint8 tier)
    {
        if (!guildId || !tier)
            return false;

        for (LegacyRuneGuildTechNode const& node : LegacyRuneGuildTechNodes)
        {
            if (node.tier != tier)
                continue;

            uint64 currentAmount = 0;
            uint64 targetAmount = 0;
            bool completed = false;
            if (!GetLegacyRuneGuildTechNodeProgress(guildId, &node, currentAmount, targetAmount, completed) || !completed)
                return false;
        }

        return true;
    }

    static bool IsLegacyRuneGuildTechNodeUnlocked(uint32 guildId, LegacyRuneGuildTechNode const* node)
    {
        if (!guildId || !node)
            return false;

        if (node->tier <= 1)
            return true;

        return AreLegacyRuneGuildTechTierNodesCompleted(guildId, node->tier - 1);
    }

    static bool GetLegacyRuneGuildTechTotalAmount(uint32 guildId, uint64& totalAmount)
    {
        totalAmount = 0;

        if (!guildId)
            return false;

        QueryResult result = LoginDatabase.PQuery(
            "SELECT COALESCE(SUM(current_amount), 0) "
            "FROM appdata.guild_tech_progress "
            "WHERE guild_id = {}",
            guildId);

        if (!result)
            return false;

        totalAmount = result->Fetch()[0].GetUInt64();
        return true;
    }

    static void SendLegacyRuneGuildTechFail(Player* player, char const* reason)
    {
        SendLegacyRuneAddonMessage(
            player,
            std::string("GUILDTECH:FAIL:") + CleanLegacyRuneAddonText(reason ? reason : "길드 테크 요청 처리에 실패했습니다."));
    }

    static bool SendLegacyRuneGuildTechState(Player* player)
    {
        if (!player || !player->GetSession())
            return false;

        uint32 guildId = uint32(player->GetGuildId());
        if (!guildId)
        {
            SendLegacyRuneGuildTechFail(player, "길드에 가입한 캐릭터만 길드 테크를 이용할 수 있습니다.");
            return true;
        }

        if (!EnsureLegacyRuneGuildTechRows(guildId))
        {
            SendLegacyRuneGuildTechFail(player, "길드 테크 정보를 준비할 수 없습니다.");
            return true;
        }

        uint32 point = 0;
        uint32 regPoint = 0;
        if (!GetLegacyRuneArmoryBalance(player, point, regPoint))
        {
            SendLegacyRuneGuildTechFail(player, "계정 RP 정보를 확인할 수 없습니다.");
            return true;
        }

        uint64 totalAmount = 0;
        if (!GetLegacyRuneGuildTechTotalAmount(guildId, totalAmount))
        {
            SendLegacyRuneGuildTechFail(player, "길드 테크 정보를 불러올 수 없습니다.");
            return true;
        }

        bool const tier1Completed = AreLegacyRuneGuildTechTierNodesCompleted(guildId, 1);
        bool const tier2Completed = AreLegacyRuneGuildTechTierNodesCompleted(guildId, 2);
        bool const tier3Completed = AreLegacyRuneGuildTechTierNodesCompleted(guildId, 3);

        SendLegacyRuneAddonMessage(player, "GUILDTECH:CLEAR");
        SendLegacyRuneAddonMessage(
            player,
            "GUILDTECH:INFO:" + std::to_string(guildId) + ":" + std::to_string(totalAmount) + ":" + std::to_string(regPoint));

        for (LegacyRuneGuildTechNode const& node : LegacyRuneGuildTechNodes)
        {
            uint64 currentAmount = 0;
            uint64 targetAmount = 0;
            bool completed = false;
            if (!GetLegacyRuneGuildTechNodeProgress(guildId, &node, currentAmount, targetAmount, completed))
            {
                SendLegacyRuneGuildTechFail(player, "길드 테크 진행 정보를 불러올 수 없습니다.");
                return true;
            }

            bool unlocked = node.tier == 1 ||
                (node.tier == 2 && tier1Completed) ||
                (node.tier == 3 && tier2Completed) ||
                (node.tier == 4 && tier3Completed);

            SendLegacyRuneAddonMessage(
                player,
                "GUILDTECH:NODE:" + std::string(node.nodeId) + ":" +
                std::to_string(currentAmount) + ":" +
                std::to_string(targetAmount) + ":" +
                std::to_string(completed ? 1 : 0) + ":" +
                std::to_string(unlocked ? 1 : 0));
        }

        SendLegacyRuneAddonMessage(player, "GUILDTECH:END");
        return true;
    }

    static bool HandleLegacyRuneGuildTechDonate(Player* player, std::string const& nodeId, uint32 amount)
    {
        if (!player || !player->GetSession())
            return false;

        uint32 guildId = uint32(player->GetGuildId());
        if (!guildId)
        {
            SendLegacyRuneGuildTechFail(player, "길드에 가입한 캐릭터만 RP를 기부할 수 있습니다.");
            return true;
        }

        LegacyRuneGuildTechNode const* node = FindLegacyRuneGuildTechNode(nodeId);
        if (!node)
        {
            SendLegacyRuneGuildTechFail(player, "올바르지 않은 길드 테크입니다.");
            return true;
        }

        if (!amount)
        {
            SendLegacyRuneGuildTechFail(player, "기부할 RP를 입력하세요.");
            return true;
        }

        if (!EnsureLegacyRuneGuildTechRows(guildId))
        {
            SendLegacyRuneGuildTechFail(player, "길드 테크 정보를 준비할 수 없습니다.");
            return true;
        }

        if (!IsLegacyRuneGuildTechNodeUnlocked(guildId, node))
        {
            SendLegacyRuneGuildTechFail(player, "선행 단계 길드 테크를 모두 완료해야 합니다.");
            SendLegacyRuneGuildTechState(player);
            return true;
        }

        uint64 currentAmount = 0;
        uint64 targetAmount = 0;
        bool completed = false;
        if (!GetLegacyRuneGuildTechNodeProgress(guildId, node, currentAmount, targetAmount, completed))
        {
            SendLegacyRuneGuildTechFail(player, "길드 테크 진행 정보를 불러올 수 없습니다.");
            return true;
        }

        if (completed || !targetAmount || currentAmount >= targetAmount)
        {
            SendLegacyRuneGuildTechFail(player, "이미 완료된 길드 테크입니다.");
            SendLegacyRuneGuildTechState(player);
            return true;
        }

        uint64 remainingAmount = targetAmount - currentAmount;
        if (uint64(amount) > remainingAmount)
        {
            SendLegacyRuneGuildTechFail(player, "남은 필요 RP보다 많이 기부할 수 없습니다.");
            return true;
        }

        uint32 point = 0;
        uint32 regPoint = 0;
        if (!GetLegacyRuneArmoryBalance(player, point, regPoint))
        {
            SendLegacyRuneGuildTechFail(player, "계정 RP 정보를 확인할 수 없습니다.");
            return true;
        }

        if (regPoint < amount)
        {
            SendLegacyRuneGuildTechFail(player, "RP가 부족합니다.");
            SendLegacyRuneGuildTechState(player);
            return true;
        }

        uint32 accountId = player->GetSession()->GetAccountId();
        uint32 expectedRegPoint = regPoint - amount;
        LoginDatabase.DirectPExecute(
            "UPDATE appdata.account_data "
            "SET reg_point = reg_point - {} "
            "WHERE id = {} AND reg_point >= {}",
            amount,
            accountId,
            amount);

        QueryResult balanceResult = LoginDatabase.PQuery(
            "SELECT reg_point "
            "FROM appdata.account_data "
            "WHERE id = {} "
            "LIMIT 1",
            accountId);

        if (!balanceResult || balanceResult->Fetch()[0].GetUInt32() != expectedRegPoint)
        {
            SendLegacyRuneGuildTechFail(player, "RP 차감에 실패했습니다.");
            SendLegacyRuneGuildTechState(player);
            return true;
        }

        LoginDatabase.DirectPExecute(
            "UPDATE appdata.guild_tech_progress "
            "SET completed = CASE WHEN current_amount + {} >= target_amount THEN 1 ELSE completed END, "
            "current_amount = current_amount + {} "
            "WHERE guild_id = {} "
            "AND node_id = '{}' "
            "AND current_amount = {} "
            "AND completed = 0 "
            "AND current_amount + {} <= target_amount",
            amount,
            amount,
            guildId,
            node->nodeId,
            currentAmount,
            amount);

        uint64 updatedCurrentAmount = 0;
        uint64 updatedTargetAmount = 0;
        bool updatedCompleted = false;
        if (!GetLegacyRuneGuildTechNodeProgress(guildId, node, updatedCurrentAmount, updatedTargetAmount, updatedCompleted) ||
            updatedCurrentAmount != currentAmount + amount)
        {
            LoginDatabase.DirectPExecute(
                "UPDATE appdata.account_data "
                "SET reg_point = reg_point + {} "
                "WHERE id = {}",
                amount,
                accountId);

            SendLegacyRuneGuildTechFail(player, "길드 테크 진행 정보가 변경되어 RP를 반환했습니다. 다시 시도하세요.");
            SendLegacyRuneGuildTechState(player);
            return true;
        }

        SendLegacyRuneAddonMessage(
            player,
            "GUILDTECH:DONATE:OK:" + CleanLegacyRuneAddonText(std::string(node->name) + "에 " + std::to_string(amount) + " RP를 기부했습니다."));
        SendLegacyRuneGuildTechState(player);
        return true;
    }

    static bool HandleLegacyRuneArmoryOpen(Player* player)
    {
        if (!player)
            return false;

        SendLegacyRuneArmoryBalance(player);
        return true;
    }

    static bool CanPlayerUseArmorItem(Player* player, uint32 itemId)
	{
		if (!player || !itemId)
			return false;

		ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
		if (!proto)
			return false;

		// 방어구가 아니면 여기서는 막지 않는다.
		if (proto->Class != ITEM_CLASS_ARMOR)
			return true;

		uint8 playerClass = player->GetClass();

		switch (proto->SubClass)
		{
			case ITEM_SUBCLASS_ARMOR_MISCELLANEOUS:
			case ITEM_SUBCLASS_ARMOR_CLOTH:
				return true;

			case ITEM_SUBCLASS_ARMOR_LEATHER:
			{
				switch (playerClass)
				{
					case CLASS_ROGUE:
					case CLASS_DRUID:
					case CLASS_HUNTER:
					case CLASS_SHAMAN:
					case CLASS_WARRIOR:
					case CLASS_PALADIN:
					case CLASS_DEATH_KNIGHT:
						return true;
					default:
						return false;
				}
			}

			case ITEM_SUBCLASS_ARMOR_MAIL:
			{
				switch (playerClass)
				{
					case CLASS_HUNTER:
					case CLASS_SHAMAN:
					case CLASS_WARRIOR:
					case CLASS_PALADIN:
					case CLASS_DEATH_KNIGHT:
						return true;
					default:
						return false;
				}
			}

			case ITEM_SUBCLASS_ARMOR_PLATE:
			{
				switch (playerClass)
				{
					case CLASS_WARRIOR:
					case CLASS_PALADIN:
					case CLASS_DEATH_KNIGHT:
						return true;
					default:
						return false;
				}
			}

			case ITEM_SUBCLASS_ARMOR_SHIELD:
			{
				switch (playerClass)
				{
					case CLASS_WARRIOR:
					case CLASS_PALADIN:
					case CLASS_SHAMAN:
						return true;
					default:
						return false;
				}
			}

			case ITEM_SUBCLASS_ARMOR_LIBRAM:
				return playerClass == CLASS_PALADIN;

			case ITEM_SUBCLASS_ARMOR_IDOL:
				return playerClass == CLASS_DRUID;

			case ITEM_SUBCLASS_ARMOR_TOTEM:
				return playerClass == CLASS_SHAMAN;

			case ITEM_SUBCLASS_ARMOR_SIGIL:
				return playerClass == CLASS_DEATH_KNIGHT;

			default:
				return true;
		}
	}

	static bool CanPlayerUseWeaponItem(Player* player, uint32 itemId)
{
    if (!player || !itemId)
        return false;

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return false;

    // 무기 class가 아니면 여기서는 검사하지 않는다.
    if (proto->Class != ITEM_CLASS_WEAPON)
        return true;

    uint8 playerClass = player->GetClass();

    switch (proto->SubClass)
    {
        case ITEM_SUBCLASS_WEAPON_AXE: // 한손 도끼
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR:
                case CLASS_PALADIN:
                case CLASS_HUNTER:
                case CLASS_ROGUE:
                case CLASS_SHAMAN:
                case CLASS_DEATH_KNIGHT:
                    return true;
                default:
                    return false;
            }
        }

        case ITEM_SUBCLASS_WEAPON_AXE2: // 양손 도끼
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR:
                case CLASS_PALADIN:
                case CLASS_HUNTER:
                case CLASS_SHAMAN:
                case CLASS_DEATH_KNIGHT:
                    return true;
                default:
                    return false;
            }
        }

        case ITEM_SUBCLASS_WEAPON_BOW: // 활
        case ITEM_SUBCLASS_WEAPON_GUN: // 총
        case ITEM_SUBCLASS_WEAPON_CROSSBOW: // 석궁
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR:
                case CLASS_HUNTER:
                case CLASS_ROGUE:
                    return true;
                default:
                    return false;
            }
        }

        case ITEM_SUBCLASS_WEAPON_MACE: // 한손 둔기
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR:
                case CLASS_PALADIN:
                case CLASS_PRIEST:
                case CLASS_ROGUE:
                case CLASS_SHAMAN:
                case CLASS_DRUID:
                case CLASS_DEATH_KNIGHT:
                    return true;
                default:
                    return false;
            }
        }

        case ITEM_SUBCLASS_WEAPON_MACE2: // 양손 둔기
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR:
                case CLASS_PALADIN:
                case CLASS_SHAMAN:
                case CLASS_DRUID:
                case CLASS_DEATH_KNIGHT:
                    return true;
                default:
                    return false;
            }
        }

        case ITEM_SUBCLASS_WEAPON_POLEARM: // 장창
        case ITEM_SUBCLASS_WEAPON_SPEAR: // 창
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR:
                case CLASS_PALADIN:
                case CLASS_HUNTER:
                case CLASS_DRUID:
                case CLASS_DEATH_KNIGHT:
                    return true;
                default:
                    return false;
            }
        }

        case ITEM_SUBCLASS_WEAPON_SWORD: // 한손 검
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR:
                case CLASS_PALADIN:
                case CLASS_HUNTER:
                case CLASS_ROGUE:
                case CLASS_MAGE:
                case CLASS_WARLOCK:
                case CLASS_DEATH_KNIGHT:
                    return true;
                default:
                    return false;
            }
        }

        case ITEM_SUBCLASS_WEAPON_SWORD2: // 양손 검
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR:
                case CLASS_PALADIN:
                case CLASS_HUNTER:
                case CLASS_DEATH_KNIGHT:
                    return true;
                default:
                    return false;
            }
        }

        case ITEM_SUBCLASS_WEAPON_STAFF: // 지팡이
        {
            switch (playerClass)
            {
                case CLASS_PRIEST:
                case CLASS_SHAMAN:
                case CLASS_MAGE:
                case CLASS_WARLOCK:
                case CLASS_DRUID:
                case CLASS_HUNTER:
                    return true;
                default:
                    return false;
            }
        }

        case ITEM_SUBCLASS_WEAPON_FIST_WEAPON: // 장착 무기
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR:
                case CLASS_HUNTER:
                case CLASS_ROGUE:
                case CLASS_SHAMAN:
                case CLASS_DRUID:
                    return true;
                default:
                    return false;
            }
        }

        case ITEM_SUBCLASS_WEAPON_DAGGER: // 단검
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR:
                case CLASS_HUNTER:
                case CLASS_ROGUE:
                case CLASS_PRIEST:
                case CLASS_SHAMAN:
                case CLASS_MAGE:
                case CLASS_WARLOCK:
                case CLASS_DRUID:
                    return true;
                default:
                    return false;
            }
        }

        case ITEM_SUBCLASS_WEAPON_THROWN: // 투척 무기
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR:
                case CLASS_ROGUE:
                case CLASS_HUNTER:
                    return true;
                default:
                    return false;
            }
        }

        case ITEM_SUBCLASS_WEAPON_WAND: // 마법봉
        {
            switch (playerClass)
            {
                case CLASS_PRIEST:
                case CLASS_MAGE:
                case CLASS_WARLOCK:
                    return true;
                default:
                    return false;
            }
        }

        case ITEM_SUBCLASS_WEAPON_FISHING_POLE: // 낚싯대
            return true;

        case ITEM_SUBCLASS_WEAPON_MISCELLANEOUS: // 기타 무기류
            return true;

        default:
            return true;
    }
}

    static bool CanShowLegacyRuneArmoryItem(Player* player, uint32 itemEntry)
	{
		if (!player)
			return false;

		ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
		if (!proto)
			return false;

		if (!CanPlayerUseArmorItem(player, itemEntry))
			return false;

		if (!CanPlayerUseWeaponItem(player, itemEntry))
			return false;

		return true;
	}

/*	static bool CanShowLegacyRuneArmoryItem(Player* player, uint32 itemEntry)
	{
		if (!player)
			return false;

		ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
		if (!proto)
			return false;

		return true;
	}
*/
    static bool HandleLegacyRuneArmorySearch(Player* player, std::string const& keyword)
    {
        if (!player)
            return false;

        std::string searchText = keyword;
        searchText.erase(0, searchText.find_first_not_of(" \t\r\n"));
        size_t last = searchText.find_last_not_of(" \t\r\n");
        if (last == std::string::npos)
            searchText.clear();
        else
            searchText.erase(last + 1);

        if (searchText.empty())
        {
            SendLegacyRuneArmoryFail(player, "검색할 아이템 이름을 입력하세요.");
            return true;
        }

        SendLegacyRuneArmoryBalance(player);
        SendLegacyRuneAddonMessage(player, "ARMORY:CLEAR");

        bool const serverSupportSearch = searchText == "서버지원";
        QueryResult result;

        if (serverSupportSearch)
        {
            result = WorldDatabase.PQuery(
                "SELECT s.id, s.game_item_id, s.price, s.rpprice, i.name "
                "FROM appdata.shop_items s "
                "INNER JOIN item_template i ON i.entry = s.game_item_id "
                "WHERE s.active = 1 "
                "AND s.category_id = 3 "
                "ORDER BY s.sort_order ASC, s.id ASC "
                "LIMIT {}",
                LEGACY_RUNE_ARMORY_SEARCH_LIMIT * 5);
        }
        else
        {
            std::string escapedKeyword = EscapeLegacyRuneSqlLike(searchText);
            result = WorldDatabase.PQuery(
                "SELECT s.id, s.game_item_id, s.price, s.rpprice, i.name "
                "FROM appdata.shop_items s "
                "INNER JOIN item_template i ON i.entry = s.game_item_id "
                "WHERE s.active = 1 "
                "AND s.category_id <> 3 "
                "AND i.name LIKE '%{}%' ESCAPE '\\\\' "
                "ORDER BY s.sort_order ASC, s.id ASC "
                "LIMIT {}",
                escapedKeyword,
                LEGACY_RUNE_ARMORY_SEARCH_LIMIT * 5);
        }

        uint32 count = 0;
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 shopId = fields[0].GetUInt32();
                uint32 itemEntry = fields[1].GetUInt32();
                uint32 price = fields[2].GetUInt32();
                uint32 rpprice = fields[3].GetUInt32();
                std::string itemName = CleanLegacyRuneAddonText(fields[4].GetString());

                if (!CanShowLegacyRuneArmoryItem(player, itemEntry))
                    continue;

                LegacyRuneArmoryPriceInfo priceInfo = GetLegacyRuneArmoryPriceInfo(player, price, rpprice);

                SendLegacyRuneAddonMessage(
                    player,
                    "ARMORY:ITEM:" + std::to_string(shopId) + ":" +
                    std::to_string(itemEntry) + ":" +
                    std::to_string(priceInfo.pointPrice) + ":" +
                    std::to_string(priceInfo.regPointPrice) + ":" +
                    std::to_string(priceInfo.originalRegPointPrice) + ":" +
                    std::to_string(priceInfo.regPointDiscount) + ":" +
                    std::to_string(priceInfo.guildTechApplied ? 1 : 0) + ":" +
                    itemName);
                ++count;
            } while (count < LEGACY_RUNE_ARMORY_SEARCH_LIMIT && result->NextRow());
        }

        SendLegacyRuneAddonMessage(player, "ARMORY:SEARCH:END:" + std::to_string(count));
        return true;
    }

    static bool HandleLegacyRuneArmoryBuy(Player* player, uint32 shopId)
    {
        if (!player || !player->GetSession())
            return false;

        if (!shopId)
        {
            SendLegacyRuneArmoryBuyFail(player, "구매할 아이템 정보가 없습니다.");
            return true;
        }

        QueryResult shopResult = WorldDatabase.PQuery(
            "SELECT s.game_item_id, s.price, s.rpprice, i.name "
            "FROM appdata.shop_items s "
            "INNER JOIN item_template i ON i.entry = s.game_item_id "
            "WHERE s.id = {} AND s.active = 1 "
            "LIMIT 1",
            shopId);

        if (!shopResult)
        {
            SendLegacyRuneArmoryBuyFail(player, "판매 중인 아이템이 아닙니다.");
            return true;
        }

        Field* shopFields = shopResult->Fetch();
        uint32 itemEntry = shopFields[0].GetUInt32();
        uint32 price = shopFields[1].GetUInt32();
        uint32 rpprice = shopFields[2].GetUInt32();
        std::string itemName = shopFields[3].GetString();

        LegacyRuneArmoryPriceInfo priceInfo = GetLegacyRuneArmoryPriceInfo(player, price, rpprice);
        price = priceInfo.pointPrice;
        rpprice = priceInfo.regPointPrice;

        if (!sObjectMgr->GetItemTemplate(itemEntry))
        {
            SendLegacyRuneArmoryBuyFail(player, "아이템 정보를 찾을 수 없습니다.");
            return true;
        }

        if (!CanShowLegacyRuneArmoryItem(player, itemEntry))
        {
            SendLegacyRuneArmoryBuyFail(player, "현재 캐릭터가 사용할 수 없는 장비입니다.");
            return true;
        }

        ItemPosCountVec dest;
        InventoryResult inventoryResult = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemEntry, 1);
        if (inventoryResult != EQUIP_ERR_OK)
        {
            SendLegacyRuneArmoryBuyFail(player, "가방 공간이 부족합니다.");
            return true;
        }

        uint32 point = 0;
        uint32 regPoint = 0;
        if (!GetLegacyRuneArmoryBalance(player, point, regPoint))
        {
            SendLegacyRuneArmoryBuyFail(player, "계정 포인트 정보를 확인할 수 없습니다.");
            return true;
        }

        if (point < price)
        {
            SendLegacyRuneArmoryBuyFail(player, "Point가 부족합니다.");
            SendLegacyRuneArmoryBalance(player);
            return true;
        }

        if (regPoint < rpprice)
        {
            SendLegacyRuneArmoryBuyFail(player, "RPoint가 부족합니다.");
            SendLegacyRuneArmoryBalance(player);
            return true;
        }

        uint32 accountId = player->GetSession()->GetAccountId();
        uint32 expectedPoint = point - price;
        uint32 expectedRegPoint = regPoint - rpprice;

        LoginDatabase.DirectPExecute(
            "UPDATE appdata.account_data "
            "SET point = point - {}, reg_point = reg_point - {} "
            "WHERE id = {} "
            "AND point >= {} "
            "AND reg_point >= {}",
            price,
            rpprice,
            accountId,
            price,
            rpprice);

        QueryResult checkResult = LoginDatabase.PQuery(
            "SELECT point, reg_point "
            "FROM appdata.account_data "
            "WHERE id = {} "
            "LIMIT 1",
            accountId);

        if (!checkResult)
        {
            SendLegacyRuneArmoryBuyFail(player, "포인트 차감 확인에 실패했습니다.");
            SendLegacyRuneArmoryBalance(player);
            return true;
        }

        Field* checkFields = checkResult->Fetch();
        uint32 afterPoint = checkFields[0].GetUInt32();
        uint32 afterRegPoint = checkFields[1].GetUInt32();

        if (afterPoint != expectedPoint || afterRegPoint != expectedRegPoint)
        {
            SendLegacyRuneArmoryBuyFail(player, "포인트 차감에 실패했습니다.");
            SendLegacyRuneArmoryBalance(player);
            return true;
        }

        Item* item = player->StoreNewItem(dest, itemEntry, true);
        if (!item)
        {
            LoginDatabase.DirectPExecute(
                "UPDATE appdata.account_data "
                "SET point = point + {}, reg_point = reg_point + {} "
                "WHERE id = {}",
                price,
                rpprice,
                accountId);

            SendLegacyRuneArmoryBuyFail(player, "아이템 지급에 실패했습니다.");
            SendLegacyRuneArmoryBalance(player);
            return true;
        }

        player->SendNewItem(item, 1, true, false);
        player->GetSession()->SendNotification("무기고: %s 구매가 완료되었습니다.", itemName.c_str());

        std::string buyOkMessage = "구매가 완료되었습니다.";
        if (priceInfo.guildTechApplied && priceInfo.originalRegPointPrice > priceInfo.regPointPrice)
            buyOkMessage += " 길드 테크니크 상인의 교섭이 적용되어 RP 15% 할인이 적용되었습니다.";

        SendLegacyRuneArmoryBuyOk(player, buyOkMessage.c_str());
        SendLegacyRuneArmoryBalance(player);
        return true;
    }

    static std::string GetLegacyRuneItemName(uint32 itemEntry)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
        if (proto && !proto->Name1.empty())
            return proto->Name1;

        return "전승 룬 #" + std::to_string(itemEntry);
    }

    static void SendLegacyRuneItemState(Player* player, uint32 itemEntry, uint32 itemClass, uint32 itemSubClass)
    {
        SendLegacyRuneAddonMessage(
            player,
            "STATE:" + std::to_string(itemEntry) + ":" + std::to_string(itemClass) + ":" + std::to_string(itemSubClass) + ":" + GetLegacyRuneItemName(itemEntry));
    }

    static void SendLegacyRuneCheckFail(Player* player, uint32 itemEntry, char const* reason)
    {
        SendLegacyRuneAddonMessage(
            player,
            "CHECK:FAIL:" + std::to_string(itemEntry) + ":" + (reason ? reason : "전승 룬을 착용할 수 없습니다."));
    }

    static void SendLegacyRuneCombineFail(Player* player, char const* reason)
    {
        SendLegacyRuneAddonMessage(
            player,
            std::string("COMBINE:FAIL:") + (reason ? reason : "전승룬 조합에 실패했습니다."));
    }

    static void SendLegacyRuneCombineOk(Player* player, uint32 resultItemEntry)
    {
        SendLegacyRuneAddonMessage(
            player,
            "COMBINE:OK:" + std::to_string(resultItemEntry) + ":" + GetLegacyRuneItemName(resultItemEntry));
    }

    static void SendLegacyRuneCombinePreviewFail(Player* player, char const* reason)
    {
        SendLegacyRuneAddonMessage(
            player,
            std::string("COMBINE:PREVIEW:FAIL:") + (reason ? reason : "전승룬 조합 결과를 확인할 수 없습니다."));
    }

    static void SendLegacyRuneCombinePreviewOk(Player* player, uint32 resultItemEntry)
    {
        SendLegacyRuneAddonMessage(
            player,
            "COMBINE:PREVIEW:OK:" + std::to_string(resultItemEntry) + ":" + GetLegacyRuneItemName(resultItemEntry));
    }

    static bool HandleLegacyRuneCheck(Player* player, LegacyRuneItemRef const& itemRef)
    {
        if (!player)
            return false;

        if (!itemRef.itemEntry)
        {
            SendLegacyRuneCheckFail(player, itemRef.itemEntry, "확인할 아이템 정보가 없습니다.");
            return true;
        }

        Item* item = FindLegacyRuneItem(player, itemRef);
        if (!item)
        {
            SendLegacyRuneCheckFail(player, itemRef.itemEntry, "해당 아이템이 현재 캐릭터 가방의 해당 위치에 없습니다.");
            return true;
        }

        ItemTemplate const* proto = item->GetTemplate();
        if (char const* reason = GetLegacyRuneApplyBlockReason(player, proto))
        {
            SendLegacyRuneCheckFail(player, itemRef.itemEntry, reason);
            return true;
        }

        SendLegacyRuneAddonMessage(player, "CHECK:OK:" + std::to_string(itemRef.itemEntry) + ":" + std::to_string(item->GetGUID().GetCounter()));
        return true;
    }

    static void SendLegacyRuneState(Player* player)
    {
        if (!player)
            return;

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT item_entry, item_class, item_subclass "
            "FROM legacy_rune_character "
            "WHERE guid = {} AND rune_slot = {} "
            "LIMIT 1",
            player->GetGUID().GetCounter(),
            LEGACY_RUNE_SLOT_MAIN);

        if (!result)
        {
            SendLegacyRuneAddonMessage(player, "STATE:NONE");
            return;
        }

        Field* fields = result->Fetch();
        uint32 itemEntry = fields[0].GetUInt32();
        uint32 itemClass = fields[1].GetUInt32();
        uint32 itemSubClass = fields[2].GetUInt32();

        SendLegacyRuneItemState(player, itemEntry, itemClass, itemSubClass);
    }

    static bool HandleLegacyRuneApply(Player* player, LegacyRuneItemRef const& itemRef)
    {
        if (!player)
            return false;

        if (!itemRef.itemEntry)
        {
            player->GetSession()->SendNotification("전승 룬: 저장할 아이템 정보가 없습니다.");
            return true;
        }

        Item* item = FindLegacyRuneItem(player, itemRef);
        if (!item)
        {
            player->GetSession()->SendNotification("전승 룬: 해당 아이템이 현재 캐릭터 가방의 해당 위치에 없습니다.");
            return true;
        }

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
        {
            player->GetSession()->SendNotification("전승 룬: 아이템 정보를 확인할 수 없습니다.");
            return true;
        }

        if (LegacyRuneSpellMap const* removeMap = FindLegacyRuneRemoveMap(proto->ItemId))
        {
            uint64 previousItemGuid = GetSavedLegacyRuneItemGuid(player);
            uint32 previousItemEntry = GetSavedLegacyRuneItemEntry(player);

            if (!previousItemGuid || previousItemEntry != removeMap->applyItemEntry)
            {
                player->GetSession()->SendNotification("전승 룬: 현재 캐릭터에 해제할 전승 룬이 없습니다.");
                return true;
            }

            DeleteLegacyRuneStoredItemByGuid(player, previousItemGuid);
            DeleteLegacyRuneCharacterState(player);
            RemoveLegacyRuneSpell(player, removeMap);
            ConsumeLegacyRuneItem(player, item);

            player->GetSession()->SendNotification("전승 룬: 저장된 자물쇠 따기 전승 룬을 해제했습니다.");
            SendLegacyRuneAddonMessage(player, "STATE:NONE");
            return true;
        }

        LegacyRuneSpellMap const* currentRuneMap = FindLegacyRuneApplyMap(proto->ItemId);
        if (char const* reason = GetLegacyRuneApplyBlockReason(player, proto))
        {
            player->GetSession()->SendNotification("전승 룬: %s", reason);
            return true;
        }

        uint32 savedItemEntry = proto->ItemId;
        uint32 savedItemClass = proto->Class;
        uint32 savedItemSubClass = proto->SubClass;
        uint64 currentItemGuid = item->GetGUID().GetCounter();
        uint64 previousItemGuid = GetSavedLegacyRuneItemGuid(player);
        uint32 previousItemEntry = GetSavedLegacyRuneItemEntry(player);

        if (!StoreLegacyRuneItemOutsideInventory(player, item))
        {
            player->GetSession()->SendNotification("전승 룬: 아이템을 인벤토리에서 분리하지 못했습니다.");
            return true;
        }

        if (!SaveLegacyRune(player, item))
        {
            player->GetSession()->SendNotification("전승 룬: 저장 중 오류가 발생했습니다.");
            return true;
        }

        if (previousItemGuid && previousItemGuid != currentItemGuid)
            DeleteLegacyRuneStoredItemByGuid(player, previousItemGuid);

        if (previousItemEntry && previousItemEntry != savedItemEntry)
            RemoveLegacyRuneSpell(player, FindLegacyRuneApplyMap(previousItemEntry));

        TeachLegacyRuneSpell(player, currentRuneMap);

        SendLegacyRuneItemState(player, savedItemEntry, savedItemClass, savedItemSubClass);
        return true;
    }

    static bool HandleLegacyRuneCombine(Player* player, LegacyRuneItemRef const (&itemRefs)[3])
    {
        if (!player)
            return false;

        Item* items[3] = { nullptr, nullptr, nullptr };
        uint32 itemEntries[3] = { 0, 0, 0 };
        uint64 itemGuids[3] = { 0, 0, 0 };
        uint16 itemPositions[3] = { 0, 0, 0 };

        for (uint8 i = 0; i < 3; ++i)
        {
            if (!IsLegacyRuneCombineReagent(itemRefs[i].itemEntry))
            {
                SendLegacyRuneCombineFail(player, "처형의 얼개 재료만 조합할 수 있습니다.");
                return true;
            }

            items[i] = FindLegacyRuneItem(player, itemRefs[i]);
            if (!items[i])
            {
                SendLegacyRuneCombineFail(player, "조합 재료가 현재 가방의 해당 위치에 없습니다.");
                return true;
            }

            itemEntries[i] = items[i]->GetEntry();
            itemGuids[i] = items[i]->GetGUID().GetCounter();
            itemPositions[i] = uint16(items[i]->GetBagSlot()) << 8 | uint16(items[i]->GetSlot());
        }

        for (uint8 i = 0; i < 3; ++i)
        {
            for (uint8 j = uint8(i + 1); j < 3; ++j)
            {
                if (itemGuids[i] == itemGuids[j] || itemPositions[i] == itemPositions[j])
                {
                    SendLegacyRuneCombineFail(player, "같은 재료 아이템은 중복으로 사용할 수 없습니다.");
                    return true;
                }
            }
        }

        uint32 resultItemEntry = FindLegacyRuneCombineResult(itemEntries[0], itemEntries[1], itemEntries[2]);
        if (!resultItemEntry)
        {
            SendLegacyRuneCombineFail(player, "등록되지 않은 전승룬 조합식입니다.");
            return true;
        }

        if (!sObjectMgr->GetItemTemplate(resultItemEntry))
        {
            SendLegacyRuneCombineFail(player, "조합 결과 아이템 정보를 찾을 수 없습니다.");
            return true;
        }

        if (!player->AddItem(resultItemEntry, 1))
        {
            SendLegacyRuneCombineFail(player, "가방 공간이 부족합니다.");
            return true;
        }

        for (Item* item : items)
            ConsumeLegacyRuneCombineReagent(player, item);

        player->GetSession()->SendNotification("전승 룬: %s 조합에 성공했습니다.", GetLegacyRuneItemName(resultItemEntry).c_str());
        SendLegacyRuneCombineOk(player, resultItemEntry);
        return true;
    }

    static bool HandleLegacyRuneCombinePreview(Player* player, uint32 const (&itemEntries)[3])
    {
        if (!player)
            return false;

        for (uint8 i = 0; i < 3; ++i)
        {
            if (!IsLegacyRuneCombineReagent(itemEntries[i]))
            {
                SendLegacyRuneCombinePreviewFail(player, "처형의 얼개 재료만 조합할 수 있습니다.");
                return true;
            }
        }

        for (uint8 i = 0; i < 3; ++i)
        {
            for (uint8 j = uint8(i + 1); j < 3; ++j)
            {
                if (itemEntries[i] == itemEntries[j])
                {
                    SendLegacyRuneCombinePreviewFail(player, "서로 다른 처형의 얼개 3종이 필요합니다.");
                    return true;
                }
            }
        }

        uint32 resultItemEntry = FindLegacyRuneCombineResult(itemEntries[0], itemEntries[1], itemEntries[2]);
        if (!resultItemEntry)
        {
            SendLegacyRuneCombinePreviewFail(player, "등록되지 않은 전승룬 조합식입니다.");
            return true;
        }

        if (!sObjectMgr->GetItemTemplate(resultItemEntry))
        {
            SendLegacyRuneCombinePreviewFail(player, "조합 결과 아이템 정보를 찾을 수 없습니다.");
            return true;
        }

        SendLegacyRuneCombinePreviewOk(player, resultItemEntry);
        return true;
    }

    /*
     * Custom Update - 2026-07-19
     * 마법 재단 광활한 가방 제작 처리
     */
    static void MarkLegacyRuneMagicAltarAccess(Player* player)
    {
        if (!player)
            return;

        LegacyRuneMagicAltarAccess[player->GetGUID().GetCounter()] =
            std::chrono::steady_clock::now() + std::chrono::seconds(LEGACY_RUNE_MAGIC_ALTAR_ACCESS_SECONDS);
    }

    static bool HasLegacyRuneMagicAltarAccess(Player* player)
    {
        if (!player)
            return false;

        uint32 playerGuid = player->GetGUID().GetCounter();
        auto itr = LegacyRuneMagicAltarAccess.find(playerGuid);
        if (itr == LegacyRuneMagicAltarAccess.end())
            return false;

        if (std::chrono::steady_clock::now() > itr->second)
        {
            LegacyRuneMagicAltarAccess.erase(itr);
            return false;
        }

        return true;
    }

    static void SendLegacyRuneMagicAltarCombineFail(Player* player, char const* reason)
    {
        SendLegacyRuneAddonMessage(
            player,
            std::string("MAGICALTAR:COMBINE:FAIL:") +
                (reason ? reason : "광활한 가방 제작에 실패했습니다."));
    }

    static void SendLegacyRuneMagicAltarCombineOk(Player* player)
    {
        SendLegacyRuneAddonMessage(
            player,
            "MAGICALTAR:COMBINE:OK:" + std::to_string(LEGACY_RUNE_MAGIC_ALTAR_RESULT_ITEM_ID));
    }

    static bool HandleLegacyRuneMagicAltarCombine(Player* player)
    {
        if (!player)
            return false;

        if (!HasLegacyRuneMagicAltarAccess(player))
        {
            SendLegacyRuneMagicAltarCombineFail(player, "마법 재단을 다시 클릭한 후 제작하세요.");
            return true;
        }

        if (!sObjectMgr->GetItemTemplate(LEGACY_RUNE_MAGIC_ALTAR_RESULT_ITEM_ID))
        {
            SendLegacyRuneMagicAltarCombineFail(player, "광활한 가방 아이템 정보를 찾을 수 없습니다.");
            return true;
        }

        if (!player->HasItemCount(LEGACY_RUNE_MAGIC_ALTAR_BAG_ITEM_ID, LEGACY_RUNE_MAGIC_ALTAR_BAG_REQUIRED_COUNT, false))
        {
            SendLegacyRuneMagicAltarCombineFail(player, "36칸 가방 1개가 필요합니다.");
            return true;
        }

        if (!player->HasItemCount(LEGACY_RUNE_MAGIC_ALTAR_CRYSTAL_ITEM_ID, LEGACY_RUNE_MAGIC_ALTAR_CRYSTAL_REQUIRED_COUNT, false))
        {
            SendLegacyRuneMagicAltarCombineFail(player, "침공의 수정 2개가 필요합니다.");
            return true;
        }

        if (!player->HasItemCount(LEGACY_RUNE_MAGIC_ALTAR_FROZEN_ORB_ITEM_ID, LEGACY_RUNE_MAGIC_ALTAR_FROZEN_ORB_REQUIRED_COUNT, false))
        {
            SendLegacyRuneMagicAltarCombineFail(player, "얼어붙은 보주 20개가 필요합니다.");
            return true;
        }

        if (!player->HasItemCount(LEGACY_RUNE_MAGIC_ALTAR_ABYSS_CRYSTAL_ITEM_ID, LEGACY_RUNE_MAGIC_ALTAR_ABYSS_CRYSTAL_REQUIRED_COUNT, false))
        {
            SendLegacyRuneMagicAltarCombineFail(player, "심연의 수정 15개가 필요합니다.");
            return true;
        }

        ItemPosCountVec dest;
        InventoryResult inventoryResult = player->CanStoreNewItem(
            NULL_BAG,
            NULL_SLOT,
            dest,
            LEGACY_RUNE_MAGIC_ALTAR_RESULT_ITEM_ID,
            1);

        if (inventoryResult != EQUIP_ERR_OK)
        {
            SendLegacyRuneMagicAltarCombineFail(player, "광활한 가방을 받을 가방 공간이 부족합니다.");
            return true;
        }

        Item* resultItem = player->StoreNewItem(
            dest,
            LEGACY_RUNE_MAGIC_ALTAR_RESULT_ITEM_ID,
            true);

        if (!resultItem)
        {
            SendLegacyRuneMagicAltarCombineFail(player, "광활한 가방 지급에 실패했습니다.");
            return true;
        }

        player->DestroyItemCount(
            LEGACY_RUNE_MAGIC_ALTAR_BAG_ITEM_ID,
            LEGACY_RUNE_MAGIC_ALTAR_BAG_REQUIRED_COUNT,
            true);
        player->DestroyItemCount(
            LEGACY_RUNE_MAGIC_ALTAR_CRYSTAL_ITEM_ID,
            LEGACY_RUNE_MAGIC_ALTAR_CRYSTAL_REQUIRED_COUNT,
            true);
        player->DestroyItemCount(
            LEGACY_RUNE_MAGIC_ALTAR_FROZEN_ORB_ITEM_ID,
            LEGACY_RUNE_MAGIC_ALTAR_FROZEN_ORB_REQUIRED_COUNT,
            true);
        player->DestroyItemCount(
            LEGACY_RUNE_MAGIC_ALTAR_ABYSS_CRYSTAL_ITEM_ID,
            LEGACY_RUNE_MAGIC_ALTAR_ABYSS_CRYSTAL_REQUIRED_COUNT,
            true);

        player->SendNewItem(resultItem, 1, true, false);
        player->GetSession()->SendNotification("마법 재단: 광활한 가방 제작이 완료되었습니다.");

        MarkLegacyRuneMagicAltarAccess(player);
        SendLegacyRuneMagicAltarCombineOk(player);
        return true;
    }

    static bool GetLegacyRuneAddonPayload(std::string const& msg, std::string& payload)
    {
        payload = msg;

        if (payload.compare(0, LEGACY_RUNE_ADDON_PREFIX.size(), LEGACY_RUNE_ADDON_PREFIX) == 0)
        {
            if (payload.size() <= LEGACY_RUNE_ADDON_PREFIX.size())
                return false;

            char separator = payload[LEGACY_RUNE_ADDON_PREFIX.size()];
            if (separator != '\t' && separator != ' ' && separator != ':')
                return false;

            payload = payload.substr(LEGACY_RUNE_ADDON_PREFIX.size() + 1);
        }

        return true;
    }

    static bool ParseLegacyRuneUInt32(std::string const& text, uint32& value)
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

    static bool ParseLegacyRuneUInt64(std::string const& text, uint64& value)
    {
        value = 0;

        if (text.empty())
            return false;

        char* end = nullptr;
        unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
        if (!end || *end != '\0' || parsed > std::numeric_limits<uint64>::max())
            return false;

        value = uint64(parsed);
        return true;
    }

    static void QueueLegacyRuneAddonVersionCheck(WorldSession* session)
    {
        if (!session || !session->GetAccountId())
            return;

        std::chrono::steady_clock::time_point const now = std::chrono::steady_clock::now();
        LegacyRuneAddonLoginChecks[session->GetAccountId()] =
        {
            now + std::chrono::seconds(LEGACY_RUNE_ADDON_LOGIN_CHECK_DELAY_SECONDS),
            false
        };

        for (auto itr = LegacyRuneAddonLoginChecks.begin(); itr != LegacyRuneAddonLoginChecks.end();)
        {
            if (itr->second.timeoutAt + std::chrono::minutes(1) < now)
                itr = LegacyRuneAddonLoginChecks.erase(itr);
            else
                ++itr;
        }
    }

    static void MarkLegacyRuneAddonDetected(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        auto itr = LegacyRuneAddonLoginChecks.find(player->GetSession()->GetAccountId());
        if (itr != LegacyRuneAddonLoginChecks.end())
            itr->second.addonDetected = true;
    }

    static void HandleLegacyRuneAddonVersion(Player* player, uint32 addonVersion)
    {
        if (!player || !player->GetSession())
            return;

        WorldSession* session = player->GetSession();
        LegacyRuneAddonLoginChecks.erase(session->GetAccountId());

        if (addonVersion < LEGACY_RUNE_ADDON_REQUIRED_VERSION)
		{
			std::string const message = Trinity::StringFormat(
                "|cffff2020[전승룬에드온]|r 전승룬 애드온 업데이트가 있습니다. 홈페이지에서 최신 버전({})을 다운로드하세요. |r", LEGACY_RUNE_ADDON_REQUIRED_VERSION_TEXT);

			ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
		}
    }

    static void TrySendLegacyRuneAddonInstallOrUpdateNotice(WorldSession* session)
    {
        if (!session)
            return;

        auto itr = LegacyRuneAddonLoginChecks.find(session->GetAccountId());
        if (itr == LegacyRuneAddonLoginChecks.end() || std::chrono::steady_clock::now() < itr->second.timeoutAt)
            return;

        Player* player = session->GetPlayer();
        if (!player)
            return;

        if (itr->second.addonDetected)
		{
			std::string const message = Trinity::StringFormat(
                "|cffff2020[전승룬에드온]|r 전승룬 애드온 업데이트가 있습니다. 홈페이지에서 최신 버전({})을 다운로드하세요. |r", LEGACY_RUNE_ADDON_REQUIRED_VERSION_TEXT);

			ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
		}
        else
		{
			std::string const message = Trinity::StringFormat(
                "|cffff2020[전승룬에드온]|r 전승룬 애드온이 검색되지 않았습니다. 홈페이지에서 전승룬 애드온을 설치하거나 활성화하세요. |r");

			ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
		}

        LegacyRuneAddonLoginChecks.erase(itr);
    }

    static bool ParseLegacyRuneGuildTechDonatePayload(std::string const& payload, std::string& nodeId, uint32& amount)
    {
        nodeId.clear();
        amount = 0;

        if (payload.compare(0, LEGACY_RUNE_GUILD_TECH_DONATE_PREFIX.size(), LEGACY_RUNE_GUILD_TECH_DONATE_PREFIX) != 0)
            return false;

        std::string body = payload.substr(LEGACY_RUNE_GUILD_TECH_DONATE_PREFIX.size());
        size_t separator = body.find(':');
        if (separator == std::string::npos || separator == 0 || separator + 1 >= body.size())
            return false;

        nodeId = body.substr(0, separator);
        return ParseLegacyRuneUInt32(body.substr(separator + 1), amount) && amount > 0;
    }

    static bool ParseLegacyRuneItemPayload(std::string const& payload, std::string const& prefix, bool requireGuid, LegacyRuneItemRef& itemRef)
    {
        itemRef = { 0, 0, 0, 0 };

        if (payload.compare(0, prefix.size(), prefix) != 0)
            return false;

        std::string body = payload.substr(prefix.size());
        size_t first = body.find(':');
        size_t second = first == std::string::npos ? std::string::npos : body.find(':', first + 1);
        size_t third = second == std::string::npos ? std::string::npos : body.find(':', second + 1);
        if (first == std::string::npos || second == std::string::npos)
            return false;

        uint32 itemEntry = 0;
        uint32 clientBag = 0;
        uint32 clientSlot = 0;
        uint64 itemGuid = 0;

        if (!ParseLegacyRuneUInt32(body.substr(0, first), itemEntry))
            return false;

        if (!ParseLegacyRuneUInt32(body.substr(first + 1, second - first - 1), clientBag))
            return false;

        std::string clientSlotText = third == std::string::npos ? body.substr(second + 1) : body.substr(second + 1, third - second - 1);
        if (!ParseLegacyRuneUInt32(clientSlotText, clientSlot))
            return false;

        if (third != std::string::npos && !ParseLegacyRuneUInt64(body.substr(third + 1), itemGuid))
            return false;

        if (requireGuid && !itemGuid)
            return false;

        itemRef = { itemEntry, clientBag, clientSlot, itemGuid };
        return itemRef.itemEntry != 0;
    }

    static bool ReadLegacyRuneNextToken(std::string const& text, size_t& offset, std::string& token)
    {
        if (offset > text.size())
            return false;

        size_t next = text.find(':', offset);
        if (next == std::string::npos)
        {
            token = text.substr(offset);
            offset = text.size() + 1;
            return !token.empty();
        }

        token = text.substr(offset, next - offset);
        offset = next + 1;
        return !token.empty();
    }

    static bool ParseLegacyRuneCombinePayload(std::string const& payload, LegacyRuneItemRef (&itemRefs)[3])
    {
        itemRefs[0] = { 0, 0, 0, 0 };
        itemRefs[1] = { 0, 0, 0, 0 };
        itemRefs[2] = { 0, 0, 0, 0 };

        if (payload.compare(0, LEGACY_RUNE_COMBINE_PREFIX.size(), LEGACY_RUNE_COMBINE_PREFIX) != 0)
            return false;

        std::string body = payload.substr(LEGACY_RUNE_COMBINE_PREFIX.size());
        size_t offset = 0;

        for (uint8 i = 0; i < 3; ++i)
        {
            std::string token;
            uint32 itemEntry = 0;
            uint32 clientBag = 0;
            uint32 clientSlot = 0;
            uint64 itemGuid = 0;

            if (!ReadLegacyRuneNextToken(body, offset, token) || !ParseLegacyRuneUInt32(token, itemEntry))
                return false;

            if (!ReadLegacyRuneNextToken(body, offset, token) || !ParseLegacyRuneUInt32(token, clientBag))
                return false;

            if (!ReadLegacyRuneNextToken(body, offset, token) || !ParseLegacyRuneUInt32(token, clientSlot))
                return false;

            if (!ReadLegacyRuneNextToken(body, offset, token) || !ParseLegacyRuneUInt64(token, itemGuid))
                return false;

            itemRefs[i] = { itemEntry, clientBag, clientSlot, itemGuid };
        }

        return offset == body.size() + 1;
    }

    static bool ParseLegacyRuneCombinePreviewPayload(std::string const& payload, uint32 (&itemEntries)[3])
    {
        itemEntries[0] = 0;
        itemEntries[1] = 0;
        itemEntries[2] = 0;

        if (payload.compare(0, LEGACY_RUNE_COMBINE_GET_PREFIX.size(), LEGACY_RUNE_COMBINE_GET_PREFIX) != 0)
            return false;

        std::string body = payload.substr(LEGACY_RUNE_COMBINE_GET_PREFIX.size());
        size_t offset = 0;

        for (uint8 i = 0; i < 3; ++i)
        {
            std::string token;
            if (!ReadLegacyRuneNextToken(body, offset, token) || !ParseLegacyRuneUInt32(token, itemEntries[i]))
                return false;
        }

        return offset == body.size() + 1;
    }

    static bool TryHandleLegacyRuneAddonPacket(WorldSession* session, WorldPacket& packet)
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

        if (language != LANG_ADDON)
            return false;

        switch (chatType)
        {
            case CHAT_MSG_WHISPER:
                break;
            default:
                return false;
        }

        std::string targetOrChannel;
        data >> targetOrChannel;

        if (!targetOrChannel.empty() && targetOrChannel != player->GetName())
            return false;

        std::string msg;
        data >> msg;

        std::string payload;
        if (!GetLegacyRuneAddonPayload(msg, payload))
            return false;

        if (payload == LEGACY_RUNE_PATCH4_OK_COMMAND || payload == LEGACY_RUNE_PATCH4_MISSING_COMMAND)
        {
            MarkLegacyRuneAddonDetected(player);

            if (payload == LEGACY_RUNE_PATCH4_MISSING_COMMAND)
			{
				std::string const message = Trinity::StringFormat(
                "|cffff2020[패치관리]|r 패치4가 감지되지 않았습니다. 일부 콘텐츠를 이용할 수 없습니다. 홈페이지에서 패치4를 설치하세요 |r");

				ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
	            return true;
			}
        }

        if (payload.compare(0, LEGACY_RUNE_ADDON_VERSION_PREFIX.size(), LEGACY_RUNE_ADDON_VERSION_PREFIX) == 0)
        {
            uint32 addonVersion = 0;
            ParseLegacyRuneUInt32(payload.substr(LEGACY_RUNE_ADDON_VERSION_PREFIX.size()), addonVersion);
            MarkLegacyRuneAddonDetected(player);
            HandleLegacyRuneAddonVersion(player, addonVersion);
            return true;
        }

        if (payload == LEGACY_RUNE_GET_COMMAND)
        {
            MarkLegacyRuneAddonDetected(player);
            SendLegacyRuneState(player);
            return true;
        }

        if (payload == LEGACY_RUNE_MAGIC_ALTAR_COMBINE_COMMAND)
        {
            HandleLegacyRuneMagicAltarCombine(player);
            return true;
        }

        if (payload == LEGACY_RUNE_ARMORY_OPEN_COMMAND)
        {
            HandleLegacyRuneArmoryOpen(player);
            return true;
        }

        if (payload.compare(0, LEGACY_RUNE_ARMORY_SEARCH_PREFIX.size(), LEGACY_RUNE_ARMORY_SEARCH_PREFIX) == 0)
        {
            HandleLegacyRuneArmorySearch(player, payload.substr(LEGACY_RUNE_ARMORY_SEARCH_PREFIX.size()));
            return true;
        }

        if (payload.compare(0, LEGACY_RUNE_ARMORY_BUY_PREFIX.size(), LEGACY_RUNE_ARMORY_BUY_PREFIX) == 0)
        {
            uint32 shopId = 0;
            if (!ParseLegacyRuneUInt32(payload.substr(LEGACY_RUNE_ARMORY_BUY_PREFIX.size()), shopId))
            {
                SendLegacyRuneArmoryBuyFail(player, "구매할 아이템 정보가 올바르지 않습니다.");
                return true;
            }

            HandleLegacyRuneArmoryBuy(player, shopId);
            return true;
        }

        if (payload == LEGACY_RUNE_GUILD_TECH_OPEN_COMMAND)
        {
            SendLegacyRuneGuildTechState(player);
            return true;
        }

        if (payload.compare(0, LEGACY_RUNE_GUILD_TECH_DONATE_PREFIX.size(), LEGACY_RUNE_GUILD_TECH_DONATE_PREFIX) == 0)
        {
            std::string nodeId;
            uint32 amount = 0;
            if (!ParseLegacyRuneGuildTechDonatePayload(payload, nodeId, amount))
            {
                SendLegacyRuneGuildTechFail(player, "기부 요청 형식이 올바르지 않습니다.");
                return true;
            }

            HandleLegacyRuneGuildTechDonate(player, nodeId, amount);
            return true;
        }

        uint32 combinePreviewEntries[3] = {};
        if (ParseLegacyRuneCombinePreviewPayload(payload, combinePreviewEntries))
        {
            HandleLegacyRuneCombinePreview(player, combinePreviewEntries);
            return true;
        }

        LegacyRuneItemRef combineRefs[3] = {};
        if (ParseLegacyRuneCombinePayload(payload, combineRefs))
        {
            HandleLegacyRuneCombine(player, combineRefs);
            return true;
        }

        LegacyRuneItemRef itemRef = { 0, 0, 0, 0 };
        if (ParseLegacyRuneItemPayload(payload, LEGACY_RUNE_CHECK_PREFIX, false, itemRef))
        {
            HandleLegacyRuneCheck(player, itemRef);
            return true;
        }

        if (!ParseLegacyRuneItemPayload(payload, LEGACY_RUNE_APPLY_PREFIX, true, itemRef))
            return false;

        HandleLegacyRuneApply(player, itemRef);
        return true;
    }
}

class legacy_rune_serverscript : public ServerScript
{
public:
    legacy_rune_serverscript() : ServerScript("legacy_rune_serverscript") { }

    void OnPacketReceive(WorldSession* session, WorldPacket& packet) override
    {
        if (session && packet.GetOpcode() == CMSG_PLAYER_LOGIN)
            QueueLegacyRuneAddonVersionCheck(session);

        if (TryHandleLegacyRuneAddonPacket(session, packet))
            return;

        TrySendLegacyRuneAddonInstallOrUpdateNotice(session);
    }
};

class go_legacy_rune_magic_altarAI : public GameObjectAI
{
public:
    explicit go_legacy_rune_magic_altarAI(GameObject* gameObject) : GameObjectAI(gameObject) { }

    bool OnGossipHello(Player* player) override
    {
        MarkLegacyRuneMagicAltarAccess(player);
        SendLegacyRuneAddonMessage(player, LEGACY_RUNE_MAGIC_ALTAR_OPEN_COMMAND);
        return true;
    }
};

class go_legacy_rune_magic_altar : public GameObjectScript
{
public:
    go_legacy_rune_magic_altar() : GameObjectScript("go_legacy_rune_magic_altar") { }

    GameObjectAI* GetAI(GameObject* gameObject) const override
    {
        return new go_legacy_rune_magic_altarAI(gameObject);
    }
};

















































//이벤트 달력표시
namespace
{
    static std::string const LEGACY_RUNE_CALENDAR_PREFIX = "WCAL";

    struct CalendarDate
    {
        uint32 Year = 0;
        uint32 Month = 1;
        uint32 Day = 1;
    };

    struct CalendarEvent
    {
        std::string DateKey;
        std::string EventType;
        uint32 Priority = 100;
        std::string ShortText;
        std::string Title;
        std::string Description;
        uint32 MapId = 0;
    };

    static std::string Pad2(uint32 value)
    {
        if (value < 10)
            return "0" + std::to_string(value);

        return std::to_string(value);
    }

    static std::string MakeMonthKey(uint32 year, uint32 month)
    {
        return std::to_string(year) + Pad2(month);
    }

    static std::string MakeDateKey(uint32 year, uint32 month, uint32 day)
    {
        return MakeMonthKey(year, month) + Pad2(day);
    }

    static CalendarDate GetLocalCalendarDate()
    {
        std::time_t now = std::time(nullptr);
        std::tm localTime = {};

#if defined(_WIN32)
        localtime_s(&localTime, &now);
#else
        localtime_r(&now, &localTime);
#endif

        CalendarDate date;
        date.Year = uint32(localTime.tm_year + 1900);
        date.Month = uint32(localTime.tm_mon + 1);
        date.Day = uint32(localTime.tm_mday);
        return date;
    }

    static bool IsDigitString(std::string const& text)
    {
        if (text.empty())
            return false;

        for (char ch : text)
            if (ch < '0' || ch > '9')
                return false;

        return true;
    }

    static bool ParseMonthKey(std::string const& monthKey, uint32& year, uint32& month)
    {
        if (monthKey.size() != 6 || !IsDigitString(monthKey))
            return false;

        year = uint32(std::stoul(monthKey.substr(0, 4)));
        month = uint32(std::stoul(monthKey.substr(4, 2)));

        if (year < 2000 || year > 2100)
            return false;

        if (month < 1 || month > 12)
            return false;

        return true;
    }

    static std::string CleanCalendarText(std::string text)
    {
        for (char& ch : text)
        {
            if (ch == ':' || ch == '\t' || ch == '\r' || ch == '\n')
                ch = ' ';
        }

        return text;
    }

    static std::string LimitCalendarText(std::string text, std::size_t maxSize)
    {
        text = CleanCalendarText(text);

        if (text.size() <= maxSize)
            return text;

        text.resize(maxSize);
        return text;
    }

    static std::string GetRaidMapName(uint32 mapId)
    {
        switch (mapId)
        {
            case 249: return "오닉시아의 둥지";
            case 409: return "화산 심장부";
            case 469: return "검은날개 둥지";
            case 509: return "안퀴라즈 폐허";
            case 531: return "안퀴라즈 사원";
            case 532: return "카라잔";
            case 533: return "낙스라마스";
            case 544: return "마그테리돈의 둥지";
            case 548: return "불뱀 제단";
            case 550: return "폭풍우 요새";
            case 564: return "검은 사원";
            case 565: return "그룰의 둥지";
            case 568: return "줄아만";
            case 580: return "태양샘 고원";
            case 603: return "울두아르";
            case 615: return "흑요석 성소";
            case 616: return "영원의 눈";
            case 624: return "아카본 석실";
            case 631: return "얼음왕관 성채";
            case 649: return "십자군의 시험장";
            case 724: return "루비 성소";
            default: return "인던 " + std::to_string(mapId);
        }
    }

    static std::string GetRaidMapShortName(uint32 mapId)
    {
        switch (mapId)
        {
            case 249: return "오닉시아의 둥지";
            case 533: return "낙스라마스";
            case 603: return "울두아르";
            case 615: return "흑요석 성소";
            case 616: return "영원의 눈";
            case 624: return "아카본";
            case 631: return "얼음왕관 성채";
            case 649: return "십자군의 시험장";
            case 724: return "루비 성소";
            default: return std::to_string(mapId);
        }
    }

    static void AddCalendarEvent(std::vector<CalendarEvent>& events,
                                 std::string const& dateKey,
                                 std::string const& eventType,
                                 uint32 priority,
                                 std::string const& shortText,
                                 std::string const& title,
                                 std::string const& description,
                                 uint32 mapId)
    {
        CalendarEvent eventInfo;
        eventInfo.DateKey = LimitCalendarText(dateKey, 8);
        eventInfo.EventType = LimitCalendarText(eventType.empty() ? "custom" : eventType, 32);
        eventInfo.Priority = priority;
        eventInfo.ShortText = LimitCalendarText(shortText.empty() ? "일정" : shortText, 24);
        eventInfo.Title = LimitCalendarText(title.empty() ? eventInfo.ShortText : title, 56);
        eventInfo.Description = LimitCalendarText(description, 80);
        eventInfo.MapId = mapId;
        events.push_back(eventInfo);
    }

    static void SendCalendarAddonMessage(Player* player, std::string const& payload)
    {
        if (!player || payload.empty())
            return;

        // WhisperAddon uses one addon whisper packet. Keep payload compact.
        player->WhisperAddon(LEGACY_RUNE_CALENDAR_PREFIX + "\t" + payload, player);
    }

    static bool IsCalendarRequestPayload(std::string const& payload)
    {
        return payload == "GET" || payload.compare(0, 4, "GET:") == 0;
    }

    static bool GetCalendarRequestPayload(std::string const& msg, std::string& payload)
    {
        payload.clear();

        if (IsCalendarRequestPayload(msg))
        {
            payload = msg;
            return true;
        }

        if (msg.compare(0, LEGACY_RUNE_CALENDAR_PREFIX.size(), LEGACY_RUNE_CALENDAR_PREFIX) != 0)
            return false;

        if (msg.size() <= LEGACY_RUNE_CALENDAR_PREFIX.size())
            return false;

        char separator = msg[LEGACY_RUNE_CALENDAR_PREFIX.size()];
        if (separator != '\t' && separator != ' ' && separator != ':')
            return false;

        std::string innerPayload = msg.substr(LEGACY_RUNE_CALENDAR_PREFIX.size() + 1);
        if (!IsCalendarRequestPayload(innerPayload))
            return false;

        payload = innerPayload;
        return true;
    }

    static bool GetRequestedMonth(std::string const& payload, uint32& year, uint32& month)
    {
        if (payload.compare(0, 4, "GET:") == 0)
            return ParseMonthKey(payload.substr(4), year, month);

        CalendarDate today = GetLocalCalendarDate();
        year = today.Year;
        month = today.Month;
        return true;
    }

    static bool CustomCalendarTableExists()
    {
        QueryResult result = LoginDatabase.Query(
            "SELECT 1 "
            "FROM information_schema.TABLES "
            "WHERE TABLE_SCHEMA = 'appdata' "
            "AND TABLE_NAME = 'custom_calendar_events' "
            "LIMIT 1");

        return bool(result);
    }

    static void LoadWeeklyX8Events(std::vector<CalendarEvent>& events, uint32 requestYear, uint32 requestMonth)
    {
        CalendarDate today = GetLocalCalendarDate();
        if (today.Year != requestYear || today.Month != requestMonth)
            return;

        QueryResult result = LoginDatabase.Query(
            "SELECT map_id, CAST(dmg_mult AS UNSIGNED) "
            "FROM appdata.instance_damage_mult "
            "WHERE dmg_mult = 8 "
            "ORDER BY map_id ASC");

        if (!result)
            return;

        std::string todayKey = MakeDateKey(today.Year, today.Month, today.Day);

        do
        {
            Field* fields = result->Fetch();
            uint32 mapId = fields[0].GetUInt32();
            uint32 damageMult = fields[1].GetUInt32();
            std::string mapName = GetRaidMapName(mapId);
            std::string shortText = GetRaidMapShortName(mapId);
            std::string title = "주간 공격대 -80%" + mapName;
            std::string description = "오늘 이벤트 공격대입니다.";

            AddCalendarEvent(events, todayKey, "weekly_x8", 10, shortText, title, description, mapId);
        } while (result->NextRow());
    }

    static void LoadCustomDbEvents(std::vector<CalendarEvent>& events, uint32 requestYear, uint32 requestMonth)
    {
        if (!CustomCalendarTableExists())
            return;

        std::string query =
            "SELECT YEAR(event_date), MONTH(event_date), DAYOFMONTH(event_date), "
            "event_type, sort_order, short_text, title, description, COALESCE(map_id, 0) "
            "FROM appdata.custom_calendar_events "
            "WHERE active = 1 "
            "AND YEAR(event_date) = " + std::to_string(requestYear) + " "
            "AND MONTH(event_date) = " + std::to_string(requestMonth) + " "
            "ORDER BY event_date ASC, sort_order ASC, id ASC";

        QueryResult result = LoginDatabase.Query(query.c_str());
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            uint32 year = fields[0].GetUInt32();
            uint32 month = fields[1].GetUInt32();
            uint32 day = fields[2].GetUInt32();
            std::string eventType = fields[3].GetString();
            uint32 priority = fields[4].GetUInt32();
            std::string shortText = fields[5].GetString();
            std::string title = fields[6].GetString();
            std::string description = fields[7].GetString();
            uint32 mapId = fields[8].GetUInt32();

            AddCalendarEvent(
                events,
                MakeDateKey(year, month, day),
                eventType,
                priority,
                shortText,
                title,
                description,
                mapId);
        } while (result->NextRow());
    }

    static void SendCalendarMonth(Player* player, uint32 requestYear, uint32 requestMonth)
    {
        if (!player)
            return;

        std::string monthKey = MakeMonthKey(requestYear, requestMonth);
        std::vector<CalendarEvent> events;

        LoadWeeklyX8Events(events, requestYear, requestMonth);
        LoadCustomDbEvents(events, requestYear, requestMonth);

        SendCalendarAddonMessage(player, "CLEAR:" + monthKey);

        for (CalendarEvent const& eventInfo : events)
        {
            SendCalendarAddonMessage(
                player,
                "EVENT:" + eventInfo.DateKey +
                ":" + eventInfo.EventType +
                ":" + std::to_string(eventInfo.Priority) +
                ":" + eventInfo.ShortText +
                ":" + eventInfo.Title +
                ":" + eventInfo.Description +
                ":" + std::to_string(eventInfo.MapId));
        }

        SendCalendarAddonMessage(player, "END:" + monthKey);
    }
}

class custom_legacy_rune_calendar : public PlayerScript
{
public:
    custom_legacy_rune_calendar() : PlayerScript("custom_legacy_rune_calendar") { }

    void OnLogin(Player* player, bool firstLogin) override
    {
        if (!player)
            return;

        if (!firstLogin)
            return;

        SendCalendarAddonMessage(player, "GUIDE:OPEN:차후등록");
    }

    void OnChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg, Player* receiver) override
    {
        if (!player || receiver != player)
            return;

        std::string payload;
        if (!GetCalendarRequestPayload(msg, payload))
            return;

        uint32 requestYear = 0;
        uint32 requestMonth = 0;
        if (!GetRequestedMonth(payload, requestYear, requestMonth))
            return;

        SendCalendarMonth(player, requestYear, requestMonth);
    }
};




















void AddSC_custom_spell_script()
{
    new spell_guild_hearthstone();
	new guild_tech_combat_bonus();
	RegisterSpellScript(spell_wg_rocket_propelled_goblin_grenade);
	RegisterSpellScript(spell_custom_firestone_preparation_redirect);
	new spell_custom_gem_combine();
	RegisterSpellScript(spell_guardian_purify);
	new legacy_rune_serverscript();
	new custom_legacy_rune_calendar();
    new go_legacy_rune_magic_altar();
}