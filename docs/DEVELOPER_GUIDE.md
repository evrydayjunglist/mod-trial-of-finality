# 1. Introduction

This document provides in-depth technical details for the `mod_trial_of_finality` module, intended for developers, advanced administrators, or those looking to contribute. For user-focused installation, setup, and gameplay information, please see the main [README.md](../README.md). For comprehensive test plans and scenarios, refer to the [Testing Guide](testing_guide.md).

## 2. Core Module Architecture

The `mod_trial_of_finality` module revolves around several key classes and a defined event flow to manage its complex PVE challenge.

**Key Classes:**

*   **`TrialManager` (Singleton):** This is the heart of the module, managing the state of active trials, player participation, wave progression (spawning, tracking kills), and ultimately determining trial success or failure. It handles the logic for applying rewards and perma-death consequences.
*   **`ModServerScript`:** Inherits from `ServerScript`. Its primary responsibilities include loading all module configurations from `mod_trial_of_finality.conf` during server startup or reload events. It also manages the server-startup caching of NPC GUIDs for the NPC cheering feature.
*   **`ModPlayerScript`:** Inherits from `PlayerScript`. This class hooks into various player-specific events such as login, death, and resurrection. These hooks are crucial for:
    *   Checking perma-death status on login.
    *   Handling player deaths during a trial (initiating the "downed" state).
    *   Tracking player resurrections to avert perma-death for a specific wave.
    *   Cleaning up trial tokens if a player logs in outside an active trial.
*   **`npc_fateweaver_arithos`:** A `CreatureScript` for the NPC who initiates the Trial of Finality. It handles gossip interactions to start the trial and provides information to players.
*   **`npc_trial_announcer`:** A `CreatureScript` for the NPC that provides flavor text and announces waves within the trial arena. It has a simple AI to deliver messages at appropriate times.
*   **`trial_commandscript`:** Inherits from `CommandScript`. It registers and handles GM commands specific to the module, such as `.trial reset` and `.trial test start`.

**Basic Event Flow:**

1.  **Initiation:** A player (group leader) interacts with Fateweaver Arithos. `npc_fateweaver_arithos` script calls `TrialManager::ValidateGroupForTrial`.
2.  If valid, `TrialManager::InitiateTrial` is called:
    *   `ActiveTrialInfo` struct is created for the group.
    *   Trial Tokens are granted to members. XP gain is disabled.
    *   Players are teleported to the arena.
    *   The Trial Announcer NPC is spawned.
3.  **Wave Cycle:**
    *   `TrialManager::PrepareAndAnnounceWave` is scheduled. The announcer NPC delivers its dialogue.
    *   After a delay, `TrialManager::SpawnActualWave` is called:
        *   NPCs are selected from configured pools based on wave difficulty.
        *   Creatures are spawned with scaled levels and health.
        *   `activeMonsters` set is populated.
    *   Players engage the wave. `TrialManager::HandleMonsterKilledInTrial` tracks kills.
    *   If a player dies, `ModPlayerScript::OnPlayerKilledByCreature` (or similar death hook) triggers `TrialManager::HandlePlayerDownedInTrial`.
    *   If a player is resurrected, `ModPlayerScript::OnPlayerResurrect` informs `TrialManager`.
4.  **Wave End:** When all `activeMonsters` are cleared, the cycle repeats for the next wave.
5.  **Trial Conclusion:**
    *   **Success:** If all 5 waves are cleared, `TrialManager::FinalizeTrialOutcome` is called with `overallSuccess = true`. Rewards are given, tokens removed.
    *   **Failure:** If the group wipes, a player forfeits, or a critical error occurs (e.g., empty NPC pool), `TrialManager::FinalizeTrialOutcome` is called with `overallSuccess = false`.
        *   Perma-death logic is applied here: for players in `downedPlayerGuids` who were not resurrected, the `is_perma_failed` flag is set in `character_trial_finality_status` (unless they are an exempt GM).
