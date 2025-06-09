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

#include <time.h>
#include <set>
#include <sstream>
#include <map>
#include <cmath>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>

#include "ObjectAccessor.h"
#include "Player.h"
#include "DBCStores.h"
#include "CharTitles.h"
#include "DatabaseEnv.h"


// Module specific namespace
namespace ModTrialOfFinality
{

// Forward declarations

// --- Creature ID Pools for Waves ---
const std::vector<uint32> POOL_WAVE_CREATURES_EASY = {
    70001, 70002, 70003, 70004, 70005, 70006, 70007, 70008, 70009, 70010
};
const std::vector<uint32> POOL_WAVE_CREATURES_MEDIUM = {
    70011, 70012, 70013, 70014, 70015, 70016, 70017, 70018, 70019, 70020
};
const std::vector<uint32> POOL_WAVE_CREATURES_HARD = {
    70021, 70022, 70023, 70024, 70025, 70026, 70027, 70028, 70029, 70030
};

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
    TRIAL_EVENT_PLAYER_FORFEIT_ARENA
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

// --- Wave Creature Data --- (Already defined above, this is just a section marker)
const Position WAVE_SPAWN_POSITIONS[5] = {
    {-13230.0f, 180.0f, 30.5f, 1.57f}, {-13218.0f, 180.0f, 30.5f, 1.57f},
    {-13235.0f, 196.0f, 30.5f, 3.14f}, {-13213.0f, 196.0f, 30.5f, 3.14f},
    {-13224.0f, 210.0f, 30.5f, 4.71f}
};
const uint32 NUM_SPAWNS_PER_WAVE = 5;

// --- Configuration Variables --- (Already defined above)
// const uint32 AURA_ID_TRIAL_PERMADEATH = 40000; ... etc.

// --- Main Trial Logic ---
struct ActiveTrialInfo { /* ... existing ... */ };
class TrialManager { /* ... existing ... */ };

bool TrialManager::InitiateTrial(Player* leader) { /* ... existing ... */ }
void TrialManager::CheckPlayerLocationsAndEnforceBoundaries(uint32 groupId) { /* ... existing ... */ }
void TrialManager::PrepareAndAnnounceWave(uint32 groupId, int waveNumber, uint32 delayMs) { /* ... existing ... */ }
void TrialManager::HandleMonsterKilledInTrial(ObjectGuid monsterGuid, uint32 groupId) { /* ... existing ... */ }

void TrialManager::SpawnActualWave(uint32 groupId) {
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo || trialInfo->currentWave == 0 || trialInfo->currentWave > 5) {
        sLog->outError("sys", "[TrialOfFinality] SpawnActualWave: Invalid trial state for group %u, wave %d.",
                       groupId, trialInfo ? trialInfo->currentWave : -1);
        return;
    }

    Map* trialMap = sMapMgr->FindMap(ArenaMapID, 0);
    if (!trialMap) {
        sLog->outError("sys", "[TrialOfFinality] SpawnActualWave: Could not find map %u for group %u.", ArenaMapID, groupId);
        CleanupTrial(groupId, false);
        return;
    }

    trialInfo->activeMonsters.clear();

    uint32 initialPlayerCount = trialInfo->memberGuids.size();
    if (initialPlayerCount == 0) {
        sLog->outError("sys", "[TrialOfFinality] SpawnActualWave: Initial player count is zero for group %u. Aborting.", groupId);
        CleanupTrial(groupId, false);
        return;
    }
    uint32 permanentlyFailedCount = trialInfo->permanentlyFailedPlayerGuids.size();
    uint32 currentActivePlayers = initialPlayerCount > permanentlyFailedCount ? initialPlayerCount - permanentlyFailedCount : 0;

    if (currentActivePlayers == 0) {
        sLog->outMessage("sys", "[TrialOfFinality] SpawnActualWave: No active players remaining for group %u before spawning wave %d.",
            groupId, trialInfo->currentWave);
        FinalizeTrialOutcome(groupId, false, "No active players to spawn wave for.");
        return;
    }

    int numNpcsToSpawn = std::max(1, static_cast<int>(round(static_cast<float>(NUM_SPAWNS_PER_WAVE) * (static_cast<float>(currentActivePlayers) / static_cast<float>(initialPlayerCount)))));
    numNpcsToSpawn = std::min(numNpcsToSpawn, static_cast<int>(NUM_SPAWNS_PER_WAVE));
    numNpcsToSpawn = std::min(numNpcsToSpawn, static_cast<int>(sizeof(WAVE_SPAWN_POSITIONS) / sizeof(Position)));

    const std::vector<uint32>* selectedPool = nullptr;
    float healthMultiplier = 1.0f;

    switch (trialInfo->currentWave) {
        case 1: case 2: selectedPool = &POOL_WAVE_CREATURES_EASY; break;
        case 3: case 4: selectedPool = &POOL_WAVE_CREATURES_MEDIUM; healthMultiplier = 1.2f; break;
        case 5: selectedPool = &POOL_WAVE_CREATURES_HARD; healthMultiplier = 1.5f; break;
        default:
            sLog->outError("sys", "[TrialOfFinality] SpawnActualWave: Invalid currentWave %d for group %u.", trialInfo->currentWave, groupId);
            CleanupTrial(groupId, false);
            return;
    }

