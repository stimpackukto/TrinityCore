-- TrinityCore 3.3.5a - Ruby Sanctum Treasure Hunt deferred item rewards.
-- Apply ONCE before rebuilding worldserver. Item loot rolls occur only after the
-- existing 30-second treasure-opening action has completed.
-- No items are put into a character's bags during the event.
-- On death, ALL held reward items move into the same dropped treasure chest.
-- Only participants who complete the event receive items, via reward mail.
-- Character item escrow and the mail-settlement marker prevent duplicate payment.
--
-- The first table uses the world DB. Item IDs are checked against the loaded
-- item templates at event start; missing/invalid items are skipped.

CREATE TABLE IF NOT EXISTS `world_test`.`custom_treasure_hunt_item_loot` (
    `item_entry` INT UNSIGNED NOT NULL,
    `chance_weight` INT UNSIGNED NOT NULL DEFAULT 1,
    `min_count` INT UNSIGNED NOT NULL DEFAULT 1,
    `max_count` INT UNSIGNED NOT NULL DEFAULT 1,
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    PRIMARY KEY (`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Exactly one item group is rolled per freshly discovered chest:
-- common 70%, uncommon 25%, rare 5%. Values are weights, not separate rolls.
-- INSERT IGNORE preserves any probability adjustments made by the operator.
INSERT IGNORE INTO `world_test`.`custom_treasure_hunt_item_loot`
    (`item_entry`, `chance_weight`, `min_count`, `max_count`, `enabled`)
VALUES
    (33470, 22, 5, 10, 1), -- Frostweave Cloth
    (36912, 16, 5, 10, 1), -- Saronite Ore
    (36908, 12, 1,  2, 1), -- Frost Lotus
    (36860, 10, 1,  2, 1), -- Eternal Fire
    (43102, 10, 1,  1, 1), -- Frozen Orb
    (34057,  8, 1,  1, 1), -- Abyss Crystal
    (44128,  6, 1,  1, 1), -- Arctic Fur
    (37663,  5, 1,  1, 1), -- Titansteel Bar
    (36919,  4, 1,  1, 1), -- Cardinal Ruby (verify local item_template)
    (36922,  2, 1,  1, 1), -- King's Amber (verify local item_template)
    (47556,  4, 1,  1, 1), -- Crusader Orb
    (49908,  1, 1,  1, 1); -- Primordial Saronite

-- Items are held as escrow and are NOT owned inventory items until event end.
CREATE TABLE IF NOT EXISTS `characters_test`.`custom_treasure_hunt_item_escrow` (
    `event_id` BIGINT UNSIGNED NOT NULL,
    `guid` INT UNSIGNED NOT NULL,
    `item_entry` INT UNSIGNED NOT NULL,
    `item_count` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`event_id`, `guid`, `item_entry`),
    KEY `idx_guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Settlement and mail inserts are committed within the SAME character DB
-- transaction, so retries cannot award this event twice.
CREATE TABLE IF NOT EXISTS `characters_test`.`custom_treasure_hunt_item_settlement` (
    `event_id` BIGINT UNSIGNED NOT NULL,
    `guid` INT UNSIGNED NOT NULL,
    `settled_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`event_id`, `guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

SELECT * FROM `world_test`.`custom_treasure_hunt_item_loot` ORDER BY `item_entry`;
