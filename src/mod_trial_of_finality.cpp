// AzerothCore includes
#include "AreaTriggerScript.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GossipDef.h"
#include "game/Maps/MapManager.h"
#include "Group.h"
#include "Item.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "CreatureScript.h"
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
#include "CharacterCache.h"
#include "InstanceScript.h"

// Module specific namespace
namespace ModTrialOfFinality
{

// --- Instance Script for the Trial ---
// This class will manage the state and events for a single Trial of Finality instance.
struct instance_trial_of_finality : public InstanceScript
{
    instance_trial_of_finality(Map* map) : InstanceScript(map) { }

    // --- State Tracking ---
    uint32 currentWave;
    uint8 highestLevelAtStart;
    ObjectGuid announcerGuid;
    std::set<ObjectGuid> activeMonsters;
    std::map<ObjectGuid, time_t> downedPlayerGuids;
    std::set<ObjectGuid> permanentlyFailedPlayerGuids;
    std::set<ObjectGuid> playersWarnedForLeavingArena;
    bool isTestTrial;

    // Forfeit Vote
    bool forfeitVoteInProgress;
    time_t forfeitVoteStartTime;
    std::set<ObjectGuid> playersWhoVotedForfeit;

    // --- Overridden Hooks ---
    uint32 boundaryCheckTimer;
    uint32 forfeitCheckTimer;

    void Initialize() override
    {
        // Set up the instance for 5 waves (boss encounters)
        SetBossNumber(5);
        currentWave = 0;
        forfeitVoteInProgress = false;
        boundaryCheckTimer = 5000;
        forfeitCheckTimer = 1000;
        isTestTrial = false;
    }

    void Update(uint32 diff) override
    {
        scheduler.Update(diff);

        // --- Boundary Check ---
        if (boundaryCheckTimer <= diff)
        {
            boundaryCheckTimer = 5000; // Reset timer
            if (GetBossState(currentWave -1) == IN_PROGRESS)
                CheckPlayerLocationsAndEnforceBoundaries();
        }
        else
        {
            boundaryCheckTimer -= diff;
        }

        // --- Forfeit Vote Check ---
        if (forfeitCheckTimer <= diff)
        {
            forfeitCheckTimer = 1000; // Reset timer
            if (forfeitVoteInProgress)
            {
                // Timeout Check
                if (time(nullptr) - forfeitVoteStartTime > 30)
                {
                    forfeitVoteInProgress = false;
                    playersWhoVotedForfeit.clear();
                    std::string msg = "The vote to forfeit the trial has failed to pass in time and is now cancelled.";
                    uint32 groupId = 0;
                    if (Player* p = instance->GetPlayer(0)) { if (p->GetGroup()) groupId = p->GetGroup()->GetId(); }
                    LogTrialDbEvent(TRIAL_EVENT_FORFEIT_VOTE_CANCEL, groupId, nullptr, currentWave, highestLevelAtStart, "Vote timed out.");
                    DoSendNotifyToInstance(msg.c_str());
                }
            }
        }
        else
        {
            forfeitCheckTimer -= diff;
        }
    }

    void HandleMonsterKilled(Creature* creature)
    {
        if (activeMonsters.erase(creature->GetGUID()))
        {
            sLog->outDetail("[TrialOfFinality] Instance %u killed a trial monster. %lu remaining in wave %d.",
                instance->GetInstanceId(), activeMonsters.size(), currentWave);

            if (activeMonsters.empty())
            {
                sLog->outInfo("sys", "[TrialOfFinality] Instance %u has cleared wave %d.", instance->GetInstanceId(), currentWave);
                SetBossState(currentWave - 1, DONE); // Mark current wave as done (wave 1 is boss 0)

                // Clear any downed players from the previous wave - they are now safe
                if (!downedPlayerGuids.empty())
                {
                    instance->DoForAllPlayers([this](Player* player)
                    {
                        if (downedPlayerGuids.count(player->GetGUID()))
                        {
                            ChatHandler(player->GetSession()).SendSysMessage("The wave is over! You have survived... for now.");
                        }
                    });
                    downedPlayerGuids.clear();
                }

                if (currentWave < 5)
                {
                    PrepareAndAnnounceWave(currentWave + 1, 8000); // 8 second delay between waves
                    SetBossState(currentWave, IN_PROGRESS);
                }
                else
                {
                    FinalizeTrialOutcome(true, "All 5 waves successfully cleared.");
                }
            }
        }
    }

    void OnCreatureCreate(Creature* creature) override
    {
        if (creature->GetEntry() == AnnouncerEntry)
        {
            announcerGuid = creature->GetGUID();
        }
        else if (creature->IsHostileToPlayers()) // A simple way to identify trial monsters
        {
            activeMonsters.insert(creature->GetGUID());
        }
    }

