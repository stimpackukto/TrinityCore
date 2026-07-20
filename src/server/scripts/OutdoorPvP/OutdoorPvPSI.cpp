/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Custom Update - 2026-06-19
 * - 실리더스 전장 이벤트 시작 시 채집 점수와 서버 버프 상태를 함께 초기화.
 * - 로그인/맵 변경 시 채집 수가 200 미만인데 영구 실리더스트 자취 버프가 있으면 제거.
 * - 실리더스 서버 전체 버프 지급 시 CastSpell 경로를 피하고 AddAura 방식으로 직접 적용.
 */

#include "OutdoorPvPSI.h"
#include "CustomTeamBuffUtil.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "GameTime.h"
#include "Language.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "OutdoorPvPMgr.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldStatePackets.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <unordered_set>

#include "../Custom/outdoorpvp/CustomOutdoorPvpWarController.h"

uint32 const SI_MAX_RESOURCES = 200;
uint32 const SI_PROTECTED_RESOURCES = 150;

uint32 const SI_AREATRIGGER_H = 4168;
uint32 const SI_AREATRIGGER_A = 4162;
uint32 const SI_TURNIN_QUEST_CM_A = 17090;
uint32 const SI_TURNIN_QUEST_CM_H = 18199;
uint32 const SI_SILITHYST_MOUND = 181597;

uint32 const SI_SILITHYST_PERIODIC_FLAG_TRIGGER = 30176;
uint32 const SI_SILITHYST_FLAG_ALLIANCE_SPEED = 29894;
uint32 const SI_SILITHYST_FLAG_HORDE_SPEED = 29895;

float const SI_SILITHYST_SPEED_RATE = 0.93f;
uint32 const SI_SILITHYST_SPEED_CHECK_TIMER = 1000;

uint32 const SI_TRACES_DURATION_12H_SEC = 12 * 60 * 60;
uint32 const SI_TRACES_DURATION_2H_SEC = 2 * 60 * 60;
int32 const SI_TRACES_DURATION_PERMANENT = -1;

uint32 const SI_HONOR_REWARD_NORMAL = 38;     // 4배 기준 약 152점
uint32 const SI_HONOR_REWARD_KEEP_MAX = 125;  // 4배 기준 약 500점

uint32 const SI_WAR_TURNIN_SCORE = 5;

uint8 const OutdoorPvPSIBuffZonesNum = 3;
uint32 const OutdoorPvPSIBuffZones[OutdoorPvPSIBuffZonesNum] = { 1377, 3428, 3429 };

namespace
{
    enum SilithusTracesBuffMode : uint32
    {
        SI_TRACES_BUFF_NONE      = 0,
        SI_TRACES_BUFF_12H       = 1,
        SI_TRACES_BUFF_2H        = 2,
        SI_TRACES_BUFF_PERMANENT = 3
    };

    uint32 GetSilithystHonorReward(uint32 beforeScore)
    {
        if (beforeScore >= SI_MAX_RESOURCES)
            return SI_HONOR_REWARD_KEEP_MAX;

        return SI_HONOR_REWARD_NORMAL;
    }

    struct SilithusTracesBuffState
    {
        uint32 Mode = SI_TRACES_BUFF_NONE;
        uint64 Expires = 0;
    };

    char const* SI_STATE_TYPE = "silithus";
    char const* SI_STATE_KEY = "state";

    std::unordered_set<uint64> sSilithystSpeedLimitedPlayers;
    uint32 sSilithystSpeedTimer = 0;

    SilithusTracesBuffState g_SilithusAllianceTracesBuff;
    SilithusTracesBuffState g_SilithusHordeTracesBuff;

    uint32 g_SilithusGatheredA = 0;
    uint32 g_SilithusGatheredH = 0;

