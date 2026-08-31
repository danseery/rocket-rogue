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
    Flyby,
    Orbit,
    Research,
    SurfaceExpedition,
    SurfaceUpgrade,
    SurfaceScan,
    SurfacePush,
    Mining,
    Upgrade,
    Legacy,
    DroneOps,
    Navigation,
    StoryBriefing
};

enum class StoryBriefingId {
    None,
    CampaignIntroduction,
    StraylightDiscovery
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

enum class GameChapter {
    ProvingGround = 1,
    LunarProgram = 2,
    RedFrontier = 3,
    Breakthrough = 4,
    Straylight = 5,
    Arkfall = 6,
    LastCampfire = 7,
    VoidCompass = 8,
    Ouroboros = 9,
    Ascent = 10
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

enum class RefitTrack {
    None,
    Reach,
    Control,
    Recovery
};

enum class LaunchUpgradeKind {
    None,
    FuelTanks,
    FlightControls,
    Cooling,
    Hull
};

struct LaunchUpgradeRanks {
    int fuelTanks = 0;
    int flightControls = 0;
    int cooling = 0;
    int hull = 0;
};

enum class SurfaceDepthUpgradeKind {
    None,
    SurveyArray,
    BoreSystem
};

struct SurfaceDepthUpgradeRanks {
    int surveyArray = 0;
    int boreSystem = 0;
};

struct RigFuelLoopRank {
    int rank = 0;
};

struct SurfaceRefitPurchaseState {
    bool surveyArray = false;
    bool boreSystem = false;
    bool rigFuelLoop = false;
};

enum class LaunchTrainingStage {
    FuelCalibration,
    FlightControlsCalibration,
    MoonTransfer,
    ThermalManagement,
    MarsTransfer,
    HullIntegrity,
    JupiterTransfer,
    Complete
};

struct LaunchLessonState {
    LaunchTrainingStage stage = LaunchTrainingStage::FuelCalibration;
};

enum class LaunchMissionKind {
    Standard,
    FuelCalibration,
    FlightControlsCalibration,
    ThermalManagement,
    AsteroidBelt
};

enum class FuelSurveyReturnTiming {
    Unqualified,
    Timely,
    Late
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

enum class RunUpgradeKind {
    Rig,
    DroneRank,
    DroneGraft,
    Synergy
};

enum class MiniDroneRole {
    Mining,
    Resource,
    Survey,
    Hazard,
    Attack,
    Defense
};

enum class MiniDroneSignatureKind {
    None,
    SentryKillbox,
    ExcavationStorm,
    ContainmentRig,
    RelicPathfinder,
    FullSpectrumSwarm
};

// Temporary per-expedition module grafted onto an equipped support-drone frame.
enum class DroneModuleKind {
    None,
    CombatDrill,
    DrillGuard,
    PulseStrike,
    SpectrumFilter,
    OreRelay,
    TreasurePing,
    ContainmentShell,
    ReclamationLoop,
    TargetedAssault,
    PenetratingImpact,
    RetributionArc,
    HazardScreen
};

struct DroneModuleDefinition {
    std::string id;
    std::string name;
    MiniDroneRole hostRole = MiniDroneRole::Mining;
    MiniDroneRole secondaryRole = MiniDroneRole::Mining;
    DroneModuleKind kind = DroneModuleKind::None;
    std::string unlockKey = content::unlock::starter;
    Rarity rarity = Rarity::Uncommon;
};

struct DroneFrameModuleAssignment {
    int equippedFrame = -1;
    std::string primaryDroneId;
    DroneModuleKind module = DroneModuleKind::None;
};

struct TreasureMark {
    int x = -1;
    int y = -1;
    int multiplier = 2;
};

struct DroneModuleRuntimeState {
    int equippedFrame = -1;
    double retributionCooldownSeconds = 0.0;
    double reclamationOxygenRecovered = 0.0;
    double reclamationFuelRecovered = 0.0;
    std::vector<std::pair<int, double>> combatDrillEnemyCooldowns;
};

struct RunRigUpgradeRank {
    std::string upgradeId;
    int rank = 0;
};

struct RunDroneRank {
    std::string droneId;
    int rank = 1;
};

struct RunUpgradeOffer {
    RunUpgradeKind kind = RunUpgradeKind::Rig;
    std::string definitionId;
    int targetRank = 0;
    int slotIndex = -1;
};

enum class MiningMiniDroneBehavior {
    Following,
    Traveling,
    Working,
    Returning,
    Engaging,
    Guarding,
    Scouting,
    Docked,
    // Logistics drones can make a deterministic off-screen run through a
    // known shaft while the player remains on another depth layer.
    DeliveringToShip,
    ReturningFromShip,
    // A Prospector that cannot find a traversable route back to its anchor
    // takes the same bounded logistics transit rather than remaining stranded.
    RecoveringToRig
};

enum class MiningOperatorMode {
    Rig,
    Jetpack
};

enum class MiningAnchorTarget {
    ControlledActor,
    Rig,
    Operator
};

enum class MiningActorIdentity {
    Rig,
    Operator
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
    Bedrock,
    // Appended to preserve the serialized values of all existing materials.
    FuelPocket,
    OxygenPocket
};

inline constexpr std::size_t miningCellMaterialCount =
    static_cast<std::size_t>(MiningCellMaterial::OxygenPocket) + 1U;

enum class MiningPickupKind {
    CommonOre,
    RareOre,
    ExoticOre,
    Fuel,
    Oxygen
};

enum class MiningCellFeature {
    None,
    MainTunnel,
    BranchTunnel,
    EncounterZone,
    TreasureVault,
    MinibossLair,
    HiveNest,
    OrganicBurrow,
    BossChamber,
    // Appended so existing serialized feature values remain stable.
    SwarmArena
};

enum class MiningEnemyType {
    None,
    Ant,
    Flying,
    Beetle,
    Elemental,
    Mammal,
    Spawner
};

enum class MiningElementalAffinity {
    None,
    Thermal,
    Cryo,
    Radiation,
    Toxic
};

// Post-solar systems are navigation/story containers. Mineable presentation
// belongs to the generated planet or moon selected inside that system.
enum class PostSolarBodyKind {
    Terrestrial,
    Moon,
    Giant,
    MinorBody
};

struct PostSolarBodyProfile {
    std::string id;
    std::string name;
    std::string parentId;
    PostSolarBodyKind kind = PostSolarBodyKind::Terrestrial;
    int visualArchetype = 1;
    std::string surfaceGeologyId;
    std::string deepGeologyId;
    MiningElementalAffinity hazardBias = MiningElementalAffinity::None;
    std::uint64_t seed = 0;
    bool mineable = true;
};

struct PostSolarSystemRoster {
    std::string systemId;
    int generatorVersion = 1;
    std::uint64_t seed = 0;
    std::string primaryBodyId;
    std::vector<PostSolarBodyProfile> bodies;
};

// Site-wide enemy art direction. Ordinary enemies use this cosmetically;
// Elementals and true elites map it to the existing affinity mechanics.
enum class MiningEnemyTheme {
    Neutral,
    Lava,
    Ice,
    Radioactive,
    Toxic
};

enum class MiningAct {
    ActOne = 1,
    ActTwo = 2,
    ActThree = 3
};

enum class MiningProgressionBand {
    Learn,
    Combine,
    Pressure,
    Mastery
};

enum class MiningGateType {
    None,
    HazardCocoon,
    EnemySealedChamber,
    SurveyTriangulation,
    FragileExcavation,
    HeavyTow,
    EnduranceVault,
    ShieldCorridor,
    BurrowBreach,
    // Keep this final ordinal stable: active saves persist gate types by value.
    CompoundVault
};

enum class MiningGateState {
    None,
    Locked,
    InProgress,
    Open,
    Completed
};

inline constexpr std::size_t miningActCount = 3;
inline constexpr std::size_t miningProgressionBandCount = 4;
inline constexpr std::size_t miningFirstClearProgressCount = miningActCount * miningProgressionBandCount;
inline constexpr std::size_t miningGateTypeCount = 10;

struct MiningArenaRequest {
    MiningAct act = MiningAct::ActOne;
    int difficulty = 1;
    std::uint64_t seed = 0;
    bool gateOverrideEnabled = false;
    MiningGateType gateOverride = MiningGateType::None;
};

struct MiningRewardBudget {
    int rareGuarantee = 0;
    int exoticGuarantee = 0;
    int rareCap = 0;
    int exoticCap = 0;
};

struct MiningMechanicGates {
    bool movement = false;
    bool drilling = false;
    bool returnZone = false;
    bool fogAndScanner = false;
    bool oxygenAndFuel = false;
    bool drillHeat = false;
    bool drillIntegrity = false;
    bool contactRebound = false;
    bool fieldRepairs = false;
    bool cargoDrag = false;
    bool environmentalHazards = false;
    bool artifactRecovery = false;
    bool artifactTethering = false;
    bool siteAndDepthVariation = false;
    bool passiveDroneCombat = false;
};

struct MiningReferenceDroneCapability {
    int slots = 0;
    int maximumMark = 0;
    std::array<MiniDroneRole, 6> roles {};
    std::size_t roleCount = 0;
    std::string_view summary;
};

struct MiningArenaRules {
    MiningArenaRequest request;
    MiningProgressionBand band = MiningProgressionBand::Learn;
    MiningMechanicGates mechanics;
    std::array<bool, miningCellMaterialCount> allowedMaterials {};
    std::array<bool, 7> allowedEnemyTypes {};
    std::array<bool, 5> allowedAffinities {};
    std::array<bool, 9> allowedRoomFeatures {};
    std::array<bool, miningGateTypeCount> allowedGateTypes {};
    int maximumGateLocks = 0;
    int maxActiveEnemies = 0;
    int maxSpawners = 0;
    double terrainToughnessScale = 1.0;
    double enemyHealthScale = 0.0;
    double enemyDamageScale = 0.0;
    MiningRewardBudget rewardBudget;
    MiningReferenceDroneCapability referenceDrones;
    std::string_view complication;
    std::string_view tutorialCallout;
    std::string_view mineralAvailability;
    std::string_view knownEnemyRoles;
    std::string_view recommendedCounters;
};

struct MiningArenaMetadata {
    MiningAct act = MiningAct::ActOne;
    int difficulty = 1;
    std::uint64_t seed = 0;
    int rulesVersion = 0;
    MiningGateType gateType = MiningGateType::None;
    bool gateOverrideEnabled = false;
};

struct MiningGateDefinition {
    MiningGateType type = MiningGateType::None;
    // True only for an active gate restored through the pre-scenario
    // compatibility migration. New authored/procedural sites stay false.
    bool compatibilityCritical = false;
    bool requiresHazardTreatment = false;
    bool requiresEnemyClearance = false;
    bool requiresSurveyTriangulation = false;
    bool fragileArtifact = false;
    bool heavyTow = false;
    bool endurancePlacement = false;
    bool shieldCorridor = false;
    bool burrowBreach = false;
    int requiredHazardMark = 0;
    int requiredSurveyOrigins = 0;
    MiningElementalAffinity hazardAffinity = MiningElementalAffinity::None;
    std::string_view name;
    std::string_view requiredCapability;
    std::string_view alternatives;
};

// A protected objective is intentionally independent of the mechanism that
// protects it. Cocoon layers, encounter rooms, and future scenario mechanics
// can all expose the same objective through this small adapter boundary.
enum class ProtectedObjectiveKind {
    None,
    Artifact
};

struct ProtectedObjectiveRef {
    ProtectedObjectiveKind kind = ProtectedObjectiveKind::None;
    std::string id;
};

struct MiningCocoonOffset {
    int x = 0;
    int y = 0;
};

enum class MiningCocoonRevealPolicy {
    OnAnyCellDiscovered,
    AfterPreviousLayerCompleted,
    Immediately
};

enum class MiningCocoonCompletionRule {
    TreatAndExcavate,
    TreatOnly
};

struct MiningCocoonLayerDefinition {
    std::string id;
    std::string label;
    std::vector<MiningCocoonOffset> offsets;
    MiningCocoonRevealPolicy revealPolicy = MiningCocoonRevealPolicy::OnAnyCellDiscovered;
    MiningCocoonCompletionRule completionRule = MiningCocoonCompletionRule::TreatAndExcavate;
    MiningElementalAffinity hazardAffinity = MiningElementalAffinity::Thermal;
    int requiredHazardMark = 1;
};

struct MiningCocoonDefinition {
    std::string id;
    int version = 1;
    // Surface Survey uses this authored offset to forecast a guaranteed
    // protected payload without turning it into a random artifact roll.
    int surveySignalDepthOffset = 0;
    std::vector<MiningCocoonLayerDefinition> layers;
    ProtectedObjectiveRef protectedObjective;
};

struct MiningCocoonLayerProgress {
    std::string id;
    std::string label;
    int total = 0;
    int remaining = 0;
    int requiredHazardMark = 0;
    bool revealed = false;
    bool completed = false;
    MiningCocoonCompletionRule completionRule = MiningCocoonCompletionRule::TreatAndExcavate;
    // Persist the authored reveal trigger with the active instance so a
    // reload never has to infer a custom layer's discovery behavior.
    MiningCocoonRevealPolicy revealPolicy = MiningCocoonRevealPolicy::OnAnyCellDiscovered;
};

enum class MiningSiteBiome {
    Default,
    ThermalLava
};

// Objective placement is an authored site affordance. It keeps early teaching
// sites from inheriting the deep-route layout that later endurance sites use.
enum class MiningSiteObjectivePlacement {
    DeepRoute,
    EntryCentered
};

// An authored site configures reusable mining mechanics. It deliberately has
// no campaign or destination semantics; scenarios decide where it is offered.
struct MiningSiteDefinition {
    std::string id;
    int version = 1;
    MiningArenaRequest arena;
    MiningSiteBiome biome = MiningSiteBiome::Default;
    MiningGateType gateType = MiningGateType::None;
    MiningSiteObjectivePlacement objectivePlacement = MiningSiteObjectivePlacement::DeepRoute;
    double baselineOxygenSeconds = 0.0;
    MiningCocoonDefinition cocoon;
    MiningEnemyTheme enemyTheme = MiningEnemyTheme::Neutral;
};

struct MiningCapabilityProfile {
    std::array<int, 6> roleMarks {};
    double oxygenSeconds = 30.0;
    double scannerRadius = 0.0;
    double artifactTowEfficiency = 0.0;
    double drillControl = 0.0;
    double combatDamagePerSecond = 0.0;
    double damageRelief = 0.0;
};

struct MiningGateMarker {
    double x = 0.0;
    double y = 0.0;
    bool activated = false;
};

struct MiningGateRuntime {
    bool active = false;
    MiningGateType type = MiningGateType::None;
    MiningGateState state = MiningGateState::None;
    // True only for a gate restored through the pre-scenario compatibility
    // migration. It is not a narrative or destination identity.
    bool compatibilityCritical = false;
    bool discovered = false;
    bool completionNotified = false;
    std::string siteId;
    std::string artifactId;
    std::string cocoonDefinitionId;
    int cocoonDefinitionVersion = 0;
    ProtectedObjectiveRef protectedObjective;
    int activeCocoonLayer = -1;
    std::vector<MiningCocoonLayerProgress> cocoonLayers;
    MiningElementalAffinity hazardAffinity = MiningElementalAffinity::None;
    int requiredHazardMark = 0;
    int shellTilesTotal = 0;
    int shellTilesRemaining = 0;
    int outerShellTilesTotal = 0;
    int outerShellTilesRemaining = 0;
    int innerShellTilesTotal = 0;
    int innerShellTilesRemaining = 0;
    int assignedEnemiesRemaining = 0;
    int requiredSurveyOrigins = 0;
    int surveyOriginsCompleted = 0;
    bool hazardTreatmentComplete = false;
    bool enemyClearanceComplete = false;
    bool surveyComplete = false;
    bool burrowBreached = false;
    bool fragileArtifact = false;
    bool heavyTow = false;
    bool endurancePlacement = false;
    bool shieldCorridor = false;
    bool burrowBreach = false;
    double anchorX = 0.0;
    double anchorY = 0.0;
    std::vector<MiningGateMarker> markers;
    // Transient cache invalidation. This is intentionally omitted from save data so
    // a restored gate recomputes its terrain/enemy/marker-derived state once.
    bool derivedStateDirty = true;
};

// Runtime progress for a reusable authored or procedurally resolved mining
// site. It carries mechanics/identity only; scenario presentation and rewards
// stay in the scenario system.
struct MiningSiteProgress {
    std::string siteId;
    std::string destinationId;
    MiningAct act = MiningAct::ActOne;
    int difficulty = 1;
    std::uint64_t seed = 0;
    MiningGateType gateType = MiningGateType::None;
    std::string artifactId;
    bool discovered = false;
    bool completed = false;
    // Set only while importing pre-scenario save records. New sites are
    // created by ScenarioInstance + MiningSiteDefinition, never by this
    // compatibility record.
    bool legacyMigrated = false;
    MiningEnemyTheme enemyTheme = MiningEnemyTheme::Neutral;
};

// Kept solely for old save and migration code. Runtime systems should use
// MiningSiteProgress and MetaProgress::miningSites.
using MiningStorySiteProgress = MiningSiteProgress;

struct MiningFirstClearProgress {
    int rareBanked = 0;
    int exoticBanked = 0;
};

enum class ArtifactKind {
    Boost,
    Story
};

enum class ArtifactRewardType {
    None,
    Credits,
    ArkFuel,
    BlueprintInsight
};

enum class MiningArtifactState {
    None,
    Embedded,
    Loose,
    Delivered,
    Destroyed
};

enum class FlybyGrade {
    Active,
    Miss,
    Good,
    Perfect
};

enum class FlybyPurpose {
    Recon,
    ScenarioChallenge,
    // Serialized compatibility name. New mechanics use ScenarioChallenge.
    SaturnSlingshot = ScenarioChallenge,
    // Compatibility ordinal for existing saves. New authored departures use
    // TransferAssist and carry their content definition ID on the run.
    JupiterSlingshot,
    TransferAssist = JupiterSlingshot
};

enum class CampaignObjectiveId {
    LunarProspector,
    MarsBayExpansion,
    IoVolcanicDescent,
    SaturnSlingshot
};

enum class CampaignObjectiveState {
    Locked,
    Active,
    ReadyToClaim,
    Complete
};

struct CampaignObjectiveStatus {
    CampaignObjectiveId id = CampaignObjectiveId::LunarProspector;
    CampaignObjectiveState state = CampaignObjectiveState::Locked;
    int current = 0;
    int required = 0;
    bool briefingAcknowledged = false;
};

enum class FrontierGateKind {
    None,
    FlightData,
    LunarProspector,
    MarsBayExpansion,
    SaturnSlingshot,
    // Legacy values remain above for compatibility; new route evaluation uses
    // a requirement supplied by an authored scenario.
    ScenarioRequirement
};

struct FrontierGateStatus {
    FrontierGateKind kind = FrontierGateKind::None;
    std::string destinationId;
    std::string scenarioId;
    std::string scenarioStepId;
    std::string blockerText;
    int current = 0;
    int required = 0;
    bool satisfied = false;
};

enum class OrbitGrade {
    Active,
    Miss,
    Good,
    Perfect
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

// Route state separates the campaign frontier (the objective the player is
// working toward) from the body the ship is physically leaving.  It keeps
// return/reapproach flights honest without making presentation infer an
// origin from the target tier.
enum class RouteTransitIntent {
    None,
    Outbound,
    Reapproach,
    Recovery
};

struct RouteTransitState {
    std::string routeLinkId;
    std::string originDestinationId;
    std::string targetDestinationId;
    RouteTransitIntent intent = RouteTransitIntent::None;

