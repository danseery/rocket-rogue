#pragma once

#include "core/Content.h"
#include "core/GameState.h"
#include "core/Random.h"
#include "core/ResearchSystem.h"
#include "core/Tuning.h"

#include <string_view>

namespace rocket {

struct MiningDrillStats {
    double power = 0.0;
    double speed = 0.0;
    double scannerRadius = 0.0;
    double oxygenSeconds = 0.0;
    double integrityRelief = 0.0;
    double extractionRiskRelief = 0.0;
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

std::string_view miningMaterialName(MiningCellMaterial material);
std::string_view miningCellFeatureName(MiningCellFeature feature);
std::string_view miningEnemyTypeName(MiningEnemyType enemy);
std::string_view miningElementalAffinityName(MiningElementalAffinity affinity);
MiningEnemy createMiningEnemy(MiningEnemyType type, MiningCellFeature sourceFeature, double x, double y, MiningElementalAffinity affinity = MiningElementalAffinity::None);
MiningEnemy createMiningEnemySpawner(double x, double y, double health, MiningEnemyType spawnType, int maxSpawns, double spawnIntervalSeconds, MiningElementalAffinity affinity = MiningElementalAffinity::None);
bool miningMaterialSolid(MiningCellMaterial material);
double miningMaterialToughness(MiningCellMaterial material, int depthZone);
MiningCell* miningCellAt(MiningTerrain& terrain, int x, int y);
const MiningCell* miningCellAt(const MiningTerrain& terrain, int x, int y);
MiningDrillStats miningDrillStats(const GameState& state, const ContentCatalog& catalog);
int miningCarriedCargo(const MiningRunState& mining);
int miningBankedCargo(const MiningRunState& mining);
bool miningAtReturnZone(const MiningRunState& mining);
MiningLoadStats miningLoadStats(const GameState& state, const ContentCatalog& catalog);
int miningDrillRepairCost(const MiningRunState& mining);
int miningDroneRepairCost(const MiningRunState& mining);
bool repairMiningDrill(GameState& state);
bool repairMiningDrone(GameState& state);
MiningTerrain generateMiningTerrain(const GameState& state, const Destination& destination, SurfaceSiteProfile profile, int depthZone, int width = tuning::mining::terrainWidth, int height = tuning::mining::terrainHeight);
SurfaceActionOutcome startMiningRun(GameState& state, const ContentCatalog& catalog);
void setMiningMove(GameState& state, double xAxis, double yAxis);
void setMiningAim(GameState& state, double normalizedX, double normalizedY);
void setMiningDrilling(GameState& state, bool drilling);
void toggleMiningTether(GameState& state);
void pulseMiningScanner(GameState& state, const ContentCatalog& catalog);
void updateMiningRun(GameState& state, const ContentCatalog& catalog, double deltaSeconds);
SurfaceActionOutcome finishMiningRun(GameState& state, const ContentCatalog& catalog, bool abort);

} // namespace rocket
