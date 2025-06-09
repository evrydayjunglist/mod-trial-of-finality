# AzerothCore Module: Trial of Finality (`mod_trial_of_finality`)

## 1. Overview

`mod_trial_of_finality` is a high-risk, high-reward PvE arena challenge module for AzerothCore. It allows groups of 1-5 players to test their mettle against waves of scaled NPCs. Success yields significant gold and a unique title, but death during the trial while holding the "Trial Token" results in the character being permanently disabled and unplayable. The module introduces a combat resurrection mechanic, allowing players a chance to be saved by their teammates within a wave.

## 2. Features

*   Custom NPC, **Fateweaver Arithos**, to initiate and explain the trial.
*   Unique **Trial Token** item signifying participation and risk.
*   Group validation (size, location, level range).
*   Teleportation to a dedicated arena (configurable, defaults to Gurubashi Arena).
*   XP gain disabled during the trial.
*   5 waves of NPCs, with increasing difficulty.
*   **Dynamic & Varied Waves:** Number of NPCs per wave adjusts based on active player count. Each NPC within a wave is a *distinct type*, randomly selected from predefined pools for that wave's difficulty tier, ensuring varied encounters.
*   Custom **Trial Announcer** NPC for flavor dialogue and wave announcements.
*   **Perma-Death Mechanic:** Dying with the Trial Token and not being resurrected before the current wave ends results in permanent character disablement.
*   **Combat Resurrection:** Players can be resurrected by teammates (any combat-capable resurrection) during a wave to avoid perma-death for that specific death event.
*   **Rewards:** Significant gold and a custom title ("Conqueror of Finality") for successful completion by surviving members.
*   Detailed logging of trial events to a database table (`trial_of_finality_log`).
*   GM debug commands for resetting player status and testing the trial.
*   Fully configurable via `mod_trial_of_finality.conf`.
*   Designed to be compatible with `mod-playerbots` (bots are excluded from participating).
*   **Dynamic NPC Cheering (Cached):** Upon successful trial completion, NPCs in configured major cities can perform cheers for the victorious players. This system uses a server-startup cache of eligible NPCs to ensure minimal performance impact. The system is designed to make NPCs cheer twice, though the second cheer's delayed execution is currently logged pending further implementation of core timer functionalities.

## 3. Installation

1.  **Copy Module Files:**
    *   Place the entire `mod_trial_of_finality` directory into your AzerothCore's `modules` directory.
2.  **CMake:**
    *   Add `add_subdirectory(mod_trial_of_finality)` to your server's main `CMakeLists.txt` in the `modules` section.
    *   Re-run CMake and rebuild your server.
3.  **SQL Execution:**
    *   Apply all SQL files located in the `mod_trial_of_finality/sql/` directory to your `acore_world` database. The recommended order is:
        1.  `item_trial_token.sql` (Defines the Trial Token item)
        2.  `title_reward.sql` (Defines the "Conqueror of Finality" title)
        3.  `npc_fateweaver_arithos_template.sql` (Creature template for the trial initiator NPC)
        4.  `npc_fateweaver_arithos_spawn.sql` (Spawns Fateweaver Arithos)
        5.  `npc_trial_announcer_template.sql` (Creature template for the Trial Announcer NPC)
        6.  `log_table_trial_of_finality.sql` (Creates the database table for logging trial events)
        7.  `character_trial_finality_status.sql` (Creates the table to store perma-death status; run after `log_table_trial_of_finality.sql`)
        8.  *(Optional)* SQL files for wave creature templates if you used custom entries (e.g., 70001-70030 from the default pools) and they don't exist in your DB.

## 4. Setup & Configuration

The module's behavior is controlled by `mod_trial_of_finality/conf/mod_trial_of_finality.conf`. Ensure this file is present in your server's configuration directory.

Key configuration options (see the `.conf` file for default values and comments):

