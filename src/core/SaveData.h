#pragma once

#include "core/GameState.h"

#include <optional>

namespace rocket {

struct SaveData {
    int version = 4;
    std::uint64_t seed = 0xC0DEC0FFEEULL;
    double credits = 100.0;
    int destinationIndex = 0;
    int frontierReadiness = 0;
    bool refitEntitled = false;
    int shipDamage = 0;
    std::string frameId = "pathfinder";
    int offerRerollsThisExpedition = 0;
    int repairOpsThisExpedition = 0;
    int trainingOpsThisExpedition = 0;
    int restOpsThisExpedition = 0;
    int shallowRecoveryStreak = 0;
    int cleanShallowRecoveryStreak = 0;
    double nextLaunchFuelBoost = 0.0;
    double nextLaunchSpeedBoost = 0.0;
    Screen screen = Screen::Hangar;
    CampaignMilestone campaignMilestone = CampaignMilestone::SolarTutorial;
    GameChapter chapter = GameChapter::ProvingGround;
    ArkState ark;
    NavigationState navigation;
    StoryBriefingState storyBriefing;
    std::vector<std::string> acknowledgedActivityBriefingIds;
    bool campaignIntroductionAcknowledged = false;
    bool straylightDiscoveryAcknowledged = false;
    std::vector<std::string> inventoryModuleIds;
    std::vector<std::string> equippedModuleIds;
    std::vector<std::string> surfaceUpgradeIds;
    std::vector<std::string> crewUpgradeIds;
    std::vector<std::string> offerModuleIds;
    std::vector<std::string> offerCrewUpgradeIds;
    std::vector<std::string> researchProjectIds;
    ArrivalOpsState arrivalOps;
    SurfaceExpeditionState surfaceExpedition;
    MiningRunState mining;
    std::vector<std::string> unlockKeys;
    int blueprintProgress = 0;
    MaterialInventory materials;
    std::vector<std::string> ownedModuleIds;
    std::vector<std::string> defaultEquippedModuleIds;
    int droneBaySlots = 0;
    std::vector<std::string> ownedDroneIds;
    std::vector<std::string> equippedDroneIds;
    std::vector<DroneUpgradeRecord> droneUpgrades;
    int prospectorCommonOreRecovered = 0;
    std::vector<ArtifactRecord> artifacts;
    std::array<MiningFirstClearProgress, miningFirstClearProgressCount> miningFirstClearProgress {};
    std::vector<MiningStorySiteProgress> miningStorySites;
    int furthestTier = 0;
    int shipsLost = 0;
    int astronautsLost = 0;
    double closestSurvivalMargin = 0.0;
    double closestSurvivalBurn = 0.0;
    double closestSurvivalFailurePoint = 0.0;
    double maxBurnDepth = 0.0;
    double maxPeakWarning = 0.0;
    double maxPeakAbortRisk = 0.0;
    double bestCreditDelta = 0.0;
    double worstCreditDelta = 0.0;
    int totalFlybyMisses = 0;
    int totalFlybyGoods = 0;
    int totalFlybyPerfects = 0;
    std::vector<std::string> destinationHistoryIds;
    std::vector<int> destinationAttempts;
    std::vector<int> destinationSuccesses;
    std::vector<int> destinationFlybys;
    std::vector<int> destinationOrbits;
    std::vector<int> destinationLandings;
    std::vector<std::string> memorials;
    std::vector<std::string> famousLaunches;
    std::vector<Astronaut> crew;
};

SaveData captureSaveData(const GameState& state);
void restoreSaveData(GameState& state, const ContentCatalog& catalog, const SaveData& save);

std::string serializeSaveData(const SaveData& save);
std::optional<SaveData> deserializeSaveData(std::string_view text);

} // namespace rocket
