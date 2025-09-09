# 1. Introduction

This document provides in-depth technical details for the `mod_trial_of_finality` module, intended for developers, advanced administrators, or those looking to contribute. For user-focused installation, setup, and gameplay information, please see the main [README.md](../README.md). For comprehensive test plans and scenarios, refer to the [Testing Guide](testing_guide.md).

For a complete list of all configuration options, please see the [Configuration Guide](docs/CONFIGURATION_GUIDE.md).

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

## 3. Database Schema

### Schema Deployment

The SQL scripts for creating the necessary database tables (`trial_of_finality_log`, `character_trial_finality_status`) and inserting initial module data (item templates, title rewards, NPC templates) are managed through AzerothCore's standard module SQL update system. These files are located in the module's `data/sql/updates/applied/world/` directory (e.g., `2024_01_01_00_tof_log_table.sql`) and are automatically executed by the `worldserver` upon startup. Refer to the main [README.md](../README.md) for user-facing installation instructions.

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

## 4. Key System Internals

*   **Perma-Death Logic:**
    *   When a trial fails and players are eligible for perma-death, `TrialManager::FinalizeTrialOutcome` is invoked.
    *   For each eligible player, it executes an SQL query: `INSERT INTO character_trial_finality_status (guid, is_perma_failed, last_failed_timestamp) VALUES (%u, 1, NOW()) ON DUPLICATE KEY UPDATE is_perma_failed = 1, last_failed_timestamp = NOW()`.
    *   The `ModPlayerScript::OnLogin` handler queries this table for the logging-in player. If `is_perma_failed` is `1`, the player's session is kicked.
    *   The `PermaDeathExemptGMs` configuration allows GMs (security level >= `SEC_GAMEMASTER`) to bypass having this flag set if they fail a trial while online.
*   **Trial Confirmation System:**
    *   **`TrialManager::InitiateTrial`**: When called (and `ConfirmationEnable` is true), this function creates a `PendingTrialInfo` object and stores it in the `m_pendingTrials` map, keyed by the group ID. This struct tracks the leader, members who need to confirm, members who have accepted, and the start time. Prompts are then sent to all online group members (excluding the leader). If there are no other members to confirm (e.g., a solo player or a group with offline members), it proceeds to start the trial directly.
    *   **`trial_player_commandscript`**: This script registers the `/trialconfirm` (and alias `/tc`) command. When a player uses the command, it parses the "yes" or "no" argument and calls `TrialManager::instance()->HandleTrialConfirmation(player, accepted)`.
    *   **`TrialManager::HandleTrialConfirmation`**: This function updates the `PendingTrialInfo` for the player's group. If a player responds with "no", the trial is immediately aborted via `AbortPendingTrial`, and all members are notified. If a player responds with "yes", their GUID is added to the `memberGuidsAccepted` set. The function then checks if all prompted members have now accepted. If so, it calls `StartConfirmedTrial`.
    *   **`TrialManager::OnUpdate`**: This function, called by the `ModWorldScript`, acts as the system's ticker. It periodically checks all `m_pendingTrials` to see if any have exceeded the `ConfirmationTimeoutSeconds`. If a pending trial has timed out, it is aborted. It also calls `CheckPlayerLocationsAndEnforceBoundaries` for active trials.
    *   **`TrialManager::StartConfirmedTrial`**: This function is the final step of the confirmation process. It retrieves the `PendingTrialInfo`, performs a final validation check (e.g., to ensure the group size is still valid), creates the `ActiveTrialInfo` struct, and removes the entry from `m_pendingTrials`. It then proceeds with the main trial logic: granting tokens, disabling XP, and teleporting players to the arena.
*   **NPC Cheering Cache:**
    *   During `ModServerScript::OnConfigLoad`, if `CheeringNpcsEnable` is true and `CheeringNpcCityZoneIDs` are provided, the server queries the `creature` table.
    *   It selects `guid` and `zoneId` for creatures matching the configured zone IDs and NPC flag criteria (target/exclude flags), and are not player-controlled.
    *   These `ObjectGuid`s are stored in `ModServerScript::s_cheeringNpcCacheByZone`, a `std::map<uint32, std::vector<ObjectGuid>>`.
    *   When `TrialManager::TriggerCityNpcCheers` is called, it iterates through online players. For players in configured cheer zones, it retrieves the cached NPC GUIDs for that zone, checks proximity, and makes them emote.
*   **Player-Initiated Forfeit System:**
    *   **`/trialforfeit` (alias `/tf`):** This new player command allows members of a group in an active trial to vote to forfeit.
    *   **`TrialManager::HandleTrialForfeit`**: When the first player in a trial types the command, a vote is initiated. A 30-second timer starts, and all group members are notified. Other active (alive) players must also type `/trialforfeit` to agree.
    *   **Vote Resolution:** The vote succeeds only if all currently active players in the trial type the command before the timer expires. If successful, the trial ends gracefully by calling `CleanupTrial` directly, which means no perma-death penalties are applied. If the timer expires, the vote is cancelled, and the trial continues.
    *   **State Management:** The voting state (whether a vote is in progress, its start time, and who has voted) is managed by new variables in the `ActiveTrialInfo` struct. The timeout is handled by a check in `TrialManager::OnUpdate`.
