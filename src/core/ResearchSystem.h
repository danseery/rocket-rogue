#pragma once

#include "core/Content.h"
#include "core/GameState.h"
#include "core/Random.h"
#include "core/Tuning.h"

#include <string>
#include <string_view>
#include <vector>

namespace rocket {

enum class SurfaceEventType {
    None,
    EquipmentFailure,
    UnexpectedDeposit,
    CrewDiscovery,
    EnemyContact
};

struct ResearchOutcome {
    bool completed = false;
    std::string projectId;
    int blueprintGain = 0;
    MaterialInventory materialCost;
    std::string rewardUnlockKey;
    bool unlockedReward = false;
    bool identifiedArtifact = false;
    std::string artifactId;
};

struct SurfaceToolEffects {
    int supplyBonus = 0;
    int surveyCommonBonus = 0;
    int mineCommonBonus = 0;
    double mineRareChanceBonus = 0.0;
    double hazardRelief = 0.0;
    double enemyEncounterRelief = 0.0;
};

struct SurfaceCrewEffects {
    int supplyBonus = 0;
    int surveyCommonBonus = 0;
    int mineCommonBonus = 0;
    double mineRareChanceBonus = 0.0;
    double hazardRelief = 0.0;
    double artifactChanceBonus = 0.0;
    std::string summary;
};

struct SurfaceSiteProfileEffects {
    int supplyBonus = 0;
    int surveyCommonBonus = 0;
    int mineCommonBonus = 0;
    double mineRareChanceBonus = 0.0;
    double hazardDelta = 0.0;
    double artifactChanceBonus = 0.0;
};

struct SurfaceUpgradeEffects {
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
    std::vector<std::string> names;
};

struct MiniDroneLoadoutEffects {
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
    double hazardTreatmentRateBonus = 0.0;
    double alliedCritChanceBonus = 0.0;
    double alliedFireRateBonus = 0.0;
    int sentryVolleyBonus = 0;
    int signatureTier = 0;
    MiniDroneSignatureKind signatureKind = MiniDroneSignatureKind::None;
    std::vector<std::string> names;
    std::vector<std::string> synergyNames;
    std::string signatureName;
    std::string signatureDetail;
};

struct SurfaceActionOutcome {
    bool applied = false;
    std::string message;
    int supplyDelta = 0;
    int fuelDelta = 0;
    int cargoDelta = 0;
    // Exact ledger values for a normal return. materialDelta remains the
    // spendable inventory gain after mission allocations are committed.
    MaterialInventory materialReturned;
    MaterialInventory materialCommitted;
    MaterialInventory materialDelta;
    MaterialInventory materialLost;
    bool hazardTriggered = false;
    std::string hazardMessage;
    double hazardDelta = 0.0;
    SurfaceEventType eventType = SurfaceEventType::None;
    std::string eventMessage;
    int blueprintDelta = 0;
    bool artifactFound = false;
    int artifactsLost = 0;
    bool enemyEncounter = false;
    bool cargoRecovered = false;
    bool prospectorUnlocked = false;
};

enum class SurfaceReturnSafetySeverity {
    Safe,
    Caution,
    Critical
};

struct SurfaceReturnSafetyAssessment {
    SurfaceReturnSafetySeverity severity = SurfaceReturnSafetySeverity::Safe;
    int depth = 0;
    int estimatedReturnSeconds = 0;
    int oxygenSeconds = 0;
    int fuelNeededAfterDeployment = 0;
    int fuelAvailableAfterDeployment = 0;
    double fuelCycleSeconds = tuning::rigFuelLoopProgression::baseCycleSeconds;
    bool oxygenCritical = false;
    bool fuelCritical = false;
};

enum class SurfaceDepthBlocker {
    None,
    SurveyRating,
    Unsurveyed,
    BoreRating,
    ReturnCritical
};

struct SurfaceDepthCapability {
    int targetDepth = 0;
    int surveyRating = tuning::surfaceDepthProgression::baseDepthRating;
    int boreRating = tuning::surfaceDepthProgression::baseDepthRating;
    int surveyedThroughDepth = 0;
    int usableDepth = 0;
    SurfaceDepthBlocker blocker = SurfaceDepthBlocker::None;
    SurfaceReturnSafetyAssessment returnSafety;
    bool canDig = false;
};

struct ExpeditionExperienceAward {
    double appliedExperience = 0.0;
    int levelsGained = 0;
    int resultingLevel = 1;
    double resultingExperience = 0.0;
    int pendingChoices = 0;
};

struct SurfaceReturnAllocation {
    std::string scenarioId;
    std::string stepId;
    std::string label;
    std::string materialId;
    int amount = 0;
    int currentAfter = 0;
    int required = 0;
};

struct SurfaceReturnLedger {
    MaterialInventory onShip;
    MaterialInventory toMaterials;
    std::vector<SurfaceReturnAllocation> allocations;
    int artifacts = 0;
};

// Compatibility-only campaign façade. New gameplay, routing, and UI actions
// must use ScenarioSystem IDs, ScenarioEvent, and performScenarioAction
// directly. These declarations preserve old save/UI/test callers while the
// non-authoritative legacy fields remain readable during migration.
CampaignObjectiveStatus campaignObjectiveStatus(const GameState& state, CampaignObjectiveId objective);
bool acknowledgeCampaignObjectiveBriefing(GameState& state, CampaignObjectiveId objective);
int creditCampaignCommonOre(GameState& state, std::string_view destinationId, int deliveredCommonOre);
int creditCampaignCommonOre(
    GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId,
    int deliveredCommonOre);
bool canClaimLunarProspector(const GameState& state);
bool claimLunarProspector(GameState& state, const ContentCatalog& catalog);
bool canClaimMarsBayExpansion(const GameState& state);
bool claimMarsBayExpansion(GameState& state, const ContentCatalog& catalog);
bool canCommissionIoHazardDrone(const GameState& state, const ContentCatalog& catalog);
bool commissionIoHazardDrone(GameState& state, const ContentCatalog& catalog);
bool creditRecoveredProtectedObjective(
    GameState& state,
    const ContentCatalog& catalog,
    ArtifactRecord& artifact,
    std::string_view miningSiteDefinitionId = {});
bool creditRecoveredIoArtifact(GameState& state, ArtifactRecord& artifact);
bool creditRecoveredIoArtifact(GameState& state, const ContentCatalog& catalog, ArtifactRecord& artifact);
bool canStartSaturnSlingshot(const GameState& state, const ContentCatalog& catalog);
bool startSaturnSlingshotRun(GameState& state, const ContentCatalog& catalog);
bool jupiterWindowReviewed(const GameState& state, const ContentCatalog& catalog);
const TransferAssistDefinition* availableTransferAssist(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view definitionId = {});
bool canStartTransferAssist(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view definitionId);
bool startTransferAssistRun(
    GameState& state,
    const ContentCatalog& catalog,
    std::string_view definitionId);
bool armTransferAssist(GameState& state, const ContentCatalog& catalog);
bool transferAssistCanContinue(const GameState& state, const ContentCatalog& catalog);
bool canStartJupiterSlingshot(const GameState& state, const ContentCatalog& catalog);
bool startJupiterSlingshotRun(GameState& state, const ContentCatalog& catalog);
bool armJupiterSlingshot(GameState& state);
bool startScenarioFlybyRun(
    GameState& state,
    const ContentCatalog& catalog,
    std::string_view scenarioId,
    std::string_view stepId,
    ScenarioActionKind action = ScenarioActionKind::BeginActivity);
bool canClaimSaturnCourse(const GameState& state);
bool claimSaturnCourse(GameState& state, const ContentCatalog& catalog);
bool acknowledgeSaturnSlingshotFailure(GameState& state);

bool destinationSupportsResearch(const Destination& destination);
bool destinationSupportsSurface(const Destination& destination);
bool destinationAllowsEnemyEncounters(const Destination& destination);
double flybyCreditRewardMinimum(const Destination& destination, FlybyGrade grade);
double flybyCreditRewardMaximum(const Destination& destination, FlybyGrade grade);
int flybyResearchDataReward(FlybyGrade grade);
double orbitCreditReward(const Destination& destination, OrbitGrade grade);
int orbitResearchDataReward(const Destination& destination, OrbitGrade grade);
bool flybyClearsGenericNextRoute(const GameState& state, const ContentCatalog& catalog);
bool bankFlybyRouteClearance(GameState& state, const ContentCatalog& catalog);
bool queueBlockedArrivalFlybyRecovery(GameState& state, const ContentCatalog& catalog);
bool captureArrivalOrbit(GameState& state);
bool shouldOpenArrivalOps(const LaunchOutcome& outcome, const ContentCatalog& catalog);
bool shouldOpenPostArrivalPhases(const LaunchOutcome& outcome, const ContentCatalog& catalog);
bool canRunArrivalFlyby(const GameState& state, const ContentCatalog& catalog);
bool canEnterArrivalOrbit(const GameState& state, const ContentCatalog& catalog);
bool requiresArrivalOrbitBeforeLanding(const GameState& state, const ContentCatalog& catalog);
bool canAttemptArrivalLanding(const GameState& state, const ContentCatalog& catalog);
bool canDepartCapturedArrivalOrbit(const GameState& state, const ContentCatalog& catalog);
bool bankArrivalLandingFlightData(GameState& state, const ContentCatalog& catalog);
int destinationHistoryValue(const std::vector<int>& values, const ContentCatalog& catalog, std::string_view destinationId);
std::string arrivalOperationBlockReason(const GameState& state, const ContentCatalog& catalog, std::string_view operation);
void clearResearchAndExpeditionState(GameState& state);
void startArrivalOps(GameState& state, const LaunchOutcome& outcome);
void completeArrivalFlyby(GameState& state, const ContentCatalog& catalog);
void startArrivalFlybyRun(GameState& state, const ContentCatalog& catalog);
void setFlybyMove(GameState& state, double xAxis, double yAxis);
void updateFlybyRun(GameState& state, double deltaSeconds);
FlybyGrade flybyGrade(const FlybyRunState& flyby);
void applyFlybyReward(GameState& state, const ContentCatalog& catalog, FlybyGrade grade);
void completeFlybyRun(GameState& state, const ContentCatalog& catalog);
void abortFlybyRun(GameState& state);
void abortFlybyRun(GameState& state, const ContentCatalog& catalog);
void acknowledgeFlybyResult(GameState& state);
void completeArrivalOrbit(GameState& state, const ContentCatalog& catalog);
void startArrivalOrbitRun(GameState& state, const ContentCatalog& catalog);
void setOrbitMove(GameState& state, double xAxis, double yAxis);
void updateOrbitRun(GameState& state, double deltaSeconds);
OrbitGrade orbitGrade(const OrbitRunState& orbit);
void applyOrbitReward(GameState& state, const ContentCatalog& catalog, OrbitGrade grade);
void completeOrbitRun(GameState& state, const ContentCatalog& catalog);
void abortOrbitRun(GameState& state);
void generateResearchProjects(GameState& state, const ContentCatalog& catalog, Random& rng);
void addMaterials(MaterialInventory& owned, const MaterialInventory& delta);
int identifiedArtifactCount(const MetaProgress& meta);
int researchFacilityBlueprintBonus(const MetaProgress& meta);
int artifactInsightBlueprintBonus(const MetaProgress& meta);
int researchBlueprintGain(const MetaProgress& meta, const ResearchProject& project);
SurfaceToolEffects surfaceToolEffects(const MetaProgress& meta);
SurfaceCrewEffects surfaceCrewEffects(const GameState& state);
SurfaceSiteProfileEffects surfaceSiteProfileEffects(SurfaceSiteProfile profile);
SurfaceUpgradeEffects surfaceUpgradeEffects(const GameState& state, const ContentCatalog& catalog);
double nominalSurfaceRigFuelCapacity(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId);
bool droneBayUnlocked(const GameState& state);
MaterialInventory droneSlotUpgradeCost(int nextSlot);
MaterialInventory miniDroneAdditionalUnitCost(const MiniDrone& drone);
int ownedMiniDroneCount(const GameState& state, std::string_view droneId);
int equippedMiniDroneCount(const GameState& state, std::string_view droneId);
void ensureDroneBayState(GameState& state, const ContentCatalog& catalog);
bool canUpgradeDroneSlot(const GameState& state);
bool upgradeDroneSlot(GameState& state, const ContentCatalog& catalog);
bool equipMiniDrone(GameState& state, const ContentCatalog& catalog, int index);
bool unequipMiniDroneSlot(GameState& state, const ContentCatalog& catalog, int slotIndex);
MiniDroneLoadoutEffects miniDroneLoadoutEffects(const GameState& state, const ContentCatalog& catalog);
int expeditionDroneRank(const GameState& state, std::string_view droneId);
int runRigUpgradeRank(const GameState& state, std::string_view upgradeId);
double expeditionExperienceThreshold(int level);
void resetExpeditionProgression(SurfaceExpeditionState& expedition);
void resetExpeditionProgression(GameState& state);
ExpeditionExperienceAward awardExpeditionExperience(GameState& state, double amount, Screen returnScreen);
int miningMaterialExperience(const MaterialInventory& materials);
bool generateRunUpgradeOffers(GameState& state, const ContentCatalog& catalog, Random& rng);
bool chooseRunUpgrade(GameState& state, const ContentCatalog& catalog, int index);
Rarity runUpgradeOfferRarity(const GameState& state, const ContentCatalog& catalog, const RunUpgradeOffer& offer);
std::string_view runUpgradeKindLabel(RunUpgradeKind kind);
std::string runUpgradeRankLabel(int rank);
std::string_view surfaceSiteProfileName(SurfaceSiteProfile profile);
std::string_view surfaceSiteProfileDetail(SurfaceSiteProfile profile);
std::string researchOutcomeSummary(const ResearchOutcome& outcome);
std::string surfaceActionSummary(const SurfaceActionOutcome& outcome);
bool surfaceOpsTutorialSurveyComplete(const GameState& state);
bool surfaceOpsTutorialDigComplete(const GameState& state);
bool surfaceOpsTutorialDigUnlocked(const GameState& state);
bool surfaceOpsTutorialMiningUnlocked(const GameState& state);
bool surfaceOpsTutorialNeedsFirstSurveyBank(const GameState& state);
ResearchOutcome completeResearchProject(GameState& state, const ContentCatalog& catalog, int index);
void startSurfaceExpedition(GameState& state, const ContentCatalog& catalog, Random* rng = nullptr);
SurfaceReturnLedger surfaceReturnLedger(const GameState& state, const ContentCatalog& catalog);
SurfaceReturnLedger surfaceReturnLedger(const GameState& state);
double surfaceEnemyEncounterChance(const GameState& state);
SurfaceActionOutcome surveySurfaceSite(GameState& state, Random& rng);
SurfaceActionOutcome mineSurfaceDeposit(GameState& state, Random& rng);
SurfaceActionOutcome pushSurfaceDeeper(GameState& state, Random& rng);
SurfaceReturnSafetyAssessment surfaceReturnSafetyAssessment(
    const GameState& state,
    const ContentCatalog& catalog,
    int absoluteDepth);
int deepestContiguousSurveyedDepth(const GameState& state);
int surfaceSurveyDepthLimit(const GameState& state);
bool surfaceSurveyLimitReached(const GameState& state);
SurfaceDepthCapability surfaceDepthCapability(
    const GameState& state,
    const ContentCatalog& catalog,
    int targetDepth);
std::string surfaceDepthBlockerMessage(const SurfaceDepthCapability& capability);
SurfaceActionOutcome startSurfaceScanRun(GameState& state, Random& rng);
SurfaceActionOutcome pulseSurfaceScan(GameState& state, Random& rng);
SurfaceActionOutcome bankSurfaceScan(GameState& state);
SurfaceActionOutcome abortSurfaceScan(GameState& state);
SurfaceActionOutcome startSurfacePushRun(GameState& state, Random& rng);
SurfaceActionOutcome pushSurfaceDepthStep(GameState& state, Random& rng);
SurfaceActionOutcome bankSurfacePush(GameState& state);
SurfaceActionOutcome extractSurfacePayload(GameState& state);
SurfaceActionOutcome extractSurfacePayload(GameState& state, const ContentCatalog& catalog);

} // namespace rocket