    bool active() const {
        return !routeLinkId.empty() && !originDestinationId.empty() &&
            !targetDestinationId.empty() && intent != RouteTransitIntent::None;
    }
};

enum class LaunchFailureCause {
    None,
    ThermalRunaway,
    PressureRupture,
    CourseLost,
    FuelExhausted,
    TrainingRescue,
    HullBreach,
    // The controls-calibration sortie is intentionally not cleared for a
    // lunar landing. Reaching the Moon before its navigation upgrade is a
    // distinct, visible collision rather than a generic training rescue.
    LunarImpact
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
    double miningPower = 0.0;
    double miningYield = 0.0;
    double miningCooling = 0.0;
    double miningDurability = 0.0;
    double miningWidth = 0.0;
    double miningDepth = 0.0;
    double miningStorage = 0.0;
    double miningEngineEfficiency = 0.0;
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
    double droneStorage = 0.0;
    double droneEngineEfficiency = 0.0;
    double artifactTowEfficiency = 0.0;
    int scannerPulseDamage = 0;
};

struct MiniDroneStats {
    double passiveMiningRate = 0.0;
    double oxygenSeconds = 0.0;
    double scannerRadius = 0.0;
    double drillIntegrityRelief = 0.0;
    double hardRockBounceRelief = 0.0;
    double enemyEncounterRelief = 0.0;
    double sentryDamagePerSecond = 0.0;
    double enemyDamageRelief = 0.0;
    double areaControlDamagePerSecond = 0.0;
    double enemySlow = 0.0;
    double reactiveArmorDamagePerSecond = 0.0;
    double environmentalShieldRelief = 0.0;
};

struct DroneSynergyStats {
    double passiveMiningRate = 0.0;
    double oxygenSeconds = 0.0;
    double scannerRadius = 0.0;
    double enemyDamageRelief = 0.0;
    double areaControlDamagePerSecond = 0.0;
    double enemySlow = 0.0;
    double reactiveArmorDamagePerSecond = 0.0;
    double environmentalShieldRelief = 0.0;
    double hazardTreatmentRateBonus = 0.0;
    double alliedCritChanceBonus = 0.0;
    double alliedFireRateBonus = 0.0;
    int sentryVolleyBonus = 0;
};

struct MaterialInventory {
    int common = 0;
    int rare = 0;
    int exotic = 0;
};

struct SurfaceDepthProspect {
    int depthOffset = 0;
    int absoluteDepth = 0;
    MaterialInventory possibleMaterials;
    int possibleArtifacts = 0;
    bool swarmNest = false;
    double swarmArtifactChance = 0.0;
    int informationPercent = 100;
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
    RefitTrack refitTrack = RefitTrack::None;
    int refitRank = 0;
    std::string prerequisiteId;
    bool provingTier = false;
    LaunchUpgradeKind launchUpgradeKind = LaunchUpgradeKind::None;
    int launchUpgradeRank = 0;
    SurfaceDepthUpgradeKind surfaceDepthUpgradeKind = SurfaceDepthUpgradeKind::None;
    int surfaceDepthUpgradeRank = 0;
    int rigFuelLoopRank = 0;
    // Retained so old saves can still resolve, equip, and display the module.
    // Compatibility modules must never enter newly generated Refit offers.
    bool compatibilityOnly = false;
};

struct CrewUpgrade {
    std::string id;
    std::string name;
    std::string description;
    Rarity rarity = Rarity::Common;
    CrewUpgradeStats stats;
    std::string unlockKey = content::unlock::starter;
    std::vector<std::string> tags;
    RefitTrack refitTrack = RefitTrack::None;
    int refitRank = 0;
    std::string prerequisiteId;
};

struct SurfaceUpgrade {
    std::string id;
    std::string name;
    std::string description;
    Rarity rarity = Rarity::Common;
    SurfaceUpgradeCategory category = SurfaceUpgradeCategory::Drill;
    SurfaceUpgradeStats stats;
    std::vector<std::string> tags;
    int maxRank = 3;
};

struct DroneSynergyDefinition {
    std::string id;
    std::string name;
    std::string description;
    Rarity rarity = Rarity::Uncommon;
    std::vector<MiniDroneRole> requiredRoles;
    DroneSynergyStats stats;
    MiniDroneSignatureKind signatureKind = MiniDroneSignatureKind::None;
    int signatureTier = 0;
    std::string requiredUnlock = content::unlock::starter;
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
    ArtifactKind kind = ArtifactKind::Boost;
    ArtifactRewardType rewardType = ArtifactRewardType::BlueprintInsight;
    double condition = 1.0;
    bool rewardApplied = false;
};

enum class ScenarioSource {
    Authored,
    Procedural
};

enum class ScenarioEventKind {
    None,
    SafeMaterialDelivered,
    ProtectedObjectiveExtracted,
    FlybyFinished,
    ManualAction,
    ActivityAborted,
    // Appended to preserve any persisted/content enum meanings above.
    // A site event is emitted only after its staged run extracts safely;
    // assignment is emitted when a player actually equips a Support Drone.
    MiningSiteCompleted,
    EquipmentAssigned,
    // Emitted only when an artifact crosses the permanent-inventory boundary.
    // Temporary, tethered, dropped, and unreturned artifacts never qualify.
    ArtifactRecovered,
    // Appended for authored route solutions that reuse the shared Flight Data
    // ledger. The event carries the physical origin and target route IDs.
    FlightDataBanked
};

enum class ScenarioActionKind {
    None,
    AcknowledgeBriefing,
    ClaimReward,
    BeginActivity,
    RetryActivity,
    AcknowledgeFailure
};

enum class ScenarioRewardKind {
    UnlockKey,
    DroneBaySlots,
    SupportDrone,
    FrontierReadiness,
    // Appended so any future serialized representation retains the existing
    // reward-kind ordinals.
    InventoryResources,
    RouteAccess
};

enum class ScenarioStepState {
    Locked,
    Active,
    ReadyToClaim,
    Complete
};

struct ScenarioReward {
    ScenarioRewardKind kind = ScenarioRewardKind::UnlockKey;
    std::string id;
    int amount = 0;
    bool equipIfSlotAvailable = false;
    // Used by InventoryResources. Other reward kinds deliberately ignore it
    // so a single typed reward record remains sufficient for authored and
    // procedural scenario content.
    MaterialInventory materials;
};

struct ScenarioStepDefinition {
    std::string id;
    std::vector<std::string> prerequisites;
    std::string location;
    std::string title;
    std::string detail;
    std::string rewardPreview;
    std::string actionLabel;
    std::string failureExplanation;
    ScenarioEventKind completionEvent = ScenarioEventKind::None;
    std::string eventOriginId;
    std::string eventTargetId;
    int requiredProgress = 1;
    int requiredGrade = 0;
    bool mandatoryBriefing = false;
    bool claimRequired = false;
    bool firstFailureExplanation = false;
    ScenarioActionKind action = ScenarioActionKind::None;
    std::string miningSiteDefinitionId;
    std::vector<ScenarioReward> rewards;
};

struct ScenarioDefinition {
    std::string id;
    int version = 1;
    std::string availabilityUnlockKey = content::unlock::starter;
    std::string destinationId;
    std::vector<ScenarioStepDefinition> steps;
    // Authored campaign scenarios are created for every eligible save. A
    // procedural factory template remains catalog data until a factory makes
    // a concrete instance, so it cannot silently become a live contract.
    bool instantiateByDefault = true;
};

struct ScenarioFactoryDefinition {
    std::string id;
    int version = 1;
    std::string templateScenarioId;
    std::uint64_t seedSalt = 0;
};

struct ScenarioStepProgress {
    std::string id;
    int progress = 0;
    bool briefingAcknowledged = false;
    bool completed = false;
    bool claimed = false;
    bool failureAcknowledged = false;
    bool failureSeen = false;
    // Activity attempts are distinct from failure explanations. Any authored
    // mining site or challenge can use this to present a retry affordance
    // after an incomplete run without inventing destination-specific state.
    bool activityStarted = false;
};

struct ScenarioInstance {
    // `id` identifies this saved runtime instance. `definitionId` identifies
    // immutable authored content. They are equal for authored campaign
    // scenarios and intentionally differ for procedurally generated runs.
    std::string id;
    std::string definitionId;
    int definitionVersion = 1;
    ScenarioSource source = ScenarioSource::Authored;
    std::string factoryId;
    int factoryVersion = 0;
    std::uint64_t seed = 0;
    std::vector<std::string> resolvedParameters;
    std::vector<ScenarioStepProgress> steps;
    std::vector<std::string> awardedRewardIds;
    bool completed = false;
};

struct ScenarioEvent {
    ScenarioEventKind kind = ScenarioEventKind::None;
    // Optional explicit routing keeps reusable events from relying on
    // destination names or presentation copy. Empty values intentionally
    // allow a single event (for example, safe delivery) to satisfy every
    // eligible matching authored or procedural step.
    std::string scenarioId;
    std::string stepId;
    std::string originId;
    std::string targetId;
    int amount = 0;
    int grade = 0;
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
    double gravityDirectionX = 0.0;
    double gravityDirectionY = 1.0;
    double gravityScale = 1.0;
    // Content-defined route keys are evaluated generically by navigation.
    std::vector<std::string> routeRequirementKeys;
    // A destination can require the late-game hostile-system navigation mode
    // without route evaluation knowing a destination ID or position.
    bool requiresHostileSystem = false;
    // Some destinations teach the arrival loop as an authored sequence before
    // surface landing becomes available. Arrival mechanics read this capability
    // instead of recognizing a destination ID.
    bool requiresArrivalSurveySequence = false;
    // A destination can begin an outward-only expedition. Recovery and
    // post-transfer presentation read this capability instead of inferring a
    // point of no return from a named destination or a tier threshold.
    bool oneWayExpedition = false;
    // Optional content-owned presentation for the route that this
    // destination unlocks from the starter proving loop. This preserves a
    // campaign's authored voice without a launch presenter recognizing a
    // destination ID.
    std::string provingRouteReadyTitle;
    std::string provingRouteReadyConsequence;
    // Content-owned approach framing keeps destination story copy out of the
    // generic Arrival board renderer.
    std::string approachBriefTitle;
    std::string approachBriefDetail;
    // Hidden origins remain addressable for save compatibility and route
    // calculations, but never appear as a player-facing destination.
    bool hiddenFromProgression = false;
    // A nonzero value opts this route into a calibrated powered-fuel margin
    // gate. The shared calculation can include a matching transfer assist.
    double calibratedTransferMarginRequired = 0.0;
    std::string transferMarginBlockerText;
};

// A directed solar route owns its source, target, calibrated powered-fuel
// demand, and whether an incomplete pass can fly the same route in reverse.
// The runtime never needs destination-name branches to decide how a route
// behaves.
struct RouteLinkDefinition {
    std::string id;
    std::string sourceDestinationId;
    std::string targetDestinationId;
    double cruiseFuelCost = 0.0;
    bool recoveryAvailable = false;
    bool oneWayExpedition = false;
};

struct TransferAssistDefinition {
    std::string id;
    std::string sourceDestinationId;
    std::string targetDestinationId;
    std::string availabilityScenarioId;
    std::string availabilityStepId;
    std::vector<LaunchTrainingStage> allowedLaunchStages;
    FlybyGrade minimumGrade = FlybyGrade::Good;
    double fuelSavings = 0.0;
    double speedBoostBase = 0.0;
    double goodInstabilityPenalty = 0.0;
    int impactHullDamage = 0;
    std::string displayName;
};

struct LaunchConfig {
    std::string destinationId;
    double burnGoalMultiplier = 1.5;
    bool frontierTransfer = false;
    LaunchMissionKind missionKind = LaunchMissionKind::Standard;
    std::string astronautId;
    std::string frameId;
    std::vector<std::string> equippedModuleIds;
    RouteTransitState routeTransit;
};

struct TelemetryEvent {
    double multiplier = 1.0;
    double heat = 0.0;
    double pressure = 0.0;
    double vibration = 0.0;
    double fuelMix = 0.0;
    double guidance = 0.0;
    double abortRisk = 0.0;
    double instability = 0.0;
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
    RouteTransitState routeTransit;
    double crashMultiplier = 1.0;
    double ejectMultiplier = 1.0;
    double payout = 0.0;
    double transferFuelRemaining = 0.0;
    double transferFuelCapacity = 0.0;
    double slingshotFuelSavings = 0.0;
    double slingshotSpeedBoost = 0.0;
    double slingshotInstabilityPenalty = 0.0;
    std::string transferAssistId;
    double recoveryCost = 0.0;
    int shipDamage = 0;
    bool crewKilled = false;
    bool crewInjured = false;
    std::string moduleDestroyedId;
    int blueprintGain = 0;
    double peakWarning = 0.0;
    double peakAbortRisk = 0.0;
    bool pilotedFlight = false;
    LaunchFailureCause failureCause = LaunchFailureCause::None;
    FuelSurveyReturnTiming fuelSurveyReturnTiming = FuelSurveyReturnTiming::Unqualified;
    double minimumSafetyMargin = 1.0;
    std::vector<TelemetryEvent> telemetry;
};

struct ArkState {
    ArkCondition condition = ArkCondition::NotFound;
    int fuelReserve = 0;
    int hullDamage = 0;
    int repairProgress = 0;
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
    GameChapter chapter = GameChapter::ProvingGround;
    ArkState ark;
    NavigationState navigation;
    LaunchUpgradeRanks launchUpgrades;
    SurfaceDepthUpgradeRanks surfaceDepthUpgrades;
    RigFuelLoopRank rigFuelLoop;
    LaunchLessonState launchLessons;
    std::vector<std::string> unlockKeys;
    int blueprintProgress = 0;
    MaterialInventory materials;
    std::vector<std::string> ownedModuleIds;
    std::vector<std::string> defaultEquippedModuleIds;
    int droneBaySlots = 0;
    std::vector<std::string> ownedDroneIds;
    std::vector<std::string> equippedDroneIds;
    std::vector<ScenarioInstance> scenarios;
    int prospectorCommonOreRecovered = 0;
    bool lunarMiningBriefingAcknowledged = false;
    bool lunarProspectorClaimed = false;
    int marsCommonOreRecovered = 0;
    bool marsMiningBriefingAcknowledged = false;
    bool marsBayExpansionClaimed = false;
    bool ioVolcanicBriefingAcknowledged = false;
    bool ioHazardDroneCommissioned = false;
    bool ioArtifactRecovered = false;
    bool saturnSlingshotBriefingAcknowledged = false;
    bool saturnSlingshotPerfect = false;
    bool saturnRouteUnlocked = false;
    bool saturnSlingshotFailed = false;
    bool saturnSlingshotFailureAcknowledged = false;
    // Combat upgrades become understandable only after the crew has met a
    // hostile force. This is campaign knowledge, not a per-sortie reset.
    bool hasEncounteredEnemy = false;
    std::vector<ArtifactRecord> artifacts;
    std::array<MiningFirstClearProgress, miningFirstClearProgressCount> miningFirstClearProgress {};
    std::vector<MiningSiteProgress> miningSites;
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
    std::vector<std::string> acknowledgedActivityBriefingIds;
    std::vector<PostSolarSystemRoster> postSolarSystemRosters;
    bool campaignIntroductionAcknowledged = false;
    bool straylightDiscoveryAcknowledged = false;
};

struct StoryBriefingState {
    StoryBriefingId pending = StoryBriefingId::None;
    Screen continuation = Screen::Hangar;
};

enum class ApproachCommitment {
    Uncommitted = 0,
    OrbitCaptured = 1
};

struct ArrivalOpsState {
    bool active = false;
    std::string destinationId;
    double transferFuelRemaining = 0.0;
    double transferFuelCapacity = 0.0;
    ApproachCommitment commitment = ApproachCommitment::Uncommitted;
    // Retained from the successful leg so a Pass Through can offer a real
    // recovery route instead of silently restarting at Earth.
    RouteTransitState incomingRoute;
};

struct FlybyTrailPoint {
    double x = 0.0;
    double y = 0.0;
};

struct FlybyRunState {
    bool active = false;
    std::string destinationId;
    FlybyPurpose purpose = FlybyPurpose::Recon;
    // Scenario challenges share reconnaissance flight behavior, while their
    // authored completion contract remains attached to this active run.
    std::string scenarioId;
    std::string scenarioStepId;
    // Empty for reconnaissance and ordinary scenario challenges. A populated
    // ID turns the finished pass into the authored physical transfer assist.
    std::string transferAssistId;
    double elapsedSeconds = 0.0;
    double durationSeconds = 18.0;
    double shipX = -0.68;
    double shipY = -0.24;
    double velocityX = 0.26;
    double velocityY = 0.13;
    double inputX = 0.0;
    // A signed input that adjusts the retained throttle setpoint. It is not
    // itself engine power: releasing the key/stick leaves selectedThrottle
    // where the player set it.
    double inputY = 0.0;
    double selectedThrottle = 0.0;
    double gravityStrength = 0.0;
    double goodBand = 0.145;
    double perfectBand = 0.050;
    double turnRateRadians = 1.45;
    double thrustAcceleration = 0.66;
    int impactHullDamage = 18;
    double pathProgress = 0.0;
    int worstZone = 2;
    double planetColliderRadius = 0.15;
    double missSeconds = 0.0;
    double goodSeconds = 0.0;
    double perfectSeconds = 0.0;
    double currentMissStreak = 0.0;
    double longestMissStreak = 0.0;
    int currentZone = 0;
    bool completed = false;
    bool collidedWithBody = false;
    FlybyGrade result = FlybyGrade::Active;
    double rewardCredits = 0.0;
    int blueprintGain = 0;
    double rewardBonusScale = 1.0;
    bool slingshotAwarded = false;
    double slingshotFuelSavings = 0.0;
    double slingshotSpeedBoost = 0.0;
    double slingshotSpeedScale = 1.0;
    std::vector<FlybyTrailPoint> trailPoints;
};

struct PendingTransferAssist {
    std::string definitionId;
    std::string sourceDestinationId;
    std::string targetDestinationId;
    FlybyGrade grade = FlybyGrade::Active;
    double fuelSavings = 0.0;
    double speedBoost = 0.0;
    double instabilityPenalty = 0.0;
    // Signed launch-corridor position inherited from the completed assist.
    // Negative and positive values preserve which side of center the ship
    // occupied; the magnitude uses the Launch course-offset scale.
    double exitCourseOffset = 0.0;