*   **NPC Spawning (`TrialManager::SpawnActualWave`):**
    *   The correct NPC pool (`NpcPoolEasy`, `NpcPoolMedium`, `NpcPoolHard`) is selected based on the current wave number.
    *   If the selected pool is empty (due to misconfiguration or all IDs being invalid), the trial is ended with an error.
    *   The number of creatures to spawn (`numCreaturesToSpawn`) is determined based on active player count, capped by `NUM_SPAWNS_PER_WAVE` and the selected pool's size.
    *   A copy of the selected pool is shuffled, and the required number of distinct creature IDs are picked.
    *   Creatures are summoned, their level set to the trial's `highestLevelAtStart`, and a health multiplier is applied for medium (1.2x) and hard (1.5x) waves (if not using custom scaling).

## 5. Key Constants and Enums (from C++)

*   **`AURA_ID_TRIAL_PERMADEATH (40000)`:** This constant (defined in `ModTrialOfFinality.h` or .cpp) represents a placeholder Aura ID. Historically, it was the primary marker for perma-death. With the introduction of the `character_trial_finality_status` table, this aura's role is diminished. It's no longer applied as the persistent lock and is actively removed if found when the DB flag is set or by the `.trial reset` command. It might be used for immediate, temporary in-session visual effects if desired, but the database is the authoritative source.
*   **`WAVE_SPAWN_POSITIONS[]`:** A hardcoded array of `Position` structs defining the 5 spawn locations for NPCs within the trial arena.
*   **`NUM_SPAWNS_PER_WAVE (5)`:** A constant defining the maximum number of NPCs that can be spawned per wave if enough players are present and the selected NPC pool has enough variety. The actual number can be lower.
*   **`TrialEventType` (enum):** Defines the types of events logged to the `trial_of_finality_log` table (see Section 4, Database Schema).
*   **Player Chat Commands:**
    *   The `trial_player_commandscript` class registers two primary commands with `SEC_PLAYER` permissions: `/trialconfirm` (alias `/tc`) and `/trialforfeit` (alias `/tf`).
    *   `HandleTrialConfirmCommand` is the static handler for confirmations. It ensures the player exists, is in a group, and parses "yes" or "no". It then calls `TrialManager::instance()->HandleTrialConfirmation(player, accepted)`.
    *   `HandleTrialForfeitCommand` is the static handler for forfeits. It calls `TrialManager::instance()->HandleTrialForfeit(player)` to process the vote.

## 6. Developer Notes & Future Considerations

This section includes notes migrated from the main README and additional points for future development.

*   **Perma-Death Mechanism:** The system now uses a database flag (`is_perma_failed` in `character_trial_finality_status`) for persistent perma-death status. This is more robust than the previous aura-only system. The `AURA_ID_TRIAL_PERMADEATH` is largely deprecated for persistence.
*   **Configurable Creature Pools:** Creature entries for waves are now fully configurable via `mod_trial_of_finality.conf`. Server administrators **must** customize these from the default placeholder IDs to ensure valid and balanced encounters.
*   **Wave Difficulty Scaling:** The `custom_scaling_rules` mode provides a flexible way to scale difficulty. Health is scaled via a direct multiplier. Damage should be scaled by creating custom passive auras that modify damage done by a percentage and adding their spell IDs to the `AurasToAdd` configuration for the desired tier. This provides a robust and flexible method for damage scaling.
*   **NPC Randomization:** Randomization of NPC types for waves is achieved by shuffling the configured creature ID pools before selecting creatures for each spawn event, ensuring variety if pools are sufficiently large.
*   **World Announcements:** This feature is implemented and configurable.
*   **NPC Cheering - Second Cheer:** This feature is implemented. It uses a vector `m_pendingSecondCheers` in the `TrialManager` and a periodic check in the `OnUpdate` ticker to avoid creating a separate timer for each NPC. When an NPC cheers, if the interval is configured, a struct containing the NPC's GUID and the target cheer time is added to the vector. The `OnUpdate` method processes this vector to trigger the second cheer.
*   **More Varied Wave Compositions:** Beyond distinct creature types, future iterations could introduce pre-defined "encounter groups" within pools, allowing for specific combinations of roles (e.g., healer + tanks + casters) to be selected as a unit.
*   **Player-Initiated Forfeit:** The current implementation requires a unanimous vote. Future enhancements could allow for a majority vote, configurable via the `.conf` file.
*   **Arena Boundaries:** This feature is implemented. The `TrialManager::CheckPlayerLocationsAndEnforceBoundaries` function is called periodically by the `OnUpdate` ticker. It verifies that each player in the trial is on the correct `Arena.MapID` and within the `Arena.Radius` distance from the teleport-in coordinates. If a player is found outside the boundary, they receive a warning. If they are found outside the boundary again on a subsequent check, the trial is ended in failure. Future improvements could involve using AreaTriggers for more complex arena shapes instead of a simple radius.
*   **Advanced Configuration Validation:** While basic parsing and template existence checks are done for NPC pools, more sophisticated validation (e.g., ensuring enough creatures for `NUM_SPAWNS_PER_WAVE` if desired) could be added, possibly with more detailed feedback to the server console on startup.

This guide should serve as a comprehensive technical reference for the `mod_trial_of_finality`.