    uint32 ExtractUIntFromJsonState(std::string const& json, char const* key)
    {
        std::string pattern = Trinity::StringFormat("\"{}\":", key);

        size_t pos = json.find(pattern);
        if (pos == std::string::npos)
            return 0;

        pos += pattern.length();

        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t'))
            ++pos;

        return static_cast<uint32>(std::strtoul(json.c_str() + pos, nullptr, 10));
    }

    uint64 ExtractUInt64FromJsonState(std::string const& json, char const* key)
    {
        std::string pattern = Trinity::StringFormat("\"{}\":", key);

        size_t pos = json.find(pattern);
        if (pos == std::string::npos)
            return 0;

        pos += pattern.length();

        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t'))
            ++pos;

        return static_cast<uint64>(std::strtoull(json.c_str() + pos, nullptr, 10));
    }

    bool IsSilithusValidBuffMode(uint32 mode)
    {
        return mode == SI_TRACES_BUFF_NONE
            || mode == SI_TRACES_BUFF_12H
            || mode == SI_TRACES_BUFF_2H
            || mode == SI_TRACES_BUFF_PERMANENT;
    }

    void NormalizeSilithusBuffState(SilithusTracesBuffState& state)
    {
        if (!IsSilithusValidBuffMode(state.Mode))
        {
            state.Mode = SI_TRACES_BUFF_NONE;
            state.Expires = 0;
            return;
        }

        if (state.Mode == SI_TRACES_BUFF_NONE)
        {
            state.Expires = 0;
            return;
        }

        if (state.Mode == SI_TRACES_BUFF_PERMANENT)
        {
            state.Expires = 0;
            return;
        }

        if (state.Expires <= GameTime::GetGameTime())
        {
            state.Mode = SI_TRACES_BUFF_NONE;
            state.Expires = 0;
        }
    }

    bool IsSilithusTracesBuffActive(SilithusTracesBuffState const& state)
    {
        if (state.Mode == SI_TRACES_BUFF_PERMANENT)
            return true;

        if (state.Mode == SI_TRACES_BUFF_NONE)
            return false;

        return state.Expires > GameTime::GetGameTime();
    }

    int32 GetSilithusTracesBuffDurationMs(SilithusTracesBuffState const& state)
    {
        if (state.Mode == SI_TRACES_BUFF_PERMANENT)
            return SI_TRACES_DURATION_PERMANENT;

        if (!IsSilithusTracesBuffActive(state))
            return 0;

        uint64 now = GameTime::GetGameTime();

        if (state.Expires <= now)
            return 0;

        return int32((state.Expires - now) * 1000);
    }

    SilithusTracesBuffState& GetSilithusTracesBuffState(TeamId teamId)
    {
        return teamId == TEAM_ALLIANCE ? g_SilithusAllianceTracesBuff : g_SilithusHordeTracesBuff;
    }

    uint32 GetSilithusGatheredScore(TeamId teamId)
    {
        return teamId == TEAM_ALLIANCE ? g_SilithusGatheredA : g_SilithusGatheredH;
    }

    void SetSilithusGatheredScoreCache(uint32 gatheredA, uint32 gatheredH)
    {
        g_SilithusGatheredA = std::min<uint32>(gatheredA, SI_MAX_RESOURCES);
        g_SilithusGatheredH = std::min<uint32>(gatheredH, SI_MAX_RESOURCES);
    }

    bool IsPermanentSilithusTracesAura(Player* player)
    {
        if (!player)
            return false;

        Aura* aura = player->GetAura(SI_TRACES_OF_SILITHYST);
        if (!aura)
            return false;

        return aura->GetMaxDuration() < 0 || aura->GetDuration() < 0;
    }

