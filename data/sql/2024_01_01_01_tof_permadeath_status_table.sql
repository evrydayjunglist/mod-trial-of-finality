CREATE TABLE IF NOT EXISTS `character_trial_finality_status` (
  `guid` INT UNSIGNED NOT NULL COMMENT 'Character GUID',
  `is_perma_failed` TINYINT(1) UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = Not failed, 1 = Perma-failed in Trial of Finality',
  `last_failed_timestamp` TIMESTAMP NULL DEFAULT NULL COMMENT 'Timestamp of when perma-death was applied. Updated when is_perma_failed is set to 1.',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Stores perma-death status for the Trial of Finality module';

-- Optional: Add an update trigger to automatically set last_failed_timestamp
-- This is good practice but might be overkill if C++ logic handles timestamp updates explicitly.
-- For simplicity, we'll assume C++ handles the timestamp update for now, as designed.
-- DELIMITER //
-- CREATE TRIGGER `before_update_character_trial_finality_status`
-- BEFORE UPDATE ON `character_trial_finality_status`
-- FOR EACH ROW
-- BEGIN
--     IF NEW.is_perma_failed = 1 AND OLD.is_perma_failed = 0 THEN
--         SET NEW.last_failed_timestamp = NOW();
--     END IF;
-- END;//
-- DELIMITER ;
