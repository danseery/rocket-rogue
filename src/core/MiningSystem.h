#pragma once

#include "core/Content.h"
#include "core/GameState.h"
#include "core/MiningProgression.h"
#include "core/Random.h"
#include "core/ResearchSystem.h"
#include "core/Tuning.h"

#include <string_view>
#include <string>

namespace rocket {

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

struct MiningLoadStats {
    double currentLoad = 0.0;
    double freeBuffer = tuning::mining::baseCarryBufferCargo;
    double burden = 0.0;
    double speedMultiplier = 1.0;
    double fuelConsumptionMultiplier = 1.0;
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

// The physical object selected by the shared T/Y tether action. This is
// intentionally independent of presentation so the command label, input, and
// simulation cannot disagree about what is closest.
enum class MiningTetherTarget {
    None,
    Artifact,
    MiningRig
};

enum class MiningTetherBlocker {
    None,
    NoTarget,
    ArtifactUnexposed,
    ArtifactOutOfRange,
    ArtifactGateLocked,
    RigDifferentDepth,
    RigOutOfRange
};

struct MiningTetherTargetResolution {
    MiningTetherTarget target = MiningTetherTarget::None;
    MiningTetherBlocker blocker = MiningTetherBlocker::NoTarget;
    bool artifactRecoverable = false;
    bool artifactExposed = false;
    bool artifactInRange = false;
    bool rigAvailable = false;
    bool rigInRange = false;
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

} // namespace rocket
