// AzerothCore includes
#include "AreaTriggerScript.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GossipDef.h"
#include "Group.h"
#include "Item.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "WorldSession.h"
#include "Log.h" // Added for logging

#include <time.h> // For time_t
#include <set> // For std::set

// Module specific namespace
namespace ModTrialOfFinality
{

// Forward declarations
// class TrialManager; // No longer needed here due to definition order

// --- Configuration Variables ---
bool ModuleEnabled = false;
uint32 FateweaverArithosEntry = 0;
uint32 AnnouncerEntry = 0;
uint32 TrialTokenEntry = 0;
uint32 TitleRewardID = 0;
uint32 GoldReward = 20000;
uint8 MinGroupSize = 1;
uint8 MaxGroupSize = 5;
uint8 MaxLevelDifference = 10;
uint16 ArenaMapID = 0;
float ArenaTeleportX = 0.0f;
float ArenaTeleportY = 0.0f;
float ArenaTeleportZ = 0.0f;
float ArenaTeleportO = 0.0f;
std::string NpcScalingMode = "match_highest_level";
std::string DisableCharacterMethod = "custom_flag";
bool GMDebugEnable = false;

// --- Helper Functions ---
// Example: void SendNotificationToGroup(Group* group, const std::string& message) { ... }

// --- Main Trial Logic ---

struct ActiveTrialInfo
{
    uint32 groupId;
    ObjectGuid leaderGuid;
    std::set<ObjectGuid> memberGuids;
    uint8 highestLevelAtStart;
    time_t startTime;

    // New fields for Step 7b
    int currentWave = 0;
    ObjectGuid announcerGuid;
    std::set<ObjectGuid> activeMonsters; // GUIDs of currently spawned wave monsters

    ActiveTrialInfo() = default;
    ActiveTrialInfo(Group* group, uint8 highestLvl) :
        groupId(group->GetId()),
        leaderGuid(group->GetLeaderGUID()),
        highestLevelAtStart(highestLvl),
        startTime(time(nullptr)),
        currentWave(0) // Initialize currentWave
    {
        memberGuids.clear(); // Ensure it's clear before populating
        if (group) // Ensure group is not null
        {
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
                if (Player* member = itr->GetSource()) {
                    memberGuids.insert(member->GetGUID());
                }
            }
        }
    }
};

class TrialManager
{
public:
    static TrialManager* instance()
    {
        static TrialManager instance;
        return &instance;
    }

    // Called from NPC gossip after validation passes
    bool InitiateTrial(Player* leader)
    {
        if (!leader || !leader->GetSession()) return false; // Added session check for leader
        Group* group = leader->GetGroup();
        if (!group) return false;

        if (m_activeTrials.count(group->GetId()))
        {
            sLog->outError("sys", "[TrialOfFinality] Attempt to start trial for group %u that is already active.", group->GetId());
            ChatHandler(leader->GetSession()).SendSysMessage("Your group seems to be already in a trial or an error occurred.");
            return false;
        }

        uint8 highestLevel = 0;
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
            if (Player* member = itr->GetSource()) {
                if (member->getLevel() > highestLevel) {
                    highestLevel = member->getLevel();
                }
            }
        }
        if (highestLevel == 0) {
            sLog->outError("sys", "[TrialOfFinality] Could not determine highest level for group %u.", group->GetId());
            ChatHandler(leader->GetSession()).SendSysMessage("Could not determine group's highest level. Aborting trial.");
            return false;
        }

        auto emplaceResult = m_activeTrials.try_emplace(group->GetId(), group, highestLevel);
        if (!emplaceResult.second) {
             sLog->outError("sys", "[TrialOfFinality] Failed to emplace trial info for group %u. Already exists?", group->GetId());
             ChatHandler(leader->GetSession()).SendSysMessage("Failed to initialize trial state. Please try again.");
             return false;
        }
        ActiveTrialInfo& currentTrial = emplaceResult.first->second; // Get reference to the new entry

