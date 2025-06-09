#

# WORK IN PROGRESS!!!!

# AzerothCore Module: Trial of Finality (`mod-trial-of-finality`)

## 1. Overview

`mod_trial_of_finality` is a high-risk, high-reward PvE arena challenge module for AzerothCore. It allows groups of 1-5 players to test their mettle against waves of scaled NPCs. Success yields significant gold and a unique title, but death during the trial while holding the "Trial Token" results in the character being permanently disabled and unplayable. The module introduces a combat resurrection mechanic, allowing players a chance to be saved by their teammates within a wave.

## 2. Features

*   Custom NPC, **Fateweaver Arithos**, to initiate and explain the trial.
*   Unique **Trial Token** item signifying participation and risk.
*   Group validation (size, location, level range).
*   Teleportation to a dedicated arena (configurable).
*   XP gain disabled during the trial.
*   5 waves of NPCs, with increasing difficulty.
*   **Dynamic & Varied Waves:** Number of NPCs per wave adjusts based on active player count. Each NPC within a wave is a *distinct type*, randomly selected from configurable pools for that wave's difficulty tier.
*   Custom **Trial Announcer** NPC for flavor dialogue and wave announcements.
*   **Perma-Death Mechanic:** Dying with the Trial Token and not being resurrected before the current wave ends results in permanent character disablement (via database flag).
*   **Combat Resurrection:** Players can be resurrected by teammates during a wave to avoid perma-death for that specific death event.
*   **Rewards:** Significant gold and a custom title for successful completion.
*   Detailed logging of trial events to a database table (`trial_of_finality_log`).
*   GM debug commands for resetting player status and testing the trial.
*   Fully configurable via `mod_trial_of_finality.conf`.
*   **Dynamic NPC Cheering (Cached):** Upon successful trial completion, NPCs in configured major cities can perform cheers. This system uses a server-startup cache. The second cheer is currently logged, pending full timer implementation.

## 3. Installation

1.  **Copy Module Files:**
    *   git clone https://github.com/Stuntmonkey4u/mod-trial-of-finality.git
    *   Place the entire `mod-trial-of-finality` directory into your AzerothCore's `modules` directory.
3.  **CMake:**
    *   Add `add_subdirectory(mod-trial-of-finality)` to your server's main `CMakeLists.txt` in the `modules` section.
    *   Re-run CMake and rebuild your server.
4.  **Database Setup:**
    *   This module uses AzerothCore's integrated SQL update system.
    *   Ensure the module is correctly placed in your `modules` directory and you have re-run CMake and rebuilt your server as per steps 1 & 2.
    *   Upon the next startup, the `worldserver` will automatically detect and apply the necessary SQL changes from the module's `data/sql/updates/applied/world/` directory.
    *   Monitor your `worldserver` console output during its first startup after adding the module. Look for messages indicating SQL updates are being applied (e.g., "Applying AZEROTHCORE SQL update 'YYYY_MM_DD_RR_tof_...'") and check for any errors.
    *   The required database tables (`trial_of_finality_log`, `character_trial_finality_status`) and initial data (items, titles, NPC templates) will be automatically created.
    *   *(Optional)* If you customize `TrialOfFinality.NpcPools.*` in the `.conf` file to use *new custom creature IDs* that don't exist in your database, you will still need to create SQL entries for those specific custom creatures yourself. The module does not provide SQL for arbitrary custom NPCs you might choose.

## 4. Essential Configuration (`mod_trial_of_finality.conf`)

The module is configured via `mod_trial_of_finality.conf`. Below are the most common settings for basic setup and gameplay adjustments. For a complete list of all options and advanced technical details, please see the [Developer Guide](docs/DEVELOPER_GUIDE.md).

*   **Core Setup:**
    *   `TrialOfFinality.Enable`: `true` or `false` to enable/disable the module.
    *   `TrialOfFinality.FateweaverArithos.EntryID`: Creature ID for the trial initiator NPC.
    *   `TrialOfFinality.Announcer.EntryID`: Creature ID for the trial announcer NPC.
    *   `TrialOfFinality.TrialToken.EntryID`: Item ID for the Trial Token.
    *   `TrialOfFinality.TitleReward.ID`: ID of the title reward from `CharTitles.dbc`.
    *   `TrialOfFinality.GoldReward`: Amount of gold (in copper) for success.
*   **Gameplay Rules:**
    *   `TrialOfFinality.MinGroupSize`, `TrialOfFinality.MaxGroupSize`: Min/max players for the trial.
    *   `TrialOfFinality.MaxLevelDifference`: Max level spread allowed in the group.
*   **Arena Location:**
    *   `TrialOfFinality.Arena.MapID`, `TrialOfFinality.Arena.TeleportX/Y/Z/O`: Define the trial arena map and teleport coordinates. Adjust these to your desired arena location.
*   **NPC Customization (Crucial for Admins):**
    *   `TrialOfFinality.NpcPools.Easy`, `TrialOfFinality.NpcPools.Medium`, `TrialOfFinality.NpcPools.Hard`: Comma-separated lists of creature entry IDs for trial waves. **Crucial: Customize these with valid creature IDs from your database.** Example: `"123,456,789"`. See the [Developer Guide](docs/DEVELOPER_GUIDE.md) for more details on NPC scaling and pool configuration.
