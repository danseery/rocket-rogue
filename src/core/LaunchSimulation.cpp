#include "core/LaunchSimulation.h"

#include "core/GameMath.h"
#include "core/GameText.h"
#include "core/Tuning.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace rocket {

namespace {

constexpr double asteroidRouteJitter = 0.012;
constexpr double asteroidCourseJitter = 0.035;
constexpr double asteroidSpinMinimum = 0.14;
constexpr double asteroidSpinMaximum = 0.38;
constexpr double asteroidCourseImpulse = 0.24;

double crewEscapeBonus(const GameState& state, const ContentCatalog& catalog)
{
    const Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        return 0.0;
    }

    double bonus = static_cast<double>(astronaut->training) * tuning::crew::escapeBonusPerTraining;
    if (astronaut->trait == tuning::traits::improvesEjectionOdds) {
        bonus += tuning::traits::improvesEjectionOddsEscapeBonus *
            (1.0 + std::max(0.0, aggregateCrewUpgradeStats(state, catalog).traitModifier));
    }
    return bonus;
}

bool isTrainingMission(LaunchMissionKind kind)
{
    return kind == LaunchMissionKind::FuelCalibration ||
        kind == LaunchMissionKind::FlightControlsCalibration ||
        kind == LaunchMissionKind::ThermalManagement ||
        kind == LaunchMissionKind::AsteroidBelt;
}

bool missionUsesHeat(LaunchMissionKind kind, const Destination& destination)
{
    return kind == LaunchMissionKind::ThermalManagement ||
        kind == LaunchMissionKind::AsteroidBelt ||
        (kind == LaunchMissionKind::Standard && destination.tier >= 2);
}

bool missionUsesAsteroids(LaunchMissionKind kind, const Destination& destination)
{
    return kind == LaunchMissionKind::AsteroidBelt ||
        (kind == LaunchMissionKind::Standard && destination.tier >= 3);
}

std::string warningMessage(const TelemetryEvent& event)
{
    if (event.heat >= tuning::launch::pilotingCriticalThreshold) {
        return "TEMPERATURE CRITICAL - turn engines off";
    }
    if (event.guidance >= tuning::launch::pilotingCriticalThreshold) {
        return "COURSE CRITICAL - steer toward center";
    }
    if (event.heat >= tuning::launch::pilotingWarningThreshold) {
        return "Temperature caution - prepare to turn engines off";
    }
    if (event.guidance >= tuning::launch::pilotingWarningThreshold) {
        return "Course caution - correct toward center";
    }
    return std::string(text::telemetry::nominal);
}

bool isShallowRecovery(const Destination& destination, double multiplier)
{
    return multiplier < 1.0 +
        (destination.targetMultiplier - 1.0) * tuning::rewards::shallowRecoveryTargetShare;
}

double shallowRecoveryPenalty(int shallowRecoveryStreak)
{
    const int exponent = std::clamp(
        std::max(0, shallowRecoveryStreak),
        0,
        tuning::rewards::shallowRecoveryPenaltyMaxExponent);
    double penalty = tuning::rewards::shallowRecoveryPenaltyBase;
    for (int i = 0; i < exponent; ++i) {
        penalty *= 2.0;
    }
    return std::min(penalty, tuning::rewards::shallowRecoveryPenaltyMaximum);
}

double overburnJackpotMultiplier(const Destination& destination, double burnMultiplier)
{
    const double overGoal = std::max(0.0, burnMultiplier - destination.targetMultiplier);
    if (overGoal <= 0.0) {
        return 1.0;
    }

    const double normalizedOverburn = overGoal /
        std::max(tuning::launch::overburnMinimumDenominator, destination.targetMultiplier - 1.0);
    return std::clamp(
        std::exp(normalizedOverburn * tuning::launch::overburnExponent),
        1.0,
        tuning::launch::overburnMaximumMultiplier);
}

void markDestroyed(
    LaunchOutcome& outcome,
    const ContentCatalog& catalog,
    const Destination& destination,
    const GameState& state,
    Random& rng)
{
    outcome.type = LaunchResultType::Destroyed;
    outcome.shipDamage = tuning::damage::destroyedShipDamage;
    outcome.blueprintGain = std::max(0, destination.tier / 2);

    const double survivalChance = std::clamp(
        tuning::outcomes::vehicleLossSurvivalBase +
            crewEscapeBonus(state, catalog) -
            destination.hazard * tuning::outcomes::survivalHazardScale,
        tuning::outcomes::survivalMinimum,
        tuning::outcomes::survivalMaximum);
    const bool survived = rng.chance(survivalChance);
    outcome.crewKilled = !survived;
    outcome.crewInjured = survived && rng.chance(tuning::outcomes::vehicleLossInjuryChance);

    if (!state.run.equippedModuleIds.empty() && rng.chance(tuning::damage::moduleLossChance)) {
        const int index = rng.rangeInt(0, static_cast<int>(state.run.equippedModuleIds.size()) - 1);
        outcome.moduleDestroyedId = state.run.equippedModuleIds[static_cast<std::size_t>(index)];
    }
}

double returnHomeNetRewardFloor(
    const PreparedLaunch& launch,
    const Destination& destination,
    double burnMultiplier)
{
    if (launch.config.frontierTransfer) {
        return 0.0;
    }

    const double dataGoal = std::min(launch.config.burnGoalMultiplier, destination.targetMultiplier);
    if (burnMultiplier + 0.000001 < dataGoal) {
        return 0.0;
    }

    if (isTrainingMission(launch.config.missionKind)) {
        return tuning::launchProgression::lessonReward;
    }
    if (burnMultiplier + 0.000001 >= destination.targetMultiplier) {
        return static_cast<double>(moduleOfferCost(Rarity::Rare));
    }

    const double uncommonThreshold = dataGoal +
        (destination.targetMultiplier - dataGoal) * tuning::rewards::pushedProfileShelfShare;
    if (burnMultiplier + 0.000001 >= uncommonThreshold) {
        return static_cast<double>(moduleOfferCost(Rarity::Uncommon));
    }
    return static_cast<double>(moduleOfferCost(Rarity::Common));
}