        sLog->outMessage("sys", "[TrialOfFinality] Starting Trial for group ID %u, leader %s (GUID %s), %lu members. Highest level: %u. Arena: Map %u (%f,%f,%f,%f)",
            group->GetId(), leader->GetName().c_str(), leader->GetGUID().ToString().c_str(), currentTrial.memberGuids.size(), highestLevel,
            ArenaMapID, ArenaTeleportX, ArenaTeleportY, ArenaTeleportZ, ArenaTeleportO);

        // Teleport players first
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member || !member->GetSession()) continue;

            if (Item* trialToken = member->AddItem(TrialTokenEntry, 1)) {
                 member->SendNewItem(trialToken, 1, true, false);
            } else {
                sLog->outError("sys", "[TrialOfFinality] Failed to grant Trial Token (ID %u) to player %s.", TrialTokenEntry, member->GetName().c_str());
                // This is critical. Consider aborting trial for the whole group if a token can't be granted.
                // For now, log and continue, but this member will not be part of the death penalty.
            }
            member->SetDisableXpGain(true, true);
            member->TeleportTo(ArenaMapID, ArenaTeleportX, ArenaTeleportY, ArenaTeleportZ, ArenaTeleportO);
        }

        group->BroadcastGroupWillBeTeleported();
        group->SendUpdate();

        // Spawn Announcer after teleporting players, so map is valid for announcer spawn
        // Announcer spawn position (example, adjust as needed for Gurubashi Arena center area)
        Position announcerPos = {-13200.0f, 200.0f, 31.0f, 0.0f};
        Map* trialMap = sMapMgr->FindMap(ArenaMapID, 0); // Assuming non-instanced for now
        if (!trialMap) {
            sLog->outError("sys", "[TrialOfFinality] Could not find trial map %u to spawn announcer for group %u.", ArenaMapID, group->GetId());
            // TODO: Critical error - cleanup and abort trial
            ChatHandler(leader->GetSession()).SendSysMessage("Error preparing trial arena. Please contact a GM.");
            m_activeTrials.erase(group->GetId()); // Clean up trial entry
            return false;
        }

        if (Creature* announcer = trialMap->SummonCreature(AnnouncerEntry, announcerPos, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 3600 * 1000)) {
            currentTrial.announcerGuid = announcer->GetGUID();
            if (npc_trial_announcer_ai* ai = CAST_AI(npc_trial_announcer_ai*, announcer->AI())) {
                ai->SetTrialGroupId(group->GetId());
            }
            sLog->outDetail("[TrialOfFinality] Announcer (Entry %u, GUID %s) spawned for group %u.", AnnouncerEntry, announcer->GetGUID().ToString().c_str(), group->GetId());
        } else {
            sLog->outError("sys", "[TrialOfFinality] Failed to spawn Trial Announcer (Entry %u) for group %u.", AnnouncerEntry, group->GetId());
            // Not necessarily fatal for the trial if announcer is just for flavor/timing, but good to log.
            // If announcer is critical for wave spawning, then this is a bigger issue.
        }

        // Send "Trial has begun" message to group members
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
            if (Player* member = itr->GetSource()) {
                if(member->GetSession()) // Check session again as they just teleported
                    ChatHandler(member->GetSession()).SendSysMessage("The Trial of Finality has begun! Prepare yourselves!");
            }
        }

        StartFirstWave(group->GetId()); // Kick off the first wave
        return true;
    }

    void StartFirstWave(uint32 groupId) {
        ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
        if (!trialInfo) {
            sLog->outError("sys", "[TrialOfFinality] StartFirstWave: Could not find active trial for group %u", groupId);
            return;
        }

        trialInfo->currentWave = 1; // Set current wave to 1
        sLog->outMessage("sys", "[TrialOfFinality] Group %u starting processing for wave %d.", groupId, trialInfo->currentWave);

        Map* trialMap = sMapMgr->FindMap(ArenaMapID, 0); // Get map again
        if (!trialMap) {
            sLog->outError("sys", "[TrialOfFinality] StartFirstWave: Could not find map %u for group %u", ArenaMapID, groupId);
            // TODO: What to do here? Cleanup?
            return;
        }

        if (Creature* announcer = trialMap->GetCreature(trialInfo->announcerGuid)) {
             if (npc_trial_announcer_ai* ai = CAST_AI(npc_trial_announcer_ai*, announcer->AI())) {
                uint32 initialDelayMs = 5000; // 5 seconds delay before first wave spawn
                ai->AnnounceAndSpawnWave(trialInfo->currentWave, initialDelayMs);
                sLog->outDetail("[TrialOfFinality] Group %u: Announcer told to announce wave %d with delay %u ms.", groupId, trialInfo->currentWave, initialDelayMs);
            } else {
                sLog->outError("sys", "[TrialOfFinality] Group %u: Could not get Announcer AI to start wave %d.", groupId, trialInfo->currentWave);
                // Fallback or error handling if AI is missing
            }
        } else {
             sLog->outError("sys", "[TrialOfFinality] Group %u: Could not find Announcer (GUID %s) to start wave %d.", groupId, trialInfo->announcerGuid.ToString().c_str(), trialInfo->currentWave);
             // If announcer is critical, this might mean the trial cannot proceed correctly.
             // For now, the AI's event will just not fire if the announcer isn't there.
             // Consider a direct call to SpawnActualWave if announcer is optional for spawning.
        }
    }
    // For later use (e.g. on player death, disconnect, trial end)
    void RemoveTrial(uint32 groupId) {
        m_activeTrials.erase(groupId);
        sLog->outMessage("sys", "[TrialOfFinality] Removed active trial for group ID %u.", groupId);
    }

    ActiveTrialInfo* GetActiveTrialInfo(uint32 groupId) {
        auto it = m_activeTrials.find(groupId);
        if (it != m_activeTrials.end()) {
            return &it->second;
        }
        return nullptr;
    }

    void StartFirstWave(uint32 groupId); // Declaration (moved definition lower)


    // Static validation function from before
    static bool ValidateGroupForTrial(Player* leader, Creature* trialNpc)
    {
        // ... (Implementation from Step 5, unchanged) ...
        ChatHandler chat(leader->GetSession());

        Group* group = leader->GetGroup();
        if (!group)
        {
            chat.SendSysMessage("You must be in a group to start the Trial.");
            return false;
        }

        if (group->GetLeaderGUID() != leader->GetGUID())
        {
            chat.SendSysMessage("Only the group leader can initiate the Trial.");
            return false;
        }

        // 1. Group Size
        uint32 groupSize = group->GetMembersCount();
        if (groupSize < MinGroupSize || groupSize > MaxGroupSize)
        {
            chat.PSendSysMessage("Your group size must be between %u and %u players. You have %u.", MinGroupSize, MaxGroupSize, groupSize);
            return false;
        }

        // Pre-loop checks
        uint8 minPlayerLevel = 255; // MAX_LEVEL from Player.h or SharedDefines.h, using 255 for uint8
        uint8 maxPlayerLevel = 0;
        uint32 leaderMapId = leader->GetMapId();
        uint32 leaderZoneId = leader->GetZoneId();
        // Assuming trialNpc is Fateweaver Arithos, players should be near him.
        // Or, more generically, they should all be on the same map/zone as the leader.
        // Let's use leader's current location as the reference.

        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member || !member->GetSession()) continue; // Check session too for online status
            if (member->GetMapId() != leaderMapId || member->GetZoneId() != leaderZoneId) { chat.PSendSysMessage("All group members must be in the same zone as you. Player %s is not.", member->GetName().c_str()); return false; }
            if (trialNpc && !member->IsWithinDistInMap(trialNpc, 100.0f)) { chat.PSendSysMessage("All group members must be near Fateweaver Arithos. Player %s is too far.", member->GetName().c_str()); return false; }
            uint8 memberLevel = member->getLevel();
            if (memberLevel < minPlayerLevel) minPlayerLevel = memberLevel;
            if (memberLevel > maxPlayerLevel) maxPlayerLevel = memberLevel;
            // Placeholder for IsPlayerBot()
            // THIS IS A PLACEHOLDER - REQUIRES ACTUAL mod-playerbots API INTEGRATION
            if (member->IsPlayerBot()) { // Placeholder for `mod-playerbots`
                 chat.PSendSysMessage("Playerbots are not allowed in the Trial of Finality. Player %s is a bot.", member->GetName().c_str());
                 sLog->outWarning("sys", "[TrialOfFinality] Playerbot %s detected in group attempting to start trial. Leader: %s", member->GetName().c_str(), leader->GetName().c_str());
                 return false;
            }
            if (member->HasItemCount(TrialTokenEntry, 1, false)) { chat.PSendSysMessage("A member (%s) already has a Trial Token.", member->GetName().c_str()); return false; }
            // Optional: Check for disablement aura again, though OnLogin is primary
            // if (DisableCharacterMethod == "custom_flag" && member->HasAura(99999))
            // {
            //     chat.PSendSysMessage("Player %s has already failed the Trial and cannot re-enter.", member->GetName().c_str());
            //     return false;
            // }
        }
        if (maxPlayerLevel == 0 && minPlayerLevel == 255) { // No valid members found or levels not set
             chat.SendSysMessage("Could not verify group members' levels or eligibility.");
             return false;
        }
        if ((maxPlayerLevel - minPlayerLevel) > MaxLevelDifference) { chat.PSendSysMessage("Level difference between highest (%u) and lowest (%u) exceeds %u.", maxPlayerLevel, minPlayerLevel, MaxLevelDifference); return false; }
        return true;
    }


