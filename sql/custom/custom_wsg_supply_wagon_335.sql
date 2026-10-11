-- STIMPACK CUSTOM WARSONG - central supply wagon and clickable supply crate
-- Apply to the configured WORLD database (NOT characters/appdata).
-- No entries are added to the global creature or gameobject spawn tables.
-- The wagon (960520) and crate (960521) exist only in custom WSG instances.
--
-- IMPORTANT: Warcraft 3.3.5 NPC wagon appearance varies between server DBs.
-- We deliberately select a VISIBLE existing wagon/cart model from your WORLD DB.
-- Check the following candidate rows before running the inserts. If no cart-like
-- model exists, add a valid creature model manually rather than using a dummy.
SELECT entry, name, modelid1 FROM creature_template
WHERE modelid1 > 0
  AND LOWER(name) REGEXP 'wagon|cart|caravan|hauler|demolisher'
ORDER BY CASE
    WHEN LOWER(name) LIKE '%wagon%' THEN 0
    WHEN LOWER(name) LIKE '%cart%' THEN 1
    WHEN LOWER(name) LIKE '%hauler%' THEN 2
    WHEN LOWER(name) LIKE '%caravan%' THEN 3
    ELSE 4 END, entry
LIMIT 30;

INSERT INTO creature_template
    (entry, modelid1, name, minlevel, maxlevel, faction,
     npcflag, speed_walk, speed_run, scale, rank,
     unit_class, unit_flags, unit_flags2, dynamicflags,
     type, type_flags, HealthModifier, RegenHealth,
     AIName, ScriptName)
SELECT
    960520, t.modelid1, '중앙 전장 보급 마차', 80, 80, 14,
    0, 1, 1.14286, 1.0, 0, 1, 0, 0, 0, 9, 0, 1, 0, '', ''
FROM creature_template t
WHERE t.modelid1 > 0
  AND LOWER(t.name) REGEXP 'wagon|cart|caravan|hauler|demolisher'
ORDER BY CASE
    WHEN LOWER(t.name) LIKE '%wagon%' THEN 0
    WHEN LOWER(t.name) LIKE '%cart%' THEN 1
    WHEN LOWER(t.name) LIKE '%hauler%' THEN 2
    WHEN LOWER(t.name) LIKE '%caravan%' THEN 3
    ELSE 4 END, t.entry
LIMIT 1
ON DUPLICATE KEY UPDATE
    modelid1 = VALUES(modelid1),
    name = VALUES(name), faction = 14, unit_flags = 0,
    npcflag = 0, RegenHealth = 0, AIName = '', ScriptName = '';

-- The crate is a GOOBER, so GameObject::Use invokes GameObjectAI::OnGossipHello
-- before opening any loot window. It gives a team-wide timed BUFF, no items.
-- Use an existing crate display ID only; keep all existing GO templates untouched.
SELECT entry, name, displayId FROM gameobject_template
WHERE displayId > 0
  AND (LOWER(name) LIKE '%crate%' OR LOWER(name) LIKE '%supply chest%')
ORDER BY CASE WHEN LOWER(name) LIKE '%supply crate%' THEN 0
              WHEN LOWER(name) LIKE '%crate%' THEN 1 ELSE 2 END, entry
LIMIT 30;

INSERT INTO gameobject_template
    (entry, type, displayId, name, IconName, castBarCaption, unk1,
     size, data0, data1, data2, data3, AIName, ScriptName)
SELECT
    960521, 10, t.displayId, '중앙 전장 보급 상자', '', '', '',
    1.0, 0, 0, 0, 3000, '', 'go_custom_wsg_supply_crate'
FROM gameobject_template t
WHERE t.displayId > 0
  AND (LOWER(t.name) LIKE '%crate%' OR LOWER(t.name) LIKE '%supply chest%')
ORDER BY CASE WHEN LOWER(t.name) LIKE '%supply crate%' THEN 0
              WHEN LOWER(t.name) LIKE '%crate%' THEN 1 ELSE 2 END, t.entry
LIMIT 1
ON DUPLICATE KEY UPDATE
    type = 10, displayId = VALUES(displayId),
    name = VALUES(name), ScriptName = 'go_custom_wsg_supply_crate',
    data0 = 0, data1 = 0, data2 = 0, data3 = 3000, AIName = '';

-- Both results must contain exactly one row. modelid1 / displayId must be nonzero.
SELECT entry, name, modelid1, faction, unit_flags FROM creature_template WHERE entry = 960520;
SELECT entry, name, displayId, type, ScriptName FROM gameobject_template WHERE entry = 960521;
