#ifndef CUSTOM_TEAM_BUFF_UTIL_H
#define CUSTOM_TEAM_BUFF_UTIL_H

#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellAuras.h"

namespace CustomTeamBuffUtil
{
    static inline bool IsValidTeamId(TeamId teamId)
    {
        return teamId == TEAM_ALLIANCE || teamId == TEAM_HORDE;
    }

    static inline bool IsValidOnlinePlayer(Player* player)
    {
        return player && player->IsInWorld();
    }

    static inline bool IsSameTeam(Player* player, TeamId teamId)
    {
        if (!IsValidOnlinePlayer(player))
            return false;

        return player->GetTeamId() == teamId;
    }

    static inline void SetAuraDuration(Player* player, uint32 spellId, int32 durationMs)
    {
        if (!player)
            return;

        Aura* aura = player->GetAura(spellId);
        if (!aura)
            return;

        aura->SetMaxDuration(durationMs);
        aura->SetDuration(durationMs);
    }

    static inline bool ApplyAuraToPlayer(
        Player* player,
        uint32 spellId,
        int32 durationMs = 0,
        bool removeBeforeApply = false,
        bool refreshDurationIfHasAura = true)
    {
        if (!IsValidOnlinePlayer(player))
            return false;

        if (removeBeforeApply)
            player->RemoveAurasDueToSpell(spellId);

        Aura* aura = player->GetAura(spellId);

        if (!aura)
            aura = player->AddAura(spellId, player);

        if (!aura)
            return false;

        if (durationMs != 0 && refreshDurationIfHasAura)
        {
            aura->SetMaxDuration(durationMs);
            aura->SetDuration(durationMs);
        }

        return true;
    }

    static inline uint32 ApplyAuraToTeam(
        TeamId teamId,
        uint32 spellId,
        int32 durationMs = 0,
        bool removeBeforeApply = false,
        bool refreshDurationIfHasAura = true)
    {
        if (!IsValidTeamId(teamId))
            return 0;

        uint32 count = 0;

        HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();

        for (HashMapHolder<Player>::MapType::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        {
            Player* player = itr->second;

            if (!IsSameTeam(player, teamId))
                continue;

            if (ApplyAuraToPlayer(
                player,
                spellId,
                durationMs,
                removeBeforeApply,
                refreshDurationIfHasAura))
            {
                ++count;
            }
        }

        return count;
    }

    static inline bool ApplySpellToPlayer(
        Player* player,
        uint32 spellId,
        int32 durationMs = 0,
        bool removeBeforeCast = false,
        bool skipCastIfHasAura = false,
        bool refreshDurationIfHasAura = true,
        bool forceAddAuraIfCastFails = true)
    {
        if (!IsValidOnlinePlayer(player))
            return false;

        if (skipCastIfHasAura && player->HasAura(spellId))
        {
            if (refreshDurationIfHasAura && durationMs != 0)
                SetAuraDuration(player, spellId, durationMs);

            return true;
        }

        if (removeBeforeCast)
            player->RemoveAurasDueToSpell(spellId);

        player->CastSpell(player, spellId, true);

        if (forceAddAuraIfCastFails && !player->HasAura(spellId))
            player->AddAura(spellId, player);

        if (!player->HasAura(spellId))
            return false;

        if (durationMs != 0)
            SetAuraDuration(player, spellId, durationMs);

        return true;
    }

    static inline uint32 ApplySpellToTeam(
        TeamId teamId,
        uint32 spellId,
        int32 durationMs = 0,
        bool removeBeforeCast = false,
        bool skipCastIfHasAura = false,
        bool refreshDurationIfHasAura = true,
        bool forceAddAuraIfCastFails = true)
    {
        if (!IsValidTeamId(teamId))
            return 0;

        uint32 count = 0;

        HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();

        for (HashMapHolder<Player>::MapType::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        {
            Player* player = itr->second;

            if (!IsSameTeam(player, teamId))
                continue;

            if (ApplySpellToPlayer(
                player,
                spellId,
                durationMs,
                removeBeforeCast,
                skipCastIfHasAura,
                refreshDurationIfHasAura,
                forceAddAuraIfCastFails))
            {
                ++count;
            }
        }

        return count;
    }

    static inline uint32 RemoveSpellFromTeam(TeamId teamId, uint32 spellId)
    {
        if (!IsValidTeamId(teamId))
            return 0;

        uint32 count = 0;

        HashMapHolder<Player>::MapType const& players = ObjectAccessor::GetPlayers();

        for (HashMapHolder<Player>::MapType::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        {
            Player* player = itr->second;

            if (!IsSameTeam(player, teamId))
                continue;

            player->RemoveAurasDueToSpell(spellId);
            ++count;
        }

        return count;
    }
}

#endif