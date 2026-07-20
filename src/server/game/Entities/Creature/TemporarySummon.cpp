/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Custom Update - 2026-07-17
 * - Keep Priestess Delrissa encounter summons from being removed by owner-idle cleanup.
 */

#include "TemporarySummon.h"
#include "CreatureAI.h"
#include "DBCStructure.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"

#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <ctime>

namespace
{
    uint32 constexpr CUSTOM_TEMP_SUMMON_OWNER_IDLE_CLEANUP_DELAY = 180000;
    uint32 constexpr CUSTOM_TEMP_SUMMON_OWNER_IDLE_CHECK_INTERVAL = 5000;
    float constexpr CUSTOM_TEMP_SUMMON_OWNER_IDLE_PLAYER_CHECK_DISTANCE = 200.0f;
    uint32 constexpr CUSTOM_TEMP_SUMMON_OWNER_IDLE_PRESERVE_SUMMONER_ENTRY = 24560;

    bool CustomTempSummonIsUnitActive(Unit* unit)
    {
        if (!unit || !unit->IsAlive())
            return false;

        return unit->IsInCombat() ||
            unit->GetVictim() ||
            unit->IsNonMeleeSpellCast(false) ||
            unit->HasUnitState(UNIT_STATE_CASTING | UNIT_STATE_CHASE | UNIT_STATE_FOLLOW | UNIT_STATE_FLEEING | UNIT_STATE_CONFUSED | UNIT_STATE_DISTRACTED);
    }

	bool CustomTempSummonUnsummonFarPlayerPet(TempSummon* summon)
    {
        if (!summon)
            return false;

        if (!summon->IsPet())
            return false;

        Pet* pet = summon->ToPet();
        if (!pet)
            return false;

        Unit* owner = pet->GetOwner();
        if (!owner || !owner->IsInWorld())
            return false;

        Player* player = owner->ToPlayer();
        if (!player || !player->IsInWorld())
            return false;

        if (pet->GetMapId() == player->GetMapId() &&
            pet->IsWithinDistInMap(player, CUSTOM_TEMP_SUMMON_OWNER_IDLE_PLAYER_CHECK_DISTANCE))
            return false;

        TC_LOG_INFO("entities.unit",
            "Custom pet owner distance unsummon. entry={} map={} zone={} owner={} distanceLimit={}",
            pet->GetEntry(),
            pet->GetMapId(),
            pet->GetZoneId(),
            player->GetGUID().ToString(),
            CUSTOM_TEMP_SUMMON_OWNER_IDLE_PLAYER_CHECK_DISTANCE);

        player->RemovePet(pet, PET_SAVE_AS_CURRENT, false);
        return true;
    }

    bool CustomTempSummonHasPlayerNear(WorldObject* object, float distance)
    {
        if (!object || !object->IsInWorld())
            return false;

        Map* map = object->GetMap();
        if (!map)
            return false;

        Map::PlayerList const& players = map->GetPlayers();

        for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        {
            Player* player = itr->GetSource();
            if (!player || !player->IsInWorld())
                continue;

            if (object->IsWithinDistInMap(player, distance))
                return true;
        }

        return false;
    }
}

TempSummon::TempSummon(SummonPropertiesEntry const* properties, WorldObject* owner, bool isWorldObject) :
Creature(isWorldObject), m_Properties(properties), m_type(TEMPSUMMON_MANUAL_DESPAWN),
m_timer(0), m_lifetime(0), m_canFollowOwner(true),
m_ownerIdleCleanupTimer(0), m_ownerIdleCheckTimer(0)
{
    if (owner)
        m_summonerGUID = owner->GetGUID();

    m_unitTypeMask |= UNIT_MASK_SUMMON;
}

WorldObject* TempSummon::GetSummoner() const
{
    return !m_summonerGUID.IsEmpty() ? ObjectAccessor::GetWorldObject(*this, m_summonerGUID) : nullptr;
}

Unit* TempSummon::GetSummonerUnit() const
{
    if (WorldObject* summoner = GetSummoner())
        return summoner->ToUnit();
    return nullptr;
}

