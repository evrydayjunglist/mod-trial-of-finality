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
#include "Log.h"
#include "ChatCommand.h"
#include "World.h"

#include <time.h>
#include <set>
#include <sstream>
#include <map>
#include <cmath>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <string>

#include "ObjectAccessor.h"
#include "Player.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "ObjectGuid.h"

// Module specific namespace
namespace ModTrialOfFinality
{

// --- Logging Enum and Function ---
enum TrialEventType {
    TRIAL_EVENT_START,
    TRIAL_EVENT_WAVE_START,
    TRIAL_EVENT_PLAYER_DEATH_TOKEN,
    TRIAL_EVENT_TRIAL_SUCCESS,
    TRIAL_EVENT_TRIAL_FAILURE,
    TRIAL_EVENT_GM_COMMAND_RESET,
    TRIAL_EVENT_GM_COMMAND_TEST_START,
    TRIAL_EVENT_PLAYER_RESURRECTED,
    TRIAL_EVENT_PERMADEATH_APPLIED,
    TRIAL_EVENT_PLAYER_DISCONNECT,
    TRIAL_EVENT_PLAYER_RECONNECT,
    TRIAL_EVENT_STRAY_TOKEN_REMOVED,
    TRIAL_EVENT_PLAYER_WARNED_ARENA_LEAVE,
    TRIAL_EVENT_PLAYER_FORFEIT_ARENA,
    TRIAL_EVENT_WORLD_ANNOUNCEMENT_SUCCESS,
    TRIAL_EVENT_NPC_CHEER_TRIGGERED,
    TRIAL_EVENT_FORFEIT_VOTE_START,
    TRIAL_EVENT_FORFEIT_VOTE_CANCEL,
    TRIAL_EVENT_FORFEIT_VOTE_SUCCESS
};

void LogTrialDbEvent(TrialEventType eventType, uint32 groupId = 0, Player* player = nullptr,
                     int waveNumber = 0, uint8 highestLevel = 0, const std::string& details = "") {
    std::string eventTypeStr = "UNKNOWN";
    switch (eventType) {
        case TRIAL_EVENT_START: eventTypeStr = "TRIAL_START"; break;
        case TRIAL_EVENT_WAVE_START: eventTypeStr = "WAVE_START"; break;
        case TRIAL_EVENT_PLAYER_DEATH_TOKEN: eventTypeStr = "PLAYER_DEATH_TOKEN"; break;
        case TRIAL_EVENT_TRIAL_SUCCESS: eventTypeStr = "TRIAL_SUCCESS"; break;
        case TRIAL_EVENT_TRIAL_FAILURE: eventTypeStr = "TRIAL_FAILURE"; break;
        case TRIAL_EVENT_GM_COMMAND_RESET: eventTypeStr = "GM_COMMAND_RESET"; break;
        case TRIAL_EVENT_GM_COMMAND_TEST_START: eventTypeStr = "GM_COMMAND_TEST_START"; break;
        case TRIAL_EVENT_PLAYER_RESURRECTED: eventTypeStr = "PLAYER_RESURRECTED"; break;
        case TRIAL_EVENT_PERMADEATH_APPLIED: eventTypeStr = "PERMADEATH_APPLIED"; break;
        case TRIAL_EVENT_PLAYER_DISCONNECT: eventTypeStr = "PLAYER_DISCONNECT"; break;
        case TRIAL_EVENT_PLAYER_RECONNECT: eventTypeStr = "PLAYER_RECONNECT"; break;
        case TRIAL_EVENT_STRAY_TOKEN_REMOVED: eventTypeStr = "STRAY_TOKEN_REMOVED"; break;
        case TRIAL_EVENT_PLAYER_WARNED_ARENA_LEAVE: eventTypeStr = "PLAYER_WARNED_ARENA_LEAVE"; break;
        case TRIAL_EVENT_PLAYER_FORFEIT_ARENA: eventTypeStr = "PLAYER_FORFEIT_ARENA"; break;
        case TRIAL_EVENT_WORLD_ANNOUNCEMENT_SUCCESS: eventTypeStr = "WORLD_ANNOUNCEMENT_SUCCESS"; break;
        case TRIAL_EVENT_NPC_CHEER_TRIGGERED: eventTypeStr = "NPC_CHEER_TRIGGERED"; break;
    }

    ObjectGuid playerGuid = player ? player->GetGUID() : ObjectGuid::Empty;
    std::string playerName_s = player ? player->GetName() : "";
    uint32 accountId = player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;

    std::ostringstream slog_message;
    slog_message << "[TrialEventSLOG] Type: " << eventTypeStr
            << ", GroupID: " << groupId
            << ", PlayerGUID: " << playerGuid.GetCounter()
            << ", PlayerName: " << (playerName_s.empty() ? "N/A" : playerName_s)
            << ", AccountID: " << accountId
            << ", HighestLvl: " << (unsigned int)highestLevel
            << ", Wave: " << waveNumber
            << ", Details: '" << details << "'";
    sLog->outMessage("sys", "%s", slog_message.str().c_str());

    std::string escaped_details = details;
    if (!details.empty()) { CharacterDatabase.EscapeString(escaped_details); }
    std::string escaped_player_name = playerName_s;
    if (!playerName_s.empty()) { CharacterDatabase.EscapeString(escaped_player_name); }

    CharacterDatabase.ExecuteFmt(
        "INSERT INTO trial_of_finality_log (event_type, group_id, player_guid, player_name, player_account_id, highest_level_in_group, wave_number, details) "
        "VALUES ('%s', %s, %s, %s, %s, %s, %s, %s)",
        eventTypeStr.c_str(),
        (groupId == 0 ? "NULL" : std::to_string(groupId).c_str()),
        (playerGuid.IsEmpty() ? "NULL" : std::to_string(playerGuid.GetCounter()).c_str()),
        (playerName_s.empty() ? "NULL" : ("'" + escaped_player_name + "'").c_str()),
        (accountId == 0 ? "NULL" : std::to_string(accountId).c_str()),
        (highestLevel == 0 ? "NULL" : std::to_string(highestLevel).c_str()),
        std::to_string(waveNumber).c_str(),
        (details.empty() ? "NULL" : ("'" + escaped_details + "'").c_str())
    );
}

// --- Creature ID Pools for Waves (Now loaded from config) ---
// Each inner vector represents a "spawn choice", which can be a single creature or a pre-defined group.
std::vector<std::vector<uint32>> NpcPoolEasy;
std::vector<std::vector<uint32>> NpcPoolMedium;
std::vector<std::vector<uint32>> NpcPoolHard;

// --- Wave Spawn Positions (Now loaded from config) ---
std::vector<Position> WAVE_SPAWN_POSITIONS;

// --- Configuration Variables ---
const uint32 AURA_ID_TRIAL_PERMADEATH = 40000;
bool ModuleEnabled = false;
uint32 FateweaverArithosEntry = 0;
uint32 AnnouncerEntry = 0;
uint32 TrialTokenEntry = 0;
uint32 TitleRewardID = 0;
uint32 GoldReward = 200000000;
uint8 MinGroupSize = 1;
uint8 MaxGroupSize = 5;
uint8 MaxLevelDifference = 10;
uint16 ArenaMapID = 0;
float ArenaTeleportX = 0.0f;
float ArenaTeleportY = 0.0f;
float ArenaTeleportZ = 0.0f;
float ArenaTeleportO = 0.0f;
float ArenaRadius = 100.0f;
bool ExitOverrideHearthstone = false;
uint16 ExitMapID = 0;
float ExitTeleportX = 0.0f;
float ExitTeleportY = 0.0f;
float ExitTeleportZ = 0.0f;
float ExitTeleportO = 0.0f;
std::string NpcScalingMode = "match_highest_level";
std::string DisableCharacterMethod = "custom_flag";
bool GMDebugEnable = false;
bool GMDebugAllowPlayerbots = false;
bool WorldAnnounceEnable = true;
std::string WorldAnnounceFormat = "Hark, heroes! The group led by {group_leader}, with valiant trialists {player_list}, has vanquished all foes and emerged victorious from the Trial of Finality! All hail the Conquerors!";
bool CheeringNpcsEnable = true;
std::set<uint32> CheeringNpcCityZoneIDs;
float CheeringNpcsRadiusAroundPlayer = 40.0f;
int CheeringNpcsMaxPerPlayerCluster = 5;
int CheeringNpcsMaxTotalWorld = 50;
uint32 CheeringNpcsTargetNpcFlags = UNIT_NPC_FLAG_NONE;
uint32 CheeringNpcsExcludeNpcFlags = UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_FLIGHTMASTER | UNIT_NPC_FLAG_REPAIRER | UNIT_NPC_FLAG_AUCTIONEER | UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_TABARDDESIGNER | UNIT_NPC_FLAG_STABLEMASTER | UNIT_NPC_FLAG_GUILDMASTER | UNIT_NPC_FLAG_BATTLEMASTER | UNIT_NPC_FLAG_INNKEEPER | UNIT_NPC_FLAG_SPIRITHEALER | UNIT_NPC_FLAG_SPIRITGUIDE | UNIT_NPC_FLAG_PETITIONER;
uint32 CheeringNpcsCheerIntervalMs = 2000; // Interval for the second cheer
bool PermaDeathExemptGMs = true; // Exempt GMs from perma-death
uint32 ConfirmationTimeoutSeconds = 60; // Seconds for players to confirm trial participation
bool ConfirmationEnable = true; // Enable/disable the confirmation system
std::string ConfirmationRequiredMode = "all"; // "all", (future: "majority", "leader_plus_one")
bool ForfeitEnable = true; // Enable/disable the forfeit feature

// --- Custom NPC Scaling Settings ---
struct CustomNpcScalingTier {
    float HealthMultiplier = 1.0f;
    std::vector<uint32> AurasToAdd;
};
CustomNpcScalingTier CustomScalingEasy;
CustomNpcScalingTier CustomScalingMedium;
CustomNpcScalingTier CustomScalingHard;


// --- Main Trial Logic ---

// Structure to hold information about trials pending player confirmation
struct PendingTrialInfo {
    ObjectGuid leaderGuid;
    std::set<ObjectGuid> memberGuidsToConfirm; // All members (excluding leader) who need to say yes
    std::set<ObjectGuid> memberGuidsAccepted;
    time_t creationTime;
    uint8 highestLevelAtStart;

    PendingTrialInfo(ObjectGuid leader, uint8 highestLvl)
        : leaderGuid(leader), creationTime(time(nullptr)), highestLevelAtStart(highestLvl) {}

    // Helper to check if a specific member still needs to confirm
    bool IsStillPending(ObjectGuid memberGuid) const {
        if (memberGuidsAccepted.count(memberGuid)) return false; // Already accepted
        if (memberGuidsToConfirm.count(memberGuid)) return true; // In the list of those who need to confirm and hasn't accepted
        return false;
    }
};

struct ActiveTrialInfo {
    uint32 groupId;
    ObjectGuid leaderGuid;
    std::set<ObjectGuid> memberGuids;
    uint8 highestLevelAtStart;
    time_t startTime;
    int currentWave = 0;
    ObjectGuid announcerGuid;
    std::set<ObjectGuid> activeMonsters;
    std::map<ObjectGuid, time_t> downedPlayerGuids;
    std::set<ObjectGuid> permanentlyFailedPlayerGuids;
    std::set<ObjectGuid> playersWarnedForLeavingArena;
    bool isTestTrial = false;

    // Forfeit Vote
    bool forfeitVoteInProgress = false;
    time_t forfeitVoteStartTime = 0;
    std::set<ObjectGuid> playersWhoVotedForfeit;

    ActiveTrialInfo() = default;
    ActiveTrialInfo(Group* group, uint8 highestLvl) :
        groupId(group->GetId()), leaderGuid(group->GetLeaderGUID()),
        highestLevelAtStart(highestLvl), startTime(time(nullptr)), currentWave(0)
    {
        memberGuids.clear();
        downedPlayerGuids.clear();
        permanentlyFailedPlayerGuids.clear();
        activeMonsters.clear();
        playersWarnedForLeavingArena.clear();
        playersWhoVotedForfeit.clear();
        if (group) {
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
                if (Player* member = itr->GetSource()) { memberGuids.insert(member->GetGUID()); }
            }
        }
    }
};

struct PendingSecondCheer {
    ObjectGuid npcGuid;
    time_t cheerTime;
};

class TrialManager
{
public:
    static TrialManager* instance() { static TrialManager instance; return &instance; }
    bool InitiateTrial(Player* leader);
    void PrepareAndAnnounceWave(uint32 groupId, int waveNumber, uint32 delayMs);
    void SpawnActualWave(uint32 groupId);
    void HandleMonsterKilledInTrial(ObjectGuid monsterGuid, uint32 groupId);
    void HandlePlayerDownedInTrial(Player* downedPlayer);
    void FinalizeTrialOutcome(uint32 groupId, bool overallSuccess, const std::string& reason);
    void HandleTrialFailure(uint32 groupId, const std::string& reason);
    void CleanupTrial(uint32 groupId, bool success);
    bool StartTestTrial(Player* gmPlayer);
    void CheckPlayerLocationsAndEnforceBoundaries(uint32 groupId);
    void TriggerCityNpcCheers(uint32 successfulGroupId);