    if (!selectedPool || selectedPool->empty()) {
        sLog->outError("sys", "[TrialOfFinality] SpawnActualWave: Creature pool for wave %d is empty or not selected for group %u.", trialInfo->currentWave, groupId);
        CleanupTrial(groupId, false);
        return;
    }

    if (numNpcsToSpawn > static_cast<int>(selectedPool->size())) {
        sLog->outWarning("sys", "[TrialOfFinality] SpawnActualWave: Requested %d NPCs but pool for wave %d only has %lu. Spawning all from pool.",
            numNpcsToSpawn, trialInfo->currentWave, selectedPool->size());
        numNpcsToSpawn = selectedPool->size();
    }

    std::vector<uint32> waveSpecificEntries = *selectedPool;
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::shuffle(waveSpecificEntries.begin(), waveSpecificEntries.end(), std::default_random_engine(seed));

    sLog->outMessage("sys", "[TrialOfFinality] Group %u, Wave %d: Initial players: %u, Active: %u. Spawning %d distinct creatures (HP Multi: %.2fx) at level %u.",
        groupId, trialInfo->currentWave, initialPlayerCount, currentActivePlayers, numNpcsToSpawn, healthMultiplier, trialInfo->highestLevelAtStart);

    std::string spawnedEntriesList_str = "Entries: ";

    for (int i = 0; i < numNpcsToSpawn; ++i) {
        uint32 creatureEntry = waveSpecificEntries[i];
        spawnedEntriesList_str += std::to_string(creatureEntry) + " ";

        Creature* spawnedCreature = trialMap->SummonCreature(creatureEntry, WAVE_SPAWN_POSITIONS[i], TEMPSUMMON_TIMED_DESPAWN, 3600 * 1000);
        if (spawnedCreature) {
            spawnedCreature->SetLevel(trialInfo->highestLevelAtStart);
            uint32 newMaxHealth = uint32(float(spawnedCreature->GetMaxHealth()) * healthMultiplier);
            spawnedCreature->SetMaxHealth(newMaxHealth);
            spawnedCreature->SetFullHealth();
            trialInfo->activeMonsters.insert(spawnedCreature->GetGUID());
        } else {
            sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: Failed to summon creature %u (from distinct list) at position index %d.",
                           groupId, trialInfo->currentWave, creatureEntry, i);
        }
    }
    sLog->outDetail("[TrialOfFinality] Group %u, Wave %d: %s", groupId, trialInfo->currentWave, spawnedEntriesList_str.c_str());
    LogTrialDbEvent(TRIAL_EVENT_WAVE_START, groupId, nullptr, trialInfo->currentWave,
                    trialInfo->highestLevelAtStart,
                    spawnedEntriesList_str + ", Count: " + std::to_string(numNpcsToSpawn) +
                    ", HPx: " + std::to_string(healthMultiplier).substr(0, std::to_string(healthMultiplier).find(".") + 3));


    if (trialInfo->activeMonsters.empty() && numNpcsToSpawn > 0) {
        sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: Failed to spawn ANY creatures despite attempting %d. Aborting trial.",
            groupId, trialInfo->currentWave, numNpcsToSpawn);
        Player* leader = ObjectAccessor::FindPlayer(trialInfo->leaderGuid);
        if (leader && leader->GetSession()) {
            ChatHandler(leader->GetSession()).SendSysMessage("A critical error occurred spawning creatures for your wave. The trial has been aborted.");
        }
        FinalizeTrialOutcome(groupId, false, "Failed to spawn any wave creatures.");
    } else {
        sLog->outDetail("[TrialOfFinality] Group %u, Wave %d: Successfully attempted to spawn %d distinct creatures, %lu are active.",
            groupId, trialInfo->currentWave, numNpcsToSpawn, trialInfo->activeMonsters.size());
    }
}

void TrialManager::HandlePlayerDownedInTrial(Player* downedPlayer) { /* ... existing ... */ }
void TrialManager::FinalizeTrialOutcome(uint32 groupId, bool overallSuccess, const std::string& reason) { /* ... existing ... */ }
void TrialManager::HandleTrialFailure(uint32 groupId, const std::string& reason) { /* ... existing ... */ }
void TrialManager::CleanupTrial(uint32 groupId, bool success) { /* ... existing ... */ }
bool TrialManager::StartTestTrial(Player* gmPlayer) { /* ... existing ... */ }
bool TrialManager::ValidateGroupForTrial(Player* leader, Creature* trialNpc) { /* ... existing ... */ }

// --- Announcer AI and Script ---
class npc_trial_announcer_ai : public ScriptedAI { /* ... */ };
class npc_trial_announcer : public CreatureScript { /* ... */ };

// --- NPC Scripts ---
enum FateweaverArithosGossipActions { /* ... */ };
class npc_fateweaver_arithos : public CreatureScript { /* ... */ };

// --- Player and World Event Scripts ---
class ModPlayerScript : public PlayerScript { /* ... existing ... */ };
class ModServerScript : public ServerScript { /* ... */ };

// --- GM Command Scripts ---
class trial_commandscript : public CommandScript { /* ... existing ... */ };

} // namespace ModTrialOfFinality

void Addmod_trial_of_finality_Scripts() { /* ... existing ... */ }
extern "C" void Addmod_trial_of_finality() { /* ... */ }
