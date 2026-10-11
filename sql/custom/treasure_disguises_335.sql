-- TrinityCore 3.3.5a / Ruby Sanctum Treasure Hunt disguise
-- Run on the same MySQL instance as the configured CharacterDatabase.
-- Account RP already lives in appdata.account_data.reg_point.
-- No RP is charged by this migration or by the disguise USE command.
-- Server and addon support 5 kinds; additional cosmetics require RP shop unlocks.
-- IMPORTANT: display_id MUST be a CreatureDisplayInfo.dbc ID, NOT a
-- gameobject_template displayId or the ID of a spell/item.
-- All display IDs default to 0 (disabled) until verified on the 3.3.5a client.
-- Do not copy random guessed model IDs into a live character appearance.

CREATE TABLE IF NOT EXISTS `appdata`.`treasure_disguise_models` (
    `disguise_id` TINYINT UNSIGNED NOT NULL,
    `name` VARCHAR(64) NOT NULL,
    `display_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `suggested_rp_price` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`disguise_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO `appdata`.`treasure_disguise_models`
    (`disguise_id`, `name`, `display_id`, `suggested_rp_price`)
VALUES
    (1, '붉은 꽃', 0, 0),
    (2, '붉은 덤불', 0, 300),
    (3, '성소의 바위', 0, 600),
    (4, '붉은 묘목', 0, 1000),
    (5, '붉은용의 알', 0, 1500)
ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);

-- Permanent PER-CHARACTER unlocks. The flower is always free in C++.
-- Character GUID is characters.guid (not the account ID).
CREATE TABLE IF NOT EXISTS `appdata`.`treasure_disguise_character_unlocks` (
    `character_guid` INT UNSIGNED NOT NULL,
    `disguise_id` TINYINT UNSIGNED NOT NULL,
    `unlocked_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`character_guid`, `disguise_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Previous account-wide table appdata.treasure_disguise_unlocks is
-- preserved unchanged as historical data. Its rows DO NOT unlock a
-- disguise under the new character-specific rules. Do not automatically
-- copy each account unlock to every character.
--
-- Once model IDs are verified, set them manually, e.g.:
-- UPDATE appdata.treasure_disguise_models SET display_id = <VERIFIED_CREATURE_DISPLAY_ID> WHERE disguise_id = 1;
-- Test ONE character unlock without deducting RP (GM/test only):
-- INSERT IGNORE INTO appdata.treasure_disguise_character_unlocks (character_guid, disguise_id)
-- VALUES (<TEST_CHARACTER_GUID>, 2);
-- RP shop implementation must charge account reg_point and grant only
-- the BUYING CHARACTER'S unlock in a consistent transaction.
-- The prices above are proposals only, NOT an active checkout system.

SELECT `disguise_id`, `name`, `display_id`, `suggested_rp_price`
FROM `appdata`.`treasure_disguise_models`
ORDER BY `disguise_id`;