    ActiveTrialInfo* GetActiveTrialInfo(uint32 groupId) {
        auto it = m_activeTrials.find(groupId);
        if (it != m_activeTrials.end()) { return &it->second; }
        return nullptr;
    }
    static bool ValidateGroupForTrial(Player* leader, Creature* trialNpc);

    // --- Confirmation System ---
    void HandleTrialConfirmation(Player* player, bool accepted);
    void StartConfirmedTrial(uint32 groupId);
    void AbortPendingTrial(uint32 groupId, const std::string& reason, Player* originator = nullptr);
    void OnUpdate(uint32 diff);
    void HandleTrialForfeit(Player* player);

private:
    time_t m_lastPendingCheck = 0; // To throttle pending check updates
    time_t m_lastBoundaryCheck = 0;
    time_t m_lastCheerCheck = 0;
    TrialManager() {}
    ~TrialManager() {}
    TrialManager(const TrialManager&) = delete;
    TrialManager& operator=(const TrialManager&) = delete;
    std::map<uint32, ActiveTrialInfo> m_activeTrials;
    std::map<uint32 /*groupId*/, PendingTrialInfo> m_pendingTrials;
    std::vector<PendingSecondCheer> m_pendingSecondCheers;
};

// --- TrialManager Method Implementations ---

void TrialManager::StartConfirmedTrial(uint32 groupId) {
    auto it = m_pendingTrials.find(groupId);
    if (it == m_pendingTrials.end()) {
        sLog->outError("sys", "[TrialOfFinality] StartConfirmedTrial called for group %u, but no pending trial found.", groupId);
        return;
    }
    PendingTrialInfo pendingInfo = it->second;
    m_pendingTrials.erase(it);

    Group* group = sGroupMgr->GetGroupById(groupId);
    if (!group) {
        sLog->outError("sys", "[TrialOfFinality] Group %u for confirmed trial not found.", groupId);
        return;
    }
    Player* leader = ObjectAccessor::FindPlayer(pendingInfo.leaderGuid);
    if(!leader) {
         sLog->outError("sys", "[TrialOfFinality] Group %u leader for confirmed trial not found.", groupId);
         return;
    }

    // FINAL VALIDATION - re-run just in case something changed (e.g. member left)
    // This is a simplified validation. A more robust one might re-check everything.
    uint32 onlineMembersCount = 0;
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
        if (Player* member = itr->GetSource()) {
            if (member->GetSession()) onlineMembersCount++;
        }
    }
    if (onlineMembersCount < MinGroupSize) {
        ChatHandler(leader->GetSession()).SendSysMessage("The trial cannot begin; your group no longer meets the minimum size requirement.");
        sLog->outWarn("sys", "[TrialOfFinality] Group %u aborted at final confirmation step: size is now %u, required %u.", groupId, onlineMembersCount, MinGroupSize);
        return;
    }

    sLog->outInfo("sys", "[TrialOfFinality] Group %u has confirmed. Starting trial.", groupId);

    // Create and store the ActiveTrialInfo
    m_activeTrials[groupId] = ActiveTrialInfo(group, pendingInfo.highestLevelAtStart);
    ActiveTrialInfo* newTrial = &m_activeTrials[groupId];

    // Grant tokens, disable XP, teleport players
    for (const auto& memberGuid : newTrial->memberGuids) {
        Player* member = ObjectAccessor::FindPlayer(memberGuid);
        if (member && member->GetSession()) {
            // Disable XP Gain
            member->SetDisableXpGain(true, true);
            // Grant Trial Token
            member->AddItem(TrialTokenEntry, 1);
            // Teleport to Arena
            member->TeleportTo(ArenaMapID, ArenaTeleportX, ArenaTeleportY, ArenaTeleportZ, ArenaTeleportO);
            ChatHandler(member->GetSession()).SendSysMessage("The Trial of Finality has begun!");
        }
    }

    LogTrialDbEvent(TRIAL_EVENT_START, groupId, leader, 0, newTrial->highestLevelAtStart, "Trial started after confirmation.");

    // Start wave 1
    PrepareAndAnnounceWave(groupId, 1, 5000); // 5 second delay before first wave announcement
}


void TrialManager::AbortPendingTrial(uint32 groupId, const std::string& reason, Player* originator) {
    auto it = m_pendingTrials.find(groupId);
    if (it == m_pendingTrials.end()) {
        return; // Already aborted or started
    }

    PendingTrialInfo pendingInfo = it->second;
    m_pendingTrials.erase(it);

    sLog->outInfo("sys", "[TrialOfFinality] Pending trial for group %u aborted. Reason: %s", groupId, reason.c_str());

    // Notify all original members who were pending
    std::set<ObjectGuid> membersToNotify = pendingInfo.memberGuidsToConfirm;
    membersToNotify.insert(pendingInfo.leaderGuid);

    for (const auto& guid : membersToNotify) {
        Player* member = ObjectAccessor::FindPlayer(guid);
        if (member && member->GetSession()) {
            if (originator) {
                 ChatHandler(member->GetSession()).PSendSysMessage("The Trial of Finality was aborted by %s. Reason: %s", originator->GetName().c_str(), reason.c_str());
            } else {
                 ChatHandler(member->GetSession()).PSendSysMessage("The Trial of Finality was aborted. Reason: %s", reason.c_str());
            }
        }
    }
}

void TrialManager::HandleTrialForfeit(Player* player) {
    if (!player || !player->GetGroup()) {
        ChatHandler(player->GetSession()).SendSysMessage("You must be in a group to use this command.");
        return;
    }

    uint32 groupId = player->GetGroup()->GetId();
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo) {
        ChatHandler(player->GetSession()).SendSysMessage("Your group is not in an active Trial of Finality.");
        return;
    }

    if (trialInfo->playersWhoVotedForfeit.count(player->GetGUID())) {
        ChatHandler(player->GetSession()).SendSysMessage("You have already voted to forfeit.");
        return;
    }

    // Count active players for voting threshold
    uint32 activePlayers = 0;
    for (const auto& memberGuid : trialInfo->memberGuids) {
        if (!trialInfo->permanentlyFailedPlayerGuids.count(memberGuid)) {
            Player* p = ObjectAccessor::FindPlayer(memberGuid);
            if (p && p->GetSession() && p->IsAlive()) {
                activePlayers++;
            }
        }
    }
    if (activePlayers == 0) { // Should not happen if player is able to type command, but as a safeguard
        ChatHandler(player->GetSession()).SendSysMessage("There are no active players to vote.");
        return;
    }

    if (!trialInfo->forfeitVoteInProgress) {
        trialInfo->forfeitVoteInProgress = true;
        trialInfo->forfeitVoteStartTime = time(nullptr);
        trialInfo->playersWhoVotedForfeit.insert(player->GetGUID());

        std::string msg = player->GetName() + " has initiated a vote to forfeit the Trial of Finality! All other active members must type `/trialforfeit` within 30 seconds to agree. (1/" + std::to_string(activePlayers) + " votes)";
        LogTrialDbEvent(TRIAL_EVENT_FORFEIT_VOTE_START, groupId, player, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Forfeit vote started.");

        for (const auto& memberGuid : trialInfo->memberGuids) {
            if (Player* member = ObjectAccessor::FindPlayer(memberGuid)) {
                if (member->GetSession()) ChatHandler(member->GetSession()).SendSysMessage(msg);
            }
        }
    } else {
        trialInfo->playersWhoVotedForfeit.insert(player->GetGUID());
        std::string msg = player->GetName() + " has also voted to forfeit. (" + std::to_string(trialInfo->playersWhoVotedForfeit.size()) + "/" + std::to_string(activePlayers) + " votes)";

        for (const auto& memberGuid : trialInfo->memberGuids) {
            if (Player* member = ObjectAccessor::FindPlayer(memberGuid)) {
                if (member->GetSession()) ChatHandler(member->GetSession()).SendSysMessage(msg);
            }
        }
    }

    // Check if all active players have voted
    if (trialInfo->playersWhoVotedForfeit.size() >= activePlayers) {
        std::string reason = "The group has unanimously voted to forfeit the trial.";
        LogTrialDbEvent(TRIAL_EVENT_FORFEIT_VOTE_SUCCESS, groupId, player, trialInfo->currentWave, trialInfo->highestLevelAtStart, reason);
        // A forfeit does not trigger permadeath, it's a graceful exit.
        // We call cleanup directly, not FinalizeTrialOutcome with failure.
        // This means players who are downed are just teleported out, not punished.
        CleanupTrial(groupId, false);
    }
}

void TrialManager::HandleTrialConfirmation(Player* player, bool accepted) {
    if (!player || !player->GetGroup()) {
        ChatHandler(player->GetSession()).SendSysMessage("You must be in a group to use this command.");
        return;
    }
    uint32 groupId = player->GetGroup()->GetId();
    auto it = m_pendingTrials.find(groupId);
    if (it == m_pendingTrials.end()) {
        ChatHandler(player->GetSession()).SendSysMessage("There is no pending trial confirmation for your group.");
        return;
    }

    PendingTrialInfo* pendingInfo = &it->second;

    // Check if this player was actually prompted
    if (!pendingInfo->memberGuidsToConfirm.count(player->GetGUID())) {
        ChatHandler(player->GetSession()).SendSysMessage("You were not required to confirm for this trial.");
        return;
    }

    if (pendingInfo->memberGuidsAccepted.count(player->GetGUID())) {
        ChatHandler(player->GetSession()).SendSysMessage("You have already accepted the trial.");
        return;
    }

    if (!accepted) {
        // Abort the trial
        AbortPendingTrial(groupId, "A member declined the invitation.", player);
        return;
    }

    // Player accepted
    pendingInfo->memberGuidsAccepted.insert(player->GetGUID());
    sLog->outDetail("[TrialOfFinality] Player %s (Group %u) accepted the trial. (%lu/%lu accepted)",
        player->GetName().c_str(), groupId, pendingInfo->memberGuidsAccepted.size(), pendingInfo->memberGuidsToConfirm.size());

    // Notify the group of the acceptance
     for (const auto& guid : pendingInfo->memberGuidsToConfirm) {
        Player* member = ObjectAccessor::FindPlayer(guid);
        if (member && member->GetSession()) {
            ChatHandler(member->GetSession()).PSendSysMessage("%s has ACCEPTED the Trial of Finality.", player->GetName().c_str());
        }
    }
    Player* leader = ObjectAccessor::FindPlayer(pendingInfo->leaderGuid);
    if(leader && leader->GetSession()) {
        ChatHandler(leader->GetSession()).PSendSysMessage("%s has ACCEPTED the Trial of Finality.", player->GetName().c_str());
    }


    // Check if all have accepted
    if (pendingInfo->memberGuidsAccepted.size() == pendingInfo->memberGuidsToConfirm.size()) {
        sLog->outInfo("sys", "[TrialOfFinality] All members for group %u have accepted. Starting trial.", groupId);
        // Notify everyone that the trial is starting
        std::set<ObjectGuid> allMembers = pendingInfo->memberGuidsToConfirm;
        allMembers.insert(pendingInfo->leaderGuid);
        for (const auto& guid : allMembers) {
            Player* member = ObjectAccessor::FindPlayer(guid);
            if (member && member->GetSession()) {
                ChatHandler(member->GetSession()).SendSysMessage("All members have confirmed! The trial is about to begin!");
            }
        }
        StartConfirmedTrial(groupId);
    }
}

