DELETE FROM `creature_template` WHERE `entry`=90000;
INSERT INTO `creature_template`
(`entry`, `modelid1`, `name`, `subname`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`, `unit_class`, `unit_flags`, `type`, `type_flags`, `RegenHealth`, `ScriptName`, `VehicleId`, `mechanic_immune_mask`, `flags_extra`)
VALUES
(90000, 24918, 'Fateweaver Arithos', 'Trial of Finality', 0, 70, 70, 35, 1, 1.0, 1.14, 1.0, 1, 0, 7, 0, 1, 'npc_fateweaver_arithos', 0, 0, 0);
