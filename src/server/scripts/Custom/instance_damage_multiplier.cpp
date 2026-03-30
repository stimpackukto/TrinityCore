// Custom/instance_damage_multiplier.cpp
// 코어 무수정: 특정 던전(mapId) 내부에서
//  1) 몬스터가 플레이어에게 주는 데미지 배수 적용(근접/주문/도트틱)
//  2) 몬스터 레벨/체력/마력(Max/Current) 배율 적용
//     - 인던 진입 직후(플레이어 주변) 선적용 + 이후 주기적으로 재스캔(신규 스폰/이동 몹 보정)
//
// 레벨 보정:
//  - 일반 몬스터: 80
//  - 보스 몬스터: 83
//
// DB: appdata.instance_damage_mult
//  - map_id 키로 dmg_mult, hp_mult, mana_mult, party_check, party_size 로드
//  - enabled=1만 적용
//
// [NEW] 2가지 모드 공존
//  - party_check=0 : 업스케일링(레벨/HP/마나 + dmg_mult 배율)
//  - party_check=1 : 업스케일링 미적용 + 인원<=party_size 일 때만 "감소" 데미지 적용
//      dmg_mult 값 의미(감소 퍼센트 포인트):
//       1.0 => 10% 감소(x0.9), 2.0 => 20% 감소(x0.8), 1.5 => 15% 감소(x0.85)
//
// 리로드 커맨드(신형):
//  - .inst_dmg_mult   -> DB 재로딩
//    권한: rbac::RBAC_PERM_COMMAND_RELOAD

#include "ScriptMgr.h"
#include "Creature.h"
#include "Player.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Map.h"
#include "World.h"
#include "WorldSession.h"
#include "SharedDefines.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "AccountMgr.h"

#include "GridNotifiers.h"
#include "GridNotifiersImpl.h" // 템플릿 구현(Visit 등) 링크 에러 방지
#include "CellImpl.h"

#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <limits>
#include <cmath>
#include <algorithm>
#include <list>

namespace
{
    static constexpr float  PRE_SCALE_RADIUS   = 250.0f;
    static constexpr uint32 RESCAN_INTERVAL_MS = 2000;
    static constexpr uint8  LEVEL_NORMAL       = 80;
    static constexpr uint8  LEVEL_BOSS         = 83;

    struct MultRow
    {
        float dmgMult     = 1.0f;   // 업스케일 모드: 배율 / 인원체크 모드: 감소 퍼센트 포인트(1=>10%)
        float hpMult      = 1.0f;
        float manaMult    = 1.0f;

        bool  partyCheck  = false;  // [NEW]
        uint8 partySize   = 0;      // [NEW] (partyCheck=1일 때 유효)
    };

    std::unordered_map<uint32, MultRow> g_ByMap;
    std::shared_mutex g_Mx;

    std::unordered_set<uint64> g_Scaled;
    std::shared_mutex g_ScaledMx;

    static uint64 MakeScaledKey(Creature* cr)
    {
        if (!cr)
            return 0;

        uint32 mapId = cr->GetMapId();
        uint32 low   = cr->GetGUID().GetCounter();
        return (uint64(mapId) << 32) | uint64(low);
    }

    static float SanitizeMult(float v)
    {
        if (!std::isfinite(v) || v <= 0.0f)
            return 1.0f;

        if (v > 100.0f)
            return 100.0f;

        return v;
    }

    static uint32 ScaleU32(uint32 v, float mult)
    {
        if (v == 0 || mult == 1.0f)
            return v;

        double scaled = double(v) * double(mult);
        if (!std::isfinite(scaled) || scaled <= 0.0)
            return v;

        uint64 nv = uint64(std::llround(scaled));
        if (nv > std::numeric_limits<uint32>::max())
            nv = std::numeric_limits<uint32>::max();

        return uint32(nv);
    }

    static int32 ScaleI32(int32 v, float mult)
    {
        if (v == 0 || mult == 1.0f)
            return v;

        double scaled = double(v) * double(mult);
        if (!std::isfinite(scaled))
            return v;

        long long nv = llround(scaled);

        if (nv > std::numeric_limits<int32>::max())
            nv = std::numeric_limits<int32>::max();
        if (nv < std::numeric_limits<int32>::min())
            nv = std::numeric_limits<int32>::min();

        return int32(nv);
    }