6.  **Cleanup:** `TrialManager::CleanupTrial` removes trial data, re-enables XP, and teleports survivors.
7.  **Login Check:** `ModPlayerScript::OnLogin` checks the `character_trial_finality_status` table for any player attempting to log in. If `is_perma_failed = 1`, the player session is kicked.

## 3. Detailed Configuration (`mod_trial_of_finality.conf`)

The following provides a detailed explanation of all configuration options available in `mod_trial_of_finality.conf`.

*   **`TrialOfFinality.Enable`**: (boolean, default: `true`)
    *   Enables or disables the entire module. If `false`, NPCs will not be interactable for trial purposes, and GM commands might be restricted.

*   **`TrialOfFinality.FateweaverArithos.EntryID`**: (uint32, default: `0` - example `90000`)
    *   Creature Entry ID for the Fateweaver Arithos NPC, who initiates the trial. This ID must exist in your `creature_template`.
*   **`TrialOfFinality.Announcer.EntryID`**: (uint32, default: `0` - example `90002`)
    *   Creature Entry ID for the Trial Announcer NPC that provides commentary during the trial. This ID must exist in your `creature_template`.
*   **`TrialOfFinality.TrialToken.EntryID`**: (uint32, default: `0` - example `90001`)
    *   Item Entry ID for the "Trial Token" item. This item must exist in your `item_template`. It signifies participation and is consumed or removed upon trial completion or reset.

*   **`TrialOfFinality.TitleReward.ID`**: (uint32, default: `0` - example `100`)
    *   The ID of the character title (from `CharTitles.dbc`) awarded to players who successfully complete the trial.
*   **`TrialOfFinality.GoldReward`**: (uint32, default: `20000`)
    *   Amount of gold (in copper coins, so `20000` is 2 gold) awarded to each eligible surviving member upon successful completion.

*   **`TrialOfFinality.MinGroupSize`**: (uint8, default: `1`)
    *   Minimum number of players required in a group to start the trial.
*   **`TrialOfFinality.MaxGroupSize`**: (uint8, default: `5`)
    *   Maximum number of players allowed in a group to start the trial.
*   **`TrialOfFinality.MaxLevelDifference`**: (uint8, default: `10`)
    *   Maximum allowed level difference between the highest and lowest level players in the group.

*   **`TrialOfFinality.Arena.MapID`**: (uint16, default: `0`)
    *   The Map ID of the arena where the trial takes place.
*   **`TrialOfFinality.Arena.TeleportX`**: (float, default: `0.0`)
*   **`TrialOfFinality.Arena.TeleportY`**: (float, default: `0.0`)
*   **`TrialOfFinality.Arena.TeleportZ`**: (float, default: `0.0`)
*   **`TrialOfFinality.Arena.TeleportO`**: (float, default: `0.0`)
    *   The X, Y, Z, and Orientation coordinates for teleporting players into the trial arena.

*   **`TrialOfFinality.NpcScaling.Mode`**: (string, default: `"match_highest_level"`)
    *   Defines how NPC levels are determined.
    *   `"match_highest_level"`: All NPCs in the trial will be scaled to the level of the highest-level player in the group at the moment the trial started.
    *   `"custom_scaling_rules"`: Placeholder for a future, more complex scaling system (not currently implemented).
*   **`TrialOfFinality.DisableCharacter.Method`**: (string, default: `"custom_flag"`)
    *   This option is largely informational due to the implementation of a database-driven perma-death system.
    *   Historically, `"custom_flag"` referred to using `AURA_ID_TRIAL_PERMADEATH` (40000) for perma-death. The primary mechanism is now the `is_perma_failed` flag in the `character_trial_finality_status` table.
*   **`TrialOfFinality.GMDebug.Enable`**: (boolean, default: `false`)
    *   Intended for enabling verbose debug logging or specific GM-only debug features. Currently, GM commands (`.trial reset`, `.trial test start`) are available if the module is enabled, regardless of this setting. It can be used by developers for conditional debug code.

*   **`TrialOfFinality.AnnounceWinners.World.Enable`**: (boolean, default: `true`)
    *   If `true`, a server-wide announcement will be made when a group successfully completes the Trial of Finality.