    void OnPlayerEnter(Player* player) override
    {
        if (!player)
        {
            return;
        }

        // The first player to enter initializes the instance's difficulty and starts the trial.
        if (GetBossState(0) == NOT_STARTED)
        {
            if (PreTrialData* data = TrialManager::instance()->GetPreTrialData(player->GetGroup()->GetId()))
            {
                highestLevelAtStart = data->highestLevel;
                isTestTrial = data->isTestTrial;
                TrialManager::instance()->CleanupPreTrialData(player->GetGroup()->GetId()); // Clean up the cached data
                sLog->outInfo("sys", "[TrialOfFinality] Instance %u initialized for group %u with highest level %u.", instance->GetInstanceId(), player->GetGroup()->GetId(), highestLevelAtStart);

                // Start Wave 1
                PrepareAndAnnounceWave(1, 5000);
                SetBossState(0, IN_PROGRESS);
                LogTrialDbEvent(TRIAL_EVENT_START, player->GetGroup()->GetId(), player, 0, highestLevelAtStart, "Trial started in instance.");
            }
            else
            {
                sLog->outError("sys", "[TrialOfFinality] Could not find pre-trial data for group %u when entering instance %u. Aborting.", player->GetGroup()->GetId(), instance->GetInstanceId());
                player->TeleportTo(player->GetBindPoint());
                return;
            }
        }

        // Setup for each player entering
        player->SetDisableXpGain(true, true);
        player->AddItem(TrialTokenEntry, 1);
        ChatHandler(player->GetSession()).SendSysMessage("The Trial of Finality has begun!");
    }

    // --- Wave Management ---
    void PrepareAndAnnounceWave(int waveNumber, uint32 delayMs)
    {
        currentWave = waveNumber;
        uint32 groupId = 0;
        if (Player* p = instance->GetPlayer(0)) { if (p->GetGroup()) groupId = p->GetGroup()->GetId(); }
        sLog->outInfo("sys", "[TrialOfFinality] Instance %u preparing for wave %d.", instance->GetInstanceId(), waveNumber);
        LogTrialDbEvent(TRIAL_EVENT_WAVE_START, groupId, nullptr, waveNumber, highestLevelAtStart, "Announcing wave.");

        Creature* announcer = nullptr;
        if (announcerGuid.IsEmpty())
        {
            Position announcerPos = { ArenaTeleportX + 5.0f, ArenaTeleportY, ArenaTeleportZ, ArenaTeleportO };
            announcer = instance->SummonCreature(AnnouncerEntry, announcerPos, TEMPSUMMON_MANUAL_DESPAWN);
        }
        else
        {
            announcer = instance->GetCreature(announcerGuid);
        }

        if (announcer)
        {
            if (auto* ai = dynamic_cast<npc_trial_announcer_ai*>(announcer->AI()))
                ai->AnnounceWave(waveNumber);
            else
            {
                std::string waveAnnounce = "Brave contenders, prepare yourselves! Wave " + std::to_string(waveNumber) + " approaches!";
                announcer->Yell(waveAnnounce, LANG_UNIVERSAL, nullptr);
            }
        }

        scheduler.Schedule(std::chrono::milliseconds(delayMs), [this]()
        {
            SpawnActualWave();
        });
    }

