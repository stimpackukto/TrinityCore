-- TrinityCore 3.3.5a / Ruby Sanctum Treasure Hunt - hidden sparkle discovery sites
-- Run ONCE (safe to rerun) against the same MySQL server hosting world_test.
-- This creates only a new sparkle GO template (900411), not any world spawns.
-- Existing GO 900410, rewards, balances and event configuration remain unchanged.
--
-- The sparkle model is selected from EXISTING display IDs in this world's DB.
-- No unverified display ID is hard-coded; inspect the verification SELECT below.
-- If no suitable visual exists, insert nothing and select a valid sparkle model manually.
-- Both 900410 and 900411 must exist BEFORE restarting the rebuilt worldserver.

SELECT `entry`, `name`, `type`, `displayId`
FROM `world_test`.`gameobject_template`
WHERE `displayId` > 0
  AND LOWER(`name`) REGEXP 'sparkle|shimmer|glimmer|glow|spark|aura|wisp'
  AND LOWER(`name`) NOT LIKE '%chest%'
ORDER BY
    CASE
        WHEN LOWER(`name`) LIKE '%sparkle%' THEN 0
        WHEN LOWER(`name`) LIKE '%shimmer%' THEN 1
        WHEN LOWER(`name`) LIKE '%glimmer%' THEN 2
        WHEN LOWER(`name`) LIKE '%spark%' THEN 3
        WHEN LOWER(`name`) LIKE '%glow%' THEN 4
        ELSE 5
    END, `entry`
LIMIT 30;

INSERT INTO `world_test`.`gameobject_template`
    (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`,
     `size`, `data0`, `data1`, `data2`, `data3`, `AIName`, `ScriptName`)
SELECT
    900411, 10, t.`displayId`, '반짝이는 보물의 흔적', '', '', '',
    0.6, 0, 0, 0, 3000, '', 'go_custom_treasure_hunt_sparkle'
FROM `world_test`.`gameobject_template` t
WHERE t.`displayId` > 0
  AND LOWER(t.`name`) REGEXP 'sparkle|shimmer|glimmer|glow|spark|aura|wisp'
  AND LOWER(t.`name`) NOT LIKE '%chest%'
  AND NOT EXISTS (
      SELECT 1 FROM `world_test`.`gameobject_template` WHERE `entry` = 900411
  )
ORDER BY
    CASE
        WHEN LOWER(t.`name`) LIKE '%sparkle%' THEN 0
        WHEN LOWER(t.`name`) LIKE '%shimmer%' THEN 1
        WHEN LOWER(t.`name`) LIKE '%glimmer%' THEN 2
        WHEN LOWER(t.`name`) LIKE '%spark%' THEN 3
        WHEN LOWER(t.`name`) LIKE '%glow%' THEN 4
        ELSE 5
    END, t.`entry`
LIMIT 1;

-- Verify that the new object exists and has the intended script/model.
SELECT `entry`, `type`, `displayId`, `name`, `size`, `ScriptName`
FROM `world_test`.`gameobject_template`
WHERE `entry` IN (900410, 900411)
ORDER BY `entry`;
