#pragma once

#include "core/ContentIds.h"
#include "core/GameText.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rocket {

enum class Screen {
    Hangar,
    Launch,
    Results,
    ArrivalFanfare,
    ArrivalOps,
    Research,
    SurfaceExpedition,
    Mining,
    Upgrade,
    Legacy,
    DroneOps,
    Navigation
};

enum class CampaignMilestone {
    SolarTutorial,
    ArkDiscovered,
    FirstArkJumpReady,
    FirstArkJumpComplete,
    GravityWellDisaster,
    HostileSystemStranded,
    ArkRepairing
};

enum class ArkCondition {
    NotFound,
    DerelictOperable,
    InFlight,
    DamagedStranded,
    Repairing
};

enum class SlotType {
    Engine,
    Fuel,
    Hull,
    Cooling,
    Sensors,
    Escape
};

enum class Rarity {
    Common,
    Uncommon,
    Rare,
    Prototype
};

enum class CrewStatus {
    Active,
    Injured,
    Dead
};

enum class SurfaceSiteProfile {
    SurveyBasin,
    OreShelf,
    FractureField
};

enum class SurfaceUpgradeCategory {
    Drill,
    Scanner,
    Drone
};

enum class MiniDroneRole {
    Mining,
    Resource,
    Survey,
    Stabilizer,
    Attack,
    Defense
};

enum class MiningCellMaterial {
    Empty,
    Regolith,
    HardRock,
    CommonOre,
    RareOre,
    ExoticVein,
    ArtifactCache,
    HazardPocket,
    Bedrock
};

enum class LaunchResultType {
    None,
    SafeEject,
    MissionComplete,
    Destroyed
};

enum class RecoveryMethod {
    None,
    ReturnHome,
    ManualEject,
    TransferArrival
};

struct ModuleStats {
    double thrust = 0.0;
    double fuel = 0.0;
    double hull = 0.0;
    double cooling = 0.0;
    double sensors = 0.0;
    double escape = 0.0;
    double pressure = 0.0;
    double volatility = 0.0;
    double payout = 0.0;
    double repair = 0.0;
    double miningPower = 0.0;
    double miningYield = 0.0;
    double miningCooling = 0.0;
    double miningDurability = 0.0;
    double miningWidth = 0.0;
    double miningDepth = 0.0;
};

struct CrewUpgradeStats {
    int trainingGain = 0;
    int trainingStressRelief = 0;
    int restStressBonus = 0;
    int launchStressRelief = 0;
    double traitModifier = 0.0;
};

struct SurfaceUpgradeStats {
    double drillPower = 0.0;
    double drillCooling = 0.0;
    double drillDurability = 0.0;
    double hardRockBounceRelief = 0.0;
    double oreYieldChance = 0.0;
    double scannerRadius = 0.0;
    double hazardRelief = 0.0;
    double droneSpeed = 0.0;
    double oxygenSeconds = 0.0;
    double extractionRiskRelief = 0.0;
};

struct MiniDroneStats {
    double passiveMiningRate = 0.0;
    double oxygenSeconds = 0.0;
    double scannerRadius = 0.0;
    double drillIntegrityRelief = 0.0;
    double hardRockBounceRelief = 0.0;
    double extractionRiskRelief = 0.0;
    double enemyEncounterRelief = 0.0;
};

struct MaterialInventory {
    int common = 0;
    int rare = 0;
    int exotic = 0;
};

struct ShipModule {
    std::string id;
    std::string name;
    SlotType slot = SlotType::Engine;
    Rarity rarity = Rarity::Common;
    ModuleStats stats;
    MaterialInventory materialCost;
    int durability = 100;
    std::string unlockKey = content::unlock::starter;
    std::vector<std::string> tags;
};

struct CrewUpgrade {
    std::string id;
    std::string name;
    std::string description;
    Rarity rarity = Rarity::Common;
    CrewUpgradeStats stats;
    std::string unlockKey = content::unlock::starter;
    std::vector<std::string> tags;
};

struct SurfaceUpgrade {
    std::string id;
    std::string name;
    std::string description;
    Rarity rarity = Rarity::Common;
    SurfaceUpgradeCategory category = SurfaceUpgradeCategory::Drill;
    SurfaceUpgradeStats stats;
    std::vector<std::string> tags;
};

