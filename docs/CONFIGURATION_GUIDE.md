# Configuration Guide

This guide provides a detailed explanation of all configuration options available in `mod_trial_of_finality.conf`.

## Module Enable
*   **`TrialOfFinality.Enable`**: (boolean, default: `true`)
    *   Enables or disables the entire module. If `false`, NPCs will not be interactable for trial purposes, and GM commands might be restricted.

## Core Entry IDs
*   **`TrialOfFinality.FateweaverArithos.EntryID`**: (uint32, default: `0` - example `90000`)
    *   Creature Entry ID for the Fateweaver Arithos NPC, who initiates the trial. This ID must exist in your `creature_template`.
*   **`TrialOfFinality.Announcer.EntryID`**: (uint32, default: `0` - example `90002`)
    *   Creature Entry ID for the Trial Announcer NPC that provides commentary during the trial. This ID must exist in your `creature_template`.
*   **`TrialOfFinality.TrialToken.EntryID`**: (uint32, default: `0` - example `90001`)
    *   Item Entry ID for the "Trial Token" item. This item must exist in your `item_template`. It signifies participation and is consumed or removed upon trial completion or reset.

## Rewards
*   **`TrialOfFinality.TitleReward.ID`**: (uint32, default: `0` - example `100`)
    *   The ID of the character title (from `CharTitles.dbc`) awarded to players who successfully complete the trial.
*   **`TrialOfFinality.GoldReward`**: (uint32, default: `20000`)
    *   Amount of gold (in copper coins, so `20000` is 2 gold) awarded to each eligible surviving member upon successful completion.

## Gameplay Rules
*   **`TrialOfFinality.MinGroupSize`**: (uint8, default: `1`)
    *   Minimum number of players required in a group to start the trial.
*   **`TrialOfFinality.MaxGroupSize`**: (uint8, default: `5`)
    *   Maximum number of players allowed in a group to start the trial.
*   **`TrialOfFinality.MaxLevelDifference`**: (uint8, default: `10`)
    *   Maximum allowed level difference between the highest and lowest level players in the group.

## Arena Settings
*   **`TrialOfFinality.Arena.MapID`**: (uint16, default: `0`)
    *   The Map ID of the arena where the trial takes place.
*   **`TrialOfFinality.Arena.TeleportX`**: (float, default: `0.0`)
*   **`TrialOfFinality.Arena.TeleportY`**: (float, default: `0.0`)
*   **`TrialOfFinality.Arena.TeleportZ`**: (float, default: `0.0`)
*   **`TrialOfFinality.Arena.TeleportO`**: (float, default: `0.0`)
    *   The X, Y, Z, and Orientation coordinates for teleporting players into the trial arena.

## NPC Scaling
*   **`TrialOfFinality.NpcScaling.Mode`**: (string, default: `"match_highest_level"`)
    *   Defines how NPC levels are determined.
    *   `"match_highest_level"`: The default mode. NPC levels match the highest level in the group. Health is boosted by a hardcoded multiplier for Medium (+20%) and Hard (+50%) waves.
    *   `"custom_scaling_rules"`: Enables the custom scaling rules below, allowing for finer control over NPC difficulty.
*   **`TrialOfFinality.NpcScaling.Custom.Easy.HealthMultiplier`**: (float, default: `1.0`)
*   **`TrialOfFinality.NpcScaling.Custom.Easy.DamageMultiplier`**: (float, default: `1.0`)
*   **`TrialOfFinality.NpcScaling.Custom.Easy.AurasToAdd`**: (string, default: `""`)
    *   Health multiplier and auras for Easy tier NPCs (Waves 1-2). To scale damage, you should create a custom passive aura spell that modifies damage done by a percentage and add its ID here.
*   **`TrialOfFinality.NpcScaling.Custom.Medium.HealthMultiplier`**: (float, default: `1.2`)
*   **`TrialOfFinality.NpcScaling.Custom.Medium.AurasToAdd`**: (string, default: `""`)
    *   Health multiplier and auras for Medium tier NPCs (Waves 3-4).
*   **`TrialOfFinality.NpcScaling.Custom.Hard.HealthMultiplier`**: (float, default: `1.5`)
*   **`TrialOfFinality.NpcScaling.Custom.Hard.AurasToAdd`**: (string, default: `""`)
    *   Health multiplier and auras for Hard tier NPCs (Wave 5).