Creature* TempSummon::GetSummonerCreatureBase() const
{
    return !m_summonerGUID.IsEmpty() ? ObjectAccessor::GetCreature(*this, m_summonerGUID) : nullptr;
}

GameObject* TempSummon::GetSummonerGameObject() const
{
    if (WorldObject* summoner = GetSummoner())
        return summoner->ToGameObject();
    return nullptr;
}

void TempSummon::Update(uint32 diff)
{
	if (CustomTempSummonUnsummonFarPlayerPet(this))
        return;

    if (m_ownerIdleCheckTimer <= diff)
    {
        m_ownerIdleCheckTimer = CUSTOM_TEMP_SUMMON_OWNER_IDLE_CHECK_INTERVAL;

        bool resetIdleTimer = false;

        if (CustomTempSummonIsUnitActive(this))
            resetIdleTimer = true;

        WorldObject* owner = GetSummoner();

        if (!owner)
        {
            // Owner disappeared. Treat this TempSummon as orphaned and clean it after the idle delay.
        }
        else if (Unit* ownerUnit = owner->ToUnit())
        {
            if (ownerUnit->GetTypeId() == TYPEID_PLAYER)
            {
                if (owner->IsInWorld() &&
                    owner->GetMapId() == GetMapId() &&
                    IsWithinDistInMap(owner, CUSTOM_TEMP_SUMMON_OWNER_IDLE_PLAYER_CHECK_DISTANCE))
                    resetIdleTimer = true;
            }
            else
            {
                if (Creature const* ownerCreature = ownerUnit->ToCreature())
                    if (ownerCreature->GetEntry() == CUSTOM_TEMP_SUMMON_OWNER_IDLE_PRESERVE_SUMMONER_ENTRY)
                        resetIdleTimer = true;

                if (CustomTempSummonIsUnitActive(ownerUnit))
                    resetIdleTimer = true;
            }
        }
        else if (GameObject* ownerGameObject = owner->ToGameObject())
        {
            if (CustomTempSummonHasPlayerNear(this, CUSTOM_TEMP_SUMMON_OWNER_IDLE_PLAYER_CHECK_DISTANCE) ||
                CustomTempSummonHasPlayerNear(ownerGameObject, CUSTOM_TEMP_SUMMON_OWNER_IDLE_PLAYER_CHECK_DISTANCE))
                resetIdleTimer = true;
        }
        else
            resetIdleTimer = true;

        if (resetIdleTimer)
            m_ownerIdleCleanupTimer = 0;
        else
        {
            if (m_ownerIdleCleanupTimer < CUSTOM_TEMP_SUMMON_OWNER_IDLE_CLEANUP_DELAY)
                m_ownerIdleCleanupTimer += CUSTOM_TEMP_SUMMON_OWNER_IDLE_CHECK_INTERVAL;

            if (m_ownerIdleCleanupTimer >= CUSTOM_TEMP_SUMMON_OWNER_IDLE_CLEANUP_DELAY)
            {
                TC_LOG_INFO("entities.unit",
                    "TempSummon owner idle cleanup. entry={} map={} zone={} summonType={} timer={} lifetime={} summonerGUID={}",
                    GetEntry(),
                    GetMapId(),
                    GetZoneId(),
                    uint32(m_type),
                    m_timer,
                    m_lifetime,
                    m_summonerGUID.ToString());

                UnSummon();
                return;
            }
        }
    }
    else
        m_ownerIdleCheckTimer -= diff;

	{
    uint32 mapId = GetMapId();

    if (mapId == 571 || mapId == 530)
    {
        struct CustomTempSummonStat
        {
            uint64 count = 0;
            uint32 mapId = 0;
            uint32 entry = 0;
            uint32 summonType = 0;
            uint32 timer = 0;
            uint32 lifetime = 0;
        };

        static std::mutex customTempSummonLogMutex;
        static std::unordered_map<uint64, CustomTempSummonStat> customTempSummonStats;
        static time_t customTempSummonLastLog = 0;

        uint32 entry = GetEntry();
        uint32 summonType = uint32(m_type);

        uint64 key =
            (uint64(mapId) << 40) |
            (uint64(entry) << 8) |
            uint64(summonType);

        time_t now = time(nullptr);

        {
            std::lock_guard<std::mutex> lock(customTempSummonLogMutex);

            CustomTempSummonStat& stat = customTempSummonStats[key];
            stat.count++;
            stat.mapId = mapId;
            stat.entry = entry;
            stat.summonType = summonType;
            stat.timer = m_timer;
            stat.lifetime = m_lifetime;

            if (now - customTempSummonLastLog >= 10)
            {
                customTempSummonLastLog = now;

                std::vector<CustomTempSummonStat> rows;
                rows.reserve(customTempSummonStats.size());

                for (std::unordered_map<uint64, CustomTempSummonStat>::const_iterator itr = customTempSummonStats.begin(); itr != customTempSummonStats.end(); ++itr)
                    rows.push_back(itr->second);

                std::sort(rows.begin(), rows.end(), [](CustomTempSummonStat const& a, CustomTempSummonStat const& b)
                {
                    return a.count > b.count;
                });

                std::ofstream log("/home/esotar/log/world_tempsummon_update_top.log", std::ios::app);

                if (log.is_open())
                {
                    log << "[" << now << "] TempSummon update top\n";

                    uint32 printed = 0;

                    for (CustomTempSummonStat const& row : rows)
                    {
                        log << "mapId=" << row.mapId
                            << " entry=" << row.entry
                            << " summonType=" << row.summonType
                            << " timer=" << row.timer
                            << " lifetime=" << row.lifetime
                            << " updates=" << row.count
                            << "\n";

                        ++printed;

                        if (printed >= 30)
                            break;
                    }

                    log << "\n";
                }

                customTempSummonStats.clear();
            }
        }
    }
}

    Creature::Update(diff);

    if (m_deathState == DEAD)
    {
        UnSummon();
        return;
    }
    switch (m_type)
    {
        case TEMPSUMMON_MANUAL_DESPAWN:
        case TEMPSUMMON_DEAD_DESPAWN:
            break;
        case TEMPSUMMON_TIMED_DESPAWN:
        {
            if (m_timer <= diff)
            {
                UnSummon();
                return;
            }

            m_timer -= diff;
            break;
        }
        case TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT:
        {
            if (!IsInCombat())
            {
                if (m_timer <= diff)
                {
                    UnSummon();
                    return;
                }

                m_timer -= diff;
            }
            else if (m_timer != m_lifetime)
                m_timer = m_lifetime;

            break;
        }

        case TEMPSUMMON_CORPSE_TIMED_DESPAWN:
        {
            if (m_deathState == CORPSE)
            {
                if (m_timer <= diff)
                {
                    UnSummon();
                    return;
                }

                m_timer -= diff;
            }
            break;
        }
        case TEMPSUMMON_CORPSE_DESPAWN:
        {
            // if m_deathState is DEAD, CORPSE was skipped
            if (m_deathState == CORPSE)
            {
                UnSummon();
                return;
            }

            break;
        }
        case TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN:
        {
            if (m_deathState == CORPSE)
            {
                UnSummon();
                return;
            }

            if (!IsInCombat())
            {
                if (m_timer <= diff)
                {
                    UnSummon();
                    return;
                }
                else
                    m_timer -= diff;
            }
            else if (m_timer != m_lifetime)
                m_timer = m_lifetime;
            break;
        }
        case TEMPSUMMON_TIMED_OR_DEAD_DESPAWN:
        {
            if (!IsInCombat() && IsAlive())
            {
                if (m_timer <= diff)
                {
                    UnSummon();
                    return;
                }
                else
                    m_timer -= diff;
            }
            else if (m_timer != m_lifetime)
                m_timer = m_lifetime;
            break;
        }
        default:
            UnSummon();
            TC_LOG_ERROR("entities.unit", "Temporary summoned creature (entry: {}) have unknown type {} of ", GetEntry(), m_type);
            break;
    }
}

