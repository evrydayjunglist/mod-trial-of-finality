-- Create table for logging Trial of Finality events
CREATE TABLE IF NOT EXISTS `trial_of_finality_log` (
    `log_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `event_timestamp` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `event_type` VARCHAR(50) NOT NULL COMMENT 'E.g., TRIAL_START, PLAYER_DEATH, WAVE_START, TRIAL_SUCCESS, TRIAL_FAILURE, GM_COMMAND',
    `group_id` INT UNSIGNED DEFAULT NULL,
    `player_guid` INT UNSIGNED DEFAULT NULL COMMENT 'Character GUID of relevant player (e.g. leader, victim)',
    `player_name` VARCHAR(12) DEFAULT NULL COMMENT 'Name of relevant player',
    `player_account_id` INT UNSIGNED DEFAULT NULL COMMENT 'Account ID of relevant player',
    `highest_level_in_group` TINYINT UNSIGNED DEFAULT NULL,
    `wave_number` TINYINT UNSIGNED DEFAULT NULL,
    `details` TEXT DEFAULT NULL COMMENT 'Additional details, e.g., reason for failure, GM command parameters',
    PRIMARY KEY (`log_id`),
    INDEX `idx_event_type` (`event_type`),
    INDEX `idx_group_id` (`group_id`),
    INDEX `idx_player_guid` (`player_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Logs events related to the Trial of Finality module.';