double pointToSegmentDistance(
    double pointX,
    double pointY,
    double startX,
    double startY,
    double endX,
    double endY)
{
    const double segmentX = endX - startX;
    const double segmentY = endY - startY;
    const double lengthSquared = segmentX * segmentX + segmentY * segmentY;
    if (lengthSquared <= 0.0000001) {
        return std::hypot(pointX - startX, pointY - startY);
    }
    const double t = std::clamp(
        ((pointX - startX) * segmentX + (pointY - startY) * segmentY) / lengthSquared,
        0.0,
        1.0);
    return std::hypot(
        pointX - (startX + segmentX * t),
        pointY - (startY + segmentY * t));
}

LaunchFailureCause terminalFailureCause(const PreparedLaunch& launch, LaunchFailureCause cause)
{
    if (cause == LaunchFailureCause::LunarImpact) {
        return cause;
    }
    const bool rescueLesson =
        launch.config.missionKind == LaunchMissionKind::FuelCalibration ||
        launch.config.missionKind == LaunchMissionKind::FlightControlsCalibration;
    return rescueLesson ? LaunchFailureCause::TrainingRescue : cause;
}

void populateAsteroidField(PreparedLaunch& launch, const Destination& destination, Random& rng)
{
    static_cast<void>(destination);
    if (!launch.asteroidsEnabled) {
        return;
    }

    int safeLane = rng.rangeInt(0, tuning::launch::asteroidLaneCount - 1);
    int previousSafeLane = safeLane;
    launch.asteroidCount = std::min(
        tuning::launch::asteroidCount,
        static_cast<int>(launch.asteroids.size()));

    int asteroidIndex = 0;
    for (int row = 0; row < tuning::launch::asteroidRowCount; ++row) {
        for (int lane = 0; lane < tuning::launch::asteroidLaneCount; ++lane) {
            if (lane == safeLane || asteroidIndex >= launch.asteroidCount) {
                continue;
            }
            LaunchAsteroid asteroid;
            double routeJitter = rng.range(-asteroidRouteJitter, asteroidRouteJitter);
            double courseJitter = rng.range(-asteroidCourseJitter, asteroidCourseJitter);
            const bool guardsLaneTransition = row > 0 &&
                safeLane != previousSafeLane &&
                lane == previousSafeLane;
            if (guardsLaneTransition) {
                // The rock that closes the previous row's safe lane sits on
                // the far/later side of its jitter window. This preserves the
                // randomized band while guaranteeing a visible diagonal gap
                // toward the adjacent safe lane.
                routeJitter = std::abs(routeJitter);
                courseJitter = safeLane > previousSafeLane
                    ? -std::abs(courseJitter)
                    : std::abs(courseJitter);
            }
            asteroid.routeProgress = std::clamp(
                launchAsteroidRowProgress(row) +
                    routeJitter,
                tuning::launch::asteroidBeltStart - asteroidRouteJitter,
                tuning::launch::asteroidBeltEnd + asteroidRouteJitter);
            asteroid.courseOffset = launchAsteroidLaneOffset(lane) +
                courseJitter;
            asteroid.scale = rng.range(
                tuning::launch::asteroidMinimumScale,
                tuning::launch::asteroidMaximumScale);
            asteroid.radius = tuning::launch::asteroidBaseRadius * asteroid.scale;
            asteroid.rotation = rng.range(0.0, math::pi * 2.0);
            const double spinMagnitude = rng.range(
                asteroidSpinMinimum,
                asteroidSpinMaximum);
            asteroid.spin = rng.chance(0.5) ? -spinMagnitude : spinMagnitude;
            launch.asteroids[static_cast<std::size_t>(asteroidIndex++)] = asteroid;
        }
        previousSafeLane = safeLane;
        safeLane = rng.rangeInt(
            std::max(0, safeLane - 1),
            std::min(tuning::launch::asteroidLaneCount - 1, safeLane + 1));
    }
}

} // namespace

double launchFuelCapacityForRank(int rank, double oneLaunchBoost)
{
    return tuning::launchProgression::baseFuelCapacity +
        static_cast<double>(std::clamp(
            rank,
            0,
            tuning::launchProgression::maximumUpgradeRank)) *
            tuning::launchProgression::fuelPerTankRank +
        std::max(0.0, oneLaunchBoost);
}

double launchCruiseFuelCostForTier(int tier)
{
    const int routeTier = std::max(1, tier);
    return std::min(
        tuning::launch::routeFuelMaximum,
        tuning::launch::routeFuelBase +
            static_cast<double>(routeTier) * tuning::launch::routeFuelPerTier);
}

double launchFuelUseMultiplier(double throttle)
{
    const double normalized = std::max(0.0, throttle) /
        std::max(0.01, tuning::launch::calibratedThrottle);
    return tuning::launch::fuelDistanceBaseMultiplier +
        tuning::launch::fuelDistanceThrottleMultiplier * normalized * normalized;
}

double launchControlChaosForRank(int rank)
{
    switch (std::clamp(rank, 0, tuning::launchProgression::maximumUpgradeRank)) {
    case 0: return tuning::launch::controlChaosRankZero;
    case 1: return tuning::launch::controlChaosRankOne;
    case 2: return tuning::launch::controlChaosRankTwo;
    default: return tuning::launch::controlChaosRankThree;
    }
}