void TempSummon::InitStats(uint32 duration)
{
    ASSERT(!IsPet());

    m_timer = duration;
    m_lifetime = duration;

    if (m_type == TEMPSUMMON_MANUAL_DESPAWN)
        m_type = (duration == 0) ? TEMPSUMMON_DEAD_DESPAWN : TEMPSUMMON_TIMED_DESPAWN;

    Unit* owner = GetSummonerUnit();

    if (owner && IsTrigger() && m_spells[0])
    {
        SetFaction(owner->GetFaction());
        SetLevel(owner->GetLevel());
        if (owner->GetTypeId() == TYPEID_PLAYER)
            m_ControlledByPlayer = true;
    }

    if (!m_Properties)
        return;

    if (owner)
    {
        std::ptrdiff_t slot = m_Properties->Slot;
        if (slot != 0)
        {
            if (!owner->m_SummonSlot[slot].IsEmpty() && owner->m_SummonSlot[slot] != GetGUID())
            {
                Creature* oldSummon = GetMap()->GetCreature(owner->m_SummonSlot[slot]);
                if (oldSummon && oldSummon->IsSummon())
                    oldSummon->ToTempSummon()->UnSummon();
            }
            owner->m_SummonSlot[slot] = GetGUID();
        }
    }

    if (m_Properties->Faction)
        SetFaction(m_Properties->Faction);
    else if (IsVehicle() && owner) // properties should be vehicle
        SetFaction(owner->GetFaction());
}