private:
    TrialManager() {} // Private constructor for singleton
    ~TrialManager() {}
    TrialManager(const TrialManager&) = delete;
    TrialManager& operator=(const TrialManager&) = delete;

    std::map<uint32 /*groupId*/, ActiveTrialInfo> m_activeTrials;
};

// --- Announcer AI and Script ---
class npc_trial_announcer_ai : public ScriptedAI
{
private:
    uint32 m_trialGroupId = 0;
    EventMap m_events;
    int m_waveToAnnounceAndSpawn = 0;

public:
    npc_trial_announcer_ai(Creature* creature) : ScriptedAI(creature) {}

    void Reset() override {
        m_events.Reset();
        m_trialGroupId = 0;
        m_waveToAnnounceAndSpawn = 0;
    }

    void SetTrialGroupId(uint32 groupId) {
        m_trialGroupId = groupId;
        sLog->outDetail("[TrialOfFinality] Announcer AI (GUID %s, Entry %u) linked to group %u.", me->GetGUID().ToString().c_str(), me->GetEntry(), m_trialGroupId);
    }

    void DoAnnounce(const std::string& text) {
        if (text.empty()) return;
        me->Say(text, LANG_UNIVERSAL, nullptr);
        sLog->outDetail("[TrialOfFinality] Announcer (Group %u): %s", m_trialGroupId, text.c_str());
    }

