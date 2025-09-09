# - WORK IN PROGRESS -
# AzerothCore Module: Trial of Finality (`mod-trial-of-finality`)

## 1. Overview

`mod_trial_of_finality` is a high-risk, high-reward PvE arena challenge module for AzerothCore. It allows groups of 1-5 players to test their mettle against waves of scaled NPCs. Success yields significant gold and a unique title, but death during the trial while holding the "Trial Token" results in the character being permanently disabled and unplayable.

## 2. Features

*   Custom NPC, **Fateweaver Arithos**, to initiate and explain the trial.
*   A high-stakes, 5-wave arena challenge.
*   A **Perma-Death** mechanic for ultimate risk.
*   **Combat Resurrection** to save teammates from perma-death.
*   A **Player-Initiated Forfeit** system (`/trialforfeit`) for a graceful exit.
*   Dynamic waves that scale with group size.
*   Advanced, configurable NPC scaling (health, damage, auras).
*   Configurable rewards (gold and a unique title).
*   GM commands for testing and administration.
*   Detailed database logging of all trial events.
*   Optional NPC cheering in major cities upon a group's victory.

## 3. Installation

1.  **Copy Module Files:**
    *   `git clone https://github.com/Stuntmonkey4u/mod-trial-of-finality.git`
    *   Place the entire `mod-trial-of-finality` directory into your AzerothCore's `modules` directory.
2.  **CMake:**
    *   Add `add_subdirectory(mod-trial-of-finality)` to your server's main `CMakeLists.txt` in the `modules` section.
    *   Re-run CMake and rebuild your server.
3.  **Database Setup:**
    *   This module uses AzerothCore's integrated SQL update system. The necessary database tables (`trial_of_finality_log`, `character_trial_finality_status`) and initial data will be automatically installed on the next server startup.

## 4. Documentation

For full details on how to use, configure, and develop this module, please see the guides in the `docs/` folder:

*   **[Gameplay Guide](docs/GAMEPLAY_GUIDE.md):** Detailed information on the trial mechanics.
*   **[Configuration Guide](docs/CONFIGURATION_GUIDE.md):** A complete list of all settings in `mod_trial_of_finality.conf`.
*   **[Commands Guide](docs/COMMANDS_GUIDE.md):** A list of all GM and player commands.
*   **[Developer Guide](docs/DEVELOPER_GUIDE.md):** In-depth technical details about the module's architecture and systems.
*   **[Testing Guide](docs/testing_guide.md):** A guide for testing all features of this module.