double launchPoweredHeatMultiplierForRank(int rank)
{
    switch (std::clamp(rank, 0, tuning::launchProgression::maximumUpgradeRank)) {
    case 0: return tuning::launch::poweredHeatRankZeroMultiplier;
    case 1: return tuning::launch::poweredHeatRankOneMultiplier;
    case 2: return tuning::launch::poweredHeatRankTwoMultiplier;
    default: return tuning::launch::poweredHeatRankThreeMultiplier;
    }
}

double launchEngineOffCoolingForRank(int rank)
{
    switch (std::clamp(rank, 0, tuning::launchProgression::maximumUpgradeRank)) {
    case 0: return tuning::launch::engineOffCoolingRankZero;
    case 1: return tuning::launch::engineOffCoolingRankOne;
    case 2: return tuning::launch::engineOffCoolingRankTwo;
    default: return tuning::launch::engineOffCoolingRankThree;
    }
}

double launchHullImpactMultiplierForRank(int rank)
{
    switch (std::clamp(rank, 0, tuning::launchProgression::maximumUpgradeRank)) {
    case 0: return tuning::launch::hullImpactRankZeroMultiplier;
    case 1: return tuning::launch::hullImpactRankOneMultiplier;
    case 2: return tuning::launch::hullImpactRankTwoMultiplier;
    default: return tuning::launch::hullImpactRankThreeMultiplier;
    }
}

double launchAsteroidRowProgress(int row)
{
    const int clampedRow = std::clamp(row, 0, tuning::launch::asteroidRowCount - 1);
    return tuning::launch::asteroidBeltStart +
        (tuning::launch::asteroidBeltEnd - tuning::launch::asteroidBeltStart) *
            static_cast<double>(clampedRow) /
            static_cast<double>(tuning::launch::asteroidRowCount - 1);
}

double launchAsteroidLaneOffset(int lane)
{
    switch (std::clamp(lane, 0, tuning::launch::asteroidLaneCount - 1)) {
    case 0: return -tuning::launch::asteroidLaneOffset;
    case 1: return 0.0;
    default: return tuning::launch::asteroidLaneOffset;
    }
}

double launchAsteroidImpactDamage(int hullRank, double asteroidScale)
{
    return tuning::launch::asteroidImpactDamageBase *
        std::clamp(
            asteroidScale,
            tuning::launch::asteroidMinimumScale,
            tuning::launch::asteroidMaximumScale) *
        launchHullImpactMultiplierForRank(hullRank);
}

PreparedLaunch prepareLaunch(const GameState& state, const ContentCatalog& catalog, Random& rng)
{
    PreparedLaunch launch;
    launch.config = state.launchConfig;
    launch.slingshotFuelBoost = std::max(0.0, state.run.nextLaunchFuelBoost);
    launch.slingshotSpeedBoost = std::max(0.0, state.run.nextLaunchSpeedBoost);

    const Destination* configuredDestination = catalog.findDestination(launch.config.destinationId);
    const Destination& destination = configuredDestination == nullptr
        ? currentDestination(state, catalog)
        : *configuredDestination;
    launch.config.destinationId = destination.id;
    launch.config.frameId = state.run.frameId;
    launch.config.equippedModuleIds = state.run.equippedModuleIds;

    launch.flightControlRank = std::clamp(
        state.meta.launchUpgrades.flightControls,
        0,
        tuning::launchProgression::maximumUpgradeRank);
    launch.coolingRank = std::clamp(
        state.meta.launchUpgrades.cooling,
        0,
        tuning::launchProgression::maximumUpgradeRank);
    launch.hullRank = std::clamp(
        state.meta.launchUpgrades.hull,
        0,
        tuning::launchProgression::maximumUpgradeRank);
    const int fuelRank = std::clamp(
        state.meta.launchUpgrades.fuelTanks,
        0,
        tuning::launchProgression::maximumUpgradeRank);
    launch.fuelCapacity = launchFuelCapacityForRank(fuelRank, launch.slingshotFuelBoost);
    launch.cruiseFuelCost = launchCruiseFuelCostForTier(destination.tier);
    // Frontier transfers land as soon as the ship reaches the destination.
    // Fuel remains a range constraint, not a hidden landing-reserve check.
    launch.arrivalReserveFuel = 0.0;
    launch.trainingMission = isTrainingMission(launch.config.missionKind) &&
        !launch.config.frontierTransfer;
    launch.manualControlsEnabled = launch.config.missionKind != LaunchMissionKind::FuelCalibration;
    launch.heatEnabled = missionUsesHeat(launch.config.missionKind, destination);
    launch.asteroidsEnabled = missionUsesAsteroids(launch.config.missionKind, destination);
    launch.controlChaos = launchControlChaosForRank(launch.flightControlRank);
    launch.controlSteeringResponseVariation = rng.range(
        -tuning::launch::controlSteeringResponseVariance,
        tuning::launch::controlSteeringResponseVariance) * launch.controlChaos;
    launch.controlKickCount = static_cast<int>(launch.controlKickDirections.size());
    for (int index = 0; index < launch.controlKickCount; ++index) {
        launch.controlKickDirections[static_cast<std::size_t>(index)] =
            rng.chance(0.5) ? -1.0 : 1.0;
    }
    launch.existingShipDamage = std::clamp(
        state.run.shipDamage,
        0,
        tuning::damage::destroyedShipDamage);

    const int requiredReadiness = frontierReadinessRequired(state, catalog);
    launch.overpreparedData = requiredReadiness <= 0
        ? 0
        : std::max(0, state.run.frontierReadiness - requiredReadiness);
    launch.provingPayoutBonus = launch.config.frontierTransfer
        ? 0.0
        : std::min(
              tuning::rewards::provingPayoutBonusMaximum,
              static_cast<double>(launch.overpreparedData) * tuning::rewards::provingPayoutPerExtraData);

    // Kept only for old result records. No live update or resolution path
    // compares player progress with this value.
    launch.crashMultiplier = destination.targetMultiplier + 1.0;
    populateAsteroidField(launch, destination, rng);
    return launch;
}