    // [NEW] 인원체크 감소모드: dmg_mult(1=>10%)를 multiplier로 변환
    // 1.0 => 0.90, 2.0 => 0.80, 1.5 => 0.85
    static float GetDownscaleDamageMultiplier(float dmgPoint)
    {
        double reduce = double(dmgPoint) * 0.10;   // 10% * value
        double mult   = 1.0 - reduce;

        if (!std::isfinite(mult))
            return 1.0f;

        if (mult < 0.05) // 바닥 제한(너무 과한 감소 방지)
            mult = 0.05;

        if (mult > 1.0)
            mult = 1.0;

        return float(mult);
    }

    // [NEW] 현재 맵 내 플레이어 수(비GM)
    static uint32 GetPlayerCountInMap(Map* map)
    {
        if (!map)
            return 0;

        uint32 count = 0;
        Map::PlayerList const& players = map->GetPlayers();
        for (auto const& itr : players)
        {
            if (Player* p = itr.GetSource())
            {
                if (p->IsGameMaster())
                    continue;
                ++count;
            }
        }
        return count;
    }

    static bool IsBossCreature(Creature* cr)
    {
        if (!cr)
            return false;

        CreatureTemplate const* ct = cr->GetCreatureTemplate();
        if (!ct)
            return false;

        // 월드보스
        if (ct->rank == CREATURE_ELITE_WORLDBOSS)
            return true;

        // 던전 보스 플래그(코어마다 매크로 유무가 달라서 숫자 fallback)
        static constexpr uint32 FLAG_EXTRA_DUNGEON_BOSS_FALLBACK = 0x00000001;

#ifdef CREATURE_FLAG_EXTRA_DUNGEON_BOSS
        if (ct->flags_extra & CREATURE_FLAG_EXTRA_DUNGEON_BOSS)
            return true;
#else
        if (ct->flags_extra & FLAG_EXTRA_DUNGEON_BOSS_FALLBACK)
            return true;
#endif

        return false;
    }

    // 이 코어에 AnyCreatureInObjectRangeCheck가 없으므로 직접 구현
    struct AnyCreatureInRangeCheck
    {
        WorldObject const* ref;
        float range;

        AnyCreatureInRangeCheck(WorldObject const* r, float dist) : ref(r), range(dist) { }

        bool operator()(Creature* c) const
        {
            if (!c || !ref)
                return false;

            return ref->IsWithinDistInMap(c, range);
        }
    };

    static void LoadAll()
    {
        std::unordered_map<uint32, MultRow> temp;

        // [CHANGED] party_check, party_size 추가 로드
        QueryResult r = CharacterDatabase.Query(
            "SELECT map_id, dmg_mult, hp_mult, mana_mult, party_check_enabled, party_check_count "
            "FROM appdata.instance_damage_mult "
            "WHERE enabled=1");

        if (r)
        {
            do
            {
                Field* f = r->Fetch();

                uint32 mapId = f[0].GetUInt32();
                if (mapId == 0)
                    continue;

                MultRow row;
                row.dmgMult    = SanitizeMult(f[1].GetFloat());
                row.hpMult     = SanitizeMult(f[2].GetFloat());
                row.manaMult   = SanitizeMult(f[3].GetFloat());
                row.partyCheck = (f[4].GetUInt8() != 0);
                row.partySize  = f[5].GetUInt8();

                temp[mapId] = row;

            } while (r->NextRow());
        }

        {
            std::unique_lock lk(g_Mx);
            g_ByMap.swap(temp);
        }

        {
            std::unique_lock lk(g_ScaledMx);
            g_Scaled.clear();
        }

        TC_LOG_INFO("custom", "[InstanceDamageMult] Loaded {} rows", g_ByMap.size());
    }

    static bool TryGetMultRow(uint32 mapId, MultRow& outRow)
    {
        outRow = MultRow{};

        std::shared_lock lk(g_Mx);
        auto it = g_ByMap.find(mapId);
        if (it == g_ByMap.end())
            return false;

        outRow = it->second;
        return true;
    }

