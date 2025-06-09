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
#include <sstream> // For std::ostringstream
#include <map> // For std::map
#include <cmath> // For round

#include "ObjectAccessor.h" // For FindPlayer
#include "Player.h" // For Player class (if not already via other headers)
#include "DBCStores.h" // For sCharTitlesStore and CharTitlesEntry
#include "CharTitles.h" // For CharTitlesEntry if not fully defined in DBCStores.h for some cores
#include "DatabaseEnv.h" // For CharacterDatabase, LoginDatabase, WorldDatabase etc.


// Module specific namespace
namespace ModTrialOfFinality
{

// Forward declarations
// class TrialManager; // No longer needed here due to definition order

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
    TRIAL_EVENT_STRAY_TOKEN_REMOVED
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
    if (!details.empty()) {
        CharacterDatabase.EscapeString(escaped_details);
    }

    std::string escaped_player_name = playerName_s;
    if (!playerName_s.empty()) {
        CharacterDatabase.EscapeString(escaped_player_name);
    }

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


// --- Wave Creature Data ---
const uint32 CREATURE_ENTRY_WAVE_EASY = 70001;
const uint32 CREATURE_ENTRY_WAVE_MEDIUM = 70002;
const uint32 CREATURE_ENTRY_WAVE_HARD = 70003;
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

// --- Main Trial Logic ---
struct ActiveTrialInfo
{
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

