#pragma once

#include "core/GameTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
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

enum class PoiGuidanceKind {
    None,
    Ship,
    Artifact,
    Boss,
    Story
};

enum class PoiGuidanceDirection {
    WorldTarget,
    Ascend,
    Descend
};

// Presentation-only guidance contract. Renderers consume the dynamic label and
// direction without needing target-specific branches or baked text assets.
struct PoiGuidanceTarget {
    bool active = false;
    PoiGuidanceKind kind = PoiGuidanceKind::None;
    std::string label;
    int targetDepthZone = 0;
    double x = 0.0;
    double y = 0.0;
    PoiGuidanceDirection direction = PoiGuidanceDirection::WorldTarget;
};

struct LaunchAsteroidSnapshot {
    double routeProgress = 0.0;
    double courseOffset = 0.0;
    double radius = 0.10;
    double scale = 1.0;
    double rotation = 0.0;
    double spin = 0.0;
    bool hit = false;
};

inline PoiGuidanceTarget miningPoiGuidanceTarget(
    const MiningRunState& mining,
    double oxygenCapacity,
    double cautionThreshold,
    bool atReturnZone)
{
    if (!mining.active) {
        return {};
    }

    const bool operatorActive =
        mining.operatorMode == MiningOperatorMode::Jetpack &&
        mining.operatorPresent;
    const double oxygenPressure = oxygenCapacity > 0.0
        ? std::clamp(1.0 - mining.oxygenSeconds / oxygenCapacity, 0.0, 1.0)
        : 0.0;
    const double actorPressure = std::clamp(
        1.0 - (operatorActive ? mining.operatorIntegrity : mining.droneHealth),
        0.0,
        1.0);
    const double drillPressure = std::clamp(1.0 - mining.drillIntegrity, 0.0, 1.0);
    if (std::max({oxygenPressure, actorPressure, drillPressure}) >= cautionThreshold) {
        if (mining.depthZone == mining.entryDepthZone && atReturnZone) {
            return {};
        }
        return {
            true,
            PoiGuidanceKind::Ship,
            "SHIP",
            mining.entryDepthZone,
            mining.returnZoneX,
            mining.returnZoneY,
            mining.depthZone > mining.entryDepthZone
                ? PoiGuidanceDirection::Ascend
                : PoiGuidanceDirection::WorldTarget
        };
    }

    const auto recoverable = [](const MiningArtifactObject& artifact) {
        return artifact.present &&
            artifact.revealed &&
            (artifact.state == MiningArtifactState::Embedded ||
                artifact.state == MiningArtifactState::Loose);
    };
    const MiningArtifactObject* artifact = recoverable(mining.artifact)
        ? &mining.artifact
        : nullptr;
    int artifactDepth = mining.depthZone;
    if (artifact == nullptr) {
        for (const MiningDepthLayerState& layer : mining.depthLayers) {
            if (recoverable(layer.artifact)) {
                artifact = &layer.artifact;
                artifactDepth = layer.depthZone;
                break;
            }
        }
    }
    if (artifact == nullptr) {
        return {};
    }

    PoiGuidanceDirection direction = PoiGuidanceDirection::WorldTarget;
    if (artifactDepth < mining.depthZone) {
        direction = PoiGuidanceDirection::Ascend;
    } else if (artifactDepth > mining.depthZone) {
        direction = PoiGuidanceDirection::Descend;
    }
    return {
        true,
        PoiGuidanceKind::Artifact,
        "ARTIFACT",
        artifactDepth,
        artifact->x,
        artifact->y,
        direction
    };
}

