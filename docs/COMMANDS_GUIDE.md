# Commands Guide

This guide details the GM and player commands for the Trial of Finality module.

## GM Commands

Access to commands requires `SEC_GAMEMASTER` level.

*   **.trial reset <CharacterName>**
    *   Resets the Trial of Finality perma-death status for the specified character by clearing the `is_perma_failed` flag in the `character_trial_finality_status` table.
    *   Also removes the Trial Token (if online) and the old perma-death aura (if online and present) as a cleanup.
    *   Makes a perma-deathed character playable again.
*   **.trial test**
    *   Allows a GM who is not in a group to start a solo test trial. Standard trial mechanics apply. The GM's perma-death outcome is subject to the `TrialOfFinality.PermaDeath.ExemptGMs` setting.

## Player Commands
*   `/trialconfirm yes` (or `/tc yes`): Confirms your participation if a Trial of Finality has been proposed for your group.
*   `/trialconfirm no` (or `/tc no`): Declines participation if a Trial of Finality has been proposed. This will typically abort the trial initiation for the group.
*   `/trialforfeit` (or `/tf`): Initiates a vote to forfeit the trial. If all active members vote to forfeit, the trial ends gracefully with no perma-death penalties.