struct MiniDrone {
    std::string id;
    std::string name;
    std::string description;
    Rarity rarity = Rarity::Common;
    MiniDroneRole role = MiniDroneRole::Mining;
    MiniDroneStats stats;
    std::string unlockKey = content::unlock::starter;
    std::vector<std::string> tags;
};

struct ArtifactRecord {
    std::string id;
    std::string originDestinationId;
    bool identified = false;
};

struct ResearchProject {
    std::string id;
    std::string name;
    std::string description;
    Rarity rarity = Rarity::Common;
    int requiredDestinationTier = 2;
    int blueprintGain = 0;
    MaterialInventory materialCost;
    std::string unlockKey = content::unlock::starter;
    std::string rewardUnlockKey;
    std::vector<std::string> tags;
};

struct ShipFrame {
    std::string id;
    std::string name;
    std::vector<SlotType> slots;
    ModuleStats baseStats;
    int maxDamage = 100;
};

struct Astronaut {
    std::string id;
    std::string name;
    std::string background;
    std::string trait;
    int training = 0;
    int stress = 0;
    CrewStatus status = CrewStatus::Active;
};

struct Destination {
    std::string id;
    std::string name;
    int tier = 0;
    double targetMultiplier = 1.5;
    double minCrashMultiplier = 1.05;
    double maxCrashMultiplier = 2.0;
    double baseReward = 10.0;
    double hazard = 1.0;
    std::string unlockKey = content::unlock::starter;
};

struct LaunchConfig {
    std::string destinationId;
    double burnGoalMultiplier = 1.5;
    bool frontierTransfer = false;
    std::string astronautId;
    std::string frameId;
    std::vector<std::string> equippedModuleIds;
};

struct TelemetryEvent {
    double multiplier = 1.0;
    double heat = 0.0;
    double pressure = 0.0;
    double vibration = 0.0;
    double fuelMix = 0.0;
    double guidance = 0.0;
    double abortRisk = 0.0;
    double stress = 0.0;
    double warning = 0.0;
    std::string message;
};

struct LaunchOutcome {
    LaunchResultType type = LaunchResultType::None;
    RecoveryMethod recoveryMethod = RecoveryMethod::None;
    std::string destinationId;
    std::string assignedAstronautId;
    bool frontierTransfer = false;
    double crashMultiplier = 1.0;
    double ejectMultiplier = 1.0;
    double payout = 0.0;
    double recoveryCost = 0.0;
    int shipDamage = 0;
    bool crewKilled = false;
    bool crewInjured = false;
    std::string moduleDestroyedId;
    int blueprintGain = 0;
    double peakWarning = 0.0;
    double peakAbortRisk = 0.0;
    std::vector<TelemetryEvent> telemetry;
};

struct ArkState {
    ArkCondition condition = ArkCondition::NotFound;
    int fuelReserve = 0;
    int hullDamage = 0;
    std::vector<std::string> repairModuleIds;
    bool firstJumpComplete = false;
    bool gravityWellDisaster = false;
};

struct NavigationState {
    std::string currentSystemId = "solar_system";
    std::string arkLocationId;
    std::string selectedDestinationId;
    std::vector<std::string> discoveredDestinationIds;
};

struct MetaProgress {
    CampaignMilestone campaignMilestone = CampaignMilestone::SolarTutorial;
    ArkState ark;
    NavigationState navigation;
    std::vector<std::string> unlockKeys;
    int blueprintProgress = 0;
    MaterialInventory materials;
    std::vector<std::string> ownedModuleIds;
    std::vector<std::string> defaultEquippedModuleIds;
    int droneBaySlots = 0;
    std::vector<std::string> ownedDroneIds;
    std::vector<std::string> equippedDroneIds;
    std::vector<ArtifactRecord> artifacts;
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
    std::vector<int> destinationAttempts;
    std::vector<int> destinationSuccesses;
    std::vector<int> destinationFlybys;
    std::vector<int> destinationOrbits;
    std::vector<int> destinationLandings;
    std::vector<std::string> memorials;
    std::vector<std::string> famousLaunches;
};

struct ArrivalOpsState {
    bool active = false;
    std::string destinationId;
};