void TrialManager::OnUpdate(uint32 diff) {
    // --- Pending Confirmations Check ---
    m_lastPendingCheck += diff;
    if (m_lastPendingCheck >= 2000) { // Check every 2 seconds
        m_lastPendingCheck = 0;
        if (!m_pendingTrials.empty()) {
            time_t now = time(nullptr);
            std::vector<uint32> timedOutGroupIds;
            for (const auto& pair : m_pendingTrials) {
                if (now - pair.second.creationTime > ConfirmationTimeoutSeconds) {
                    timedOutGroupIds.push_back(pair.first);
                }
            }
            for (uint32 groupId : timedOutGroupIds) {
                AbortPendingTrial(groupId, "The confirmation request timed out.");
            }
        }
    }

    // --- Active Trial Boundary Check ---
    m_lastBoundaryCheck += diff;
    if (m_lastBoundaryCheck >= 5000) { // Check every 5 seconds
        m_lastBoundaryCheck = 0;
        if (!m_activeTrials.empty()) {
            std::vector<uint32> groupIds;
            for(auto const& [key, val] : m_activeTrials) { groupIds.push_back(key); }
            for (uint32 groupId : groupIds) {
                if(GetActiveTrialInfo(groupId)) { CheckPlayerLocationsAndEnforceBoundaries(groupId); }
            }
        }
    }

    // --- Active Trial Forfeit & Cheer Checks ---
    m_lastCheerCheck += diff;
    if (m_lastCheerCheck >= 1000) { // Check every second
        m_lastCheerCheck = 0;
        time_t now = time(nullptr);

        // Second Cheer
        if (!m_pendingSecondCheers.empty()) {
            auto it = m_pendingSecondCheers.begin();
            while (it != m_pendingSecondCheers.end()) {
                if (now >= it->cheerTime) {
                    if (Creature* npc = ObjectAccessor::GetCreature(*sWorld, it->npcGuid)) {
                        if (npc->IsAlive() && !npc->IsInCombat()) { npc->HandleEmoteCommand(EMOTE_ONESHOT_CHEER); }
                    }
                    it = m_pendingSecondCheers.erase(it);
                } else { ++it; }
            }
        }

        // Forfeit Vote Checks
        if (!m_activeTrials.empty()) {
            // Create a copy of keys because the vote cancellation logic can modify m_activeTrials, invalidating iterators
            std::vector<uint32> groupIds;
            for(auto const& [key, val] : m_activeTrials) { groupIds.push_back(key); }

            for (uint32 groupId : groupIds) {
                ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
                if (!trialInfo || !trialInfo->forfeitVoteInProgress) {
                    continue;
                }

                // Timeout Check
                if (now - trialInfo->forfeitVoteStartTime > 30) {
                    trialInfo->forfeitVoteInProgress = false;
                    trialInfo->playersWhoVotedForfeit.clear();
                    std::string msg = "The vote to forfeit the trial has failed to pass in time and is now cancelled.";
                    LogTrialDbEvent(TRIAL_EVENT_FORFEIT_VOTE_CANCEL, groupId, nullptr, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Vote timed out.");
                    for (const auto& memberGuid : trialInfo->memberGuids) {
                        if (Player* member = ObjectAccessor::FindPlayer(memberGuid)) {
                            if (member->GetSession()) ChatHandler(member->GetSession()).SendSysMessage(msg);
                        }
                    }
                } else { // Only do this check if not timed out
                    // Active player count check to prevent solo forfeit exploit
                    uint32 activePlayers = 0;
                    for (const auto& guid : trialInfo->memberGuids) {
                        if (!trialInfo->permanentlyFailedPlayerGuids.count(guid)) {
                            if (Player* p = ObjectAccessor::FindPlayer(guid)) {
                                if (p->GetSession() && p->IsAlive()) activePlayers++;
                            }
                        }
                    }
                    if (activePlayers < 2) {
                        trialInfo->forfeitVoteInProgress = false;
                        trialInfo->playersWhoVotedForfeit.clear();
                        std::string msg = "The vote to forfeit was cancelled because there are no longer enough active players to vote.";
                        LogTrialDbEvent(TRIAL_EVENT_FORFEIT_VOTE_CANCEL, groupId, nullptr, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Not enough active players.");
                        for (const auto& memberGuid : trialInfo->memberGuids) {
                            if (Player* member = ObjectAccessor::FindPlayer(memberGuid)) {
                                if (member->GetSession()) ChatHandler(member->GetSession()).SendSysMessage(msg);
                            }
                        }
                    }
                }
            }
        }
    }
}

bool TrialManager::InitiateTrial(Player* leader) {
    if (!leader || !leader->GetSession()) return false;
    Group* group = leader->GetGroup();
    if (!group) {
        ChatHandler(leader->GetSession()).SendSysMessage("You must be in a group to initiate the Trial.");
        return false;
    }

    // Existing validation call - this should check critical things like location, perma-death, etc.
    // Assuming ValidateGroupForTrial is static and accessible, or called by the NPC script before this.
    // For this refactor, we assume basic group validity is checked by the caller (NPC script via ValidateGroupForTrial).
    // Here, we focus on trial-specific states (active/pending) and group composition for confirmation.

    // Initial Validations (leader, group exist already done by caller or first lines)
    // Check if already active or pending
    if (m_activeTrials.count(group->GetId())) {
        sLog->outError("sys", "[TrialOfFinality] Attempt to start trial for group %u that is already active.", group->GetId());
        ChatHandler(leader->GetSession()).SendSysMessage("Your group is already in an active Trial of Finality.");
        return false;
    }
    if (m_pendingTrials.count(group->GetId())) {
        ChatHandler(leader->GetSession()).SendSysMessage("Your group already has a pending Trial of Finality confirmation. Please wait or have members respond.");
        return false;
    }

    // Determine highestLevel and onlineMembers for validation and PendingTrialInfo
    uint8 highestLevel = 0;
    uint32 onlineMembers = 0;
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
        if (Player* member = itr->GetSource()) {
            if (!member->GetSession()) {
                continue;
            }
            onlineMembers++;
            if (member->getLevel() > highestLevel) highestLevel = member->getLevel();
        }
    }

    if (highestLevel == 0 || onlineMembers < MinGroupSize) {
        sLog->outError("sys", "[TrialOfFinality] Group %u: Could not determine highest level or not enough online members (%u found, %u required for confirmation start).", group->GetId(), onlineMembers, MinGroupSize);
        ChatHandler(leader->GetSession()).SendSysMessage("Could not determine group's highest level or not enough online members meeting criteria for trial start.");
        return false;
    }

    // If confirmation system is disabled, bypass confirmation and start directly
    if (!ConfirmationEnable) {
        sLog->outInfo("sys", "[TrialOfFinality] Group %u (Leader: %s) starting trial directly as confirmation system is disabled.", group->GetId(), leader->GetName().c_str());
        PendingTrialInfo tempPendingInfo(leader->GetGUID(), highestLevel);
        // Populate members for StartConfirmedTrial to use (all online members including leader)
        // In a disabled-confirmation scenario, all present and eligible members are considered "accepted".
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
            if (Player* member = itr->GetSource()) {
                if (member->GetSession()) { // Ensure member is online
                     // For direct start, all members who would be prompted are considered "to confirm"
                     // and also immediately "accepted".
                     tempPendingInfo.memberGuidsToConfirm.insert(member->GetGUID());
                }
            }
        }
        // Leader is implicitly accepted. Others are added to both lists.
        tempPendingInfo.memberGuidsAccepted = tempPendingInfo.memberGuidsToConfirm;
        if (!tempPendingInfo.memberGuidsAccepted.count(leader->GetGUID())) { // Ensure leader is in accepted if not prompted
             tempPendingInfo.memberGuidsAccepted.insert(leader->GetGUID());
        }


        m_pendingTrials[group->GetId()] = tempPendingInfo; // Add briefly for StartConfirmedTrial
        StartConfirmedTrial(group->GetId());
        return true;
    }

    PendingTrialInfo pendingInfo(leader->GetGUID(), highestLevel);
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
        Player* member = itr->GetSource();
        // Only prompt online members who are not the leader
        if (member && member->GetSession() && member->GetGUID() != leader->GetGUID()) {
            pendingInfo.memberGuidsToConfirm.insert(member->GetGUID());
        }
    }

    m_pendingTrials[group->GetId()] = pendingInfo; // Add to map

    sLog->outInfo("sys", "[TrialOfFinality] Group %u (Leader: %s, GUID %u) initiated confirmation phase. %lu members to confirm. Timeout: %u s.",
        group->GetId(), leader->GetName().c_str(), leader->GetGUID().GetCounter(), pendingInfo.memberGuidsToConfirm.size(), ConfirmationTimeoutSeconds);

    if (pendingInfo.memberGuidsToConfirm.empty()) {
        sLog->outInfo("sys", "[TrialOfFinality] Group %u (Leader: %s) has no other members to confirm. Proceeding to start trial directly.", group->GetId(), leader->GetName().c_str());
        StartConfirmedTrial(group->GetId());
        // StartConfirmedTrial will handle moving from m_pendingTrials to m_activeTrials
        return true;
    }

    // Send prompts
    std::string leaderName = leader->GetName();
    std::string warningMsg = "WARNING: This trial involves PERMANENT CHARACTER DEATH if you fail and are not resurrected!";
    std::string confirmInstructions = "Type '/trialconfirm yes' to accept or '/trialconfirm no' to decline. You have " + std::to_string(ConfirmationTimeoutSeconds) + " seconds to respond.";

    ChatHandler(leader->GetSession()).SendSysMessage("Confirmation requests for the Trial of Finality have been sent to your group members. Waiting for responses...");

    for (const auto& memberGuid : pendingInfo.memberGuidsToConfirm) {
        Player* member = ObjectAccessor::FindPlayer(memberGuid);
        if (member && member->GetSession()) {
            ChatHandler(member->GetSession()).PSendSysMessage("Your group leader, %s, has proposed to start the Trial of Finality!", leaderName.c_str());
            ChatHandler(member->GetSession()).SendSysMessage(warningMsg);
            ChatHandler(member->GetSession()).SendSysMessage(confirmInstructions);
        }
    }
    // LogTrialDbEvent for PENDING_START or similar could be added here if desired.
    return true; // Indicates confirmation phase started successfully
}

void TrialManager::CheckPlayerLocationsAndEnforceBoundaries(uint32 groupId) {
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo) return;

    Position centerPos(ArenaTeleportX, ArenaTeleportY, ArenaTeleportZ, 0.0f);

    for (const auto& memberGuid : trialInfo->memberGuids) {
        Player* member = ObjectAccessor::FindPlayer(memberGuid);
        // Only check living players who are not GMs in debug mode
        if (member && member->GetSession() && member->IsAlive() && !(GMDebugEnable && member->GetSession()->GetSecurity() >= SEC_GAMEMASTER)) {
            bool isOutside = false;
            if (member->GetMapId() != ArenaMapID) {
                isOutside = true;
            } else if (member->GetDistance(centerPos) > ArenaRadius) {
                isOutside = true;
            }

            if (isOutside) {
                if (trialInfo->playersWarnedForLeavingArena.count(memberGuid)) {
                    // Already warned, now fail the trial.
                    sLog->outWarn("sys", "[TrialOfFinality] Player %s (Group %u) left the arena after being warned. Failing the trial.",
                        member->GetName().c_str(), groupId);
                    std::string reason = member->GetName() + " has fled the Trial of Finality, forfeiting the challenge for the group.";
                    LogTrialDbEvent(TRIAL_EVENT_PLAYER_FORFEIT_ARENA, groupId, member, trialInfo->currentWave, trialInfo->highestLevelAtStart, reason);
                    FinalizeTrialOutcome(groupId, false, reason);
                    return; // Stop checking once trial is failed
                } else {
                    // First warning.
                    trialInfo->playersWarnedForLeavingArena.insert(memberGuid);
                    ChatHandler(member->GetSession()).SendSysMessage("WARNING: You have left the trial arena! Return immediately or you will forfeit the trial for your entire group!");
                    LogTrialDbEvent(TRIAL_EVENT_PLAYER_WARNED_ARENA_LEAVE, groupId, member, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Player left arena boundary and was warned.");
                }
            }
        }
    }
}

void TrialManager::PrepareAndAnnounceWave(uint32 groupId, int waveNumber, uint32 delayMs) {
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo) {
        sLog->outError("sys", "[TrialOfFinality] PrepareAndAnnounceWave called for group %u but no ActiveTrialInfo found.", groupId);
        return;
    }

    trialInfo->currentWave = waveNumber;
    sLog->outInfo("sys", "[TrialOfFinality] Group %u preparing for wave %d.", groupId, waveNumber);
    LogTrialDbEvent(TRIAL_EVENT_WAVE_START, groupId, nullptr, waveNumber, trialInfo->highestLevelAtStart, "Announcing wave.");

    // Announcer logic will be more detailed in the announcer's AI script.
    // For now, we can find/spawn and make it yell.
    Creature* announcer = nullptr;
    if (trialInfo->announcerGuid.IsEmpty()) {
        // Spawn announcer on first wave prep
        Position announcerPos = { ArenaTeleportX + 5.0f, ArenaTeleportY, ArenaTeleportZ, ArenaTeleportO }; // Example position
        Map* trialMap = sMapMgr->FindMap(ArenaMapID, 0);
        if(trialMap) {
            announcer = trialMap->SummonCreature(AnnouncerEntry, announcerPos, TEMPSUMMON_MANUAL_DESPAWN);
            if (announcer) {
                trialInfo->announcerGuid = announcer->GetGUID();
            }
        }
    } else {
        announcer = ObjectAccessor::GetCreature(*sWorld, trialInfo->announcerGuid);
    }

    if (announcer) {
        if (auto* ai = dynamic_cast<npc_trial_announcer_ai*>(announcer->AI())) {
            ai->AnnounceWave(waveNumber);
        } else {
            // Fallback for safety
            std::string waveAnnounce = "Brave contenders, prepare yourselves! Wave " + std::to_string(waveNumber) + " approaches!";
            announcer->Yell(waveAnnounce, LANG_UNIVERSAL, nullptr);
        }
    }

    // Schedule the actual wave spawn
    sTaskScheduler->Schedule(std::chrono::milliseconds(delayMs), [this, groupId]()
    {
        SpawnActualWave(groupId);
    });
}

void TrialManager::HandleMonsterKilledInTrial(ObjectGuid monsterGuid, uint32 groupId) {
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo) {
        return;
    }

    // Erase returns the number of elements removed (0 or 1 for a set)
    if (trialInfo->activeMonsters.erase(monsterGuid)) {
        sLog->outDetail("[TrialOfFinality] Group %u killed a trial monster. %lu remaining in wave %d.",
            groupId, trialInfo->activeMonsters.size(), trialInfo->currentWave);

        if (trialInfo->activeMonsters.empty()) {
            sLog->outInfo("sys", "[TrialOfFinality] Group %u has cleared wave %d.", groupId, trialInfo->currentWave);

            // Clear any downed players from the previous wave - they are now safe
            if (!trialInfo->downedPlayerGuids.empty()) {
                 for(auto const& [guid, time] : trialInfo->downedPlayerGuids) {
                    Player* p = ObjectAccessor::FindPlayer(guid);
                    if(p && p->GetSession()) {
                        ChatHandler(p->GetSession()).SendSysMessage("The wave is over! You have survived... for now.");
                    }
                 }
                trialInfo->downedPlayerGuids.clear();
            }

            if (trialInfo->currentWave < 5) {
                // Prepare next wave
                PrepareAndAnnounceWave(groupId, trialInfo->currentWave + 1, 8000); // 8 second delay between waves
            } else {
                // All 5 waves cleared! Success!
                FinalizeTrialOutcome(groupId, true, "All 5 waves successfully cleared.");
            }
        }
    }
}

