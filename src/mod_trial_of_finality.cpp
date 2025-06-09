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

#include "ObjectAccessor.h"
#include "Player.h"
#include "DBCStores.h"
#include "CharTitles.h"
#include "DatabaseEnv.h"


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
    std::set<ObjectGuid> playersWarnedForLeavingArena; // New for 11c

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

bool TrialManager::InitiateTrial(Player* leader) { /* ... existing ... */
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

void TrialManager::CheckPlayerLocationsAndEnforceBoundaries(uint32 groupId) {
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo || trialInfo->currentWave == 0) { return; }
    sLog->outDetail("[TrialOfFinality] Group %u: Checking player locations for arena boundary enforcement (Wave %d).", groupId, trialInfo->currentWave);
    Map* trialMap = sMapMgr->FindMap(ArenaMapID, 0);
    if (!trialMap) { sLog->outError("sys", "[TrialOfFinality] CheckPlayerLocations: Could not find map %u for group %u. Cannot enforce boundaries.", ArenaMapID, groupId); return; }
    std::set<ObjectGuid> currentMembersToProcess = trialInfo->memberGuids;
    for (const auto& playerGuid : currentMembersToProcess) {
        if (trialInfo->permanentlyFailedPlayerGuids.count(playerGuid) || trialInfo->downedPlayerGuids.count(playerGuid)) { continue; }
        Player* player = ObjectAccessor::FindPlayer(playerGuid);
        if (!player || !player->GetSession()) { continue; }
        bool isOutsideArena = (player->GetMapId() != ArenaMapID);
        if (isOutsideArena) {
            if (trialInfo->playersWarnedForLeavingArena.count(playerGuid)) {
                sLog->outCritical("[TrialOfFinality] Player %s (GUID %s, Group %u) left arena AGAIN. Forfeiting trial for this player.", player->GetName().c_str(), playerGuid.ToString().c_str(), groupId);
                ChatHandler(player->GetSession()).SendSysMessage("You have left the Trial of Finality arena again and forfeited your attempt. Your fate is sealed.");
                if (!player->HasAura(AURA_ID_TRIAL_PERMADEATH)) { player->AddAura(AURA_ID_TRIAL_PERMADEATH, player); }
                trialInfo->permanentlyFailedPlayerGuids.insert(playerGuid);
                LogTrialDbEvent(TRIAL_EVENT_PLAYER_FORFEIT_ARENA, groupId, player, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Left arena after warning.");
                if (player->HasItemCount(TrialTokenEntry, 1, false)) { player->DestroyItemCount(TrialTokenEntry, 1, true); }
                player->SetDisableXpGain(false, true);
                bool activePlayersStillInTrial = false;
                for (const auto& memberGuid_inner : trialInfo->memberGuids) {
                    if (!trialInfo->permanentlyFailedPlayerGuids.count(memberGuid_inner)) { activePlayersStillInTrial = true; break; }
                }
                if (!activePlayersStillInTrial) {
                    sLog->outMessage("sys", "[TrialOfFinality] Group %u: Last active player forfeited by leaving arena. Trial ends.", groupId);
                    FinalizeTrialOutcome(groupId, false, "Last active player forfeited by leaving arena.");
                } else {
                    std::string forfeitMessage = player->GetName() + " has forfeited the Trial of Finality by leaving the arena.";
                     for(ObjectGuid memberGuid_inner : trialInfo->memberGuids) {
                        if (!trialInfo->permanentlyFailedPlayerGuids.count(memberGuid_inner) && memberGuid_inner != playerGuid) {
                            if(Player* otherMember = ObjectAccessor::FindPlayer(memberGuid_inner)) {
                                if (otherMember->GetSession()) ChatHandler(otherMember->GetSession()).SendSysMessage(forfeitMessage.c_str());
                            }
                        }
                    }
                }
            } else {
                sLog->outWarning("sys", "[TrialOfFinality] Player %s (GUID %s, Group %u) found outside arena. Warning and teleporting back.", player->GetName().c_str(), playerGuid.ToString().c_str(), groupId);
                player->TeleportTo(ArenaMapID, ArenaTeleportX, ArenaTeleportY, ArenaTeleportZ, ArenaTeleportO);
                ChatHandler(player->GetSession()).SendSysMessage("WARNING: You have strayed from the Trial of Finality arena! You have been returned. Leaving again will result in forfeiture and permanent consequences!");
                trialInfo->playersWarnedForLeavingArena.insert(playerGuid);
                LogTrialDbEvent(TRIAL_EVENT_PLAYER_WARNED_ARENA_LEAVE, groupId, player, trialInfo->currentWave, trialInfo->highestLevelAtStart, "Found outside arena, teleported back.");
            }
        }
    }
}

void TrialManager::PrepareAndAnnounceWave(uint32 groupId, int waveNumber, uint32 delayMs) {
    ActiveTrialInfo* trialInfo = GetActiveTrialInfo(groupId);
    if (!trialInfo) { sLog->outError("sys", "[TrialOfFinality] PrepareAndAnnounceWave: Could not find active trial for group %u", groupId); return; }
    CheckPlayerLocationsAndEnforceBoundaries(groupId); // Call added
    trialInfo = GetActiveTrialInfo(groupId); // Re-fetch
    if (!trialInfo) { sLog->outMessage("sys", "[TrialOfFinality] PrepareAndAnnounceWave: Trial for group %u ended during location check.", groupId); return; }
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

void TrialManager::HandleMonsterKilledInTrial(ObjectGuid monsterGuid, uint32 groupId) { /* ... existing ... */
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
void TrialManager::SpawnActualWave(uint32 groupId) { /* ... existing ... */ }
void TrialManager::HandlePlayerDownedInTrial(Player* downedPlayer) { /* ... existing ... */ }
void TrialManager::FinalizeTrialOutcome(uint32 groupId, bool overallSuccess, const std::string& reason) { /* ... existing ... */ }
void TrialManager::HandleTrialFailure(uint32 groupId, const std::string& reason) { /* ... existing ... */ }
void TrialManager::CleanupTrial(uint32 groupId, bool success) { /* ... existing ... */ }
bool TrialManager::ValidateGroupForTrial(Player* leader, Creature* trialNpc) { /* ... existing ... */ }

// --- Announcer AI and Script ---
class npc_trial_announcer_ai : public ScriptedAI { /* ... */ };
class npc_trial_announcer : public CreatureScript { /* ... */ };

// --- NPC Scripts ---
enum FateweaverArithosGossipActions { /* ... */ };
class npc_fateweaver_arithos : public CreatureScript { /* ... */ };

// --- Player and World Event Scripts ---
class ModPlayerScript : public PlayerScript { /* ... existing, including OnLogin, OnCreatureKill, OnPlayerKilledByCreature, OnPlayerLogout, OnPlayerResurrect ... */ };
class ModServerScript : public ServerScript { /* ... */ };

// --- GM Command Scripts ---
class trial_commandscript : public CommandScript { /* ... existing ... */ };

} // namespace ModTrialOfFinality

void Addmod_trial_of_finality_Scripts() { /* ... existing ... */ }
extern "C" void Addmod_trial_of_finality() { /* ... */ }
