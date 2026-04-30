#include "ScriptMgr.h"
#include "SpellScript.h"
#include "Spell.h"
#include "Player.h"
#include "WorldSession.h"
#include "CustomRP.h"
#include "SpellHistory.h"
#include "Item.h"

static constexpr uint32 SPELL_HEARTHSTONE = 8690;

class spell_guild_hearthstone : public SpellScriptLoader
{
public:
    spell_guild_hearthstone() : SpellScriptLoader("spell_guild_hearthstone") { }

    class spell_guild_hearthstone_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_guild_hearthstone_SpellScript);

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
			AfterCast += SpellCastFn(spell_guild_hearthstone_SpellScript::HandleAfterCast);
		}
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_guild_hearthstone_SpellScript();
    }
};

#include "ScriptMgr.h"
#include "Player.h"
#include "Creature.h"
#include "CreatureData.h"
#include "Unit.h"
#include "WorldSession.h"
#include "CustomRP.h"
#include "GameTime.h"
#include "Random.h"

#include <unordered_map>
#include <string>

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


void AddSC_custom_spell_script()
{
    new spell_guild_hearthstone();
	new guild_tech_combat_bonus();
	RegisterSpellScript(spell_wg_rocket_propelled_goblin_grenade);
}