void TrialManager::SpawnActualWave(uint32 groupId) {
    ActiveTrialInfo* currentTrial = GetActiveTrialInfo(groupId);
    if (!currentTrial) {
        sLog->outError("sys", "[TrialOfFinality] SpawnActualWave called for group %u but no ActiveTrialInfo found.", groupId);
        return;
    }

    Map* trialMap = sMapMgr->FindMap(ArenaMapID, 0);
    if (!trialMap) {
        sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: Could not find trial map %u to spawn wave.", groupId, currentTrial->currentWave, ArenaMapID);
        FinalizeTrialOutcome(groupId, false, "Internal error: Trial map not found for wave spawn.");
        return;
    }

    uint32 activePlayers = 0;
    for (const auto& memberGuid : currentTrial->memberGuids) {
        if (!currentTrial->permanentlyFailedPlayerGuids.count(memberGuid)) {
            Player* player = ObjectAccessor::FindPlayer(memberGuid);
            if (player && player->IsAlive() && !currentTrial->downedPlayerGuids.count(memberGuid)) {
                activePlayers++;
            }
        }
    }
    if (activePlayers == 0 && !currentTrial->memberGuids.empty()) {
        sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: No active players left to spawn wave for. Finalizing trial.", groupId, currentTrial->currentWave);
        FinalizeTrialOutcome(groupId, false, "All players defeated or disconnected before wave " + std::to_string(currentTrial->currentWave) + " could spawn.");
        return;
    }


    const std::vector<std::vector<uint32>>* currentWaveNpcPool = nullptr;
    const CustomNpcScalingTier* customScalingTier = nullptr;
    float healthMultiplier = 1.0f;
    const std::vector<uint32>* aurasToAdd = nullptr;

    if (currentTrial->currentWave <= 2) {
        currentWaveNpcPool = &NpcPoolEasy;
        customScalingTier = &CustomScalingEasy;
    } else if (currentTrial->currentWave <= 4) {
        currentWaveNpcPool = &NpcPoolMedium;
        customScalingTier = &CustomScalingMedium;
    } else {
        currentWaveNpcPool = &NpcPoolHard;
        customScalingTier = &CustomScalingHard;
    }

    if (NpcScalingMode == "custom_scaling_rules" && customScalingTier) {
        healthMultiplier = customScalingTier->HealthMultiplier;
        aurasToAdd = &customScalingTier->AurasToAdd;
        sLog->outDetail("[TrialOfFinality] Group %u, Wave %d: Using CUSTOM scaling rules. HealthMult: %.2f, Auras: %lu",
            groupId, currentTrial->currentWave, healthMultiplier, (aurasToAdd ? aurasToAdd->size() : 0));
    }

    if (!currentWaveNpcPool || currentWaveNpcPool->empty()) {
        sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: Cannot spawn wave. NPC pool for this difficulty is empty.", groupId, currentTrial->currentWave);
        FinalizeTrialOutcome(groupId, false, "Internal error: NPC pool empty for wave " + std::to_string(currentTrial->currentWave));
        return;
    }

    if (WAVE_SPAWN_POSITIONS.empty()) {
        sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: Cannot spawn wave. No spawn positions are configured or loaded. Check Arena.SpawnPositions in the .conf file.", groupId, currentTrial->currentWave);
        FinalizeTrialOutcome(groupId, false, "Internal error: No spawn positions configured.");
        return;
    }
    uint32 numSpawnsPerWave = WAVE_SPAWN_POSITIONS.size();

    uint32 numGroupsToSpawn = std::min((uint32)numSpawnsPerWave, activePlayers + 1);
    numGroupsToSpawn = std::max(numGroupsToSpawn, 1u);

    if (numGroupsToSpawn > currentWaveNpcPool->size()) {
        sLog->outWarn("sys", "[TrialOfFinality] Group %u, Wave %d: Requested %u encounter groups, but pool only has %lu. Spawning %lu instead.",
            groupId, currentTrial->currentWave, numGroupsToSpawn, currentWaveNpcPool->size(), currentWaveNpcPool->size());
        numGroupsToSpawn = currentWaveNpcPool->size();
    }

    std::vector<std::vector<uint32>> selectedGroups = *currentWaveNpcPool;
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(selectedGroups.begin(), selectedGroups.end(), g);

    sLog->outMessage("sys", "[TrialOfFinality] Group %u, Wave %d: Spawning %u encounter groups. Highest Lvl: %u. Health Multi: %.2f",
        groupId, currentTrial->currentWave, numGroupsToSpawn, currentTrial->highestLevelAtStart, healthMultiplier);
    currentTrial->activeMonsters.clear();

    uint32 spawnPosIndex = 0;
    uint32 totalCreaturesSpawned = 0;


    for (uint32 i = 0; i < numGroupsToSpawn; ++i) {
        const std::vector<uint32>& groupOfNpcs = selectedGroups[i];
        if (spawnPosIndex + groupOfNpcs.size() > numSpawnsPerWave) {
            sLog->outWarn("sys", "[TrialOfFinality] Group %u, Wave %d: Encounter group with %lu members exceeds remaining spawn points (%u). Skipping group.", groupId, currentTrial->currentWave, groupOfNpcs.size(), numSpawnsPerWave - spawnPosIndex);
            continue;
        }

        for (uint32 creatureEntry : groupOfNpcs) {
            const Position& spawnPos = WAVE_SPAWN_POSITIONS[spawnPosIndex++];
            if (Creature* creature = trialMap->SummonCreature(creatureEntry, spawnPos, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 3600 * 1000)) {
                creature->SetLevel(currentTrial->highestLevelAtStart);

                if (healthMultiplier != 1.0f) {
                    creature->SetMaxHealth(uint32(creature->GetMaxHealth() * healthMultiplier));
                    creature->SetHealth(creature->GetMaxHealth());
                }

                if (aurasToAdd) {
                    for (uint32 auraId : *aurasToAdd) {
                        creature->AddAura(auraId, creature);
                    }
                }

                currentTrial->activeMonsters.insert(creature->GetGUID());
                totalCreaturesSpawned++;
                sLog->outDetail("[TrialOfFinality] Group %u, Wave %d: Spawned NPC %u (GUID %s) at %f,%f,%f",
                    groupId, currentTrial->currentWave, creatureEntry, creature->GetGUID().ToString().c_str(), spawnPos.GetPositionX(), spawnPos.GetPositionY(), spawnPos.GetPositionZ());
            } else {
                sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: Failed to spawn NPC %u at %f,%f,%f",
                    groupId, currentTrial->currentWave, creatureEntry, spawnPos.GetPositionX(), spawnPos.GetPositionY(), spawnPos.GetPositionZ());
            }
        }
    }

    if (currentTrial->activeMonsters.empty() && !currentWaveNpcPool->empty()) {
        sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: Failed to spawn ANY monsters. Check pool config and spawn positions. Finalizing trial.", groupId, currentTrial->currentWave);
        FinalizeTrialOutcome(groupId, false, "Internal error: Failed to spawn any NPCs for wave " + std::to_string(currentTrial->currentWave));
        return;
    }

    sLog->outMessage("sys", "[TrialOfFinality] Group %u, Wave %d: Spawned a total of %u creatures.", groupId, currentTrial->currentWave, totalCreaturesSpawned);
}

void TrialManager::HandlePlayerDownedInTrial(Player* downedPlayer) {
    if (!downedPlayer || !downedPlayer->GetGroup()) return;
    uint32 groupId = downedPlayer->GetGroup()->GetId();
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo) return;

    ObjectGuid playerGuid = downedPlayer->GetGUID();
    trialInfo->downedPlayerGuids[playerGuid] = time(nullptr);

    sLog->outInfo("sys", "[TrialOfFinality] Player %s (GUID %s, Group %u) has been downed in wave %d.",
        downedPlayer->GetName().c_str(), playerGuid.ToString().c_str(), groupId, trialInfo->currentWave);

    ChatHandler(downedPlayer->GetSession()).SendSysMessage("You have been defeated! You must be resurrected before the wave ends to avoid permanent failure!");
    LogTrialDbEvent(TRIAL_EVENT_PLAYER_DEATH_TOKEN, groupId, downedPlayer, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Player downed, awaiting resurrection or wave end.");

    // Check if this was the last player
    uint32 activePlayers = 0;
    for (const auto& memberGuid : trialInfo->memberGuids) {
        if (!trialInfo->permanentlyFailedPlayerGuids.count(memberGuid) && !trialInfo->downedPlayerGuids.count(memberGuid)) {
            Player* p = ObjectAccessor::FindPlayer(memberGuid);
            if (p && p->IsAlive()) {
                activePlayers++;
            }
        }
    }

    if (activePlayers == 0) {
        sLog->outInfo("sys", "[TrialOfFinality] All players in group %u are downed. Finalizing trial as a failure.", groupId);
        FinalizeTrialOutcome(groupId, false, "All players were defeated.");
    }
}