    void SpawnActualWave()
    {
        uint32 activePlayers = 0;
        instance->DoForAllPlayers([&](Player* player)
        {
            if (player->IsAlive() && !downedPlayerGuids.count(player->GetGUID()))
                activePlayers++;
        });

        if (activePlayers == 0)
        {
            sLog->outError("sys", "[TrialOfFinality] Instance %u, Wave %d: No active players left to spawn wave for. Finalizing trial.", instance->GetInstanceId(), currentWave);
            // FinalizeTrialOutcome(false, "All players defeated or disconnected before wave " + std::to_string(currentWave) + " could spawn."); // to be implemented
            return;
        }

        const std::vector<std::vector<uint32>>* currentWaveNpcPool = nullptr;
        const CustomNpcScalingTier* customScalingTier = nullptr;
        float healthMultiplier = 1.0f;
        const std::vector<uint32>* aurasToAdd = nullptr;

        if (currentWave <= 2) {
            currentWaveNpcPool = &NpcPoolEasy;
            customScalingTier = &CustomScalingEasy;
        } else if (currentWave <= 4) {
            currentWaveNpcPool = &NpcPoolMedium;
            customScalingTier = &CustomScalingMedium;
        } else {
            currentWaveNpcPool = &NpcPoolHard;
            customScalingTier = &CustomScalingHard;
        }

        if (NpcScalingMode == "custom_scaling_rules" && customScalingTier) {
            healthMultiplier = customScalingTier->HealthMultiplier;
            aurasToAdd = &customScalingTier->AurasToAdd;
        }

        if (!currentWaveNpcPool || currentWaveNpcPool->empty()) {
            sLog->outError("sys", "[TrialOfFinality] Instance %u, Wave %d: Cannot spawn wave. NPC pool for this difficulty is empty.", instance->GetInstanceId(), currentWave);
            // FinalizeTrialOutcome(false, "Internal error: NPC pool empty for wave " + std::to_string(currentWave));
            return;
        }

        if (WAVE_SPAWN_POSITIONS.empty()) {
            sLog->outError("sys", "[TrialOfFinality] Instance %u, Wave %d: Cannot spawn wave. No spawn positions are configured or loaded.", instance->GetInstanceId(), currentWave);
            // FinalizeTrialOutcome(false, "Internal error: No spawn positions configured.");
            return;
        }
        uint32 numSpawnsPerWave = WAVE_SPAWN_POSITIONS.size();

        uint32 numGroupsToSpawn = std::min((uint32)numSpawnsPerWave, activePlayers + 1);
        numGroupsToSpawn = std::max(numGroupsToSpawn, 1u);

        if (numGroupsToSpawn > currentWaveNpcPool->size()) {
            sLog->outWarn("sys", "[TrialOfFinality] Instance %u, Wave %d: Requested %u encounter groups, but pool only has %lu. Spawning %lu instead.",
                instance->GetInstanceId(), currentWave, numGroupsToSpawn, currentWaveNpcPool->size(), currentWaveNpcPool->size());
            numGroupsToSpawn = currentWaveNpcPool->size();
        }

        std::vector<std::vector<uint32>> selectedGroups = *currentWaveNpcPool;
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(selectedGroups.begin(), selectedGroups.end(), g);

        sLog->outInfo("sys", "[TrialOfFinality] Instance %u, Wave %d: Spawning %u encounter groups. Highest Lvl: %u. Health Multi: %.2f",
            instance->GetInstanceId(), currentWave, numGroupsToSpawn, highestLevelAtStart, healthMultiplier);
        activeMonsters.clear();

        uint32 spawnPosIndex = 0;
        for (uint32 i = 0; i < numGroupsToSpawn; ++i)
        {
            const std::vector<uint32>& groupOfNpcs = selectedGroups[i];
            if (spawnPosIndex + groupOfNpcs.size() > numSpawnsPerWave)
                continue;

            for (uint32 creatureEntry : groupOfNpcs)
            {
                const Position& spawnPos = WAVE_SPAWN_POSITIONS[spawnPosIndex++];
                if (Creature* creature = instance->SummonCreature(creatureEntry, spawnPos, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 3600 * 1000))
                {
                    creature->SetAI(new npc_trial_monster_ai(creature));
                    creature->SetLevel(highestLevelAtStart);
                    if (healthMultiplier != 1.0f)
                    {
                        creature->SetMaxHealth(uint32(creature->GetMaxHealth() * healthMultiplier));
                        creature->SetHealth(creature->GetMaxHealth());
                    }
                    if (aurasToAdd)
                    {
                        for (uint32 auraId : *aurasToAdd)
                            creature->AddAura(auraId, creature);
                    }
                    // OnCreatureCreate will add to activeMonsters
                }
            }
        }
    }

    // --- Player State and Trial Outcome ---
    void HandlePlayerDowned(Player* downedPlayer)
    {
        if (!downedPlayer) return;

        ObjectGuid playerGuid = downedPlayer->GetGUID();
        downedPlayerGuids[playerGuid] = time(nullptr);
        uint32 groupId = downedPlayer->GetGroup() ? downedPlayer->GetGroup()->GetId() : 0;

        sLog->outInfo("sys", "[TrialOfFinality] Player %s (GUID %s, Instance %u) has been downed in wave %d.",
            downedPlayer->GetName().c_str(), playerGuid.ToString().c_str(), instance->GetInstanceId(), currentWave);

        ChatHandler(downedPlayer->GetSession()).SendSysMessage("You have been defeated! You must be resurrected before the wave ends to avoid permanent failure!");
        LogTrialDbEvent(TRIAL_EVENT_PLAYER_DEATH_TOKEN, groupId, downedPlayer, currentWave, highestLevelAtStart, "Player downed, awaiting resurrection or wave end.");

        // Check if this was the last player
        uint32 activePlayers = 0;
        instance->DoForAllPlayers([&](Player* player)
        {
            if (player->IsAlive() && !downedPlayerGuids.count(player->GetGUID()))
                activePlayers++;
        });

        if (activePlayers == 0)
        {
            sLog->outInfo("sys", "[TrialOfFinality] All players in instance %u are downed. Finalizing trial as a failure.", instance->GetInstanceId());
            FinalizeTrialOutcome(false, "All players were defeated.");
        }
    }