*   `TrialOfFinality.Enable`: Enable or disable the module (true/false).
*   `TrialOfFinality.FateweaverArithos.EntryID`: Creature Entry ID for Fateweaver Arithos.
*   `TrialOfFinality.Announcer.EntryID`: Creature Entry ID for the Trial Announcer.
*   `TrialOfFinality.TrialToken.EntryID`: Item Entry ID for the Trial Token.
*   `TrialOfFinality.TitleReward.ID`: Entry ID for the "Conqueror of Finality" title from `CharTitles.dbc`.
*   `TrialOfFinality.GoldReward`: Amount of gold awarded on success.
*   `TrialOfFinality.MinGroupSize`, `TrialOfFinality.MaxGroupSize`: Allowed group size.
*   `TrialOfFinality.MaxLevelDifference`: Maximum level difference allowed between group members.
*   `TrialOfFinality.Arena.MapID`, `TrialOfFinality.Arena.TeleportX/Y/Z/O`: Coordinates for the trial arena.
*   `TrialOfFinality.NpcScaling.Mode`: (Currently "match_highest_level", future expansion possible).
*   `TrialOfFinality.DisableCharacter.Method`: This option is now effectively superseded by the database-driven perma-death flag system in the `character_trial_finality_status` table. The "custom_flag" historically referred to `AURA_ID_TRIAL_PERMADEATH` (40000), which is no longer the primary mechanism for persistent perma-death.
*   `TrialOfFinality.GMDebug.Enable`: (Not currently used to gate GM commands, they are available if module is enabled).
*   `TrialOfFinality.AnnounceWinners.World.Enable`: (true/false) Enables or disables world announcements upon successful trial completion. Default: `true`.
*   `TrialOfFinality.AnnounceWinners.World.MessageFormat`: String format for the world announcement. Placeholders: `{group_leader}`, `{player_list}`.
*   `TrialOfFinality.PermaDeath.ExemptGMs`: (true/false) If `true`, Game Master accounts (security level `SEC_GAMEMASTER` or higher) will not have the perma-death flag set in the database if they fail the trial. This allows GMs to test the mechanics without risk to their characters. Default: `true`.

### NPC Wave Creature Pools
These settings allow you to define the pools of creature entry IDs used for each difficulty tier of the trial waves. IDs must be comma-separated. It is crucial to customize these pools with valid creature IDs suitable for your server's balance. The module will log errors and may fail to spawn waves if pools are misconfigured or IDs are invalid.

*   `TrialOfFinality.NpcPools.Easy`: Comma-separated list of creature entry IDs for easy waves (typically waves 1-2). Default: (placeholder IDs from original hardcoded list).
*   `TrialOfFinality.NpcPools.Medium`: Comma-separated list of creature entry IDs for medium waves (typically waves 3-4). Default: (placeholder IDs from original hardcoded list).
*   `TrialOfFinality.NpcPools.Hard`: Comma-separated list of creature entry IDs for hard waves (typically wave 5). Default: (placeholder IDs from original hardcoded list).

### NPC Cheering Settings

These settings control the behavior of NPCs cheering in cities when a trial is successfully completed. This feature uses a cached list of NPCs generated at server startup.

*   `TrialOfFinality.CheeringNpcs.Enable`: (true/false) Enables or disables the NPC cheering feature. Default: `true`.
*   `TrialOfFinality.CheeringNpcs.CityZoneIDs`: Comma-separated string of Zone IDs where NPCs are eligible to cheer. Example: `"1519,1537"` for Stormwind and Orgrimmar. Default: (common city zones as per conf file).
*   `TrialOfFinality.CheeringNpcs.RadiusAroundPlayer`: Float value defining the radius (in yards) around a player (who was part of the winning group and is in a cheering zone) within which cached NPCs will be selected to cheer. Default: `40.0`.
*   `TrialOfFinality.CheeringNpcs.MaxNpcsToCheerPerPlayerCluster`: Integer defining the maximum number of NPCs that will cheer around a single player or a close cluster of players. Default: `5`.
*   `TrialOfFinality.CheeringNpcs.MaxTotalNpcsToCheerWorld`: Integer defining the overall maximum number of NPCs that will cheer across the entire world for a single trial success event. Default: `50`.
*   `TrialOfFinality.CheeringNpcs.TargetNpcFlags`: Uint32 value. NPCs must have at least one of these flags to be considered for cheering. Set to `0` (UNIT_NPC_FLAG_NONE) to target NPCs regardless of their specific flags (unless excluded). Refer to core documentation for NPC flag values. Default: `0`.
*   `TrialOfFinality.CheeringNpcs.ExcludeNpcFlags`: Uint32 value. NPCs with any of these flags will be excluded from cheering, even if they match `TargetNpcFlags`. This is useful for preventing guards, vendors, trainers, etc., from cheering. Default: (flags for common utility NPCs, see conf file).
*   `TrialOfFinality.CheeringNpcs.CheerIntervalMs`: Uint32 value. The intended interval in milliseconds for a second cheer from the same NPC. A value of `0` disables the second cheer. *Note: Currently, the execution of this second cheer is logged, and its full implementation depends on future enhancements to timer mechanisms.* Default: `2000`.

