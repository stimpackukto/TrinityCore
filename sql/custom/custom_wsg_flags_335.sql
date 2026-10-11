-- STIMPACK Custom Warsong - attackable BATTLE STANDARD creatures (map 489).
-- Apply to configured WORLD database and restart the worldserver.
-- Normal Warsong battleground, its gameobjects, and original capture flags
-- remain unchanged. The four custom siege flags are spawned by BattlegroundWS.cpp.
--
-- Why this update is necessary:
-- GameObject flag + invisible NPC (display 11686) cannot be targeted for
-- normal melee attacks or hostile spells by the 3.3.5 client.
-- The siege flags therefore use the actual CreatureDisplayInfo models of
-- existing Icecrown Citadel battle standard creatures, with attackable HP.
--
-- Reference NPC: Kor'kron Battle Standard (37044) for Horde;
--                Skybreaker Battle Standard (37041) for Alliance.
-- Inspect their modelid1 values BEFORE applying. Both must be valid visible
-- CreatureDisplayInfo IDs (not 0 or 11686).
SELECT entry, name, modelid1, modelid2
FROM creature_template WHERE entry IN (37044, 37041);

INSERT INTO `creature_template`
  (`entry`, `modelid1`, `name`, `minlevel`, `maxlevel`, `faction`,
   `npcflag`, `speed_walk`, `speed_run`, `scale`, `rank`,
   `unit_class`, `unit_flags`, `unit_flags2`, `dynamicflags`,
   `type`, `type_flags`, `HealthModifier`, `RegenHealth`,
   `AIName`, `ScriptName`)
SELECT 960510, `modelid1`, '호드 공성 방어 깃발 1', 80, 80, 1735,
    0, 1, 1.14286, 1, 0, 1, 0, 0, 0, 9, 0, 1, 0, '', ''
FROM `creature_template` WHERE `entry` = 37044
  AND `modelid1` NOT IN (0, 11686)
ON DUPLICATE KEY UPDATE
  `modelid1` = VALUES(`modelid1`), `modelid2` = 0, `modelid3` = 0, `modelid4` = 0,
  `name` = VALUES(`name`),
  `faction` = VALUES(`faction`),
  `unit_flags` = 0,
  `npcflag` = 0,
  `RegenHealth` = 0,
  `scale` = 1,
  `AIName` = '',
  `ScriptName` = '';

INSERT INTO `creature_template`
  (`entry`, `modelid1`, `name`, `minlevel`, `maxlevel`, `faction`,
   `npcflag`, `speed_walk`, `speed_run`, `scale`, `rank`,
   `unit_class`, `unit_flags`, `unit_flags2`, `dynamicflags`,
   `type`, `type_flags`, `HealthModifier`, `RegenHealth`,
   `AIName`, `ScriptName`)
SELECT 960511, `modelid1`, '호드 공성 방어 깃발 2', 80, 80, 1735,
    0, 1, 1.14286, 1, 0, 1, 0, 0, 0, 9, 0, 1, 0, '', ''
FROM `creature_template` WHERE `entry` = 37044
  AND `modelid1` NOT IN (0, 11686)
ON DUPLICATE KEY UPDATE
  `modelid1` = VALUES(`modelid1`), `modelid2` = 0, `modelid3` = 0, `modelid4` = 0,
  `name` = VALUES(`name`),
  `faction` = VALUES(`faction`),
  `unit_flags` = 0,
  `npcflag` = 0,
  `RegenHealth` = 0,
  `scale` = 1,
  `AIName` = '',
  `ScriptName` = '';

INSERT INTO `creature_template`
  (`entry`, `modelid1`, `name`, `minlevel`, `maxlevel`, `faction`,
   `npcflag`, `speed_walk`, `speed_run`, `scale`, `rank`,
   `unit_class`, `unit_flags`, `unit_flags2`, `dynamicflags`,
   `type`, `type_flags`, `HealthModifier`, `RegenHealth`,
   `AIName`, `ScriptName`)
SELECT 960512, `modelid1`, '얼라이언스 공성 방어 깃발 1', 80, 80, 1732,
    0, 1, 1.14286, 1, 0, 1, 0, 0, 0, 9, 0, 1, 0, '', ''
FROM `creature_template` WHERE `entry` = 37041
  AND `modelid1` NOT IN (0, 11686)
ON DUPLICATE KEY UPDATE
  `modelid1` = VALUES(`modelid1`), `modelid2` = 0, `modelid3` = 0, `modelid4` = 0,
  `name` = VALUES(`name`),
  `faction` = VALUES(`faction`),
  `unit_flags` = 0,
  `npcflag` = 0,
  `RegenHealth` = 0,
  `scale` = 1,
  `AIName` = '',
  `ScriptName` = '';

INSERT INTO `creature_template`
  (`entry`, `modelid1`, `name`, `minlevel`, `maxlevel`, `faction`,
   `npcflag`, `speed_walk`, `speed_run`, `scale`, `rank`,
   `unit_class`, `unit_flags`, `unit_flags2`, `dynamicflags`,
   `type`, `type_flags`, `HealthModifier`, `RegenHealth`,
   `AIName`, `ScriptName`)
SELECT 960513, `modelid1`, '얼라이언스 공성 방어 깃발 2', 80, 80, 1732,
    0, 1, 1.14286, 1, 0, 1, 0, 0, 0, 9, 0, 1, 0, '', ''
FROM `creature_template` WHERE `entry` = 37041
  AND `modelid1` NOT IN (0, 11686)
ON DUPLICATE KEY UPDATE
  `modelid1` = VALUES(`modelid1`), `modelid2` = 0, `modelid3` = 0, `modelid4` = 0,
  `name` = VALUES(`name`),
  `faction` = VALUES(`faction`),
  `unit_flags` = 0,
  `npcflag` = 0,
  `RegenHealth` = 0,
  `scale` = 1,
  `AIName` = '',
  `ScriptName` = '';

-- Remove only obsolete hut test templates, never other world spawns.
DELETE FROM `creature_template`
WHERE (`entry` = 960500 AND `name` = '얼라이언스 전방 방어 군막')
   OR (`entry` = 960501 AND `name` = '호드 전방 방어 군막');

-- Verify the 4 siege NPCs are visible models after running this script.
-- Four rows must appear and modelid1 must NOT be 0 or 11686.
SELECT `entry`, `name`, `modelid1`, `faction`, `unit_flags`, `RegenHealth`
FROM `creature_template`
WHERE `entry` IN (960510, 960511, 960512, 960513)
ORDER BY `entry`;