    void HandlePlayerResurrect(Player* player)
    {
        if (downedPlayerGuids.erase(player->GetGUID()))
        {
            uint32 groupId = player->GetGroup() ? player->GetGroup()->GetId() : 0;
            sLog->outInfo("sys", "[TrialOfFinality] Player %s (GUID %s, Instance %u) was resurrected during the trial.",
                player->GetName().c_str(), player->GetGUID().ToString().c_str(), instance->GetInstanceId());
            ChatHandler(player->GetSession()).SendSysMessage("You have been resurrected! Your fate is no longer sealed... for now.");
            LogTrialDbEvent(TRIAL_EVENT_PLAYER_RESURRECTED, groupId, player, currentWave, highestLevelAtStart, "Player resurrected mid-wave.");
        }
    }

    void FinalizeTrialOutcome(bool overallSuccess, const std::string& reason)
    {
        uint32 groupId = 0;
        Player* leader = nullptr;
        if (Player* p = instance->GetPlayer(0))
        {
            if (p->GetGroup())
                groupId = p->GetGroup()->GetId();
            leader = p; // Use first player as a proxy for logging if needed
        }

        sLog->outInfo("sys", "[TrialOfFinality] Finalizing trial for instance %u. Overall Success: %s. Reason: %s.",
            instance->GetInstanceId(), (overallSuccess ? "Yes" : "No"), reason.c_str());

        if (!overallSuccess)
        {
            SetBossState(currentWave - 1, FAIL);
            if (!downedPlayerGuids.empty())
            {
                for(const auto& pair : downedPlayerGuids)
                {
                    ObjectGuid playerGuid = pair.first;
                    permanentlyFailedPlayerGuids.insert(playerGuid); // Mark for internal logic
                    Player* downedPlayer = ObjectAccessor::FindPlayer(playerGuid);
                    if (downedPlayer && downedPlayer->GetSession())
                    {
                        if (PermaDeathExemptGMs && downedPlayer->GetSession()->GetSecurity() >= SEC_GAMEMASTER)
                        {
                            sLog->outInfo("sys", "[TrialOfFinality] GM Player %s (GUID %s) is EXEMPT from perma-death.", downedPlayer->GetName().c_str(), playerGuid.ToString().c_str());
                        }
                        else
                        {
                            CharacterDatabase.ExecuteFmt("INSERT INTO character_trial_finality_status (guid, is_perma_failed, last_failed_timestamp) VALUES (%u, 1, NOW()) ON DUPLICATE KEY UPDATE is_perma_failed = 1, last_failed_timestamp = NOW()", playerGuid.GetCounter());
                            sLog->outFatal("[TrialOfFinality] Player %s (GUID %s) PERMANENTLY FAILED due to trial failure: %s.", downedPlayer->GetName().c_str(), playerGuid.ToString().c_str(), reason.c_str());
                            LogTrialDbEvent(TRIAL_EVENT_PERMADEATH_APPLIED, groupId, downedPlayer, currentWave, highestLevelAtStart, "Perma-death DB flag set: " + reason);
                            ChatHandler(downedPlayer->GetSession()).SendSysMessage("The trial has ended in failure. Your fate is sealed.");
                        }
                    }
                    else
                    {
                        CharacterDatabase.ExecuteFmt("INSERT INTO character_trial_finality_status (guid, is_perma_failed, last_failed_timestamp) VALUES (%u, 1, NOW()) ON DUPLICATE KEY UPDATE is_perma_failed = 1, last_failed_timestamp = NOW()", playerGuid.GetCounter());
                        sLog->outFatal("[TrialOfFinality] Offline Player (GUID %s) PERMANENTLY FAILED due to trial failure: %s.", playerGuid.ToString().c_str(), reason.c_str());
                        LogTrialDbEvent(TRIAL_EVENT_PERMADEATH_APPLIED, groupId, nullptr, currentWave, highestLevelAtStart, "Offline Player - Perma-death DB flag set: " + reason);
                    }
                }
            }
            downedPlayerGuids.clear();
            LogTrialDbEvent(TRIAL_EVENT_TRIAL_FAILURE, groupId, leader, currentWave, highestLevelAtStart, reason);
        }
        else
        {
            LogTrialDbEvent(TRIAL_EVENT_TRIAL_SUCCESS, groupId, leader, currentWave, highestLevelAtStart, reason);
            // World announcement and cheering logic could be triggered here if desired
        }
        CleanupTrial(overallSuccess);
    }