*   **`TrialOfFinality.AnnounceWinners.World.MessageFormat`**: (string, default: `"Hark, heroes! The group led by {group_leader}, with valiant trialists {player_list}, has vanquished all foes and emerged victorious from the Trial of Finality! All hail the Conquerors!"`)
    *   The format string for the world announcement. Available placeholders:
        *   `{group_leader}`: Name of the group leader.
        *   `{player_list}`: Comma-separated list of surviving player names who completed the trial.

*   **`TrialOfFinality.PermaDeath.ExemptGMs`**: (boolean, default: `true`)
    *   If `true`, player accounts with a security level of `SEC_GAMEMASTER` or higher will not have the `is_perma_failed` flag set in the `character_trial_finality_status` table if they "die" and are not resurrected during a trial. This allows GMs to test mechanics without risking their characters. They will still be considered "failed" for the purpose of that specific trial instance's rewards.

### Trial Confirmation Settings
*   **`TrialOfFinality.Confirmation.Enable`**: (boolean, default: `true`)
    *   Enables or disables the trial start confirmation system. If `true`, group members (excluding the leader) must confirm their participation via `/trialconfirm yes`. If `false`, `TrialManager::InitiateTrial` bypasses the confirmation prompt phase and attempts to start the trial directly (still creating a temporary `PendingTrialInfo` which is immediately processed as if all confirmed).
*   **`TrialOfFinality.Confirmation.TimeoutSeconds`**: (uint32, default: `60`)
    *   The number of seconds group members have to respond to a trial confirmation prompt. This timeout is managed by `TrialManager::UpdatePendingConfirmations`.
*   **`TrialOfFinality.Confirmation.RequiredMode`**: (string, default: `"all"`)
    *   Defines how many members need to confirm.
    *   `"all"`: All prompted members (online members of the group, excluding the leader) must type `/trialconfirm yes`. A single `/trialconfirm no` from any member will abort the trial initiation. If members time out, and not all have said "yes", it's also aborted.
    *   Future modes like `"majority"` or `"leader_plus_one"` are not currently implemented but this setting allows for future expansion. The C++ code currently defaults to "all" if an unsupported mode is specified.

### NPC Wave Creature Pools
These settings define the pools of creature entry IDs for each wave difficulty tier. IDs must be comma-separated strings (e.g., `"123,456,789"`). Whitespace around IDs is automatically trimmed during parsing.

**It is CRUCIAL that these IDs correspond to actual creature templates in your database.** The module performs checks during config loading:
*   Each item is validated to be a number.
*   Each number is checked to be within `uint32` valid range (not zero).
*   Each valid numeric ID is checked against `sObjectMgr->GetCreatureTemplate` to ensure the creature template exists.
Invalid entries or non-existent template IDs are logged as errors and skipped. If a pool is empty after parsing (either due to an empty config string or all entries being invalid), waves requiring that pool will fail to spawn, likely resulting in the trial ending prematurely with an error. Customize these from the provided defaults.

*   **`TrialOfFinality.NpcPools.Easy`**: (string, default: `"70001,70002,70003,70004,70005,70006,70007,70008,70009,70010"`)
    *   Creature IDs for easy waves (typically Waves 1-2).
*   **`TrialOfFinality.NpcPools.Medium`**: (string, default: `"70011,70012,70013,70014,70015,70016,70017,70018,70019,70020"`)
    *   Creature IDs for medium waves (typically Waves 3-4). These NPCs also receive a 20% health boost.
*   **`TrialOfFinality.NpcPools.Hard`**: (string, default: `"70021,70022,70023,70024,70025,70026,70027,70028,70029,70030"`)
    *   Creature IDs for hard waves (typically Wave 5). These NPCs also receive a 50% health boost.

### NPC Cheering Settings
Controls the NPC cheering feature upon successful trial completion. This uses a server-startup cache of eligible NPCs.

*   **`TrialOfFinality.CheeringNpcs.Enable`**: (boolean, default: `true`)
    *   Enables or disables the NPC cheering feature.
