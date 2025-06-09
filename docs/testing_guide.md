# Trial of Finality - Testing & Balancing Guide

## 1. Introduction

This guide outlines a comprehensive testing and balancing strategy for the `mod_trial_of_finality` AzerothCore module. Its purpose is to ensure all features function as intended, the module is stable, and the gameplay experience is challenging yet fair. For an overview of features, installation, and user configuration, please see the main [README.md](../README.md). For deep technical details, refer to the [Developer Guide](DEVELOPER_GUIDE.md).

## 2. Prerequisites/Setup

Before testing, ensure you have the following:

*   **GM Account Access:** Full Game Master privileges are required.
*   **Knowledge of GM Commands:** Familiarity with common GM commands is essential. Specific commands relevant to this module include:
    *   `.trial reset <CharacterName>`
    *   `.trial test start`
    *   `.debug hostil` (to make NPCs attack)
    *   `.modify hp <value>` (to adjust NPC health for testing)
    *   `.additem <item_id>` (e.g., for resurrection items)
    *   `.tele <location>` (for quick navigation)
    *   `.revive`
*   **Test Characters:** Characters of various levels (e.g., level 60, 70, 80, or whatever your server's cap is) to test NPC scaling and difficulty across different level brackets.
*   **Client & Server Access:** Ability to run the game client and access server-side logs in real-time.
*   **Database Access:** Ability to query and inspect relevant database tables, primarily:
    *   `trial_of_finality_log` (for event logging)
    *   `character_trial_finality_status` (stores the `is_perma_failed` flag and timestamp)
    *   `character_aura` (to verify `AURA_ID_TRIAL_PERMADEATH` (currently 40000) is cleaned up if it was applied).
*   **Configuration Awareness:** Be aware of the `TrialOfFinality.PermaDeath.ExemptGMs` and NPC Wave Pool settings (`TrialOfFinality.NpcPools.*`) in `mod_trial_of_finality.conf`. For GMs to test the full perma-death loop on their own characters, `PermaDeath.ExemptGMs` might need to be temporarily set to `false`. Testers may also need to adjust NPC pools to verify specific creature interactions or test edge cases like empty/invalid pools.

## 3. Testing Areas

### A. Trial Initiation

*   **A.1. Group Size Validation:**
    *   Test with fewer than `MinGroupSize` players (should fail).
    *   Test with more than `MaxGroupSize` players (should fail).
    *   Test with a valid group size (should succeed).
*   **A.2. Level Difference Validation:**
    *   Test with group members whose level difference exceeds `MaxLevelDifference` (should fail).
    *   Test with group members within the allowed level difference (should succeed).
*   **A.3. Location/Zone Requirements:**
    *   Ensure all group members must be near Fateweaver Arithos and in the same zone/map to start. Test with members far away or in different zones (should fail).
*   **A.4. Token Granting & XP Disable:**
    *   Verify each member receives the `TrialTokenEntry` item upon starting.
    *   Verify XP gain is disabled for all members (`player->SetDisableXpGain(true)`).
*   **A.5. Teleportation to Arena:**
    *   Confirm all members are teleported to the configured arena coordinates (`ArenaMapID`, X, Y, Z, O).
*   **A.6. `ActiveTrialInfo` Creation:**
    *   Check server logs for correct initialization of `ActiveTrialInfo` (group ID, leader, member GUIDs, highest level, start time).
*   **A.7. Pre-existing Conditions:**
    *   Attempt to start with a player who already has a Trial Token (should fail or have token removed and replaced).
    *   Attempt to start with a player whose character has `is_perma_failed = 1` in `character_trial_finality_status` (should fail).
*   **A.8. Playerbot Exclusion:**
    *   If using `mod-playerbots`, ensure groups with playerbots cannot start the trial.

### B. Wave Mechanics

*   **B.1. Announcer NPC:**
    *   Verify the Trial Announcer NPC spawns.
    *   Verify flavor dialogue and wave announcements occur at appropriate times (e.g., before each wave).
*   **B.2. NPC Count Scaling:**
    *   Test with different group sizes (e.g., 1 player, 3 players, 5 players).
    *   Verify the number of NPCs spawned per wave scales correctly according to the number of *active* (not perma-deathed) players.
*   **B.3. Randomized & Distinct NPC Selection from Configured Pools:**
    *   Verify that NPCs for each wave (Easy: 1-2, Medium: 3-4, Hard: 5) are randomly selected from the creature ID lists defined in `TrialOfFinality.NpcPools.Easy`, `TrialOfFinality.NpcPools.Medium`, and `TrialOfFinality.NpcPools.Hard` in the `.conf` file.
    *   Ensure each NPC spawned within a single wave is of a *distinct* creature ID from the configured pool.
    *   Test with pools of varying sizes, including pools smaller than `NUM_SPAWNS_PER_WAVE` (or the calculated `numCreaturesToSpawn`), to ensure the spawn count adjustment logic works correctly (spawns only up to the number of unique IDs available in the pool).
*   **B.4. Misconfigured NPC Pools:**
    *   **Empty Pool String:** Set one pool to an empty string (e.g., `TrialOfFinality.NpcPools.Easy = ""`).
        *   Expected: When a wave requiring this pool is reached, the trial should fail gracefully, logging an error about the empty pool. No creatures should spawn for this wave.
    *   **Invalid IDs:** Configure a pool with some invalid creature IDs (e.g., non-numeric, zero, non-existent in `creature_template`) mixed with valid ones.
        *   Expected: Valid NPCs from the pool are spawned. Invalid/non-existent IDs are skipped, and errors are logged for each skipped ID.
    *   **All Invalid IDs:** Configure a pool with only invalid or non-existent creature IDs.
        *   Expected: Similar to an empty pool string, the trial should fail for waves requiring this pool, with appropriate error logs.
*   **B.5. NPC Scaling & Health Multipliers:**
    *   Verify NPC levels are set to the `highestLevelAtStart` of the group.
    *   Verify health multipliers are applied correctly for Medium (+20%) and Hard (+50%) waves. Use `.debug hostil` and `.info` or damage meters to check effective health.
*   **B.5. NPC Aggression & Combat:**
    *   Ensure NPCs correctly aggro and engage players.
    *   Test their pathing and ability usage (if any defined in their templates).
*   **B.6. Wave Progression:**
    *   Confirm that a wave only ends and the next begins after all NPCs in the current wave are killed.
    *   Verify smooth transition from one wave to the next.
*   **B.7. `activeMonsters` Tracking:**
    *   Monitor server logs or use a debugger to ensure the `activeMonsters` set in `ActiveTrialInfo` correctly tracks spawned and killed creatures.

### C. Death, Resurrection, and Perma-Death

*   **C.1. Player Death with Token:**
    *   Player dies while holding the Trial Token.
    *   Verify they enter a "downed" state and appear as a ghost.
*   **C.2. `downedPlayerGuids` Tracking:**
    *   Confirm the player's GUID is added to `downedPlayerGuids` in `ActiveTrialInfo`.
*   **C.3. Resurrection:**
    *   Have a teammate resurrect the downed player *during the same wave*.
    *   Verify the player is revived, removed from `downedPlayerGuids`, and can continue fighting.
    *   If the wave ends after resurrection, ensure they are NOT perma-deathed.
*   **C.4. No Resurrection / Perma-Death Application:**
    *   Player dies and is *not* resurrected before the current wave ends.
    *   Verify `is_perma_failed` is set to `1` and `last_failed_timestamp` is updated in the `character_trial_finality_status` table for the player's GUID (unless they are an exempt GM).
    *   Verify their GUID is added to `permanentlyFailedPlayerGuids` (internal tracking for current trial).
    *   Verify `AURA_ID_TRIAL_PERMADEATH` (currently 40000) is ideally *not* applied, or if it is for an immediate effect, that it's cleaned up upon setting the DB flag or by the GM reset command. The DB flag is the source of truth.
*   **C.5. GM Exemption for Perma-Death:**
    *   Set `TrialOfFinality.PermaDeath.ExemptGMs = true` in the configuration.
    *   Have a GM character (account with `SEC_GAMEMASTER` or higher) fail the trial under conditions that would normally cause perma-death.
    *   **Expected:** The GM character should *not* have `is_perma_failed = 1` in `character_trial_finality_status`. A log message should indicate the exemption. The GM should not be kicked on next login.
    *   Set `TrialOfFinality.PermaDeath.ExemptGMs = false` in the configuration.
    *   Have a GM character fail the trial similarly.
    *   **Expected:** The GM character *should* have `is_perma_failed = 1` in `character_trial_finality_status`. The GM character *will* be kicked on the next login (unless `.trial reset` is used before relogging).
*   **C.6. Login Kick (Non-GM or GM with Exemption Disabled):**
    *   After a character (non-GM, or GM with exemption disabled) has `is_perma_failed = 1` in the database, log out and attempt to log back in with that character.
    *   Verify they are kicked from the game with the message "Your fate was sealed in the Trial of Finality."
*   **C.7. Group Wipe:**
    *   Simulate a scenario where all active players die in a wave.
    *   Verify the trial ends in failure.
    *   Verify all players who were "downed" at that point are perma-deathed (DB flag set), considering GM exemption status.

### D. Arena Boundary Enforcement

*   **D.1. Leaving Arena Warning:**
    *   Have a player move outside the designated arena boundaries (defined by `ArenaMapID` and a conceptual radius, or specific area triggers if implemented).
    *   Verify they receive a warning message.
*   **D.2. `playersWarnedForLeavingArena` Tracking:**
    *   Confirm the player's GUID is added to this set in `ActiveTrialInfo`.
*   **D.3. Forfeit/Failure on Boundary Violation:**
    *   Player stays outside too long after a warning.
    *   Player leaves the arena again after being warned.
    *   Verify the trial ends in failure for the group, and the violating player is appropriately handled (e.g., perma-deathed if they held a token and were "downed" by this forfeit).

### E. Disconnect/Reconnect Handling

*   **E.1. Player Disconnects:**
    *   Simulate a player disconnecting during a wave.
    *   Verify they are treated as "downed" for wave scaling and failure conditions.
*   **E.2. Player Reconnects:**
    *   Player reconnects while the trial is still active.
    *   Verify they rejoin the trial instance.
    *   Their state (active or downed, if they died before DC) should be restored.
*   **E.3. Stray Token Removal:**
    *   Player disconnects, trial ends (success or failure). Player logs back in.
    *   Verify their Trial Token is removed.
*   **E.4. Group Wipe by Disconnect:**
    *   If all active players disconnect, verify the trial ends in failure and perma-death applies to token holders.

### F. Trial Success

*   **F.1. Success Conditions:**
    *   Group successfully clears all 5 waves.
    *   At least one original member (who received a token) is still active (not perma-deathed).
*   **F.2. Gold Reward:**
    *   Verify each surviving, eligible member receives `GoldReward`.
*   **F.3. Title Reward:**
    *   Verify each surviving, eligible member receives the title specified by `TitleRewardID`.
*   **F.4. Trial Token Removal:**
    *   Confirm Trial Tokens are removed from all survivors.
*   **F.5. XP Re-enabled:**
    *   Verify XP gain is re-enabled for survivors.
*   **F.6. Teleportation Out:**
    *   Confirm survivors are teleported out of the arena to a safe location (e.g., their hearthstone location or a pre-defined exit point).

### G. Trial Failure

*   **G.1. Failure Conditions:**
    *   Test various failure scenarios: group wipe, forfeit due to boundary violation, critical error (e.g., unable to spawn NPCs - may require code modification to simulate).
*   **G.2. No Rewards:**
    *   Confirm no gold or title is awarded on failure.
*   **G.3. Token Removal (Survivors):**
    *   Trial Tokens removed from any non-perma-deathed survivors.
*   **G.4. XP Re-enabled (Survivors):**
    *   XP gain re-enabled for non-perma-deathed survivors.
*   **G.5. Teleportation Out (Survivors):**
    *   Non-perma-deathed survivors are teleported out.

### H. GM Commands

*   **H.1. `.trial reset <CharacterName>`:**
    *   Use on a character who has `is_perma_failed = 1` in `character_trial_finality_status`.
    *   Verify the table is updated (`is_perma_failed = 0`, `last_failed_timestamp = NULL`).
    *   If the character is online, verify their Trial Token is removed (if they had one).
    *   If the character is online, verify `AURA_ID_TRIAL_PERMADEATH` is removed (if they had it).
    *   Verify the character can log in successfully and is playable again.
*   **H.2. `.trial test start`:**
    *   GM uses command: verify solo trial starts.
    *   Verify all normal trial mechanics (token, XP disable, waves, death rules) apply to the GM.

### I. Database Logging

*   **I.1. Event Type Coverage:**
    *   Attempt to trigger every `TrialEventType`. For a detailed list of `TrialEventType` enum values and their meanings, refer to the [Developer Guide](DEVELOPER_GUIDE.md#database-schema).
    *   Query the `trial_of_finality_log` table.
    *   Verify each event is logged with the correct `event_type` string.
    *   Query the `character_trial_finality_status` table to cross-reference perma-death events.
*   **I.2. Data Accuracy:**
    *   For `trial_of_finality_log` entries, check accuracy of: `group_id`, `player_guid`, `wave_number`, `details`, etc.
    *   For `character_trial_finality_status` entries, check accuracy of `guid`, `is_perma_failed`, and `last_failed_timestamp`.

### J. NPC Cheering (Cached Implementation)

*   **J.1. Cache Population:**
    *   On server startup, check server logs for messages indicating the cheering NPC cache is being populated.
    *   Verify logs for the number of NPCs cached per zone and total.
*   **J.2. Cheering Conditions:**
    *   Successfully complete a trial with a group.
    *   Have players from the winning group travel to configured `CheeringNpcCityZoneIDs`.
*   **J.3. Correct NPC Behavior:**
    *   Verify NPCs within `CheeringNpcsRadiusAroundPlayer` of a winner actually cheer.
    *   Test that `CheeringNpcsTargetNpcFlags` and `CheeringNpcsExcludeNpcFlags` are respected (e.g., vendors/trainers should not cheer if excluded).
    *   Check `CheeringNpcsMaxPerPlayerCluster` and `CheeringNpcsMaxTotalWorld` limits.
*   **J.4. Double Cheer Log:**
    *   Verify the first cheer emote occurs.
    *   If `CheeringNpcsCheerIntervalMs > 0`, check server logs for the detail message indicating an NPC "would perform a second cheer".

### K. World Announcements

*   **K.1. Broadcast on Success:**
    *   Upon successful trial completion, verify a global server message is broadcast if `TrialOfFinality.AnnounceWinners.World.Enable` is true.
*   **K.2. Message Formatting:**
    *   Ensure the message uses the format from `TrialOfFinality.AnnounceWinners.World.MessageFormat`.
    *   Verify placeholders `{group_leader}` and `{player_list}` are correctly filled with player names.

## 4. Balancing Considerations

Balancing is crucial for making the Trial of Finality engaging and appropriately challenging.

*   **A. NPC Difficulty:**
    *   **Creature Stats:** For various group sizes and average player levels, assess if NPC health, damage output, and armor are appropriate for each wave. Use combat logs and GM commands (`.modify hp`, `.modify damage`) for live adjustments and testing.
    *   **Creature Pools (Configured):** Review the creature IDs configured in `TrialOfFinality.NpcPools.Easy/Medium/Hard` in the `.conf` file. Are these creatures thematically appropriate for their difficulty tier? Do their abilities create a balanced challenge? (This is a server admin task but crucial for testing feedback).
    *   **Health Multipliers:** Is the +20% for medium waves and +50% for hard waves a good baseline? Does it provide a noticeable but fair increase in difficulty?
*   **B. Wave Progression:**
    *   **Difficulty Curve:** Does the challenge ramp up smoothly from Wave 1 to Wave 5? Or are there sudden spikes or drops in difficulty?
    *   **Wave Pacing:** Is any particular wave consistently too easy or too difficult, potentially leading to player frustration or boredom?
*   **C. Rewards:**
    *   **Gold Amount:** Is `GoldReward` a significant enough incentive given the risk of perma-death? Consider your server's economy.
    *   **Title Desirability:** Is the "Conqueror of Finality" title prestigious and sought after?
*   **D. Player Experience:**
    *   **Engagement:** Is the trial fun and exciting?
    *   **Length:** Is the overall time to complete the trial reasonable? (Too long might be fatiguing, too short might feel unrewarding).
    *   **Clarity:** Are the mechanics of the trial (especially perma-death, resurrection, arena boundaries) made clear to players through NPC dialogue (Fateweaver, Announcer) and system messages?

## 5. Reporting Issues

When reporting bugs or balancing concerns, please include:

*   **Clear Title:** Summarize the issue.
*   **Steps to Reproduce:** Detailed, step-by-step instructions.
*   **Expected Result:** What you anticipated happening.
*   **Actual Result:** What actually occurred.
*   **Severity/Priority:** (e.g., Crash, Major Gameplay Bug, Minor Visual Issue, Balancing Concern).
*   **Screenshots/Videos:** If helpful in illustrating the issue.
*   **Relevant Logs:** Server log snippets (especially `[TrialEventSLOG]` messages or errors), client console output if applicable.
*   **Character(s) Involved:** Names and levels of characters used for testing.
*   **Module Version/Commit:** If known.

Thorough testing and iterative balancing will be key to the success of the `mod_trial_of_finality`.
