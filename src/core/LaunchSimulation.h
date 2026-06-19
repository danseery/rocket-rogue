#pragma once

#include "core/GameState.h"

namespace rocket {

struct PreparedLaunch {
    LaunchConfig config;
    ModuleStats stats;
    double crashMultiplier = 1.0;
    double sensorQuality = 0.0;
    double heatRate = 1.0;
};

PreparedLaunch prepareLaunch(const GameState& state, const ContentCatalog& catalog, Random& rng);
double returnHomeRisk(const PreparedLaunch& launch, const ContentCatalog& catalog, const GameState& state, double burnMultiplier);
LaunchOutcome resolveLaunch(const PreparedLaunch& launch, const ContentCatalog& catalog, const GameState& state, double burnMultiplier, RecoveryMethod method, Random& rng);
LaunchOutcome simulateLaunchToTarget(const GameState& state, const ContentCatalog& catalog, Random& rng);
TelemetryEvent telemetryAt(const PreparedLaunch& launch, double multiplier);

} // namespace rocket