    void CleanupTrial(bool success)
    {
        // Despawn any remaining monsters
        for (const auto& monsterGuid : activeMonsters)
            if (Creature* monster = instance->GetCreature(monsterGuid))
                monster->DespawnOrUnsummon();
        activeMonsters.clear();

        // Despawn announcer
        if (!announcerGuid.IsEmpty())
            if (Creature* announcer = instance->GetCreature(announcerGuid))
                announcer->DespawnOrUnsummon();

        // Process all players in the instance
        instance->DoForAllPlayers([this, success](Player* player)
        {
            player->DestroyItemCount(TrialTokenEntry, 1, true, false);
            player->SetDisableXpGain(false, true);

            if (!permanentlyFailedPlayerGuids.count(player->GetGUID()))
            {
                 ChatHandler(player->GetSession()).SendSysMessage("The Trial of Finality has concluded. You are being teleported out.");
                 if (ExitOverrideHearthstone)
                     player->TeleportTo(ExitMapID, ExitTeleportX, ExitTeleportY, ExitTeleportZ, ExitTeleportO);
                 else
                     player->TeleportTo(player->GetBindPoint());
            }
        });

        // Give rewards on success
        if (success)
        {
            instance->DoForAllPlayers([this](Player* player)
            {
                if (permanentlyFailedPlayerGuids.count(player->GetGUID())) return;

                if (GoldReward > 0)
                {
                    player->ModifyMoney(GoldReward);
                    ChatHandler(player->GetSession()).PSendSysMessage("You have been awarded %u gold for your victory!", GoldReward / 10000);
                }
                if (TitleRewardID > 0)
                {
                    if (CharTitlesEntry const* titleEntry = sCharTitlesStore.LookupEntry(TitleRewardID))
                    {
                        player->SetTitle(titleEntry);
                        ChatHandler(player->GetSession()).SendSysMessage("You have been granted a new title!");
                    }
                }
            });
        }

        // If it was a test trial with a temporary group, disband it
        if (isTestTrial)
        {
            if (Player* p = instance->GetPlayer(0))
            {
                if (Group* group = p->GetGroup())
                {
                    sLog->outDetail("[TrialOfFinality] Disbanding temporary test trial group %u for instance %u.", group->GetId(), instance->GetInstanceId());
                    group->Disband();
                }
            }
        }

        // The instance will be destroyed automatically when the last player leaves.
        // No need to manually manage trial info maps anymore.
        sLog->outInfo("sys", "[TrialOfFinality] Cleaned up trial for instance %u.", instance->GetInstanceId());
    }

    void CheckPlayerLocationsAndEnforceBoundaries()
    {
        Position centerPos(ArenaTeleportX, ArenaTeleportY, ArenaTeleportZ, 0.0f);
        uint32 groupId = 0;
        if (Player* p = instance->GetPlayer(0)) { if (p->GetGroup()) groupId = p->GetGroup()->GetId(); }

        instance->DoForAllPlayers([this, &centerPos, groupId](Player* player)
        {
            if (player->IsAlive() && !(GMDebugEnable && player->GetSession()->GetSecurity() >= SEC_GAMEMASTER))
            {
                bool isOutside = player->GetDistance(centerPos) > ArenaRadius;
                if (isOutside)
                {
                    if (playersWarnedForLeavingArena.count(player->GetGUID()))
                    {
                        sLog->outWarn("sys", "[TrialOfFinality] Player %s (Instance %u) left the arena after being warned. Failing the trial.", player->GetName().c_str(), instance->GetInstanceId());
                        std::string reason = player->GetName() + " has fled the Trial of Finality, forfeiting the challenge for the group.";
                        LogTrialDbEvent(TRIAL_EVENT_PLAYER_FORFEIT_ARENA, groupId, player, currentWave, highestLevelAtStart, reason);
                        FinalizeTrialOutcome(false, reason);
                    }
                    else
                    {
                        playersWarnedForLeavingArena.insert(player->GetGUID());
                        ChatHandler(player->GetSession()).SendSysMessage("WARNING: You have left the trial arena! Return immediately or you will forfeit the trial for your entire group!");
                        LogTrialDbEvent(TRIAL_EVENT_PLAYER_WARNED_ARENA_LEAVE, groupId, player, currentWave, highestLevelAtStart, "Player left arena boundary and was warned.");
                    }
                }
            }
        });
    }

