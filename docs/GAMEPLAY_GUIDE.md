# Gameplay Guide

This guide details the gameplay mechanics of the Trial of Finality.

## Starting the Trial
*   Locate **Fateweaver Arithos** (default spawn in Shattrath City, Aldor Rise).
*   Only the party leader can speak to Fateweaver Arithos to propose the trial.
*   **Initial Validation:** The system first checks basic group requirements (size, member levels, location, player status regarding perma-death or existing tokens).
*   **Confirmation Phase (if enabled via `TrialOfFinality.Confirmation.Enable`):**
    *   If enabled, a confirmation prompt is sent to all other online group members.
    *   Members are warned: "WARNING: This trial involves PERMANENT CHARACTER DEATH if you fail and are not resurrected!"
    *   Members must type `/trialconfirm yes` to accept or `/trialconfirm no` to decline within the configured `TrialOfFinality.Confirmation.TimeoutSeconds` (default 60 seconds).
    *   If any member types `/trialconfirm no`, or if not all required members confirm "yes" before the timeout, the trial is aborted, and the group is notified.
    *   If all required members (currently all other online members) type `/trialconfirm yes`, the trial proceeds.
*   **Direct Start (if confirmation disabled):** If `TrialOfFinality.Confirmation.Enable` is `false`, the trial starts immediately for all eligible group members after the leader's initiation, bypassing the `/trialconfirm` step.
*   **Group Requirements (checked before confirmation or direct start):**
    *   Group size must be within configured `MinGroupSize` and `MaxGroupSize`.
    *   All members must be near Fateweaver Arithos.
    *   Level difference between highest and lowest member must be within `MaxLevelDifference`.
    *   No playerbots in the group.
    *   No member may already possess a Trial Token or be marked as perma-failed in the `character_trial_finality_status` table.

## During the Trial
*   Upon starting, all group members receive a **Trial Token**, have XP gain disabled, and are teleported to the designated arena.
*   The **Trial Announcer** will provide commentary and announce incoming waves.
*   There are 5 waves of NPCs. NPC types are randomly selected from pools configured in `mod_trial_of_finality.conf`.
*   The number of NPCs per wave scales with the number of active (not perma-deathed) players.
*   Creature levels match the highest level in the group at trial start. Later waves feature health-boosted NPCs.

## Death, Resurrection, and Perma-Death
*   If a player dies while holding the Trial Token, they enter a "downed" ghost state for the current wave.
*   **Resurrection:** Downed players can be resurrected by teammates before the current wave ends. This removes them from the "downed" state and averts perma-death for that instance.
*   **Perma-Death:** If a wave ends and a player is still "downed", their character is flagged as `is_perma_failed = 1` in the `character_trial_finality_status` database table. This makes the character unplayable upon next login (unless reset by a GM).
    *   Note: If `TrialOfFinality.PermaDeath.ExemptGMs` is enabled, Game Master accounts are exempt from this database flag.
*   **Group Wipe:** If all active players are downed or disconnect, the trial ends in failure. All "downed" players at that point are subject to the perma-death rule.

## Trial Success
*   Successfully defeating all 5 waves with at least one original member surviving results in trial success.
*   **Rewards (for each eligible survivor):** Configurable gold amount and a unique title.
*   Trial Tokens are removed, XP gain is re-enabled, and survivors are teleported out.

## Trial Failure
*   Failure occurs on a group wipe, if all players are perma-deathed, or due to critical errors (e.g., misconfigured NPC pools).
*   Survivors (not perma-deathed) have tokens removed, XP re-enabled, and are teleported out.
