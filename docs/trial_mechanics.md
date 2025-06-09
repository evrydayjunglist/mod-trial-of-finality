# Trial of Finality – Mechanics Specification

## Entry Rules
- Group size: 1–5 players
- Group level range must be ≤10
- All players must be online and present
- All must accept via dialog
- Each must have 1 free inventory slot

## Flow
1. NPC checks group status
2. All players receive a non-removable Trial Token
3. XP gain is disabled for all players
4. Group is teleported to arena instance/phase
5. Arena announcer gives pre-trial speech
6. Trial begins:
   - 5 waves × 5 NPCs
   - Enemies scale to highest level in group
7. Victory: Reward gold + title
8. Death: Character becomes permanently disabled if token is in inventory

## Anti-Exploit Features
- Trial Token cannot be deleted, banked, mailed, sold
- If a player leaves mid-trial, they are flagged as disqualified
- All XP gain blocked during event
- Trial cleanup on disconnect, death, or crash

## Developer Tasks
- Implement arena logic in C++
- Add NPCs via SQL
- Add Trial Token item via SQL
- Add reward title via SQL
- Hook into OnPlayerDeath
- Log each trial start, result, and death
- Ensure perma-dead characters are unplayable
