#pragma once

#include "core/GameState.h"

#include <array>
#include <cstddef>

namespace rocket {

struct LaunchAsteroidState {
    double routeProgress = 0.0;
    double courseOffset = 0.0;
    double radius = 0.10;
    double scale = 1.0;
    double rotation = 0.0;
    double spin = 0.0;
};

using LaunchAsteroid = LaunchAsteroidState;

struct PreparedLaunch {
    LaunchConfig config;

    // Legacy outcome records still expose a failure point. Live piloted
    // survival never consults this value.
    double crashMultiplier = 0.0;
    double slingshotFuelSavings = 0.0;
    double slingshotSpeedBoost = 0.0;
    double slingshotInstabilityPenalty = 0.0;
    double slingshotCourseOffset = 0.0;
    std::string transferAssistId;
    // A recovery leg still uses the forward link's encounter/heat profile
    // even though its physical target is the prior staging body.
    std::string routeProfileDestinationId;
    int overpreparedData = 0;
    double provingPayoutBonus = 0.0;

    double fuelCapacity = 10.0;
    double cruiseFuelCost = 10.0;
    double arrivalReserveFuel = 0.0;
    int flightControlRank = 0;
    int coolingRank = 0;
    int hullRank = 0;
    int existingShipDamage = 0;
    bool manualControlsEnabled = true;
    bool heatEnabled = false;
    bool asteroidsEnabled = false;
    bool trainingMission = false;
    bool orbitRequired = false;

    double controlChaos = 0.0;
    double controlSteeringResponseVariation = 0.0;
    std::array<double, launchControlKickCapacity> controlKickDirections {};
    int controlKickCount = 0;

    std::array<LaunchAsteroid, launchAsteroidCapacity> asteroids {};
    int asteroidCount = 0;
};

struct FlightActionState {
    bool returningHome = false;
    bool cutEnginesActive = false;
};

struct LaunchFlightStep {
    std::string landingZoneId;
    bool reachedDestination = false;
    bool reachedHome = false;
    bool failed = false;
    bool asteroidHit = false;
    bool trainingRescue = false;
    int hullDamageTaken = 0;
    LaunchFailureCause failureCause = LaunchFailureCause::None;
    bool orbitCaptured = false;
    bool safeTouchdown = false;
    bool hardTouchdown = false;
    bool flyby = false;
    bool surfaceImpact = false;
};

// Both opening calibration flights teach the same readable fuel-return
// windows. The survey adds payout consequences; the controls flight keeps the
// warnings so its new steering lesson does not change the established fuel UI.
enum class CalibrationFuelWarning {
    None,
    Approaching,
    TurnAround,
    Critical,
};

struct LaunchResolutionContext {
    bool pilotedFlight = false;
    LaunchFailureCause failureCause = LaunchFailureCause::None;
    double minimumSafetyMargin = 1.0;
    int hullDamageTaken = 0;
    FuelSurveyReturnTiming fuelSurveyReturnTiming = FuelSurveyReturnTiming::Unqualified;
};

PreparedLaunch prepareLaunch(const GameState& state, const ContentCatalog& catalog, Random& rng);
double launchFuelCapacityForRank(int rank);
double launchPoweredFuelCost(
    double cruiseFuelCost,
    double throttle,
    double slingshotFuelSavings = 0.0);
double launchCruiseFuelCostForTier(int tier);
double launchFuelUseMultiplier(double throttle);
double launchControlChaosForRank(int rank);
double launchPoweredHeatMultiplierForRank(int rank);
double launchEngineOffCoolingForRank(int rank);
double launchHullImpactMultiplierForRank(int rank);
double launchAsteroidRowProgress(int row);
double launchAsteroidLaneOffset(int lane);
double launchAsteroidImpactDamage(int hullRank, double asteroidScale);
FlightRunState beginLaunchFlight(const PreparedLaunch& launch, const Destination& destination);
void beginLaunchReturn(FlightRunState& flight);
CalibrationFuelWarning calibrationFuelWarning(const PreparedLaunch& launch, const FlightRunState& flight);
double launchCourseLimit(const PreparedLaunch& launch);
LaunchFlightStep updateLaunchFlight(
    FlightRunState& flight,
    const PreparedLaunch& launch,
    const Destination& destination,
    const FlightInput& input,
    double deltaSeconds,
    const MiningRunState* landingSite = nullptr);
TelemetryEvent launchTelemetryAt(const PreparedLaunch& launch, const FlightRunState& flight);
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
