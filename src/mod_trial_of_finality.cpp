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
#include <string> // For std::stoul in OnConfigLoad for 15b

#include "ObjectAccessor.h"
#include "Player.h"
#include "DBCStores.h"
#include "CharTitles.h"
#include "DatabaseEnv.h"
#include "ObjectGuid.h" // Added for NPC caching
#include "GridNotifiers.h" // For CreatureListSearcher in 15b


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
    TRIAL_EVENT_NPC_CHEER_TRIGGERED
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
std::vector<uint32> NpcPoolEasy;
std::vector<uint32> NpcPoolMedium;
std::vector<uint32> NpcPoolHard;

// --- Wave Spawn Positions ---
const Position WAVE_SPAWN_POSITIONS[5] = {
    {-13230.0f, 180.0f, 30.5f, 1.57f}, {-13218.0f, 180.0f, 30.5f, 1.57f},
    {-13235.0f, 196.0f, 30.5f, 3.14f}, {-13213.0f, 196.0f, 30.5f, 3.14f},
    {-13224.0f, 210.0f, 30.5f, 4.71f}
};
const uint32 NUM_SPAWNS_PER_WAVE = 5;

// --- Configuration Variables ---
const uint32 AURA_ID_TRIAL_PERMADEATH = 40000;
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

// --- Main Trial Logic ---
struct ActiveTrialInfo { /* ... as of Step 11d ... */
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
        if (group) {
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
                if (Player* member = itr->GetSource()) { memberGuids.insert(member->GetGUID()); }
            }
        }
    }
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
    void TriggerCityNpcCheers(uint32 successfulGroupId); // New for 15b

    ActiveTrialInfo* GetActiveTrialInfo(uint32 groupId) {
        auto it = m_activeTrials.find(groupId);
        if (it != m_activeTrials.end()) { return &it->second; }
        return nullptr;
    }
    static bool ValidateGroupForTrial(Player* leader, Creature* trialNpc);
private:
    TrialManager() {}
    ~TrialManager() {}
    TrialManager(const TrialManager&) = delete;
    TrialManager& operator=(const TrialManager&) = delete;
    std::map<uint32, ActiveTrialInfo> m_activeTrials;
};

bool TrialManager::InitiateTrial(Player* leader) { /* ... as of Step 12 ... */
    if (!leader || !leader->GetSession()) return false;
    Group* group = leader->GetGroup();
    if (!group) return false;
    if (m_activeTrials.count(group->GetId())) {
        sLog->outError("sys", "[TrialOfFinality] Attempt to start trial for group %u that is already active.", group->GetId());
        ChatHandler(leader->GetSession()).SendSysMessage("Your group seems to be already in a trial or an error occurred.");
        return false;
    }
    uint8 highestLevel = 0;
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
        if (Player* member = itr->GetSource()) {
            if (member->getLevel() > highestLevel) highestLevel = member->getLevel();
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
    ActiveTrialInfo& currentTrial = emplaceResult.first->second;
    LogTrialDbEvent(TRIAL_EVENT_START, group->GetId(), leader, 0, currentTrial.highestLevelAtStart, "Members: " + std::to_string(currentTrial.memberGuids.size()));
    sLog->outMessage("sys", "[TrialOfFinality] Starting Trial for group ID %u, leader %s (GUID %s), %lu members. Highest level: %u. Arena: Map %u (%f,%f,%f,%f)",
        group->GetId(), leader->GetName().c_str(), leader->GetGUID().ToString().c_str(), currentTrial.memberGuids.size(), highestLevel,
        ArenaMapID, ArenaTeleportX, ArenaTeleportY, ArenaTeleportZ, ArenaTeleportO);
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
        Player* member = itr->GetSource();
        if (!member || !member->GetSession()) continue;
        if (Item* trialToken = member->AddItem(TrialTokenEntry, 1)) { member->SendNewItem(trialToken, 1, true, false); }
        else { sLog->outError("sys", "[TrialOfFinality] Failed to grant Trial Token (ID %u) to player %s.", TrialTokenEntry, member->GetName().c_str()); }
        member->SetDisableXpGain(true, true);
        member->TeleportTo(ArenaMapID, ArenaTeleportX, ArenaTeleportY, ArenaTeleportZ, ArenaTeleportO);
    }
    group->BroadcastGroupWillBeTeleported();
    group->SendUpdate();
    Position announcerPos = {-13200.0f, 200.0f, 31.0f, 0.0f};
    Map* trialMap = sMapMgr->FindMap(ArenaMapID, 0);
    if (!trialMap) {
        sLog->outError("sys", "[TrialOfFinality] Could not find trial map %u to spawn announcer for group %u.", ArenaMapID, group->GetId());
        ChatHandler(leader->GetSession()).SendSysMessage("Error preparing trial arena. Please contact a GM.");
        m_activeTrials.erase(group->GetId());
        return false;
    }
    if (Creature* announcer = trialMap->SummonCreature(AnnouncerEntry, announcerPos, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 3600 * 1000)) {
        currentTrial.announcerGuid = announcer->GetGUID();
        if (npc_trial_announcer_ai* ai = CAST_AI(npc_trial_announcer_ai*, announcer->AI())) { ai->SetTrialGroupId(group->GetId()); }
        sLog->outDetail("[TrialOfFinality] Announcer (Entry %u, GUID %s) spawned for group %u.", AnnouncerEntry, announcer->GetGUID().ToString().c_str(), group->GetId());
    } else { sLog->outError("sys", "[TrialOfFinality] Failed to spawn Trial Announcer (Entry %u) for group %u.", AnnouncerEntry, group->GetId()); }
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
        if (Player* member = itr->GetSource()) {
            if(member->GetSession()) ChatHandler(member->GetSession()).SendSysMessage("The Trial of Finality has begun! Prepare yourselves!");
        }
    }
    uint32 initialWaveDelayMs = 5000;
    PrepareAndAnnounceWave(group->GetId(), 1, initialWaveDelayMs);
    return true;
}