LaunchFlightState beginLaunchFlight(const PreparedLaunch& launch, const Destination&)
{
    LaunchFlightState flight;
    flight.active = true;
    flight.selectedThrottle = tuning::launch::pilotingInitialThrottle;
    flight.throttleAtLastKick = flight.selectedThrottle;
    if (launch.manualControlsEnabled && launch.controlChaos > 0.0 &&
        launch.controlKickCount > 0) {
        flight.courseVelocity =
            launch.controlKickDirections[0] * tuning::launch::controlStartupDrift *
            launch.controlChaos;
        flight.nextControlKickIndex = 1;
        flight.throttleKickCooldownSeconds = tuning::launch::controlThrottleKickCooldown;
    }
    flight.fuelCapacity = launch.fuelCapacity;
    flight.fuelRemaining = launch.fuelCapacity;
    flight.calibrationReturnFuelProtected =
        (launch.config.missionKind == LaunchMissionKind::FuelCalibration ||
         launch.config.missionKind == LaunchMissionKind::FlightControlsCalibration) &&
        !launch.config.frontierTransfer;
    flight.fuelSurveyReturnClassifiable =
        launch.config.missionKind == LaunchMissionKind::FuelCalibration &&
        !launch.config.frontierTransfer;
    flight.projectedFuelRequired = launch.config.frontierTransfer
        ? launch.cruiseFuelCost * launchFuelUseMultiplier(flight.selectedThrottle) +
            launch.arrivalReserveFuel
        : 0.0;
    flight.projectedFuelReserve = flight.fuelRemaining - flight.projectedFuelRequired;
    flight.heat = launch.heatEnabled ? tuning::launch::pilotingHeatInitial : 0.0;
    flight.hullMaximum = tuning::launch::hullBaseIntegrity +
        static_cast<double>(launch.hullRank) * tuning::launch::hullIntegrityPerRank;
    flight.hullRemaining = flight.hullMaximum *
        (1.0 - static_cast<double>(launch.existingShipDamage) /
            static_cast<double>(tuning::damage::destroyedShipDamage));
    return flight;
}

void beginLaunchReturn(LaunchFlightState& flight)
{
    if (flight.active && !flight.returningHome) {
        // The opening lesson teaches the decision to turn back, not a
        // frame-perfect click. Any turnaround made while fuel remains gets a
        // calibrated return burn that uses exactly that remaining fuel over
        // the distance home.
        if (flight.calibrationReturnFuelProtected &&
            flight.fuelRemaining > 0.000001 &&
            flight.travelProgress > 0.000001) {
            if (flight.fuelSurveyReturnClassifiable &&
                flight.fuelSurveyReturnTiming == FuelSurveyReturnTiming::Unqualified &&
                flight.fuelSurveyLateLatched) {
                flight.fuelSurveyReturnTiming = FuelSurveyReturnTiming::Late;
            }
            if (flight.fuelSurveyReturnClassifiable &&
                flight.fuelSurveyReturnTiming == FuelSurveyReturnTiming::Unqualified &&
                flight.fuelCapacity > 0.0 &&
                flight.fuelRemaining / flight.fuelCapacity <=
                    tuning::launchProgression::fuelSurveyTargetFuelShare + 0.000001) {
                flight.fuelSurveyReturnTiming = FuelSurveyReturnTiming::Timely;
            }
            flight.fuelSurveyReturnUsePerProgress =
                flight.fuelRemaining / flight.travelProgress;
        }
        flight.returningHome = true;
        flight.asteroidHit.fill(false);
    }
}

double launchCourseLimit(const PreparedLaunch&)
{
    return tuning::launch::pilotingCourseLost;
}

