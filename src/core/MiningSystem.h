#pragma once

#include "core/Content.h"
#include "core/GameState.h"
#include "core/MiningProgression.h"
#include "core/Random.h"
#include "core/ResearchSystem.h"
#include "core/Tuning.h"

#include <string_view>
#include <string>
#include <vector>

namespace rocket {

namespace orbital_laser {
inline constexpr double secondsPerLayer = 3.0;
// Twice the original five-cell shaft. Even-width cuts sit between two cells.
inline constexpr int shaftWidthCells = 10;
inline constexpr int shaftLeftCells = shaftWidthCells / 2;
inline constexpr int shaftRightCells = shaftWidthCells - shaftLeftCells - 1;
inline constexpr double shaftCenterOffset = 0.5 + (shaftRightCells - shaftLeftCells) * 0.5;
}

struct MiningDrillStats {
    double power = 0.0;
    double speed = 0.0;
    double scannerRadius = 0.0;
    double oxygenSeconds = 0.0;
    double integrityRelief = 0.0;
    double passiveDroneMiningRate = 0.0;
    double hardRockBounceRelief = 0.0;
    double rareYieldChance = 0.0;
    double oreYieldChance = 0.0;
    double heatRiseScale = 1.0;
    double heatCoolingPerSecond = 0.0;
    double storage = 0.0;
    double engineEfficiency = 0.0;
    double artifactTowEfficiency = 0.0;
    int terrainWidth = 0;
    int terrainHeight = 0;
};

enum class RigLoadBand {
    Light,
    Standard,
    Laden,
    Packrat,
    Full
};

struct MiningActorHull {
    double halfLength = tuning::mining::rigHullHalfLengthCells;
    double halfWidth = tuning::mining::rigHullHalfWidthCells;
};

struct MiningLoadStats {
    double currentLoad = 0.0;
    double capacity = tuning::mining::rigCargoCapacityMass;
    double freeBuffer = tuning::mining::baseCarryBufferCargo;
    double burden = 0.0;
    double speedMultiplier = 1.0;
    double fuelConsumptionMultiplier = 1.0;
    RigLoadBand band = RigLoadBand::Light;
    bool full = false;
};

// A single normalized source for discovery/luck effects. New Luck upgrades
// extend this profile instead of coupling each encounter to equipment IDs.
struct MiningLootLuckProfile {
    double swarmArtifactDropBonus = 0.0;
};

struct MiningSwarmPreview {
    bool available = false;
    int depthZone = -1;
    double artifactChance = 0.0;
    std::uint64_t seed = 0;
};

// A surface landing is built before it is visible so the descent renderer and
// the eventual Mining run consume the exact same deterministic world.  The
// prepared value is deliberately session-owned by RocketGameApp; none of this
// cinematic staging is part of the save contract.
struct SurfaceLandingBuildRequest {
    std::string destinationId;
    int landingOrdinal = 0;
    std::uint64_t siteSeed = 0;
    std::string scenarioId;
    std::string scenarioStepId;
    std::string miningSiteDefinitionId;
    std::string zoneId = "zone_1";
};

struct PreparedSurfaceLanding {
    std::uint64_t preparationKey = 0;
    SurfaceLandingBuildRequest request;
    PlanetaryExpeditionState expeditionTemplate;
    MiningRunState miningTemplate;
    int destinationIndex = -1;
    int landingOrdinal = 0;
    std::vector<MiningSiteProgress> miningSites;
    std::vector<PostSolarSystemRoster> postSolarSystemRosters;
    bool valid = false;
    std::string error;
    // Session-only orbital preparation. Modified terrain already uses the
    // existing Mining layer persistence when the landing is committed.
    std::vector<OrbitalSurveyLayer> surveyLayers;
    int surveyedDepth = -1;
    int laserDepth = 0;
    int laserRow = 0;
    int shaftX = 0;
    double laserRowWork = 0.0;
    bool laserBlocked = false;
    bool laserComplete = false;
};

bool prepareOrbitalSurvey(const GameState&, const ContentCatalog&, PreparedSurfaceLanding&, int depth);
std::uint64_t surfaceLandingBuildKey(const GameState&, const ContentCatalog&, const SurfaceLandingBuildRequest&);
void excavateOrbitalShaft(PreparedSurfaceLanding&, int maximumDepth, double seconds);

PreparedSurfaceLanding prepareSurfaceLanding(
    const GameState& state,
    const ContentCatalog& catalog,
    const SurfaceLandingBuildRequest& request);
bool preparedSurfaceLandingCurrent(
    const GameState& state,
    const ContentCatalog& catalog,
    const PreparedSurfaceLanding& prepared);
bool commitPreparedSurfaceLanding(
    GameState& state,
    PreparedSurfaceLanding&& prepared,
    double actualTransferFuel);
bool surfaceLandingStaging(const MiningRunState& mining, double shipX, double shipY,
    double& rigX, double& rigY);
bool positionSurfaceLandingTeam(MiningRunState& mining, double shipX, double shipY);

// A read-only, site-wide Cartesian projection of authoritative cached layers.
// The flattened terrain is a session cache, never a second saved world.
struct LandingLayerPlacement {
    int depth = 0;
    int topRow = 0;
    int height = 0;
};
struct LandingSiteView {
    MiningRunState world;
    std::vector<LandingLayerPlacement> layers;
    int depthAt(double row) const;
    int topRow(int depth) const;
};
bool prepareLandingLayers(const GameState&, const ContentCatalog&, MiningRunState&, int throughDepth);
bool activateLandingLayer(MiningRunState&, int depth);
LandingSiteView buildLandingSiteView(const MiningRunState&);
bool positionSurfaceLandingTeam(MiningRunState&, const LandingSiteView&, double shipX, double siteRow);
bool revealLandingSurroundings(MiningRunState&, const LandingSiteView&, double gridX, double siteRow);

// The physical object selected by the shared T/Y tether action. This is
// intentionally independent of presentation so the command label, input, and
// simulation cannot disagree about what is closest.
enum class MiningTetherTarget {
    None,
    Artifact,
    MiningRig,
    FuelCell
};

enum class MiningTetherBlocker {
    None,
    NoTarget,
    ArtifactUnexposed,
    ArtifactOutOfRange,
    SuitRequired,
    ArtifactGateLocked,
    RigDifferentDepth,
    RigOutOfRange,
    FuelCellOutOfRange
};

struct MiningTetherTargetResolution {
    MiningTetherTarget target = MiningTetherTarget::None;
    MiningTetherBlocker blocker = MiningTetherBlocker::NoTarget;
    bool artifactRecoverable = false;
    bool artifactExposed = false;
    bool artifactInRange = false;
    bool rigAvailable = false;
    bool rigInRange = false;
    std::uint64_t fuelCellId = 0;
    bool fuelCellInRange = false;
    double fuelCellDistance = 0.0;
    double artifactDistance = 0.0;
    double rigDistance = 0.0;
};

std::string_view miningMaterialName(MiningCellMaterial material);
std::string_view miningCellFeatureName(MiningCellFeature feature);
std::string_view miningEnemyTypeName(MiningEnemyType enemy);
std::string_view miningElementalAffinityName(MiningElementalAffinity affinity);
MiningEnemy createMiningEnemy(MiningEnemyType type, MiningCellFeature sourceFeature, double x, double y, MiningElementalAffinity affinity = MiningElementalAffinity::None);
MiningEnemy createMiningEnemySpawner(double x, double y, double health, MiningEnemyType spawnType, int maxSpawns, double spawnIntervalSeconds, MiningElementalAffinity affinity = MiningElementalAffinity::None);
bool miningMaterialSolid(MiningCellMaterial material);
MaterialInventory applyMiningTreasureMultiplier(MaterialInventory gain, MiningCellMaterial material, int multiplier);
// Deterministic secondary-module tuning used by the live mining simulation and behavioral tests.
double secondaryModuleValue(DroneModuleKind module, int rank);
int secondaryModuleSecondaryHits(DroneModuleKind module, int rank);
int miningEnemyDefeatExperience(const MiningEnemy& enemy, int difficulty);
int miningHazardTreatmentExperience(MiningElementalAffinity affinity);
int miningSwarmWaveExperience(int wave, int difficulty);
double miningMaterialToughness(MiningCellMaterial material, int depthZone);
MiningCell* miningCellAt(MiningTerrain& terrain, int x, int y);
const MiningCell* miningCellAt(const MiningTerrain& terrain, int x, int y);
MiningDrillStats miningDrillStats(const GameState& state, const ContentCatalog& catalog);
MiningDrillStats miningOperatorDrillStats();
std::string_view rigLoadBandName(RigLoadBand band);
int miningRigCargoCapacityMass();
int miningRigCargoAvailableMass(const MiningRunState& mining);
// These are the single source of truth for the currently controlled actor's
// oxygen readout. UI warnings and guidance use them rather than duplicating
// the rig/EVA branch.
double miningActiveOxygenSeconds(const MiningRunState& mining);
double miningActiveOxygenCapacity(const GameState& state, const ContentCatalog& catalog);
MiningLootLuckProfile miningLootLuckProfile(
    const GameState& state,
    const ContentCatalog& catalog,
    SurfaceSiteProfile profile);
MiningSwarmPreview miningSwarmPreview(
    const GameState& state,
    const ContentCatalog& catalog,
    const MiningArenaRules& rules,
    int startDepth,
    bool authoredSite = false);
MiningCapabilityProfile miningCapabilityProfile(const GameState& state, const ContentCatalog& catalog);
bool miningCapabilityReadyForGate(const MiningCapabilityProfile& profile, const MiningGateDefinition& gate);
std::string miningGateCapabilityStatus(const MiningCapabilityProfile& profile, const MiningGateDefinition& gate);
double miningRigFuelCycleSeconds(const GameState& state);
double miningRigFuelConsumptionPerSecond(
    const GameState& state,
    double loadMultiplier = 1.0);
int miningCarriedCargo(const MiningRunState& mining);
int miningBankedCargo(const MiningRunState& mining);
bool miningAtReturnZone(const MiningRunState& mining);
bool miningRigAtReturnZone(const MiningRunState& mining);
struct MiningDroneRecoveryStatus {
    int outstandingDrones = 0;
    int outstandingCargoMass = 0;
    bool recallInProgress = false;
};
MiningDroneRecoveryStatus miningDroneRecoveryStatus(const MiningRunState& mining);
bool requestMiningDroneRecall(GameState& state);
MiningLoadStats miningLoadStats(const GameState& state, const ContentCatalog& catalog);
int miningDrillRepairCost(const MiningRunState& mining);
int miningDroneRepairCost(const MiningRunState& mining);
bool repairMiningDrill(GameState& state);
bool repairMiningDrone(GameState& state);
MiningTerrain generateMiningTerrain(const GameState& state, const Destination& destination, SurfaceSiteProfile profile, int depthZone, int width = tuning::mining::terrainWidth, int height = tuning::mining::terrainHeight);
SurfaceActionOutcome startMiningRun(GameState& state, const ContentCatalog& catalog);
SurfaceActionOutcome startMiningRun(
    GameState& state,
    const ContentCatalog& catalog,
    const MiningArenaRequest& request,
    bool progressionCreditEligible);
bool enterMiningSwarmArenaForDebug(GameState& state, const ContentCatalog& catalog);
void setMiningMove(GameState& state, double xAxis, double yAxis);
void setMiningAim(GameState& state, double normalizedX, double normalizedY);
void setMiningDrilling(GameState& state, bool drilling);
void setMiningFire(GameState& state, bool firing);
void setMiningOperatorToggleProgress(GameState& state, double progress);
bool toggleMiningOperator(GameState& state);
MiningTetherTargetResolution resolveMiningTetherTarget(const MiningRunState& mining);
void toggleMiningTether(GameState& state);
void pulseMiningScanner(GameState& state, const ContentCatalog& catalog);
bool repairMiningOperator(GameState& state);
void updateMiningRun(GameState& state, const ContentCatalog& catalog, double deltaSeconds);
SurfaceActionOutcome finishMiningRun(GameState& state, const ContentCatalog& catalog, bool abort);
bool bankMiningPayloadAtShip(GameState& state, const ContentCatalog& catalog);

} // namespace rocket