    // [CHANGED] party_check 모드도 통과시키되, 적용 조건은 아래에서 분기
    static bool ShouldApply(Unit* target, Unit* attacker, MultRow& outRow)
    {
        outRow = MultRow{};

        if (!target || !attacker)
            return false;

        if (!target->ToPlayer())
            return false;

        Creature* cr = attacker->ToCreature();
        if (!cr)
            return false;

        if (cr->IsPet() || cr->IsGuardian() || cr->IsHunterPet())
            return false;

        Map* map = cr->GetMap();
        if (!map || !map->IsDungeon())
            return false;

        MultRow row;
        if (!TryGetMultRow(map->GetId(), row))
            return false;

        // 업스케일 모드: dmg_mult가 1이면 의미 없으니 제외
        if (!row.partyCheck)
        {
            if (row.dmgMult == 1.0f)
                return false;
        }
        else
        {
            // 인원체크 모드: partySize 없으면 비활성 취급
            if (row.partySize == 0)
                return false;
            // dmgMult는 1이어도 10% 감소가 되므로 여기서 걸러버리면 안 됨
        }

        outRow = row;
        return true;
    }

    // [NEW] 실제 데미지 multiplier 결정 (업스케일 vs 인원체크 감소)
    static float ResolveDamageMultiplier(Map* map, MultRow const& row)
    {
        if (!map)
            return 1.0f;

        if (!row.partyCheck)
            return row.dmgMult;

        uint32 cur = GetPlayerCountInMap(map);
        if (cur == 0)
            return 1.0f;

        if (cur <= row.partySize)
            return GetDownscaleDamageMultiplier(row.dmgMult);

        return 1.0f;
    }

    // [CHANGED] 업스케일 모드에서만 레벨/HP/마나 스케일링 수행
    static void EnsureCreatureScaled(Creature* cr, MultRow const& row)
    {
        if (!cr)
            return;

        Map* map = cr->GetMap();
        if (!map || !map->IsDungeon())
            return;

        if (cr->IsPet() || cr->IsGuardian() || cr->IsHunterPet())
            return;

        // [NEW] 인원체크 감소 모드면 업스케일링(레벨/HP/마나) 자체를 하지 않음
        if (row.partyCheck)
            return;

        uint64 key = MakeScaledKey(cr);
        if (!key)
            return;

        {
            std::shared_lock rlk(g_ScaledMx);
            if (g_Scaled.find(key) != g_Scaled.end())
                return;
        }
        {
            std::unique_lock wlk(g_ScaledMx);
            if (g_Scaled.find(key) != g_Scaled.end())
                return;
            g_Scaled.insert(key);
        }

        uint8 newLevel = IsBossCreature(cr) ? LEVEL_BOSS : LEVEL_NORMAL;
        if (cr->GetLevel() != newLevel)
            cr->SetLevel(newLevel);

        if (row.hpMult != 1.0f)
        {
            uint32 oldMax = cr->GetMaxHealth();
            uint32 oldCur = cr->GetHealth();

            uint32 newMax = ScaleU32(oldMax, row.hpMult);
            uint32 newCur = ScaleU32(oldCur, row.hpMult);

            cr->SetMaxHealth(newMax);
            cr->SetHealth(std::min(newCur, newMax));
        }

        if (row.manaMult != 1.0f && cr->GetMaxPower(POWER_MANA) > 0)
        {
            uint32 oldMax = cr->GetMaxPower(POWER_MANA);
            uint32 oldCur = cr->GetPower(POWER_MANA);

            uint32 newMax = ScaleU32(oldMax, row.manaMult);
            uint32 newCur = ScaleU32(oldCur, row.manaMult);

            cr->SetMaxPower(POWER_MANA, newMax);
            cr->SetPower(POWER_MANA, std::min(newCur, newMax));
        }
    }

    static void ScaleCreaturesAroundPlayer(Player* player)
    {
        if (!player)
            return;

        Map* map = player->GetMap();
        if (!map || !map->IsDungeon())
            return;

        MultRow row;
        if (!TryGetMultRow(map->GetId(), row))
            return;

        // [NEW] 인원체크 감소 모드면 "선스케일/재스캔 스케일"을 하지 않음
        if (row.partyCheck)
            return;

        std::list<Creature*> creatures;
        AnyCreatureInRangeCheck check(player, PRE_SCALE_RADIUS);
        Trinity::CreatureListSearcher<AnyCreatureInRangeCheck> searcher(player, creatures, check);
        Cell::VisitAllObjects(player, searcher, PRE_SCALE_RADIUS);

        for (Creature* cr : creatures)
            EnsureCreatureScaled(cr, row);
    }