void TempSummon::InitSummon()
{
    WorldObject* owner = GetSummoner();
    if (owner)
    {
        if (owner->GetTypeId() == TYPEID_UNIT)
        {
            if (owner->ToCreature()->IsAIEnabled())
                owner->ToCreature()->AI()->JustSummoned(this);
        }
        else if (owner->GetTypeId() == TYPEID_GAMEOBJECT)
        {
            if (owner->ToGameObject()->AI())
                owner->ToGameObject()->AI()->JustSummoned(this);
        }
        if (IsAIEnabled())
            AI()->IsSummonedBy(owner);
    }
}

void TempSummon::UpdateObjectVisibilityOnCreate()
{
    WorldObject::UpdateObjectVisibility(true);
}

void TempSummon::SetTempSummonType(TempSummonType type)
{
    m_type = type;
}

void TempSummon::UnSummon(uint32 msTime)
{
    if (msTime)
    {
        ForcedUnsummonDelayEvent* pEvent = new ForcedUnsummonDelayEvent(*this);

        m_Events.AddEvent(pEvent, m_Events.CalculateTime(Milliseconds(msTime)));
        return;
    }

    //ASSERT(!IsPet());
    if (IsPet())
    {
        ToPet()->Remove(PET_SAVE_NOT_IN_SLOT);
        ASSERT(!IsInWorld());
        return;
    }

    if (WorldObject * owner = GetSummoner())
    {
        if (owner->GetTypeId() == TYPEID_UNIT && owner->ToCreature()->IsAIEnabled())
            owner->ToCreature()->AI()->SummonedCreatureDespawn(this);
        else if (owner->GetTypeId() == TYPEID_GAMEOBJECT && owner->ToGameObject()->AI())
            owner->ToGameObject()->AI()->SummonedCreatureDespawn(this);
    }

    AddObjectToRemoveList();
}

bool ForcedUnsummonDelayEvent::Execute(uint64 /*e_time*/, uint32 /*p_time*/)
{
    m_owner.UnSummon();
    return true;
}

void TempSummon::RemoveFromWorld()
{
    if (!IsInWorld())
        return;

    if (m_Properties)
        if (Unit* owner = GetSummonerUnit())
            for (ObjectGuid& summonSlot : owner->m_SummonSlot)
                if (summonSlot == GetGUID())
                    summonSlot.Clear();

    //if (GetOwnerGUID())
    //    TC_LOG_ERROR("entities.unit", "Unit {} has owner guid when removed from world", GetEntry());

    Creature::RemoveFromWorld();
}

std::string TempSummon::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << Creature::GetDebugInfo() << "\n"
        << std::boolalpha
        << "TempSummonType: " << std::to_string(GetSummonType()) << " Summoner: " << GetSummonerGUID().ToString()
        << "Timer: " << GetTimer();
    return sstr.str();
}

Minion::Minion(SummonPropertiesEntry const* properties, Unit* owner, bool isWorldObject)
    : TempSummon(properties, owner, isWorldObject), m_owner(owner)
{
    ASSERT(m_owner);
    m_unitTypeMask |= UNIT_MASK_MINION;
    m_followAngle = PET_FOLLOW_ANGLE;
}

