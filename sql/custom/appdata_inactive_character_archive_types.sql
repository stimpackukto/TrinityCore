-- Apply ONCE to appdata BEFORE deploying the updated TrinityCore worldserver.
-- Existing archive and lock records receive the legacy six-month category by default.
-- Archive categories: under40_1month (legacy DB identifier; level 1-39, inactive >= 6 months);
-- inactive_6months (legacy DB identifier; level 40+, inactive >= 2 years, or legacy records).
-- This migration does not delete or unlock any character.

ALTER TABLE appdata.inactive_character_lock
    ADD COLUMN archive_type VARCHAR(32) NOT NULL DEFAULT 'inactive_6months';

ALTER TABLE appdata.character_archive_log
    ADD COLUMN archive_type VARCHAR(32) NOT NULL DEFAULT 'inactive_6months';

-- Optional reporting query (execute separately):
-- SELECT archive_type, COUNT(*) AS locked_characters
-- FROM appdata.inactive_character_lock
-- WHERE active = 1
-- GROUP BY archive_type;