*   **`TrialOfFinality.CheeringNpcs.CityZoneIDs`**: (string, default: `"1519,1537,1637,1638,1657,3487,4080,4395,3557"`)
    *   Comma-separated string of Zone IDs where NPCs are eligible to cheer (e.g., major cities).
*   **`TrialOfFinality.CheeringNpcs.RadiusAroundPlayer`**: (float, default: `40.0`)
    *   Radius (in yards) around a victorious player (in a cheering zone) to find cached NPCs to cheer.
*   **`TrialOfFinality.CheeringNpcs.MaxNpcsToCheerPerPlayerCluster`**: (int, default: `5`)
    *   Maximum NPCs to cheer around a single player or a close cluster of players.
*   **`TrialOfFinality.CheeringNpcs.MaxTotalNpcsToCheerWorld`**: (int, default: `50`)
    *   Overall maximum NPCs to cheer across the world for one trial success event.
*   **`TrialOfFinality.CheeringNpcs.TargetNpcFlags`**: (uint32, default: `0` which is `UNIT_NPC_FLAG_NONE`)
    *   NPCs must have at least one of these flags (bitwise OR) to be considered for cheering. `0` means any NPC type (not specifically excluded) can cheer. Other flags include `GOSSIP (1)`, `QUESTGIVER (2)`, etc.
*   **`TrialOfFinality.CheeringNpcs.ExcludeNpcFlags`**: (uint32, default: `32764`)
    *   NPCs with any of these flags (bitwise OR) will be excluded, even if targeted. Default value excludes common utility NPCs (vendors, trainers, etc.).
*   **`TrialOfFinality.CheeringNpcs.CheerIntervalMs`**: (uint32, default: `2000`)
    *   Intended interval in milliseconds for a second cheer. Currently, the second cheer is only logged; its delayed execution is not yet implemented. `0` disables this.

## 4. Database Schema

The module uses two custom database tables in the `acore_world` database.

### `trial_of_finality_log` Table
Stores a log of all significant trial events for auditing and tracking.

*   **Columns:**
    *   `event_id` (BIGINT UNSIGNED, PK, AI): Unique identifier for the log entry.
    *   `event_time` (TIMESTAMP, default: CURRENT_TIMESTAMP): Time of the event.
    *   `event_type` (VARCHAR(50)): Type of event, corresponding to the `TrialEventType` enum.
        *   `TRIAL_START`, `WAVE_START`, `PLAYER_DEATH_TOKEN`, `TRIAL_SUCCESS`, `TRIAL_FAILURE`, `GM_COMMAND_RESET`, `GM_COMMAND_TEST_START`, `PLAYER_RESURRECTED`, `PERMADEATH_APPLIED`, `PLAYER_DISCONNECT`, `PLAYER_RECONNECT`, `STRAY_TOKEN_REMOVED`, `PLAYER_WARNED_ARENA_LEAVE`, `PLAYER_FORFEIT_ARENA`, `WORLD_ANNOUNCEMENT_SUCCESS`, `NPC_CHEER_TRIGGERED`.
    *   `group_id` (INT UNSIGNED, NULL): Group ID if the event is related to a specific group trial.
    *   `player_guid` (INT UNSIGNED, NULL): Player GUID if the event is specific to a player.
    *   `player_name` (VARCHAR(12), NULL): Name of the player.
    *   `player_account_id` (INT UNSIGNED, NULL): Account ID of the player.
    *   `highest_level_in_group` (TINYINT UNSIGNED, NULL): Highest player level in the group at trial start.
    *   `wave_number` (INT, default: 0): Current wave number relevant to the event.
    *   `details` (TEXT, NULL): Additional textual details about the event (e.g., reason for failure, list of winners).

### `character_trial_finality_status` Table
Stores the perma-death status of characters. This is the authoritative source for determining if a character is permanently locked out due to trial failure.

