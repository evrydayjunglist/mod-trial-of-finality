# To-Do List & Future Enhancements

This file tracks potential future enhancements for the `mod-trial-of-finality` module.

## High Priority
- [ ] **More Varied Wave Compositions:** Implement a system to allow for pre-defined "encounter groups" within the NPC pools, allowing for specific combinations of roles (e.g., a healer with two tanks) to be selected as a unit.
- [ ] **Enhanced Forfeit System:**
    - [ ] Change the forfeit vote from unanimous to majority-based.
    - [ ] Make the required vote threshold (majority vs. unanimous) configurable.
    - [ ] Implement a UI popup for voting after the forfeit is initiated, to replace the need for other players to type the command.

## Medium Priority
- [ ] **Advanced Configuration Validation:** Add more sophisticated validation for the `.conf` file at startup. For example, check if NPC pools have enough unique creatures for the configured `NUM_SPAWNS_PER_WAVE` and provide detailed feedback to the server console.

## Low Priority
- [ ] **Arena Boundary Improvements:** Investigate using AreaTriggers for more complex arena shapes instead of a simple radius check.

## Arena & Spawn Customization
- [ ] **Configurable Spawn Positions:** Move the hardcoded `WAVE_SPAWN_POSITIONS` array into the `.conf` file to allow admins to customize exactly where NPCs spawn in the arena.
- [ ] **Configurable Exit Location:** Add a new set of teleport coordinates to the `.conf` file to define where players are sent after the trial, instead of just their hearthstone location.
