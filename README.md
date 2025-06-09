# Trial of Finality (AzerothCore Module)

A high-risk, high-reward arena challenge module for AzerothCore. Supports up to 5 players per trial. If a player dies during the event while holding the Trial Token, their character becomes permanently unusable.

## Features
- Group-based high-stakes combat arena
- 5 rounds of enemies scaled to the highest-level party member
- Level freeze during trial (XP gain disabled)
- Perma-death via unremovable item (Trial Token)
- 20,000g reward for survivors
- Works with `mod-playerbots` (bots excluded)
- C++ only (no Lua)

## Module Components
- Trial NPC: Fateweaver Arithos
- Arena staging announcer
- Wave controller
- Level locking
- Cleanup logic
- Logging system
- Perma-death flagging

## Setup
1. Import SQL files in `/sql/`
2. Add config from `/conf/`
3. Enable module in your `modules/` folder
4. Recompile AzerothCore

## TODO (for agent implementation)
See `docs/trial_mechanics.md` for detailed requirements.
# mod_trial_of_finality