*   **Perma-Death Options:**
    *   `TrialOfFinality.PermaDeath.ExemptGMs`: If `true` (default), Game Master accounts are not permanently affected by trial failure.
    *   *Note:* Perma-death status is stored in the `character_trial_finality_status` database table. The old `DisableCharacter.Method` config option and direct reliance on `AURA_ID_TRIAL_PERMADEATH` (40000) are now primarily historical/secondary. See the [Developer Guide](docs/DEVELOPER_GUIDE.md) for full details.
*   **Trial Confirmation:**
    *   `TrialOfFinality.Confirmation.Enable`: Enable/disable the trial start confirmation (default: true). If enabled, members must type `/trialconfirm yes`.
    *   `TrialOfFinality.Confirmation.TimeoutSeconds`: How long members have to confirm (default: 60s).
    *   *Note:* `TrialOfFinality.Confirmation.RequiredMode` (default: "all") is also available; currently, only "all" is supported. See Developer Guide for details.
*   **Optional Features (Briefly):**
    *   `TrialOfFinality.AnnounceWinners.World.Enable`: `true` or `false` to enable/disable server-wide announcements for trial winners. (See Dev Guide for message format).
    *   `TrialOfFinality.CheeringNpcs.Enable`: `true` or `false` to enable/disable NPCs cheering in cities for winners. (See Dev Guide for detailed sub-options like ZoneIDs, flags, radius, interval, and second cheer status).

For detailed explanations of all configuration options, including NPC flags, specific default values, and advanced settings, refer to the [Developer Guide](docs/DEVELOPER_GUIDE.md).
For guidance on testing all features of this module, see the [Testing Guide](docs/testing_guide.md).

## 5. Gameplay Mechanics

### 5.1. Starting the Trial
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

### 5.2. During the Trial
*   Upon starting, all group members receive a **Trial Token**, have XP gain disabled, and are teleported to the designated arena.
*   The **Trial Announcer** will provide commentary and announce incoming waves.
*   There are 5 waves of NPCs. NPC types are randomly selected from pools configured in `mod_trial_of_finality.conf`.
*   The number of NPCs per wave scales with the number of active (not perma-deathed) players.
*   Creature levels match the highest level in the group at trial start. Later waves feature health-boosted NPCs.

### 5.3. Death, Resurrection, and Perma-Death
*   If a player dies while holding the Trial Token, they enter a "downed" ghost state for the current wave.
*   **Resurrection:** Downed players can be resurrected by teammates before the current wave ends. This removes them from the "downed" state and averts perma-death for that instance.
*   **Perma-Death:** If a wave ends and a player is still "downed", their character is flagged as `is_perma_failed = 1` in the `character_trial_finality_status` database table. This makes the character unplayable upon next login (unless reset by a GM).
    *   Note: If `TrialOfFinality.PermaDeath.ExemptGMs` is enabled, Game Master accounts are exempt from this database flag.
*   **Group Wipe:** If all active players are downed or disconnect, the trial ends in failure. All "downed" players at that point are subject to the perma-death rule.

### 5.4. Trial Success
*   Successfully defeating all 5 waves with at least one original member surviving results in trial success.
*   **Rewards (for each eligible survivor):** Configurable gold amount and a unique title.
*   Trial Tokens are removed, XP gain is re-enabled, and survivors are teleported out.

### 5.5. Trial Failure
*   Failure occurs on a group wipe, if all players are perma-deathed, or due to critical errors (e.g., misconfigured NPC pools).
*   Survivors (not perma-deathed) have tokens removed, XP re-enabled, and are teleported out.

## 6. GM Commands

Access to commands requires `SEC_GAMEMASTER` level.

*   **.trial reset <CharacterName>**
    *   Resets the Trial of Finality perma-death status for the specified character by clearing the `is_perma_failed` flag in the `character_trial_finality_status` table.
    *   Also removes the Trial Token (if online) and the old perma-death aura (if online and present) as a cleanup.
    *   Makes a perma-deathed character playable again.
*   **.trial test start**
    *   Allows a GM to start a solo test trial. Standard trial mechanics apply. The GM's perma-death outcome is subject to the `TrialOfFinality.PermaDeath.ExemptGMs` setting.

### Player Commands
*   `/trialconfirm yes` (or `/tc yes`): Confirms your participation if a Trial of Finality has been proposed for your group.
*   `/trialconfirm no` (or `/tc no`): Declines participation if a Trial of Finality has been proposed. This will typically abort the trial initiation for the group.

## 7. Logging

Key trial events are logged to the `trial_of_finality_log` database table and also to the server console (`sLog`). For details on event types and log structure, please see the [Developer Guide](docs/DEVELOPER_GUIDE.md).

## 8. Advanced Information & Development

For detailed technical information about all configuration options, internal systems, database schema, contribution guidelines, or future development notes, please refer to the [Developer Guide](docs/DEVELOPER_GUIDE.md).