## NPC Wave Creature Pools
These settings define the pools of creature entry IDs for each wave difficulty tier. IDs must be comma-separated strings (e.g., `"123,456,789"`). Whitespace around IDs is automatically trimmed during parsing.

**It is CRUCIAL that these IDs correspond to actual creature templates in your database.** The module performs checks during config loading:
*   Each item is validated to be a number.
*   Each number is checked to be within `uint32` valid range (not zero).
*   Each valid numeric ID is checked against `sObjectMgr->GetCreatureTemplate` to ensure the creature template exists.
Invalid entries or non-existent template IDs are logged as errors and skipped. If a pool is empty after parsing (either due to an empty config string or all entries being invalid), waves requiring that pool will fail to spawn, likely resulting in the trial ending prematurely with an error. Customize these from the provided defaults.

*   **`TrialOfFinality.NpcPools.Easy`**: (string, default: `"70001,70002,70003,70004,70005,70006,70007,70008,70009,70010"`)
*   **`TrialOfFinality.NpcPools.Medium`**: (string, default: `"70011,70012,70013,70014,70015,70016,70017,70018,70019,70020"`)
*   **`TrialOfFinality.NpcPools.Hard`**: (string, default: `"70021,70022,70023,70024,70025,70026,70027,70028,70029,70030"`)

## Perma-Death Settings
*   **`TrialOfFinality.PermaDeath.ExemptGMs`**: (boolean, default: `true`)
    *   If `true`, player accounts with a security level of `SEC_GAMEMASTER` or higher will not have the `is_perma_failed` flag set in the `character_trial_finality_status` table if they "die" and are not resurrected during a trial.

## Trial Confirmation Settings
*   **`TrialOfFinality.Confirmation.Enable`**: (boolean, default: `true`)
    *   Enables or disables the trial start confirmation system.
*   **`TrialOfFinality.Confirmation.TimeoutSeconds`**: (uint32, default: `60`)
    *   The number of seconds group members have to respond to a trial confirmation prompt.
*   **`TrialOfFinality.Confirmation.RequiredMode`**: (string, default: `"all"`)
    *   Defines how many members need to confirm. Only "all" is currently supported.

## Forfeit Settings
*   **`TrialOfFinality.Forfeit.Enable`**: (boolean, default: `true`)
    *   If `true`, the `/trialforfeit` command is available to players. If `false`, the command is disabled.

## World Announcement Settings
*   **`TrialOfFinality.AnnounceWinners.World.Enable`**: (boolean, default: `true`)
*   **`TrialOfFinality.AnnounceWinners.World.MessageFormat`**: (string, default: `"Hark, heroes! The group led by {group_leader}, with valiant trialists {player_list}, has vanquished all foes and emerged victorious from the Trial of Finality! All hail the Conquerors!"`)

## NPC Cheering Settings
*   **`TrialOfFinality.CheeringNpcs.Enable`**: (boolean, default: `true`)
*   **`TrialOfFinality.CheeringNpcs.CityZoneIDs`**: (string, default: `"1519,1537,1637,1638,1657,3487,4080,4395,3557"`)
*   **`TrialOfFinality.CheeringNpcs.RadiusAroundPlayer`**: (float, default: `40.0`)
*   **`TrialOfFinality.CheeringNpcs.MaxNpcsToCheerPerPlayerCluster`**: (int, default: `5`)
*   **`TrialOfFinality.CheeringNpcs.MaxTotalNpcsToCheerWorld`**: (int, default: `50`)
*   **`TrialOfFinality.CheeringNpcs.TargetNpcFlags`**: (uint32, default: `0`)
*   **`TrialOfFinality.CheeringNpcs.ExcludeNpcFlags`**: (uint32, default: `32764`)
*   **`TrialOfFinality.CheeringNpcs.CheerIntervalMs`**: (uint32, default: `2000`)
    *   Interval in milliseconds for a second cheer emote. Set to 0 to disable.

## Miscellaneous
*   **`TrialOfFinality.DisableCharacter.Method`**: (string, default: `"custom_flag"`)
    *   This is an informational setting. The perma-death system is now database-driven.
*   **`TrialOfFinality.GMDebug.Enable`**: (boolean, default: `false`)
    *   Enables verbose debug logging.
