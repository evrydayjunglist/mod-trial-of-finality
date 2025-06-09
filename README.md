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
*   **Dynamic Wave Scaling:** Number of NPCs per wave adjusts based on the number of currently active (not perma-deathed) players.
*   Custom **Trial Announcer** NPC for flavor dialogue and wave announcements.
*   **Perma-Death Mechanic:** Dying with the Trial Token and not being resurrected before the current wave ends results in permanent character disablement.
*   **Combat Resurrection:** Players can be resurrected by teammates (any combat-capable resurrection) during a wave to avoid perma-death for that specific death event.
*   **Rewards:** Significant gold and a custom title ("Conqueror of Finality") for successful completion by surviving members.
*   Detailed logging of trial events to a database table (`trial_of_finality_log`).
*   GM debug commands for resetting player status and testing the trial.
*   Fully configurable via `mod_trial_of_finality.conf`.
*   Designed to be compatible with `mod-playerbots` (bots are excluded from participating).

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
        7.  *(Optional)* SQL files for wave creature templates if you used custom entries (e.g., 70001, 70002, 70003) and they don't exist in your DB. Default implementation uses placeholder IDs that need to be valid.

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
*   `TrialOfFinality.DisableCharacter.Method`: (Currently "custom_flag" via Aura ID `40000`. Future DB flag possible).
*   `TrialOfFinality.GMDebug.Enable`: (Not currently used to gate GM commands, they are available if module is enabled).

Placeholder Aura ID for Perma-Death: `AURA_ID_TRIAL_PERMADEATH = 40000`. Ensure this aura ID is a passive, persistent aura in your DBCs, or change it to one that is.

Placeholder Creature IDs for Waves:
*   `CREATURE_ENTRY_WAVE_EASY = 70001`
*   `CREATURE_ENTRY_WAVE_MEDIUM = 70002`
*   `CREATURE_ENTRY_WAVE_HARD = 70003`
    Ensure these creature templates exist in your database, or update these constants in `src/mod_trial_of_finality.cpp` to use existing creature IDs.

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
    *   No member may be currently marked by the perma-death flag (`AURA_ID_TRIAL_PERMADEATH`).

### 5.2. During the Trial
*   Upon starting, all group members receive a **Trial Token**, have XP gain disabled, and are teleported to the arena.
*   The **Trial Announcer** will announce incoming waves.
*   There are 5 waves of NPCs.
*   **Wave Scaling:** The number of NPCs per wave scales based on the number of currently active (not perma-deathed) players in the trial. Creature levels are set to the highest level present in the group when the trial started. Creatures in later waves have increased health.

### 5.3. Death, Resurrection, and Perma-Death
*   If a player dies while holding the Trial Token, they enter a "downed" state for the current wave. They become a ghost.
*   **Resurrection:** Downed players can be resurrected by any combat-capable resurrection spell or item cast by a teammate *before the current wave ends*. A successful resurrection removes them from the "downed" state, and they avoid perma-death for that specific death event.
*   **Perma-Death:** If a wave ends and a player is still in the "downed" state (i.e., died and was not resurrected), they are permanently affected:
    *   The `AURA_ID_TRIAL_PERMADEATH` (placeholder `40000`) is applied.
    *   They are marked as `permanentlyFailed` for the remainder of this trial instance.
    *   The trial *continues* for any remaining active players. Future waves will scale to the new group size.
    *   Players with `AURA_ID_TRIAL_PERMADEATH` will be kicked from the game upon their next login and will be unable to log into that character again (unless a GM intervenes).
*   **Group Wipe:** If all currently active players in the trial are downed simultaneously (or disconnect), the trial ends in failure. All players who were "downed" at that point will be perma-deathed.

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
    *   Resets the Trial of Finality status for the specified character.
    *   Removes the Trial Token item from their inventory.
    *   Removes the `AURA_ID_TRIAL_PERMADEATH` (perma-death flag) if present.
    *   This allows a character to become playable again if they were perma-deathed.

*   **.trial test start**
    *   Allows a GM to start a solo test version of the Trial of Finality for themselves.
    *   Bypasses normal group validation. The GM acts as a solo participant.
    *   The trial mechanics (token, XP disable, teleport, waves, announcer, death rules) apply to the GM. Useful for testing.

## 7. Logging

*   All significant trial events (start, wave starts, player deaths/downed, resurrections, perma-deaths applied, success, failure, GM command usage) are logged to the `trial_of_finality_log` table in the `acore_world` database.
*   These events are also logged to the server console (`sLog`) with the prefix `[TrialEventSLOG]`.

## 8. Developer Notes & Future Considerations
*   The perma-death mechanism currently uses a placeholder Aura ID (`40000`). This should be verified or changed to a suitable passive, persistent aura in your DBC setup. A database flag in a custom table would be a more robust alternative for future development.
*   Creature entries for waves (`70001`, `70002`, `70003`) are placeholders. Update these in `src/mod_trial_of_finality.cpp` or create these custom NPCs.
*   Wave difficulty scaling currently adjusts NPC count and provides a basic health multiplier for later waves. More complex stat scaling or varied NPC abilities per wave could be added.
*   Consider randomizing NPC types for waves from predefined lists to enhance replayability.
*   World announcements for trial winners could be a future addition.
```