void TrialManager::FinalizeTrialOutcome(uint32 groupId, bool overallSuccess, const std::string& reason) {
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo) {
        sLog->outWarning("sys", "[TrialOfFinality] FinalizeTrialOutcome called for group %u but no ActiveTrialInfo found. Reason: %s. Trial might have been already cleaned up.",
            groupId, reason.c_str());
        m_activeTrials.erase(groupId);
        return;
    }

    sLog->outMessage("sys", "[TrialOfFinality] Finalizing trial for group %u. Overall Success: %s. Reason: %s.",
        groupId, (overallSuccess ? "Yes" : "No"), reason.c_str());

    if (!overallSuccess) {
        if (!trialInfo->downedPlayerGuids.empty()) {
            sLog->outDetail("[TrialOfFinality] Group %u trial failed. Processing %lu downed players for perma-death.",
                groupId, trialInfo->downedPlayerGuids.size());
            for(const auto& pair : trialInfo->downedPlayerGuids) {
                ObjectGuid playerGuid = pair.first;
                if (trialInfo->permanentlyFailedPlayerGuids.count(playerGuid)) { continue; }
                Player* downedPlayer_obj = ObjectAccessor::FindPlayer(playerGuid);

                // This line should be called for any player who failed this trial instance, for internal tracking (rewards, scaling).
                trialInfo->permanentlyFailedPlayerGuids.insert(playerGuid);

                if (downedPlayer_obj && downedPlayer_obj->GetSession()) // Player is online
                {
                    if (PermaDeathExemptGMs && downedPlayer_obj->GetSession()->GetSecurity() >= SEC_GAMEMASTER) // Check security level (adjust if using different GM levels)
                    {
                        sLog->outInfo("sys", "[TrialOfFinality] GM Player %s (GUID %s, Account %u, Group %u) is EXEMPT from perma-death DB flag due to configuration and GM status.",
                            downedPlayer_obj->GetName().c_str(), playerGuid.ToString().c_str(), downedPlayer_obj->GetSession()->GetAccountId(), groupId);
                        // GM is "down" for the trial's reward/state purposes, but no persistent DB flag.
                        // Aura cleanup (if any was applied before this new logic)
                        if (downedPlayer_obj->HasAura(AURA_ID_TRIAL_PERMADEATH)) downedPlayer_obj->RemoveAura(AURA_ID_TRIAL_PERMADEATH);
                    }
                    else
                    {
                        CharacterDatabase.ExecuteFmt("INSERT INTO character_trial_finality_status (guid, is_perma_failed, last_failed_timestamp) VALUES (%u, 1, NOW()) ON DUPLICATE KEY UPDATE is_perma_failed = 1, last_failed_timestamp = NOW()",
                            playerGuid.GetCounter());

                        std::string safe_reason = reason;
                        CharacterDatabase.EscapeString(safe_reason);
                        LogTrialDbEvent(TRIAL_EVENT_PERMADEATH_APPLIED, groupId, downedPlayer_obj, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Perma-death DB flag set: " + safe_reason);
                        sLog->outCritical("[TrialOfFinality] Player %s (GUID %s, Account %u, Group %u) PERMANENTLY FAILED (DB flag set) due to trial failure: %s (Wave %d).",
                            downedPlayer_obj->GetName().c_str(), playerGuid.ToString().c_str(), downedPlayer_obj->GetSession()->GetAccountId(), groupId, reason.c_str(), trialInfo->currentWave);
                        ChatHandler(downedPlayer_obj->GetSession()).SendSysMessage("The trial has ended in failure. Your fate is sealed.");
                        // Ensure aura is removed as DB is master
                        if (downedPlayer_obj->HasAura(AURA_ID_TRIAL_PERMADEATH)) downedPlayer_obj->RemoveAura(AURA_ID_TRIAL_PERMADEATH);
                    }
                }
                else // Player is offline
                {
                    // For offline players, apply perma-death regardless of GM status for now,
                    // as reliably checking security level without a session is complex.
                    // GMs who were offline and failed can use .trial reset.
                    CharacterDatabase.ExecuteFmt("INSERT INTO character_trial_finality_status (guid, is_perma_failed, last_failed_timestamp) VALUES (%u, 1, NOW()) ON DUPLICATE KEY UPDATE is_perma_failed = 1, last_failed_timestamp = NOW()",
                        playerGuid.GetCounter());

                    std::string safe_reason_offline = reason;
                    CharacterDatabase.EscapeString(safe_reason_offline);
                    LogTrialDbEvent(TRIAL_EVENT_PERMADEATH_APPLIED, groupId, nullptr, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Player GUID " + playerGuid.ToString() + " (offline) - Perma-death DB flag set: " + safe_reason_offline);
                    sLog->outCritical("[TrialOfFinality] Offline Player (GUID %s, Group %u) PERMANENTLY FAILED (DB flag set) due to trial failure: %s (Wave %d).", playerGuid.ToString().c_str(), groupId, reason.c_str(), trialInfo->currentWave);
                }
            }
        }
        trialInfo->downedPlayerGuids.clear();
        LogTrialDbEvent(TRIAL_EVENT_TRIAL_FAILURE, groupId, nullptr, trialInfo->currentWave, trialInfo->highestLevelAtStart, reason);
    } else { // overallSuccess == true
        LogTrialDbEvent(TRIAL_EVENT_TRIAL_SUCCESS, groupId, nullptr, trialInfo->currentWave, trialInfo->highestLevelAtStart, reason);

        if (WorldAnnounceEnable) {
            std::string playerListStr;
            std::string leaderNameStr = "Unknown Leader";
            Player* leaderPlayerObj = ObjectAccessor::FindPlayer(trialInfo->leaderGuid);
            if (leaderPlayerObj) {
                leaderNameStr = leaderPlayerObj->GetName();
            }
            int validWinners = 0;
            for (const auto& memberGuid : trialInfo->memberGuids) {
                if (!trialInfo->permanentlyFailedPlayerGuids.count(memberGuid)) {
                    Player* member = ObjectAccessor::FindPlayer(memberGuid);
                    if (member && member->GetSession()) {
                        if (!playerListStr.empty()) { playerListStr += ", "; }
                        playerListStr += member->GetName();
                        validWinners++;
                    }
                }
            }
            if (validWinners > 0 && !WorldAnnounceFormat.empty()) {
                std::string finalMessage = WorldAnnounceFormat;
                size_t pos = finalMessage.find("{group_leader}");
                if (pos != std::string::npos) { finalMessage.replace(pos, strlen("{group_leader}"), leaderNameStr); }
                pos = finalMessage.find("{player_list}");
                if (pos != std::string::npos) { finalMessage.replace(pos, strlen("{player_list}"), playerListStr); }
                sWorld->SendServerMessage(SERVER_MSG_STRING, finalMessage.c_str());
                sLog->outMessage("sys", "[TrialOfFinality] World announcement made for group %u: %s", groupId, finalMessage.c_str());
                LogTrialDbEvent(TRIAL_EVENT_WORLD_ANNOUNCEMENT_SUCCESS, groupId, leaderPlayerObj, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Winners: " + playerListStr);
            } else if (validWinners == 0) {
                sLog->outMessage("sys", "[TrialOfFinality] World announcement skipped for group %u: No valid online winners found to announce.", groupId);
            }
        }
        TriggerCityNpcCheers(groupId); // New call for 15b
    }
    CleanupTrial(groupId, overallSuccess);
}

void TrialManager::HandleTrialFailure(uint32 groupId, const std::string& reason) {
    FinalizeTrialOutcome(groupId, false, reason);
}

void TrialManager::CleanupTrial(uint32 groupId, bool success) {
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo) {
        return;
    }

    // Despawn any remaining monsters
    for (const auto& monsterGuid : trialInfo->activeMonsters) {
        if (Creature* monster = ObjectAccessor::GetCreature(*sWorld, monsterGuid)) {
            monster->DespawnOrUnsummon();
        }
    }
    trialInfo->activeMonsters.clear();

    // Despawn announcer
    if (!trialInfo->announcerGuid.IsEmpty()) {
        if (Creature* announcer = ObjectAccessor::GetCreature(*sWorld, trialInfo->announcerGuid)) {
            announcer->DespawnOrUnsummon();
        }
    }

    // Process all original members
    for (const auto& memberGuid : trialInfo->memberGuids) {
        Player* member = ObjectAccessor::FindPlayer(memberGuid);
        if (member && member->GetSession()) {
            // Remove token
            member->DestroyItemCount(TrialTokenEntry, 1, true, false);
            // Re-enable XP
            member->SetDisableXpGain(false, true);

            // Teleport out survivors (those not permanently failed)
            if (!trialInfo->permanentlyFailedPlayerGuids.count(memberGuid)) {
                 ChatHandler(member->GetSession()).SendSysMessage("The Trial of Finality has concluded. You are being teleported out.");
                 if (ExitOverrideHearthstone) {
                     member->TeleportTo(ExitMapID, ExitTeleportX, ExitTeleportY, ExitTeleportZ, ExitTeleportO);
                 } else {
                     member->TeleportTo(member->GetBindPoint());
                 }
            }
        }
    }

    // Give rewards on success
    if (success) {
        for (const auto& memberGuid : trialInfo->memberGuids) {
            // Only give rewards to survivors
            if (trialInfo->permanentlyFailedPlayerGuids.count(memberGuid)) continue;

            Player* member = ObjectAccessor::FindPlayer(memberGuid);
            if (member && member->GetSession()) {
                 // Gold Reward
                if (GoldReward > 0) {
                    member->ModifyMoney(GoldReward);
                    ChatHandler(member->GetSession()).PSendSysMessage("You have been awarded %u gold for your victory!", GoldReward / 10000);
                }
                // Title Reward
                if (TitleRewardID > 0) {
                    if (CharTitlesEntry const* titleEntry = sCharTitlesStore.LookupEntry(TitleRewardID))
                    {
                        member->SetTitle(titleEntry);
                        ChatHandler(member->GetSession()).SendSysMessage("You have been granted a new title!");
                    }
                }
            }
        }
    }

    // If it was a test trial with a temporary group, disband it
    if (trialInfo->isTestTrial) {
        if (Group* group = sGroupMgr->GetGroupById(groupId)) {
            sLog->outDetail("[TrialOfFinality] Disbanding temporary test trial group %u.", groupId);
            group->Disband();
        }
    }

    sLog->outInfo("sys", "[TrialOfFinality] Cleaned up trial for group %u.", groupId);
    // Finally, remove the trial from active map
    m_activeTrials.erase(groupId);
}

bool TrialManager::StartTestTrial(Player* gmPlayer) {
    if (!gmPlayer || !gmPlayer->GetSession() || gmPlayer->GetSession()->GetSecurity() < SEC_GAMEMASTER) {
        return false;
    }
    if (gmPlayer->GetGroup()) {
        ChatHandler(gmPlayer->GetSession()).SendSysMessage("You cannot start a test trial while in a group.");
        return false;
    }

    // Create a temporary, virtual group for the solo GM
    Group* tempGroup = new Group;
    tempGroup->Create(gmPlayer->GetGUID());
    sGroupMgr->AddGroup(tempGroup); // Register group with the manager to prevent memory leak
    gmPlayer->SetGroup(tempGroup, GRP_STATUS_DEFAULT);
    uint32 tempGroupId = tempGroup->GetId();

    sLog->outInfo("sys", "[TrialOfFinality] GM %s starting a solo test trial in temporary group %u.", gmPlayer->GetName().c_str(), tempGroupId);

    // Use a simplified initiation path
    m_activeTrials[tempGroupId] = ActiveTrialInfo(tempGroup, gmPlayer->getLevel());
    ActiveTrialInfo* newTrial = &m_activeTrials[tempGroupId];
    newTrial->isTestTrial = true; // Mark for cleanup

    gmPlayer->SetDisableXpGain(true, true);
    gmPlayer->AddItem(TrialTokenEntry, 1);
    gmPlayer->TeleportTo(ArenaMapID, ArenaTeleportX, ArenaTeleportY, ArenaTeleportZ, ArenaTeleportO);
    ChatHandler(gmPlayer->GetSession()).SendSysMessage("Test Trial has begun!");

    LogTrialDbEvent(TRIAL_EVENT_GM_COMMAND_TEST_START, tempGroupId, gmPlayer, 0, newTrial->highestLevelAtStart, "GM test trial started.");

    PrepareAndAnnounceWave(tempGroupId, 1, 5000);

    return true;
}
bool TrialManager::ValidateGroupForTrial(Player* leader, Creature* trialNpc) {
    ChatHandler handler(leader->GetSession());
    Group* group = leader->GetGroup();

    if (!group) {
        handler.SendSysMessage("You must be in a group to start the trial.");
        return false;
    }
    if (group->GetLeaderGUID() != leader->GetGUID()) {
        handler.SendSysMessage("Only the group leader can initiate the trial.");
        return false;
    }

    uint8 memberCount = 0;
    uint8 minLevel = 255, maxLevel = 0;
    std::vector<ObjectGuid> memberGuids;

    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
        Player* member = itr->GetSource();
        if (!member || !member->GetSession()) {
            continue; // Skip offline members
        }
        memberCount++;
        memberGuids.push_back(member->GetGUID());

        if (member->getLevel() > maxLevel) maxLevel = member->getLevel();
        if (member->getLevel() < minLevel) minLevel = member->getLevel();

        if (!trialNpc->IsWithinDistInMap(member, 50.0f)) {
            handler.PSendSysMessage("Your group member %s is too far away from Fateweaver Arithos.", member->GetName().c_str());
            return false;
        }

        if (member->GetSession()->IsPlayerBot()) {
            // Playerbots are allowed if the leader is a GM and the debug setting is enabled
            if (leader->GetSession()->GetSecurity() >= SEC_GAMEMASTER && GMDebugAllowPlayerbots) {
                handler.PSendSysMessage("Playerbot %s is permitted in the trial due to GM debug settings.", member->GetName().c_str());
            } else {
                handler.PSendSysMessage("Playerbots like %s are not permitted in the Trial of Finality.", member->GetName().c_str());
                return false;
            }
        }

        if (member->HasItemCount(TrialTokenEntry, 1, true)) {
            handler.PSendSysMessage("Your group member %s already possesses a Trial Token and cannot start a new trial.", member->GetName().c_str());
            return false;
        }
    }

    if (memberCount < MinGroupSize) {
        handler.PSendSysMessage("Your group is too small. You need at least %u members to attempt the trial.", MinGroupSize);
        return false;
    }
    if (memberCount > MaxGroupSize) {
        handler.PSendSysMessage("Your group is too large. You can have at most %u members to attempt the trial.", MaxGroupSize);
        return false;
    }
    if ((maxLevel - minLevel) > MaxLevelDifference) {
        handler.PSendSysMessage("The level difference in your group is too high. The maximum allowed difference is %u levels.", MaxLevelDifference);
        return false;
    }

    // Check perma-death status for all members in one query
    if (!memberGuids.empty()) {
        std::string guidString;
        for(size_t i = 0; i < memberGuids.size(); ++i) {
            guidString += std::to_string(memberGuids[i].GetCounter());
            if (i < memberGuids.size() - 1) guidString += ",";
        }

        if (!guidString.empty()) {
            QueryResult result = CharacterDatabase.QueryFmt("SELECT guid FROM character_trial_finality_status WHERE guid IN (%s) AND is_perma_failed = 1", guidString.c_str());
            if (result) {
                Field* fields = result->Fetch();
                uint32 failedGuid = fields[0].Get<uint32>();
                Player* failedPlayer = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(failedGuid));
                std::string failedPlayerName = "Unknown";
                if(failedPlayer)
                {
                    failedPlayerName = failedPlayer->GetName();
                }
                else
                {
                    // If player is not online, we have to do another query to get the name.
                    // This is for a better error message.
                    QueryResult charResult = CharacterDatabase.QueryFmt("SELECT name FROM characters WHERE guid = %u", failedGuid);
                    if(charResult)
                    {
                        failedPlayerName = (*charResult)[0].Get<std::string>();
                    }
                    else
                    {
                        failedPlayerName = "GUID " + std::to_string(failedGuid);
                    }
                }

                handler.PSendSysMessage("A member of your group, %s, has already had their fate sealed and cannot enter the trial again.", failedPlayerName.c_str());
                return false;
            }
        }
    }

    return true;
}

