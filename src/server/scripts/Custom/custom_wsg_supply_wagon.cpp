// STIMPACK custom Warsong supply wagon reward scripts.
// The moving wagon, drop scheduling, reward timers and route belong to BattlegroundWS.
// This file only handles the clickable crate and siege-only damage modifier.

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Player.h"
#include "Creature.h"
#include "Unit.h"
#include "Battleground.h"
#include "BattlegroundWS.h"

#include <algorithm>
#include <limits>

namespace
{
    class go_custom_wsg_supply_crate : public GameObjectScript
    {
    public:
        go_custom_wsg_supply_crate() : GameObjectScript("go_custom_wsg_supply_crate") { }

        struct AI : public GameObjectAI
        {
            explicit AI(GameObject* go) : GameObjectAI(go) { }

            bool OnGossipHello(Player* player) override
            {
                if (!player)
                    return true;

                Battleground* bg = player->GetBattleground();
                if (bg && bg->GetTypeID() == BATTLEGROUND_WS && bg->IsCustomWarsong())
                    static_cast<BattlegroundWS*>(bg)->ClaimSupplyCrate(player, me->GetGUID());

                // No loot window, currency, inventory item, or repeat claim.
                return true;
            }
        };

        GameObjectAI* GetAI(GameObject* go) const override
        {
            return new AI(go);
        }
    };

    class custom_wsg_supply_siege_damage : public UnitScript
    {
    public:
        custom_wsg_supply_siege_damage()
            : UnitScript("custom_wsg_supply_siege_damage") { }

        void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
        {
            if (!attacker || !victim || !damage)
                return;

            Creature* creature = victim->ToCreature();
            if (!creature)
                return;

            uint32 entry = creature->GetEntry();
            // Do not increase damage to other players or unrelated PvE creatures.
            // Extend this whitelist when the custom WSG gates and bosses are implemented.
            if (entry != 960510 && entry != 960511 && entry != 960512 && entry != 960513)
                return;

            Player* player = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
            if (!player)
                return;

            Battleground* bg = player->GetBattleground();
            if (!bg || bg->GetTypeID() != BATTLEGROUND_WS || !bg->IsCustomWarsong())
                return;

            if (!static_cast<BattlegroundWS*>(bg)->HasSupplySiegeBuff(player))
                return;

            damage = uint32(std::min<uint64>(uint64(damage) * 3 / 2,
                std::numeric_limits<uint32>::max()));
        }
    };
}

void AddSC_custom_wsg_supply_wagon()
{
    new go_custom_wsg_supply_crate();
    new custom_wsg_supply_siege_damage();
}