LaunchFlightStep updateLaunchFlight(
    LaunchFlightState& flight,
    const PreparedLaunch& launch,
    const Destination& destination,
    const LaunchControlInput& input,
    double deltaSeconds)
{
    LaunchFlightStep result;
    if (!flight.active || flight.failureCause != LaunchFailureCause::None) {
        result.failed = flight.failureCause != LaunchFailureCause::None;
        result.failureCause = flight.failureCause;
        result.trainingRescue = flight.failureCause == LaunchFailureCause::TrainingRescue;
        return result;
    }

    const double dt = std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds);
    flight.elapsedSeconds += dt;
    flight.asteroidInvulnerabilitySeconds = std::max(
        0.0,
        flight.asteroidInvulnerabilitySeconds - dt);
    flight.throttleKickCooldownSeconds = std::max(
        0.0,
        flight.throttleKickCooldownSeconds - dt);

    const bool enginesCut = launch.heatEnabled && input.enginesCut;
    if (launch.manualControlsEnabled) {
        flight.selectedThrottle = std::clamp(
            flight.selectedThrottle +
                std::clamp(input.throttle, -1.0, 1.0) *
                    tuning::launch::pilotingThrottleChangePerSecond * dt,
            tuning::launch::pilotingMinimumPoweredThrottle,
            1.0);
    } else {
        flight.selectedThrottle = tuning::launch::calibratedThrottle;
    }

    const double targetSpan = std::max(
        tuning::session::minTravelDenominator,
        destination.targetMultiplier - 1.0);
    const double tierFactor = 1.0 +
        static_cast<double>(std::max(0, destination.tier)) * tuning::launch::pilotingTierDurationScale;
    const double poweredDrive = tuning::launch::pilotingPoweredSteeringBase +
        flight.selectedThrottle;
    const double lessonSpeedScale =
        launch.config.missionKind == LaunchMissionKind::FuelCalibration
        ? tuning::launchProgression::fuelSurveyProgressRateScale
        : 1.0;
    const double targetVelocity =
        tuning::launch::pilotingBaseProgressRate * poweredDrive / tierFactor *
        std::max(0.25, 1.0 + launch.slingshotSpeedBoost) * lessonSpeedScale;

    if (enginesCut) {
        flight.travelVelocity = std::max(
            0.0,
            flight.travelVelocity - tuning::launch::coastDecelerationPerSecond * dt);
        if (flight.travelVelocity < tuning::launch::coastStopSpeed) {
            flight.travelVelocity = 0.0;
        }
    } else {
        const double response = std::clamp(
            tuning::launch::poweredVelocityResponse * dt,
            0.0,
            1.0);
        flight.travelVelocity += (targetVelocity - flight.travelVelocity) * response;
    }
    flight.burnRatePerSecond = flight.travelVelocity * targetSpan;

    flight.previousTravelProgress = flight.travelProgress;
    const double previousCourseOffset = flight.courseOffset;
    const double direction = flight.returningHome ? -1.0 : 1.0;
    const double requestedProgressDelta = direction * flight.travelVelocity * dt;
    const double maximumTravelProgress = !flight.returningHome && launch.config.frontierTransfer
        ? 1.0
        : tuning::launch::pilotingMaximumTravelProgress;
    flight.travelProgress = std::clamp(
        flight.travelProgress + requestedProgressDelta,
        0.0,
        maximumTravelProgress);
    const double traveledDistance = std::abs(flight.travelProgress - flight.previousTravelProgress);
    if (!enginesCut && traveledDistance > 0.0) {
        const double fuelUsePerProgress = flight.returningHome &&
                flight.fuelSurveyReturnUsePerProgress > 0.0
            ? flight.fuelSurveyReturnUsePerProgress
            : launch.cruiseFuelCost *
                launchFuelUseMultiplier(flight.selectedThrottle);
        flight.fuelRemaining = std::max(
            0.0,
            flight.fuelRemaining - traveledDistance * fuelUsePerProgress);
    }
    flight.currentMultiplier = 1.0 + targetSpan * flight.travelProgress;
    flight.peakMultiplier = std::max(flight.peakMultiplier, flight.currentMultiplier);
    const double projectedDistance = flight.returningHome
        ? flight.travelProgress
        : (launch.config.frontierTransfer ? 1.0 - flight.travelProgress : flight.travelProgress);
    flight.projectedFuelRequired = std::max(0.0, projectedDistance) *
        launch.cruiseFuelCost * launchFuelUseMultiplier(flight.selectedThrottle) +
        (!flight.returningHome && launch.config.frontierTransfer
                ? launch.arrivalReserveFuel
                : 0.0);
    flight.projectedFuelReserve = flight.fuelRemaining - flight.projectedFuelRequired;
    if (flight.fuelSurveyReturnClassifiable && !flight.returningHome &&
        flight.fuelRemaining / std::max(0.01, flight.fuelCapacity) <=
            tuning::launchProgression::fuelSurveyLateFuelShare + 0.000001) {
        flight.fuelSurveyLateLatched = true;
    }

    if (launch.heatEnabled) {
        double heatDelta = 0.0;
        if (enginesCut) {
            heatDelta = -launchEngineOffCoolingForRank(launch.coolingRank);
        } else {
            const double throttleSquared = flight.selectedThrottle * flight.selectedThrottle;
            const double heatInput = tuning::launch::poweredHeatIdleInput +
                (tuning::launch::poweredHeatThrottleInput +
                    destination.hazard * tuning::launch::poweredHeatHazardInput) * throttleSquared;
            heatDelta = heatInput * launchPoweredHeatMultiplierForRank(launch.coolingRank) -
                tuning::launch::poweredHeatCoolingBase;
        }
        flight.heat = std::clamp(
            flight.heat + heatDelta * dt,
            0.0,
            tuning::telemetry::heatMaximum);
    } else {
        flight.heat = 0.0;
    }

    if (launch.manualControlsEnabled) {
        const double steer = std::clamp(input.steer, -1.0, 1.0);
        const double directionalGain = std::max(
            0.05,
            1.0 +
                (steer > 0.0 ? tuning::launch::controlRightOvershoot * launch.controlChaos : 0.0) +
                launch.controlSteeringResponseVariation);
        const double steeringAuthority = tuning::launch::pilotingSteeringBase *
            (enginesCut ? tuning::launch::pilotingCutSteeringScale : 1.0);

        const bool throttleIncreasing = !enginesCut && input.throttle > 0.10;
        if (flight.selectedThrottle < flight.throttleAtLastKick) {
            flight.throttleAtLastKick = flight.selectedThrottle;
        }
        if (throttleIncreasing &&
            flight.selectedThrottle - flight.throttleAtLastKick >=
                tuning::launch::controlThrottleKickThreshold &&
            flight.throttleKickCooldownSeconds <= 0.0 &&
            flight.nextControlKickIndex < launch.controlKickCount) {
            flight.courseVelocity +=
                launch.controlKickDirections[static_cast<std::size_t>(flight.nextControlKickIndex)] *
                tuning::launch::controlThrottleKick * launch.controlChaos;
            ++flight.nextControlKickIndex;
            flight.throttleAtLastKick = flight.selectedThrottle;
            flight.throttleKickCooldownSeconds = tuning::launch::controlThrottleKickCooldown;
        }
        flight.throttleInputActive = throttleIncreasing;

        const double damping = tuning::launch::controlDampingMinimum +
            (1.0 - launch.controlChaos) * tuning::launch::controlDampingChaosRelief;
        const double autoTrim = tuning::launch::controlAutoTrimMinimum +
            (1.0 - launch.controlChaos) * tuning::launch::controlAutoTrimChaosRelief;
        flight.courseVelocity += (
            steer * directionalGain * steeringAuthority -
            flight.courseOffset * autoTrim -
            flight.courseVelocity * damping) * dt;
        flight.courseOffset += flight.courseVelocity * dt;
    } else {
        flight.courseOffset = 0.0;
        flight.courseVelocity = 0.0;
        flight.throttleInputActive = false;
    }

    if (launch.asteroidsEnabled && flight.asteroidInvulnerabilitySeconds <= 0.0) {
        const double startX = flight.previousTravelProgress * tuning::launch::asteroidRouteAxisScale;
        const double endX = flight.travelProgress * tuning::launch::asteroidRouteAxisScale;
        for (int index = 0; index < launch.asteroidCount; ++index) {
            const std::size_t asteroidIndex = static_cast<std::size_t>(index);
            if (flight.asteroidHit[asteroidIndex]) {
                continue;
            }
            const LaunchAsteroid& asteroid = launch.asteroids[asteroidIndex];
            const double collisionDistance = pointToSegmentDistance(
                asteroid.routeProgress * tuning::launch::asteroidRouteAxisScale,
                asteroid.courseOffset,
                startX,
                previousCourseOffset,
                endX,
                flight.courseOffset);
            if (collisionDistance > asteroid.radius + tuning::launch::asteroidShipRadius) {
                continue;
            }

            flight.asteroidHit[asteroidIndex] = true;
            flight.asteroidInvulnerabilitySeconds = tuning::launch::asteroidInvulnerabilitySeconds;
            const double damage = launchAsteroidImpactDamage(
                launch.hullRank,
                asteroid.scale);
            flight.hullRemaining = std::max(0.0, flight.hullRemaining - damage);
            const int campaignDamage = std::max(
                1,
                static_cast<int>(std::round(
                    damage / std::max(1.0, flight.hullMaximum) *
                    static_cast<double>(tuning::damage::destroyedShipDamage))));
            flight.hullDamageTaken = std::min(
                tuning::damage::destroyedShipDamage,
                flight.hullDamageTaken + campaignDamage);
            const double impactSeparation = flight.courseOffset - asteroid.courseOffset;
            const double impactDirection = std::abs(impactSeparation) > 0.0001
                ? (impactSeparation < 0.0 ? -1.0 : 1.0)
                : (asteroid.courseOffset < 0.0
                        ? 1.0
                        : (asteroid.courseOffset > 0.0
                                ? -1.0
                                : (index % 2 == 0 ? -1.0 : 1.0)));
            flight.courseVelocity += impactDirection * asteroidCourseImpulse * asteroid.scale;
            result.asteroidHit = true;
            result.hullDamageTaken = campaignDamage;
            break;
        }
    }

    result.reachedHome = flight.returningHome && flight.travelProgress <= 0.000001;
    if (result.reachedHome) {
        flight.active = false;
        return result;
    }

    if (!flight.returningHome && flight.travelProgress >= 1.0) {
        if (launch.config.missionKind == LaunchMissionKind::FlightControlsCalibration &&
            !launch.config.frontierTransfer) {
            // The calibration line is the end of the safe test flight. The
            // Moon is deliberately visible beyond it, but landing guidance is
            // not installed until Flight Controls I is earned and fitted.
            flight.failureCause = LaunchFailureCause::LunarImpact;
        } else {
            // Transfer fuel may reach exactly zero on the destination frame.
            // The isolated expedition pack and return stage are not connected
            // to this feed, so touchdown still succeeds. Running dry before
            // travel reaches the destination remains a terminal failure below.
            result.reachedDestination = true;
        }
    }

    if (!result.reachedDestination && !flight.returningHome &&
        launch.config.frontierTransfer && flight.fuelRemaining <= 0.000001 &&
        flight.travelProgress < 1.0 - 0.000001) {
        flight.failureCause = terminalFailureCause(launch, LaunchFailureCause::FuelExhausted);
    }

    const double courseLimit = launchCourseLimit(launch);
    flight.heatFailureSeconds = launch.heatEnabled &&
            flight.heat >= tuning::launch::pilotingFailureThreshold
        ? flight.heatFailureSeconds + dt
        : 0.0;
    flight.courseFailureSeconds = launch.manualControlsEnabled &&
            std::abs(flight.courseOffset) >= courseLimit
        ? flight.courseFailureSeconds + dt
        : 0.0;
    flight.fuelFailureSeconds = flight.fuelRemaining <= 0.0
        ? flight.fuelFailureSeconds + dt
        : 0.0;

    const double heatMargin = launch.heatEnabled ? 1.0 - flight.heat : 1.0;
    const double courseMargin = launch.manualControlsEnabled
        ? 1.0 - std::abs(flight.courseOffset) / std::max(0.01, courseLimit)
        : 1.0;
    const double fuelMargin = flight.fuelRemaining / std::max(0.01, flight.fuelCapacity);
    const double hullMargin = launch.asteroidsEnabled
        ? flight.hullRemaining / std::max(1.0, flight.hullMaximum)
        : 1.0;
    flight.minimumSafetyMargin = std::min(
        flight.minimumSafetyMargin,
        std::min({heatMargin, courseMargin, fuelMargin, hullMargin}));

    if (flight.failureCause == LaunchFailureCause::None && flight.hullRemaining <= 0.0) {
        flight.failureCause = terminalFailureCause(launch, LaunchFailureCause::HullBreach);
    } else if (flight.failureCause == LaunchFailureCause::None &&
        flight.heatFailureSeconds >= tuning::launch::pilotingHeatFailureSeconds) {
        flight.failureCause = terminalFailureCause(launch, LaunchFailureCause::ThermalRunaway);
    } else if (flight.failureCause == LaunchFailureCause::None &&
        flight.courseFailureSeconds >= tuning::launch::pilotingCourseFailureSeconds) {
        flight.failureCause = terminalFailureCause(launch, LaunchFailureCause::CourseLost);
    } else if (flight.failureCause == LaunchFailureCause::None &&
        !(result.reachedDestination && launch.config.frontierTransfer) &&
        flight.fuelFailureSeconds >= tuning::launch::pilotingFuelFailureSeconds) {
        flight.failureCause = terminalFailureCause(launch, LaunchFailureCause::FuelExhausted);
    }

    if (flight.failureCause != LaunchFailureCause::None) {
        flight.active = false;
        result.failed = true;
        result.failureCause = flight.failureCause;
        result.trainingRescue = flight.failureCause == LaunchFailureCause::TrainingRescue;
    }
    return result;
}