void TrialManager::TriggerCityNpcCheers(uint32 /*successfulGroupId*/) {
    if (!CheeringNpcsEnable || CheeringNpcCityZoneIDs.empty() || ModServerScript::s_cheeringNpcCacheByZone.empty()) {
        sLog->outDetail("[TrialOfFinality] NPC cheering skipped (disabled, no zones, or empty cache).");
        return;
    }

    sLog->outDetail("[TrialOfFinality] Attempting to trigger city NPC cheers using cached NPCs.");
    std::set<ObjectGuid> alreadyCheeringNpcs;
    int totalCheeredThisEvent = 0;

    Map::PlayerList const& players = sWorld->GetAllPlayers();
    if (players.isEmpty()) {
        sLog->outDetail("[TrialOfFinality] No players online to trigger cheers for.");
        return;
    }

    for (auto const& itr : players) {
        if (totalCheeredThisEvent >= CheeringNpcsMaxTotalWorld) {
            sLog->outDetail("[TrialOfFinality] Reached max total world cheers (%d). Stopping.", CheeringNpcsMaxTotalWorld);
            break;
        }

        Player* player = itr.GetSource();
        if (!player || !player->GetSession() || !player->IsInWorld() || !player->GetMap()) {
            continue;
        }

        uint32 currentZoneId = player->GetZoneId();
        if (CheeringNpcCityZoneIDs.count(currentZoneId)) {
            auto cacheIt = ModServerScript::s_cheeringNpcCacheByZone.find(currentZoneId);
            if (cacheIt == ModServerScript::s_cheeringNpcCacheByZone.end() || cacheIt->second.empty()) {
                // No NPCs cached for this specific zone, though the zone itself is a cheer zone.
                continue;
            }

            const std::vector<ObjectGuid>& zoneNpcGuids = cacheIt->second;
            int cheeredThisCluster = 0;

            for (const ObjectGuid& npcGuid : zoneNpcGuids) {
                if (totalCheeredThisEvent >= CheeringNpcsMaxTotalWorld) break;
                if (cheeredThisCluster >= CheeringNpcsMaxPerPlayerCluster) {
                    sLog->outDetail("[TrialOfFinality] Reached max cheers for player %s's cluster (%d).", player->GetName().c_str(), CheeringNpcsMaxPerPlayerCluster);
                    break;
                }
                if (alreadyCheeringNpcs.count(npcGuid)) {
                    continue;
                }

                Creature* creature = ObjectAccessor::GetCreatureOrPetOrVehicle(*player, npcGuid);
                if (creature && creature->IsAlive() && !creature->IsInCombat() && player->IsWithinDistInMap(creature, CheeringNpcsRadiusAroundPlayer)) {
                    // NpcFlag checks were done at caching time, so not strictly needed here
                    // but could be added if dynamic flags are a concern.
                    creature->HandleEmoteCommand(EMOTE_ONESHOT_CHEER); // First cheer

                    if (CheeringNpcsCheerIntervalMs > 0) {
                        m_pendingSecondCheers.push_back({npcGuid, time(nullptr) + (CheeringNpcsCheerIntervalMs / 1000)});
                    }

                    alreadyCheeringNpcs.insert(npcGuid);
                    cheeredThisCluster++;
                    totalCheeredThisEvent++;
                }
            }
        }
    }

    sLog->outMessage("sys", "[TrialOfFinality] Triggered %d city NPCs to cheer using cache.", totalCheeredThisEvent);
    if (totalCheeredThisEvent > 0) { // Only log DB event if someone actually cheered
        LogTrialDbEvent(TRIAL_EVENT_NPC_CHEER_TRIGGERED, 0, nullptr, 0, 0, "Total NPCs cheered (cached): " + std::to_string(totalCheeredThisEvent));
    }
}


// --- Announcer AI and Script ---
struct npc_trial_announcer_ai : public ScriptedAI
{
    npc_trial_announcer_ai(Creature* creature) : ScriptedAI(creature) {}

    void AnnounceWave(int waveNumber)
    {
        std::vector<std::string> announcements;
        switch (waveNumber)
        {
            case 1:
                announcements = {
                    "Let the trial commence! Your first challenge awaits!",
                    "The first wave approaches! Show them your might!",
                    "Prove your worth, contenders! The trial begins!"
                };
                break;
            case 2:
                announcements = {
                    "A commendable start! But can you withstand the second wave?",
                    "Do not falter! The next wave is upon you!",
                    "Impressive... but the trial has just begun."
                };
                break;
            case 3:
                announcements = {
                    "You show promise. Now, face a greater challenge!",
                    "The third wave will test your resolve!",
                    "Halfway there... or halfway to your doom?"
                };
                break;
            case 4:
                announcements = {
                    "Only the strongest may proceed! The fourth wave descends!",
                    "Your victory is within reach! Do not let it slip away!",
                    "Feel the rising intensity? The end is near!"
                };
                break;
            case 5:
                announcements = {
                    "The final wave! Your destiny is at hand!",
                    "This is the ultimate test! Conquer them and achieve glory!",
                    "Everything you have fought for comes to this! Annihilate them!"
                };
                break;
        }

        if (!announcements.empty()) {
            uint32 rand_idx = urand(0, announcements.size() - 1);
            me->Yell(announcements[rand_idx], LANG_UNIVERSAL, nullptr);
        }
    }
};

class npc_trial_announcer : public CreatureScript
{
public:
    npc_trial_announcer() : CreatureScript("npc_trial_announcer") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_trial_announcer_ai(creature);
    }
};
// --- NPC Scripts ---
enum FateweaverArithosGossipActions
{
    GOSSIP_ACTION_INFO = 1,
    GOSSIP_ACTION_START_TRIAL = 2,
    GOSSIP_ACTION_RETURN = 3
};

class npc_fateweaver_arithos : public CreatureScript
{
public:
    npc_fateweaver_arithos() : CreatureScript("npc_fateweaver_arithos") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!ModuleEnabled) {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "The Trial of Finality is currently dormant.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO + 100);
            SendGossipMenuFor(player, creature->GetGossipMenuId(), creature->GetGUID());
            return true;
        }

        ClearGossipMenuFor(player);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Tell me more about the Trial of Finality.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO);

        if (player->GetGroup() && player->GetGroup()->GetLeaderGUID() == player->GetGUID()) {
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "I am ready. Propose the Trial for my group.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_START_TRIAL);
        } else {
             AddGossipItemFor(player, GOSSIP_ICON_CHAT, "(You must be your group's leader to propose the trial)", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO + 100);
        }
        SendGossipMenuFor(player, creature->GetGossipMenuId(), creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        ClearGossipMenuFor(player);
        switch(action)
        {
            case GOSSIP_ACTION_INFO:
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "The trial is a test of mettle for groups of 1 to 5. You will face 5 waves of increasingly difficult foes. Success grants great rewards, but failure while holding a Trial Token means your character's journey ends, permanently. Only a teammate's resurrection during a wave can save you from this fate.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO + 100);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Return", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_RETURN);
                SendGossipMenuFor(player, creature->GetGossipMenuId(), creature->GetGUID());
                break;
            case GOSSIP_ACTION_START_TRIAL:
                CloseGossipMenuFor(player);
                if (TrialManager::ValidateGroupForTrial(player, creature)) {
                    if (!TrialManager::instance()->InitiateTrial(player)) {
                        // InitiateTrial sends its own messages on failure
                    }
                }
                // ValidateGroupForTrial sends its own messages on failure
                break;
            case GOSSIP_ACTION_RETURN:
                OnGossipHello(player, creature);
                break;
            default:
                CloseGossipMenuFor(player);
                break;
        }
        return true;
    }
};
// --- Player and World Event Scripts ---

class ModWorldScript : public WorldScript
{
public:
    ModWorldScript() : WorldScript("ModTrialOfFinalityWorldScript") { }

    void OnUpdate(uint32 diff) override
    {
        if (ModuleEnabled) {
            TrialManager::instance()->OnUpdate(diff);
        }
    }
};

class ModPlayerScript : public PlayerScript
{
public:
    ModPlayerScript() : PlayerScript("ModTrialOfFinalityPlayerScript") {}

    void OnCreatureKill(Player* killer, Creature* killed) override
    {
        if (!ModuleEnabled || !killer || !killed || !killer->GetGroup()) return;

        uint32 groupId = killer->GetGroup()->GetId();
        if (ActiveTrialInfo* trialInfo = TrialManager::instance()->GetActiveTrialInfo(groupId)) {
            // Check if the killed creature is part of this trial
            if (trialInfo->activeMonsters.count(killed->GetGUID())) {
                TrialManager::instance()->HandleMonsterKilledInTrial(killed->GetGUID(), groupId);
            }
        }
    }

    void OnPlayerKilledByCreature(Creature* /*killer*/, Player* killed) override
    {
        if (!ModuleEnabled || !killed || !killed->GetGroup()) return;
        if (ActiveTrialInfo* trialInfo = TrialManager::instance()->GetActiveTrialInfo(killed->GetGroup()->GetId())) {
            // Check if player is part of this trial and not already downed/failed
            if (trialInfo->memberGuids.count(killed->GetGUID()) &&
                !trialInfo->downedPlayerGuids.count(killed->GetGUID()) &&
                !trialInfo->permanentlyFailedPlayerGuids.count(killed->GetGUID()))
            {
                TrialManager::instance()->HandlePlayerDownedInTrial(killed);
            }
        }
    }

    void OnPlayerResurrect(Player* player, float /*percentHealth*/, float /*percentMana*/) override
    {
        if (!ModuleEnabled || !player || !player->GetGroup()) return;
        if (ActiveTrialInfo* trialInfo = TrialManager::instance()->GetActiveTrialInfo(player->GetGroup()->GetId())) {
            // If player was downed, mark them as no longer downed
            if (trialInfo->downedPlayerGuids.erase(player->GetGUID())) {
                sLog->outInfo("sys", "[TrialOfFinality] Player %s (GUID %s, Group %u) was resurrected during the trial and is no longer considered downed.",
                    player->GetName().c_str(), player->GetGUID().ToString().c_str(), player->GetGroup()->GetId());
                ChatHandler(player->GetSession()).SendSysMessage("You have been resurrected! Your fate is no longer sealed... for now.");
                LogTrialDbEvent(TRIAL_EVENT_PLAYER_RESURRECTED, player->GetGroup()->GetId(), player, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Player resurrected mid-wave.");
            }
        }
    }

    void OnLogin(Player* player) override {
        if (!ModuleEnabled) return;

        // Remove stray trial tokens if player logs in and is not in an active trial context
        if (player->HasItemCount(TrialTokenEntry, 1, true)) {
            if (player->GetMapId() != ArenaMapID) { // Simple check: if not in arena map
                player->DestroyItemCount(TrialTokenEntry, 1, true, false);
                sLog->outDetail("[TrialOfFinality] Player %s (GUID %u) logged in outside arena with Trial Token; token removed.",
                    player->GetName().c_str(), player->GetGUID().GetCounter());
                LogTrialDbEvent(TRIAL_EVENT_STRAY_TOKEN_REMOVED, 0, player, 0, player->getLevel(), "Logged in outside arena with token.");
            }
        }

        // Check for perma-death flag on login using the database
        if (player && player->GetSession()) {
            QueryResult result = CharacterDatabase.QueryFmt("SELECT is_perma_failed FROM character_trial_finality_status WHERE guid = %u", player->GetGUID().GetCounter());
            if (result)
            {
                Field* fields = result->Fetch();
                if (fields[0].Get<bool>()) // is_perma_failed is true
                {
                    player->GetSession()->KickPlayer("Your fate was sealed in the Trial of Finality.");
                    sLog->outWarn("sys", "[TrialOfFinality] Player %s (GUID %u, Account %u) kicked on login due to perma-death flag in DB.",
                                 player->GetName().c_str(), player->GetGUID().GetCounter(), player->GetSession()->GetAccountId());
                    if(player->HasAura(AURA_ID_TRIAL_PERMADEATH)) player->RemoveAura(AURA_ID_TRIAL_PERMADEATH); // Cleanup aura if it exists
                    return;
                }
            }
        }
        // Old aura-based check is now removed/obsolete.
    }
};

class ModServerScript : public ServerScript
{
public:
    ModServerScript() : ServerScript("ModTrialOfFinalityServerScript") {}

