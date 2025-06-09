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

// --- Creature ID Pools for Waves ---
const std::vector<uint32> POOL_WAVE_CREATURES_EASY = { 70001, 70002, 70003, 70004, 70005, 70006, 70007, 70008, 70009, 70010 };
const std::vector<uint32> POOL_WAVE_CREATURES_MEDIUM = { 70011, 70012, 70013, 70014, 70015, 70016, 70017, 70018, 70019, 70020 };
const std::vector<uint32> POOL_WAVE_CREATURES_HARD = { 70021, 70022, 70023, 70024, 70025, 70026, 70027, 70028, 70029, 70030 };

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

void TrialManager::SpawnActualWave(uint32 groupId) { /* ... (as of Enhancement 1) ... */ }

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
                if (downedPlayer_obj && downedPlayer_obj->GetSession()) {
                    if (!downedPlayer_obj->HasAura(AURA_ID_TRIAL_PERMADEATH)) { downedPlayer_obj->AddAura(AURA_ID_TRIAL_PERMADEATH, downedPlayer_obj); }
                    trialInfo->permanentlyFailedPlayerGuids.insert(playerGuid);
                    LogTrialDbEvent(TRIAL_EVENT_PERMADEATH_APPLIED, groupId, downedPlayer_obj, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Perma-death due to trial failure: " + reason);
                    sLog->outCritical("[TrialOfFinality] Player %s (GUID %s, Group %u) PERMANENTLY FAILED due to trial failure: %s (Wave %d).",
                        downedPlayer_obj->GetName().c_str(), playerGuid.ToString().c_str(), groupId, reason.c_str(), trialInfo->currentWave);
                    ChatHandler(downedPlayer_obj->GetSession()).SendSysMessage("The trial has ended in failure. Your fate is sealed.");
                } else {
                    trialInfo->permanentlyFailedPlayerGuids.insert(playerGuid);
                    LogTrialDbEvent(TRIAL_EVENT_PERMADEATH_APPLIED, groupId, nullptr, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Player GUID " + playerGuid.ToString() + " (offline) - Perma-death due to trial failure: " + reason);
                    sLog->outCritical("[TrialOfFinality] Offline Player (GUID %s, Group %u) PERMANENTLY FAILED due to trial failure: %s (Wave %d).", playerGuid.ToString().c_str(), groupId, reason.c_str(), trialInfo->currentWave);
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
    if (!CheeringNpcsEnable || CheeringNpcCityZoneIDs.empty()) {
        return;
    }
    sLog->outDetail("[TrialOfFinality] Attempting to trigger city NPC cheers.");
    std::set<ObjectGuid> alreadyCheeringNpcs;
    int totalCheeredThisEvent = 0;
    Map::PlayerList const& players = sWorld->GetAllPlayers();
    if (players.isEmpty()) return;
    for (auto const& itr : players) {
        Player* player = itr.GetSource();
        if (!player || !player->GetSession() || !player->IsInWorld() || !player->GetMap()) { continue; }
        if (totalCheeredThisEvent >= CheeringNpcsMaxTotalWorld) { break; }
        if (CheeringNpcCityZoneIDs.count(player->GetZoneId())) {
            std::list<Creature*> nearbyCreatures;
            CellCoord cell(player->GetPosition());
            Cell cellObj = Cell(cell);
            Trinity::DefaultGridTypechecker<Creature> checker;
            Trinity::CreatureListSearcher<Trinity::DefaultGridTypechecker<Creature>> searcher(player, nearbyCreatures, CheeringNpcsRadiusAroundPlayer, checker);
            player->VisitNearbyGridObject(searcher, CheeringNpcsRadiusAroundPlayer);
            int cheeredThisCluster = 0;
            for (Creature* creature : nearbyCreatures) {
                if (totalCheeredThisEvent >= CheeringNpcsMaxTotalWorld || cheeredThisCluster >= CheeringNpcsMaxPerPlayerCluster) { break; }
                if (alreadyCheeringNpcs.count(creature->GetGUID())) { continue; }
                if (creature->IsAlive() && !creature->IsInCombat() && creature->GetTypeId() == TYPEID_UNIT && !creature->ToPlayer()) {
                    uint32 npcFlags = creature->GetNpcFlags();
                    bool isTargetType = (CheeringNpcsTargetNpcFlags == UNIT_NPC_FLAG_NONE) || (npcFlags & CheeringNpcsTargetNpcFlags);
                    bool isExcludedType = (CheeringNpcsExcludeNpcFlags != 0) && (npcFlags & CheeringNpcsExcludeNpcFlags);
                    if (isTargetType && !isExcludedType) {
                        creature->HandleEmoteCommand(EMOTE_ONESHOT_CHEER);
                        alreadyCheeringNpcs.insert(creature->GetGUID());
                        cheeredThisCluster++;
                        totalCheeredThisEvent++;
                    }
                }
            }
        }
    }
    sLog->outMessage("sys", "[TrialOfFinality] Triggered %d city NPCs to cheer.", totalCheeredThisEvent);
    LogTrialDbEvent(TRIAL_EVENT_NPC_CHEER_TRIGGERED, 0, nullptr, 0, 0, "Total NPCs cheered: " + std::to_string(totalCheeredThisEvent));
}


// --- Announcer AI and Script ---
class npc_trial_announcer_ai : public ScriptedAI { /* ... */ };
class npc_trial_announcer : public CreatureScript { /* ... */ };
// --- NPC Scripts ---
enum FateweaverArithosGossipActions { /* ... */ };
class npc_fateweaver_arithos : public CreatureScript { /* ... */ };
// --- Player and World Event Scripts ---
class ModPlayerScript : public PlayerScript { /* ... */ };

class ModServerScript : public ServerScript
{
public:
    ModServerScript() : ServerScript("ModTrialOfFinalityServerScript") {}
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

        if (!FateweaverArithosEntry || !TrialTokenEntry || !AnnouncerEntry || !TitleRewardID) {
            sLog->outError("sys", "Trial of Finality: Critical EntryID (NPC, Item, Title) not configured. Disabling module functionality.");
            ModuleEnabled = false; return;
        }
        sLog->outMessage("sys", "Trial of Finality: Configuration loaded. Module enabled.");
        if (reload) { sLog->outMessage("sys", "Trial of Finality: Configuration reloaded. Consider restarting for full effect if scripts were already registered."); }
    }
};

// --- GM Command Scripts ---
class trial_commandscript : public CommandScript { /* ... existing ... */ };

}

void Addmod_trial_of_finality_Scripts() { /* ... existing ... */ }
extern "C" void Addmod_trial_of_finality() { /* ... */ }