    bool active() const { return !definitionId.empty() && !targetDestinationId.empty(); }
};

struct OrbitRunState {
    bool active = false;
    std::string destinationId;
    double elapsedSeconds = 0.0;
    double durationSeconds = 15.0;
    double planetRadius = 0.16;
    double targetRadius = 0.44;
    double goodBand = 0.070;
    double perfectBand = 0.030;
    double shipX = -0.44;
    double shipY = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.30;
    double inputX = 0.0;
    // Vertical input adjusts the retained prograde throttle setpoint. Releasing
    // the key/stick holds selectedThrottle while radial steering stays direct.
    double inputY = 0.0;
    double selectedThrottle = 0.0;
    bool trimApplied = false;
    double gravityStrength = 0.040;
    double thrustAcceleration = 0.075;
    double collisionPadding = 0.018;
    double orbitProgress = 0.0;
    double angleRadians = 0.0;
    int worstZone = 2;
    int currentZone = 0;
    double missSeconds = 0.0;
    double goodSeconds = 0.0;
    double perfectSeconds = 0.0;
    bool completed = false;
    OrbitGrade result = OrbitGrade::Active;
    double rewardCredits = 0.0;
    int blueprintGain = 0;
    std::vector<FlybyTrailPoint> trailPoints;
};

struct SurfaceExpeditionState {
    bool active = false;
    std::string destinationId;
    std::string postSolarSystemId;
    std::string bodyId;
    SurfaceSiteProfile siteProfile = SurfaceSiteProfile::SurveyBasin;
    int supply = 0;
    double rigFuel = 0.0;
    double rigFuelCapacity = 0.0;
    double transferFuelRecovered = 0.0;
    double expeditionPackFuel = 0.0;
    int cargo = 0;
    double hazard = 0.0;
    int depth = 0;
    MaterialInventory temporaryMaterials;
    std::vector<ArtifactRecord> temporaryArtifacts;
    MaterialInventory prospectMaterials;
    int prospectArtifacts = 0;
    std::vector<SurfaceDepthProspect> depthProspects;
    std::vector<std::string> logEntries;
    int expeditionLevel = 1;
    double expeditionExperience = 0.0;
    int pendingRunUpgradeChoices = 0;
    std::array<RunUpgradeOffer, 3> runUpgradeOffers {};
    int runUpgradeOfferCount = 0;
    bool runUpgradeOfferPending = false;
    Screen runUpgradeReturnScreen = Screen::SurfaceExpedition;
    std::vector<RunRigUpgradeRank> runRigUpgradeRanks;
    std::vector<RunDroneRank> runDroneRanks;
    std::vector<std::string> selectedSynergyIds;
    bool enemyEncountersEnabled = false;
    bool miningSitePrepared = false;
    bool miningRunUsed = false;
    // A generic scenario can stage a reusable mining site without teaching
    // the surface loop any narrative destination or artifact terminology.
    std::string pendingScenarioId;
    std::string pendingScenarioStepId;
    std::string pendingMiningSiteDefinitionId;
    bool bankedMiningArenaValid = false;
    bool bankedMiningProgressionEligible = false;
    MiningArenaMetadata bankedMiningArenaMetadata;
    MaterialInventory bankedMiningMaterials;
    std::vector<DroneFrameModuleAssignment> droneModuleAssignments;
    std::vector<DroneModuleRuntimeState> droneModuleRuntime;
    double scannerCooldownSeconds = 0.0;
    std::vector<TreasureMark> treasureMarks;
    int reclamationOxygenUses = 0;
    double reclamationFuelRecovered = 0.0;
};

enum class SurfaceScanPulseGrade {
    None,
    Miss,
    Good,
    Perfect
};

struct SurfaceScanRunState {
    bool active = false;
    bool completed = false;
    bool busted = false;
    std::string destinationId;
    int pulses = 0;
    int maxPulses = 5;
    double elapsedSeconds = 0.0;
    SurfaceScanPulseGrade lastPulseGrade = SurfaceScanPulseGrade::None;
    int lastPulseDepthOffset = 0;
    double successFanfareSeconds = 0.0;
    // Presentation-only sting after a missed pulse. It has no impact on
    // survey rewards, risk, or saved progression.
    double missFanfareSeconds = 0.0;
    double signal = 0.0;
    double interference = 0.0;
    double bustRisk = 0.0;
    double hazardDelta = 0.0;
    int cargo = 0;
    MaterialInventory temporaryMaterials;
    std::vector<ArtifactRecord> temporaryArtifacts;
    std::vector<SurfaceDepthProspect> depthProspects;
    std::string message;
};

struct SurfacePushRunState {
    bool active = false;
    bool completed = false;
    bool busted = false;
    std::string destinationId;
    int steps = 0;
    int maxSteps = 4;
    int depthGain = 0;
    double pressure = 0.0;
    double collapseRisk = 0.0;
    double hazardDelta = 0.0;
    int cargo = 0;
    MaterialInventory temporaryMaterials;
    std::vector<ArtifactRecord> temporaryArtifacts;
    std::vector<MiningCellMaterial> rewardMarkers;
    std::vector<int> rewardMarkerDepthOffsets;
    std::string message;
};

struct MiningCell {
    MiningCellMaterial material = MiningCellMaterial::Empty;
    double maxToughness = 0.0;
    double remainingToughness = 0.0;
    bool revealed = false;
    bool hazard = false;
    MiningCellFeature feature = MiningCellFeature::None;
    MiningEnemyType enemy = MiningEnemyType::None;
    MiningElementalAffinity hazardAffinity = MiningElementalAffinity::None;
    bool gateAssociated = false;
    bool suitOnlyPassage = false;
    // -1 means the cell is not part of a layered cocoon.
    int cocoonLayer = -1;
};

struct MiningTerrain {
    int width = 64;
    int height = 40;
    int depthZone = 0;
    std::vector<MiningCell> cells;
    std::vector<std::uint8_t> dirtyChunks;
};

struct MiningEnemySpawnSpec {
    MiningEnemyType enemyType = MiningEnemyType::None;
    MiningElementalAffinity affinity = MiningElementalAffinity::None;
    int maxSpawns = 0;
    int spawned = 0;
    double intervalSeconds = 0.0;
    double cooldownSeconds = 0.0;
};

struct MiningEnemy {
    MiningEnemyType type = MiningEnemyType::None;
    MiningCellFeature sourceFeature = MiningCellFeature::None;
    double x = 0.0;
    double y = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    double health = 0.0;
    double maxHealth = 0.0;
    double armor = 0.0;
    double speed = 0.0;
    double damagePerSecond = 0.0;
    double effectRadius = 0.0;
    bool active = true;
    MiningElementalAffinity affinity = MiningElementalAffinity::None;
    double attackCooldownSeconds = 0.0;
    double scannedPrioritySeconds = 0.0;
    int insightGroupKey = -1;
    MiningEnemySpawnSpec spawn;
    bool gateAssociated = false;
    bool swarmAssociated = false;
    bool elite = false;
    // Presentation timers do not drive combat outcomes. They select the
    // shared Attack, Hit, and Defeat clips in the scene renderer.
    double attackAnimationSeconds = 0.0;
    double hitAnimationSeconds = 0.0;
    double defeatAnimationSeconds = 0.0;
};

// One optional Swarm Nest is seeded for an eligible expedition. Its enemies
// live in the normal per-depth vectors, while this record tracks the wave and
// cache across depth transitions and save/load.
struct MiningSwarmState {
    bool enabled = false;
    int depthZone = -1;
    std::uint64_t seed = 0;
    int chamberX = 0;
    int chamberY = 0;
    int triggerX = 0;
    bool alerted = false;
    double alertSeconds = 0.0;
    int wave = 0;
    int spawnedInWave = 0;
    int waveSize = 0;
    double spawnCooldownSeconds = 0.0;
    double intermissionSeconds = 0.0;
    bool cacheExposed = false;
    bool cacheClaimed = false;
    bool cacheBanked = false;
    double cacheX = 0.0;
    double cacheY = 0.0;
    MaterialInventory cacheMaterials;
    int blueprintInsight = 0;
    bool bonusArtifactRolled = false;
    double artifactChance = 0.0;
    ArtifactRecord bonusArtifact;
};

enum class MiningCombatTeam {
    Allied,
    Enemy
};

enum class MiningCombatTextKind {
    Damage,
    Defeat,
    CommonReward,
    RareReward,
    ExoticReward
};

struct MiningProjectileVisual {
    double startX = 0.0;
    double startY = 0.0;
    double endX = 0.0;
    double endY = 0.0;
    double age = 0.0;
    double lifetime = 0.35;
    MiningCombatTeam team = MiningCombatTeam::Allied;
    MiningEnemyType sourceType = MiningEnemyType::None;
    MiningElementalAffinity affinity = MiningElementalAffinity::None;
    bool critical = false;
};

struct MiningDamageNumber {
    double x = 0.0;
    double y = 0.0;
    double amount = 0.0;
    double age = 0.0;
    double lifetime = 0.90;
    MiningCombatTeam team = MiningCombatTeam::Allied;
    MiningCombatTextKind kind = MiningCombatTextKind::Damage;
    bool critical = false;
    bool rigDamage = false;
};

struct MiningLooseChunk {
    MiningCellMaterial material = MiningCellMaterial::CommonOre;
    double x = 0.0;
    double y = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    int cargoValue = 1;
    bool active = true;
};

struct MiningPickupEvent {
    std::uint64_t sequence = 0;
    MiningPickupKind kind = MiningPickupKind::CommonOre;
    int amount = 0;
    double x = 0.0;
    double y = 0.0;
};

struct MiniDroneAnchorFrame {
    MiningActorIdentity actor = MiningActorIdentity::Rig;
    double x = 0.0;
    double y = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    double facingX = 0.0;
    double facingY = 1.0;
    double colliderRadius = 0.48;
    int depthZone = 0;
    bool valid = false;
};

struct MiningMiniDroneAgent {
    MiniDroneRole role = MiniDroneRole::Mining;
    int roleIndex = 0;
    int equippedFrame = -1;
    int upgradeLevel = 1;
    double x = 0.0;
    double y = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    MiningMiniDroneBehavior behavior = MiningMiniDroneBehavior::Following;
    int targetCellX = -1;
    int targetCellY = -1;
    int targetEnemyIndex = -1;
    double actionCooldownSeconds = 0.0;
    double taskProgressSeconds = 0.0;
    double surveyPulseSeconds = 0.0;
    double defenseAngleRadians = 0.0;
    double shieldCharge = 1.0;
    double shieldRechargeSeconds = 0.0;
    double shieldImpactSeconds = 0.0;
    MaterialInventory haulMaterials;
    MaterialInventory uncreditedHaulMaterials;
    bool finishTargetBeforeReturn = false;
    double returnPathFailureSeconds = 0.0;
    bool defenseAngleInitialized = false;
    MiningAnchorTarget anchorTarget = MiningAnchorTarget::ControlledActor;
    int stableFormationSlot = 0;
    double orbitPhaseRadians = 0.0;
};

struct MiningArtifactObject {
    bool present = false;
    std::string id;
    ArtifactKind kind = ArtifactKind::Boost;
    ArtifactRewardType rewardType = ArtifactRewardType::BlueprintInsight;
    MiningArtifactState state = MiningArtifactState::None;
    double x = 0.0;
    double y = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    double health = 1.0;
    double maxHealth = 1.0;
    double embedStrength = 1.0;
    bool tethered = false;
    bool revealed = false;
};

// A mining run owns one active depth in MiningRunState and keeps every other
// visited depth here. Moving between depths swaps the complete world state so
// carved tunnels, threats, gates, and abandoned artifacts remain where the
// player left them.
struct MiningDepthLayerState {
    int depthZone = 0;
    MiningTerrain terrain;
    std::vector<MiningEnemy> enemies;
    MiningArtifactObject artifact;
    std::vector<MiningLooseChunk> looseChunks;
    MiningGateRuntime gate;
    double downwardTransitionX = 0.0;
    bool hasDownwardTransition = false;
};

struct MiningRunState {
    bool active = false;
    MiningArenaMetadata arenaMetadata;
    MiningRewardBudget rewardBudget;
    MaterialInventory richRewardsAwarded;
    bool progressionCreditEligible = true;
    std::string destinationId;
    std::string postSolarSystemId;
    std::string bodyId;
    std::string surfaceGeologyId;
    std::string deepGeologyId;
    std::uint64_t geologySeed = 0;
    std::string scenarioId;
    std::string scenarioStepId;
    std::string miningSiteDefinitionId;
    // Persist the resolved site profile so active procedural/authored sites
    // never fall back to destination-specific generation after a reload.
    MiningSiteBiome miningSiteBiome = MiningSiteBiome::Default;
    MiningEnemyTheme enemyTheme = MiningEnemyTheme::Neutral;
    double siteBaselineOxygenSeconds = 0.0;
    SurfaceSiteProfile siteProfile = SurfaceSiteProfile::SurveyBasin;
    double elapsedSeconds = 0.0;
    // Rig life support. This continues to receive rig upgrades, site
    // baselines, pockets, and support-drone bonuses.
    double oxygenSeconds = 30.0;
    double fuelCycleProgress = 0.0;
    int fuelSpent = 0;
    double droneX = 32.0;
    double droneY = 4.0;
    double rigVelocityX = 0.0;
    double rigVelocityY = 0.0;
    bool rigDisabled = false;
    // Retained only to read older saves that recorded the removed ship-winch
    // behavior. New runtime and save data always normalize this to false.
    bool rigTethered = false;
    // EVA uses this line to physically tow the Mining Rig through a return
    // shaft.
    bool operatorRigTethered = false;
    int rigDepthZone = 0;
    MiningOperatorMode operatorMode = MiningOperatorMode::Rig;
    bool operatorPresent = false;
    double operatorX = 32.0;
    double operatorY = 4.0;
    double operatorVelocityX = 0.0;
    double operatorVelocityY = 0.0;
    double operatorAimDirX = 0.0;
    double operatorAimDirY = 1.0;
    double operatorIntegrity = 1.0;
    // EVA carries a fixed emergency reserve independent of the rig tank.
    double operatorOxygenSeconds = 15.0;
    double operatorFireCooldownSeconds = 0.0;
    double operatorFirePulseSeconds = 0.0;
    double operatorToggleProgress = 0.0;
    bool firing = false;
    double gravityDirectionX = 0.0;
    double gravityDirectionY = 1.0;
    double gravityStrength = 0.0;
    double moveX = 0.0;
    double moveY = 0.0;
    double aimX = 32.0;
    double aimY = 5.0;
    double aimDirX = 0.0;
    double aimDirY = 1.0;
    double hullDirX = 0.0;
    double hullDirY = 1.0;
    bool drilling = false;
    bool drillThermalLock = false;
    bool failurePending = false;
    double failureSeconds = 0.0;
    std::string failureMessage;
    double drillHeat = 0.0;
    double drillIntegrity = 1.0;
    double droneHealth = 1.0;
    double returnZoneX = 0.0;
    double returnZoneY = 0.0;
    double contactIntensity = 0.0;
    // Transient presentation state for a player-driven collision. Unlike
    // contactIntensity, this remembers which edge of the active vehicle hit
    // terrain so the scene can make tight clearances legible.
    double contactIndicatorSeconds = 0.0;
    double contactIndicatorDirX = 0.0;
    double contactIndicatorDirY = 0.0;
    // Short-lived presentation acknowledgement when a nearby artifact is
    // still sealed. This is deliberately transient and is not save data.
    double artifactTetherDeniedFlashSeconds = 0.0;
    double recoilX = 0.0;
    double recoilY = 0.0;
    double contactBounce = 0.0;
    double contactBounceVelocity = 0.0;
    double contactBounceCooldown = 0.0;
    double contactSpeedRecovery = 1.0;
    double scannerPulseSeconds = 0.0;
    int depthZone = 0;
    int entryDepthZone = 0;
    int deepestDepthZone = 0;
    double downwardTransitionX = 0.0;
    bool hasDownwardTransition = false;
    double depthTransitionCooldownSeconds = 0.0;
    int cargo = 0;
    MaterialInventory temporaryMaterials;
    std::vector<ArtifactRecord> temporaryArtifacts;
    int stowedCargo = 0;
    MaterialInventory stowedMaterials;
    std::vector<ArtifactRecord> stowedArtifacts;
    double hazardDelta = 0.0;
    bool drillBreakNotified = false;
    bool oxygenDepletedNotified = false;
    bool operatorOxygenDepletedNotified = false;
    // Ship service keeps this latched while the applicable actor remains in
    // the service field so the successful refill does not spam the status
    // line each simulation tick.
    bool shipServiceOxygenNotified = false;
    int passiveDroneYield = 0;
    int cellsBroken = 0;
    int enemiesDefeated = 0;
    double defenseDamageDealt = 0.0;
    double enemyDamageTaken = 0.0;
    double areaControlDamageDealt = 0.0;
    double reactiveArmorDamageDealt = 0.0;
    double environmentalShieldAbsorbed = 0.0;
    double elementalExposureSeconds = 0.0;
    double movementSlowSeconds = 0.0;
    double movementSlowScale = 1.0;
    double alliedFireCooldownSeconds = 0.0;
    double areaControlPulseCooldownSeconds = 0.0;
    int combatSequence = 0;
    int targetCellX = -1;
    int targetCellY = -1;
    double targetTipX = 32.0;
    double targetTipY = 5.0;
    MiningCellMaterial targetMaterial = MiningCellMaterial::Empty;
    double targetRemainingToughness = 0.0;
    double targetMaxToughness = 0.0;
    MiningTerrain terrain;
    std::vector<MiningEnemy> enemies;
    std::vector<MiningMiniDroneAgent> miniDrones;
    std::vector<MiningLooseChunk> looseChunks;
    std::uint64_t pickupEventSequence = 0;
    std::vector<MiningPickupEvent> pickupEvents;
    std::vector<MiningProjectileVisual> combatProjectiles;
    std::vector<MiningDamageNumber> damageNumbers;
    MiningArtifactObject artifact;
    MiningGateRuntime gate;
    std::vector<MiningDepthLayerState> depthLayers;
    MiningSwarmState swarm;
};

struct RunState {
    bool active = true;
    int destinationIndex = 0;
    int frontierReadiness = 0;
    bool refitEntitled = false;
    SurfaceRefitPurchaseState surfaceRefitPurchases;
    int shipDamage = 0;
    double credits = 0.0;
    std::string frameId;
    std::vector<std::string> inventoryModuleIds;
    std::vector<std::string> equippedModuleIds;
    std::vector<std::string> crewUpgradeIds;
    std::vector<Astronaut> crew;
    std::array<std::string, 3> offerModuleIds {};
    std::array<std::string, 3> offerCrewUpgradeIds {};
    std::array<std::string, 3> researchProjectIds {};
    ArrivalOpsState arrivalOps;
    FlybyRunState flyby;
    OrbitRunState orbit;
    SurfaceExpeditionState surfaceExpedition;
    SurfaceScanRunState surfaceScan;
    SurfacePushRunState surfacePush;
    MiningRunState mining;
    // Serialized under the compatibility field name nextLaunchFuelBoost.
    // The value is powered-route fuel saved by existing Flyby momentum; it
    // never changes the physical Transfer Tank capacity.
    double nextLaunchFuelBoost = 0.0;
    double nextLaunchSpeedBoost = 0.0;
    double nextLaunchInstabilityPenalty = 0.0;
    PendingTransferAssist pendingTransferAssist;
    // A queued outbound, recovery, or reapproach leg shown in the Hangar.
    RouteTransitState routeTransit;
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
    StoryBriefingState storyBriefing;
    std::string statusLine = std::string(text::status::welcome);
};

std::string_view toString(SlotType slot);
std::string_view toString(RefitTrack track);
std::string_view toString(LaunchUpgradeKind kind);
std::string_view toString(SurfaceDepthUpgradeKind kind);
std::string_view toString(LaunchTrainingStage stage);
std::string_view toString(LaunchMissionKind kind);
std::string_view toString(Rarity rarity);
std::string_view toString(SurfaceUpgradeCategory category);
std::string_view toString(MiniDroneRole role);
std::string_view toString(CrewStatus status);
std::string_view toString(LaunchResultType result);
std::string_view toString(RecoveryMethod method);
std::string_view toString(LaunchFailureCause cause);
std::string_view toString(CampaignMilestone milestone);
std::string_view toString(GameChapter chapter);
int chapterNumber(GameChapter chapter);
std::string chapterLabel(GameChapter chapter);
std::string_view chapterGate(GameChapter chapter);
std::string_view toString(ArkCondition condition);

ModuleStats operator+(ModuleStats lhs, const ModuleStats& rhs);
ModuleStats& operator+=(ModuleStats& lhs, const ModuleStats& rhs);

} // namespace rocket
