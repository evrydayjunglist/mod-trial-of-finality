#include "ScriptMgr.h"
#include "Player.h"
#include "Chat.h"
#include "Group.h"

class mod_trial_of_finality : public PlayerScript
{
public:
    mod_trial_of_finality() : PlayerScript("mod_trial_of_finality") {}

    void OnLogin(Player* player) override
    {
        // Prevent perma-dead players from playing
        if (player->GetUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS) == 999999)
        {
            ChatHandler(player->GetSession()).SendSysMessage("You died in the Trial of Finality. Your journey is over.");
            player->LogoutPlayer(true);
        }
    }

    void OnBeforeWorldObjectDespawn(WorldObject* obj) override
    {
        // Arena cleanup (TODO)
    }

    void OnCreatureKill(Player* killer, Creature* killed) override
    {
        // Progress tracking (TODO)
    }

    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override
    {
        if (killed->HasItemCount(99999, 1)) // Replace with actual Trial Token item ID
        {
            // Mark player as dead
            killed->SetUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS, 999999); // Example perma-death flag
            killed->SaveToDB();
            ChatHandler(killed->GetSession()).SendSysMessage("You have fallen in the Trial of Finality. This character is now retired.");
        }
    }
};

void Addmod_trial_of_finality()
{
    new mod_trial_of_finality();
}