*   **Columns:**
    *   `guid` (INT UNSIGNED, PK): Character GUID.
    *   `is_perma_failed` (TINYINT(1) UNSIGNED, default: 0): Boolean flag. `1` if the character is perma-failed, `0` otherwise.
    *   `last_failed_timestamp` (TIMESTAMP, NULL, default: NULL): Timestamp of when `is_perma_failed` was last set to `1`. Automatically updated by C++ logic.

## 5. Key System Internals

*   **Perma-Death Logic:**
    *   When a trial fails and players are eligible for perma-death, `TrialManager::FinalizeTrialOutcome` is invoked.
    *   For each eligible player, it executes an SQL query: `INSERT INTO character_trial_finality_status (guid, is_perma_failed, last_failed_timestamp) VALUES (%u, 1, NOW()) ON DUPLICATE KEY UPDATE is_perma_failed = 1, last_failed_timestamp = NOW()`.
    *   The `ModPlayerScript::OnLogin` handler queries this table for the logging-in player. If `is_perma_failed` is `1`, the player's session is kicked.
    *   The `PermaDeathExemptGMs` configuration allows GMs (security level >= `SEC_GAMEMASTER`) to bypass having this flag set if they fail a trial while online.
*   **Trial Confirmation System:**
    *   `TrialManager::InitiateTrial`: When called (and `ConfirmationEnable` is true), it creates a `PendingTrialInfo` object stored in `m_pendingTrials` (map keyed by group ID). This struct holds `leaderGuid`, `memberGuidsToConfirm` (online members excluding leader), `memberGuidsAccepted`, `creationTime`, and `highestLevelAtStart`. Prompts are sent to members. If no members need to confirm (e.g. solo leader), it calls `StartConfirmedTrial` directly.
    *   `trial_player_commandscript`: Handles `/trialconfirm yes` and `/trialconfirm no` (and `/tc` alias). It calls `TrialManager::instance()->HandleTrialConfirmation(player, accepted)`.
    *   `TrialManager::HandleTrialConfirmation` (to be detailed when implemented): Updates the `PendingTrialInfo` for the player's group. If 'no', aborts the pending trial. If 'yes', adds to `memberGuidsAccepted`. If all prompted have accepted, calls `StartConfirmedTrial`.
    *   `TrialManager::UpdatePendingConfirmations` (to be detailed when implemented): Periodically checks `m_pendingTrials` for entries exceeding `ConfirmationTimeoutSeconds`. Timed-out trials are aborted.
    *   `TrialManager::StartConfirmedTrial` (to be detailed when implemented): Retrieves `PendingTrialInfo`, validates final conditions, creates `ActiveTrialInfo`, removes from `m_pendingTrials`, and starts the actual trial (token grant, teleport, etc.).
*   **NPC Cheering Cache:**
    *   During `ModServerScript::OnConfigLoad`, if `CheeringNpcsEnable` is true and `CheeringNpcCityZoneIDs` are provided, the server queries the `creature` table.
    *   It selects `guid` and `zoneId` for creatures matching the configured zone IDs and NPC flag criteria (target/exclude flags), and are not player-controlled.
    *   These `ObjectGuid`s are stored in `ModServerScript::s_cheeringNpcCacheByZone`, a `std::map<uint32, std::vector<ObjectGuid>>`.
    *   When `TrialManager::TriggerCityNpcCheers` is called, it iterates through online players. For players in configured cheer zones, it retrieves the cached NPC GUIDs for that zone, checks proximity, and makes them emote.
*   **NPC Spawning (`TrialManager::SpawnActualWave`):**
    *   The correct NPC pool (`NpcPoolEasy`, `NpcPoolMedium`, `NpcPoolHard`) is selected based on the current wave number.
    *   If the selected pool is empty (due to misconfiguration or all IDs being invalid), the trial is ended with an error.
    *   The number of creatures to spawn (`numCreaturesToSpawn`) is determined based on active player count, capped by `NUM_SPAWNS_PER_WAVE` and the selected pool's size.
    *   A copy of the selected pool is shuffled, and the required number of distinct creature IDs are picked.
    *   Creatures are summoned, their level set to the trial's `highestLevelAtStart`, and a health multiplier is applied for medium (1.2x) and hard (1.5x) waves.