TelemetryEvent launchTelemetryAt(const PreparedLaunch& launch, const LaunchFlightState& flight)
{
    TelemetryEvent event;
    event.multiplier = flight.currentMultiplier;
    event.heat = launch.heatEnabled ? flight.heat : 0.0;
    event.guidance = launch.manualControlsEnabled
        ? std::clamp(
              std::abs(flight.courseOffset) /
                  std::max(0.01, launchCourseLimit(launch)),
              0.0,
              1.0)
        : 0.0;
    // Retained for version-9 record compatibility only. Fuel is now reported
    // directly in absolute units instead of masquerading as a telemetry fault.
    event.fuelMix = 0.0;
    event.instability = std::clamp(
        flight.courseFailureSeconds /
            std::max(0.01, tuning::launch::pilotingCourseFailureSeconds),
        0.0,
        1.0);
    // Retained for version-9 record compatibility only. Live launch survival
    // is decided by the visible heat, course, fuel, and hull mechanics.
    event.abortRisk = 0.0;
    event.warning = std::clamp(
        std::max(event.heat, event.guidance),
        0.0,
        1.0);
    event.stress = std::clamp(
        event.heat * tuning::telemetry::stressHeatScale +
            event.guidance * tuning::telemetry::stressGuidanceScale,
        0.0,
        1.0);
    event.message = warningMessage(event);
    return event;
}

