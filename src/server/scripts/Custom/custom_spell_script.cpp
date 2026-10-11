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
#include "SpellMgr.h"
#include "World.h"

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
#include "DBCStores.h"
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
    static constexpr uint32 LEGACY_RUNE_ADDON_REQUIRED_VERSION = 7;
    static constexpr uint32 LEGACY_RUNE_ADDON_LOGIN_CHECK_DELAY_SECONDS = 8;
    static std::string const LEGACY_RUNE_ADDON_REQUIRED_VERSION_TEXT = "7";
    static std::string const LEGACY_RUNE_ADDON_VERSION_PREFIX = "VERSION:";
	static std::string const LEGACY_RUNE_MPQ_RUNTIME_HELLO_COMMAND = "MPQRUNTIME:HELLO";

    /*
     * Custom Update - 2026-07-18
     * 패치4 Spell.dbc 표식 감지 및 미설치 안내
     */
    static std::string const LEGACY_RUNE_PATCH4_OK_COMMAND = "PATCH4:OK";
    static std::string const LEGACY_RUNE_PATCH4_MISSING_COMMAND = "PATCH4:MISSING";

	/*
     * Custom Update - 2026-07-27
     * 패치4 미설치 안내를 3초 간격으로 총 3회 출력
     */
    static constexpr uint32 LEGACY_RUNE_PATCH4_NOTICE_INTERVAL_SECONDS = 3;
    static constexpr uint8 LEGACY_RUNE_PATCH4_NOTICE_TOTAL_COUNT = 3;

    /*
     * Custom Update - 2026-07-18
     * 전승 룬 애드온에서 길드 테크 조회 및 RP 기부 처리
     */
    static std::string const LEGACY_RUNE_APPLY_PREFIX = "APPLY:";
    static std::string const LEGACY_RUNE_CHECK_PREFIX = "CHECK:";
    static std::string const LEGACY_RUNE_COMBINE_PREFIX = "COMBINE:";
    static std::string const LEGACY_RUNE_COMBINE_GET_PREFIX = "GETCOMBINE:";
    static std::string const LEGACY_RUNE_GET_COMMAND = "GET";

    /*
     * Custom Update - 2026-07-28
     * 전승룬 컬렉션 조회
     */
    static std::string const LEGACY_RUNE_COLLECTION_GET_COMMAND = "COLLECTION:GET";

    /*
     * Custom Update - 2026-08-16
     * 기간제 커스텀 스펠 조회
     */
    static std::string const LEGACY_RUNE_CUSTOM_SPELL_GET_COMMAND = "CUSTOMSPELL:GET";

    /*
     * Custom Update - 2026-07-28
     * 전승룬 컬렉션 완성 보상: 하루 1회 전승 재각인
     */
    static std::string const LEGACY_RUNE_REENGRAVE_GET_COMMAND = "REENGRAVE:GET";
    static std::string const LEGACY_RUNE_REENGRAVE_APPLY_PREFIX = "REENGRAVE:APPLY:";

    /*
     * Custom Update - 2026-07-28
     * 전승룬 컬렉션 10종 완성 칭호
     */
    static constexpr uint32 LEGACY_RUNE_MASTER_TITLE_ID = 178;
    static constexpr uint32 LEGACY_RUNE_MASTER_TITLE_MASK_ID = 143;
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

		uint32 secondSpellId;
		uint32 allianceSpellId;
		uint32 hordeSpellId;
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

	struct LegacyRunePatch4Notice
    {
        std::chrono::steady_clock::time_point nextNoticeAt;
        uint8 remainingCount;
    };

    static std::unordered_map<uint32, LegacyRuneAddonLoginCheck> LegacyRuneAddonLoginChecks;
    static std::unordered_map<uint32, std::chrono::steady_clock::time_point> LegacyRuneMagicAltarAccess;
	static std::unordered_map<uint32, LegacyRunePatch4Notice> LegacyRunePatch4Notices;

    static constexpr LegacyRuneSpellMap LegacyRuneSpellMaps[] =
	{
		{ 56930, 0, 1804,  633, 4,  0,     0,     0 }, // 전승 룬 : 자물쇠 따기, 도적 착용 불가
		{ 56931, 0, 1953,  0,   8,  0,     0,     0 }, // 전승 룬 : 점멸, 마법사 착용 불가
		{ 56932, 0, 5384,  0,   3,  0,     0,     0 }, // 전승 룬 : 죽은척 하기, 사냥꾼 착용 불가
		{ 56933, 0, 900205, 0,   1,  0,     0,     0 }, // 전승 룬 : 지휘의 외침, 전사 착용 불가
		{ 56934, 0, 19752, 0,   2,  0,     0,     0 }, // 전승 룬 : 성스러운 중재, 성기사 착용 불가
		{ 56935, 0, 29166, 0,  11,  0,     0,     0 }, // 전승 룬 : 정신 자극, 드루이드 착용 불가
		{ 56936, 0, 49576, 0,   6,  0,     0,     0 }, // 전승 룬 : 죽음의 손아귀, 죽음의 기사 착용 불가
		{ 56937, 0, 48066, 0,   5,  0,     0,     0 }, // 전승 룬 : 신의 권능 보호막, 사제 착용 불가
		{ 56938, 0, 0,     0,   7,  0, 32182,  2825 }, // 전승 룬 : 피의 욕망·영웅심, 주술사 착용 불가
		{ 56939, 0, 48018, 0,   9, 48020,  0,     0 }  // 전승 룬 : 악마의 마법진, 흑마법사 착용 불가
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
        { "r1a", "황금의 흐름", 500000, 1 },
        { "r1b", "상인의 교섭", 500000, 1 },
        { "r1c", "기여의 확장", 500000, 1 },
        { "r2a", "결속의 전달", 1500000, 2 },
        { "r2b", "귀환의 숙련", 1500000, 2 },
        { "r2c", "장비의 관리", 1500000, 2 },
        { "r3a", "파괴의 교리", 3000000, 3 },
        { "r3b", "불굴의 수호", 3000000, 3 },
        { "r3c", "생명의 회복", 3000000, 3 },
        { "core", "최후의 결의", 10000000, 4 }
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

	static void TeachLegacyRuneSingleSpell(Player* player, uint32 spellId)
	{
		if (!player || !spellId)
			return;

		if (player->HasSpell(spellId))
			return;

		player->LearnSpell(spellId, false);
	}

	/*
	 * Custom Update - 2026-07-27
	 * 등급이 있는 전승 룬 주문 제거 시 1레벨부터 최고 레벨까지 전체 제거
	 */
	static void RemoveLegacyRuneSingleSpell(Player* player, uint32 spellId)
	{
		if (!player || !spellId)
			return;

		uint32 firstSpellId = sSpellMgr->GetFirstSpellInChain(spellId);
		if (!firstSpellId)
			firstSpellId = spellId;

		if (!player->HasSpell(firstSpellId) && !player->HasSpell(spellId))
			return;

		player->RemoveSpell(firstSpellId, false, false);
	}

    static void TeachLegacyRuneSpell(Player* player, LegacyRuneSpellMap const* runeMap)
	{
		if (!player || !runeMap)
			return;

		TeachLegacyRuneSingleSpell(player, runeMap->spellId);
		TeachLegacyRuneSingleSpell(player, runeMap->secondSpellId);

		if (player->GetTeamId() == TEAM_ALLIANCE)
			TeachLegacyRuneSingleSpell(player, runeMap->allianceSpellId);
		else
			TeachLegacyRuneSingleSpell(player, runeMap->hordeSpellId);

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

		RemoveLegacyRuneSingleSpell(player, runeMap->spellId);
		RemoveLegacyRuneSingleSpell(player, runeMap->secondSpellId);
		RemoveLegacyRuneSingleSpell(player, runeMap->allianceSpellId);
		RemoveLegacyRuneSingleSpell(player, runeMap->hordeSpellId);

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

    /*
     * Custom Update - 2026-07-28
     * 전승룬 캐릭터별 컬렉션 영구 기록 및 애드온 동기화
     */
    static bool IsLegacyRuneCollectionItem(uint32 itemEntry)
    {
        for (LegacyRuneSpellMap const& runeMap : LegacyRuneSpellMaps)
            if (runeMap.applyItemEntry == itemEntry)
                return true;

        return false;
    }

    static void MarkLegacyRuneCollection(Player* player, uint32 itemEntry)
    {
        if (!player || !player->GetSession() || !IsLegacyRuneCollectionItem(itemEntry))
            return;

        CharacterDatabase.PExecute(
            "INSERT IGNORE INTO legacy_rune_collection "
            "(guid, account_id, rune_item_id) "
            "VALUES ({}, {}, {})",
            player->GetGUID().GetCounter(),
            player->GetSession()->GetAccountId(),
            itemEntry);
    }

    static uint32 GetLegacyRuneCollectionCount(Player* player)
    {
        if (!player)
            return 0;

        static constexpr uint32 collectionSize =
            sizeof(LegacyRuneSpellMaps) / sizeof(LegacyRuneSpellMaps[0]);

        bool collected[collectionSize] = {};

        for (uint32 index = 0; index < collectionSize; ++index)
        {
            if (LegacyRuneSpellMaps[index].blockedClass == player->GetClass())
                collected[index] = true;
        }

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT rune_item_id "
            "FROM legacy_rune_collection "
            "WHERE guid = {}",
            player->GetGUID().GetCounter());

        if (result)
        {
            do
            {
                uint32 const itemEntry = result->Fetch()[0].GetUInt32();

                for (uint32 index = 0; index < collectionSize; ++index)
                {
                    if (LegacyRuneSpellMaps[index].applyItemEntry == itemEntry)
                    {
                        collected[index] = true;
                        break;
                    }
                }
            }
            while (result->NextRow());
        }

        uint32 collectedCount = 0;

        for (bool isCollected : collected)
            if (isCollected)
                ++collectedCount;

        return collectedCount;
    }

    static bool TryGrantLegacyRuneMasterTitle(Player* player)
    {
        if (!player || !player->GetSession())
            return false;

        static constexpr uint32 collectionSize =
            sizeof(LegacyRuneSpellMaps) / sizeof(LegacyRuneSpellMaps[0]);

        if (GetLegacyRuneCollectionCount(player) < collectionSize)
            return false;

        CharTitlesEntry const* titleEntry =
            sCharTitlesStore.LookupEntry(LEGACY_RUNE_MASTER_TITLE_ID);

        if (!titleEntry
            || titleEntry->MaskID != LEGACY_RUNE_MASTER_TITLE_MASK_ID)
        {
            player->GetSession()->SendNotification(
                "전승룬 컬렉션: 전승의 대가 칭호 DBC 정보를 찾을 수 없습니다.");
            return false;
        }

        if (player->HasTitle(titleEntry))
            return true;

        player->SetTitle(titleEntry);
        player->GetSession()->SendNotification(
            "전승룬 컬렉션 10종을 완성하여 '전승의 대가' 칭호를 획득했습니다.");

        SendLegacyRuneAddonMessage(
            player,
            "COLLECTION:TITLE:AWARDED");

        return true;
    }

    static void SendLegacyRuneCollectionState(Player* player)
    {
        if (!player)
            return;

        /*
         * 컬렉션 기능 적용 전에 현재 장착 중이던 전승룬도
         * 컬렉션을 처음 조회할 때 자동 등록한다.
         */
        uint32 const currentItemEntry = GetSavedLegacyRuneItemEntry(player);
        if (currentItemEntry)
            MarkLegacyRuneCollection(player, currentItemEntry);

        TryGrantLegacyRuneMasterTitle(player);

        std::string payload = "COLLECTION:STATE:";

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT rune_item_id "
            "FROM legacy_rune_collection "
            "WHERE guid = {} "
            "ORDER BY rune_item_id",
            player->GetGUID().GetCounter());

        if (result)
        {
            bool first = true;

            do
            {
                uint32 const itemEntry = result->Fetch()[0].GetUInt32();
                if (!IsLegacyRuneCollectionItem(itemEntry))
                    continue;

                if (!first)
                    payload += ",";

                payload += std::to_string(itemEntry);
                first = false;
            }
            while (result->NextRow());
        }

        SendLegacyRuneAddonMessage(player, payload);
    }

    /*
     * Custom Update - 2026-08-16
     * 기간제 커스텀 스펠 조회
     *
     * DB: character_temporary_spell
     * 전송 형식:
     * CUSTOMSPELL:STATE:900212:남은초,900213:남은초
     */
    static void SendLegacyRuneCustomSpellState(Player* player)
    {
        if (!player)
            return;

        uint32 const guid = player->GetGUID().GetCounter();
        uint32 const now = uint32(::time(nullptr));

        std::string payload = "CUSTOMSPELL:STATE:";

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT `spell`, `expire_time` "
            "FROM `character_temporary_spell` "
            "WHERE `guid` = {} "
            "ORDER BY `spell`",
            guid);

        bool first = true;

        if (result)
        {
            do
            {
                Field* fields = result->Fetch();

                uint32 const spellId = fields[0].GetUInt32();
                uint32 const expireTime = fields[1].GetUInt32();

                if (expireTime <= now)
                    continue;

                if (spellId != 900212 && spellId != 900213)
                    continue;

                uint32 const remaining = expireTime - now;

                if (!first)
                    payload += ",";

                payload += std::to_string(spellId);
                payload += ":";
                payload += std::to_string(remaining);

                first = false;
            }
            while (result->NextRow());
        }

        SendLegacyRuneAddonMessage(player, payload);
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

		/*
		 * Custom Update - 2026-08-01
		 *
		 * 애드온 길드 테크 기부 로그
		 * - 주간 기부 보상 정산용
		 * - created_at은 UNIX timestamp로 저장
		 */
		LoginDatabase.DirectPExecute(
			"INSERT INTO appdata.guild_tech_donation_log "
			"(guild_id, account_id, character_guid, node_id, amount, created_at) "
			"VALUES ({}, {}, {}, '{}', {}, UNIX_TIMESTAMP())",
			guildId,
			accountId,
			player->GetGUID().GetCounter(),
			node->nodeId,
			amount);

		SendLegacyRuneAddonMessage(
			player,
			"GUILDTECH:DONATE:OK:" +
			CleanLegacyRuneAddonText(
				std::string(node->name) +
				"에 " +
				std::to_string(amount) +
				" RP를 기부했습니다."));

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

    /*
     * Custom Update - 2026-07-28
     * 전승룬 컬렉션 10종 완성 보상: 하루 1회 전승 재각인
     */
    static bool HasCollectedLegacyRune(Player* player, uint32 itemEntry)
    {
        if (!player || !IsLegacyRuneCollectionItem(itemEntry))
            return false;

        LegacyRuneSpellMap const* runeMap =
            FindLegacyRuneApplyMap(itemEntry);

        if (!runeMap)
            return false;

        if (runeMap->blockedClass == player->GetClass())
            return true;

        QueryResult result = CharacterDatabase.PQuery(
            "SELECT 1 "
            "FROM legacy_rune_collection "
            "WHERE guid = {} AND rune_item_id = {} "
            "LIMIT 1",
            player->GetGUID().GetCounter(),
            itemEntry);

        return bool(result);
    }

    static bool IsLegacyRuneCollectionComplete(Player* player)
    {
        if (!player)
            return false;

        static constexpr uint32 collectionSize =
            sizeof(LegacyRuneSpellMaps) / sizeof(LegacyRuneSpellMaps[0]);

        return GetLegacyRuneCollectionCount(player) >= collectionSize;
    }

    static bool HasUsedLegacyRuneReengraveToday(Player* player)
	{
		if (!player)
			return false;

		QueryResult result = CharacterDatabase.PQuery(
			"SELECT 1 "
			"FROM legacy_rune_reengrave_daily "
			"WHERE guid = {} "
			"AND updated_at > DATE_SUB(CURRENT_TIMESTAMP, INTERVAL 1 HOUR) "
			"LIMIT 1",
			player->GetGUID().GetCounter());

		return bool(result);
	}

    static void SendLegacyRuneReengraveFail(
        Player* player,
        char const* reason)
    {
        SendLegacyRuneAddonMessage(
            player,
            std::string("REENGRAVE:FAIL:") +
                CleanLegacyRuneAddonText(
                    reason ? reason : "전승 재각인에 실패했습니다."));
    }

    static void SendLegacyRuneReengraveState(Player* player)
    {
        if (!player)
            return;

        bool const unlocked = IsLegacyRuneCollectionComplete(player);
        bool const available =
            unlocked && !HasUsedLegacyRuneReengraveToday(player);
        uint32 const currentItemEntry =
            GetSavedLegacyRuneItemEntry(player);

        SendLegacyRuneAddonMessage(
            player,
            "REENGRAVE:STATE:" +
                std::to_string(unlocked ? 1 : 0) + ":" +
                std::to_string(available ? 1 : 0) + ":" +
                std::to_string(currentItemEntry));
    }

    static void SaveLegacyRuneReengraveState(
        Player* player,
        ItemTemplate const* proto)
    {
        if (!player || !player->GetSession() || !proto)
            return;

        CharacterDatabase.PExecute(
            "INSERT INTO legacy_rune_character "
            "(guid, account_id, player_class, player_race, player_level, "
            "rune_slot, item_guid, item_entry, item_class, item_subclass) "
            "VALUES ({}, {}, {}, {}, {}, {}, 0, {}, {}, {}) "
            "ON DUPLICATE KEY UPDATE "
            "account_id = VALUES(account_id), "
            "player_class = VALUES(player_class), "
            "player_race = VALUES(player_race), "
            "player_level = VALUES(player_level), "
            "item_guid = 0, "
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
            proto->ItemId,
            proto->Class,
            proto->SubClass);
    }

    static void MarkLegacyRuneReengraveUsed(
        Player* player,
        uint32 itemEntry)
    {
        if (!player || !player->GetSession())
            return;

        CharacterDatabase.PExecute(
            "INSERT INTO legacy_rune_reengrave_daily "
            "(guid, account_id, last_reengrave_date, last_item_entry) "
            "VALUES ({}, {}, CURDATE(), {}) "
            "ON DUPLICATE KEY UPDATE "
            "account_id = VALUES(account_id), "
            "last_reengrave_date = VALUES(last_reengrave_date), "
            "last_item_entry = VALUES(last_item_entry), "
            "updated_at = CURRENT_TIMESTAMP",
            player->GetGUID().GetCounter(),
            player->GetSession()->GetAccountId(),
            itemEntry);
    }

    static bool HandleLegacyRuneReengrave(
        Player* player,
        uint32 itemEntry)
    {
        if (!player || !player->GetSession())
            return false;

        if (!IsLegacyRuneCollectionComplete(player))
        {
            SendLegacyRuneReengraveFail(
                player,
                "전승룬 컬렉션 10종을 모두 수집해야 재각인이 열립니다.");
            SendLegacyRuneReengraveState(player);
            return true;
        }

        LegacyRuneSpellMap const* runeMap =
            FindLegacyRuneApplyMap(itemEntry);

        if (!runeMap)
        {
            SendLegacyRuneReengraveFail(
                player,
                "등록되지 않은 전승룬 기술입니다.");
            return true;
        }

        if (runeMap->blockedClass == player->GetClass())
        {
            SendLegacyRuneReengraveFail(
                player,
                "자기 직업의 기본 기술은 재각인할 수 없습니다.");
            return true;
        }

        if (!HasCollectedLegacyRune(player, itemEntry))
        {
            SendLegacyRuneReengraveFail(
                player,
                "수집하지 않은 전승룬 기술입니다.");
            return true;
        }

        if (HasUsedLegacyRuneReengraveToday(player))
        {
            SendLegacyRuneReengraveFail(
                player,
                "전승 재각인 후 1시간이 지나야 다시 재각인할 수 있습니다.");
            SendLegacyRuneReengraveState(player);
            return true;
        }

        uint32 const previousItemEntry =
            GetSavedLegacyRuneItemEntry(player);

        if (previousItemEntry == itemEntry)
        {
            SendLegacyRuneReengraveFail(
                player,
                "현재 사용 중인 전승 기술입니다.");
            return true;
        }

        ItemTemplate const* proto =
            sObjectMgr->GetItemTemplate(itemEntry);

        if (!proto)
        {
            SendLegacyRuneReengraveFail(
                player,
                "전승룬 아이템 정보를 찾을 수 없습니다.");
            return true;
        }

        uint64 const previousItemGuid =
            GetSavedLegacyRuneItemGuid(player);

        if (previousItemEntry)
            RemoveLegacyRuneSpell(
                player,
                FindLegacyRuneApplyMap(previousItemEntry));

        if (previousItemGuid)
            DeleteLegacyRuneStoredItemByGuid(
                player,
                previousItemGuid);

        SaveLegacyRuneReengraveState(player, proto);
        TeachLegacyRuneSpell(player, runeMap);
        MarkLegacyRuneReengraveUsed(player, itemEntry);

        player->GetSession()->SendNotification(
            "전승 재각인: %s 기술로 변경했습니다. 1시간 후 다시 재각인할 수 있습니다.",
            GetLegacyRuneItemName(itemEntry).c_str());

        SendLegacyRuneAddonMessage(
            player,
            "REENGRAVE:OK:" +
                std::to_string(itemEntry) + ":" +
                CleanLegacyRuneAddonText(
                    GetLegacyRuneItemName(itemEntry)));

        SendLegacyRuneItemState(
            player,
            itemEntry,
            proto->Class,
            proto->SubClass);

        SendLegacyRuneReengraveState(player);
        return true;
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

        MarkLegacyRuneCollection(player, savedItemEntry);
        TryGrantLegacyRuneMasterTitle(player);

        SendLegacyRuneAddonMessage(
            player,
            "COLLECTION:UNLOCK:" + std::to_string(savedItemEntry));

        SendLegacyRuneReengraveState(player);
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

	    /*
     * Custom Update - 2026-07-27
     * 패치4 미설치 안내 반복 처리
     */
    static void SendLegacyRunePatch4MissingNotice(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        std::string const message = Trinity::StringFormat(
            "|cffff2020[패치관리]|r 신규패치4가 감지되지 않았습니다. 일부 콘텐츠를 이용할 수 없습니다. 홈페이지에서 신규패치4를 설치하세요.|r");

        ChatHandler(player->GetSession()).SendSysMessage(message.c_str());
    }

    static void CancelLegacyRunePatch4MissingNotice(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        LegacyRunePatch4Notices.erase(player->GetSession()->GetAccountId());
    }

    static void QueueLegacyRunePatch4MissingNotice(Player* player)
    {
        if (!player || !player->GetSession())
            return;

        uint32 const accountId = player->GetSession()->GetAccountId();

        LegacyRunePatch4Notices.erase(accountId);

        // 첫 번째 안내는 즉시 출력
        SendLegacyRunePatch4MissingNotice(player);

        if (LEGACY_RUNE_PATCH4_NOTICE_TOTAL_COUNT <= 1)
            return;

        LegacyRunePatch4Notices[accountId] =
        {
            std::chrono::steady_clock::now() +
                std::chrono::seconds(LEGACY_RUNE_PATCH4_NOTICE_INTERVAL_SECONDS),

            uint8(LEGACY_RUNE_PATCH4_NOTICE_TOTAL_COUNT - 1)
        };
    }

    static void UpdateLegacyRunePatch4MissingNotices()
    {
        if (LegacyRunePatch4Notices.empty())
            return;

        std::chrono::steady_clock::time_point const now =
            std::chrono::steady_clock::now();

        for (auto itr = LegacyRunePatch4Notices.begin(); itr != LegacyRunePatch4Notices.end();)
        {
            if (now < itr->second.nextNoticeAt)
            {
                ++itr;
                continue;
            }

            WorldSession* session = sWorld->FindSession(itr->first);
            Player* player = session ? session->GetPlayer() : nullptr;

            if (!player)
            {
                itr = LegacyRunePatch4Notices.erase(itr);
                continue;
            }

            SendLegacyRunePatch4MissingNotice(player);

            if (itr->second.remainingCount <= 1)
            {
                itr = LegacyRunePatch4Notices.erase(itr);
                continue;
            }

            --itr->second.remainingCount;
            itr->second.nextNoticeAt =
                now + std::chrono::seconds(LEGACY_RUNE_PATCH4_NOTICE_INTERVAL_SECONDS);

            ++itr;
        }
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

// ============================================================================
// [LIGHT_GUARDIAN_MPQ_AUTH - 향후 서버 강제 인증 적용]
//
// 현재는 기존 클라이언트 사용자를 위해 서버 강제 인증을 사용하지 않는다.
//
// 향후 patch-4.MPQ 등 필수 패치가 충분히 배포되면:
// 1. 플레이어 로그인 시 MPQ 미인증 상태로 등록
// 2. MPQRUNTIME:HELLO 수신 시 인증완료 상태로 변경
// 3. 로그인 후 약 10초 이내 HELLO가 없으면 서버에서 강제 접속 종료
// 4. 실제 강제 종료는 WorldSession::KickPlayer() 방식으로 구현
//
// 현재 클라이언트 동작:
// MPQRUNTIME:HELLO -> 서버 MPQRUNTIME:OK
//
// 주의: 기존 클라이언트가 남아있는 동안에는 강제 인증을 활성화하지 말 것.
// 검색 키워드: LIGHT_GUARDIAN_MPQ_AUTH
// ============================================================================

		if (payload == LEGACY_RUNE_MPQ_RUNTIME_HELLO_COMMAND)
		{
			SendLegacyRuneAddonMessage(player, "MPQRUNTIME:OK");
			return true;
		}

        if (payload == LEGACY_RUNE_PATCH4_OK_COMMAND)
        {
            MarkLegacyRuneAddonDetected(player);
            CancelLegacyRunePatch4MissingNotice(player);
            return true;
        }

        if (payload == LEGACY_RUNE_PATCH4_MISSING_COMMAND)
        {
            MarkLegacyRuneAddonDetected(player);
            QueueLegacyRunePatch4MissingNotice(player);
            return true;
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

        if (payload == LEGACY_RUNE_COLLECTION_GET_COMMAND)
        {
            MarkLegacyRuneAddonDetected(player);
            SendLegacyRuneCollectionState(player);
            return true;
        }

        /*
         * Custom Update - 2026-08-16
         * 기간제 커스텀 스펠 조회
         */
        if (payload == LEGACY_RUNE_CUSTOM_SPELL_GET_COMMAND)
        {
            MarkLegacyRuneAddonDetected(player);
            SendLegacyRuneCustomSpellState(player);
            return true;
        }

        if (payload == LEGACY_RUNE_REENGRAVE_GET_COMMAND)
        {
            MarkLegacyRuneAddonDetected(player);
            SendLegacyRuneReengraveState(player);
            return true;
        }

        if (payload.compare(
                0,
                LEGACY_RUNE_REENGRAVE_APPLY_PREFIX.size(),
                LEGACY_RUNE_REENGRAVE_APPLY_PREFIX) == 0)
        {
            uint32 itemEntry = 0;

            if (!ParseLegacyRuneUInt32(
                    payload.substr(
                        LEGACY_RUNE_REENGRAVE_APPLY_PREFIX.size()),
                    itemEntry))
            {
                SendLegacyRuneReengraveFail(
                    player,
                    "재각인할 전승 기술 정보가 올바르지 않습니다.");
                return true;
            }

            HandleLegacyRuneReengrave(player, itemEntry);
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

/*
 * Custom Update - 2026-07-27
 * 패치4 미설치 안내 반복 타이머
 */
class legacy_rune_worldscript : public WorldScript
{
public:
    legacy_rune_worldscript() : WorldScript("legacy_rune_worldscript") { }

    void OnUpdate(uint32 /*diff*/) override
    {
        UpdateLegacyRunePatch4MissingNotices();
    }
};

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























//전승룬 제작
namespace
{
    /*
     * Custom Update - 2026-07-31
     * 달력 아래에 독립적으로 연결되는 전승룬 제작 시스템
     *
     * - 월드 DB에서 활성 레시피와 재료를 읽는다.
     * - 캐릭터 DB에는 습득한 recipe_type과 마지막 제작 시각을 저장한다.
     * - 레시피별 제작 쿨다운은 12시간이다.
     * - 애드온에는 전체 활성 레시피, 소유 여부, 남은 쿨다운을 전송한다.
     * - 습득했고 쿨다운이 끝난 레시피만 밝게 표시하고 제작할 수 있다.
     * - 처형의 레시피 아이템 사용 시 recipe_type 1~5 중 하나를
     *   현재 습득 여부와 관계없이 완전히 무작위로 선택한다.
     * - 이미 습득한 타입이 선택되어도 아이템은 정상 소모된다.
     * - 첫 레시피:
     *   죽음의 룬 100개 + 심연의 수정 1개
     *   -> 절단의 파편 1개
     */
    static std::string const LEGACY_RUNE_CRAFT_PREFIX = "LRCRAFT";
    static std::string const LEGACY_RUNE_CRAFT_GET_COMMAND = "GET";
    static std::string const LEGACY_RUNE_CRAFT_MAKE_PREFIX = "MAKE:";
    static std::string const LEGACY_RUNE_CRAFT_ITEM_GET_PREFIX = "ITEM:GET:";
    static constexpr uint32 LEGACY_RUNE_CRAFT_COOLDOWN_SECONDS =
        12 * 60 * 60;

    static constexpr uint32 LEGACY_RUNE_EXECUTION_RECIPE_ITEM_ID = 33145;
    static constexpr uint32 LEGACY_RUNE_EXECUTION_RECIPE_MIN_TYPE = 1;
    static constexpr uint32 LEGACY_RUNE_EXECUTION_RECIPE_MAX_TYPE = 5;

    struct LegacyRuneCraftRecipe
    {
        uint32 RecipeType = 0;
        std::string RecipeName;
        uint32 ResultItem = 0;
        uint32 ResultCount = 0;
    };

    struct LegacyRuneCraftMaterial
    {
        uint8 MaterialSlot = 0;
        uint32 ItemEntry = 0;
        uint32 ItemCount = 0;
    };

    static bool IsLegacyRuneCraftRequestPayload(
        std::string const& payload)
    {
        return payload == LEGACY_RUNE_CRAFT_GET_COMMAND
            || payload.compare(
                0,
                LEGACY_RUNE_CRAFT_MAKE_PREFIX.size(),
                LEGACY_RUNE_CRAFT_MAKE_PREFIX) == 0
            || payload.compare(
                0,
                LEGACY_RUNE_CRAFT_ITEM_GET_PREFIX.size(),
                LEGACY_RUNE_CRAFT_ITEM_GET_PREFIX) == 0;
    }

    static bool GetLegacyRuneCraftRequestPayload(
        std::string const& message,
        std::string& payload)
    {
        payload.clear();

        if (IsLegacyRuneCraftRequestPayload(message))
        {
            payload = message;
            return true;
        }

        if (message.compare(
                0,
                LEGACY_RUNE_CRAFT_PREFIX.size(),
                LEGACY_RUNE_CRAFT_PREFIX) != 0)
        {
            return false;
        }

        if (message.size() <= LEGACY_RUNE_CRAFT_PREFIX.size())
            return false;

        char separator =
            message[LEGACY_RUNE_CRAFT_PREFIX.size()];

        if (separator != '\t'
            && separator != ' '
            && separator != ':')
        {
            return false;
        }

        std::string innerPayload =
            message.substr(
                LEGACY_RUNE_CRAFT_PREFIX.size() + 1);

        if (!IsLegacyRuneCraftRequestPayload(innerPayload))
            return false;

        payload = innerPayload;
        return true;
    }

    static std::string CleanLegacyRuneCraftAddonText(
        std::string text)
    {
        if (text.size() > 100)
            text.resize(100);

        for (char& ch : text)
        {
            if (ch == ':'
                || ch == '\t'
                || ch == '\r'
                || ch == '\n')
            {
                ch = ' ';
            }
        }

        return text;
    }

    static void SendLegacyRuneCraftAddonMessage(
        Player* player,
        std::string const& payload)
    {
        if (!player || payload.empty())
            return;

        player->WhisperAddon(
            LEGACY_RUNE_CRAFT_PREFIX
                + "\t"
                + payload,
            player);
    }

    static bool SendLegacyRuneCraftItemCache(
        Player* player,
        uint32 itemEntry)
    {
        if (!player
            || !player->GetSession()
            || !itemEntry)
        {
            return false;
        }

        ItemTemplate const* itemTemplate =
            sObjectMgr->GetItemTemplate(itemEntry);

        if (!itemTemplate)
            return false;

        WorldPacket itemQueryData =
            itemTemplate->BuildQueryData(
                player->GetSession()->GetSessionDbLocaleIndex());

        player->GetSession()->SendPacket(
            &itemQueryData);

        return true;
    }

    static void SendLegacyRuneCraftFail(
        Player* player,
        std::string const& reason)
    {
        SendLegacyRuneCraftAddonMessage(
            player,
            "CRAFT:FAIL:"
                + CleanLegacyRuneCraftAddonText(reason));
    }

    static bool ParseLegacyRuneCraftRecipeType(
        std::string const& payload,
        uint32& recipeType)
    {
        recipeType = 0;

        if (payload.compare(
                0,
                LEGACY_RUNE_CRAFT_MAKE_PREFIX.size(),
                LEGACY_RUNE_CRAFT_MAKE_PREFIX) != 0)
        {
            return false;
        }

        std::string recipeTypeText =
            payload.substr(
                LEGACY_RUNE_CRAFT_MAKE_PREFIX.size());

        if (recipeTypeText.empty())
            return false;

        char* end = nullptr;

        unsigned long parsed =
            std::strtoul(
                recipeTypeText.c_str(),
                &end,
                10);

        if (!end
            || *end != '\0'
            || parsed == 0
            || parsed > std::numeric_limits<uint32>::max())
        {
            return false;
        }

        recipeType = uint32(parsed);
        return true;
    }

    static bool ParseLegacyRuneCraftItemEntry(
        std::string const& payload,
        uint32& itemEntry)
    {
        itemEntry = 0;

        if (payload.compare(
                0,
                LEGACY_RUNE_CRAFT_ITEM_GET_PREFIX.size(),
                LEGACY_RUNE_CRAFT_ITEM_GET_PREFIX) != 0)
        {
            return false;
        }

        std::string itemEntryText =
            payload.substr(
                LEGACY_RUNE_CRAFT_ITEM_GET_PREFIX.size());

        if (itemEntryText.empty())
            return false;

        char* end = nullptr;

        unsigned long parsed =
            std::strtoul(
                itemEntryText.c_str(),
                &end,
                10);

        if (!end
            || *end != '\0'
            || parsed == 0
            || parsed > std::numeric_limits<uint32>::max())
        {
            return false;
        }

        itemEntry = uint32(parsed);
        return true;
    }

    static bool GetLegacyRuneCraftRecipeState(
        Player* player,
        uint32 recipeType,
        uint32& cooldownRemaining)
    {
        cooldownRemaining = 0;

        if (!player || !recipeType)
            return false;

        QueryResult result =
            CharacterDatabase.PQuery(
                "SELECT "
                "CASE "
                "WHEN last_craft_at IS NULL THEN 0 "
                "ELSE GREATEST(0, {} - "
                "TIMESTAMPDIFF(SECOND, last_craft_at, CURRENT_TIMESTAMP)) "
                "END AS cooldown_remaining "
                "FROM custom_character_legacy_rune_recipe "
                "WHERE guid = {} "
                "AND recipe_type = {} "
                "LIMIT 1",
                LEGACY_RUNE_CRAFT_COOLDOWN_SECONDS,
                player->GetGUID().GetCounter(),
                recipeType);

        if (!result)
            return false;

        cooldownRemaining =
            result->Fetch()[0].GetUInt32();

        return true;
    }

    static std::string FormatLegacyRuneCraftCooldown(
        uint32 cooldownRemaining)
    {
        uint32 hours =
            cooldownRemaining / 3600;

        uint32 minutes =
            (cooldownRemaining % 3600) / 60;

        uint32 seconds =
            cooldownRemaining % 60;

        return std::to_string(hours)
            + "시간 "
            + std::to_string(minutes)
            + "분 "
            + std::to_string(seconds)
            + "초";
    }

    static bool LoadLegacyRuneCraftRecipe(
        uint32 recipeType,
        LegacyRuneCraftRecipe& recipe)
    {
        recipe = LegacyRuneCraftRecipe();

        QueryResult result =
            WorldDatabase.PQuery(
                "SELECT recipe_type, recipe_name, "
                "result_item, result_count "
                "FROM custom_legacy_rune_craft_recipe "
                "WHERE recipe_type = {} "
                "AND enabled = 1 "
                "LIMIT 1",
                recipeType);

        if (!result)
            return false;

        Field* fields = result->Fetch();

        recipe.RecipeType =
            fields[0].GetUInt32();

        recipe.RecipeName =
            fields[1].GetString();

        recipe.ResultItem =
            fields[2].GetUInt32();

        recipe.ResultCount =
            fields[3].GetUInt32();

        return recipe.RecipeType != 0
            && recipe.ResultItem != 0
            && recipe.ResultCount != 0;
    }

    static bool LoadLegacyRuneCraftMaterials(
        uint32 recipeType,
        std::vector<LegacyRuneCraftMaterial>& materials)
    {
        materials.clear();

        QueryResult result =
            WorldDatabase.PQuery(
                "SELECT material_slot, item_entry, item_count "
                "FROM custom_legacy_rune_craft_material "
                "WHERE recipe_type = {} "
                "ORDER BY material_slot ASC",
                recipeType);

        if (!result)
            return false;

        do
        {
            Field* fields = result->Fetch();

            LegacyRuneCraftMaterial material;

            material.MaterialSlot =
                fields[0].GetUInt8();

            material.ItemEntry =
                fields[1].GetUInt32();

            material.ItemCount =
                fields[2].GetUInt32();

            if (!material.MaterialSlot
                || !material.ItemEntry
                || !material.ItemCount)
            {
                return false;
            }

            materials.push_back(material);
        }
        while (result->NextRow());

        return !materials.empty();
    }

    static void SendLegacyRuneCraftState(
        Player* player)
    {
        if (!player)
            return;

        std::unordered_map<uint32, uint32> ownedRecipeCooldowns;

        QueryResult ownedResult =
            CharacterDatabase.PQuery(
                "SELECT recipe_type, "
                "CASE "
                "WHEN last_craft_at IS NULL THEN 0 "
                "ELSE GREATEST(0, {} - "
                "TIMESTAMPDIFF(SECOND, last_craft_at, CURRENT_TIMESTAMP)) "
                "END AS cooldown_remaining "
                "FROM custom_character_legacy_rune_recipe "
                "WHERE guid = {} "
                "ORDER BY recipe_type ASC",
                LEGACY_RUNE_CRAFT_COOLDOWN_SECONDS,
                player->GetGUID().GetCounter());

        if (ownedResult)
        {
            do
            {
                Field* fields =
                    ownedResult->Fetch();

                uint32 recipeType =
                    fields[0].GetUInt32();

                uint32 cooldownRemaining =
                    fields[1].GetUInt32();

                ownedRecipeCooldowns[recipeType] =
                    cooldownRemaining;
            }
            while (ownedResult->NextRow());
        }

        SendLegacyRuneCraftAddonMessage(
            player,
            "CLEAR");

        QueryResult recipeResult =
            WorldDatabase.Query(
                "SELECT recipe_type, recipe_name, "
                "result_item, result_count "
                "FROM custom_legacy_rune_craft_recipe "
                "WHERE enabled = 1 "
                "ORDER BY sort_order ASC, recipe_type ASC");

        if (recipeResult)
        {
            do
            {
                Field* fields =
                    recipeResult->Fetch();

                uint32 recipeType =
                    fields[0].GetUInt32();

                std::string recipeName =
                    CleanLegacyRuneCraftAddonText(
                        fields[1].GetString());

                uint32 resultItem =
                    fields[2].GetUInt32();

                uint32 resultCount =
                    fields[3].GetUInt32();

                SendLegacyRuneCraftItemCache(
                    player,
                    resultItem);

                auto ownedItr =
                    ownedRecipeCooldowns.find(recipeType);

                uint32 owned =
                    ownedItr != ownedRecipeCooldowns.end()
                    ? 1
                    : 0;

                uint32 cooldownRemaining =
                    ownedItr != ownedRecipeCooldowns.end()
                    ? ownedItr->second
                    : 0;

                SendLegacyRuneCraftAddonMessage(
                    player,
                    "RECIPE:"
                        + std::to_string(recipeType)
                        + ":"
                        + std::to_string(owned)
                        + ":"
                        + std::to_string(cooldownRemaining)
                        + ":"
                        + std::to_string(resultItem)
                        + ":"
                        + std::to_string(resultCount)
                        + ":"
                        + recipeName);
            }
            while (recipeResult->NextRow());
        }

        QueryResult materialResult =
            WorldDatabase.Query(
                "SELECT m.recipe_type, "
                "m.material_slot, "
                "m.item_entry, "
                "m.item_count "
                "FROM custom_legacy_rune_craft_material m "
                "INNER JOIN custom_legacy_rune_craft_recipe r "
                "ON r.recipe_type = m.recipe_type "
                "WHERE r.enabled = 1 "
                "ORDER BY r.sort_order ASC, "
                "m.recipe_type ASC, "
                "m.material_slot ASC");

        if (materialResult)
        {
            do
            {
                Field* fields =
                    materialResult->Fetch();

                uint32 recipeType =
                    fields[0].GetUInt32();

                uint8 materialSlot =
                    fields[1].GetUInt8();

                uint32 itemEntry =
                    fields[2].GetUInt32();

                uint32 itemCount =
                    fields[3].GetUInt32();

                SendLegacyRuneCraftItemCache(
                    player,
                    itemEntry);

                SendLegacyRuneCraftAddonMessage(
                    player,
                    "MATERIAL:"
                        + std::to_string(recipeType)
                        + ":"
                        + std::to_string(materialSlot)
                        + ":"
                        + std::to_string(itemEntry)
                        + ":"
                        + std::to_string(itemCount));
            }
            while (materialResult->NextRow());
        }

        SendLegacyRuneCraftAddonMessage(
            player,
            "END");
    }

    static bool HandleLegacyRuneCraftMake(
        Player* player,
        uint32 recipeType)
    {
        if (!player)
            return false;

        uint32 cooldownRemaining = 0;

        if (!GetLegacyRuneCraftRecipeState(
                player,
                recipeType,
                cooldownRemaining))
        {
            SendLegacyRuneCraftFail(
                player,
                "습득하지 않은 전승룬 레시피입니다.");
            return true;
        }

        if (cooldownRemaining > 0)
        {
            SendLegacyRuneCraftFail(
                player,
                "제작 대기시간이 남아 있습니다. "
                    + FormatLegacyRuneCraftCooldown(
                        cooldownRemaining));

            SendLegacyRuneCraftState(player);
            return true;
        }

        LegacyRuneCraftRecipe recipe;

        if (!LoadLegacyRuneCraftRecipe(
                recipeType,
                recipe))
        {
            SendLegacyRuneCraftFail(
                player,
                "사용할 수 없는 전승룬 레시피입니다.");
            return true;
        }

        if (!sObjectMgr->GetItemTemplate(
                recipe.ResultItem))
        {
            SendLegacyRuneCraftFail(
                player,
                "제작 결과 아이템 정보가 없습니다.");
            return true;
        }

        std::vector<LegacyRuneCraftMaterial> materials;

        if (!LoadLegacyRuneCraftMaterials(
                recipeType,
                materials))
        {
            SendLegacyRuneCraftFail(
                player,
                "레시피 재료 정보가 없습니다.");
            return true;
        }

        std::unordered_map<uint32, uint64> requiredItems;

        for (LegacyRuneCraftMaterial const& material : materials)
        {
            if (!sObjectMgr->GetItemTemplate(
                    material.ItemEntry))
            {
                SendLegacyRuneCraftFail(
                    player,
                    "등록되지 않은 재료 아이템이 포함되어 있습니다.");
                return true;
            }

            requiredItems[material.ItemEntry] +=
                material.ItemCount;
        }

        for (auto const& requiredItem : requiredItems)
        {
            if (requiredItem.second
                > std::numeric_limits<uint32>::max())
            {
                SendLegacyRuneCraftFail(
                    player,
                    "재료 수량이 허용 범위를 초과했습니다.");
                return true;
            }

            uint32 requiredCount =
                uint32(requiredItem.second);

            if (!player->HasItemCount(
                    requiredItem.first,
                    requiredCount,
                    false))
            {
                SendLegacyRuneCraftFail(
                    player,
                    "제작에 필요한 재료가 부족합니다.");
                return true;
            }
        }

        ItemPosCountVec destination;

        InventoryResult storeResult =
            player->CanStoreNewItem(
                NULL_BAG,
                NULL_SLOT,
                destination,
                recipe.ResultItem,
                recipe.ResultCount);

        if (storeResult != EQUIP_ERR_OK)
        {
            SendLegacyRuneCraftFail(
                player,
                "가방에 제작 결과를 받을 공간이 없습니다.");
            return true;
        }

        for (auto const& requiredItem : requiredItems)
        {
            player->DestroyItemCount(
                requiredItem.first,
                uint32(requiredItem.second),
                true);
        }

        Item* resultItem =
            player->StoreNewItem(
                destination,
                recipe.ResultItem,
                true);

        if (!resultItem)
        {
            for (auto const& requiredItem : requiredItems)
            {
                player->AddItem(
                    requiredItem.first,
                    uint32(requiredItem.second));
            }

            SendLegacyRuneCraftFail(
                player,
                "제작 결과 아이템 지급에 실패하여 재료를 반환했습니다.");
            return true;
        }

        player->SendNewItem(
            resultItem,
            recipe.ResultCount,
            true,
            false);

        CharacterDatabase.PExecute(
            "UPDATE custom_character_legacy_rune_recipe "
            "SET last_craft_at = CURRENT_TIMESTAMP "
            "WHERE guid = {} "
            "AND recipe_type = {}",
            player->GetGUID().GetCounter(),
            recipe.RecipeType);

        SendLegacyRuneCraftAddonMessage(
            player,
            "CRAFT:OK:"
                + std::to_string(recipe.RecipeType)
                + ":"
                + std::to_string(recipe.ResultItem)
                + ":"
                + std::to_string(recipe.ResultCount)
                + ":"
                + std::to_string(
                    LEGACY_RUNE_CRAFT_COOLDOWN_SECONDS));

        std::string notification = Trinity::StringFormat(
            "전승룬 제작: {} 제작이 완료되었습니다. "
            "다음 제작은 12시간 후 가능합니다.",
            recipe.RecipeName);

        player->GetSession()->SendNotification(
			"%s",
			notification.c_str()
		);

        return true;
    }

    static bool HandleLegacyRuneExecutionRecipeUse(
        Player* player,
        Item* item)
    {
        if (!player
            || !player->GetSession()
            || !item)
        {
            return false;
        }

        if (item->GetEntry()
            != LEGACY_RUNE_EXECUTION_RECIPE_ITEM_ID)
        {
            return false;
        }

        QueryResult recipeResult =
            WorldDatabase.PQuery(
                "SELECT recipe_type, recipe_name "
                "FROM custom_legacy_rune_craft_recipe "
                "WHERE recipe_type BETWEEN {} AND {} "
                "AND enabled = 1 "
                "ORDER BY RAND() "
                "LIMIT 1",
                LEGACY_RUNE_EXECUTION_RECIPE_MIN_TYPE,
                LEGACY_RUNE_EXECUTION_RECIPE_MAX_TYPE);

        if (!recipeResult)
        {
            player->GetSession()->SendNotification(
                "처형의 레시피: 사용 가능한 제작 레시피가 없습니다.");
            return true;
        }

        Field* fields =
            recipeResult->Fetch();

        uint32 recipeType =
            fields[0].GetUInt32();

        std::string recipeName =
            fields[1].GetString();

        uint32 cooldownRemaining = 0;

        bool alreadyOwned =
            GetLegacyRuneCraftRecipeState(
                player,
                recipeType,
                cooldownRemaining);

        if (!alreadyOwned)
        {
            CharacterDatabase.PExecute(
                "INSERT IGNORE INTO "
                "custom_character_legacy_rune_recipe "
                "(guid, recipe_type, learned_at, last_craft_at) "
                "VALUES ({}, {}, CURRENT_TIMESTAMP, NULL)",
                player->GetGUID().GetCounter(),
                recipeType);
        }

        uint32 consumeCount = 1;

        player->DestroyItemCount(
            item,
            consumeCount,
            true);

        SendLegacyRuneCraftState(player);

        std::string notification;

        if (alreadyOwned)
        {
            notification =
                Trinity::StringFormat(
                    "처형의 레시피: {} 레시피가 나왔지만 "
                    "이미 습득한 레시피입니다.",
                    recipeName);
        }
        else
        {
            notification =
                Trinity::StringFormat(
                    "처형의 레시피: {} 레시피를 습득했습니다.",
                    recipeName);
        }

        ChatHandler(
            player->GetSession()).SendSysMessage(
                notification.c_str());

        return true;
    }
}

class item_legacy_rune_execution_recipe : public ItemScript
{
public:
    item_legacy_rune_execution_recipe()
        : ItemScript(
            "item_legacy_rune_execution_recipe")
    {
    }

    bool OnUse(
        Player* player,
        Item* item,
        SpellCastTargets const& /*targets*/) override
    {
        return HandleLegacyRuneExecutionRecipeUse(
            player,
            item);
    }
};

class custom_legacy_rune_craft : public ServerScript
{
public:
    custom_legacy_rune_craft()
        : ServerScript(
            "custom_legacy_rune_craft")
    {
    }

    void OnPacketReceive(
        WorldSession* session,
        WorldPacket& packet) override
    {
        if (!session
            || packet.GetOpcode() != CMSG_MESSAGECHAT)
        {
            return;
        }

        Player* player = session->GetPlayer();
        if (!player)
            return;

        WorldPacket data(packet);
        data.rpos(0);

        uint32 chatType = 0;
        uint32 language = 0;

        data >> chatType;
        data >> language;

        if (language != LANG_ADDON)
            return;

        if (chatType != CHAT_MSG_WHISPER)
            return;

        std::string target;
        data >> target;

        if (!target.empty()
            && target != player->GetName())
        {
            return;
        }

        std::string message;
        data >> message;

        std::string payload;

        if (!GetLegacyRuneCraftRequestPayload(
                message,
                payload))
        {
            return;
        }

        if (payload == LEGACY_RUNE_CRAFT_GET_COMMAND)
        {
            SendLegacyRuneCraftState(player);
            return;
        }

        if (payload.compare(
                0,
                LEGACY_RUNE_CRAFT_ITEM_GET_PREFIX.size(),
                LEGACY_RUNE_CRAFT_ITEM_GET_PREFIX) == 0)
        {
            uint32 itemEntry = 0;

            if (!ParseLegacyRuneCraftItemEntry(
                    payload,
                    itemEntry))
            {
                return;
            }

            SendLegacyRuneCraftItemCache(
                player,
                itemEntry);

            return;
        }

        uint32 recipeType = 0;

        if (!ParseLegacyRuneCraftRecipeType(
                payload,
                recipeType))
        {
            SendLegacyRuneCraftFail(
                player,
                "제작할 레시피 타입이 올바르지 않습니다.");
            return;
        }

        HandleLegacyRuneCraftMake(
            player,
            recipeType);
    }
};

















//보석결합 배우기
/*
 * Custom Update - 2026-08-01
 *
 * 캐릭터 접속 시 보석 결합 주문 자동 습득
 * - 보석 결합 주문 900200을 배우지 않은 경우 영구 습득
 */
class custom_gem_combine_login : public PlayerScript
{
public:
    custom_gem_combine_login()
        : PlayerScript("custom_gem_combine_login")
    {
    }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player)
            return;

        static constexpr uint32 SPELL_GEM_COMBINE = 900200;

        if (!player->HasSpell(SPELL_GEM_COMBINE))
            player->LearnSpell(SPELL_GEM_COMBINE, false);
    }
};

/*
 * Custom Burning Adrenaline
 *
 * Spell ID : 900212
 *
 * 보조 Spell ID : 900214
 * - Burning Adrenaline - No Threat
 * - 불타는 아드레날린 유지 중 Threat -100%
 *
 * 효과:
 * - 공격력 50% 증가               : DBC
 * - 모든 주문 즉시 시전           : DBC
 * - 12초 지속                     : DBC
 * - 매초 현재 생명력의 5% 피해    : C++
 * - 12초 동안 신규 Threat -100%   : 900214
 * - 12초 정상 종료 시
 *      본인에게 5000 화염 피해
 *      10야드 내 아군 플레이어에게 15000 화염 피해
 *
 * 쿨다운:
 * - 5분
 * - character_spell_cooldown 직접 저장
 *
 * 주의:
 * - 기존 어그로는 유지
 * - 불타는 아드레날린 동안 신규 Threat만 -100%
 * - 900212 제거 시 900214도 즉시 제거
 * - 디스펠/죽음/수동 제거 시 종료 폭발 없음
 * - 정상적으로 지속시간이 끝났을 때만 종료 폭발
 */

#include "ScriptMgr.h"
#include "SpellScript.h"
#include "SpellAuraEffects.h"
#include "Player.h"
#include "Map.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"
#include "StringFormat.h"
#include "SpellHistory.h"
#include "Duration.h"
#include "WorldPacket.h"
#include "Opcodes.h"

#include <ctime>
#include <string>

namespace
{
    enum CustomBurningAdrenaline
    {
        SPELL_CUSTOM_BURNING_ADRENALINE            = 900212,
        SPELL_CUSTOM_BURNING_ADRENALINE_NO_THREAT  = 900214,

        BURNING_ADRENALINE_TICK_PCT                 = 5,

        // 종료 시 본인 피해
        BURNING_ADRENALINE_END_DAMAGE_ME            = 15000,

        // 종료 시 주변 아군 피해
        BURNING_ADRENALINE_END_DAMAGE               = 15000,

        // 쿨다운 5분
        BURNING_ADRENALINE_COOLDOWN_SECONDS         = 5 * 60
    };

    float constexpr BURNING_ADRENALINE_END_RANGE = 10.0f;

    // ---------------------------------------------------------
    // 남은 쿨다운 표시 문자열
    // ---------------------------------------------------------
    std::string FormatBurningAdrenalineCooldown(uint32 seconds)
    {
        uint32 minutes = seconds / 60;
        uint32 remainSeconds = seconds % 60;

        if (minutes > 0 && remainSeconds > 0)
        {
            return Trinity::StringFormat(
                "{}분 {}초",
                minutes,
                remainSeconds
            );
        }

        if (minutes > 0)
        {
            return Trinity::StringFormat(
                "{}분",
                minutes
            );
        }

        return Trinity::StringFormat(
            "{}초",
            remainSeconds
        );
    }

    // ---------------------------------------------------------
    // character_spell_cooldown에서 남은 시간 확인
    // ---------------------------------------------------------
    uint32 GetBurningAdrenalineCooldownRemaining(Player* player)
    {
        if (!player)
            return 0;

        uint32 now = uint32(::time(nullptr));
        uint64 guid = player->GetGUID().GetCounter();

        std::string query = Trinity::StringFormat(
            "SELECT `time` "
            "FROM `character_spell_cooldown` "
            "WHERE `guid` = {} "
            "AND `spell` = {} "
            "AND `item` = 0",
            guid,
            uint32(SPELL_CUSTOM_BURNING_ADRENALINE)
        );

        QueryResult result =
            CharacterDatabase.Query(query.c_str());

        if (!result)
            return 0;

        Field* fields =
            result->Fetch();

        uint32 cooldownUntil =
            fields[0].GetUInt32();

        // -------------------------------------------------
        // 이미 만료된 DB 데이터라면 제거
        // -------------------------------------------------
        if (cooldownUntil <= now)
        {
            std::string deleteQuery =
                Trinity::StringFormat(
                    "DELETE FROM `character_spell_cooldown` "
                    "WHERE `guid` = {} "
                    "AND `spell` = {} "
                    "AND `item` = 0",
                    guid,
                    uint32(SPELL_CUSTOM_BURNING_ADRENALINE)
                );

            CharacterDatabase.DirectExecute(
                deleteQuery.c_str()
            );

            return 0;
        }

        return cooldownUntil - now;
    }

    // ---------------------------------------------------------
    // 클라이언트 쿨다운 표시
    // ---------------------------------------------------------
    void SendBurningAdrenalineCooldownPacket(
        Player* player,
        uint32 cooldownSeconds)
    {
        if (!player)
            return;

        if (cooldownSeconds == 0)
            return;

        WorldPacket data(
            SMSG_SPELL_COOLDOWN,
            8 + 1 + 4 + 4
        );

        data << uint64(
            player->GetGUID().GetRawValue()
        );

        data << uint8(0);

        data << uint32(
            SPELL_CUSTOM_BURNING_ADRENALINE
        );

        data << uint32(
            cooldownSeconds * 1000
        );

        player->SendDirectMessage(
            &data
        );
    }

    // ---------------------------------------------------------
    // 쿨다운 적용
    //
    // 1. 서버 메모리
    // 2. 클라이언트 표시
    // 3. character_spell_cooldown DB 저장
    // ---------------------------------------------------------
    void ApplyBurningAdrenalineCooldown(Player* player)
    {
        if (!player)
            return;

        uint32 now =
            uint32(::time(nullptr));

        uint32 cooldownUntil =
            now +
            uint32(BURNING_ADRENALINE_COOLDOWN_SECONDS);

        uint64 guid =
            player->GetGUID().GetCounter();

        // -------------------------------------------------
        // 서버 메모리 쿨다운
        // -------------------------------------------------
        player->GetSpellHistory()->AddCooldown(
            uint32(SPELL_CUSTOM_BURNING_ADRENALINE),
            0,
            Seconds(
                uint32(BURNING_ADRENALINE_COOLDOWN_SECONDS)
            )
        );

        // -------------------------------------------------
        // 클라이언트 쿨다운 표시
        // -------------------------------------------------
        SendBurningAdrenalineCooldownPacket(
            player,
            uint32(BURNING_ADRENALINE_COOLDOWN_SECONDS)
        );

        // -------------------------------------------------
        // DB 직접 저장
        // -------------------------------------------------
        std::string query =
            Trinity::StringFormat(
                "REPLACE INTO `character_spell_cooldown` "
                "(`guid`, `spell`, `item`, `time`, `categoryId`, `categoryEnd`) "
                "VALUES "
                "({}, {}, 0, {}, 0, 0)",
                guid,
                uint32(SPELL_CUSTOM_BURNING_ADRENALINE),
                cooldownUntil
            );

        CharacterDatabase.DirectExecute(
            query.c_str()
        );
    }
}

// ---------------------------------------------------------
// 900212 - 불타는 아드레날린
// ---------------------------------------------------------
class spell_custom_burning_adrenaline
    : public SpellScriptLoader
{
public:
    spell_custom_burning_adrenaline()
        : SpellScriptLoader(
            "spell_custom_burning_adrenaline"
        )
    {
    }

    // =========================================================
    // SpellScript
    //
    // - 시전 전 DB 쿨다운 확인
    // - 정상 시전 후 5분 쿨다운 저장
    // =========================================================
    class spell_custom_burning_adrenaline_SpellScript
        : public SpellScript
    {
        PrepareSpellScript(
            spell_custom_burning_adrenaline_SpellScript
        );

        // -------------------------------------------------
        // 시전 가능 여부 확인
        // -------------------------------------------------
        SpellCastResult CheckCast()
        {
            Player* player =
                GetCaster()
                    ? GetCaster()->ToPlayer()
                    : nullptr;

            if (!player)
                return SPELL_FAILED_DONT_REPORT;

            uint32 remaining =
                GetBurningAdrenalineCooldownRemaining(
                    player
                );

            if (remaining == 0)
                return SPELL_CAST_OK;

            if (player->GetSession())
            {
                std::string message =
                    Trinity::StringFormat(
                        "불타는 아드레날린 재사용 대기시간이 남아 있습니다. 남은 시간: {}",
                        FormatBurningAdrenalineCooldown(
                            remaining
                        )
                    );

                ChatHandler(player->GetSession()).SendSysMessage(
					message.c_str()
				);
            }

            return SPELL_FAILED_DONT_REPORT;
        }

        // -------------------------------------------------
        // 정상 시전 후 5분 쿨다운 적용/저장
        // -------------------------------------------------
        void HandleAfterCast()
        {
            Player* player =
                GetCaster()
                    ? GetCaster()->ToPlayer()
                    : nullptr;

            if (!player)
                return;

            ApplyBurningAdrenalineCooldown(
                player
            );
        }

        void Register() override
        {
            OnCheckCast += SpellCheckCastFn(
                spell_custom_burning_adrenaline_SpellScript::CheckCast
            );

            AfterCast += SpellCastFn(
                spell_custom_burning_adrenaline_SpellScript::HandleAfterCast
            );
        }
    };

    // =========================================================
    // AuraScript
    // =========================================================
    class spell_custom_burning_adrenaline_AuraScript
        : public AuraScript
    {
        PrepareAuraScript(
            spell_custom_burning_adrenaline_AuraScript
        );

        // -------------------------------------------------
        // 900212 적용
        //
        // 900214 Threat -100% 보조 Aura 적용
        // -------------------------------------------------
        void HandleApply(
            AuraEffect const* /*aurEff*/,
            AuraEffectHandleModes /*mode*/)
        {
            Player* player =
                GetTarget()->ToPlayer();

            if (!player)
                return;

            if (!player->IsAlive())
                return;

            // 혹시 기존 900214가 남아 있다면 제거
            player->RemoveAurasDueToSpell(
                uint32(
                    SPELL_CUSTOM_BURNING_ADRENALINE_NO_THREAT
                )
            );

            // -------------------------------------------------
            // Threat -100% 보조 Aura
            // -------------------------------------------------
            player->CastSpell(
                player,
                uint32(
                    SPELL_CUSTOM_BURNING_ADRENALINE_NO_THREAT
                ),
                true
            );
        }

        // -------------------------------------------------
        // 매초 현재 생명력의 5% 피해
        //
        // DBC Effect3:
        // SPELL_AURA_PERIODIC_TRIGGER_SPELL
        //
        // 원본 23620은 매초 23619를 발동하지만
        // 기본 발동을 막고 직접 처리한다.
        // -------------------------------------------------
        void HandlePeriodic(
            AuraEffect const* /*aurEff*/)
        {
            PreventDefaultAction();

            Player* player =
                GetTarget()->ToPlayer();

            if (!player)
                return;

            if (!player->IsAlive())
                return;

            // -------------------------------------------------
            // 현재 생명력
            // -------------------------------------------------
            uint32 currentHealth =
                player->GetHealth();

            // -------------------------------------------------
            // 현재 생명력의 5%
            // -------------------------------------------------
            uint32 damage =
                uint32(
                    (
                        uint64(currentHealth) *
                        BURNING_ADRENALINE_TICK_PCT
                    ) / 100
                );

            if (!damage)
                damage = 1;

            Unit::DealDamage(
                player,
                player,
                damage,
                nullptr,
                DIRECT_DAMAGE,
                SPELL_SCHOOL_MASK_FIRE,
                nullptr,
                false
            );
        }

        // -------------------------------------------------
        // 900212 제거
        //
        // 900214는 제거 원인과 관계없이 항상 같이 제거
        //
        // 단, 종료 폭발은 정상 만료일 때만 발생
        // -------------------------------------------------
        void HandleRemove(
            AuraEffect const* /*aurEff*/,
            AuraEffectHandleModes /*mode*/)
        {
            Player* player =
                GetTarget()->ToPlayer();

            if (!player)
                return;

            // -------------------------------------------------
            // Threat -100% 보조 Aura 즉시 제거
            //
            // 정상 종료 / 디스펠 / 죽음 / 강제 제거
            // 모두 처리
            // -------------------------------------------------
            player->RemoveAurasDueToSpell(
                uint32(
                    SPELL_CUSTOM_BURNING_ADRENALINE_NO_THREAT
                )
            );

            AuraApplication const* application =
                GetTargetApplication();

            if (!application)
                return;

            // -------------------------------------------------
            // 정상적으로 12초가 끝났을 때만 폭발
            // -------------------------------------------------
            if (
                application->GetRemoveMode()
                != AURA_REMOVE_BY_EXPIRE
            )
            {
                return;
            }

            if (!player->IsAlive())
                return;

            std::list<Player*> friendlyPlayers;

            Map* map =
                player->GetMap();

            if (map)
            {
                Map::PlayerList const& players =
                    map->GetPlayers();

                if (!players.isEmpty())
                {
                    for (
                        Map::PlayerList::const_iterator itr =
                            players.begin();
                        itr != players.end();
                        ++itr
                    )
                    {
                        Player* target =
                            itr->GetSource();

                        if (!target)
                            continue;

                        if (target == player)
                            continue;

                        if (!target->IsAlive())
                            continue;

                        // -------------------------------------
                        // 10야드
                        // -------------------------------------
                        if (
                            !player->IsWithinDistInMap(
                                target,
                                BURNING_ADRENALINE_END_RANGE
                            )
                        )
                        {
                            continue;
                        }

                        // -------------------------------------
                        // 아군만
                        // -------------------------------------
                        if (
                            !player->IsFriendlyTo(
                                target
                            )
                        )
                        {
                            continue;
                        }

                        friendlyPlayers.push_back(
                            target
                        );
                    }
                }
            }

            // -------------------------------------------------
            // 주변 아군
            // 10야드 / 15000 화염 피해
            // -------------------------------------------------
            for (
                Player* target
                : friendlyPlayers
            )
            {
                if (!target)
                    continue;

                if (!target->IsAlive())
                    continue;

                Unit::DealDamage(
                    player,
                    target,
                    BURNING_ADRENALINE_END_DAMAGE,
                    nullptr,
                    DIRECT_DAMAGE,
                    SPELL_SCHOOL_MASK_FIRE,
                    nullptr,
                    false
                );
            }

            // -------------------------------------------------
            // 본인
            // 5000 화염 피해
            // -------------------------------------------------
            if (player->IsAlive())
            {
                Unit::DealDamage(
                    player,
                    player,
                    BURNING_ADRENALINE_END_DAMAGE_ME,
                    nullptr,
                    DIRECT_DAMAGE,
                    SPELL_SCHOOL_MASK_FIRE,
                    nullptr,
                    false
                );
            }
        }

        void Register() override
        {
            // -------------------------------------------------
            // Effect1 적용 시
            // 900214 Threat -100% 보조 Aura 적용
            // -------------------------------------------------
            AfterEffectApply += AuraEffectApplyFn(
                spell_custom_burning_adrenaline_AuraScript::HandleApply,
                EFFECT_0,
                SPELL_AURA_MOD_DAMAGE_PERCENT_DONE,
                AURA_EFFECT_HANDLE_REAL
            );

            // -------------------------------------------------
            // Effect3
            // 1초 Periodic Trigger
            // -------------------------------------------------
            OnEffectPeriodic += AuraEffectPeriodicFn(
                spell_custom_burning_adrenaline_AuraScript::HandlePeriodic,
                EFFECT_2,
                SPELL_AURA_PERIODIC_TRIGGER_SPELL
            );

            // -------------------------------------------------
            // Effect1 기준 제거 감지
            //
            // 여기서 900214 제거
            // 정상 만료인 경우 종료 폭발까지 처리
            // -------------------------------------------------
            AfterEffectRemove += AuraEffectRemoveFn(
                spell_custom_burning_adrenaline_AuraScript::HandleRemove,
                EFFECT_0,
                SPELL_AURA_MOD_DAMAGE_PERCENT_DONE,
                AURA_EFFECT_HANDLE_REAL
            );
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_custom_burning_adrenaline_SpellScript();
    }

    AuraScript* GetAuraScript() const override
    {
        return new spell_custom_burning_adrenaline_AuraScript();
    }
};

/*
 * Custom Time Rewind
 *
 * Spell ID : 900213
 *
 * 효과:
 * - 사용 순간 현재 생명력 저장
 * - 마나 / 분노 / 기력 / 룬마력 저장
 * - 8초 동안 새로 발생하는 Threat 100% 감소
 * - 8초 후 저장 당시 생명력 / 자원으로 복원
 * - 위치는 되돌리지 않음
 *
 * 쿨다운:
 * - 3분
 * - character_spell_cooldown 직접 저장
 *
 * 주의:
 * - 기존에 이미 가지고 있던 어그로는 유지
 * - 8초 동안 새로 발생하는 어그로만 0
 * - 죽으면 되감기 없음
 * - 디스펠/강제 제거 시 되감기 없음
 * - 정상적으로 8초가 끝났을 때만 HP / 자원 복원
 */

#include "ScriptMgr.h"
#include "SpellScript.h"
#include "SpellAuraEffects.h"
#include "Player.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"
#include "StringFormat.h"
#include "SpellHistory.h"
#include "Duration.h"
#include "WorldPacket.h"
#include "Opcodes.h"

#include <ctime>
#include <string>
#include <algorithm>

namespace
{
    enum CustomTimeRewind
    {
        SPELL_CUSTOM_TIME_REWIND     = 900213,

        // 쿨다운 3분
        TIME_REWIND_COOLDOWN_SECONDS = 3 * 60
    };

    // ---------------------------------------------------------
    // 남은 쿨다운 표시
    // ---------------------------------------------------------
    std::string FormatTimeRewindCooldown(uint32 seconds)
    {
        uint32 minutes = seconds / 60;
        uint32 remainSeconds = seconds % 60;

        if (minutes > 0 && remainSeconds > 0)
        {
            return Trinity::StringFormat(
                "{}분 {}초",
                minutes,
                remainSeconds
            );
        }

        if (minutes > 0)
        {
            return Trinity::StringFormat(
                "{}분",
                minutes
            );
        }

        return Trinity::StringFormat(
            "{}초",
            remainSeconds
        );
    }

    // ---------------------------------------------------------
    // DB에서 남은 쿨다운 확인
    // ---------------------------------------------------------
    uint32 GetTimeRewindCooldownRemaining(Player* player)
    {
        if (!player)
            return 0;

        uint32 now = uint32(::time(nullptr));
        uint64 guid = player->GetGUID().GetCounter();

        std::string query = Trinity::StringFormat(
            "SELECT `time` "
            "FROM `character_spell_cooldown` "
            "WHERE `guid` = {} "
            "AND `spell` = {} "
            "AND `item` = 0",
            guid,
            uint32(SPELL_CUSTOM_TIME_REWIND)
        );

        QueryResult result =
            CharacterDatabase.Query(query.c_str());

        if (!result)
            return 0;

        Field* fields =
            result->Fetch();

        uint32 cooldownUntil =
            fields[0].GetUInt32();

        // 이미 만료된 경우 삭제
        if (cooldownUntil <= now)
        {
            std::string deleteQuery =
                Trinity::StringFormat(
                    "DELETE FROM `character_spell_cooldown` "
                    "WHERE `guid` = {} "
                    "AND `spell` = {} "
                    "AND `item` = 0",
                    guid,
                    uint32(SPELL_CUSTOM_TIME_REWIND)
                );

            CharacterDatabase.DirectExecute(
                deleteQuery.c_str()
            );

            return 0;
        }

        return cooldownUntil - now;
    }

    // ---------------------------------------------------------
    // 클라이언트 쿨다운 표시
    // ---------------------------------------------------------
    void SendTimeRewindCooldownPacket(
        Player* player,
        uint32 cooldownSeconds)
    {
        if (!player)
            return;

        if (cooldownSeconds == 0)
            return;

        WorldPacket data(
            SMSG_SPELL_COOLDOWN,
            8 + 1 + 4 + 4
        );

        data << uint64(
            player->GetGUID().GetRawValue()
        );

        data << uint8(0);

        data << uint32(
            SPELL_CUSTOM_TIME_REWIND
        );

        data << uint32(
            cooldownSeconds * 1000
        );

        player->SendDirectMessage(
            &data
        );
    }

    // ---------------------------------------------------------
    // 쿨다운 적용 및 DB 저장
    // ---------------------------------------------------------
    void ApplyTimeRewindCooldown(Player* player)
    {
        if (!player)
            return;

        uint32 now =
            uint32(::time(nullptr));

        uint32 cooldownUntil =
            now +
            uint32(TIME_REWIND_COOLDOWN_SECONDS);

        uint64 guid =
            player->GetGUID().GetCounter();

        // 서버 메모리
        player->GetSpellHistory()->AddCooldown(
            uint32(SPELL_CUSTOM_TIME_REWIND),
            0,
            Seconds(
                uint32(TIME_REWIND_COOLDOWN_SECONDS)
            )
        );

        // 클라이언트 표시
        SendTimeRewindCooldownPacket(
            player,
            uint32(TIME_REWIND_COOLDOWN_SECONDS)
        );

        // DB 직접 저장
        std::string query =
            Trinity::StringFormat(
                "REPLACE INTO `character_spell_cooldown` "
                "(`guid`, `spell`, `item`, `time`, `categoryId`, `categoryEnd`) "
                "VALUES "
                "({}, {}, 0, {}, 0, 0)",
                guid,
                uint32(SPELL_CUSTOM_TIME_REWIND),
                cooldownUntil
            );

        CharacterDatabase.DirectExecute(
            query.c_str()
        );
    }
}

// ---------------------------------------------------------
// 900213 - 시간 되감기
// ---------------------------------------------------------
class spell_custom_time_rewind
    : public SpellScriptLoader
{
public:
    spell_custom_time_rewind()
        : SpellScriptLoader(
            "spell_custom_time_rewind"
        )
    {
    }

    // =========================================================
    // SpellScript
    // =========================================================
    class spell_custom_time_rewind_SpellScript
        : public SpellScript
    {
        PrepareSpellScript(
            spell_custom_time_rewind_SpellScript
        );

        SpellCastResult CheckCast()
        {
            Player* player =
                GetCaster()
                    ? GetCaster()->ToPlayer()
                    : nullptr;

            if (!player)
                return SPELL_FAILED_DONT_REPORT;

            uint32 remaining =
                GetTimeRewindCooldownRemaining(
                    player
                );

            if (remaining == 0)
                return SPELL_CAST_OK;

            if (player->GetSession())
            {
                std::string message =
                    Trinity::StringFormat(
                        "시간 되감기 재사용 대기시간이 남아 있습니다. 남은 시간: {}",
                        FormatTimeRewindCooldown(
                            remaining
                        )
                    );

                ChatHandler(player->GetSession()).SendSysMessage(
					message.c_str()
				);
            }

            return SPELL_FAILED_DONT_REPORT;
        }

        void HandleAfterCast()
        {
            Player* player =
                GetCaster()
                    ? GetCaster()->ToPlayer()
                    : nullptr;

            if (!player)
                return;

            ApplyTimeRewindCooldown(
                player
            );
        }

        void Register() override
        {
            OnCheckCast += SpellCheckCastFn(
                spell_custom_time_rewind_SpellScript::CheckCast
            );

            AfterCast += SpellCastFn(
                spell_custom_time_rewind_SpellScript::HandleAfterCast
            );
        }
    };

    // =========================================================
    // AuraScript
    //
    // Effect1:
    // SPELL_AURA_DUMMY
    //
    // Effect2:
    // SPELL_AURA_MOD_THREAT
    // DBC에서 -100%
    // =========================================================
    class spell_custom_time_rewind_AuraScript
        : public AuraScript
    {
        PrepareAuraScript(
            spell_custom_time_rewind_AuraScript
        );

    private:

        bool _saved = false;

        uint32 _health = 0;

        uint32 _mana = 0;
        uint32 _rage = 0;
        uint32 _energy = 0;
        uint32 _runicPower = 0;

        // -------------------------------------------------
        // 버프 적용 순간
        //
        // HP / 자원 저장
        // -------------------------------------------------
        void HandleApply(
            AuraEffect const* /*aurEff*/,
            AuraEffectHandleModes /*mode*/)
        {
            Player* player =
                GetTarget()->ToPlayer();

            if (!player)
                return;

            if (!player->IsAlive())
                return;

            // 현재 HP 저장
            _health =
                player->GetHealth();

            // 마나 저장
            _mana =
                player->GetPower(
                    POWER_MANA
                );

            // 분노 저장
            _rage =
                player->GetPower(
                    POWER_RAGE
                );

            // 기력 저장
            _energy =
                player->GetPower(
                    POWER_ENERGY
                );

            // 룬마력 저장
            _runicPower =
                player->GetPower(
                    POWER_RUNIC_POWER
                );

            _saved = true;
        }

        // -------------------------------------------------
        // 8초 정상 종료 시
        //
        // HP / 자원 복원
        //
        // Threat는 따로 건드리지 않는다.
        // Effect2의 SPELL_AURA_MOD_THREAT가
        // 8초 동안 신규 Threat 생성을 막는다.
        // -------------------------------------------------
        void HandleRemove(
            AuraEffect const* /*aurEff*/,
            AuraEffectHandleModes /*mode*/)
        {
            if (!_saved)
                return;

            AuraApplication const* application =
                GetTargetApplication();

            if (!application)
                return;

            // 정상 만료일 때만 복원
            if (
                application->GetRemoveMode()
                != AURA_REMOVE_BY_EXPIRE
            )
            {
                return;
            }

            Player* player =
                GetTarget()->ToPlayer();

            if (!player)
                return;

            if (!player->IsAlive())
                return;

            // -------------------------------------------------
            // HP 복원
            // -------------------------------------------------
            uint32 restoreHealth =
                std::min(
                    _health,
                    player->GetMaxHealth()
                );

            if (!restoreHealth)
                restoreHealth = 1;

            player->SetHealth(
                restoreHealth
            );

            // -------------------------------------------------
            // 마나 복원
            // -------------------------------------------------
            player->SetPower(
                POWER_MANA,
                std::min(
                    _mana,
                    player->GetMaxPower(
                        POWER_MANA
                    )
                )
            );

            // -------------------------------------------------
            // 분노 복원
            // -------------------------------------------------
            player->SetPower(
                POWER_RAGE,
                std::min(
                    _rage,
                    player->GetMaxPower(
                        POWER_RAGE
                    )
                )
            );

            // -------------------------------------------------
            // 기력 복원
            // -------------------------------------------------
            player->SetPower(
                POWER_ENERGY,
                std::min(
                    _energy,
                    player->GetMaxPower(
                        POWER_ENERGY
                    )
                )
            );

            // -------------------------------------------------
            // 룬마력 복원
            // -------------------------------------------------
            player->SetPower(
                POWER_RUNIC_POWER,
                std::min(
                    _runicPower,
                    player->GetMaxPower(
                        POWER_RUNIC_POWER
                    )
                )
            );

            _saved = false;
        }

        void Register() override
        {
            // Effect1 = Dummy
            AfterEffectApply += AuraEffectApplyFn(
                spell_custom_time_rewind_AuraScript::HandleApply,
                EFFECT_0,
                SPELL_AURA_DUMMY,
                AURA_EFFECT_HANDLE_REAL
            );

            AfterEffectRemove += AuraEffectRemoveFn(
                spell_custom_time_rewind_AuraScript::HandleRemove,
                EFFECT_0,
                SPELL_AURA_DUMMY,
                AURA_EFFECT_HANDLE_REAL
            );
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_custom_time_rewind_SpellScript();
    }

    AuraScript* GetAuraScript() const override
    {
        return new spell_custom_time_rewind_AuraScript();
    }
};

/*
 * Custom Temporary Spell Scroll
 *
 * Item 35556
 * - 불타는 아드레날린
 * - Spell 900212
 *
 * Item 24229
 * - 시간의 쇠퇴
 * - Spell 900213
 *
 * 기능:
 * - 두루마리 사용 시 해당 스펠 습득
 * - 사용 시점부터 30일 동안 사용 가능
 * - DB에 learn_time / expire_time 저장
 * - 동일 두루마리 재사용 시 현재 시점부터 30일로 갱신
 * - 로그인 시 남은 사용기간 표시
 * - 만료 시 스펠 자동 삭제
 *
 * DB:
 * character_temporary_spell
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Item.h"
#include "DatabaseEnv.h"
#include "WorldSession.h"
#include "StringFormat.h"

#include <ctime>
#include <string>

namespace
{
    enum CustomTemporarySpellScroll
    {
        // -----------------------------------------------------
        // 두루마리 아이템
        // -----------------------------------------------------
        ITEM_BURNING_ADRENALINE_SCROLL = 35556,
        ITEM_TIME_REWIND_SCROLL        = 24229,

        // -----------------------------------------------------
        // 습득 스펠
        // -----------------------------------------------------
        SPELL_BURNING_ADRENALINE       = 900212,
        SPELL_TIME_REWIND              = 900213,

        // -----------------------------------------------------
        // 30일
        // -----------------------------------------------------
        TEMPORARY_SPELL_DURATION       = 30 * 24 * 60 * 60
    };

    // =========================================================
    // 아이템 Entry -> 습득 Spell
    // =========================================================
    uint32 GetTemporarySpellByItem(uint32 itemEntry)
    {
        switch (itemEntry)
        {
            case ITEM_BURNING_ADRENALINE_SCROLL:
                return uint32(SPELL_BURNING_ADRENALINE);

            case ITEM_TIME_REWIND_SCROLL:
                return uint32(SPELL_TIME_REWIND);

            default:
                return 0;
        }
    }

    // =========================================================
    // 스펠 이름
    // =========================================================
    char const* GetTemporarySpellName(uint32 spellId)
    {
        switch (spellId)
        {
            case SPELL_BURNING_ADRENALINE:
                return "불타는 아드레날린";

            case SPELL_TIME_REWIND:
                return "시간의 쇠퇴";

            default:
                return "기간제 스펠";
        }
    }

    // =========================================================
    // 남은 기간 문자열
    //
    // 예:
    // 29일 18시간 32분
    // =========================================================
    std::string FormatTemporarySpellRemaining(uint32 seconds)
    {
        uint32 days =
            seconds / (24 * 60 * 60);

        seconds %=
            (24 * 60 * 60);

        uint32 hours =
            seconds / (60 * 60);

        seconds %=
            (60 * 60);

        uint32 minutes =
            seconds / 60;

        if (days > 0)
        {
            return Trinity::StringFormat(
                "{}일 {}시간 {}분",
                days,
                hours,
                minutes
            );
        }

        if (hours > 0)
        {
            return Trinity::StringFormat(
                "{}시간 {}분",
                hours,
                minutes
            );
        }

        if (minutes > 0)
        {
            return Trinity::StringFormat(
                "{}분",
                minutes
            );
        }

        return Trinity::StringFormat(
            "{}초",
            seconds
        );
    }

    // =========================================================
    // 기간제 스펠 DB 저장
    //
    // 재사용하면:
    //
    // learn_time  = 현재시간
    // expire_time = 현재시간 + 30일
    //
    // 로 완전히 갱신
    // =========================================================
    void SaveTemporarySpell(
        Player* player,
        uint32 spellId)
    {
        if (!player)
            return;

        uint64 guid =
            player->GetGUID().GetCounter();

        uint32 now =
            uint32(::time(nullptr));

        uint32 expireTime =
            now +
            uint32(TEMPORARY_SPELL_DURATION);

        std::string query =
            Trinity::StringFormat(
                "REPLACE INTO `character_temporary_spell` "
                "(`guid`, `spell`, `learn_time`, `expire_time`) "
                "VALUES ({}, {}, {}, {})",
                guid,
                spellId,
                now,
                expireTime
            );

        CharacterDatabase.DirectExecute(
            query.c_str()
        );
    }

    // =========================================================
    // 기간제 스펠 만료 검사
    //
    // showRemaining = true
    // 로그인 시:
    // 남은 기간도 표시
    //
    // showRemaining = false
    // 캐릭터 저장 시:
    // 만료만 검사하고 메시지는 표시하지 않음
    // =========================================================
    void CheckTemporarySpellExpiration(
        Player* player,
        bool showRemaining)
    {
        if (!player)
            return;

        uint64 guid =
            player->GetGUID().GetCounter();

        uint32 now =
            uint32(::time(nullptr));

        std::string query =
            Trinity::StringFormat(
                "SELECT "
                "`spell`, "
                "`expire_time` "
                "FROM `character_temporary_spell` "
                "WHERE `guid` = {}",
                guid
            );

        QueryResult result =
            CharacterDatabase.Query(
                query.c_str()
            );

        if (!result)
            return;

        do
        {
            Field* fields =
                result->Fetch();

            uint32 spellId =
                fields[0].GetUInt32();

            uint32 expireTime =
                fields[1].GetUInt32();

            // =================================================
            // 아직 기간이 남아 있음
            // =================================================
            if (expireTime > now)
            {
                // ---------------------------------------------
                // DB에는 기록이 있는데
                // 스펠이 없다면 다시 복구
                //
                // 접속 데이터 문제 등 대비
                // ---------------------------------------------
                if (!player->HasSpell(spellId))
                {
                    player->LearnSpell(
                        spellId,
                        false
                    );
                }

                // ---------------------------------------------
                // 로그인 시에만 남은 기간 표시
                // ---------------------------------------------
                if (
                    showRemaining &&
                    player->GetSession()
                )
                {
                    uint32 remaining =
                        expireTime - now;

                    std::string message =
                        Trinity::StringFormat(
                            "[기간제 스펠] {} - 남은 사용기간: {}",
                            GetTemporarySpellName(spellId),
                            FormatTemporarySpellRemaining(
                                remaining
                            )
                        );

                    ChatHandler(player->GetSession()).SendSysMessage(
						message.c_str()
					);
                }

                continue;
            }

            // =================================================
            // 기간 만료
            // =================================================

            // ---------------------------------------------
            // 스펠 제거
            // ---------------------------------------------
            if (player->HasSpell(spellId))
            {
                player->RemoveSpell(
                    spellId,
                    false,
                    false
                );
            }

            // ---------------------------------------------
            // DB 기록 삭제
            // ---------------------------------------------
            std::string deleteQuery =
                Trinity::StringFormat(
                    "DELETE FROM `character_temporary_spell` "
                    "WHERE `guid` = {} "
                    "AND `spell` = {}",
                    guid,
                    spellId
                );

            CharacterDatabase.DirectExecute(
                deleteQuery.c_str()
            );

            // ---------------------------------------------
            // 만료 안내
            // ---------------------------------------------
            if (player->GetSession())
            {
                std::string message =
                    Trinity::StringFormat(
                        "{} 사용 기간이 만료되어 스펠이 삭제되었습니다.",
                        GetTemporarySpellName(spellId)
                    );

                ChatHandler(player->GetSession()).SendSysMessage(
					message.c_str()
				);
            }

        } while (result->NextRow());
    }
}

// =============================================================
// 기간제 스펠 두루마리
//
// 35556
// 불타는 아드레날린 -> 900212
//
// 24229
// 시간의 쇠퇴 -> 900213
// =============================================================
class item_custom_temporary_spell_scroll
    : public ItemScript
{
public:
    item_custom_temporary_spell_scroll()
        : ItemScript(
            "item_custom_temporary_spell_scroll"
        )
    {
    }

    bool OnUse(
        Player* player,
        Item* item,
        SpellCastTargets const& /*targets*/)
        override
    {
        if (!player)
            return true;

        if (!item)
            return true;

        uint32 itemEntry =
            item->GetEntry();

        uint32 spellId =
            GetTemporarySpellByItem(
                itemEntry
            );

        // -----------------------------------------------------
        // 등록되지 않은 아이템
        // -----------------------------------------------------
        if (!spellId)
            return true;

        bool alreadyLearned =
            player->HasSpell(
                spellId
            );

        // =====================================================
        // 스펠 습득
        // =====================================================
        if (!alreadyLearned)
        {
            player->LearnSpell(
                spellId,
                false
            );
        }

        // =====================================================
        // 30일 기간 저장
        //
        // 기존에 배우고 있는 상태여도
        // 현재 시간부터 30일로 다시 갱신
        // =====================================================
        SaveTemporarySpell(
            player,
            spellId
        );

        // =====================================================
        // 두루마리 1개 소비
        // =====================================================
        player->DestroyItemCount(
            itemEntry,
            1,
            true
        );

        // =====================================================
        // 안내
        // =====================================================
        if (player->GetSession())
        {
            std::string message;

            if (alreadyLearned)
            {
                message =
                    Trinity::StringFormat(
                        "{}의 사용 기간이 현재 시점부터 30일로 갱신되었습니다.",
                        GetTemporarySpellName(spellId)
                    );
            }
            else
            {
                message =
                    Trinity::StringFormat(
                        "{}을(를) 습득했습니다. 30일 동안 사용할 수 있습니다.",
                        GetTemporarySpellName(spellId)
                    );
            }

            ChatHandler(player->GetSession()).SendSysMessage(
				message.c_str()
			);
        }

        return true;
    }
};

// =============================================================
// 기간제 스펠 관리
// =============================================================
class player_custom_temporary_spell
    : public PlayerScript
{
public:
    player_custom_temporary_spell()
        : PlayerScript(
            "player_custom_temporary_spell"
        )
    {
    }

    // =========================================================
    // 로그인
    //
    // - 만료 확인
    // - 남은 기간 표시
    // =========================================================
    void OnLogin(
        Player* player,
        bool /*firstLogin*/)
        override
    {
        CheckTemporarySpellExpiration(
            player,
            true
        );
    }

    // =========================================================
    // 캐릭터 저장
    //
    // - 만료 여부만 검사
    // - 남은 기간 메시지는 표시하지 않음
    // =========================================================
    void OnSave(
        Player* player)
        override
    {
        CheckTemporarySpellExpiration(
            player,
            false
        );
    }
};

/*
 * 광폭화 무기 강화 시스템
 *
 * 900216 : 광폭화 무기 강화
 * 3923   : 광폭화 임시 인챈트
 * 900217 : 광폭화 Proc Aura
 * 900215 : 실제 광폭화 버프
 *
 * 동작:
 *
 * 900216 사용
 *   ↓
 * 무기에 3923 적용
 *   ↓
 * 플레이어에게 900217 부여
 *
 * 장착 무기에 3923이 하나라도 존재
 *   → 900217 유지
 *
 * 3923이 더 이상 존재하지 않음
 *   → 900217 제거
 *
 * 인챈트된 무기를 다시 장착
 *   → 900217 자동 복구
 */

#include "ScriptMgr.h"
#include "SpellScript.h"
#include "Player.h"
#include "Item.h"
#include "DBCStores.h"
#include "Chat.h"

namespace
{
    enum CustomBerserkWeaponEnchant
    {
        SPELL_CUSTOM_BERSERK_BUFF          = 900215,
        SPELL_CUSTOM_BERSERK_WEAPON_ENCHANT = 900216,
        SPELL_CUSTOM_BERSERK_PROC_AURA     = 900217,

        ENCHANT_CUSTOM_BERSERK             = 3923,

        // 1시간
        BERSERK_ENCHANT_DURATION           = 60 * 60 * 1000
    };

    // ---------------------------------------------------------
    // 모든 무기 허용
    // ---------------------------------------------------------
    static bool IsCustomBerserkWeapon(Item const* item)
    {
        if (!item)
            return false;

        ItemTemplate const* proto =
            item->GetTemplate();

        if (!proto)
            return false;

        return proto->Class == ITEM_CLASS_WEAPON;
    }

    // ---------------------------------------------------------
    // 특정 아이템에 3923이 있는지 확인
    // ---------------------------------------------------------
    static bool HasCustomBerserkEnchant(Item const* item)
    {
        if (!item)
            return false;

        return
            item->GetEnchantmentId(
                TEMP_ENCHANTMENT_SLOT
            ) == ENCHANT_CUSTOM_BERSERK;
    }

    // ---------------------------------------------------------
    // 현재 장착 중인 무기 중
    // 3923 인챈트가 하나라도 존재하는지 확인
    // ---------------------------------------------------------
    static bool HasEquippedCustomBerserkEnchant(Player* player)
    {
        if (!player)
            return false;

        // 주무기
        Item* mainHand =
            player->GetItemByPos(
                INVENTORY_SLOT_BAG_0,
                EQUIPMENT_SLOT_MAINHAND
            );

        if (HasCustomBerserkEnchant(mainHand))
            return true;

        // 보조무기
        Item* offHand =
            player->GetItemByPos(
                INVENTORY_SLOT_BAG_0,
                EQUIPMENT_SLOT_OFFHAND
            );

        if (HasCustomBerserkEnchant(offHand))
            return true;

        // 원거리 무기
        Item* ranged =
            player->GetItemByPos(
                INVENTORY_SLOT_BAG_0,
                EQUIPMENT_SLOT_RANGED
            );

        if (HasCustomBerserkEnchant(ranged))
            return true;

        return false;
    }

    // ---------------------------------------------------------
    // 900217 상태 동기화
    // ---------------------------------------------------------
    static void UpdateCustomBerserkProcAura(Player* player)
    {
        if (!player)
            return;

        bool const hasEnchant =
            HasEquippedCustomBerserkEnchant(player);

        bool const hasAura =
            player->HasAura(
                SPELL_CUSTOM_BERSERK_PROC_AURA
            );

        // -----------------------------------------------------
        // 광폭화 인챈트 있음
        // 900217 없음
        //
        // → 900217 부여
        // -----------------------------------------------------
        if (hasEnchant && !hasAura)
        {
            player->CastSpell(
                player,
                SPELL_CUSTOM_BERSERK_PROC_AURA,
                true
            );

            return;
        }

        // -----------------------------------------------------
        // 광폭화 인챈트 없음
        // 900217 있음
        //
        // → 900217 제거
        // -----------------------------------------------------
        if (!hasEnchant && hasAura)
        {
            player->RemoveAurasDueToSpell(
                SPELL_CUSTOM_BERSERK_PROC_AURA
            );
        }
    }

    // ---------------------------------------------------------
    // 실제 3923 적용
    // ---------------------------------------------------------
    static bool ApplyCustomBerserkEnchant(
        Player* player,
        Item* item)
    {
        if (!player || !item)
            return false;

        if (!IsCustomBerserkWeapon(item))
            return false;

        // 서버 DBC에서 3923 존재 확인
        if (!sSpellItemEnchantmentStore.LookupEntry(
                ENCHANT_CUSTOM_BERSERK))
        {
            return false;
        }

        // 기존 임시 인챈트 제거
        player->ApplyEnchantment(
            item,
            TEMP_ENCHANTMENT_SLOT,
            false
        );

        // 3923 적용
        item->SetEnchantment(
            TEMP_ENCHANTMENT_SLOT,
            ENCHANT_CUSTOM_BERSERK,
            BERSERK_ENCHANT_DURATION,
            0,
            player->GetGUID()
        );

        // 새 인챈트 적용
        player->ApplyEnchantment(
            item,
            TEMP_ENCHANTMENT_SLOT,
            true
        );

        // 지속시간 등록
        player->AddEnchantmentDuration(
            item,
            TEMP_ENCHANTMENT_SLOT,
            BERSERK_ENCHANT_DURATION
        );

        return true;
    }
}

// =============================================================
// 900216 - 광폭화 무기 강화
// =============================================================
class spell_custom_berserk_weapon_enchant
    : public SpellScriptLoader
{
public:
    spell_custom_berserk_weapon_enchant()
        : SpellScriptLoader(
            "spell_custom_berserk_weapon_enchant"
        )
    {
    }

    class spell_custom_berserk_weapon_enchant_SpellScript
        : public SpellScript
    {
        PrepareSpellScript(
            spell_custom_berserk_weapon_enchant_SpellScript
        );

        SpellCastResult CheckCast()
        {
            Player* player =
                GetCaster()
                    ? GetCaster()->ToPlayer()
                    : nullptr;

            if (!player)
                return SPELL_FAILED_DONT_REPORT;

            Item* targetItem =
                GetExplTargetItem();

            if (!targetItem)
            {
                ChatHandler(
                    player->GetSession()
                ).SendSysMessage(
                    "광폭화를 적용할 무기를 선택하세요."
                );

                return SPELL_FAILED_DONT_REPORT;
            }

            if (!IsCustomBerserkWeapon(targetItem))
            {
                ChatHandler(
                    player->GetSession()
                ).SendSysMessage(
                    "광폭화는 무기에만 적용할 수 있습니다."
                );

                return SPELL_FAILED_DONT_REPORT;
            }

            return SPELL_CAST_OK;
        }

        void HandleEnchant(
            SpellEffIndex effIndex)
        {
            // DBC Effect 54의 기본 처리는 막고
            // 직접 3923을 적용
            PreventHitDefaultEffect(
                effIndex
            );

            Player* player =
                GetCaster()
                    ? GetCaster()->ToPlayer()
                    : nullptr;

            if (!player)
                return;

            Item* targetItem =
                GetHitItem();

            if (!targetItem)
            {
                targetItem =
                    GetExplTargetItem();
            }

            if (!targetItem)
                return;

            // -------------------------------------------------
            // 3923 적용
            // -------------------------------------------------
            if (!ApplyCustomBerserkEnchant(
                    player,
                    targetItem))
            {
                ChatHandler(
                    player->GetSession()
                ).SendSysMessage(
                    "광폭화 무기 강화 적용에 실패했습니다."
                );

                return;
            }

            // -------------------------------------------------
            // 3923 적용 성공
            //
            // 즉시 900217 상태 동기화
            // -------------------------------------------------
            UpdateCustomBerserkProcAura(
                player
            );

            ChatHandler(
                player->GetSession()
            ).SendSysMessage(
                "무기에 광폭화가 적용되었습니다. 지속시간: 1시간"
            );
        }

        void Register() override
        {
            OnCheckCast += SpellCheckCastFn(
                spell_custom_berserk_weapon_enchant_SpellScript::CheckCast
            );

            OnEffectHitTarget += SpellEffectFn(
                spell_custom_berserk_weapon_enchant_SpellScript::HandleEnchant,
                EFFECT_0,
                SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY
            );
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_custom_berserk_weapon_enchant_SpellScript();
    }
};

#include "ScriptMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"

#include <ctime>

namespace
{
    constexpr uint32 kBuffSpellA = 16609;
    constexpr uint32 kBuffSpellB = 24425;
    constexpr uint32 kBuffSpellC = 22888;
    constexpr uint32 kBuffSpellD = 34410;
    constexpr uint32 kBuffSpellE = 29235;

    constexpr uint32 kBuffSpells[] =
    {
        kBuffSpellA,
        kBuffSpellB,
        kBuffSpellC,
        kBuffSpellD,
        kBuffSpellE
    };
}

class hourly_random_buff_world : public WorldScript
{
public:
    hourly_random_buff_world()
        : WorldScript("hourly_random_buff_world")
    {
    }

    void OnUpdate(uint32 diff) override
    {
        // 매 틱마다 시간 확인하지 않고 1초에 한 번만 확인
        if (_checkTimer > diff)
        {
            _checkTimer -= diff;
            return;
        }

        _checkTimer = 1000;

        std::time_t now = std::time(nullptr);
        std::tm* localTime = std::localtime(&now);

        if (!localTime)
            return;

        // 정각이 아니면 종료
        if (localTime->tm_min != 0)
            return;

        // 같은 정각에 중복 실행 방지
        if (_lastYear == localTime->tm_year &&
            _lastDay == localTime->tm_yday &&
            _lastHour == localTime->tm_hour)
        {
            return;
        }

        _lastYear = localTime->tm_year;
        _lastDay = localTime->tm_yday;
        _lastHour = localTime->tm_hour;

        // 이번 정각에 사용할 버프 하나만 선택
        uint32 selectedSpell = kBuffSpells[urand(0, 4)];

        HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();

        for (auto const& kv : players)
        {
            Player* player = kv.second;

            if (!player)
                continue;

            if (!player->IsInWorld())
                continue;

            player->AddAura(selectedSpell, player);
        }
    }

private:
    uint32 _checkTimer = 1000;

    int32 _lastYear = -1;
    int32 _lastDay = -1;
    int32 _lastHour = -1;
};























void AddSC_custom_spell_script()
{
    new spell_guild_hearthstone();
	new guild_tech_combat_bonus();
	RegisterSpellScript(spell_wg_rocket_propelled_goblin_grenade);
	RegisterSpellScript(spell_custom_firestone_preparation_redirect);
	new spell_custom_gem_combine();
	RegisterSpellScript(spell_guardian_purify);
	new legacy_rune_worldscript();
	new legacy_rune_serverscript();
	new custom_legacy_rune_calendar();
    new go_legacy_rune_magic_altar();
	new custom_legacy_rune_craft();
	new item_legacy_rune_execution_recipe();
	new custom_gem_combine_login();

	//불타ㅓ는 아드레날린
	new spell_custom_burning_adrenaline();
	new spell_custom_time_rewind();
	new item_custom_temporary_spell_scroll();
	new player_custom_temporary_spell();

	//광폭화
	new spell_custom_berserk_weapon_enchant();

	//매 정시에 버프
	new hourly_random_buff_world();

}