-- TrinityCore 3.3.5a / Ruby Sanctum Treasure Hunt - treasure chests
-- RUN THIS ON THE SAME MYSQL SERVER AS world_test, characters_test AND appdata.
-- Apply BEFORE rebuilding/restarting worldserver.
-- World DB name in this deployment: world_test.
-- The GO model is copied from an EXISTING local chest template; no unverified display ID is guessed.
-- 900410 must be unused. Verify with SELECT entry, name FROM world_test.gameobject_template WHERE entry = 900410;
-- Player accounts receive RP in appdata.account_data.reg_point only at event end.
-- Existing disguises, event settings, and rewards are not reset by this script.

CREATE TABLE IF NOT EXISTS `characters_test`.`custom_treasure_hunt_treasure_config` (
    `id` TINYINT UNSIGNED NOT NULL,
    `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `respawn_seconds` INT UNSIGNED NOT NULL DEFAULT 20,
    `max_active_chests` INT UNSIGNED NOT NULL DEFAULT 20,
    `min_treasure` INT UNSIGNED NOT NULL DEFAULT 1,
    `max_treasure` INT UNSIGNED NOT NULL DEFAULT 3,
    `rp_per_treasure` INT UNSIGNED NOT NULL DEFAULT 500,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO `characters_test`.`custom_treasure_hunt_treasure_config`
    (`id`, `enabled`, `respawn_seconds`, `max_active_chests`, `min_treasure`, `max_treasure`, `rp_per_treasure`)
VALUES (1, 1, 20, 20, 1, 3, 500);

-- Update existing installations: INSERT IGNORE above does not update an existing row.
UPDATE `characters_test`.`custom_treasure_hunt_treasure_config`
SET `max_active_chests` = 20
WHERE `id` = 1 AND `max_active_chests` <> 20;

-- Set the new event reward rate on existing servers as well as clean installs.
-- Every eligible participant receives base 1000 RP and 3 Emblems of Triumph,
-- plus 500 RP, 1 Emblem of Triumph, and 1 treasure token per held treasure.
UPDATE `characters_test`.`custom_treasure_hunt_treasure_config`
SET `rp_per_treasure` = 500
WHERE `id` = 1 AND `rp_per_treasure` <> 500;

-- Verify the Emblem of Triumph (47241) exists in the 3.3.5a world database.
SELECT `entry`, `name` FROM `world_test`.`item_template` WHERE `entry` = 47241;

CREATE TABLE IF NOT EXISTS `characters_test`.`custom_treasure_hunt_treasure_balance` (
    `event_id` BIGINT UNSIGNED NOT NULL,
    `guid` INT UNSIGNED NOT NULL,
    `account_id` INT UNSIGNED NOT NULL,
    `held_treasure` INT UNSIGNED NOT NULL DEFAULT 0,
    `awarded_rp` INT UNSIGNED NOT NULL DEFAULT 0,
    `settled` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`event_id`, `guid`),
    KEY `idx_event_settled` (`event_id`, `settled`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Type 10 = GOOBER, which calls the GameObjectAI OnGossipHello callback on use.
-- Select a display ID from an existing valid 3.3.5a CHEST in this server's own world DB.
-- Does not overwrite a pre-existing entry.
INSERT INTO `world_test`.`gameobject_template`
    (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`,
     `size`, `data0`, `data1`, `data2`, `data3`, `AIName`, `ScriptName`)
SELECT
    900410, 10, t.`displayId`, '성소의 보물상자', '', '', '',
    1, 0, 0, 0, 3000, '', 'go_custom_treasure_hunt_chest'
FROM `world_test`.`gameobject_template` t
WHERE t.`type` = 3 AND t.`displayId` > 0
  AND NOT EXISTS (SELECT 1 FROM `world_test`.`gameobject_template` WHERE `entry` = 900410)
ORDER BY
    CASE WHEN t.`name` LIKE '%Treasure Chest%' THEN 0
         WHEN t.`name` LIKE '%Chest%' THEN 1 ELSE 2 END,
    t.`entry`
LIMIT 1;

-- Verify before enabling the event. If no row is returned, the source world DB
-- has no suitable CHEST; choose a verified local chest model and create entry 900410.
SELECT `entry`, `type`, `displayId`, `name`, `ScriptName`
FROM `world_test`.`gameobject_template` WHERE `entry` = 900410;
SELECT * FROM `characters_test`.`custom_treasure_hunt_treasure_config` WHERE id = 1;
