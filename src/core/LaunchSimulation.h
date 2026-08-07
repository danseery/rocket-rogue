#pragma once

#include "core/GameState.h"

#include <array>

namespace rocket {

struct TelemetryIncident {
    double centerMultiplier = 1.0;
    double width = 0.10;
    double heat = 0.0;
    double pressure = 0.0;
    double vibration = 0.0;
    double fuelMix = 0.0;
    double guidance = 0.0;
    double abortRisk = 0.0;
};

struct PreparedLaunch {
    LaunchConfig config;
    ModuleStats stats;
    double crashMultiplier = 1.0;
    double sensorQuality = 0.0;
    double heatRate = 1.0;
    double pressureModifier = 0.0;
    double throttleFactor = 1.0;
    double cutHeatRelief = 0.0;
    double cutVibrationRelief = 0.0;
    double cutGuidancePenalty = 0.0;
    double pressureRelief = 0.0;
    double pressureReliefFailure = 0.0;
    double reliefGuidancePenalty = 0.0;
    double cargoFuelRelief = 0.0;
    double cargoGuidancePenalty = 0.0;
    double cargoVibrationPenalty = 0.0;
    double cargoReturnPenalty = 0.0;
    double slingshotFuelBoost = 0.0;
    double slingshotSpeedBoost = 0.0;
    int overpreparedData = 0;
    double provingPayoutBonus = 0.0;
    double objectiveConfidence = 0.0;
    int crewStressSteps = 0;
    double crewGuidancePenalty = 0.0;
    double crewAbortMultiplier = 1.0;
    std::array<TelemetryIncident, 4> incidents {};
    int incidentCount = 0;
};

struct FlightActionState {
    bool returningHome = false;
    bool cutEnginesActive = false;
    bool pressureReliefOpen = false;
    bool pressureReliefFailed = false;
    bool cargoJettisoned = false;
};

struct LaunchControlInput {
    double steer = 0.0;
    double throttle = 0.0;
    bool enginesCut = false;
    bool pressureReliefOpen = false;
};

struct LaunchFlightState {
    bool active = false;
    bool returningHome = false;
    double travelProgress = 0.0;
    double currentMultiplier = 1.0;
    double peakMultiplier = 1.0;
    double selectedThrottle = 0.60;
    double burnRatePerSecond = 0.0;
    double fuelCapacity = 1.0;
    double fuelRemaining = 1.0;
    double heat = 0.18;
    double pressure = 0.18;
    double courseOffset = 0.0;
    double courseVelocity = 0.0;
    double valveToggleCooldown = 0.0;
    double heatFailureSeconds = 0.0;
    double pressureFailureSeconds = 0.0;
    double courseFailureSeconds = 0.0;
    double fuelFailureSeconds = 0.0;
    double minimumSafetyMargin = 1.0;
    double incidentStrength = 0.0;
    double incidentWarningSeconds = 0.0;
    int forecastIncidentIndex = -1;
    LaunchFailureCause failureCause = LaunchFailureCause::None;
};

struct LaunchFlightStep {
    bool reachedDestination = false;
    bool reachedHome = false;
    bool failed = false;
    LaunchFailureCause failureCause = LaunchFailureCause::None;
};

struct LaunchResolutionContext {
    bool pilotedFlight = false;
    LaunchFailureCause failureCause = LaunchFailureCause::None;
    double minimumSafetyMargin = 1.0;
};

PreparedLaunch prepareLaunch(const GameState& state, const ContentCatalog& catalog, Random& rng);
PreparedLaunch withCutEngines(const PreparedLaunch& launch);
PreparedLaunch withPressureRelief(const PreparedLaunch& launch, bool failed);
PreparedLaunch withJettisonedCargo(const PreparedLaunch& launch);
PreparedLaunch applyFlightActions(const PreparedLaunch& launch, const FlightActionState& actions);
LaunchFlightState beginLaunchFlight(const PreparedLaunch& launch, const Destination& destination);
void beginLaunchReturn(LaunchFlightState& flight);
double launchPressureGraceSeconds(const PreparedLaunch& launch);
double launchCourseLimit(const PreparedLaunch& launch);
double launchIncidentWarningLeadSeconds(const PreparedLaunch& launch);
LaunchFlightStep updateLaunchFlight(
    LaunchFlightState& flight,
    const PreparedLaunch& launch,
    const Destination& destination,
    const LaunchControlInput& input,
    double deltaSeconds);
TelemetryEvent launchTelemetryAt(const PreparedLaunch& launch, const LaunchFlightState& flight);
double burnMultiplierDelta(const PreparedLaunch& launch, const Destination& destination, double elapsedSeconds, double deltaSeconds);
double returnTelemetryMultiplier(double commitMultiplier, double crashMultiplier, double returnElapsed, double returnDuration);
double returnHomeRisk(const PreparedLaunch& launch, const ContentCatalog& catalog, const GameState& state, double burnMultiplier);
LaunchOutcome resolveLaunch(
    const PreparedLaunch& launch,
    const ContentCatalog& catalog,
    const GameState& state,
    double burnMultiplier,
    RecoveryMethod method,
    Random& rng,
    LaunchResolutionContext resolution = {});
LaunchOutcome simulateLaunchToTarget(const GameState& state, const ContentCatalog& catalog, Random& rng);
TelemetryEvent telemetryAt(const PreparedLaunch& launch, double multiplier);

} // namespace rocket
