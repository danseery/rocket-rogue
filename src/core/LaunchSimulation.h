#pragma once

#include "core/GameState.h"

#include <array>
#include <cstddef>

namespace rocket {

inline constexpr std::size_t launchControlKickCapacity = 16;
inline constexpr std::size_t launchAsteroidCapacity = 24;

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
    double slingshotFuelBoost = 0.0;
    double slingshotSpeedBoost = 0.0;
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

struct LaunchControlInput {
    double steer = 0.0;
    double throttle = 0.0;
    bool enginesCut = false;
};

struct LaunchFlightState {
    bool active = false;
    bool returningHome = false;
    double travelProgress = 0.0;
    double previousTravelProgress = 0.0;
    double currentMultiplier = 1.0;
    double peakMultiplier = 1.0;
    double selectedThrottle = 0.60;
    double burnRatePerSecond = 0.0;
    double travelVelocity = 0.0;
    double fuelCapacity = 10.0;
    double fuelRemaining = 10.0;
    double projectedFuelRequired = 0.0;
    double projectedFuelReserve = 10.0;
    double heat = 0.0;
    double courseOffset = 0.0;
    double courseVelocity = 0.0;
    bool throttleInputActive = false;
    double throttleAtLastKick = 0.60;
    double throttleKickCooldownSeconds = 0.0;
    int nextControlKickIndex = 0;
    double heatFailureSeconds = 0.0;
    double courseFailureSeconds = 0.0;
    double fuelFailureSeconds = 0.0;
    double minimumSafetyMargin = 1.0;
    double hullMaximum = 100.0;
    double hullRemaining = 100.0;
    int hullDamageTaken = 0;
    double asteroidInvulnerabilitySeconds = 0.0;
    std::array<bool, launchAsteroidCapacity> asteroidHit {};
    LaunchFailureCause failureCause = LaunchFailureCause::None;
};

struct LaunchFlightStep {
    bool reachedDestination = false;
    bool reachedHome = false;
    bool failed = false;
    bool asteroidHit = false;
    bool trainingRescue = false;
    int hullDamageTaken = 0;
    LaunchFailureCause failureCause = LaunchFailureCause::None;
};

struct LaunchResolutionContext {
    bool pilotedFlight = false;
    LaunchFailureCause failureCause = LaunchFailureCause::None;
    double minimumSafetyMargin = 1.0;
    int hullDamageTaken = 0;
};

PreparedLaunch prepareLaunch(const GameState& state, const ContentCatalog& catalog, Random& rng);
double launchFuelCapacityForRank(int rank, double oneLaunchBoost = 0.0);
double launchCruiseFuelCostForTier(int tier);
double launchFuelUseMultiplier(double throttle);
double launchControlChaosForRank(int rank);
double launchPoweredHeatMultiplierForRank(int rank);
double launchEngineOffCoolingForRank(int rank);
double launchHullImpactMultiplierForRank(int rank);
double launchAsteroidRowProgress(int row);
double launchAsteroidLaneOffset(int lane);
double launchAsteroidImpactDamage(int hullRank, double asteroidScale);
LaunchFlightState beginLaunchFlight(const PreparedLaunch& launch, const Destination& destination);
void beginLaunchReturn(LaunchFlightState& flight);
double launchCourseLimit(const PreparedLaunch& launch);
LaunchFlightStep updateLaunchFlight(
    LaunchFlightState& flight,
    const PreparedLaunch& launch,
    const Destination& destination,
    const LaunchControlInput& input,
    double deltaSeconds);
TelemetryEvent launchTelemetryAt(const PreparedLaunch& launch, const LaunchFlightState& flight);
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