    void AnnounceAndSpawnWave(int waveNum, uint32 spawnDelayMs) {
        if (waveNum <= 0) return;
        m_waveToAnnounceAndSpawn = waveNum;

        std::string announcement_text = "Wave " + std::to_string(waveNum) + "! Prepare yourselves!";

        if (waveNum == 1) announcement_text = "Let the Trial of Finality commence! Your first challengers approach!";
        else if (waveNum == 5) announcement_text = "The final wave! Overcome this, and glory is yours!"; // Example: Max 5 waves

        DoAnnounce(announcement_text);

        m_events.ScheduleEvent(1 /*EVENT_SPAWN_WAVE*/, spawnDelayMs);
        sLog->outDetail("[TrialOfFinality] Announcer (Group %u) scheduled wave %d spawn in %u ms.", m_trialGroupId, waveNum, spawnDelayMs);
    }

    void UpdateAI(uint32 diff) override {
        if (!m_trialGroupId && m_events.Empty()) return; // No group or no pending events, do nothing

        m_events.Update(diff);
        switch (m_events.ExecuteEvent()) {
            case 1: // EVENT_SPAWN_WAVE
                if (m_trialGroupId != 0 && m_waveToAnnounceAndSpawn != 0) {
                    sLog->outDetail("[TrialOfFinality] Announcer (Group %u) UpdateAI: Event 1 triggered for wave %d. Calling TrialManager to spawn.", m_trialGroupId, m_waveToAnnounceAndSpawn);
                    // This will be implemented in a later sub-step to call:
                    // ModTrialOfFinality::TrialManager::instance()->SpawnActualWave(m_trialGroupId, m_waveToAnnounceAndSpawn);
                    DoAnnounce("They are here!"); // Placeholder for spawn happening
                }
                m_waveToAnnounceAndSpawn = 0;
                break;
        }
    }
};