    static std::map<uint32, std::vector<ObjectGuid>> s_cheeringNpcCacheByZone;
    void OnConfigLoad(bool reload) override
    {
        sLog->outMessage("sys", "Loading Trial of Finality module configuration...");
        ModuleEnabled = sConfigMgr->GetOption<bool>("TrialOfFinality.Enable", false);
        if (!ModuleEnabled) { sLog->outMessage("sys", "Trial of Finality: Module disabled by configuration."); return; }
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
        ArenaRadius = sConfigMgr->GetOption<float>("TrialOfFinality.Arena.Radius", 100.0f);

        // Parse Spawn Positions
        WAVE_SPAWN_POSITIONS.clear();
        std::string spawnPosStr = sConfigMgr->GetOption<std::string>("TrialOfFinality.Arena.SpawnPositions", "");
        if (!spawnPosStr.empty()) {
            std::stringstream ssPos(spawnPosStr);
            std::string segment;
            while(std::getline(ssPos, segment, ';')) {
                std::stringstream ssCoord(segment);
                std::string coord;
                std::vector<float> coords;
                try {
                    while(std::getline(ssCoord, coord, ',')) {
                        coords.push_back(std::stof(coord));
                    }
                    if (coords.size() == 4) {
                        WAVE_SPAWN_POSITIONS.push_back({coords[0], coords[1], coords[2], coords[3]});
                    } else {
                        sLog->outError("sys", "[TrialOfFinality] Invalid coordinate segment in Arena.SpawnPositions: '%s'. It must have exactly 4 comma-separated floats (X,Y,Z,O).", segment.c_str());
                    }
                } catch (const std::exception& e) {
                    sLog->outError("sys", "[TrialOfFinality] Failed to parse coordinate segment in Arena.SpawnPositions: '%s'. Error: %s.", segment.c_str(), e.what());
                }
            }
        }
        if (WAVE_SPAWN_POSITIONS.empty()) {
             sLog->outError("sys", "[TrialOfFinality] Configuration for Arena.SpawnPositions is empty or invalid. The trial may not function correctly. Please provide at least one valid spawn position.");
        } else {
             sLog->outDetail("[TrialOfFinality] Loaded %lu spawn positions.", WAVE_SPAWN_POSITIONS.size());
        }

        ExitOverrideHearthstone = sConfigMgr->GetOption<bool>("TrialOfFinality.Exit.OverrideHearthstone", false);
        ExitMapID = sConfigMgr->GetOption<uint16>("TrialOfFinality.Exit.MapID", 0);
        ExitTeleportX = sConfigMgr->GetOption<float>("TrialOfFinality.Exit.TeleportX", 0.0f);
        ExitTeleportY = sConfigMgr->GetOption<float>("TrialOfFinality.Exit.TeleportY", 0.0f);
        ExitTeleportZ = sConfigMgr->GetOption<float>("TrialOfFinality.Exit.TeleportZ", 0.0f);
        ExitTeleportO = sConfigMgr->GetOption<float>("TrialOfFinality.Exit.TeleportO", 0.0f);
        NpcScalingMode = sConfigMgr->GetOption<std::string>("TrialOfFinality.NpcScaling.Mode", "match_highest_level");
        DisableCharacterMethod = sConfigMgr->GetOption<std::string>("TrialOfFinality.DisableCharacter.Method", "custom_flag");
        GMDebugEnable = sConfigMgr->GetOption<bool>("TrialOfFinality.GMDebug.Enable", false);
        GMDebugAllowPlayerbots = sConfigMgr->GetOption<bool>("TrialOfFinality.GMDebug.AllowPlayerbots", false);
        PermaDeathExemptGMs = sConfigMgr->GetOption<bool>("TrialOfFinality.PermaDeath.ExemptGMs", true);
        sLog->outDetail("[TrialOfFinality] GM Perma-Death Exemption: %s", PermaDeathExemptGMs ? "Enabled" : "Disabled");

        ConfirmationEnable = sConfigMgr->GetOption<bool>("TrialOfFinality.Confirmation.Enable", true);
        sLog->outDetail("[TrialOfFinality] Trial Confirmation System: %s", ConfirmationEnable ? "Enabled" : "Disabled");
        ConfirmationTimeoutSeconds = sConfigMgr->GetOption<uint32>("TrialOfFinality.Confirmation.TimeoutSeconds", 60);
        sLog->outDetail("[TrialOfFinality] Trial Confirmation Timeout: %u seconds", ConfirmationTimeoutSeconds);
        ConfirmationRequiredMode = sConfigMgr->GetOption<std::string>("TrialOfFinality.Confirmation.RequiredMode", "all");
        std::transform(ConfirmationRequiredMode.begin(), ConfirmationRequiredMode.end(), ConfirmationRequiredMode.begin(), ::tolower); // Normalize to lowercase
        if (ConfirmationRequiredMode != "all") {
            sLog->outWarn("sys", "[TrialOfFinality] Confirmation.RequiredMode is set to '%s', but only 'all' is currently supported. Defaulting to 'all'.", ConfirmationRequiredMode.c_str());
            ConfirmationRequiredMode = "all";
        }
        sLog->outDetail("[TrialOfFinality] Trial Confirmation Required Mode: %s", ConfirmationRequiredMode.c_str());

        ForfeitEnable = sConfigMgr->GetOption<bool>("TrialOfFinality.Forfeit.Enable", true);
        sLog->outDetail("[TrialOfFinality] Forfeit System: %s", ForfeitEnable ? "Enabled" : "Disabled");

        WorldAnnounceEnable = sConfigMgr->GetOption<bool>("TrialOfFinality.AnnounceWinners.World.Enable", true);
        WorldAnnounceFormat = sConfigMgr->GetOption<std::string>("TrialOfFinality.AnnounceWinners.World.MessageFormat",
            "Hark, heroes! The group led by {group_leader}, with valiant trialists {player_list}, has vanquished all foes and emerged victorious from the Trial of Finality! All hail the Conquerors!");

        CheeringNpcsEnable = sConfigMgr->GetOption<bool>("TrialOfFinality.CheeringNpcs.Enable", true);
        std::string zoneIDsStr = sConfigMgr->GetOption<std::string>("TrialOfFinality.CheeringNpcs.CityZoneIDs", "1519,1537,1637,1638,1657,3487,4080,4395,3557");
        CheeringNpcsRadiusAroundPlayer = sConfigMgr->GetOption<float>("TrialOfFinality.CheeringNpcs.RadiusAroundPlayer", 40.0f);
        CheeringNpcsMaxPerPlayerCluster = sConfigMgr->GetOption<int>("TrialOfFinality.CheeringNpcs.MaxNpcsToCheerPerPlayerCluster", 5);
        CheeringNpcsMaxTotalWorld = sConfigMgr->GetOption<int>("TrialOfFinality.CheeringNpcs.MaxTotalNpcsToCheerWorld", 50);
        CheeringNpcsTargetNpcFlags = sConfigMgr->GetOption<uint32>("TrialOfFinality.CheeringNpcs.TargetNpcFlags", UNIT_NPC_FLAG_NONE);
        CheeringNpcsExcludeNpcFlags = sConfigMgr->GetOption<uint32>("TrialOfFinality.CheeringNpcs.ExcludeNpcFlags", UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_FLIGHTMASTER | UNIT_NPC_FLAG_REPAIRER | UNIT_NPC_FLAG_AUCTIONEER | UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_TABARDDESIGNER | UNIT_NPC_FLAG_STABLEMASTER | UNIT_NPC_FLAG_GUILDMASTER | UNIT_NPC_FLAG_BATTLEMASTER | UNIT_NPC_FLAG_INNKEEPER | UNIT_NPC_FLAG_SPIRITHEALER | UNIT_NPC_FLAG_SPIRITGUIDE | UNIT_NPC_FLAG_PETITIONER);
        CheeringNpcsCheerIntervalMs = sConfigMgr->GetOption<uint32>("TrialOfFinality.CheeringNpcs.CheerIntervalMs", 2000);

        CheeringNpcCityZoneIDs.clear();
        std::stringstream ss(zoneIDsStr);
        std::string item;
        while (getline(ss, item, ',')) {
            if (!item.empty()) {
                uint32 zoneId = std::stoul(item);
                if (zoneId > 0) CheeringNpcCityZoneIDs.insert(zoneId);
            }
        }
        sLog->outDetail("[TrialOfFinality] Loaded %lu City Zone IDs for NPC cheering.", CheeringNpcCityZoneIDs.size());

        // NPC Caching Logic
        s_cheeringNpcCacheByZone.clear();
        if (CheeringNpcsEnable && !CheeringNpcCityZoneIDs.empty())
        {
            sLog->outMessage("sys", "[TrialOfFinality] Caching cheering NPCs...");
            std::string zoneIdString;
            for (uint32 zoneId : CheeringNpcCityZoneIDs)
            {
                if (!zoneIdString.empty())
                    zoneIdString += ",";
                zoneIdString += std::to_string(zoneId);
            }

            std::ostringstream query;
            query << "SELECT guid, zoneId FROM creature WHERE zoneId IN (" << zoneIdString << ") AND playercreateinfo_guid = 0";

            if (CheeringNpcsTargetNpcFlags != UNIT_NPC_FLAG_NONE)
            {
                query << " AND (npcflag & " << CheeringNpcsTargetNpcFlags << ") != 0";
            }

            if (CheeringNpcsExcludeNpcFlags != 0)
            {
                query << " AND (npcflag & " << CheeringNpcsExcludeNpcFlags << ") = 0";
            }

            QueryResult result = WorldDatabase.Query(query.str().c_str());
            if (result)
            {
                uint32 count = 0;
                do
                {
                    Field* fields = result->Fetch();
                    uint32 guidLow = fields[0].Get<uint32>();
                    uint32 zoneId = fields[1].Get<uint32>();
                    s_cheeringNpcCacheByZone[zoneId].push_back(ObjectGuid(HighGuid::Creature, 0, guidLow));
                    count++;
                } while (result->NextRow());
                sLog->outMessage("sys", "[TrialOfFinality] Cached %u cheering NPCs in %lu zones.", count, s_cheeringNpcCacheByZone.size());
                for (const auto& pair : s_cheeringNpcCacheByZone)
                {
                    sLog->outDetail("[TrialOfFinality] Zone %u: Cached %lu NPCs.", pair.first, pair.second.size());
                }
            }
            else
            {
                sLog->outMessage("sys", "[TrialOfFinality] No cheering NPCs found to cache or database error.");
            }
        }
        else if (!CheeringNpcsEnable)
        {
            sLog->outMessage("sys", "[TrialOfFinality] Cheering NPCs disabled, cache not populated.");
        }
        else if (CheeringNpcCityZoneIDs.empty())
        {
            sLog->outMessage("sys", "[TrialOfFinality] No City Zone IDs configured for cheering NPCs, cache not populated.");
        }

        // Helper lambda for parsing aura ID strings
        auto parseAuraIdString = [](const std::string& auraStr, const std::string& tierName) -> std::vector<uint32> {
            std::vector<uint32> auras;
            if (auraStr.empty()) {
                return auras;
            }

            std::stringstream ss(auraStr);
            std::string item;
            while (getline(ss, item, ',')) {
                // Trim whitespace
                size_t first = item.find_first_not_of(" \t\n\r\f\v");
                if (std::string::npos == first) continue;
                size_t last = item.find_last_not_of(" \t\n\r\f\v");
                item = item.substr(first, (last - first + 1));

                if (item.empty()) continue;

                try {
                    unsigned long id_ul = std::stoul(item);
                    if (id_ul == 0 || id_ul > UINT32_MAX) {
                        sLog->outError("sys", "[TrialOfFinality] Invalid Aura ID '%s' (out of range or zero) in Custom Scaling for tier '%s'. Skipping.", item.c_str(), tierName.c_str());
                        continue;
                    }
                    if (!sSpellMgr->GetSpellInfo(static_cast<uint32>(id_ul))) {
                        sLog->outError("sys", "[TrialOfFinality] Aura ID %lu in Custom Scaling for tier '%s' does not exist. Skipping.", id_ul, tierName.c_str());
                        continue;
                    }
                    auras.push_back(static_cast<uint32>(id_ul));
                } catch (const std::exception& e) {
                    sLog->outError("sys", "[TrialOfFinality] Invalid Aura ID '%s' in Custom Scaling for tier '%s'. Skipping. Error: %s", item.c_str(), tierName.c_str(), e.what());
                }
            }
            sLog->outDetail("[TrialOfFinality] Loaded %lu auras for custom scaling tier '%s'.", auras.size(), tierName.c_str());
            return auras;
        };

        // Helper lambda for parsing NPC pool strings with encounter groups
        auto parseNpcPoolString = [](const std::string& poolStr, const std::string& poolName) -> std::vector<std::vector<uint32>> {
            std::vector<std::vector<uint32>> pool;
            if (poolStr.empty()) {
                sLog->outWarn("sys", "[TrialOfFinality] NPC Pool '%s' is empty or not found in configuration.", poolName.c_str());
                return pool;
            }

            std::string currentNumber;
            std::vector<uint32> currentGroup;
            bool inGroup = false;
            int entryCount = 0;
            int invalidCount = 0;

            auto processNumber = [&](bool isEndOfGroup) {
                if (currentNumber.empty()) {
                    if (isEndOfGroup) { // e.g. (1,)
                        sLog->outError("sys", "[TrialOfFinality] Malformed group in NPC Pool '%s' (e.g., empty entry or trailing comma).", poolName.c_str());
                        invalidCount++;
                    }
                    return;
                }

                try {
                    uint32 id = std::stoul(currentNumber);
                    if (id == 0) {
                         sLog->outError("sys", "[TrialOfFinality] Creature ID 0 is invalid in NPC Pool '%s'.", poolName.c_str());
                         invalidCount++;
                         currentNumber.clear();
                         return;
                    }
                    if (!sObjectMgr->GetCreatureTemplate(id)) {
                        sLog->outError("sys", "[TrialOfFinality] Creature ID %u in NPC Pool '%s' does not exist. Skipping.", id, poolName.c_str());
                        invalidCount++;
                        currentNumber.clear();
                        return;
                    }

                    if (inGroup) {
                        currentGroup.push_back(id);
                    } else {
                        pool.push_back({id});
                    }
                    entryCount++;
                } catch (const std::exception& e) {
                    sLog->outError("sys", "[TrialOfFinality] Invalid Creature ID format '%s' in NPC Pool '%s'. Error: %s", currentNumber.c_str(), poolName.c_str(), e.what());
                    invalidCount++;
                }
                currentNumber.clear();
            };

            for (char c : poolStr) {
                if (std::isspace(c)) continue;

                if (std::isdigit(c)) {
                    currentNumber += c;
                } else if (c == '(') {
                    if (inGroup) {
                        sLog->outError("sys", "[TrialOfFinality] Nested parentheses are not allowed in NPC Pool '%s'. Aborting parse.", poolName.c_str());
                        return {};
                    }
                    processNumber(false); // Process any pending single number before the group
                    inGroup = true;
                    currentGroup.clear();
                } else if (c == ')') {
                    if (!inGroup) {
                        sLog->outError("sys", "[TrialOfFinality] Mismatched closing parenthesis in NPC Pool '%s'. Aborting parse.", poolName.c_str());
                        return {};
                    }
                    processNumber(true); // Process the last number inside the parenthesis
                    if (!currentGroup.empty()) {
                        pool.push_back(currentGroup);
                    } else if (invalidCount > 0) {
                        sLog->outWarn("sys", "[TrialOfFinality] An encounter group in NPC Pool '%s' was invalid and has been skipped.", poolName.c_str());
                    }
                    inGroup = false;
                } else if (c == ',') {
                    processNumber(inGroup); // Process the number before the comma
                } else {
                    sLog->outError("sys", "[TrialOfFinality] Invalid character '%c' in NPC Pool '%s'. Aborting parse.", c, poolName.c_str());
                    return {};
                }
            }

            if (inGroup) {
                sLog->outError("sys", "[TrialOfFinality] Unclosed parenthesis at end of NPC Pool '%s'. Aborting parse.", poolName.c_str());
                return {};
            }
            processNumber(false); // Process any trailing number

            sLog->outDetail("[TrialOfFinality] Loaded %lu encounter groups with a total of %d valid NPC entries for pool '%s' (%d invalid entries skipped).", pool.size(), entryCount, poolName.c_str(), invalidCount);
            if (pool.empty() && !poolStr.empty()) {
                 sLog->outWarn("sys", "[TrialOfFinality] NPC Pool '%s' was configured as '%s' but resulted in an empty pool after parsing. Check formatting.", poolName.c_str(), poolStr.c_str());
            }
            return pool;
        };

        // Load NPC Pools from configuration
        NpcPoolEasy.clear();
        NpcPoolMedium.clear();
        NpcPoolHard.clear();

        // Default strings here are fallbacks if .conf key is missing, actual defaults user sees are in .conf file.
        std::string easyPoolStr = sConfigMgr->GetOption<std::string>("TrialOfFinality.NpcPools.Easy", "70001,70002,70003,70004,70005");
        std::string mediumPoolStr = sConfigMgr->GetOption<std::string>("TrialOfFinality.NpcPools.Medium", "70011,70012,70013,70014,70015");
        std::string hardPoolStr = sConfigMgr->GetOption<std::string>("TrialOfFinality.NpcPools.Hard", "70021,70022,70023,70024,70025");

        NpcPoolEasy = parseNpcPoolString(easyPoolStr, "Easy");
        NpcPoolMedium = parseNpcPoolString(mediumPoolStr, "Medium");
        NpcPoolHard = parseNpcPoolString(hardPoolStr, "Hard");

        // Load Custom Scaling Rules
        sLog->outDetail("[TrialOfFinality] Loading Custom NPC Scaling Rules...");
        CustomScalingEasy.HealthMultiplier = sConfigMgr->GetOption<float>("TrialOfFinality.NpcScaling.Custom.Easy.HealthMultiplier", 1.0f);
        CustomScalingEasy.AurasToAdd = parseAuraIdString(sConfigMgr->GetOption<std::string>("TrialOfFinality.NpcScaling.Custom.Easy.AurasToAdd", ""), "Easy");

        CustomScalingMedium.HealthMultiplier = sConfigMgr->GetOption<float>("TrialOfFinality.NpcScaling.Custom.Medium.HealthMultiplier", 1.2f);
        CustomScalingMedium.AurasToAdd = parseAuraIdString(sConfigMgr->GetOption<std::string>("TrialOfFinality.NpcScaling.Custom.Medium.AurasToAdd", ""), "Medium");

        CustomScalingHard.HealthMultiplier = sConfigMgr->GetOption<float>("TrialOfFinality.NpcScaling.Custom.Hard.HealthMultiplier", 1.5f);
        CustomScalingHard.AurasToAdd = parseAuraIdString(sConfigMgr->GetOption<std::string>("TrialOfFinality.NpcScaling.Custom.Hard.AurasToAdd", ""), "Hard");

        if (!FateweaverArithosEntry || !TrialTokenEntry || !AnnouncerEntry || !TitleRewardID) {
            sLog->outError("sys", "Trial of Finality: Critical EntryID (NPC, Item, Title) not configured. Disabling module functionality.");
            ModuleEnabled = false; return;
        }
        sLog->outMessage("sys", "Trial of Finality: Configuration loaded. Module enabled.");
        if (reload) { sLog->outMessage("sys", "Trial of Finality: Configuration reloaded. Consider restarting for full effect if scripts were already registered."); }
    }
};

namespace ModTrialOfFinality {
    std::map<uint32, std::vector<ObjectGuid>> ModServerScript::s_cheeringNpcCacheByZone;
} // end namespace ModTrialOfFinality

// --- GM Command Scripts ---
class trial_commandscript : public CommandScript
{
public:
    trial_commandscript() : CommandScript("trial_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> trialCommandTable = {
            { "reset", SEC_GAMEMASTER, true, &ChatCommand_trial_reset, "" },
            { "test",  SEC_GAMEMASTER, true, &ChatCommand_trial_test,  "" }
        };
        static std::vector<ChatCommand> commandTable = {
            { "trial", SEC_GAMEMASTER, true, nullptr, "", trialCommandTable }
        };
        return commandTable;
    }

