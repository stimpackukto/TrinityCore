-- TrinityCore 3.3.5a - near-invisible treasure discovery marker (900411 only).
-- Run on the same MySQL server as world_test, then restart worldserver.
-- 271 is the Briarthorn GameObject model, verified in this world's herb templates.
-- Keep the real model for click targeting, but shrink its geometry almost out
-- of view (previous size: 0.25). This is NOT true texture transparency.
-- The quest-style sparkle is supplied by GameObject.cpp (GO dynamic flags).
-- If the tiny model makes clicking/sparkling unreliable, restore size = 0.25.
-- Do not alter treasure chests (900410), normal herbs, or any raid instance locks.

UPDATE `world_test`.`gameobject_template` AS marker
INNER JOIN `world_test`.`gameobject_template` AS herb
    ON herb.`entry` = 1621
    AND herb.`type` = 3
    AND herb.`displayId` = 271
SET
    marker.`displayId` = herb.`displayId`,
    marker.`size` = 0.02
WHERE marker.`entry` = 900411
  AND marker.`type` = 10
  AND marker.`ScriptName` = 'go_custom_treasure_hunt_sparkle';

SELECT `entry`, `type`, `displayId`, `name`, `size`, `ScriptName`
FROM `world_test`.`gameobject_template`
WHERE `entry` = 900411;