class npc_trial_announcer : public CreatureScript
{
public:
    npc_trial_announcer() : CreatureScript("npc_trial_announcer") {}

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_trial_announcer_ai(creature);
    }
};

// --- NPC Scripts ---
enum FateweaverArithosGossipActions
{
    GOSSIP_ACTION_TEXT_ONLY_BASE = 9900,
    GOSSIP_ACTION_TEXT_NO_GROUP = 9901,
    GOSSIP_ACTION_TEXT_NOT_LEADER = 9902,
    GOSSIP_ACTION_TEXT_INFO_EXPLAIN_LINE = 9903, // Renamed for clarity

    GOSSIP_ACTION_SHOW_INFO_PAGE = 1,
    GOSSIP_ACTION_START_TRIAL = 2,
    GOSSIP_ACTION_GO_BACK_TO_MAIN_MENU = 3
};

class npc_fateweaver_arithos : public CreatureScript
{
public:
    npc_fateweaver_arithos() : CreatureScript("npc_fateweaver_arithos") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!ModuleEnabled || FateweaverArithosEntry != creature->GetEntry()) {
            return false;
        }

        ClearGossipMenuFor(player);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Tell me about the Trial of Finality.", GOSSIP_SENDER_MAIN, FateweaverArithosGossipActions::GOSSIP_ACTION_SHOW_INFO_PAGE);

        Group* group = player->GetGroup();
        if (group)
        {
            if (group->GetLeaderGUID() == player->GetGUID())
            {
                AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "I am ready to lead my group into the Trial of Finality!",
                                 GOSSIP_SENDER_MAIN, FateweaverArithosGossipActions::GOSSIP_ACTION_START_TRIAL,
                                 "The Trial is perilous and death within it is permanent for this character. Are you absolutely certain you wish to proceed?", 0, false);
            }
            else
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "(Only your party leader can initiate the trial.)", GOSSIP_SENDER_MAIN, FateweaverArithosGossipActions::GOSSIP_ACTION_TEXT_NOT_LEADER);
            }
        }
        else
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "(You must be in a group to undertake the Trial.)", GOSSIP_SENDER_MAIN, FateweaverArithosGossipActions::GOSSIP_ACTION_TEXT_NO_GROUP);
        }

        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
        return true;
    }

    void ShowInfoPage(Player* player, Creature* creature) {
        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "The Trial of Finality is a high-stakes challenge for 1 to 5 players.", GOSSIP_SENDER_MAIN, FateweaverArithosGossipActions::GOSSIP_ACTION_TEXT_INFO_EXPLAIN_LINE);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "You will face increasingly difficult waves of enemies.", GOSSIP_SENDER_MAIN, FateweaverArithosGossipActions::GOSSIP_ACTION_TEXT_INFO_EXPLAIN_LINE);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "If you die while holding the Trial Token, your character will be PERMANENTLY RETIRED.", GOSSIP_SENDER_MAIN, FateweaverArithosGossipActions::GOSSIP_ACTION_TEXT_INFO_EXPLAIN_LINE);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Success grants significant gold and the prestigious title 'Conqueror of Finality'.", GOSSIP_SENDER_MAIN, FateweaverArithosGossipActions::GOSSIP_ACTION_TEXT_INFO_EXPLAIN_LINE);
        AddGossipItemFor(player, GOSSIP_ICON_DOT, "[Go back]", GOSSIP_SENDER_MAIN, FateweaverArithosGossipActions::GOSSIP_ACTION_GO_BACK_TO_MAIN_MENU);
        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        if (!ModuleEnabled || FateweaverArithosEntry != creature->GetEntry()) {
            CloseGossipMenuFor(player);
            return true;
        }
        ClearGossipMenuFor(player);

        if (sender == GOSSIP_SENDER_MAIN) {
            switch (action) {
            case FateweaverArithosGossipActions::GOSSIP_ACTION_SHOW_INFO_PAGE:
                ShowInfoPage(player, creature);
                break;
            case FateweaverArithosGossipActions::GOSSIP_ACTION_GO_BACK_TO_MAIN_MENU:
                OnGossipHello(player, creature);
                break;
            case FateweaverArithosGossipActions::GOSSIP_ACTION_START_TRIAL:
                {
                    if (TrialManager::ValidateGroupForTrial(player, creature)) {
                        if (ModTrialOfFinality::TrialManager::instance()->InitiateTrial(player)) {
                            // Notification sent by InitiateTrial or implied by teleport
                            sLog->outMessage("sys", "[TrialOfFinality] Group validation and initiation successful for leader %s (GUID %u).", player->GetName().c_str(), player->GetGUID().GetCounter());
                        } else {
                            // InitiateTrial failed, message should have been sent by it.
                            sLog->outError("sys", "[TrialOfFinality] Trial initiation failed for leader %s after successful validation.", player->GetName().c_str(), player->GetGUID().ToString().c_str());
                        }
                    }
                    // If validation fails, ValidateGroupForTrial already sent the message.
                    CloseGossipMenuFor(player); // Always close after this attempt.
                }
                break;
            case FateweaverArithosGossipActions::GOSSIP_ACTION_TEXT_NO_GROUP:
            case FateweaverArithosGossipActions::GOSSIP_ACTION_TEXT_NOT_LEADER:
                OnGossipHello(player, creature);
                break;
            case FateweaverArithosGossipActions::GOSSIP_ACTION_TEXT_INFO_EXPLAIN_LINE:
                ShowInfoPage(player, creature);
                break;
            default:
                CloseGossipMenuFor(player);
                break;
            }
        }
        return true;
    }
};