LaunchOutcome resolveLaunch(
    const PreparedLaunch& launch,
    const ContentCatalog& catalog,
    const GameState& state,
    double burnMultiplier,
    RecoveryMethod method,
    Random& rng,
    LaunchResolutionContext resolution)
{
    LaunchOutcome outcome;
    outcome.recoveryMethod = method;
    outcome.destinationId = launch.config.destinationId;
    outcome.assignedAstronautId = launch.config.astronautId;
    outcome.frontierTransfer = launch.config.frontierTransfer;
    outcome.crashMultiplier = launch.crashMultiplier;
    outcome.ejectMultiplier = std::max(1.0, burnMultiplier);
    outcome.pilotedFlight = resolution.pilotedFlight;
    outcome.failureCause = resolution.failureCause;
    outcome.fuelSurveyReturnTiming = resolution.fuelSurveyReturnTiming;
    outcome.minimumSafetyMargin = resolution.minimumSafetyMargin;

    const Destination* destination = catalog.findDestination(launch.config.destinationId);
    if (destination == nullptr) {
        outcome.type = LaunchResultType::Destroyed;
        outcome.shipDamage = tuning::damage::destroyedShipDamage;
        return outcome;
    }

    if (resolution.failureCause == LaunchFailureCause::TrainingRescue) {
        outcome.type = LaunchResultType::SafeEject;
        outcome.recoveryMethod = RecoveryMethod::ReturnHome;
        outcome.shipDamage = std::clamp(
            resolution.hullDamageTaken,
            0,
            tuning::damage::destroyedShipDamage - 1);
        return outcome;
    }
    if (resolution.failureCause != LaunchFailureCause::None || method == RecoveryMethod::None) {
        markDestroyed(outcome, catalog, *destination, state, rng);
        if (launch.trainingMission) {
            outcome.blueprintGain = 0;
        }
        return outcome;
    }

    // Unknown legacy recovery requests resolve as deterministic rescue rather
    // than reviving any hidden survival roll.
    if (method != RecoveryMethod::ReturnHome && method != RecoveryMethod::TransferArrival) {
        outcome.type = LaunchResultType::SafeEject;
        outcome.recoveryMethod = RecoveryMethod::ReturnHome;
        outcome.shipDamage = std::clamp(
            resolution.hullDamageTaken,
            0,
            tuning::damage::destroyedShipDamage - 1);
        return outcome;
    }

    const double payoutMultiplier = 1.0 + launch.provingPayoutBonus;
    const bool reachedDestination = outcome.ejectMultiplier >= destination->targetMultiplier;
    const TelemetryEvent event = telemetryAt(launch, outcome.ejectMultiplier);

    if (method == RecoveryMethod::TransferArrival) {
        outcome.type = LaunchResultType::MissionComplete;
        outcome.payout = destination->baseReward * outcome.ejectMultiplier *
            payoutMultiplier * tuning::rewards::transferArrivalPayoutFactor;
        if (resolution.pilotedFlight) {
            outcome.shipDamage = std::clamp(
                resolution.hullDamageTaken,
                0,
                tuning::damage::destroyedShipDamage - 1);
        } else {
            const double arrivalDamage =
                destination->hazard * tuning::outcomes::transferArrivalDamageHazardScale +
                outcome.ejectMultiplier * tuning::outcomes::transferArrivalDamageBurnScale +
                event.stress * tuning::outcomes::transferArrivalDamageStressScale;
            outcome.shipDamage = std::clamp(
                static_cast<int>(std::round(arrivalDamage)) + resolution.hullDamageTaken,
                tuning::outcomes::transferArrivalDamageMinimum,
                tuning::damage::destroyedShipDamage - 1);
        }
        outcome.blueprintGain = 1 + destination->tier / 2;
        return outcome;
    }

    outcome.type = reachedDestination
        ? LaunchResultType::MissionComplete
        : LaunchResultType::SafeEject;
    const double jackpotMultiplier = reachedDestination
        ? overburnJackpotMultiplier(*destination, outcome.ejectMultiplier)
        : 1.0;
    outcome.payout = destination->baseReward * outcome.ejectMultiplier * payoutMultiplier *
        (reachedDestination
                ? tuning::rewards::returnHomeReachedGoalFactor * jackpotMultiplier
                : tuning::rewards::returnHomeBasePayoutFactor);
    outcome.recoveryCost = std::clamp(
        tuning::outcomes::returnHomeRecoveryBase +
            static_cast<double>(destination->tier) * tuning::outcomes::returnHomeRecoveryTierScale +
            outcome.ejectMultiplier * tuning::outcomes::returnHomeRecoveryBurnScale,
        tuning::outcomes::returnHomeRecoveryMinimum,
        tuning::outcomes::returnHomeRecoveryMaximum);
    const double lessonSpan = std::max(
        0.000001,
        launch.config.burnGoalMultiplier - 1.0);
    const double fuelSurveyShare = std::clamp(
        (outcome.ejectMultiplier - 1.0) / lessonSpan,
        0.0,
        1.0);
    const bool partialFuelSurvey =
        launch.config.missionKind == LaunchMissionKind::FuelCalibration &&
        !launch.config.frontierTransfer &&
        fuelSurveyShare > 0.0 &&
        outcome.ejectMultiplier + 0.000001 < launch.config.burnGoalMultiplier;
    if (isShallowRecovery(*destination, outcome.ejectMultiplier) && !partialFuelSurvey) {
        outcome.recoveryCost += shallowRecoveryPenalty(state.run.shallowRecoveryStreak);
        outcome.payout = std::min(outcome.payout, outcome.recoveryCost);
    }
    if (partialFuelSurvey) {
        outcome.payout = outcome.recoveryCost +
            tuning::launchProgression::lessonReward * fuelSurveyShare;
    } else {
        const double netRewardFloor = isShallowRecovery(*destination, outcome.ejectMultiplier)
            ? 0.0
            : returnHomeNetRewardFloor(launch, *destination, outcome.ejectMultiplier);
        if (netRewardFloor > 0.0) {
            outcome.payout = std::max(outcome.payout, outcome.recoveryCost + netRewardFloor);
        }
    }

    if (resolution.pilotedFlight) {
        outcome.shipDamage = std::clamp(
            resolution.hullDamageTaken,
            0,
            tuning::damage::destroyedShipDamage - 1);
    } else {
        const double stressDamage =
            destination->hazard * tuning::outcomes::returnHomeDamageHazardScale +
            outcome.ejectMultiplier * tuning::outcomes::returnHomeDamageBurnScale +
            event.stress * tuning::outcomes::returnHomeDamageStressScale;
        outcome.shipDamage = std::clamp(
            static_cast<int>(std::round(stressDamage)) + resolution.hullDamageTaken,
            tuning::outcomes::returnHomeDamageMinimum,
            tuning::damage::destroyedShipDamage - 1);
    }
    outcome.blueprintGain = reachedDestination
        ? 1 + destination->tier / 2
        : (outcome.ejectMultiplier >=
                  destination->targetMultiplier * tuning::outcomes::returnHomeBlueprintTargetShare
              ? 1
              : 0);
    return outcome;
}