void TrialManager::CheckPlayerLocationsAndEnforceBoundaries(uint32 groupId) { /* ... (as of Step 11d) ... */ }

void TrialManager::PrepareAndAnnounceWave(uint32 groupId, int waveNumber, uint32 delayMs) { /* ... (as of Step 11d, with CheckPlayerLocations call) ... */ }

void TrialManager::HandleMonsterKilledInTrial(ObjectGuid monsterGuid, uint32 groupId) { /* ... (as of Step 8c) ... */ }

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
            if (player && player->IsAlive() && !currentTrial->downedPlayerGuids.count(memberGuid)) { // Also check not downed for this wave
                activePlayers++;
            }
        }
    }
    if (activePlayers == 0 && !currentTrial->memberGuids.empty()) { // Should be caught by other checks, but as a safeguard
        sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: No active players left to spawn wave for. Finalizing trial.", groupId, currentTrial->currentWave);
        FinalizeTrialOutcome(groupId, false, "All players defeated or disconnected before wave " + std::to_string(currentTrial->currentWave) + " could spawn.");
        return;
    }

    uint32 numCreaturesToSpawn = std::min(NUM_SPAWNS_PER_WAVE, activePlayers + 1); // Example: 1 player -> 2 mobs, up to max NUM_SPAWNS_PER_WAVE
    numCreaturesToSpawn = std::max(numCreaturesToSpawn, 1u); // Ensure at least 1 creature spawns if activePlayers > 0

    const std::vector<uint32>* currentWaveNpcPool = nullptr;
    float healthMultiplier = 1.0f;

    if (currentTrial->currentWave <= 2) { // Waves 1-2: Easy
        currentWaveNpcPool = &ModTrialOfFinality::NpcPoolEasy;
    } else if (currentTrial->currentWave <= 4) { // Waves 3-4: Medium
        currentWaveNpcPool = &ModTrialOfFinality::NpcPoolMedium;
        healthMultiplier = 1.2f; // 20% health boost
    } else { // Wave 5: Hard
        currentWaveNpcPool = &ModTrialOfFinality::NpcPoolHard;
        healthMultiplier = 1.5f; // 50% health boost
    }

    if (!currentWaveNpcPool || currentWaveNpcPool->empty()) {
        sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: Cannot spawn wave. NPC pool for this difficulty is empty or not configured correctly.",
            groupId, currentTrial->currentWave);
        FinalizeTrialOutcome(groupId, false, "Internal error: NPC pool empty for wave " + std::to_string(currentTrial->currentWave));
        return;
    }

    if (numCreaturesToSpawn > currentWaveNpcPool->size()) {
        sLog->outWarn("sys", "[TrialOfFinality] Group %u, Wave %d: Requested %u distinct NPCs, but pool only has %lu. Spawning %lu instead.",
            groupId, currentTrial->currentWave, numCreaturesToSpawn, currentWaveNpcPool->size(), currentWaveNpcPool->size());
        numCreaturesToSpawn = currentWaveNpcPool->size();
    }
    if (numCreaturesToSpawn == 0 && !currentWaveNpcPool->empty()){ // If pool is not empty, but we decided to spawn 0 (e.g. activePlayers was 0 but somehow we reached here)
        sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: Calculated numCreaturesToSpawn is 0, though pool is not empty. This should not happen if activePlayers > 0.", groupId, currentTrial->currentWave);
        FinalizeTrialOutcome(groupId, false, "Internal error: Calculated 0 creatures to spawn for wave " + std::to_string(currentTrial->currentWave));
        return;
    }


    std::vector<uint32> selectedNpcEntries = *currentWaveNpcPool; // Copy the pool
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(selectedNpcEntries.begin(), selectedNpcEntries.end(), g); // Shuffle to get random distinct NPCs

    sLog->outMessage("sys", "[TrialOfFinality] Group %u, Wave %d: Spawning %u creatures. Highest Lvl: %u. Health Multi: %.2f",
        groupId, currentTrial->currentWave, numCreaturesToSpawn, currentTrial->highestLevelAtStart, healthMultiplier);
    currentTrial->activeMonsters.clear();

    for (uint32 i = 0; i < numCreaturesToSpawn; ++i) {
        uint32 creatureEntry = selectedNpcEntries[i];
        const Position& spawnPos = WAVE_SPAWN_POSITIONS[i % NUM_SPAWNS_PER_WAVE]; // Use modulo in case numCreaturesToSpawn > NUM_SPAWN_POSITIONS (though capped by pool size)

        if (Creature* creature = trialMap->SummonCreature(creatureEntry, spawnPos, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 3600 * 1000)) {
            creature->SetLevel(currentTrial->highestLevelAtStart);
            if (healthMultiplier > 1.0f) {
                creature->SetMaxHealth(uint32(creature->GetMaxHealth() * healthMultiplier));
                creature->SetHealth(creature->GetMaxHealth());
            }
            // Make creatures aggressive or link them if needed by AI or scripts
            // creature->SetReactState(REACT_AGGRESSIVE); // Example
            currentTrial->activeMonsters.insert(creature->GetGUID());
            sLog->outDetail("[TrialOfFinality] Group %u, Wave %d: Spawned NPC %u (GUID %s) at %f,%f,%f",
                groupId, currentTrial->currentWave, creatureEntry, creature->GetGUID().ToString().c_str(), spawnPos.GetPositionX(), spawnPos.GetPositionY(), spawnPos.GetPositionZ());
        } else {
            sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: Failed to spawn NPC %u at %f,%f,%f",
                groupId, currentTrial->currentWave, creatureEntry, spawnPos.GetPositionX(), spawnPos.GetPositionY(), spawnPos.GetPositionZ());
        }
    }
     if (currentTrial->activeMonsters.empty() && numCreaturesToSpawn > 0) {
        sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: Failed to spawn ANY monsters despite requesting %u. Finalizing trial.", groupId, currentTrial->currentWave, numCreaturesToSpawn);
        FinalizeTrialOutcome(groupId, false, "Internal error: Failed to spawn any NPCs for wave " + std::to_string(currentTrial->currentWave));
        return;
    }
}

