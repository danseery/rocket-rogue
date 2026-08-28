#pragma once

#include "core/Content.h"
#include "core/Random.h"

#include <vector>

namespace rocket {

struct HangarOperationPreview {
    int repairAmount = 0;
    double repairCost = 0.0;
    bool repairAvailable = false;
    int trainingGain = 0;
    int trainingStressGain = 0;
    double trainingCost = 0.0;
    bool trainingAvailable = false;
    int restStressRecovery = 0;
    double restCost = 0.0;
    bool restNeeded = false;
    bool restAvailable = false;
    bool emergencyRecruitment = false;
    double recruitCost = 0.0;
    bool recruitAvailable = false;
};

struct PostLaunchCrewStress {
    int baseStress = 0;
    int warningStress = 0;
    int abortStress = 0;
    int relief = 0;
    int total = 0;
};

GameState createNewGame(const ContentCatalog& catalog, std::uint64_t seed);

int moduleOfferCost(Rarity rarity);
int moduleOfferCost(const ShipModule& module);
int crewUpgradeCost(const CrewUpgrade& upgrade);
int launchUpgradeRank(const GameState& state, LaunchUpgradeKind kind);
int surfaceDepthUpgradeRank(const GameState& state, SurfaceDepthUpgradeKind kind);
int surfaceDepthRating(const GameState& state, SurfaceDepthUpgradeKind kind);
int installedRigFuelLoopRank(const GameState& state);
double launchFuelCapacity(const GameState& state);
double pendingLaunchFuelSavings(const GameState& state);
double pendingLaunchInstabilityPenalty(const GameState& state);
const PendingTransferAssist* pendingTransferAssistForDestination(
    const GameState& state,
    std::string_view destinationId);
double pendingLaunchFuelSavingsForDestination(
    const GameState& state,
    std::string_view destinationId);
double pendingLaunchSpeedBoostForDestination(
    const GameState& state,
    std::string_view destinationId);
double pendingLaunchInstabilityPenaltyForDestination(
    const GameState& state,
    std::string_view destinationId);
const RouteLinkDefinition* routeLinkForTransit(
    const ContentCatalog& catalog,
    const RouteTransitState& transit);
RouteTransitState makeRouteTransit(
    const ContentCatalog& catalog,
    std::string_view sourceDestinationId,
    std::string_view targetDestinationId,
    RouteTransitIntent intent);
bool routeTransitIsRecovery(const RouteTransitState& transit);
double calibratedTransferFuelMargin(
    const GameState& state,
    const Destination& destination);
bool jupiterTransferMarginReady(const GameState& state);
bool destinationTransferMarginReady(
    const GameState& state,
    const ContentCatalog& catalog,
    const Destination& destination);
const ShipModule* nextLaunchUpgrade(const GameState& state, const ContentCatalog& catalog, LaunchUpgradeKind kind);
bool launchUpgradeUnlocked(const GameState& state, LaunchUpgradeKind kind, int rank);
bool canInstallLaunchUpgrade(const GameState& state, const ContentCatalog& catalog, LaunchUpgradeKind kind);
bool installLaunchUpgrade(GameState& state, const ContentCatalog& catalog, LaunchUpgradeKind kind);
const ShipModule* nextSurfaceDepthUpgrade(
    const GameState& state,
    const ContentCatalog& catalog,
    SurfaceDepthUpgradeKind kind);
bool surfaceDepthUpgradeUnlocked(
    const GameState& state,
    SurfaceDepthUpgradeKind kind,
    int rank);
bool canInstallSurfaceDepthUpgrade(
    const GameState& state,
    const ContentCatalog& catalog,
    SurfaceDepthUpgradeKind kind);
bool installSurfaceDepthUpgrade(
    GameState& state,
    const ContentCatalog& catalog,
    SurfaceDepthUpgradeKind kind);
const ShipModule* nextRigFuelLoopUpgrade(
    const GameState& state,
    const ContentCatalog& catalog);
bool rigFuelLoopUpgradeUnlocked(const GameState& state, int rank);
bool canInstallRigFuelLoopUpgrade(
    const GameState& state,
    const ContentCatalog& catalog);
bool installRigFuelLoopUpgrade(
    GameState& state,
    const ContentCatalog& catalog);
bool launchTutorialComplete(const GameState& state);
bool launchMissionReady(const GameState& state);
bool launchMissionReady(const GameState& state, const ContentCatalog& catalog);
bool currentDestinationLaunchReady(const GameState& state, const ContentCatalog& catalog);
bool launchStageUsesArrival(LaunchTrainingStage stage);
LaunchMissionKind currentLaunchMissionKind(const GameState& state, const ContentCatalog& catalog);
void syncLaunchTrainingProgress(GameState& state, const ContentCatalog& catalog);
bool canAffordMaterials(const MaterialInventory& owned, const MaterialInventory& cost);
bool canAffordModuleOffer(const GameState& state, const ShipModule& module);
bool spendMaterials(MaterialInventory& owned, const MaterialInventory& cost);
int crewStressStepCount(int stress);
int effectiveTrainingLevel(const Astronaut& astronaut);
double crewNavigationPenaltyFromStress(int stress);
double crewAbortRiskMultiplierFromStress(int stress);
PostLaunchCrewStress postLaunchCrewStress(const LaunchOutcome& outcome, const CrewUpgradeStats& upgrades);
int postLaunchCrewStressGain(const LaunchOutcome& outcome, const CrewUpgradeStats& upgrades);
void startNewExpedition(GameState& state, const ContentCatalog& catalog);
void syncLaunchConfig(GameState& state, const ContentCatalog& catalog);
bool curatedProvingRefitsActive(const GameState& state);
void generateModuleOffers(GameState& state, const ContentCatalog& catalog, Random& rng);
void beginRefitVisit(GameState& state);
double offerRerollCost(const GameState& state);
bool rerollOffers(GameState& state, const ContentCatalog& catalog, Random& rng);
bool buyOffer(GameState& state, const ContentCatalog& catalog, int index);
int repairShipAmount(const GameState& state);
double repairShipCost(const GameState& state);
bool repairShip(GameState& state);
int crewTrainingGain(const GameState& state, const ContentCatalog& catalog);
int crewTrainingStressGain(const GameState& state, const ContentCatalog& catalog);
double crewTrainingCost(const GameState& state, const ContentCatalog& catalog);
double crewRestCost(const GameState& state, const ContentCatalog& catalog);
bool trainCrew(GameState& state, const ContentCatalog& catalog);
bool restCrew(GameState& state, const ContentCatalog& catalog);
int crewRestStressRecovery(const GameState& state, const ContentCatalog& catalog);
double recruitCrewCost(const GameState& state);
HangarOperationPreview hangarOperationPreview(const GameState& state, const ContentCatalog& catalog);
std::vector<const Astronaut*> recruitCandidateTemplates(const GameState& state, const ContentCatalog& catalog, int count = 3);
bool recruitCrew(GameState& state, const ContentCatalog& catalog);
bool recruitCrew(GameState& state, const ContentCatalog& catalog, int candidateIndex);
bool commitToNextFrontier(GameState& state, const ContentCatalog& catalog);
bool bankFrontierReadiness(GameState& state, const ContentCatalog& catalog);
bool arkDiscovered(const GameState& state);
bool hostileSystemActive(const GameState& state);
bool navigationAvailable(const GameState& state);
GameChapter chapterForState(const GameState& state, const ContentCatalog& catalog);
void syncChapterProgress(GameState& state, const ContentCatalog& catalog);
bool migrateLegacyDeepSpaceFrontier(GameState& state, const ContentCatalog& catalog);
void scheduleStoryBriefing(GameState& state, StoryBriefingId briefing, Screen continuation);
bool acknowledgeStoryBriefing(GameState& state, const ContentCatalog& catalog);
std::vector<const Destination*> navigationDestinations(const GameState& state, const ContentCatalog& catalog);
void discoverArk(GameState& state, const ContentCatalog& catalog);
bool performArkJump(GameState& state, const ContentCatalog& catalog);
bool selectNavigationDestination(GameState& state, const ContentCatalog& catalog, int index);
double defaultProvingTarget(const Destination& destination);
void unlockFromBlueprints(GameState& state);
void applyLaunchOutcome(GameState& state, const ContentCatalog& catalog, const LaunchOutcome& outcome);
int frontierReadinessRequired(const GameState& state, const ContentCatalog& catalog);
int frontierReadinessCap(const GameState& state, const ContentCatalog& catalog);
FrontierGateStatus frontierGateStatusForDestination(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId);
FrontierGateStatus frontierGateStatus(const GameState& state, const ContentCatalog& catalog);
bool canCommitToNextFrontier(const GameState& state, const ContentCatalog& catalog);
double missionPressureModifier(const GameState& state, const ContentCatalog& catalog, const Destination& destination);

ModuleStats aggregateShipStats(const GameState& state, const ContentCatalog& catalog);
CrewUpgradeStats aggregateCrewUpgradeStats(const GameState& state, const ContentCatalog& catalog);
Astronaut* activeAstronaut(GameState& state);
const Astronaut* activeAstronaut(const GameState& state);
const Destination& currentDestination(const GameState& state, const ContentCatalog& catalog);
const Destination* nextDestination(const GameState& state, const ContentCatalog& catalog);

} // namespace rocket