LaunchOutcome simulateLaunchToTarget(
    const GameState& state,
    const ContentCatalog& catalog,
    Random& rng)
{
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
    return resolveLaunch(
        launch,
        catalog,
        state,
        state.launchConfig.burnGoalMultiplier,
        RecoveryMethod::ReturnHome,
        rng,
        {true, LaunchFailureCause::None, 1.0, 0});
}

TelemetryEvent telemetryAt(const PreparedLaunch& launch, double multiplier)
{
    TelemetryEvent event;
    event.multiplier = multiplier;
    const double burnSpan = std::max(
        tuning::session::minTravelDenominator,
        launch.config.burnGoalMultiplier - 1.0);
    const double progress = std::clamp(
        (multiplier - 1.0) / burnSpan,
        0.0,
        tuning::launch::pilotingMaximumTravelProgress);
    event.heat = launch.heatEnabled
        ? std::clamp(
              tuning::launch::pilotingHeatInitial + progress * 0.55,
              0.0,
              tuning::telemetry::heatMaximum)
        : 0.0;
    event.fuelMix = 0.0;
    event.abortRisk = 0.0;
    event.warning = event.heat;
    event.stress = std::clamp(
        event.heat * tuning::telemetry::stressHeatScale,
        0.0,
        1.0);
    event.message = warningMessage(event);
    return event;
}

} // namespace rocket