void TrialManager::HandlePlayerDownedInTrial(Player* downedPlayer) { /* ... (as of Step 8a) ... */ }

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

void TrialManager::HandleTrialFailure(uint32 groupId, const std::string& reason) { /* ... existing ... */ }
void TrialManager::CleanupTrial(uint32 groupId, bool success) { /* ... existing ... */ }
bool TrialManager::StartTestTrial(Player* gmPlayer) { /* ... existing ... */ }
bool TrialManager::ValidateGroupForTrial(Player* leader, Creature* trialNpc) { /* ... existing ... */ }

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
                        sLog->outDetail("[TrialOfFinality] NPC %s (GUID %u) would perform a second cheer after %u ms.",
                                        creature->GetName().c_str(), creature->GetGUID().GetCounter(), CheeringNpcsCheerIntervalMs);
                    }

                    alreadyCheeringNpcs.insert(npcGuid);
                    cheeredThisCluster++;
                    totalCheeredThisEvent++;
                    // sLog->outDebug("[TrialOfFinality] NPC %s (GUID %u) cheered for player %s.", creature->GetName().c_str(), npcGuid.GetCounter(), player->GetName().c_str());
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
class npc_trial_announcer_ai : public ScriptedAI { /* ... */ };
class npc_trial_announcer : public CreatureScript { /* ... */ };
// --- NPC Scripts ---
enum FateweaverArithosGossipActions { /* ... */ };
class npc_fateweaver_arithos : public CreatureScript { /* ... */ };
// --- Player and World Event Scripts ---
class ModPlayerScript : public PlayerScript
{
public:
    ModPlayerScript() : PlayerScript("ModTrialOfFinalityPlayerScript") {}

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

