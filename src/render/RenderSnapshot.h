#pragma once

#include "core/GameTypes.h"

#include <array>
#include <span>
#include <vector>

namespace rocket {

struct MiningArtifactSnapshot {
    bool present = false;
    double x = 0.0;
    double y = 0.0;
    double health = 0.0;
    double maxHealth = 1.0;
    int kind = 0;
    int rewardType = 0;
    int state = 0;
    bool tethered = false;
    bool revealed = false;
    int gateType = 0;
    int gateState = 0;
};

// Immutable presentation input assembled from authoritative gameplay state.
// Collection views remain valid only through the synchronous render call.
struct RenderSnapshot {
    Screen screen = Screen::Hangar;
    bool titleScreen = false;
    LaunchResultType lastResult = LaunchResultType::None;
    double currentMultiplier = 1.0;
    double targetMultiplier = 1.5;
    double travelProgress = 0.0;
    double heat = 0.0;
    double warning = 0.0;
    double shipDamage = 0.0;
    int destinationTier = 0;
    int debugActOneCheckpoint = -1;
    ArkCondition arkCondition = ArkCondition::NotFound;
    bool straylightStoryReveal = false;
    bool frontierTransfer = false;
    bool returningHome = false;
    bool poweredFlight = false;
    bool preflightActive = false;
    double preflightProgress = 1.0;
    double launchShake = 0.0;
    double returnTurnProgress = 1.0;
    std::array<double, 12> telemetry {};
    std::array<double, 12> heatTelemetry {};
    int telemetryCount = 0;
    double animationTime = 0.0;
    int miningWidth = 0;
    int miningHeight = 0;
    double miningDroneX = 0.0;
    double miningDroneY = 0.0;
    double miningTargetX = 0.0;
    double miningTargetY = 0.0;
    double miningHeat = 0.0;
    double miningDrillIntegrity = 1.0;
    double miningDroneHealth = 1.0;
    double miningReturnZoneX = 0.0;
    double miningReturnZoneY = 0.0;
    bool miningAtReturnZone = false;
    double miningLoad = 0.0;
    double miningLoadSpeedMultiplier = 1.0;
    double miningContactIntensity = 0.0;
    double miningScannerPulse = 0.0;
    double miningScannerRadius = 5.5;
    double miningFailurePulse = 0.0;
    double miningRecoilX = 0.0;
    double miningRecoilY = 0.0;
    double miningMoveX = 0.0;
    double miningMoveY = 0.0;
    double miningHullDirX = 0.0;
    double miningHullDirY = 1.0;
    double miningBounce = 0.0;
    double miningBounceRelief = 0.0;
    bool miningTargetDrillable = false;
    bool miningDrilling = false;
    int miningCargo = 0;
    int miningStowedCargo = 0;
    bool miningExtractionActive = false;
    double miningExtractionProgress = 0.0;
    MaterialInventory miningMaterials;
    MaterialInventory miningStowedMaterials;
    MiningArtifactSnapshot miningArtifact;
    std::span<const MiningGateMarker> miningGateMarkers;
    std::span<const MiningCell> miningCells;
    std::span<const MiningEnemy> miningEnemies;
    std::span<const MiningMiniDroneAgent> miningMiniDrones;
    std::span<const MiningProjectileVisual> miningProjectiles;
    std::span<const MiningDamageNumber> miningDamageNumbers;
    bool flybyCompleted = false;
    int flybyZone = 0;
    int flybyResult = 0;
    double flybyShipX = 0.0;
    double flybyShipY = 0.0;
    double flybyVelocityX = 0.0;
    double flybyVelocityY = 0.0;
    double flybyInputY = 0.0;
    double flybyDestinationX = 0.0;
    double flybyDestinationY = 0.0;
    double flybyGoodBand = 0.0;
    double flybyPerfectBand = 0.0;
    std::span<const FlybyTrailPoint> flybyTrailPoints;
    bool orbitCompleted = false;
    int orbitZone = 0;
    int orbitResult = 0;
    double orbitProgress = 0.0;
    double orbitShipX = 0.0;
    double orbitShipY = 0.0;
    double orbitVelocityX = 0.0;
    double orbitVelocityY = 0.0;
    double orbitInputX = 0.0;
    double orbitInputY = 0.0;
    double orbitPlanetRadius = 0.0;
    double orbitTargetRadius = 0.0;
    double orbitGoodBand = 0.0;
    double orbitPerfectBand = 0.0;
    std::span<const FlybyTrailPoint> orbitTrailPoints;
    bool surfaceScanBusted = false;
    int surfaceScanPulses = 0;
    int surfaceScanMaxPulses = 1;
    double surfaceScanSignal = 0.0;
    double surfaceScanInterference = 0.0;
    double surfaceScanBustRisk = 0.0;
    MaterialInventory surfaceScanMaterials;
    int surfaceScanArtifacts = 0;
    std::vector<MiningCellMaterial> surfaceScanPreviewMarkers;
    std::vector<int> surfaceScanPreviewDepthOffsets;
    bool surfacePushBusted = false;
    int surfacePushSteps = 0;
    int surfacePushMaxSteps = 1;
    double surfacePushPressure = 0.0;
    double surfacePushCollapseRisk = 0.0;
    MaterialInventory surfacePushMaterials;
    int surfacePushArtifacts = 0;
    std::vector<MiningCellMaterial> surfacePushRewardMarkers;
    std::vector<int> surfacePushRewardDepthOffsets;
    std::vector<MiningCellMaterial> surfacePushForecastMarkers;
    std::vector<int> surfacePushForecastDepthOffsets;

    void bindMiningFrameViews(const MiningRunState& mining) noexcept
    {
        miningGateMarkers = mining.gate.markers;
        miningCells = mining.terrain.cells;
        miningEnemies = mining.enemies;
        miningMiniDrones = mining.miniDrones;
        miningProjectiles = mining.combatProjectiles;
        miningDamageNumbers = mining.damageNumbers;
    }
};

} // namespace rocket