The `AURA_ID_TRIAL_PERMADEATH` (constant `40000` in C++) is no longer the primary mechanism for persistent perma-death. It might be used for immediate in-session effects or visual cues if necessary, but the authoritative source for a character's perma-death status is the `character_trial_finality_status` database table.

### Configurable Creature ID Pools for Waves
The creature entry IDs for NPCs spawned in each wave are now configurable via the `mod_trial_of_finality.conf` file (see `TrialOfFinality.NpcPools.Easy`, `TrialOfFinality.NpcPools.Medium`, `TrialOfFinality.NpcPools.Hard` options).
You **must** ensure the creature IDs listed in these configuration settings correspond to actual creature templates in your database.
The default values in the configuration file are the original placeholder IDs (e.g., 70001-70010 for Easy).
**It is crucial to customize these creature ID pools with entries suitable for your server's balance and available custom NPCs.** Misconfiguration can lead to errors or empty waves. Each pool should ideally contain at least 5-10 distinct creature IDs appropriate for its difficulty tier to ensure variety.

## 5. Gameplay Mechanics

### 5.1. Starting the Trial
*   Locate **Fateweaver Arithos** (default spawn in Shattrath City, Aldor Rise).
*   Speak to him. Only the party leader can initiate the trial.
*   **Group Requirements:**
    *   Group size between `MinGroupSize` and `MaxGroupSize`.
    *   All members on the same map and zone as Fateweaver Arithos, and reasonably close to him.
    *   Level difference between highest and lowest member must be `<= MaxLevelDifference`.
    *   No playerbots in the group.
    *   No member may already possess a Trial Token.
    *   No member may be currently marked as perma-failed in the `character_trial_finality_status` table (and by extension, should not have the old `AURA_ID_TRIAL_PERMADEATH` if cleanup was successful).

### 5.2. During the Trial
*   Upon starting, all group members receive a **Trial Token**, have XP gain disabled, and are teleported to the arena.
*   The **Trial Announcer** will announce incoming waves.
*   There are 5 waves of NPCs.
*   **Wave Scaling:** The number of NPCs per wave scales based on the number of currently active (not perma-deathed) players in the trial.
*   Creature levels are set to the highest level present in the group when the trial started. For each wave, a set of *distinct* NPC types are randomly selected from a pre-defined pool for that wave's difficulty tier (Easy, Medium, Hard). Creatures in later waves also receive a health boost (e.g., +20% for medium waves, +50% for hard wave).