    static std::map<uint32, std::vector<ObjectGuid>> s_cheeringNpcCacheByZone; // Keep this one
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
        NpcScalingMode = sConfigMgr->GetOption<std::string>("TrialOfFinality.NpcScaling.Mode", "match_highest_level");
        DisableCharacterMethod = sConfigMgr->GetOption<std::string>("TrialOfFinality.DisableCharacter.Method", "custom_flag");
        GMDebugEnable = sConfigMgr->GetOption<bool>("TrialOfFinality.GMDebug.Enable", false);
        PermaDeathExemptGMs = sConfigMgr->GetOption<bool>("TrialOfFinality.PermaDeath.ExemptGMs", true);
        sLog->outDetail("[TrialOfFinality] GM Perma-Death Exemption: %s", PermaDeathExemptGMs ? "Enabled" : "Disabled");

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
            else
            {
                query << " AND npcflag = 0";
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

        // Helper lambda for parsing NPC pool strings
        auto parseNpcPoolString = [](const std::string& poolStr, const std::string& poolName) -> std::vector<uint32> {
            std::vector<uint32> pool;
            if (poolStr.empty()) {
                sLog->outWarn("sys", "[TrialOfFinality] NPC Pool '%s' is empty or not found in configuration. Using empty pool.", poolName.c_str());
                return pool;
            }

            std::stringstream ss(poolStr);
            std::string item;
            int validCount = 0;
            int invalidCount = 0;
            while (getline(ss, item, ',')) {
                // Trim whitespace from item
                size_t first = item.find_first_not_of(" \t\n\r\f\v");
                if (std::string::npos == first) continue; // item is all whitespace
                size_t last = item.find_last_not_of(" \t\n\r\f\v");
                item = item.substr(first, (last - first + 1));

                if (item.empty()) continue;

                try {
                    unsigned long id_ul = std::stoul(item);
                    if (id_ul == 0 || id_ul > UINT32_MAX) {
                        sLog->outError("sys", "[TrialOfFinality] Invalid Creature ID '%s' (out of range or zero) in NPC Pool '%s'. Skipping.", item.c_str(), poolName.c_str());
                        invalidCount++;
                        continue;
                    }
                    CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(static_cast<uint32>(id_ul));
                    if (!cInfo) {
                        sLog->outError("sys", "[TrialOfFinality] Creature ID %lu in NPC Pool '%s' does not exist in creature_template. Skipping.", id_ul, poolName.c_str());
                        invalidCount++;
                        continue;
                    }
                    pool.push_back(static_cast<uint32>(id_ul));
                    validCount++;
                } catch (const std::invalid_argument& ia) {
                    sLog->outError("sys", "[TrialOfFinality] Invalid Creature ID '%s' (not a number) in NPC Pool '%s'. Skipping. Error: %s", item.c_str(), poolName.c_str(), ia.what());
                    invalidCount++;
                } catch (const std::out_of_range& oor) {
                    sLog->outError("sys", "[TrialOfFinality] Creature ID '%s' (out of range for stoul) in NPC Pool '%s'. Skipping. Error: %s", item.c_str(), poolName.c_str(), oor.what());
                    invalidCount++;
                }
            }
            sLog->outDetail("[TrialOfFinality] Loaded %d valid NPCs for pool '%s' (encountered %d invalid or non-existent entries).", validCount, poolName.c_str(), invalidCount);
            if (pool.empty() && validCount == 0 && invalidCount == 0 && !poolStr.empty()){
                 sLog->outWarn("sys", "[TrialOfFinality] NPC Pool '%s' was configured as '%s' but resulted in an empty pool after parsing (check for formatting issues like trailing commas).", poolName.c_str(), poolStr.c_str());
            } else if (pool.empty() && (validCount > 0 || invalidCount > 0)) {
                 sLog->outError("sys", "[TrialOfFinality] NPC Pool '%s' is empty after processing entries. Trial may not function correctly if this pool is used.", poolName.c_str());
            }
            return pool;
        };

        // Load NPC Pools from configuration
        NpcPoolEasy.clear();
        NpcPoolMedium.clear();
        NpcPoolHard.clear();

        std::string easyPoolStr = sConfigMgr->GetOption<std::string>("TrialOfFinality.NpcPools.Easy", "70001,70002,70003,70004,70005"); // Default reduced for brevity
        std::string mediumPoolStr = sConfigMgr->GetOption<std::string>("TrialOfFinality.NpcPools.Medium", "70011,70012,70013,70014,70015");
        std::string hardPoolStr = sConfigMgr->GetOption<std::string>("TrialOfFinality.NpcPools.Hard", "70021,70022,70023,70024,70025");

        NpcPoolEasy = parseNpcPoolString(easyPoolStr, "Easy");
        NpcPoolMedium = parseNpcPoolString(mediumPoolStr, "Medium");
        NpcPoolHard = parseNpcPoolString(hardPoolStr, "Hard");

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

}

void Addmod_trial_of_finality_Scripts() { /* ... existing ... */ }
extern "C" void Addmod_trial_of_finality() { /* ... */ }
