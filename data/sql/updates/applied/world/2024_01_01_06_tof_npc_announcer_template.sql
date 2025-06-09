DELETE FROM `creature_template` WHERE `entry`=90002;
INSERT INTO `creature_template`
(`entry`, `modelid1`, `name`, `subname`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`, `unit_class`, `unit_flags`, `type`, `type_flags`, `RegenHealth`, `ScriptName`, `mechanic_immune_mask`, `flags_extra`)
VALUES
(90002, 24920, 'Trial Announcer', 'Voice of Finality', 0, 70, 70, 35, 0, 1.0, 1.14, 1.0, 1, 2048, 7, 0, 1, 'npc_trial_announcer', 32767, 2);
-- Notes: entry=90002 (Announcer.EntryID), modelid1=24920 (Ethereal Soul-Trader), npcflag=0, unit_flags=2048 (UNIT_FLAG_NON_ATTACKABLE), ScriptName="npc_trial_announcer", mechanic_immune_mask=all, flags_extra=2 (Civilian)
