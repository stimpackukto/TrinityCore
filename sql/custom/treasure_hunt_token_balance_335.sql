-- TrinityCore 3.3.5a / Ruby Sanctum Treasure Hunt - account-wide treasure tokens.
-- Apply once to the SAME MySQL server containing appdata and characters_test,
-- BEFORE rebuilding / restarting worldserver with the token settlement code.
--
-- One treasure held at event completion awards 1 account-wide token,
-- IN ADDITION to the existing configurable RP-per-treasure reward.
-- Unclaimed treasure and treasure lost on death never create token balance.
-- This script does not grant retroactive tokens for past events.
-- Token spending / token shop is intentionally a separate future feature.
--
-- This creates no WoW item_template item and does not affect inventory.
-- Existing legacy item-loot / mail-escrow tables are left untouched, but
-- the revised event code no longer reads or writes them.

CREATE TABLE IF NOT EXISTS `appdata`.`treasure_token_balance` (
    `account_id` INT UNSIGNED NOT NULL,
    `balance` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `total_earned` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Optional balance verification (no rows until tokens are first awarded).
SELECT `account_id`, `balance`, `total_earned`, `updated_at`
FROM `appdata`.`treasure_token_balance`
ORDER BY `account_id`;
