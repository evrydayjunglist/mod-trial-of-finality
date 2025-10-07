-- ----------------------------------------------------------------------------
-- Instance Template for the Trial of Finality
-- ----------------------------------------------------------------------------
-- IMPORTANT: The `map` ID below (defaulting to 0 for Gurubashi Arena) MUST
-- match the `TrialOfFinality.Arena.MapID` setting in your .conf file.
-- If you change the arena map ID in the config, you MUST update it here too.
-- ----------------------------------------------------------------------------
DELETE FROM `instance_template` WHERE `map` = 0 AND `script` = 'instance_trial_of_finality';
INSERT INTO `instance_template` (`map`, `parent`, `script`, `allowMount`) VALUES
(0, 0, 'instance_trial_of_finality', 0);