    void SaveOutdoorPvPSIState(uint32 gatheredA, uint32 gatheredH)
    {
        gatheredA = std::min<uint32>(gatheredA, SI_MAX_RESOURCES);
        gatheredH = std::min<uint32>(gatheredH, SI_MAX_RESOURCES);

        SetSilithusGatheredScoreCache(gatheredA, gatheredH);

        NormalizeSilithusBuffState(g_SilithusAllianceTracesBuff);
        NormalizeSilithusBuffState(g_SilithusHordeTracesBuff);

        std::string stateValue = Trinity::StringFormat(
            "{{\"gathered_a\":{},\"gathered_h\":{},"
            "\"buff_a_mode\":{},\"buff_a_expires\":{},"
            "\"buff_h_mode\":{},\"buff_h_expires\":{}}}",
            gatheredA,
            gatheredH,
            g_SilithusAllianceTracesBuff.Mode,
            g_SilithusAllianceTracesBuff.Expires,
            g_SilithusHordeTracesBuff.Mode,
            g_SilithusHordeTracesBuff.Expires
        );

        LoginDatabase.PExecute(
            "INSERT INTO appdata.outdoorpvp_state "
            "    (pvp_type, state_key, state_value) "
            "VALUES "
            "    ('{}', '{}', '{}') "
            "ON DUPLICATE KEY UPDATE "
            "    state_value = VALUES(state_value)",
            SI_STATE_TYPE,
            SI_STATE_KEY,
            stateValue
        );
    }

    void RemoveInvalidPermanentSilithusTracesBuff(Player* player, TeamId teamId)
    {
        if (!player)
            return;

        uint32 gatheredScore = GetSilithusGatheredScore(teamId);

        if (gatheredScore >= SI_MAX_RESOURCES)
            return;

        SilithusTracesBuffState& state = GetSilithusTracesBuffState(teamId);

        if (state.Mode == SI_TRACES_BUFF_PERMANENT)
        {
            state.Mode = SI_TRACES_BUFF_NONE;
            state.Expires = 0;

            SaveOutdoorPvPSIState(g_SilithusGatheredA, g_SilithusGatheredH);
        }

        if (IsPermanentSilithusTracesAura(player))
            player->RemoveAurasDueToSpell(SI_TRACES_OF_SILITHYST);
    }

    void LoadOutdoorPvPSIState(uint32& gatheredA, uint32& gatheredH)
    {
        gatheredA = 0;
        gatheredH = 0;

        QueryResult result = LoginDatabase.PQuery(
            "SELECT state_value "
            "FROM appdata.outdoorpvp_state "
            "WHERE pvp_type = '{}' AND state_key = '{}' "
            "LIMIT 1",
            SI_STATE_TYPE,
            SI_STATE_KEY
        );

        if (!result)
        {
            SetSilithusGatheredScoreCache(gatheredA, gatheredH);
            SaveOutdoorPvPSIState(gatheredA, gatheredH);
            return;
        }

        std::string stateValue = result->Fetch()[0].GetString();

        gatheredA = std::min<uint32>(ExtractUIntFromJsonState(stateValue, "gathered_a"), SI_MAX_RESOURCES);
        gatheredH = std::min<uint32>(ExtractUIntFromJsonState(stateValue, "gathered_h"), SI_MAX_RESOURCES);

        SetSilithusGatheredScoreCache(gatheredA, gatheredH);

        g_SilithusAllianceTracesBuff.Mode = ExtractUIntFromJsonState(stateValue, "buff_a_mode");
        g_SilithusAllianceTracesBuff.Expires = ExtractUInt64FromJsonState(stateValue, "buff_a_expires");

        g_SilithusHordeTracesBuff.Mode = ExtractUIntFromJsonState(stateValue, "buff_h_mode");
        g_SilithusHordeTracesBuff.Expires = ExtractUInt64FromJsonState(stateValue, "buff_h_expires");

        NormalizeSilithusBuffState(g_SilithusAllianceTracesBuff);
        NormalizeSilithusBuffState(g_SilithusHordeTracesBuff);
    }

    bool IsSilithusOutdoorPvPZone(uint32 zone)
    {
        for (uint8 i = 0; i < OutdoorPvPSIBuffZonesNum; ++i)
        {
            if (OutdoorPvPSIBuffZones[i] == zone)
                return true;
        }

        return false;
    }