// The new npc_trial_announcer and its AI are defined above.
// The old placeholder version of npc_trial_announcer is now removed by replacing it with nothing.

// --- Player and World Event Scripts ---
class ModPlayerScript : public PlayerScript
{
public:
    ModPlayerScript() : PlayerScript("ModTrialOfFinalityPlayerScript") {}

    void OnLogin(Player* player) override {
        if (!ModuleEnabled) return;
        // TODO: Implement actual disablement check based on DisableCharacterMethod
        // For now, this is a placeholder:
        if (DisableCharacterMethod == "custom_flag" && player->HasAura(99999)) { // Assuming 99999 is a placeholder aura for "disabled"
             player->GetSession()->SendNotification("This character is permanently disabled due to the Trial of Finality.");
             player->LogoutPlayer(true);
        }
    }

    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override {
        if (!ModuleEnabled) return;
        // TODO: Integrate with TrialManager
    }
};

class ModServerScript : public ServerScript
{
public:
    ModServerScript() : ServerScript("ModTrialOfFinalityServerScript") {}

    void OnConfigLoad(bool reload) override
    {
        sLog->outMessage("sys", "Loading Trial of Finality module configuration...");
        ModuleEnabled = sConfigMgr->GetOption<bool>("TrialOfFinality.Enable", false);

        if (!ModuleEnabled) {
            sLog->outMessage("sys", "Trial of Finality: Module disabled by configuration.");
            return;
        }

        FateweaverArithosEntry = sConfigMgr->GetOption<uint32>("TrialOfFinality.FateweaverArithos.EntryID", 0);
        AnnouncerEntry = sConfigMgr->GetOption<uint32>("TrialOfFinality.Announcer.EntryID", 0);
        TrialTokenEntry = sConfigMgr->GetOption<uint32>("TrialOfFinality.TrialToken.EntryID", 0);
        TitleRewardID = sConfigMgr->GetOption<uint32>("TrialOfFinality.TitleReward.ID", 0);
        GoldReward = sConfigMgr->GetOption<uint32>("TrialOfFinality.GoldReward", 20000);
        MinGroupSize = sConfigMgr->GetOption<uint8>("TrialOfFinality.MinGroupSize", 1);
        MaxGroupSize = sConfigMgr->GetOption<uint8>("TrialOfFinality.MaxGroupSize", 5);
        MaxLevelDifference = sConfigMgr->GetOption<uint8>("TrialOfFinality.MaxLevelDifference", 10);
        ArenaMapID = sConfigMgr->GetOption<uint16>("TrialOfFinality.Arena.MapID", 0);
        ArenaTeleportX = sConfigMgr->GetOption<float>("TrialOfFinality.Arena.TeleportX", 0.0f);
        ArenaTeleportY = sConfigMgr->GetOption<float>("TrialOfFinality.Arena.TeleportY", 0.0f);
        ArenaTeleportZ = sConfigMgr->GetOption<float>("TrialOfFinality.Arena.TeleportZ", 0.0f);
        ArenaTeleportO = sConfigMgr->GetOption<float>("TrialOfFinality.Arena.TeleportO", 0.0f);
        NpcScalingMode = sConfigMgr->GetOption<std::string>("TrialOfFinality.NpcScaling.Mode", "match_highest_level");
        DisableCharacterMethod = sConfigMgr->GetOption<std::string>("TrialOfFinality.DisableCharacter.Method", "custom_flag");
        GMDebugEnable = sConfigMgr->GetOption<bool>("TrialOfFinality.GMDebug.Enable", false);

        if (!FateweaverArithosEntry || !TrialTokenEntry || !AnnouncerEntry || !TitleRewardID) {
            sLog->outError("sys", "Trial of Finality: Critical EntryID (NPC, Item, Title) not configured. Disabling module functionality.");
            ModuleEnabled = false; // Effectively disable if critical parts are missing
            return;
        }

        sLog->outMessage("sys", "Trial of Finality: Configuration loaded. Module enabled.");
        if (reload) {
            // If config is reloaded, scripts might need to be re-registered or updated.
            // This can be complex. For now, assume scripts are registered once at startup.
            // A more robust solution might involve unregistering and re-registering scripts.
            sLog->outMessage("sys", "Trial of Finality: Configuration reloaded. Consider restarting the server for all changes to take effect if scripts were already registered.");
        }
    }
};

