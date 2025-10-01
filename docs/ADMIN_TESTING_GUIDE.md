### Admin Testing Guide: `mod-trial-of-finality`

**Objective:** This guide provides a comprehensive walkthrough for administrators to test the functionality, stability, and features of the Trial of Finality module.

**1. Initial Setup & Configuration Verification**

*   **Build & Installation:** Confirm that the module compiles successfully with the latest source code and that the server starts without database errors. The necessary tables (`trial_of_finality_log`, `character_trial_finality_status`) should be present in your `character` database.
*   **Configuration File:** Open `mod_trial_of_finality.conf` and review the settings. For initial testing, ensure the following are set to valid IDs from your database:
    *   `TrialOfFinality.FateweaverArithos.EntryID`
    *   `TrialOfFinality.Announcer.EntryID`
    *   `TrialOfFinality.TrialToken.EntryID`
    *   `TrialOfFinality.TitleReward.ID`
    *   Ensure the arena coordinates and map ID are set to a valid, accessible location.

**2. Core Gameplay Walkthrough (Success Scenario)**

*   **Team Formation:** Log in with one or more player accounts. Form a group that meets the `MinGroupSize` and `MaxLevelDifference` requirements.
*   **Initiation:** Travel to **Fateweaver Arithos**. As the group leader, speak with the NPC and select the option to start the trial. If the confirmation system is enabled, have all group members type `/trialconfirm yes`.
*   **Verification:**
    *   All players should be teleported to the specified arena.
    *   Each player should have a "Trial Token" item in their inventory.
    *   The Trial Announcer NPC should appear and announce Wave 1.
*   **Combat:** Defeat all five waves of enemies.
    *   **Observe:** Do waves spawn correctly? Does the announcer provide commentary? Do the NPCs scale as expected based on your configuration?
*   **Completion:** After defeating the final wave, the trial should conclude.
    *   **Verification:** Players should receive the configured gold and title rewards. They should be teleported out of the arena, and the "Trial Token" should be removed from their inventory. Check the world chat for the victory announcement (if enabled).

**3. Failure & Perma-Death Testing**

*   **Player Death:** During a wave, have one player character die and *do not* resurrect them.
*   **Wave Completion:** Have the rest of the group clear the current wave.
*   **Verification:**
    *   At the end of the wave, the trial should fail for the entire group.
    *   All living players should be teleported out.
    *   The player who died should also be teleported out (or to their bind point).
    *   Attempt to log back in with the character that died. The login should be rejected with a message indicating their fate is sealed.
    *   Use a database tool to inspect the `character_trial_finality_status` table. The `guid` of the deceased character should be present with `is_perma_failed` set to `1`.
*   **Resurrection Test:** Repeat the scenario, but this time, have a teammate resurrect the fallen player *before* the wave ends. The resurrected player should be able to continue fighting, and the trial should not fail.

**4. GM Command & Feature Testing**

*   **`.trial reset <CharacterName>`:** Use this command on the character that was permanently disabled in the previous step.
    *   **Verification:** The command should confirm the reset. The player should now be able to log in successfully. Check the database to ensure `is_perma_failed` is set to `0`.
*   **`.trial test`:** Log in with a GM account that is *not* in a group.
    *   **Verification:** Run the command. The GM should be teleported into the arena alone, and the trial should begin. This tests the solo GM test-start feature.
*   **Playerbot Testing:**
    1.  In `mod_trial_of_finality.conf`, set `TrialOfFinality.GMDebug.AllowPlayerbots = 1` and restart the server.
    2.  Log in as a GM, form a group, and add a playerbot (`.bot add <name>`).
    3.  Start the trial. It should proceed with the bot.
    4.  Set the config option to `0`, restart, and try again. The trial should now be rejected because a playerbot is in the group.

**5. Forfeit System Testing**

*   **Initiate Forfeit:** During an active trial, have one player type `/trialforfeit`.
*   **Verification:** A vote should be announced to the group.
*   **Vote 'Yes':** Have all other active players type `/trialforfeit`. The trial should end gracefully (no perma-death), and all players should be teleported out.
*   **Vote Timeout:** Initiate another vote and simply wait. After 30 seconds, the vote should time out and be cancelled.