struct SurfaceExpeditionState {
    bool active = false;
    std::string destinationId;
    SurfaceSiteProfile siteProfile = SurfaceSiteProfile::SurveyBasin;
    int supply = 0;
    int cargo = 0;
    double hazard = 0.0;
    int depth = 0;
    MaterialInventory temporaryMaterials;
    std::vector<ArtifactRecord> temporaryArtifacts;
    std::vector<std::string> logEntries;
    bool enemyEncountersEnabled = false;
    std::array<std::string, 3> surfaceUpgradeOfferIds {};
    bool surfaceUpgradeOfferAvailable = false;
    int surfaceUpgradeOffersSeen = 0;
};

struct MiningCell {
    MiningCellMaterial material = MiningCellMaterial::Empty;
    double maxToughness = 0.0;
    double remainingToughness = 0.0;
    bool revealed = false;
    bool hazard = false;
};

struct MiningTerrain {
    int width = 64;
    int height = 40;
    int depthZone = 0;
    std::vector<MiningCell> cells;
    std::vector<std::uint8_t> dirtyChunks;
};

struct MiningRunState {
    bool active = false;
    std::string destinationId;
    SurfaceSiteProfile siteProfile = SurfaceSiteProfile::SurveyBasin;
    double elapsedSeconds = 0.0;
    double oxygenSeconds = 180.0;
    double droneX = 32.0;
    double droneY = 4.0;
    double moveX = 0.0;
    double moveY = 0.0;
    double aimX = 32.0;
    double aimY = 5.0;
    double aimDirX = 0.0;
    double aimDirY = 1.0;
    bool drilling = false;
    bool failurePending = false;
    double failureSeconds = 0.0;
    std::string failureMessage;
    double drillHeat = 0.0;
    double drillIntegrity = 1.0;
    double contactIntensity = 0.0;
    double recoilX = 0.0;
    double recoilY = 0.0;
    double contactBounce = 0.0;
    double contactBounceVelocity = 0.0;
    double contactBounceCooldown = 0.0;
    double scannerPulseSeconds = 0.0;
    int depthZone = 0;
    int cargo = 0;
    MaterialInventory temporaryMaterials;
    std::vector<ArtifactRecord> temporaryArtifacts;
    double hazardDelta = 0.0;
    int passiveDroneYield = 0;
    int cellsBroken = 0;
    int targetCellX = -1;
    int targetCellY = -1;
    double targetTipX = 32.0;
    double targetTipY = 5.0;
    MiningCellMaterial targetMaterial = MiningCellMaterial::Empty;
    double targetRemainingToughness = 0.0;
    double targetMaxToughness = 0.0;
    MiningTerrain terrain;
};

struct RunState {
    bool active = true;
    int destinationIndex = 0;
    int frontierReadiness = 0;
    int shipDamage = 0;
    double credits = 100.0;
    std::string frameId;
    std::vector<std::string> inventoryModuleIds;
    std::vector<std::string> equippedModuleIds;
    std::vector<std::string> surfaceUpgradeIds;
    std::vector<std::string> crewUpgradeIds;
    std::vector<Astronaut> crew;
    std::array<std::string, 3> offerModuleIds {};
    std::array<std::string, 3> offerCrewUpgradeIds {};
    std::array<std::string, 3> researchProjectIds {};
    ArrivalOpsState arrivalOps;
    SurfaceExpeditionState surfaceExpedition;
    MiningRunState mining;
    int launchesThisExpedition = 0;
    int offerRerollsThisExpedition = 0;
    int repairOpsThisExpedition = 0;
    int trainingOpsThisExpedition = 0;
    int restOpsThisExpedition = 0;
    int shallowRecoveryStreak = 0;
    int cleanShallowRecoveryStreak = 0;
};

struct GameState {
    Screen screen = Screen::Hangar;
    std::uint64_t seed = 0xC0DEC0FFEEULL;
    RunState run;
    MetaProgress meta;
    LaunchConfig launchConfig;
    LaunchOutcome lastOutcome;
    std::string statusLine = std::string(text::status::welcome);
};

std::string_view toString(SlotType slot);
std::string_view toString(Rarity rarity);
std::string_view toString(SurfaceUpgradeCategory category);
std::string_view toString(MiniDroneRole role);
std::string_view toString(CrewStatus status);
std::string_view toString(LaunchResultType result);
std::string_view toString(RecoveryMethod method);
std::string_view toString(CampaignMilestone milestone);
std::string_view toString(ArkCondition condition);

ModuleStats operator+(ModuleStats lhs, const ModuleStats& rhs);
ModuleStats& operator+=(ModuleStats& lhs, const ModuleStats& rhs);

} // namespace rocket