    bool HasSilithystCarryAura(Player* player)
    {
        if (!player)
            return false;

        return player->HasAura(SI_SILITHYST_FLAG);
    }

    void ApplySilithystSpeedLimit(Player* player)
    {
        if (!player)
            return;

        uint64 const guid = player->GetGUID().GetCounter();
        sSilithystSpeedLimitedPlayers.insert(guid);

        player->SetSpeedRate(MOVE_RUN, SI_SILITHYST_SPEED_RATE);
        player->SetSpeedRate(MOVE_SWIM, SI_SILITHYST_SPEED_RATE);
        player->SetSpeedRate(MOVE_FLIGHT, SI_SILITHYST_SPEED_RATE);
    }

    void RemoveSilithystSpeedLimit(Player* player)
    {
        if (!player)
            return;

        uint64 const guid = player->GetGUID().GetCounter();

        if (sSilithystSpeedLimitedPlayers.erase(guid) == 0)
            return;

        player->SetSpeedRate(MOVE_RUN, 1.0f);
        player->SetSpeedRate(MOVE_SWIM, 1.0f);
        player->SetSpeedRate(MOVE_FLIGHT, 1.0f);

        player->UpdateSpeed(MOVE_RUN);
        player->UpdateSpeed(MOVE_SWIM);
        player->UpdateSpeed(MOVE_FLIGHT);
    }

    void UpdateSilithystSpeedLimit(Player* player)
    {
        if (!player)
            return;

        if (HasSilithystCarryAura(player))
            ApplySilithystSpeedLimit(player);
        else
            RemoveSilithystSpeedLimit(player);
    }

    uint32 GetSilithystTeamFlagSpell(Player* player)
    {
        return player->GetTeam() == ALLIANCE ? SI_SILITHYST_FLAG_ALLIANCE_SPEED : SI_SILITHYST_FLAG_HORDE_SPEED;
    }

    uint32 GetSilithystOtherTeamFlagSpell(Player* player)
    {
        return player->GetTeam() == ALLIANCE ? SI_SILITHYST_FLAG_HORDE_SPEED : SI_SILITHYST_FLAG_ALLIANCE_SPEED;
    }

    void ApplySilithystTeamFlag(Player* player)
    {
        if (!player)
            return;

        uint32 const teamFlagSpell = GetSilithystTeamFlagSpell(player);
        uint32 const otherFlagSpell = GetSilithystOtherTeamFlagSpell(player);

        player->RemoveAurasDueToSpell(otherFlagSpell);

        if (!player->HasAura(teamFlagSpell))
            player->CastSpell(player, teamFlagSpell, true);

        if (!player->HasAura(teamFlagSpell))
            player->AddAura(teamFlagSpell, player);

        ApplySilithystSpeedLimit(player);
    }

    void RemoveSilithystTeamFlags(Player* player)
    {
        if (!player)
            return;

        player->RemoveAurasDueToSpell(SI_SILITHYST_FLAG_ALLIANCE_SPEED);
        player->RemoveAurasDueToSpell(SI_SILITHYST_FLAG_HORDE_SPEED);

        RemoveSilithystSpeedLimit(player);
    }

    void ApplySilithusServerTracesAuraDirect(Player* player, int32 durationMs)
    {
        if (!player || !player->IsInWorld())
            return;

        Aura* aura = player->GetAura(SI_TRACES_OF_SILITHYST);

        if (!aura)
            aura = player->AddAura(SI_TRACES_OF_SILITHYST, player);

        if (!aura)
            return;

        if (durationMs != 0)
        {
            aura->SetMaxDuration(durationMs);
            aura->SetDuration(durationMs);
        }
    }

    void ApplyPersonalTracesOfSilithyst(Player* player)
    {
        if (!player)
            return;

        CustomTeamBuffUtil::ApplySpellToPlayer(
            player,
            SI_TRACES_OF_SILITHYST,
            0,
            true,
            false,
            true,
            true
        );
    }

