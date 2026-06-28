#pragma once

#include "core/Content.h"
#include "core/GameState.h"
#include "core/Random.h"

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
    double extractionRiskRelief = 0.0;
    double cargoRiskRelief = 0.0;
    double enemyEncounterRelief = 0.0;
};

struct SurfaceCrewEffects {
    int supplyBonus = 0;
    int surveyCommonBonus = 0;
    int mineCommonBonus = 0;
    double mineRareChanceBonus = 0.0;
    double hazardRelief = 0.0;
    double extractionRiskRelief = 0.0;
    double artifactChanceBonus = 0.0;
    std::string summary;
};

struct SurfaceSiteProfileEffects {
    int supplyBonus = 0;
    int surveyCommonBonus = 0;
    int mineCommonBonus = 0;
    double mineRareChanceBonus = 0.0;
    double hazardDelta = 0.0;
    double extractionRiskDelta = 0.0;
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
    double extractionRiskRelief = 0.0;
    std::vector<std::string> names;
};

struct MiniDroneLoadoutEffects {
    double passiveMiningRate = 0.0;
    double oxygenSeconds = 0.0;
    double scannerRadius = 0.0;
    double drillIntegrityRelief = 0.0;
    double hardRockBounceRelief = 0.0;
    double extractionRiskRelief = 0.0;
    double enemyEncounterRelief = 0.0;
    std::vector<std::string> names;
};

struct SurfaceActionOutcome {
    bool applied = false;
    std::string message;
    int supplyDelta = 0;
    int cargoDelta = 0;
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
    double extractionRisk = 0.0;
    double extractionRiskDelta = 0.0;
};

bool destinationSupportsResearch(const Destination& destination);
bool destinationSupportsSurface(const Destination& destination);
bool destinationAllowsEnemyEncounters(const Destination& destination);
bool shouldOpenArrivalOps(const LaunchOutcome& outcome, const ContentCatalog& catalog);
bool shouldOpenPostArrivalPhases(const LaunchOutcome& outcome, const ContentCatalog& catalog);
bool canRunArrivalFlyby(const GameState& state, const ContentCatalog& catalog);
bool canEnterArrivalOrbit(const GameState& state, const ContentCatalog& catalog);
bool canAttemptArrivalLanding(const GameState& state, const ContentCatalog& catalog);
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
void acknowledgeFlybyResult(GameState& state);
void completeArrivalOrbit(GameState& state, const ContentCatalog& catalog);
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
bool droneBayUnlocked(const GameState& state);
MaterialInventory droneSlotUpgradeCost(int nextSlot);
void ensureDroneBayState(GameState& state, const ContentCatalog& catalog);
bool canUpgradeDroneSlot(const GameState& state);
bool upgradeDroneSlot(GameState& state, const ContentCatalog& catalog);
bool equipMiniDrone(GameState& state, const ContentCatalog& catalog, int index);
MiniDroneLoadoutEffects miniDroneLoadoutEffects(const GameState& state, const ContentCatalog& catalog);
std::string_view surfaceSiteProfileName(SurfaceSiteProfile profile);
std::string_view surfaceSiteProfileDetail(SurfaceSiteProfile profile);
std::string researchOutcomeSummary(const ResearchOutcome& outcome);
std::string surfaceActionSummary(const SurfaceActionOutcome& outcome);
ResearchOutcome completeResearchProject(GameState& state, const ContentCatalog& catalog, int index);
void startSurfaceExpedition(GameState& state, const ContentCatalog& catalog, Random* rng = nullptr);
void generateSurfaceUpgradeOffers(GameState& state, const ContentCatalog& catalog, Random& rng);
bool rerollSurfaceUpgradeOffers(GameState& state, const ContentCatalog& catalog, Random& rng);
bool chooseSurfaceUpgrade(GameState& state, const ContentCatalog& catalog, int index);
double surfaceExtractionRisk(const GameState& state);
double surfaceEnemyEncounterChance(const GameState& state);
SurfaceActionOutcome surveySurfaceSite(GameState& state, Random& rng);
SurfaceActionOutcome mineSurfaceDeposit(GameState& state, Random& rng);
SurfaceActionOutcome pushSurfaceDeeper(GameState& state, Random& rng);
SurfaceActionOutcome extractSurfacePayload(GameState& state, Random& rng);

} // namespace rocket