### 5.3. Death, Resurrection, and Perma-Death
*   If a player dies while holding the Trial Token, they enter a "downed" state for the current wave. They become a ghost.
*   **Resurrection:** Downed players can be resurrected by any combat-capable resurrection spell or item cast by a teammate *before the current wave ends*. A successful resurrection removes them from the "downed" state, and they avoid perma-death for that specific death event.
*   **Perma-Death:** If a wave ends and a player is still in the "downed" state (i.e., died and was not resurrected), they are permanently affected:
    *   A flag (`is_perma_failed = 1`) is set in the `character_trial_finality_status` database table for their character, along with a timestamp. This is the authoritative mark of perma-death.
    *   The old `AURA_ID_TRIAL_PERMADEATH` (constant `40000`) is removed if present, as the DB flag supersedes it for persistence.
    *   They are marked as `permanentlyFailed` for the remainder of this trial instance (internal tracking).
    *   The trial *continues* for any remaining active players. Future waves will scale to the new group size.
    *   Players whose characters have the `is_perma_failed = 1` flag in the database will be kicked from the game upon their next login and will be unable to log into that character again (unless a GM intervenes).
    *   Note: If `TrialOfFinality.PermaDeath.ExemptGMs` is enabled, Game Master accounts (security level `SEC_GAMEMASTER` or higher) will not have this flag set, allowing them to test without permanent consequences to their characters.
*   **Group Wipe:** If all currently active players in the trial are downed simultaneously (or disconnect), the trial ends in failure. All players who were "downed" at that point will have the perma-death flag set in the database (unless they are GMs and the exemption is active).

### 5.4. Trial Success
*   Successfully defeating all 5 waves while at least one original member remains active (not perma-deathed) results in trial success.
*   **Rewards (for each surviving, eligible member):**
    *   `TrialOfFinality.GoldReward` (e.g., 20,000 gold).
    *   The title "Conqueror of Finality" (ID `TrialOfFinality.TitleReward.ID`).
*   Trial Tokens are removed, XP gain is re-enabled, and survivors are teleported out.

### 5.5. Trial Failure
*   Trial failure occurs if:
    *   A group wipe happens (all active players are downed/disconnected).
    *   No active players remain after wave-end perma-death processing.
    *   Critical errors occur (e.g., unable to spawn NPCs for a wave).
*   On failure, Trial Tokens are removed from survivors, XP gain is re-enabled, and survivors are teleported out. Any players perma-deathed remain so.

## 6. GM Commands

Access to commands requires `SEC_GAMEMASTER` level.

*   **.trial reset <CharacterName>**
    *   Resets the Trial of Finality perma-death status for the specified character by setting `is_perma_failed = 0` in the `character_trial_finality_status` table.
    *   It also removes the Trial Token item from their inventory (if online) and attempts to clean up the old `AURA_ID_TRIAL_PERMADEATH` (if online and present).
    *   This allows a character to become playable again if they were previously perma-deathed and kicked on login.

*   **.trial test start**
    *   Allows a GM to start a solo test version of the Trial of Finality for themselves.
    *   Bypasses normal group validation. The GM acts as a solo participant.
    *   The trial mechanics (token, XP disable, teleport, waves, announcer, death rules) apply to the GM. Note that the outcome regarding perma-death for the GM character is subject to the `TrialOfFinality.PermaDeath.ExemptGMs` configuration setting. Useful for testing.

## 7. Logging

*   All significant trial events (start, wave starts, player deaths/downed, resurrections, perma-deaths applied, success, failure, GM command usage) are logged to the `trial_of_finality_log` table in the `acore_world` database.
*   These events are also logged to the server console (`sLog`) with the prefix `[TrialEventSLOG]`.

## 8. Developer Notes & Future Considerations
*   The perma-death mechanism now uses a database flag in the `character_trial_finality_status` table for persistence. This replaces the previous system that relied solely on `AURA_ID_TRIAL_PERMADEATH` (constant `40000`) for long-term status. The aura might still be used for immediate in-session effects or as a visual cue but is cleaned up/secondary to the DB flag.
*   Creature entries for waves are now configurable in `mod_trial_of_finality.conf`. Ensure these are updated from the default placeholders (e.g., `70001`-`70030`) to valid creature IDs in your database.
*   Wave difficulty scaling currently adjusts NPC count and provides a basic health multiplier for later waves. More complex stat scaling or varied NPC abilities per wave could be added.
*   Randomization of NPC types for waves is achieved by shuffling the configured creature ID pools for each spawn event.
*   World announcements for trial winners have been added (see configuration options).
```