    void ApplySilithusServerTracesBuff(Player* player, TeamId teamId)
    {
        if (!player || !player->IsInWorld())
            return;

        if (player->GetTeamId() != teamId)
            return;

        RemoveInvalidPermanentSilithusTracesBuff(player, teamId);

        SilithusTracesBuffState const& state = GetSilithusTracesBuffState(teamId);

        if (!IsSilithusTracesBuffActive(state))
            return;

        int32 durationMs = GetSilithusTracesBuffDurationMs(state);
        if (durationMs == 0)
            return;

        ApplySilithusServerTracesAuraDirect(player, durationMs);
    }

    void ApplySilithusServerTracesBuffToTeam(TeamId teamId)
    {
        SilithusTracesBuffState const& state = GetSilithusTracesBuffState(teamId);

        if (!IsSilithusTracesBuffActive(state))
            return;

        int32 durationMs = GetSilithusTracesBuffDurationMs(state);
        if (durationMs == 0)
            return;

        SessionMap const& sessions = sWorld->GetAllSessions();

        for (SessionMap::const_iterator itr = sessions.begin(); itr != sessions.end(); ++itr)
        {
            WorldSession* session = itr->second;
            if (!session)
                continue;

            Player* target = session->GetPlayer();
            if (!target)
                continue;

            if (!target->IsInWorld())
                continue;

            if (target->GetTeamId() != teamId)
                continue;

            ApplySilithusServerTracesAuraDirect(target, durationMs);
        }
    }

    void ClearSilithusServerTracesBuff()
    {
        g_SilithusAllianceTracesBuff.Mode = SI_TRACES_BUFF_NONE;
        g_SilithusAllianceTracesBuff.Expires = 0;

        g_SilithusHordeTracesBuff.Mode = SI_TRACES_BUFF_NONE;
        g_SilithusHordeTracesBuff.Expires = 0;

        CustomTeamBuffUtil::RemoveSpellFromTeam(TEAM_ALLIANCE, SI_TRACES_OF_SILITHYST);
        CustomTeamBuffUtil::RemoveSpellFromTeam(TEAM_HORDE, SI_TRACES_OF_SILITHYST);
    }

    void SetSilithusServerTracesBuff(TeamId teamId, uint32 mode, uint32 durationSeconds, uint32 gatheredA, uint32 gatheredH)
    {
        SilithusTracesBuffState& state = GetSilithusTracesBuffState(teamId);

        state.Mode = mode;

        if (mode == SI_TRACES_BUFF_PERMANENT)
            state.Expires = 0;
        else if (durationSeconds > 0)
            state.Expires = GameTime::GetGameTime() + durationSeconds;
        else
            state.Expires = 0;

        SaveOutdoorPvPSIState(gatheredA, gatheredH);
        ApplySilithusServerTracesBuffToTeam(teamId);
    }

    void ApplyScoreDropFromMaxReward(TeamId teamId, uint32 beforeScore, uint32 afterScore, uint32 gatheredA, uint32 gatheredH)
    {
        if (beforeScore >= SI_MAX_RESOURCES && afterScore < SI_MAX_RESOURCES)
            SetSilithusServerTracesBuff(teamId, SI_TRACES_BUFF_12H, SI_TRACES_DURATION_12H_SEC, gatheredA, gatheredH);
    }

    void ApplyTracesRewardForTurnIn(Player* player, TeamId teamId, uint32 afterScore, uint32 gatheredA, uint32 gatheredH)
    {
        if (afterScore >= SI_MAX_RESOURCES)
        {
            SetSilithusServerTracesBuff(teamId, SI_TRACES_BUFF_PERMANENT, 0, gatheredA, gatheredH);
            return;
        }

        if (afterScore >= SI_PROTECTED_RESOURCES)
        {
            SetSilithusServerTracesBuff(teamId, SI_TRACES_BUFF_2H, SI_TRACES_DURATION_2H_SEC, gatheredA, gatheredH);
            return;
        }

        ApplyPersonalTracesOfSilithyst(player);
    }
}