## 6. Key Constants and Enums (from C++)

*   **`AURA_ID_TRIAL_PERMADEATH (40000)`:** This constant (defined in `ModTrialOfFinality.h` or .cpp) represents a placeholder Aura ID. Historically, it was the primary marker for perma-death. With the introduction of the `character_trial_finality_status` table, this aura's role is diminished. It's no longer applied as the persistent lock and is actively removed if found when the DB flag is set or by the `.trial reset` command. It might be used for immediate, temporary in-session visual effects if desired, but the database is the authoritative source.
*   **`WAVE_SPAWN_POSITIONS[]`:** A hardcoded array of `Position` structs defining the 5 spawn locations for NPCs within the trial arena.
*   **`NUM_SPAWNS_PER_WAVE (5)`:** A constant defining the maximum number of NPCs that can be spawned per wave if enough players are present and the selected NPC pool has enough variety. The actual number can be lower.
*   **`TrialEventType` (enum):** Defines the types of events logged to the `trial_of_finality_log` table (see Section 4, Database Schema).
*   **Player Chat Commands:**
    *   The `trial_player_commandscript` class registers `/trialconfirm` (alias `/tc`) with `SEC_PLAYER` permissions.
    *   `HandleTrialConfirmCommand` is the static handler. It ensures the player exists, is in a group, and parses "yes" or "no". It then calls `TrialManager::instance()->HandleTrialConfirmation(player, accepted)`.

## 7. Developer Notes & Future Considerations

This section includes notes migrated from the main README and additional points for future development.

*   **Perma-Death Mechanism:** The system now uses a database flag (`is_perma_failed` in `character_trial_finality_status`) for persistent perma-death status. This is more robust than the previous aura-only system. The `AURA_ID_TRIAL_PERMADEATH` is largely deprecated for persistence.
*   **Configurable Creature Pools:** Creature entries for waves are now fully configurable via `mod_trial_of_finality.conf`. Server administrators **must** customize these from the default placeholder IDs to ensure valid and balanced encounters.
*   **Wave Difficulty Scaling:** Currently, scaling involves adjusting NPC count per wave based on active players and applying a health multiplier for medium/hard waves. Future enhancements could include:
    *   More granular stat scaling (damage, armor, resistances).
    *   Applying specific auras or abilities to NPCs based on wave difficulty.
    *   Scripted events or unique boss-like creatures for certain waves.
*   **NPC Randomization:** Randomization of NPC types for waves is achieved by shuffling the configured creature ID pools before selecting creatures for each spawn event, ensuring variety if pools are sufficiently large.
*   **World Announcements:** This feature is implemented and configurable.
*   **NPC Cheering - Second Cheer:** The configuration for a second cheer interval (`CheeringNpcs.CheerIntervalMs`) exists, and the intent is logged. However, the actual delayed execution of the second cheer is not yet implemented due to complexities with managing timers for potentially many NPCs without direct AI contexts. This requires further investigation into suitable core mechanisms or a custom scheduler.
*   **More Varied Wave Compositions:** Beyond distinct creature types, future iterations could introduce pre-defined "encounter groups" within pools, allowing for specific combinations of roles (e.g., healer + tanks + casters) to be selected as a unit.
*   **Player-Initiated Forfeit:** Consider adding a command or UI option for a group to unanimously agree to forfeit a trial, perhaps with less severe consequences than a full wipe or boundary violation (e.g., no perma-death but also no rewards).
*   **Arena Boundaries:** Currently conceptual. Implementing precise coordinate-based or AreaTrigger-based arena boundaries would make the `CheckPlayerLocationsAndEnforceBoundaries` logic more robust.
*   **Advanced Configuration Validation:** While basic parsing and template existence checks are done for NPC pools, more sophisticated validation (e.g., ensuring enough creatures for `NUM_SPAWNS_PER_WAVE` if desired) could be added, possibly with more detailed feedback to the server console on startup.

This guide should serve as a comprehensive technical reference for the `mod_trial_of_finality`.