    static void TryScaleOnInteraction(Unit* a, Unit* b)
    {
        if (!a || !b)
            return;

        Creature* cr = nullptr;

        if (a->ToCreature() && b->ToPlayer())
            cr = a->ToCreature();
        else if (b->ToCreature() && a->ToPlayer())
            cr = b->ToCreature();

        if (!cr)
            return;

        Map* map = cr->GetMap();
        if (!map || !map->IsDungeon())
            return;

        MultRow row;
        if (!TryGetMultRow(map->GetId(), row))
            return;

        // EnsureCreatureScaled 내부에서 partyCheck면 바로 리턴됨
        EnsureCreatureScaled(cr, row);
    }
}

class InstanceDamageMult_World : public WorldScript
{
public:
    InstanceDamageMult_World() : WorldScript("InstanceDamageMult_World") { }

    void OnStartup() override
    {
        LoadAll();
    }

    void OnUpdate(uint32 diff) override
    {
        static uint32 acc = 0;
        acc += diff;
        if (acc < RESCAN_INTERVAL_MS)
            return;
        acc = 0;

        // 타입은 SessionMap(글로벌 typedef)로 잡는 게 가장 안전
        SessionMap const& sessions = sWorld->GetAllSessions();
        for (auto const& itr : sessions)
        {
            WorldSession* sess = itr.second;
            if (!sess)
                continue;

            Player* p = sess->GetPlayer();
            if (!p)
                continue;

            Map* map = p->GetMap();
            if (!map || !map->IsDungeon())
                continue;

            ScaleCreaturesAroundPlayer(p);
        }
    }
};

class InstanceDamageMult_Player : public PlayerScript
{
public:
    InstanceDamageMult_Player() : PlayerScript("InstanceDamageMult_Player") { }

    void OnMapChanged(Player* player) override
    {
        if (!player)
            return;

        Map* map = player->GetMap();
        if (!map || !map->IsDungeon())
            return;

        ScaleCreaturesAroundPlayer(player);
    }
};

class InstanceDamageMult_Unit : public UnitScript
{
public:
    InstanceDamageMult_Unit() : UnitScript("InstanceDamageMult_Unit") { }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        TryScaleOnInteraction(attacker, target);

        MultRow row;
        if (!ShouldApply(target, attacker, row))
            return;

        Creature* cr = attacker->ToCreature();
        if (!cr)
            return;

        Map* map = cr->GetMap();
        float mult = ResolveDamageMultiplier(map, row);
        if (mult != 1.0f)
            damage = ScaleU32(damage, mult);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage) override
    {
        TryScaleOnInteraction(attacker, target);

        MultRow row;
        if (!ShouldApply(target, attacker, row))
            return;

        Creature* cr = attacker->ToCreature();
        if (!cr)
            return;

        Map* map = cr->GetMap();
        float mult = ResolveDamageMultiplier(map, row);
        if (mult != 1.0f)
            damage = ScaleI32(damage, mult);
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage) override
    {
        TryScaleOnInteraction(attacker, target);

        MultRow row;
        if (!ShouldApply(target, attacker, row))
            return;

        Creature* cr = attacker->ToCreature();
        if (!cr)
            return;

        Map* map = cr->GetMap();
        float mult = ResolveDamageMultiplier(map, row);
        if (mult != 1.0f)
            damage = ScaleU32(damage, mult);
    }
};

class InstanceDamageMult_Command : public CommandScript
{
public:
    InstanceDamageMult_Command() : CommandScript("InstanceDamageMult_Command") { }

    static bool HandleReload(ChatHandler* handler)
    {
        LoadAll();
        handler->SendGlobalGMSysMessage("InstanceDamageMult: appdata.instance_damage_mult reloaded.");
        return true;
    }

    Trinity::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Trinity::ChatCommands;

        static ChatCommandTable table =
        {
            { "inst_dmg_mult", HandleReload, rbac::RBAC_PERM_COMMAND_RELOAD, Console::Yes },
        };

        return table;
    }
};

void AddSC_instance_damage_multiplier()
{
    new InstanceDamageMult_World();
    new InstanceDamageMult_Player();
    new InstanceDamageMult_Unit();
    new InstanceDamageMult_Command();
}