    static bool ChatCommand_trial_reset(ChatHandler* handler, const char* args)
    {
        if (!ModuleEnabled) { handler->SendSysMessage("Trial of Finality module is disabled."); return false; }
        if (!*args) { handler->SendSysMessage("Usage: .trial reset <CharacterName>"); return false; }

        Player* targetPlayer = handler->getSelectedPlayer();
        ObjectGuid playerGuid = ObjectGuid::Empty;
        std::string charNameStr = strtok((char*)args, " ");
        std::string charName = charNameStr;

        if (targetPlayer && Acore::CaseInsensitiveCompare(targetPlayer->GetName(), charName)) {
            playerGuid = targetPlayer->GetGUID();
        } else {
            targetPlayer = nullptr; // Not selected or name mismatch, try to find by name
            playerGuid = sObjectAccessor->GetPlayerGUIDByName(charName);
        }

        if (playerGuid.IsEmpty()) {
            handler->PSendSysMessage("Player %s not found.", charName.c_str());
            return false;
        }

        // Re-fetch player object if it was null or mismatched, now that we have a GUID
        if (!targetPlayer && playerGuid) {
            targetPlayer = ObjectAccessor::FindPlayer(playerGuid);
        }

        // 1. Clear Perma-death DB flag
        CharacterDatabase.ExecuteFmt("UPDATE character_trial_finality_status SET is_perma_failed = 0, last_failed_timestamp = NULL WHERE guid = %u", playerGuid.GetCounter());
        handler->PSendSysMessage("Cleared Trial of Finality perma-death DB flag for character %s (GUID %u).", charName.c_str(), playerGuid.GetCounter());

        std::string log_detail = "GM cleared perma-death DB flag for " + charName;
        Player* loggerPlayer = targetPlayer ? targetPlayer : (handler->GetPlayer() ? handler->GetPlayer() : nullptr);
        uint32 loggerGroupId = (loggerPlayer && loggerPlayer->GetGroup()) ? loggerPlayer->GetGroup()->GetId() : 0;
        uint8 loggerLevel = loggerPlayer ? loggerPlayer->getLevel() : 0;

        LogTrialDbEvent(TRIAL_EVENT_GM_COMMAND_RESET, loggerGroupId, loggerPlayer, 0, loggerLevel, log_detail);
        sLog->outInfo("sys", "[TrialOfFinality] GM %s cleared perma-death DB flag for %s (GUID %u).",
                     (handler->GetPlayer() ? handler->GetPlayer()->GetName().c_str() : "UnknownGM"), charName.c_str(), playerGuid.GetCounter());
        if (targetPlayer && targetPlayer->GetSession()) {
             ChatHandler(targetPlayer->GetSession()).SendSysMessage("Your Trial of Finality perma-death status (DB) has been reset by a GM.");
        }

        // 2. Remove Trial Token if player has it (only if online)
        if (targetPlayer) {
            if (targetPlayer->HasItemCount(TrialTokenEntry, 1, true)) {
                targetPlayer->DestroyItemCount(TrialTokenEntry, 1, true, false);
                if (targetPlayer->GetSession()) ChatHandler(targetPlayer->GetSession()).SendSysMessage("Your Trial Token has been removed by a GM.");
                handler->PSendSysMessage("Removed Trial Token from %s.", charName.c_str());
            }
        } else {
            handler->PSendSysMessage("Note: If %s is offline, their Trial Token (if any) was not removed by this command.", charName.c_str());
        }

        // 3. Remove Perma-Death Aura if player has it (cleanup, only if online)
        if (targetPlayer) {
            if (targetPlayer->HasAura(AURA_ID_TRIAL_PERMADEATH)) {
                targetPlayer->RemoveAura(AURA_ID_TRIAL_PERMADEATH);
                if (targetPlayer->GetSession()) ChatHandler(targetPlayer->GetSession()).SendSysMessage("Your Trial of Finality perma-death aura (if present) has been removed by a GM.");
                handler->PSendSysMessage("Removed Trial of Finality perma-death aura from %s as a cleanup.", charName.c_str());
            }
        } else {
            handler->PSendSysMessage("Note: If %s is offline, their perma-death aura (if any) was not removed by this command.", charName.c_str());
        }

        handler->SendSysMessage("Trial of Finality status for " + charName + " has been reset.");
        return true;
    }

    static bool ChatCommand_trial_test(ChatHandler* handler, const char* /*args*/)
    {
        if (!ModuleEnabled) { handler->SendSysMessage("Trial of Finality module is disabled."); return false; }
        Player* gmPlayer = handler->GetPlayer();
        if (!gmPlayer) { handler->SendSysMessage("You must be a player to use this command."); return false; }

        if (TrialManager::instance()->StartTestTrial(gmPlayer)) {
            handler->SendSysMessage("Test trial initiated successfully.");
            LogTrialDbEvent(TRIAL_EVENT_GM_COMMAND_TEST_START, (gmPlayer->GetGroup() ? gmPlayer->GetGroup()->GetId() : 0), gmPlayer, 0, gmPlayer->getLevel(), "GM started test trial.");
             sLog->outInfo("sys", "[TrialOfFinality] GM %s (GUID %u) started a test trial.", gmPlayer->GetName().c_str(), gmPlayer->GetGUID().GetCounter());
        } else {
            handler->SendSysMessage("Failed to initiate test trial. Check server logs for details.");
        }
        return true;
    }
};

// --- Player Command Scripts ---
class trial_player_commandscript : public CommandScript
{
public:
    trial_player_commandscript() : CommandScript("trial_player_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> commandTable;

        if (commandTable.empty()) {
            commandTable.push_back({ "trialconfirm", SEC_PLAYER, true, &HandleTrialConfirmCommand, "Confirms or denies participation in the Trial of Finality. Usage: /trialconfirm <yes|no>" });
            commandTable.push_back({ "tc",           SEC_PLAYER, true, &HandleTrialConfirmCommand, "Alias for /trialconfirm. Usage: /tc <yes|no>" });
            if (ForfeitEnable) {
                commandTable.push_back({ "trialforfeit", SEC_PLAYER, true, &HandleTrialForfeitCommand, "Votes to forfeit the current Trial of Finality." });
                commandTable.push_back({ "tf",           SEC_PLAYER, true, &HandleTrialForfeitCommand, "Alias for /trialforfeit." });
            }
        }
        return commandTable;
    }

    static bool HandleTrialForfeitCommand(ChatHandler* handler, const char* /*args*/)
    {
        Player* player = handler->GetPlayer();
        if (!player) {
            handler->SendSysMessage("This command can only be used by a player.");
            return false;
        }

        if (!ModuleEnabled) {
            ChatHandler(player->GetSession()).SendSysMessage("The Trial of Finality module is currently disabled.");
            return false;
        }

        sLog->outDetail("[TrialOfFinality] Player %s (GUID %u) is attempting to use /trialforfeit command.", player->GetName().c_str(), player->GetGUID().GetCounter());
        TrialManager::instance()->HandleTrialForfeit(player);
        return true;
    }

    static bool HandleTrialConfirmCommand(ChatHandler* handler, const char* args)
    {
        Player* player = handler->GetPlayer();
        if (!player) {
            // This check is mostly a safeguard; SEC_PLAYER should ensure a player context.
            handler->SendSysMessage("This command can only be used by a player.");
            return false;
        }

        if (!ModuleEnabled) {
            ChatHandler(player->GetSession()).SendSysMessage("The Trial of Finality module is currently disabled.");
            return false;
        }

        if (!player->GetGroup()) {
            ChatHandler(player->GetSession()).SendSysMessage("You must be in a group to use this command.");
            return false;
        }

        std::string argStr = args ? args : "";
        std::transform(argStr.begin(), argStr.end(), argStr.begin(), ::tolower); // Case-insensitive

        bool accepted;
        if (argStr == "yes") {
            accepted = true;
        } else if (argStr == "no") {
            accepted = false;
        } else {
            ChatHandler(player->GetSession()).SendSysMessage("Usage: /trialconfirm <yes|no>");
            return false;
        }

        TrialManager::instance()->HandleTrialConfirmation(player, accepted);
        return true;
    }
};

} // namespace ModTrialOfFinality

void Addmod_trial_of_finality_Scripts() {
    new ModTrialOfFinality::npc_trial_announcer();
    new ModTrialOfFinality::npc_fateweaver_arithos();
    new ModTrialOfFinality::ModPlayerScript();
    new ModTrialOfFinality::ModServerScript();
    new ModTrialOfFinality::ModWorldScript();
    new ModTrialOfFinality::trial_commandscript(); // GM commands
    new ModTrialOfFinality::trial_player_commandscript(); // Player commands
}
extern "C" void Addmod_trial_of_finality() { Addmod_trial_of_finality_Scripts(); }