    void HandleTrialForfeit(Player* player)
    {
        if (playersWhoVotedForfeit.count(player->GetGUID()))
        {
            ChatHandler(player->GetSession()).SendSysMessage("You have already voted to forfeit.");
            return;
        }

        uint32 activePlayers = 0;
        instance->DoForAllPlayers([&](Player* p) {
            if (p->IsAlive() && !permanentlyFailedPlayerGuids.count(p->GetGUID()))
                activePlayers++;
        });

        if (activePlayers == 0)
        {
            ChatHandler(player->GetSession()).SendSysMessage("There are no active players to vote.");
            return;
        }

        uint32 groupId = player->GetGroup() ? player->GetGroup()->GetId() : 0;

        if (!forfeitVoteInProgress)
        {
            forfeitVoteInProgress = true;
            forfeitVoteStartTime = time(nullptr);
            playersWhoVotedForfeit.insert(player->GetGUID());
            std::string msg = player->GetName() + " has initiated a vote to forfeit! Type `/trialforfeit` to agree. (1/" + std::to_string(activePlayers) + " votes)";
            LogTrialDbEvent(TRIAL_EVENT_FORFEIT_VOTE_START, groupId, player, currentWave, highestLevelAtStart, "Forfeit vote started.");
            DoSendNotifyToInstance(msg.c_str());
        }
        else
        {
            playersWhoVotedForfeit.insert(player->GetGUID());
            std::string msg = player->GetName() + " has also voted to forfeit. (" + std::to_string(playersWhoVotedForfeit.size()) + "/" + std::to_string(activePlayers) + " votes)";
            DoSendNotifyToInstance(msg.c_str());
        }

        if (playersWhoVotedForfeit.size() >= activePlayers)
        {
            std::string reason = "The group has unanimously voted to forfeit the trial.";
            LogTrialDbEvent(TRIAL_EVENT_FORFEIT_VOTE_SUCCESS, groupId, player, currentWave, highestLevelAtStart, reason);
            CleanupTrial(false);
        }
    }
};

// --- Instance Script Loader ---
// Registers our custom instance script with the server.
class instance_trial_of_finality_loader : public InstanceScriptLoader
{
    public:
        instance_trial_of_finality_loader() : InstanceScriptLoader("instance_trial_of_finality") { }

        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_trial_of_finality(map);
        }
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
        case TRIAL_EVENT_FORFEIT_VOTE_START: eventTypeStr = "FORFEIT_VOTE_START"; break;
        case TRIAL_EVENT_FORFEIT_VOTE_CANCEL: eventTypeStr = "FORFEIT_VOTE_CANCEL"; break;
        case TRIAL_EVENT_FORFEIT_VOTE_SUCCESS: eventTypeStr = "FORFEIT_VOTE_SUCCESS"; break;
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
    sLog->outMessage("sys", LOG_LEVEL_INFO, "%s", slog_message.str().c_str());

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
uint32 FateweaverArithosDisplayID = 0;
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

struct PendingSecondCheer {
    ObjectGuid npcGuid;
    time_t cheerTime;
};

// --- Pre-Trial Data Structures and Manager ---
// This manager is now a simple data-passing utility to transfer information
// from the outer world script (NPC interaction) into the newly created instance.
struct PreTrialData
{
    uint8 highestLevel;
    bool isTestTrial = false;
};

class TrialManager
{
public:
    static TrialManager* instance() { static TrialManager instance; return &instance; }

    // Caches the necessary pre-trial data for a group.
    void PrepareForInstance(Group* group, uint8 highestLevel, bool isTest = false)
    {
        if (!group) return;
        m_preTrialData[group->GetId()] = { highestLevel, isTest };
        sLog->outDetail("[TrialOfFinality] Preparing group %u for instance, highest level: %u, isTest: %d", group->GetId(), highestLevel, isTest);
    }

    // Retrieves the cached data; intended to be called from the InstanceScript.
    PreTrialData* GetPreTrialData(uint32 groupId)
    {
        auto it = m_preTrialData.find(groupId);
        if (it != m_preTrialData.end())
            return &it->second;
        return nullptr;
    }

    // Cleans up the cached data once it has been consumed by the InstanceScript.
    void CleanupPreTrialData(uint32 groupId)
    {
        m_preTrialData.erase(groupId);
    }

    static bool ValidateGroupForTrial(Player* leader, Creature* trialNpc);

private:
    TrialManager() {}
    ~TrialManager() {}
    TrialManager(const TrialManager&) = delete;
    TrialManager& operator=(const TrialManager&) = delete;
    std::map<uint32, PreTrialData> m_preTrialData;
};

// --- TrialManager Method Implementations ---
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
                Player* failedPlayer = ObjectAccessor::FindPlayer(ObjectGuid(HighGuid::Player, failedGuid));
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

// --- AI for Trial Monsters ---
struct npc_trial_monster_ai : public ScriptedAI
{
    npc_trial_monster_ai(Creature* creature) : ScriptedAI(creature) {}