void Minion::InitStats(uint32 duration)
{
    TempSummon::InitStats(duration);

    SetReactState(REACT_PASSIVE);

    SetCreatorGUID(GetOwner()->GetGUID());
    SetFaction(GetOwner()->GetFaction());

    GetOwner()->SetMinion(this, true);
}

void Minion::RemoveFromWorld()
{
    if (!IsInWorld())
        return;

    GetOwner()->SetMinion(this, false);
    TempSummon::RemoveFromWorld();
}

void Minion::setDeathState(DeathState s)
{
    Creature::setDeathState(s);
    if (s != JUST_DIED || !IsGuardianPet())
        return;

    Unit* owner = GetOwner();
    if (!owner || owner->GetTypeId() != TYPEID_PLAYER || owner->GetMinionGUID() != GetGUID())
        return;

    for (Unit* controlled : owner->m_Controlled)
    {
        if (controlled->GetEntry() == GetEntry() && controlled->IsAlive())
        {
            owner->SetMinionGUID(controlled->GetGUID());
            owner->SetPetGUID(controlled->GetGUID());
            owner->ToPlayer()->CharmSpellInitialize();
            break;
        }
    }
}

bool Minion::IsGuardianPet() const
{
    return IsPet() || (m_Properties && m_Properties->Control == SUMMON_CATEGORY_PET);
}

std::string Minion::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << TempSummon::GetDebugInfo() << "\n"
        << std::boolalpha
        << "Owner: " << (GetOwner() ? GetOwner()->GetGUID().ToString() : "");
    return sstr.str();
}

Guardian::Guardian(SummonPropertiesEntry const* properties, Unit* owner, bool isWorldObject) : Minion(properties, owner, isWorldObject)
, m_bonusSpellDamage(0)
{
    memset(m_statFromOwner, 0, sizeof(float)*MAX_STATS);
    m_unitTypeMask |= UNIT_MASK_GUARDIAN;
    if (properties && (properties->Title == SUMMON_TYPE_PET || properties->Control == SUMMON_CATEGORY_PET))
    {
        m_unitTypeMask |= UNIT_MASK_CONTROLABLE_GUARDIAN;
        InitCharmInfo();
    }
}

void Guardian::InitStats(uint32 duration)
{
    Minion::InitStats(duration);

    InitStatsForLevel(GetOwner()->GetLevel());

    if (GetOwner()->GetTypeId() == TYPEID_PLAYER && HasUnitTypeMask(UNIT_MASK_CONTROLABLE_GUARDIAN))
        m_charmInfo->InitCharmCreateSpells();

    SetReactState(REACT_AGGRESSIVE);
}

void Guardian::InitSummon()
{
    TempSummon::InitSummon();

    if (GetOwner()->GetTypeId() == TYPEID_PLAYER
            && GetOwner()->GetMinionGUID() == GetGUID()
            && !GetOwner()->GetCharmedGUID())
    {
        GetOwner()->ToPlayer()->CharmSpellInitialize();
    }
}

std::string Guardian::GetDebugInfo() const
{
    std::stringstream sstr;
    sstr << Minion::GetDebugInfo();
    return sstr.str();
}

Puppet::Puppet(SummonPropertiesEntry const* properties, Unit* owner)
    : Minion(properties, owner, false) //maybe true?
{
    ASSERT(m_owner->GetTypeId() == TYPEID_PLAYER);
    m_unitTypeMask |= UNIT_MASK_PUPPET;
}

void Puppet::InitStats(uint32 duration)
{
    Minion::InitStats(duration);
    SetLevel(GetOwner()->GetLevel());
    SetReactState(REACT_PASSIVE);
}

void Puppet::InitSummon()
{
    Minion::InitSummon();
    if (!SetCharmedBy(GetOwner(), CHARM_TYPE_POSSESS))
        ABORT();
}

void Puppet::Update(uint32 time)
{
    Minion::Update(time);
    //check if caster is channelling?
    if (IsInWorld())
    {
        if (!IsAlive())
        {
            UnSummon();
            /// @todo why long distance .die does not remove it
        }
    }
}