OutdoorPvPSI::OutdoorPvPSI()
{
    m_TypeId = OUTDOOR_PVP_SI;
    m_Gathered_A = 0;
    m_Gathered_H = 0;
    m_LastController = 0;
}

void OutdoorPvPSI::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(SI_GATHERED_A, m_Gathered_A);
    packet.Worldstates.emplace_back(SI_GATHERED_H, m_Gathered_H);
    packet.Worldstates.emplace_back(SI_SILITHYST_MAX, SI_MAX_RESOURCES);
}

void OutdoorPvPSI::SendRemoveWorldStates(Player* player)
{
    player->SendUpdateWorldState(SI_GATHERED_A, 0);
    player->SendUpdateWorldState(SI_GATHERED_H, 0);
    player->SendUpdateWorldState(SI_SILITHYST_MAX, 0);
}

void OutdoorPvPSI::UpdateWorldState()
{
    SendUpdateWorldState(SI_GATHERED_A, m_Gathered_A);
    SendUpdateWorldState(SI_GATHERED_H, m_Gathered_H);
    SendUpdateWorldState(SI_SILITHYST_MAX, SI_MAX_RESOURCES);
}

bool OutdoorPvPSI::SetupOutdoorPvP()
{
    SetMapFromZone(OutdoorPvPSIBuffZones[0]);

    LoadOutdoorPvPSIState(m_Gathered_A, m_Gathered_H);

    for (uint8 i = 0; i < OutdoorPvPSIBuffZonesNum; ++i)
        RegisterZone(OutdoorPvPSIBuffZones[i]);

    return true;
}

bool OutdoorPvPSI::Update(uint32 diff)
{
    if (OutdoorPvpWar_ConsumeSilithusScoreResetRequest())
    {
        m_Gathered_A = 0;
        m_Gathered_H = 0;
        m_LastController = 0;

        ClearSilithusServerTracesBuff();

        SaveOutdoorPvPSIState(m_Gathered_A, m_Gathered_H);
        UpdateWorldState();
    }

    if (sSilithystSpeedTimer > diff)
    {
        sSilithystSpeedTimer -= diff;
        return false;
    }

    sSilithystSpeedTimer = SI_SILITHYST_SPEED_CHECK_TIMER;

    if (!m_map)
        return false;

    Map::PlayerList const& players = m_map->GetPlayers();
    if (players.isEmpty())
        return false;

    for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
    {
        Player* player = itr->GetSource();
        if (!player)
            continue;

        if (!player->IsInWorld())
            continue;

        if (!IsSilithusOutdoorPvPZone(player->GetZoneId()))
        {
            RemoveSilithystSpeedLimit(player);
            continue;
        }

        UpdateSilithystSpeedLimit(player);
    }

    return false;
}

void OutdoorPvPSI::HandlePlayerEnterZone(Player* player, uint32 zone)
{
    if (player->GetTeam() == m_LastController)
        player->CastSpell(player, SI_CENARION_FAVOR, true);

    ApplySilithusServerTracesBuff(player, player->GetTeamId());

    if (player->HasAura(SI_SILITHYST_FLAG))
    {
        ApplySilithystTeamFlag(player);
        ApplySilithystSpeedLimit(player);
    }

    OutdoorPvP::HandlePlayerEnterZone(player, zone);
}

void OutdoorPvPSI::HandlePlayerLeaveZone(Player* player, uint32 zone)
{
    player->RemoveAurasDueToSpell(SI_CENARION_FAVOR);
    RemoveSilithystTeamFlags(player);
    RemoveSilithystSpeedLimit(player);

    OutdoorPvP::HandlePlayerLeaveZone(player, zone);
}