    ActiveTrialInfo() = default;
    ActiveTrialInfo(Group* group, uint8 highestLvl) :
        groupId(group->GetId()), leaderGuid(group->GetLeaderGUID()),
        highestLevelAtStart(highestLvl), startTime(time(nullptr)), currentWave(0)
    {
        memberGuids.clear();
        downedPlayerGuids.clear();
        permanentlyFailedPlayerGuids.clear();
        activeMonsters.clear();
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

bool TrialManager::InitiateTrial(Player* leader) {
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

void TrialManager::PrepareAndAnnounceWave(uint32 groupId, int waveNumber, uint32 delayMs) {
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo) { sLog->outError("sys", "[TrialOfFinality] PrepareAndAnnounceWave: Could not find active trial for group %u", groupId); return; }
    trialInfo->currentWave = waveNumber;
    sLog->outMessage("sys", "[TrialOfFinality] Group %u preparing wave %d.", groupId, trialInfo->currentWave);
    Map* trialMap = sMapMgr->FindMap(ArenaMapID, 0);
    if (!trialMap) { sLog->outError("sys", "[TrialOfFinality] PrepareAndAnnounceWave: Could not find map %u for group %u.", ArenaMapID, groupId); CleanupTrial(groupId, false); return; }
    if (Creature* announcer = trialMap->GetCreature(trialInfo->announcerGuid)) {
         if (npc_trial_announcer_ai* ai = CAST_AI(npc_trial_announcer_ai*, announcer->AI())) {
            ai->AnnounceAndSpawnWave(trialInfo->currentWave, delayMs);
            sLog->outDetail("[TrialOfFinality] Group %u: Announcer told to announce wave %d with delay %u ms.", groupId, trialInfo->currentWave, delayMs);
        } else { sLog->outError("sys", "[TrialOfFinality] Group %u: Could not get Announcer AI for wave %d.", groupId, trialInfo->currentWave); CleanupTrial(groupId, false); }
    } else { sLog->outError("sys", "[TrialOfFinality] Group %u: Could not find Announcer (GUID %s) for wave %d.", groupId, trialInfo->announcerGuid.ToString().c_str(), trialInfo->currentWave); CleanupTrial(groupId, false); }
}

void TrialManager::HandleMonsterKilledInTrial(ObjectGuid monsterGuid, uint32 groupId) {
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo) { sLog->outWarning("sys", "[TrialOfFinality] HandleMonsterKilledInTrial: No active trial for group %u (monster %s).", groupId, monsterGuid.ToString().c_str()); return; }
    if (!trialInfo->activeMonsters.count(monsterGuid)) { sLog->outWarning("sys", "[TrialOfFinality] HandleMonsterKilledInTrial: Monster %s (Group %u) not in active set for wave %d.", monsterGuid.ToString().c_str(), groupId, trialInfo->currentWave); return; }
    trialInfo->activeMonsters.erase(monsterGuid);
    sLog->outMessage("sys", "[TrialOfFinality] Group %u, Wave %d: Monster %s removed. %lu monsters remaining.", groupId, trialInfo->currentWave, monsterGuid.ToString().c_str(), trialInfo->activeMonsters.size());
    if (trialInfo->activeMonsters.empty()) {
        int justClearedWave = trialInfo->currentWave;
        sLog->outMessage("sys", "[TrialOfFinality] Group %u: All monsters in Wave %d cleared!", groupId, justClearedWave);
        if (!trialInfo->downedPlayerGuids.empty()) {
            std::string fallenPlayerNamesThisWave;
            for (auto const& [playerGuid, deathTime] : trialInfo->downedPlayerGuids) {
                if (trialInfo->permanentlyFailedPlayerGuids.count(playerGuid)) { continue; }
                Player* failedPlayer = ObjectAccessor::FindPlayer(playerGuid);
                if (failedPlayer && failedPlayer->GetSession()) {
                    if (!failedPlayer->HasAura(AURA_ID_TRIAL_PERMADEATH)) { failedPlayer->AddAura(AURA_ID_TRIAL_PERMADEATH, failedPlayer); }
                    trialInfo->permanentlyFailedPlayerGuids.insert(playerGuid);
                    LogTrialDbEvent(TRIAL_EVENT_PERMADEATH_APPLIED, groupId, failedPlayer, justClearedWave, trialInfo->highestLevelAtStart, "Not resurrected by end of wave.");
                    sLog->outCritical("[TrialOfFinality] Player %s (GUID %s, Group %u) PERMANENTLY FAILED Trial at end of Wave %d (unresurrected).", failedPlayer->GetName().c_str(), playerGuid.ToString().c_str(), groupId, justClearedWave);
                    ChatHandler(failedPlayer->GetSession()).SendSysMessage("You were not resurrected in time. Your fate is sealed.");
                    if (!fallenPlayerNamesThisWave.empty()) fallenPlayerNamesThisWave += ", ";
                    fallenPlayerNamesThisWave += failedPlayer->GetName();
                } else {
                     trialInfo->permanentlyFailedPlayerGuids.insert(playerGuid);
                     LogTrialDbEvent(TRIAL_EVENT_PERMADEATH_APPLIED, groupId, nullptr, justClearedWave, trialInfo->highestLevelAtStart, "Player GUID " + playerGuid.ToString() + " (offline) - Not resurrected by end of wave. Marked perm-failed.");
                     sLog->outCritical("[TrialOfFinality] Offline Player (GUID %s, Group %u) PERMANENTLY FAILED Trial at end of Wave %d (marked via GUID, unresurrected).", playerGuid.ToString().c_str(), groupId, justClearedWave);
                }
            }
            if (!fallenPlayerNamesThisWave.empty()) {
                std::string groupMessage = "The following trialists have permanently fallen this wave: " + fallenPlayerNamesThisWave + ". Their journey ends here.";
                 for(ObjectGuid memberGuid : trialInfo->memberGuids) {
                    if (!trialInfo->permanentlyFailedPlayerGuids.count(memberGuid) && !trialInfo->downedPlayerGuids.count(memberGuid)) {
                        if(Player* member = ObjectAccessor::FindPlayer(memberGuid)) {
                            if (member->GetSession()) ChatHandler(member->GetSession()).SendSysMessage(groupMessage.c_str());
                        }
                    }
                }
            }
        }
        trialInfo->downedPlayerGuids.clear();
        bool activePlayersRemain = false;
        for (const auto& memberGuid : trialInfo->memberGuids) {
            if (!trialInfo->permanentlyFailedPlayerGuids.count(memberGuid)) { activePlayersRemain = true; break; }
        }
        if (!activePlayersRemain) {
            sLog->outMessage("sys", "[TrialOfFinality] Group %u: No active players remaining after wave %d processing. Trial ends.", groupId, justClearedWave);
            FinalizeTrialOutcome(groupId, false, "No active players remaining to continue.");
            return;
        }
        uint32 maxWaves = 5;
        if (justClearedWave >= maxWaves) {
            sLog->outMessage("sys", "[TrialOfFinality] Group %u has cleared all %u waves with active members remaining!", groupId, maxWaves);
            // Reward distribution happens here for successful completion of all waves
            Map* trialMap = sMapMgr->FindMap(ArenaMapID, 0);
            if (trialMap) {
                for(ObjectGuid memberGuid : trialInfo->memberGuids) {
                    Player* member = ObjectAccessor::FindPlayer(memberGuid);
                    if (member && member->GetSession() && member->GetMap() == trialMap && !trialInfo->permanentlyFailedPlayerGuids.count(memberGuid)) {
                        ChatHandler(member->GetSession()).SendSysMessage("Congratulations! You have conquered the Trial of Finality!");
                        if (GoldReward > 0) { member->ModifyMoney(GoldReward); member->GetSession()->SendNotification("You have received %u gold!", GoldReward); sLog->outDetail("[TrialOfFinality] Player %s awarded %u gold.", member->GetName().c_str(), GoldReward); }
                        if (TitleRewardID > 0) {
                            CharTitlesEntry const* titleEntry = sCharTitlesStore.LookupEntry(TitleRewardID);
                            if (titleEntry) { member->SetKnownTitle(titleEntry, true); sLog->outDetail("[TrialOfFinality] Player %s awarded title ID %u ('%s').", member->GetName().c_str(), TitleRewardID, titleEntry->Name_Lang_enUS.c_str()); }
                            else { sLog->outError("sys", "[TrialOfFinality] Failed to grant title: Title ID %u not found in CharTitles.dbc.", TitleRewardID); }
                        }
                    }
                }
            }
            FinalizeTrialOutcome(groupId, true, "All waves cleared successfully.");
        } else {
            int nextWaveToPrepare = justClearedWave + 1;
            sLog->outMessage("sys", "[TrialOfFinality] Group %u: Proceeding to prepare Wave %d.", groupId, nextWaveToPrepare);
            uint32 delayBetweenWavesMs = 10000;
            PrepareAndAnnounceWave(groupId, nextWaveToPrepare, delayBetweenWavesMs);
        }
    }
}

void TrialManager::SpawnActualWave(uint32 groupId) {
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo || trialInfo->currentWave == 0 || trialInfo->currentWave > 5) {
        sLog->outError("sys", "[TrialOfFinality] SpawnActualWave: Invalid trial state for group %u, wave %d.", groupId, trialInfo ? trialInfo->currentWave : -1); return;
    }
    Map* trialMap = sMapMgr->FindMap(ArenaMapID, 0);
    if (!trialMap) { sLog->outError("sys", "[TrialOfFinality] SpawnActualWave: Could not find map %u for group %u.", ArenaMapID, groupId); CleanupTrial(groupId, false); return; }
    trialInfo->activeMonsters.clear();
    uint32 initialPlayerCount = trialInfo->memberGuids.size();
    if (initialPlayerCount == 0) { sLog->outError("sys", "[TrialOfFinality] SpawnActualWave: Initial player count is zero for group %u. Aborting.", groupId); CleanupTrial(groupId, false); return; }
    uint32 permanentlyFailedCount = trialInfo->permanentlyFailedPlayerGuids.size();
    uint32 currentActivePlayers = initialPlayerCount > permanentlyFailedCount ? initialPlayerCount - permanentlyFailedCount : 0;
    if (currentActivePlayers == 0) {
        sLog->outMessage("sys", "[TrialOfFinality] SpawnActualWave: No active players remaining for group %u before spawning wave %d. Trial should have ended.", groupId, trialInfo->currentWave);
        FinalizeTrialOutcome(groupId, false, "No active players to spawn wave for.");
        return;
    }
    int numNpcsToSpawn = std::max(1, static_cast<int>(round(static_cast<float>(NUM_SPAWNS_PER_WAVE) * (static_cast<float>(currentActivePlayers) / static_cast<float>(initialPlayerCount)))));
    numNpcsToSpawn = std::min({numNpcsToSpawn, static_cast<int>(NUM_SPAWNS_PER_WAVE), static_cast<int>(sizeof(WAVE_SPAWN_POSITIONS) / sizeof(Position))});
    uint32 creatureEntry; float healthMultiplier = 1.0f;
    switch (trialInfo->currentWave) {
        case 1: case 2: creatureEntry = CREATURE_ENTRY_WAVE_EASY; break;
        case 3: case 4: creatureEntry = CREATURE_ENTRY_WAVE_MEDIUM; healthMultiplier = 1.2f; break;
        case 5: creatureEntry = CREATURE_ENTRY_WAVE_HARD; healthMultiplier = 1.5f; break;
        default: sLog->outError("sys", "[TrialOfFinality] SpawnActualWave: Invalid currentWave %d for group %u.", trialInfo->currentWave, groupId); CleanupTrial(groupId, false); return;
    }
    sLog->outMessage("sys", "[TrialOfFinality] Group %u, Wave %d: Initial players: %u, Active: %u. Spawning %d creatures (Entry: %u, HP Multi: %.2fx) at level %u.",
        groupId, trialInfo->currentWave, initialPlayerCount, currentActivePlayers, numNpcsToSpawn, creatureEntry, healthMultiplier, trialInfo->highestLevelAtStart);
    LogTrialDbEvent(TRIAL_EVENT_WAVE_START, groupId, nullptr, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Entry: " + std::to_string(creatureEntry) + ", Count: " + std::to_string(numNpcsToSpawn) + ", HPx: " + std::to_string(healthMultiplier).substr(0, std::to_string(healthMultiplier).find(".") + 3));
    for (int i = 0; i < numNpcsToSpawn; ++i) {
        Creature* spawnedCreature = trialMap->SummonCreature(creatureEntry, WAVE_SPAWN_POSITIONS[i], TEMPSUMMON_TIMED_DESPAWN, 3600 * 1000);
        if (spawnedCreature) {
            spawnedCreature->SetLevel(trialInfo->highestLevelAtStart);
            uint32 newMaxHealth = uint32(float(spawnedCreature->GetMaxHealth()) * healthMultiplier);
            spawnedCreature->SetMaxHealth(newMaxHealth); spawnedCreature->SetFullHealth();
            trialInfo->activeMonsters.insert(spawnedCreature->GetGUID());
        } else { sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: Failed to summon creature %u at position index %d.", groupId, trialInfo->currentWave, creatureEntry, i); }
    }
    if (trialInfo->activeMonsters.empty() && numNpcsToSpawn > 0) {
        sLog->outError("sys", "[TrialOfFinality] Group %u, Wave %d: Failed to spawn ANY creatures despite attempting %d. Aborting trial.", groupId, trialInfo->currentWave, numNpcsToSpawn);
        Player* leader = ObjectAccessor::FindPlayer(trialInfo->leaderGuid);
        if (leader && leader->GetSession()) { ChatHandler(leader->GetSession()).SendSysMessage("A critical error occurred spawning creatures for your wave. The trial has been aborted."); }
        FinalizeTrialOutcome(groupId, false, "Failed to spawn any wave creatures.");
    } else { sLog->outDetail("[TrialOfFinality] Group %u, Wave %d: Successfully attempted to spawn %d creatures, %lu are active.", groupId, trialInfo->currentWave, numNpcsToSpawn, trialInfo->activeMonsters.size()); }
}

void TrialManager::HandlePlayerDownedInTrial(Player* downedPlayer) {
    if (!downedPlayer || !downedPlayer->GetSession()) return;
    Group* group = downedPlayer->GetGroup();
    if (!group) return;
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(group->GetId());
    if (!trialInfo) { sLog->outWarning("sys", "[TrialOfFinality] HandlePlayerDownedInTrial: Player %s died but no active trial for group %u.", downedPlayer->GetName().c_str(), group->GetId()); return; }
    if (trialInfo->downedPlayerGuids.count(downedPlayer->GetGUID()) || trialInfo->permanentlyFailedPlayerGuids.count(downedPlayer->GetGUID())) { sLog->outDetail("[TrialOfFinality] HandlePlayerDownedInTrial: Player %s already processed as downed or permfailed.", downedPlayer->GetName().c_str()); return; }
    trialInfo->downedPlayerGuids[downedPlayer->GetGUID()] = time(nullptr);
    LogTrialDbEvent(TRIAL_EVENT_PLAYER_DEATH_TOKEN, group->GetId(), downedPlayer, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Player downed, awaiting resurrection or wave end.");
    uint32 potentialCombatants = 0;
    for (const auto& guid : trialInfo->memberGuids) { if (!trialInfo->permanentlyFailedPlayerGuids.count(guid)) { potentialCombatants++; } }
    bool allEffectivelyDown = false;
    if (potentialCombatants > 0) {
        uint32 currentlyDownOrOfflineAmongPotential = 0;
        for (const auto& guid : trialInfo->memberGuids) {
            if (trialInfo->permanentlyFailedPlayerGuids.count(guid)) continue;
            if (trialInfo->downedPlayerGuids.count(guid)) { currentlyDownOrOfflineAmongPotential++; }
            else { Player* member = ObjectAccessor::FindPlayer(guid); if (!member || !member->GetSession()) { currentlyDownOrOfflineAmongPotential++; } }
        }
        if (currentlyDownOrOfflineAmongPotential >= potentialCombatants) { allEffectivelyDown = true; }
    }
    if (allEffectivelyDown) { sLog->outMessage("sys", "[TrialOfFinality] Group %u: GROUP WIPE detected. All active members downed or offline.", group->GetId()); FinalizeTrialOutcome(group->GetId(), false, "Group wipe - all members downed or offline."); }
    else { sLog->outDetail("[TrialOfFinality] Group %u: Player %s downed. Other combatants may still be active.", group->GetId(), downedPlayer->GetName().c_str()); }
}

void TrialManager::FinalizeTrialOutcome(uint32 groupId, bool overallSuccess, const std::string& reason) {
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo) { sLog->outWarning("sys", "[TrialOfFinality] FinalizeTrialOutcome called for group %u but no ActiveTrialInfo found. Reason: %s. Trial might have been already cleaned up.", groupId, reason.c_str()); m_activeTrials.erase(groupId); return; }
    sLog->outMessage("sys", "[TrialOfFinality] Finalizing trial for group %u. Overall Success: %s. Reason: %s.", groupId, (overallSuccess ? "Yes" : "No"), reason.c_str());
    if (!overallSuccess) {
        if (!trialInfo->downedPlayerGuids.empty()) {
            sLog->outDetail("[TrialOfFinality] Group %u trial failed. Processing %lu downed players for perma-death.", groupId, trialInfo->downedPlayerGuids.size());
            for(const auto& pair : trialInfo->downedPlayerGuids) {
                ObjectGuid playerGuid = pair.first;
                if (trialInfo->permanentlyFailedPlayerGuids.count(playerGuid)) { continue; }
                Player* downedPlayer = ObjectAccessor::FindPlayer(playerGuid);
                if (downedPlayer && downedPlayer->GetSession()) {
                    if (!downedPlayer->HasAura(AURA_ID_TRIAL_PERMADEATH)) { downedPlayer->AddAura(AURA_ID_TRIAL_PERMADEATH, downedPlayer); }
                    trialInfo->permanentlyFailedPlayerGuids.insert(playerGuid);
                    LogTrialDbEvent(TRIAL_EVENT_PERMADEATH_APPLIED, groupId, downedPlayer, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Perma-death due to trial failure: " + reason);
                    sLog->outCritical("[TrialOfFinality] Player %s (GUID %s, Group %u) PERMANENTLY FAILED due to trial failure: %s (Wave %d).", downedPlayer->GetName().c_str(), playerGuid.ToString().c_str(), groupId, reason.c_str(), trialInfo->currentWave);
                    ChatHandler(downedPlayer->GetSession()).SendSysMessage("The trial has ended in failure. Your fate is sealed.");
                } else {
                    trialInfo->permanentlyFailedPlayerGuids.insert(playerGuid);
                    LogTrialDbEvent(TRIAL_EVENT_PERMADEATH_APPLIED, groupId, nullptr, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Player GUID " + playerGuid.ToString() + " (offline) - Perma-death due to trial failure: " + reason);
                    sLog->outCritical("[TrialOfFinality] Offline Player (GUID %s, Group %u) PERMANENTLY FAILED due to trial failure: %s (Wave %d).", playerGuid.ToString().c_str(), groupId, reason.c_str(), trialInfo->currentWave);
                }
            }
        }
        trialInfo->downedPlayerGuids.clear();
        LogTrialDbEvent(TRIAL_EVENT_TRIAL_FAILURE, groupId, nullptr, trialInfo->currentWave, trialInfo->highestLevelAtStart, reason);
    } else {
        LogTrialDbEvent(TRIAL_EVENT_TRIAL_SUCCESS, groupId, nullptr, trialInfo->currentWave, trialInfo->highestLevelAtStart, reason);
    }
    CleanupTrial(groupId, overallSuccess);
}

void TrialManager::HandleTrialFailure(uint32 groupId, const std::string& reason) {
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo) { sLog->outWarning("sys", "[TrialOfFinality] HandleTrialFailure called for group %u but no active trial found.", groupId); return; }
    sLog->outMessage("sys", "[TrialOfFinality] Trial FAILED for group %u. Reason: %s. Triggering Finalization.", groupId, reason.c_str());
    FinalizeTrialOutcome(groupId, false, reason);
}

void TrialManager::CleanupTrial(uint32 groupId, bool success) { /* ... content from previous correct state ... */
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo) { sLog->outWarning("sys", "[TrialOfFinality] CleanupTrial called for group %u but no active trial info found.", groupId); m_activeTrials.erase(groupId); return; }
    sLog->outMessage("sys", "[TrialOfFinality] Cleaning up trial for group %u (Success: %s).", groupId, success ? "Yes" : "No");
    Map* trialMap = sMapMgr->FindMap(ArenaMapID, 0);
    if (trialMap) {
        if (trialInfo->announcerGuid != ObjectGuid::Empty) {
            if (Creature* announcer = trialMap->GetCreature(trialInfo->announcerGuid)) { announcer->DespawnOrUnsummon(); }
        }
        trialInfo->announcerGuid.Clear();
        for (ObjectGuid monsterGuid : trialInfo->activeMonsters) {
            if (Creature* monster = trialMap->GetCreature(monsterGuid)) { monster->DespawnOrUnsummon(); }
        }
        trialInfo->activeMonsters.clear();
    } else { sLog->outError("sys", "[TrialOfFinality] CleanupTrial: Could not find map %u for group %u to despawn NPCs.", ArenaMapID, groupId); }
    for (ObjectGuid memberGuid : trialInfo->memberGuids) {
        Player* member = ObjectAccessor::FindPlayer(memberGuid);
        if (member && member->GetSession()) {
            if (member->HasItemCount(TrialTokenEntry, 1, false)) { member->DestroyItemCount(TrialTokenEntry, 1, true); sLog->outDetail("[TrialOfFinality] Removed Trial Token from %s.", member->GetName().c_str()); }
            member->SetDisableXpGain(false, true);
            sLog->outDetail("[TrialOfFinality] XP gain re-enabled for %s.", member->GetName().c_str());
            if (!member->HasAura(AURA_ID_TRIAL_PERMADEATH)) {
                sLog->outDetail("[TrialOfFinality] Teleporting %s out of arena.", member->GetName().c_str());
                if (!member->TeleportToHearthstone()) {
                     WorldLocation safeLoc = member->GetStartPosition();
                     member->TeleportTo(safeLoc.GetMapId(), safeLoc.GetPositionX(), safeLoc.GetPositionY(), safeLoc.GetPositionZ(), safeLoc.GetOrientation());
                }
            }
        }
    }
    m_activeTrials.erase(groupId);
}

bool TrialManager::ValidateGroupForTrial(Player* leader, Creature* trialNpc) { /* ... content from previous correct state ... */
    ChatHandler chat(leader->GetSession());
    Group* group = leader->GetGroup();
    if (!group) { chat.SendSysMessage("You must be in a group to start the Trial."); return false; }
    if (group->GetLeaderGUID() != leader->GetGUID()) { chat.SendSysMessage("Only the group leader can initiate the Trial."); return false; }
    uint32 groupSize = group->GetMembersCount();
    if (groupSize < MinGroupSize || groupSize > MaxGroupSize) { chat.PSendSysMessage("Your group size must be between %u and %u players. You have %u.", MinGroupSize, MaxGroupSize, groupSize); return false; }
    uint8 minPlayerLevel = 255; uint8 maxPlayerLevel = 0;
    uint32 leaderMapId = leader->GetMapId(); uint32 leaderZoneId = leader->GetZoneId();
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
        Player* member = itr->GetSource();
        if (!member || !member->GetSession()) continue;
        if (member->GetMapId() != leaderMapId || member->GetZoneId() != leaderZoneId) { chat.PSendSysMessage("All group members must be in the same zone as you. Player %s is not.", member->GetName().c_str()); return false; }
        if (trialNpc && !member->IsWithinDistInMap(trialNpc, 100.0f)) { chat.PSendSysMessage("All group members must be near Fateweaver Arithos. Player %s is too far.", member->GetName().c_str()); return false; }
        uint8 memberLevel = member->getLevel();
        if (memberLevel < minPlayerLevel) minPlayerLevel = memberLevel;
        if (memberLevel > maxPlayerLevel) maxPlayerLevel = memberLevel;
        if (member->IsPlayerBot()) { chat.PSendSysMessage("Playerbots are not allowed in the Trial of Finality. Player %s is a bot.", member->GetName().c_str()); sLog->outWarning("sys", "[TrialOfFinality] Playerbot %s detected in group attempting to start trial. Leader: %s", member->GetName().c_str(), leader->GetName().c_str()); return false; }
        if (member->HasItemCount(TrialTokenEntry, 1, false)) { chat.PSendSysMessage("A member (%s) already has a Trial Token.", member->GetName().c_str()); return false; }
    }
    if (maxPlayerLevel == 0 && minPlayerLevel == 255) { chat.SendSysMessage("Could not verify group members' levels or eligibility."); return false;}
    if ((maxPlayerLevel - minPlayerLevel) > MaxLevelDifference) { chat.PSendSysMessage("Level difference between highest (%u) and lowest (%u) exceeds %u.", maxPlayerLevel, minPlayerLevel, MaxLevelDifference); return false; }
    return true;
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

    // Called from NPC gossip after validation passes
    bool InitiateTrial(Player* leader)
    {
        if (!ModuleEnabled || !player || !player->GetSession()) return;

        // 1. Handle Perma-deathed players (kick them)
        if (player->HasAura(AURA_ID_TRIAL_PERMADEATH)) {
            sLog->outMessage("sys", "[TrialOfFinality] Player %s (GUID %s, Account %u) attempted to login with Permadeath Aura. Preventing login.",
                player->GetName().c_str(), player->GetGUID().ToString().c_str(), player->GetSession()->GetAccountId());
            player->GetSession()->SendNotification("This character succumbed to the Trial of Finality and can no longer enter the world.");
            player->GetSession()->KickPlayer();
            return;
        }

        // 2. Handle players rejoining an active trial or dealing with stray tokens
        bool inActiveTrialAndEligible = false;
        ActiveTrialInfo* trialInfoForRejoin = nullptr;

        Group* group = player->GetGroup();
        if (group) {
            trialInfoForRejoin = TrialManager::instance()->GetActiveTrialInfo(group->GetId());
            if (trialInfoForRejoin &&
                trialInfoForRejoin->memberGuids.count(player->GetGUID()) &&
                !trialInfoForRejoin->permanentlyFailedPlayerGuids.count(player->GetGUID())) {
                inActiveTrialAndEligible = true;
            }
        }

        if (inActiveTrialAndEligible && trialInfoForRejoin) {
            sLog->outMessage("sys", "[TrialOfFinality] Player %s rejoining active trial for group %u (Wave %d).",
                player->GetName().c_str(), group->GetId(), trialInfoForRejoin->currentWave);
            LogTrialDbEvent(TRIAL_EVENT_PLAYER_RECONNECT,
                            group->GetId(), player, trialInfoForRejoin->currentWave, trialInfoForRejoin->highestLevelAtStart,
                            "Player reconnected to active trial.");

            if (player->GetMapId() != ArenaMapID || !player->IsWithinDist3d(ArenaTeleportX, ArenaTeleportY, ArenaTeleportZ, 200.0f)) {
                player->TeleportTo(ArenaMapID, ArenaTeleportX, ArenaTeleportY, ArenaTeleportZ, ArenaTeleportO);
                sLog->outDetail("[TrialOfFinality] Teleported rejoining player %s to arena.", player->GetName().c_str());
            }

            if (!player->HasItemCount(TrialTokenEntry, 1, false)) {
                if (Item* trialToken = player->AddItem(TrialTokenEntry, 1)) {
                     player->SendNewItem(trialToken, 1, true, false);
                     sLog->outWarning("sys", "[TrialOfFinality] Re-granted Trial Token to rejoining player %s.", player->GetName().c_str());
                } else {
                     sLog->outError("sys", "[TrialOfFinality] Failed to re-grant Trial Token to rejoining player %s!", player->GetName().c_str());
                }
            }

            if (!player->IsDisableXpGain()) {
                player->SetDisableXpGain(true, true);
                sLog->outDetail("[TrialOfFinality] XP gain re-disabled for rejoining player %s.", player->GetName().c_str());
            }
        } else if (player->HasItemCount(TrialTokenEntry, 1, false)) {
            sLog->outWarning("sys", "[TrialOfFinality] Player %s logged in with a stray Trial Token. Removing it.", player->GetName().c_str());
            player->DestroyItemCount(TrialTokenEntry, 1, true);
            LogTrialDbEvent(TRIAL_EVENT_STRAY_TOKEN_REMOVED,
                            0, player, 0, 0, "Stray token removed on login.");
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

    void OnPlayerLogout(Player* player) override
    {
        if (!ModuleEnabled || !player) {
            return;
        }

        Group* group = player->GetGroup();
        if (!group) {
            return;
        }

        ActiveTrialInfo* trialInfo = TrialManager::instance()->GetActiveTrialInfo(group->GetId());
        if (trialInfo) {
            if (trialInfo->memberGuids.count(player->GetGUID()) &&
                !trialInfo->permanentlyFailedPlayerGuids.count(player->GetGUID())) {

                sLog->outMessage("sys", "[TrialOfFinality] Player %s (GUID %s, Group %u) LOGGED OUT during active trial (Wave %d).",
                    player->GetName().c_str(), player->GetGUID().ToString().c_str(), group->GetId(), trialInfo->currentWave);

                if (!trialInfo->downedPlayerGuids.count(player->GetGUID())) {
                    trialInfo->downedPlayerGuids[player->GetGUID()] = time(nullptr);
                    LogTrialDbEvent(TRIAL_EVENT_PLAYER_DISCONNECT,
                                    group->GetId(), player, trialInfo->currentWave, trialInfo->highestLevelAtStart,
                                    "Player disconnected during wave.");
                }

                uint32 potentialCombatants = 0;
                for (const auto& guid : trialInfo->memberGuids) {
                    if (!trialInfo->permanentlyFailedPlayerGuids.count(guid)) {
                        potentialCombatants++;
                    }
                }

                bool allEffectivelyDownOrOffline = false;
                if (potentialCombatants > 0) {
                    uint32 currentlyDownOrActuallyOffline = 0;
                    for (const auto& guid : trialInfo->memberGuids) {
                        if (trialInfo->permanentlyFailedPlayerGuids.count(guid)) continue;

                        if (trialInfo->downedPlayerGuids.count(guid)) {
                            currentlyDownOrActuallyOffline++;
                        } else {
                            Player* member = ObjectAccessor::FindPlayer(guid);
                            if (!member || !member->GetSession()) {
                                currentlyDownOrActuallyOffline++;
                            }
                        }
                    }
                    if (currentlyDownOrActuallyOffline >= potentialCombatants) {
                        allEffectivelyDownOrOffline = true;
                    }
                }

                if (allEffectivelyDownOrOffline) {
                    sLog->outMessage("sys", "[TrialOfFinality] Group %u: GROUP WIPE detected due to disconnect. All active members downed or offline.", group->GetId());
                    TrialManager::instance()->FinalizeTrialOutcome(group->GetId(), false /*success*/, "Group wipe due to player disconnect.");
                }
            }
        }
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
        if (!ModuleEnabled || !killer || !killed || !killer->GetSession()) { return; }
        Group* group = killer->GetGroup();
        if (!group) { return; }
        ActiveTrialInfo* trialInfo = TrialManager::instance()->GetActiveTrialInfo(group->GetId());
        if (trialInfo) {
            if (trialInfo->activeMonsters.count(killed->GetGUID())) {
                sLog->outDetail("[TrialOfFinality] Player %s (Group %u) killed trial monster %s (Entry %u) from wave %d.", killer->GetName().c_str(), group->GetId(), killed->GetGUID().ToString().c_str(), killed->GetEntry(), trialInfo->currentWave);
                TrialManager::instance()->HandleMonsterKilledInTrial(killed->GetGUID(), group->GetId());
            }
        }
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

    void OnPlayerKilledByCreature(Creature* /*killer*/, Player* killed) override
    {
        if (!ModuleEnabled || !killed || !killed->GetSession()) { return; }
        Group* group = killed->GetGroup();
        if (!group) { return; }
        ActiveTrialInfo* trialInfo = TrialManager::instance()->GetActiveTrialInfo(group->GetId());
        if (trialInfo) {
            if (trialInfo->memberGuids.count(killed->GetGUID()) &&
                !trialInfo->permanentlyFailedPlayerGuids.count(killed->GetGUID()) &&
                !trialInfo->downedPlayerGuids.count(killed->GetGUID())) {
                if (killed->HasItemCount(TrialTokenEntry, 1, false)) {
                    sLog->outMessage("sys", "[TrialOfFinality] Player %s (GUID %s, Group %u) was downed with Trial Token in Wave %d.",
                        killed->GetName().c_str(), killed->GetGUID().ToString().c_str(), group->GetId(), trialInfo->currentWave);
                    TrialManager::instance()->HandlePlayerDownedInTrial(killed);
                }
            }
        }
    }

    void OnPlayerResurrect(Player* player, Player* resurrector) override
    {
        if (!ModuleEnabled || !player || !player->GetSession()) { return; }
        Group* group = player->GetGroup();
        if (!group) { return; }
        ActiveTrialInfo* trialInfo = TrialManager::instance()->GetActiveTrialInfo(group->GetId());
        if (trialInfo) {
            auto it = trialInfo->downedPlayerGuids.find(player->GetGUID());
            if (it != trialInfo->downedPlayerGuids.end()) {
                ObjectGuid healerGuid = resurrector ? resurrector->GetGUID() : ObjectGuid::Empty;
                std::string healerName = resurrector ? resurrector->GetName() : "Unknown/Self";
                sLog->outMessage("sys", "[TrialOfFinality] Player %s (GUID %s, Group %u) was RESURRECTED by %s (GUID %s) during Wave %d.", player->GetName().c_str(), player->GetGUID().ToString().c_str(), group->GetId(), healerName.c_str(), healerGuid.ToString().c_str(), trialInfo->currentWave);
                trialInfo->downedPlayerGuids.erase(it);
                LogTrialDbEvent(TRIAL_EVENT_PLAYER_RESURRECTED, group->GetId(), player, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Resurrected by " + healerName + " (GUID " + healerGuid.ToString() + ").");
                ChatHandler(player->GetSession()).SendSysMessage("You have been resurrected and rejoin the Trial of Finality!");
                if (resurrector && resurrector->GetSession() && resurrector != player) { ChatHandler(resurrector->GetSession()).PSendSysMessage("%s has been successfully resurrected and rejoins the Trial!", player->GetName().c_str()); }
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

class ModServerScript : public ServerScript { /* ... */ }; // Contents of ModServerScript, Addmod_trial_of_finality_Scripts, Addmod_trial_of_finality are unchanged from previous correct state
}

void Addmod_trial_of_finality_Scripts() { /* ... */ }
extern "C" void Addmod_trial_of_finality() { /* ... */ }