// Immutable presentation input assembled from authoritative gameplay state.
// Collection views remain valid only through the synchronous render call.
struct RenderSnapshot {
    Screen screen = Screen::Hangar;
    bool titleScreen = false;
    LaunchResultType lastResult = LaunchResultType::None;
    LaunchFailureCause lastLaunchFailureCause = LaunchFailureCause::None;
    double currentMultiplier = 1.0;
    double targetMultiplier = 1.5;
    double travelProgress = 0.0;
    double heat = 0.0;
    double warning = 0.0;
    double launchThrottle = 0.60;
    double launchFuel = 1.0;
    double launchFuelCapacity = 10.0;
    double launchFuelRemaining = 10.0;
    double launchProjectedFuelReserve = 0.0;
    double launchInsertionReserve = 0.0;
    double launchCourseOffset = 0.0;
    double launchCourseVelocity = 0.0;
    double launchCourseLimit = 1.0;
    double launchMissionTargetProgress = 1.0;
    double launchHullRemaining = 100.0;
    double launchHullMaximum = 100.0;
    double launchHeatFailureProgress = 0.0;
    double launchCourseFailureProgress = 0.0;
    double launchFuelFailureProgress = 0.0;
    bool launchManualControlsEnabled = true;
    bool launchHeatEnabled = false;
    bool launchAsteroidsEnabled = false;
    std::array<LaunchAsteroidSnapshot, 24> launchAsteroids {};
    int launchAsteroidCount = 0;
    double launchImpactFlash = 0.0;
    bool launchLunarImpactActive = false;
    double launchLunarImpactElapsed = 0.0;
    double shipDamage = 0.0;
    int destinationTier = 0;
    int debugActOneCheckpoint = -1;
    ArkCondition arkCondition = ArkCondition::NotFound;
    bool straylightStoryReveal = false;
    bool campaignStoryIntroduction = false;
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
    // Transient presentation envelope for the survivor-style Level Up board.
    // One is the impact frame and zero is fully settled; it is intentionally
    // excluded from save data.
    double levelUpFanfare = 0.0;
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
    bool miningShipPresent = false;
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
    bool miningOperatorPresent = false;
    bool miningOperatorActive = false;
    double miningOperatorX = 0.0;
    double miningOperatorY = 0.0;
    double miningOperatorVelocityX = 0.0;
    double miningOperatorVelocityY = 0.0;
    double miningOperatorAimX = 0.0;
    double miningOperatorAimY = 1.0;
    double miningOperatorThrustX = 0.0;
    double miningOperatorThrustY = 0.0;
    double miningOperatorIntegrity = 1.0;
    double miningOperatorToggleProgress = 0.0;
    double miningOperatorFirePulse = 0.0;
    bool miningRigPresent = true;
    bool miningRigDisabled = false;
    bool miningRigTethered = false;
    bool miningOperatorRigTethered = false;
    bool miningAnchorValid = false;
    double miningAnchorX = 0.0;
    double miningAnchorY = 0.0;
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
    bool miningSwarmActive = false;
    bool miningSwarmAlert = false;
    int miningSwarmWave = 0;
    int miningSwarmDepth = -1;
    double miningSwarmAlertProgress = 0.0;
    bool miningSwarmCacheExposed = false;
    bool miningSwarmCacheClaimed = false;
    bool miningSwarmArtifact = false;
    double miningSwarmCacheX = 0.0;
    double miningSwarmCacheY = 0.0;
    MiningArtifactSnapshot miningArtifact;
    PoiGuidanceTarget miningPoiGuidance;
    std::span<const MiningGateMarker> miningGateMarkers;
    std::span<const MiningCell> miningCells;
    std::span<const MiningEnemy> miningEnemies;
    std::span<const MiningMiniDroneAgent> miningMiniDrones;
    std::span<const DroneFrameModuleAssignment> miningDroneModuleAssignments;
    std::span<const TreasureMark> miningTreasureMarks;
    std::span<const MiningLooseChunk> miningLooseChunks;
    std::span<const MiningProjectileVisual> miningProjectiles;
    std::span<const MiningDamageNumber> miningDamageNumbers;
    std::span<const MiningPickupEvent> miningPickupEvents;
    std::uint64_t miningPickupEventSequence = 0;
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
    double surfaceScanSuccessFanfare = 0.0;
    SurfaceScanPulseGrade surfaceScanLastPulseGrade = SurfaceScanPulseGrade::None;
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
        miningLooseChunks = mining.looseChunks;
        miningProjectiles = mining.combatProjectiles;
        miningDamageNumbers = mining.damageNumbers;
        miningPickupEvents = mining.pickupEvents;
        miningPickupEventSequence = mining.pickupEventSequence;
    }
};

} // namespace rocket