// --- GM Command Scripts ---
// class command_trial : public CommandScript { ... } // To be added later

} // namespace ModTrialOfFinality

// --- Script Loader ---
void Addmod_trial_of_finality_Scripts()
{
    using namespace ModTrialOfFinality;

    // Server script is always registered to load configuration
    new ModServerScript();

    // Other scripts are registered only if the module is enabled via config
    // This check relies on OnConfigLoad being called before this part of the script registration.
    // AzerothCore typically loads configs before it initializes script modules.
    if (ModuleEnabled)
    {
        sLog->outMessage("sys", "Trial of Finality: Registering Player and NPC scripts.");
        new ModPlayerScript();

        if (FateweaverArithosEntry != 0)
            new npc_fateweaver_arithos();
        else
            sLog->outError("sys", "Trial of Finality: Fateweaver Arithos NPC script not registered due to missing EntryID.");

        if (AnnouncerEntry != 0)
            new npc_trial_announcer();
        else
            sLog->outError("sys", "Trial of Finality: Trial Announcer NPC script not registered due to missing EntryID.");

        // Register GM commands if GMDebugEnable is true
        // if (GMDebugEnable) { new command_trial(); } // Placeholder
    }
    else
    {
        sLog->outMessage("sys", "Trial of Finality: Player and NPC scripts not registered as module is disabled or critical configs missing.");
    }
}

// SC_AddPlayerScripts - If this older style is used by the project for PlayerScript registration
#ifndef __clang_analyzer__
// void AddPlayerScripts(ScriptMgr* mgr)
// {
//     // This registration should also ideally check ModuleEnabled,
//     // but PlayerScripts are often registered early.
//     // The ModPlayerScript::OnLogin and other handlers will check ModuleEnabled internally.
//     // if (ModTrialOfFinality::ModuleEnabled) // This check might be too early here.
//     //    mgr->AddScript(new ModTrialOfFinality::ModPlayerScript());
// }
#endif

extern "C" void Addmod_trial_of_finality() {
    Addmod_trial_of_finality_Scripts();
}