bool OutdoorPvPSI::HandleAreaTrigger(Player* player, uint32 trigger)
{
    switch (trigger)
    {
        case SI_AREATRIGGER_A:
        {
            if (player->GetTeam() == ALLIANCE && player->HasAura(SI_SILITHYST_FLAG))
            {
                uint32 const beforeA = m_Gathered_A;
                uint32 const beforeH = m_Gathered_H;

                player->RemoveAurasDueToSpell(SI_SILITHYST_FLAG);
                RemoveSilithystTeamFlags(player);
                RemoveSilithystSpeedLimit(player);

                if (m_Gathered_A < SI_MAX_RESOURCES)
                    m_Gathered_A = std::min<uint32>(SI_MAX_RESOURCES, m_Gathered_A + SI_WAR_TURNIN_SCORE);

                uint32 const afterA = m_Gathered_A;
                uint32 const afterH = m_Gathered_H;

                if (beforeA < SI_MAX_RESOURCES && afterA >= SI_MAX_RESOURCES)
                {
                    TeamApplyBuff(TEAM_ALLIANCE, SI_CENARION_FAVOR);
                    m_map->SendZoneText(OutdoorPvPSIBuffZones[0], sObjectMgr->GetTrinityStringForDBCLocale(LANG_OPVP_SI_CAPTURE_A));
                    m_LastController = ALLIANCE;
                }

                SaveOutdoorPvPSIState(m_Gathered_A, m_Gathered_H);
                UpdateWorldState();

                ApplyTracesRewardForTurnIn(player, TEAM_ALLIANCE, afterA, m_Gathered_A, m_Gathered_H);
                ApplyScoreDropFromMaxReward(TEAM_HORDE, beforeH, afterH, m_Gathered_A, m_Gathered_H);

                player->RewardHonor(nullptr, 1, GetSilithystHonorReward(beforeA));
                player->GetReputationMgr().ModifyReputation(sFactionStore.LookupEntry(609), 20);
                player->KilledMonsterCredit(SI_TURNIN_QUEST_CM_A);

                OutdoorPvpWar_OnSilithusTurnIn(player, SI_WAR_TURNIN_SCORE);
            }

            return true;
        }
        case SI_AREATRIGGER_H:
        {
            if (player->GetTeam() == HORDE && player->HasAura(SI_SILITHYST_FLAG))
            {
                uint32 const beforeA = m_Gathered_A;
                uint32 const beforeH = m_Gathered_H;

                player->RemoveAurasDueToSpell(SI_SILITHYST_FLAG);
                RemoveSilithystTeamFlags(player);
                RemoveSilithystSpeedLimit(player);

                if (m_Gathered_H < SI_MAX_RESOURCES)
                    m_Gathered_H = std::min<uint32>(SI_MAX_RESOURCES, m_Gathered_H + SI_WAR_TURNIN_SCORE);

                uint32 const afterA = m_Gathered_A;
                uint32 const afterH = m_Gathered_H;

                if (beforeH < SI_MAX_RESOURCES && afterH >= SI_MAX_RESOURCES)
                {
                    TeamApplyBuff(TEAM_HORDE, SI_CENARION_FAVOR);
                    m_map->SendZoneText(OutdoorPvPSIBuffZones[0], sObjectMgr->GetTrinityStringForDBCLocale(LANG_OPVP_SI_CAPTURE_H));
                    m_LastController = HORDE;
                }

                SaveOutdoorPvPSIState(m_Gathered_A, m_Gathered_H);
                UpdateWorldState();

                ApplyTracesRewardForTurnIn(player, TEAM_HORDE, afterH, m_Gathered_A, m_Gathered_H);
                ApplyScoreDropFromMaxReward(TEAM_ALLIANCE, beforeA, afterA, m_Gathered_A, m_Gathered_H);

                player->RewardHonor(nullptr, 1, GetSilithystHonorReward(beforeH));
                player->GetReputationMgr().ModifyReputation(sFactionStore.LookupEntry(609), 20);
                player->KilledMonsterCredit(SI_TURNIN_QUEST_CM_H);

                OutdoorPvpWar_OnSilithusTurnIn(player, SI_WAR_TURNIN_SCORE);
            }

            return true;
        }
    }

    return false;
}