    void JustDied(Unit* /*killer*/) override
    {
        if (auto* instance = (instance_trial_of_finality*)me->GetInstanceScript())
        {
            instance->HandleMonsterKilled(me);
        }
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
        if (FateweaverArithosDisplayID != 0)
        {
            creature->SetDisplayId(FateweaverArithosDisplayID);
        }

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

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
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
                if (TrialManager::ValidateGroupForTrial(player, creature))
                {
                    // Calculate highest level before creating the instance
                    uint8 highestLevel = 0;
                    if (Group* group = player->GetGroup())
                    {
                        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
                        {
                            if (Player* member = itr->GetSource())
                            {
                                if (member->GetSession() && member->getLevel() > highestLevel)
                                {
                                    highestLevel = member->getLevel();
                                }
                            }
                        }
                    }

                    if (highestLevel == 0)
                    {
                        sLog->outError("sys", "[TrialOfFinality] Could not start trial for group %u, unable to determine highest level.", player->GetGroup()->GetId());
                        ChatHandler(player->GetSession()).SendSysMessage("Could not start the trial. Unable to determine the group's highest level.");
                        return true;
                    }

                    // Cache the data for the instance script to pick up
                    TrialManager::instance()->PrepareForInstance(player->GetGroup(), highestLevel);

                    // Create a new private instance for the group
                    InstanceMap* instanceMap = sMapMgr->CreateNewInstance(ArenaMapID, player, INSTANCE_DIFFICULTY_NORMAL);
                    if (!instanceMap)
                    {
                        sLog->outError("sys", "[TrialOfFinality] Could not create instance map %u for group %u.", ArenaMapID, player->GetGroup()->GetId());
                        ChatHandler(player->GetSession()).SendSysMessage("An error occurred while preparing the trial arena. Please try again later.");
                        return true;
                    }

                    // Teleport all group members to the new instance
                    for (GroupReference* itr = player->GetGroup()->GetFirstMember(); itr != nullptr; itr = itr->next())
                    {
                        if (Player* member = itr->GetSource())
                        {
                            if (member->GetSession())
                            {
                                member->TeleportTo(ArenaMapID, ArenaTeleportX, ArenaTeleportY, ArenaTeleportZ, ArenaTeleportO, 0, instanceMap->GetInstanceId());
                            }
                        }
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

class ModPlayerScript : public PlayerScript
{
public:
    ModPlayerScript() : PlayerScript("ModTrialOfFinalityPlayerScript") {}

    void OnPlayerKilledByCreature(Creature* /*killer*/, Player* killed) override
    {
        if (!ModuleEnabled) return;
        if (auto* instance = (instance_trial_of_finality*)killed->GetInstanceScript())
        {
            instance->HandlePlayerDowned(killed);
        }
    }

    void OnPlayerResurrect(Player* player, float /*percentHealth*/, float /*percentMana*/) override
    {
        if (!ModuleEnabled) return;
        if (auto* instance = (instance_trial_of_finality*)player->GetInstanceScript())
        {
            instance->HandlePlayerResurrect(player);
        }
    }

    void OnLogin(Player* player) override {
        if (!ModuleEnabled) return;

        // Remove stray trial tokens if player logs in and is not in an active trial context
        if (player->HasItemCount(TrialTokenEntry, 1, true)) {
            // This check is now more robust. We check if the player is in ANY instance map.
            // A player with a token should only ever be inside the trial instance.
            if (!player->GetMap()->IsDungeon()) {
                player->DestroyItemCount(TrialTokenEntry, 1, true, false);
                sLog->outDetail("[TrialOfFinality] Player %s (GUID %u) logged in outside instance with Trial Token; token removed.",
                    player->GetName().c_str(), player->GetGUID().GetCounter());
                LogTrialDbEvent(TRIAL_EVENT_STRAY_TOKEN_REMOVED, 0, player, 0, player->getLevel(), "Logged in outside instance with token.");
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
        sLog->outInfo("sys", "Loading Trial of Finality module configuration...");
        ModuleEnabled = sConfigMgr->GetOption<bool>("TrialOfFinality.Enable", false);
        if (!ModuleEnabled) { sLog->outInfo("sys", "Trial of Finality: Module disabled by configuration."); return; }
        FateweaverArithosEntry = sConfigMgr->GetOption<uint32>("TrialOfFinality.FateweaverArithos.EntryID", 0);
        FateweaverArithosDisplayID = sConfigMgr->GetOption<uint32>("TrialOfFinality.FateweaverArithos.DisplayID", 0);
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
            sLog->outInfo("sys", "[TrialOfFinality] Caching cheering NPCs...");
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
                sLog->outInfo("sys", "[TrialOfFinality] Cached %u cheering NPCs in %lu zones.", count, s_cheeringNpcCacheByZone.size());
                for (const auto& pair : s_cheeringNpcCacheByZone)
                {
                    sLog->outDetail("[TrialOfFinality] Zone %u: Cached %lu NPCs.", pair.first, pair.second.size());
                }
            }
            else
            {
                sLog->outInfo("sys", "[TrialOfFinality] No cheering NPCs found to cache or database error.");
            }
        }
        else if (!CheeringNpcsEnable)
        {
            sLog->outInfo("sys", "[TrialOfFinality] Cheering NPCs disabled, cache not populated.");
        }
        else if (CheeringNpcCityZoneIDs.empty())
        {
            sLog->outInfo("sys", "[TrialOfFinality] No City Zone IDs configured for cheering NPCs, cache not populated.");
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
        sLog->outInfo("sys", "Trial of Finality: Configuration loaded. Module enabled.");
        if (reload) { sLog->outInfo("sys", "Trial of Finality: Configuration reloaded. Consider restarting for full effect if scripts were already registered."); }
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

    static bool ChatCommand_trial_test(ChatHandler* handler, const char* /*args*/)
    {
        Player* gmPlayer = handler->GetPlayer();
        if (!gmPlayer)
        {
            handler->SendSysMessage("You must be a player to use this command.");
            return false;
        }

        if (!ModuleEnabled)
        {
            ChatHandler(gmPlayer->GetSession()).SendSysMessage("The Trial of Finality module is currently disabled.");
            return false;
        }

        if (gmPlayer->GetGroup())
        {
            ChatHandler(gmPlayer->GetSession()).SendSysMessage("You cannot start a test trial while in a group.");
            return false;
        }

        // Create a temporary, virtual group for the solo GM
        Group* tempGroup = new Group;
        tempGroup->Create(gmPlayer->GetGUID());
        sGroupMgr->AddGroup(tempGroup);
        gmPlayer->SetGroup(tempGroup, GRP_STATUS_DEFAULT);

        sLog->outInfo("sys", "[TrialOfFinality] GM %s starting a solo test trial in temporary group %u.", gmPlayer->GetName().c_str(), tempGroup->GetId());

        TrialManager::instance()->PrepareForInstance(tempGroup, gmPlayer->getLevel(), true);

        InstanceMap* instanceMap = sMapMgr->CreateNewInstance(ArenaMapID, gmPlayer, INSTANCE_DIFFICULTY_NORMAL);
        if (!instanceMap)
        {
            sLog->outError("sys", "[TrialOfFinality] Could not create instance map %u for GM test trial.", ArenaMapID);
            ChatHandler(gmPlayer->GetSession()).SendSysMessage("An error occurred while preparing the trial arena.");
            tempGroup->Disband(); // Clean up the temporary group
            return true;
        }

        gmPlayer->TeleportTo(ArenaMapID, ArenaTeleportX, ArenaTeleportY, ArenaTeleportZ, ArenaTeleportO, 0, instanceMap->GetInstanceId());
        handler->SendSysMessage("Test trial initiated successfully. Teleporting to instance.");
        return true;
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
            playerGuid = sCharacterCache->GetCharacterGuidByName(charName);
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
};

// --- Player Command Scripts ---
class trial_player_commandscript : public CommandScript
{
public:
    trial_player_commandscript() : CommandScript("trial_player_commandscript") { }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> commandTable;
        if (commandTable.empty() && ForfeitEnable)
        {
            commandTable.push_back({ "trialforfeit", SEC_PLAYER, true, &HandleTrialForfeitCommand, "Votes to forfeit the current Trial of Finality." });
            commandTable.push_back({ "tf",           SEC_PLAYER, true, &HandleTrialForfeitCommand, "Alias for /trialforfeit." });
        }
        return commandTable;
    }

    static bool HandleTrialForfeitCommand(ChatHandler* handler, const char* /*args*/)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendSysMessage("This command can only be used by a player.");
            return false;
        }

        if (!ModuleEnabled)
        {
            ChatHandler(player->GetSession()).SendSysMessage("The Trial of Finality module is currently disabled.");
            return false;
        }

        if (auto* instance = (instance_trial_of_finality*)player->GetInstanceScript())
        {
            sLog->outDetail("[TrialOfFinality] Player %s (GUID %u) is attempting to use /trialforfeit command inside an instance.", player->GetName().c_str(), player->GetGUID().GetCounter());
            instance->HandleTrialForfeit(player);
        }
        else
        {
            ChatHandler(player->GetSession()).SendSysMessage("You can only use this command inside the Trial of Finality.");
        }

        return true;
    }
};

} // namespace ModTrialOfFinality

void Addmod_trial_of_finality_Scripts() {
    new ModTrialOfFinality::instance_trial_of_finality_loader();
    new ModTrialOfFinality::npc_trial_announcer();
    new ModTrialOfFinality::npc_fateweaver_arithos();
    new ModTrialOfFinality::ModPlayerScript();
    new ModTrialOfFinality::ModServerScript();
    new ModTrialOfFinality::trial_commandscript(); // GM commands
    new ModTrialOfFinality::trial_player_commandscript(); // Player commands
}
extern "C" void Addmod_trial_of_finality() { Addmod_trial_of_finality_Scripts(); }