bool OutdoorPvPSI::HandleDropFlag(Player* player, uint32 spellId)
{
    if (spellId == SI_SILITHYST_FLAG)
    {
        RemoveSilithystTeamFlags(player);
        RemoveSilithystSpeedLimit(player);

        switch (player->GetTeam())
        {
            case ALLIANCE:
            {
                AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(SI_AREATRIGGER_A);
                if (atEntry)
                {
                    if (player->GetDistance(atEntry->Pos.X, atEntry->Pos.Y, atEntry->Pos.Z) > 5.0f + atEntry->Radius)
                    {
                        GameObject* go = new GameObject;
                        Map* map = player->GetMap();

                        if (!go->Create(map->GenerateLowGuid<HighGuid::GameObject>(), SI_SILITHYST_MOUND, map, player->GetPhaseMask(), *player, QuaternionData(), 255, GO_STATE_READY))
                        {
                            delete go;
                            return true;
                        }

                        go->SetRespawnTime(0);

                        if (!map->AddToMap(go))
                        {
                            delete go;
                            return true;
                        }
                    }
                }

                break;
            }
            case HORDE:
            {
                AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(SI_AREATRIGGER_H);
                if (atEntry)
                {
                    if (player->GetDistance(atEntry->Pos.X, atEntry->Pos.Y, atEntry->Pos.Z) > 5.0f + atEntry->Radius)
                    {
                        GameObject* go = new GameObject;
                        Map* map = player->GetMap();

                        if (!go->Create(map->GenerateLowGuid<HighGuid::GameObject>(), SI_SILITHYST_MOUND, map, player->GetPhaseMask(), *player, QuaternionData(), 255, GO_STATE_READY))
                        {
                            delete go;
                            return true;
                        }

                        go->SetRespawnTime(0);

                        if (!map->AddToMap(go))
                        {
                            delete go;
                            return true;
                        }
                    }
                }

                break;
            }
        }

        return true;
    }

    return false;
}

bool OutdoorPvPSI::HandleCustomSpell(Player* player, uint32 spellId, GameObject* go)
{
    if (!player)
        return false;

    if (spellId == SI_SILITHYST_PERIODIC_FLAG_TRIGGER)
    {
        if (player->HasAura(SI_SILITHYST_FLAG))
        {
            ApplySilithystTeamFlag(player);
            ApplySilithystSpeedLimit(player);
        }
        else
        {
            RemoveSilithystTeamFlags(player);
            RemoveSilithystSpeedLimit(player);
        }

        return true;
    }

    if (!go || spellId != SI_SILITHYST_FLAG_GO_SPELL)
        return false;

    player->CastSpell(player, SI_SILITHYST_FLAG, true);

    if (player->HasAura(SI_SILITHYST_FLAG))
    {
        ApplySilithystTeamFlag(player);
        ApplySilithystSpeedLimit(player);
    }

    if (go->GetGOInfo()->entry == SI_SILITHYST_MOUND)
    {
        go->SetRespawnTime(0);
        go->Delete();
    }

    return true;
}

class player_silithus_server_traces_buff : public PlayerScript
{
public:
    player_silithus_server_traces_buff() : PlayerScript("player_silithus_server_traces_buff") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        if (!player)
            return;

        ApplySilithusServerTracesBuff(player, player->GetTeamId());
    }

    void OnMapChanged(Player* player) override
    {
        if (!player)
            return;

        ApplySilithusServerTracesBuff(player, player->GetTeamId());
    }
};

class OutdoorPvP_silithus : public OutdoorPvPScript
{
public:
    OutdoorPvP_silithus() : OutdoorPvPScript("outdoorpvp_si") { }

    OutdoorPvP* GetOutdoorPvP() const override
    {
        return new OutdoorPvPSI();
    }
};

void AddSC_outdoorpvp_si()
{
    new OutdoorPvP_silithus();
    new player_silithus_server_traces_buff();
}