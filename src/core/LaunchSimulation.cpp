#include "core/LaunchSimulation.h"

#include "core/FlightSystem.h"

#include "core/GameMath.h"
#include "core/GameText.h"
#include "core/ResearchSystem.h"
#include "core/Tuning.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace rocket {

namespace {

constexpr double physicalFlightStartX = flight_geometry::startX;
constexpr double physicalFlightStartY = flight_geometry::startY;
constexpr double physicalFlightStartVelocityX = 0.26;
constexpr double physicalFlightStartVelocityY = 0.0;

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

    double bonus = 0.0;
    if (const CrewArchetypeDefinition* archetype = catalog.findCrewArchetype(astronaut->archetypeId)) {
        bonus += archetype->stats.emergencyRecovery;
    }
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

bool isStraylightApproach(LaunchMissionKind kind)
{
    return kind == LaunchMissionKind::StraylightApproach;
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
    if (launch.config.frontierTransfer &&
        !isStraylightApproach(launch.config.missionKind)) {
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

double poweredLaunchTargetVelocity(
    const PreparedLaunch& launch,
    const Destination& destination,
    double throttle)
{
    const double tierFactor = 1.0 +
        static_cast<double>(std::max(0, destination.tier)) *
            tuning::launch::pilotingTierDurationScale;
    const double poweredDrive = tuning::launch::pilotingPoweredSteeringBase +
        std::clamp(throttle, tuning::launch::pilotingMinimumPoweredThrottle, 1.0);
    const double lessonSpeedScale =
        launch.config.missionKind == LaunchMissionKind::FuelCalibration
        ? tuning::launchProgression::fuelSurveyProgressRateScale
        : 1.0;
    return tuning::launch::pilotingBaseProgressRate * poweredDrive / tierFactor *
        std::max(0.25, 1.0 + launch.slingshotSpeedBoost) * lessonSpeedScale;
}

} // namespace

double launchFuelCapacityForRank(int rank)
{
    return tuning::launchProgression::baseFuelCapacity +
        static_cast<double>(std::clamp(
            rank,
            0,
            tuning::launchProgression::maximumUpgradeRank)) *
            tuning::launchProgression::fuelPerTankRank;
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

double launchPoweredFuelCost(
    double cruiseFuelCost,
    double throttle,
    double slingshotFuelSavings)
{
    return std::max(
        0.0,
        std::max(0.0, cruiseFuelCost) * launchFuelUseMultiplier(throttle) -
            std::max(0.0, slingshotFuelSavings));
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

    const Destination* configuredDestination = catalog.findDestination(launch.config.destinationId);
    const Destination& destination = configuredDestination == nullptr
        ? currentDestination(state, catalog)
        : *configuredDestination;
    launch.config.destinationId = destination.id;
    if (const RouteLinkDefinition* link = routeLinkForTransit(catalog, launch.config.routeTransit)) {
        launch.cruiseFuelCost = link->cruiseFuelCost;
        launch.routeProfileDestinationId = link->targetDestinationId;
    }
    launch.slingshotFuelSavings = pendingLaunchFuelSavingsForDestination(state, destination.id);
    launch.slingshotSpeedBoost = pendingLaunchSpeedBoostForDestination(state, destination.id);
    launch.slingshotInstabilityPenalty = pendingLaunchInstabilityPenaltyForDestination(state, destination.id);
    if (const PendingTransferAssist* assist = pendingTransferAssistForDestination(state, destination.id)) {
        launch.transferAssistId = assist->definitionId;
        launch.slingshotCourseOffset = std::clamp(
            assist->exitCourseOffset,
            -tuning::launch::pilotingCourseLost,
            tuning::launch::pilotingCourseLost);
    }
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
    launch.fuelCapacity = launchFuelCapacityForRank(fuelRank);
    if (launch.cruiseFuelCost <= 0.0) {
        launch.cruiseFuelCost = launchCruiseFuelCostForTier(destination.tier);
    }
    if (isStraylightApproach(launch.config.missionKind)) {
        // The Act I rendezvous is story motion, not a final mechanical exam.
        // Autoguidance carries the ship to the Ark without consuming the
        // expedition's transfer fuel or exposing a hidden failure condition.
        launch.cruiseFuelCost = 0.0;
        launch.slingshotFuelSavings = 0.0;
        launch.slingshotSpeedBoost = 0.0;
        launch.slingshotInstabilityPenalty = 0.0;
    }
    // Frontier transfers land as soon as the ship reaches the destination.
    // Fuel remains a range constraint, not a hidden landing-reserve check.
    launch.arrivalReserveFuel = 0.0;
    launch.trainingMission = isTrainingMission(launch.config.missionKind) &&
        !launch.config.frontierTransfer;
    launch.manualControlsEnabled = launch.config.missionKind != LaunchMissionKind::FuelCalibration &&
        !isStraylightApproach(launch.config.missionKind);
    launch.heatEnabled = missionUsesHeat(launch.config.missionKind, destination);
    launch.asteroidsEnabled = missionUsesAsteroids(launch.config.missionKind, destination);
    launch.controlChaos = std::clamp(
        launchControlChaosForRank(launch.flightControlRank) +
            launch.slingshotInstabilityPenalty,
        0.0,
        1.0);
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
    launch.orbitRequired = destination.requiresArrivalSurveySequence &&
        destinationHistoryValue(
            state.meta.destinationLandings,
            catalog,
            destination.id) == 0;

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

FlightRunState beginLaunchFlight(const PreparedLaunch& launch, const Destination& destination)
{
    FlightRunState flight;
    flight.originId = launch.config.routeTransit.originDestinationId;
    flight.destinationId = launch.config.destinationId;
    flight.active = true;
    flight.selectedThrottle = tuning::launch::pilotingInitialThrottle;
    flight.throttleAtLastKick = flight.selectedThrottle;
    flight.courseOffset = std::clamp(
        launch.slingshotCourseOffset,
        -launchCourseLimit(launch),
        launchCourseLimit(launch));
    if (!launch.transferAssistId.empty() || launch.slingshotSpeedBoost > 0.0) {
        // The assist starts the next leg already moving at its earned boosted
        // rate. Without this, the earned +0-40% award only changed the eventual
        // target velocity while every transfer visibly launched from rest.
        flight.travelVelocity = poweredLaunchTargetVelocity(
            launch,
            destination,
            flight.selectedThrottle);
        const double targetSpan = std::max(
            tuning::session::minTravelDenominator,
            destination.targetMultiplier - 1.0);
        flight.burnRatePerSecond = flight.travelVelocity * targetSpan;
    }
    if (launch.manualControlsEnabled && launch.controlChaos > 0.0 &&
        launch.controlKickCount > 0) {
        flight.courseVelocity =
            launch.controlKickDirections[0] * tuning::launch::controlStartupDrift *
            launch.controlChaos;
        flight.nextControlKickIndex = 1;
        flight.throttleKickCooldownSeconds = tuning::launch::controlThrottleKickCooldown;
    }
    if (!launch.transferAssistId.empty()) {
        flight.courseVelocity +=
            flight.courseOffset /
                std::max(0.01, launchCourseLimit(launch)) *
            tuning::launch::slingshotExitCourseDrift;
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
        ? launchPoweredFuelCost(
              launch.cruiseFuelCost,
              flight.selectedThrottle,
              launch.slingshotFuelSavings) +
            launch.arrivalReserveFuel
        : 0.0;
    flight.projectedFuelReserve = flight.fuelRemaining - flight.projectedFuelRequired;
    flight.heat = launch.heatEnabled ? tuning::launch::pilotingHeatInitial : 0.0;
    flight.hullMaximum = tuning::launch::hullBaseIntegrity +
        static_cast<double>(launch.hullRank) * tuning::launch::hullIntegrityPerRank;
    flight.hullRemaining = flight.hullMaximum *
        (1.0 - static_cast<double>(launch.existingShipDamage) /
            static_cast<double>(tuning::damage::destroyedShipDamage));
    if (launch.config.frontierTransfer &&
        !isStraylightApproach(launch.config.missionKind)) {
        flight.physicalFlight = true;
        flight.phase = FlightPhase::Transfer;
        // Start visibly destination-bound while preserving player agency. The
        // untouched physical course crosses the outer capture corridor and
        // continues into a flyby rather than auto-solving orbit or impact.
        flight.positionX = physicalFlightStartX;
        flight.positionY = physicalFlightStartY +
            std::clamp(launch.slingshotCourseOffset, -0.25, 0.25);
        flight.velocityX = physicalFlightStartVelocityX +
            std::max(0.0, launch.slingshotSpeedBoost) * 0.025;
        flight.velocityY = physicalFlightStartVelocityY +
            launch.slingshotCourseOffset /
                std::max(0.01, launchCourseLimit(launch)) *
                tuning::launch::slingshotExitCourseDrift;
        flight.heading = std::atan2(flight.velocityY, flight.velocityX);
        flight.angularVelocity = 0.0;
        flight.orbit = {};
        flight.orbit.previousAngle = std::atan2(flight.positionY, flight.positionX);
        flight.landing = {};
        flight.selectedThrottle = 0.0;
        flight.travelProgress = 0.0;
        flight.previousTravelProgress = 0.0;
        flight.courseOffset = flight.positionY;
        flight.courseVelocity = flight.velocityY;
    }
    return flight;
}

void beginLaunchReturn(FlightRunState& flight)
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

CalibrationFuelWarning calibrationFuelWarning(const PreparedLaunch& launch, const FlightRunState& flight)
{
    const bool calibrationFlight =
        launch.config.missionKind == LaunchMissionKind::FuelCalibration ||
        launch.config.missionKind == LaunchMissionKind::FlightControlsCalibration;
    if (!calibrationFlight || launch.config.frontierTransfer || flight.returningHome) {
        return CalibrationFuelWarning::None;
    }

    const double fuelShare = flight.fuelRemaining / std::max(0.01, flight.fuelCapacity);
    if (fuelShare <= tuning::launchProgression::fuelSurveyLateFuelShare) {
        return CalibrationFuelWarning::Critical;
    }
    if (fuelShare <= tuning::launchProgression::fuelSurveyTargetFuelShare) {
        return CalibrationFuelWarning::TurnAround;
    }
    if (fuelShare <= tuning::launchProgression::fuelSurveyPrepareFuelShare) {
        return CalibrationFuelWarning::Approaching;
    }
    return CalibrationFuelWarning::None;
}

double launchCourseLimit(const PreparedLaunch&)
{
    return tuning::launch::pilotingCourseLost;
}

namespace {

void updatePhysicalTrajectoryPrediction(FlightRunState& flight, bool landingAuthorized)
{
    constexpr double predictionStep = 0.2;
    constexpr int futureSamples = 100;
    flight.predictedTrajectory.clear();
    flight.predictedTrajectory.reserve(futureSamples + 1);
    CoastPredictionPose pose{flight.positionX, flight.positionY, flight.velocityX, flight.velocityY};
    flight.predictedTrajectory.push_back({pose.x, pose.y});
    bool gateArmed = flight.landing.gateArmed;
    for (int index = 0; index < futureSamples; ++index) {
        const CoastPredictionPose next = stepCoastPrediction(pose, predictionStep);
        // Sweep this interval, then refine the crossing with the same integrator.
        double earliest = predictionStep + 1.0;
        const auto crossing = [&](double boundary, bool inward, bool sectorOnly) {
            const double dx = next.x - pose.x, dy = next.y - pose.y;
            const double a = dx*dx + dy*dy;
            const double b = 2.0*(pose.x*dx + pose.y*dy);
            const double c = pose.x*pose.x + pose.y*pose.y - boundary*boundary;
            const double disc = b*b - 4.0*a*c;
            if (a < 1.0e-14 || disc < 0.0) return;
            const double t = (-b + (inward ? -1.0 : 1.0)*std::sqrt(disc))/(2.0*a);
            if (t < 0.0 || t > 1.0) return;
            double low = 0.0, high = 0.0;
            bool bracketed = false;
            // Confirm a real curved-path crossing, not just a chord grazing
            // the circle. This also brackets enter-and-exit in one interval.
            for (int probe = 1; probe <= 16; ++probe) {
                high = predictionStep * probe / 16.0;
                const auto point = stepCoastPrediction(pose, high);
                const bool crossed = inward ? std::hypot(point.x,point.y) <= boundary
                                            : std::hypot(point.x,point.y) >= boundary;
                if (crossed) { bracketed = true; break; }
                low = high;
            }
            if (!bracketed) return;
            for (int iteration = 0; iteration < 16; ++iteration) {
                const double mid = (low + high)*0.5;
                const auto sample = stepCoastPrediction(pose, mid);
                const bool before = inward ? std::hypot(sample.x,sample.y) > boundary
                                           : std::hypot(sample.x,sample.y) < boundary;
                if (before) low = mid; else high = mid;
            }
            const auto contact = stepCoastPrediction(pose, high);
            if (sectorOnly && !enabledLandingZoneAt(std::atan2(contact.y,contact.x))) return;
            earliest = std::min(earliest, high);
        };
        crossing(flight_geometry::bodyRadius, true, false);
        // Travel and Orbit share world coordinates and coasting gravity.
        // Keep a rolling forecast through their handling/camera boundaries;
        // only contact or entry into the different local Landing frame ends it.
        if (landingAuthorized && gateArmed)
            crossing(flight_geometry::landingBoundary, true, true);
        if (earliest <= predictionStep) {
            const auto end = stepCoastPrediction(pose, earliest);
            flight.predictedTrajectory.push_back({end.x,end.y});
            break;
        }
        pose = next;
        flight.predictedTrajectory.push_back({pose.x,pose.y});
        if (std::hypot(pose.x,pose.y) > flight_landing::gateRearmRadius &&
            flight.handoff.elapsed + (index+1)*predictionStep >= flight_landing::handoffSeconds)
            gateArmed = true;
    }
}
LaunchFlightStep updateSpaceFlight(
    FlightRunState& flight,
    const PreparedLaunch& launch,
    const FlightInput& input,
    double deltaSeconds,
    const MiningRunState* landingSite)
{
    LaunchFlightStep result;
    if (!flight.active) {
        return result;
    }

    constexpr double bodyRadius = flight_geometry::bodyRadius;
    constexpr double influenceRadius = flight_geometry::influenceRadius;
    constexpr double thrustAcceleration = 0.23;
    constexpr double turnAcceleration = flight_controls::turnAcceleration;
    constexpr double turnDamping = 5.2;
    constexpr double velocityToMetersPerSecond = flight_geometry::velocityToMetersPerSecond;
    const double realDt = std::clamp(
        deltaSeconds,
        0.0,
        tuning::launch::maxFrameStepSeconds);
    if (flight.mode == FlightMode::Orbit) {
        const double start=std::hypot(physicalFlightStartX,physicalFlightStartY)*0.4;
        const double end=flight.orbit.targetRadius+flight.orbit.goodBand;
        flight.orbitZoomProgress=std::max(flight.orbitZoomProgress,
            std::clamp((start-std::hypot(flight.positionX,flight.positionY))/(start-end),0.0,1.0));
    }
    const double currentTravelProgress = std::clamp(
        (flight.positionX - physicalFlightStartX) / -physicalFlightStartX,
        0.0,
        1.0);
    const FlightScaleProfile scaleProfile = flightScaleProfile(flight);
    (void)currentTravelProgress;
    const double worldDt = realDt * scaleProfile.timeScale;
    const double controlDt = realDt * scaleProfile.controlScale;
    // UI pulses and other presentation use elapsed wall time. Everything
    // physically consequential below uses the world or player-control clock.
    flight.elapsedSeconds += realDt;
    flight.asteroidInvulnerabilitySeconds = std::max(
        0.0,
        flight.asteroidInvulnerabilitySeconds - worldDt);
    flight.orbitCelebrationPending = false;
    flight.touchdownCelebrationPending = false;

    const double turn = std::clamp(input.steer, -1.0, 1.0);
    // Input steer is semantic screen direction: negative is left and positive
    // is right. Mathematical heading angles increase counterclockwise, so the
    // input sign must be inverted here for A/left to visibly turn left and
    // D/right to visibly turn right.
    flight.angularVelocity -= turn * turnAcceleration * controlDt;
    flight.angularVelocity *= std::exp(-turnDamping * controlDt);
    flight.heading += flight.angularVelocity * controlDt;

    // W is forward main thrust; S is reverse thrust, not a velocity brake.
    // With the nose upright, W arrests descent and S accelerates the fall.
    // Short keyboard presses provide graduated pulses; the stick stays
    // directly proportional. Releasing both keys coasts and consumes no fuel.
    double signedThrust = flightThrottleForInput(
        flight.selectedThrottle, input.enginesCut ? 0.0 : input.throttle,
        realDt, input.analogThrottle);
    if (flight.fuelRemaining <= 0.000001) {
        signedThrust = 0.0;
    }
    if (std::abs(signedThrust) > 0.001) {
        flight.velocityX += std::cos(flight.heading) * signedThrust * thrustAcceleration * controlDt;
        flight.velocityY += std::sin(flight.heading) * signedThrust * thrustAcceleration * controlDt;
        flight.fuelRemaining = std::max(
            0.0,
            flight.fuelRemaining - std::abs(signedThrust) * 0.13 * controlDt);
    }
    flight.selectedThrottle = signedThrust;
    flight.burnRatePerSecond =
        std::abs(signedThrust) * 0.13 * scaleProfile.controlScale;

    if (launch.heatEnabled) {
        if (std::abs(signedThrust) <= 0.001) {
            flight.heat = std::max(
                0.0,
                flight.heat - launchEngineOffCoolingForRank(launch.coolingRank) * worldDt);
        } else {
            const double throttleSquared = signedThrust * signedThrust;
            const double heatInput = tuning::launch::poweredHeatIdleInput +
                tuning::launch::poweredHeatThrottleInput * throttleSquared;
            flight.heat = std::clamp(
                flight.heat +
                    heatInput * launchPoweredHeatMultiplierForRank(launch.coolingRank) * controlDt -
                    tuning::launch::poweredHeatCoolingBase * worldDt,
                0.0,
                tuning::telemetry::heatMaximum);
        }
        if (flight.heat >= tuning::launch::pilotingCriticalThreshold) {
            flight.heatFailureSeconds += worldDt;
        } else {
            flight.heatFailureSeconds = std::max(
                0.0,
                flight.heatFailureSeconds - worldDt * 1.5);
        }
        if (flight.heatFailureSeconds >= tuning::launch::pilotingHeatFailureSeconds) {
            flight.active = false;
            flight.phase = FlightPhase::Impact;
            flight.failureCause = LaunchFailureCause::ThermalRunaway;
            result.failed = true;
            result.failureCause = flight.failureCause;
            return result;
        }
    } else {
        flight.heat = 0.0;
    }

    const double previousX = flight.positionX;
    const double previousY = flight.positionY;
    const double radiusBefore = std::max(0.0001, std::hypot(flight.positionX, flight.positionY));
    const double gravity = flightGravityAcceleration(radiusBefore, scaleProfile.landingBlend);
    flight.velocityX += (-flight.positionX / radiusBefore) * gravity * worldDt;
    flight.velocityY += (-flight.positionY / radiusBefore) * gravity * worldDt;
    flight.positionX += flight.velocityX * worldDt;
    flight.positionY += flight.velocityY * worldDt;

    if (launch.asteroidsEnabled && flight.asteroidInvulnerabilitySeconds <= 0.0) {
        for (int index = 0; index < launch.asteroidCount; ++index) {
            if (flight.asteroidHit[static_cast<std::size_t>(index)]) {
                continue;
            }
            const LaunchAsteroid& asteroid = launch.asteroids[static_cast<std::size_t>(index)];
            const double asteroidX = physicalFlightStartX +
                asteroid.routeProgress * -physicalFlightStartX;
            const double asteroidY = asteroid.courseOffset;
            if (pointToSegmentDistance(
                    asteroidX,
                    asteroidY,
                    previousX,
                    previousY,
                    flight.positionX,
                    flight.positionY) > asteroid.radius + 0.075) {
                continue;
            }
            flight.asteroidHit[static_cast<std::size_t>(index)] = true;
            flight.asteroidInvulnerabilitySeconds = tuning::launch::asteroidInvulnerabilitySeconds;
            const double damage = launchAsteroidImpactDamage(launch.hullRank, asteroid.scale);
            flight.hullRemaining = std::max(0.0, flight.hullRemaining - damage);
            flight.hullDamageTaken = std::min(
                tuning::damage::destroyedShipDamage,
                flight.hullDamageTaken + static_cast<int>(std::round(damage)));
            result.asteroidHit = true;
            result.hullDamageTaken = static_cast<int>(std::round(damage));
            if (flight.hullRemaining <= 0.0) {
                flight.active = false;
                flight.phase = FlightPhase::Impact;
                flight.failureCause = LaunchFailureCause::HullBreach;
                result.failed = true;
                result.failureCause = flight.failureCause;
            }
            break;
        }
        if (result.failed) {
            return result;
        }
    }

    const FlightKinematics kinematics = flightKinematics(
        flight.positionX,
        flight.positionY,
        flight.velocityX,
        flight.velocityY);
    const double radius = kinematics.radius;
    const double radialVelocity = kinematics.radialVelocity;
    const double tangentialVelocity = kinematics.tangentialVelocity;
    const double angle = kinematics.angle;
    flight.orbit.previousAngle = angle;

    // Keep landing telemetry live throughout approach so presentation can
    // ease toward the surface frame before the simulation crosses the local
    // landing boundary. These values do not authorize or resolve a landing.
    flight.landing.altitude = std::max(0.0, (radius - bodyRadius) * 100.0);
    flight.landing.verticalVelocity = radialVelocity * velocityToMetersPerSecond;
    flight.landing.lateralVelocity = tangentialVelocity * velocityToMetersPerSecond;
    flight.landing.surfaceAngle = std::abs(flightWrappedAngleDelta(angle, flight.heading));

    if (radius < influenceRadius) {
        flight.orbit.enteredInfluence = true;
        if (flight.phase == FlightPhase::Transfer) {
            flight.phase = FlightPhase::TargetApproach;
        }
    }

    const auto loop = flight.mode == FlightMode::Orbit ? assessOrbitLoop(flight) : OrbitLoopAssessment{};
    flight.orbit.loopQualifies = loop.qualifies;
    flight.orbit.loopPerfect = loop.perfect;
    if (!flight.orbit.captured) {
        flight.orbit.confirmationSeconds = loop.qualifies && std::abs(flight.selectedThrottle) <= 0.001 &&
            !result.asteroidHit ? flight.orbit.confirmationSeconds + realDt : 0.0;
        flight.orbit.stableAngularProgress = 0.0;
    }
    if (!flight.orbit.captured &&
        flight.orbit.confirmationSeconds >= 2.0) {
        flight.orbit.captured = true;
        flight.orbit.grade = loop.perfect
            ? OrbitGrade::Perfect
            : OrbitGrade::Good;
        flight.phase = FlightPhase::Orbiting;
        flight.orbitCelebrationPending = true;
        result.orbitCaptured = true;
    }

    const bool landingAuthorized = flight.orbit.captured || !launch.orbitRequired;
    const double approachBoundary = std::hypot(physicalFlightStartX,physicalFlightStartY)*0.4;
    if (flight.mode == FlightMode::Travel && radius <= approachBoundary && radialVelocity < 0.0) {
        flight.mode = FlightMode::Orbit;
        flight.orbitZoomProgress=0.0;
        flight.phase = flight.orbit.captured ? FlightPhase::Orbiting : FlightPhase::TargetApproach;
        flight.handoff = {FlightMode::Travel,FlightMode::Orbit,0.0,flight.positionX,flight.positionY,flight.heading};
    }
    if (!flight.landing.gateArmed && radius > flight_landing::gateRearmRadius &&
        flight.handoff.elapsed >= flight_landing::handoffSeconds) flight.landing.gateArmed = true;
    // Swept segment-circle entry: fast approaches cannot skip the gate.
    const double dx=flight.positionX-previousX, dy=flight.positionY-previousY;
    const double a=dx*dx+dy*dy, b=2.0*(previousX*dx+previousY*dy);
    const double c=previousX*previousX+previousY*previousY-
        flight_geometry::landingBoundary*flight_geometry::landingBoundary;
    const double discriminant=b*b-4.0*a*c;
    const double gateT=a>1e-14 && discriminant>=0.0 ? (-b-std::sqrt(discriminant))/(2.0*a) : -1.0;
    const auto* crossedZone = enabledLandingZoneAt(std::atan2(previousY+dy*gateT,previousX+dx*gateT));
    if (flight.mode == FlightMode::Orbit && landingAuthorized && flight.landing.gateArmed &&
        gateT>=0.0 && gateT<=1.0 &&
        crossedZone != nullptr) {
        result.landingZoneId = crossedZone->id;
        flight.positionX=previousX+dx*gateT;
        flight.positionY=previousY+dy*gateT;
        if (landingSite != nullptr) bindLandingSite(flight,*landingSite);
        enterLocalLanding(flight);
        flight.predictedTrajectory.clear();
        return result;
    }

    if (pointToSegmentDistance(0.0,0.0,previousX,previousY,flight.positionX,flight.positionY) <= bodyRadius) {
        // Outside the authorized gate this is an impact, never an alternate
        // landing path. Keep the explosion on the swept surface contact.
        const double bodyC=previousX*previousX+previousY*previousY-bodyRadius*bodyRadius;
        const double bodyDiscriminant=b*b-4.0*a*bodyC;
        if (a>1e-14 && bodyDiscriminant>=0.0) {
            const double impactT=std::clamp((-b-std::sqrt(bodyDiscriminant))/(2.0*a),0.0,1.0);
            flight.positionX=previousX+dx*impactT;
            flight.positionY=previousY+dy*impactT;
        }
        const double contactRadius=std::max(0.00001,std::hypot(flight.positionX,flight.positionY));
        const double nx=flight.positionX/contactRadius,ny=flight.positionY/contactRadius;
        const double vx=flight.velocityX*velocityToMetersPerSecond,vy=flight.velocityY*velocityToMetersPerSecond;
        const double speed=flightContactSpeed(vx,vy,flight.angularVelocity,0.0,0.0,nx,ny);
        // Planet contact in space is always fatal. Recoverable hull damage
        // belongs exclusively to the committed local Landing activity.
        const double hullBefore=flight.hullRemaining;
        flight.hullRemaining=0.0;
        flight.impact={true,speed,hullBefore,hullBefore,0.0,
            flight.positionX*velocityToMetersPerSecond,
            flight.positionY*velocityToMetersPerSecond,flight.destinationId};
        flight.impactDisplaySeconds=3.0;
        flight.active=false;
        flight.phase=FlightPhase::Impact;
        flight.failureCause=LaunchFailureCause::LunarImpact;
        flight.predictedTrajectory.clear();
        result.surfaceImpact=true;
        result.failed=true;
        result.failureCause=flight.failureCause;
        return result;
    } else if (flight.mode == FlightMode::Orbit && radius > approachBoundary*1.10 && radialVelocity > 0.0) {
        flight.mode=FlightMode::Travel;
        flight.phase=FlightPhase::Transfer;
        flight.handoff={FlightMode::Orbit,FlightMode::Travel,0.0,flight.positionX,flight.positionY,flight.heading};
    } else if (flight.mode == FlightMode::Travel && flight.orbit.enteredInfluence &&
        radius > approachBoundary*1.25 && radialVelocity > 0.0) {
        flight.active = false;
        flight.phase = FlightPhase::Flyby;
        flight.flybyRecorded = true;
        result.flyby = true;
    }
    if (std::hypot(flight.positionX,flight.positionY)>bodyRadius+0.002) {
        flight.contactEpisode=false;
    }

    flight.previousTravelProgress = flight.travelProgress;
    flight.travelProgress = std::clamp(
        (flight.positionX - physicalFlightStartX) / -physicalFlightStartX,
        0.0,
        1.0);
    flight.courseOffset = flight.positionY;
    flight.courseVelocity = flight.velocityY;
    flight.currentMultiplier = 1.0 + flight.travelProgress;
    flight.peakMultiplier = std::max(flight.peakMultiplier, flight.currentMultiplier);
    flight.projectedFuelRequired = 0.0;
    flight.projectedFuelReserve = flight.fuelRemaining;
    updatePhysicalTrajectoryPrediction(flight, landingAuthorized);
    (void)launch;
    return result;
}

LaunchFlightStep updateLocalLandingFlight(FlightRunState& flight, const PreparedLaunch& launch,
    const FlightInput& input, double deltaSeconds, const MiningRunState* site)
{
    LaunchFlightStep result;
    if (!flight.active) return result;
    const double dt=std::clamp(deltaSeconds,0.0,tuning::launch::maxFrameStepSeconds);
    auto& land=flight.landing;
    if (site) bindLandingSite(flight,*site);
    flight.elapsedSeconds+=dt;
    flight.orbitCelebrationPending=false;
    flight.touchdownCelebrationPending=false;
    const double target=-std::clamp(input.steer,-1.0,1.0)*flight_landing::turnRate;
    flight.angularVelocity=std::lerp(flight.angularVelocity,target,
        1.0-std::exp(-dt/flight_landing::turnResponseSeconds));
    double thrust=flightThrottleForInput(flight.selectedThrottle,input.enginesCut?0.0:input.throttle,dt,input.analogThrottle);
    if (flight.fuelRemaining<=0.0 && !land.departureActive) thrust=0.0;
    flight.selectedThrottle=thrust;
    flight.burnRatePerSecond=land.departureActive ? 0.0 : std::abs(thrust)*0.13;
    flight.fuelRemaining=std::max(0.0,flight.fuelRemaining-flight.burnRatePerSecond*dt);
    if (launch.heatEnabled) {
        const double powered=std::abs(thrust)>0.001
            ? (tuning::launch::poweredHeatIdleInput+tuning::launch::poweredHeatThrottleInput*thrust*thrust)*launchPoweredHeatMultiplierForRank(launch.coolingRank) : 0.0;
        const double cooling=std::abs(thrust)>0.001 ? tuning::launch::poweredHeatCoolingBase : launchEngineOffCoolingForRank(launch.coolingRank);
        flight.heat=std::clamp(flight.heat+(powered-cooling)*dt,0.0,tuning::telemetry::heatMaximum);
        flight.heatFailureSeconds=flight.heat>=tuning::launch::pilotingCriticalThreshold
            ? flight.heatFailureSeconds+dt : std::max(0.0,flight.heatFailureSeconds-dt*1.5);
        if (flight.heatFailureSeconds>=tuning::launch::pilotingHeatFailureSeconds) {
            flight.active=false; flight.phase=FlightPhase::Impact;
            flight.failureCause=LaunchFailureCause::ThermalRunaway;
            result.failed=true; result.failureCause=flight.failureCause; return result;
        }
    }
    const double t=std::clamp(flight.handoff.elapsed/flight_landing::handoffSeconds,0.0,1.0);
    const double blend=t*t*t*(t*(t*6.0-15.0)+10.0);
    const double acceleration=thrust*std::lerp(0.23*flight_landing::velocityConversion,
        thrust>=0.0 ? flight_landing::forwardAcceleration : flight_landing::reverseAcceleration,blend);
    const double gravity=std::lerp(0.52*0.4*flight_landing::velocityConversion,
        flight_landing::gravityAcceleration,blend);
    // Substeps sweep the entire ship footprint through real terrain, even on
    // dangerous fast entries. These are local metres and real seconds.
    const double sweepSpeed=std::hypot(land.lateralVelocity,land.verticalVelocity)+
        std::abs(flight.angularVelocity)*std::hypot(flight_landing::hullHalfWidth,flight_landing::hullHalfHeight)+10.0;
    const int steps=std::max(1,static_cast<int>(std::ceil(std::max(dt*240.0,sweepSpeed*dt/0.10))));
    const double step=dt/steps;
    for (int i=0;i<steps;++i) {
        const LandingState previousPose=land;
        land.heading+=flight.angularVelocity*step;
        land.lateralVelocity+=std::cos(land.heading)*acceleration*step;
        land.verticalVelocity+=(std::sin(land.heading)*acceleration-gravity)*step;
        land.horizontalPosition+=land.lateralVelocity*step;
        land.altitude+=land.verticalVelocity*step;
        land.surfaceAngle=std::abs(flightWrappedAngleDelta(1.5707963267948966,land.heading));
        auto contacts=site && land.siteBound ? localLandingContacts(land,*site) : std::vector<FlightSurfaceContact>{};
        if (!contacts.empty() && localLandingContacts(previousPose,*site).empty()) {
            // Locate first contact inside this small sweep, before frame-step
            // penetration adds spurious speed or damage. Includes rotation.
            const LandingState advanced=land;
            const auto poseAt=[&](double fraction) {
                LandingState pose=advanced;
                pose.horizontalPosition=std::lerp(previousPose.horizontalPosition,advanced.horizontalPosition,fraction);
                pose.altitude=std::lerp(previousPose.altitude,advanced.altitude,fraction);
                pose.heading=std::lerp(previousPose.heading,advanced.heading,fraction);
                pose.lateralVelocity=std::lerp(previousPose.lateralVelocity,advanced.lateralVelocity,fraction);
                pose.verticalVelocity=std::lerp(previousPose.verticalVelocity,advanced.verticalVelocity,fraction);
                pose.surfaceAngle=std::abs(flightWrappedAngleDelta(1.5707963267948966,pose.heading));
                return pose;
            };
            double low=0.0,high=1.0;
            for (int sweep=0;sweep<12;++sweep) {
                const double mid=(low+high)*0.5;
                if (localLandingContacts(poseAt(mid),*site).empty()) low=mid; else high=mid;
            }
            land=poseAt(high);
            contacts=localLandingContacts(land,*site);
        }
        if (!contacts.empty()) {
            const bool newContact=!flight.contactEpisode;
            double speed=0.0;
            FlightSurfaceContact strongest=contacts.front();
            for (const auto& contact:contacts) {
                const double closing=flightContactSpeed(land.lateralVelocity,land.verticalVelocity,flight.angularVelocity,
                    contact.pointX-land.horizontalPosition,
                    contact.pointY-land.altitude-flight_landing::hullHalfHeight,contact.normalX,contact.normalY);
                if (closing>speed) {speed=closing;strongest=contact;}
            }
            if (newContact) {
                applyFlightImpact(flight,speed,strongest.pointX,strongest.pointY);
                result.surfaceImpact=result.surfaceImpact || speed>1.0;
            }
            flight.contactEpisode=true;flight.contactClearSeconds=0.0;
            if (flight.hullRemaining<=0.0) {
                flight.active=false;
                flight.phase=FlightPhase::Impact; flight.failureCause=LaunchFailureCause::LunarImpact;
                result.failed=true; result.failureCause=flight.failureCause;
                break;
            }
            // Damage owns survival; posture owns sticking the landing. A
            // survivable hard contact must not be rejected by rebound speed.
            const auto support=std::find_if(contacts.begin(),contacts.end(),
                [](const auto& contact){return contact.suitable;});
            if (!land.launchSupportActive && land.verticalVelocity<=0.0 &&
                support!=contacts.end() && land.surfaceAngle<=flight_landing::stickTiltRadians+1e-9) {
                land.touchdownGridX=support->gridX;land.touchdownGridY=support->gridY;
                land.hardLanding=flight.impact.valid && flight.impact.damage>0.0;
                land.lateralVelocity=land.verticalVelocity=0.0;
                flight.angularVelocity=0.0;
                flight.selectedThrottle=flight.burnRatePerSecond=0.0;
                flight.active=false;
                flight.phase=FlightPhase::Landed; flight.touchdownCelebrationPending=true;
                result.reachedDestination=true; result.hardTouchdown=land.hardLanding; result.safeTouchdown=!land.hardLanding;
                break;
            }
            // One damage episode; several positional corrections may be needed
            // at a corner. Tile count must not multiply impact damage or drag.
            if (newContact) {
                reboundFlightContact(land.lateralVelocity,land.verticalVelocity,flight.angularVelocity,
                    strongest.normalX,strongest.normalY,strongest.pointX-land.horizontalPosition,
                    strongest.pointY-land.altitude-flight_landing::hullHalfHeight);
            } else {
                // Continued support cancels penetration without repeatedly
                // halving steering authority or multiplying tangential drag.
                land.lateralVelocity+=strongest.normalX*speed;
                land.verticalVelocity+=strongest.normalY*speed;
            }
            for (int separation=0;separation<12 && !contacts.empty();++separation) {
                const auto deepest=*std::max_element(contacts.begin(),contacts.end(),[](const auto& a,const auto& b){return a.penetration<b.penetration;});
                land.horizontalPosition+=deepest.normalX*(deepest.penetration+0.001);
                land.altitude+=deepest.normalY*(deepest.penetration+0.001);
                const double inward=land.lateralVelocity*deepest.normalX+land.verticalVelocity*deepest.normalY;
                if (inward<0.0) {land.lateralVelocity-=inward*deepest.normalX;land.verticalVelocity-=inward*deepest.normalY;}
                contacts=localLandingContacts(land,*site);
            }
        } else {
            flight.contactClearSeconds+=step;
            if (flight.contactClearSeconds>=0.05) {
                flight.contactEpisode=false;
            }
        }
        if (land.altitude>=flight_landing::departureAltitude &&
            land.verticalVelocity>=flight_landing::departureSpeed) {
            leaveLocalLanding(flight); break;
        }
    }
    // Separation nudges create millimetre air gaps while the engine ramps.
    // Those are not a completed liftoff. Keep the departure latch until the
    // whole hull is clear of nearby rock and has actually started ascending.
    // This is only a ceremony latch: collision response and damage stay live.
    if (land.launchSupportActive && flight.mode==FlightMode::Landing && flight.active && site) {
        if (land.verticalVelocity>0.0 &&
            flight.contactClearSeconds>=flight_landing::takeoffClearSeconds &&
            localLandingContacts(land,*site,flight_landing::takeoffClearanceMeters).empty()) {
            land.launchSupportActive=false;
        }
    }
    flight.predictedTrajectory.clear();
    if (flight.mode == FlightMode::Landing && flight.active) {
        constexpr int samples = 96;
        constexpr double step = 3.84 / (samples - 1);
        LandingState predicted = land;
        const double nx = std::cos(land.basisAngle), ny = std::sin(land.basisAngle);
        const auto append = [&](const LandingState& point) {
            const double radius = flight_geometry::bodyRadius +
                point.altitude / flight_landing::metersPerOrbitUnit;
            flight.predictedTrajectory.push_back({
                nx*radius + ny*point.horizontalPosition/flight_landing::metersPerOrbitUnit,
                ny*radius - nx*point.horizontalPosition/flight_landing::metersPerOrbitUnit});
        };
        const auto advance = [](LandingState p, double dt) {
            p.horizontalPosition += p.lateralVelocity*dt;
            p.altitude += p.verticalVelocity*dt - 0.5*flight_landing::gravityAcceleration*dt*dt;
            p.verticalVelocity -= flight_landing::gravityAcceleration*dt;
            return p;
        };
        const auto endsForecast = [&](const LandingState& p) {
            bool suitable = false; double gx = 0.0, gy = 0.0;
            return (site && localLandingContact(p,*site,suitable,gx,gy)) ||
                (p.altitude >= flight_landing::departureAltitude &&
                 p.verticalVelocity >= flight_landing::departureSpeed);
        };
        append(predicted);
        if (!endsForecast(predicted)) for (int i = 1; i < samples; ++i) {
            const LandingState next = advance(predicted, step);
            if (endsForecast(next)) {
                double low = 0.0, high = step;
                for (int iteration = 0; iteration < 16; ++iteration) {
                    const double mid = (low+high)*0.5;
                    if (endsForecast(advance(predicted,mid))) high = mid; else low = mid;
                }
                append(advance(predicted,high));
                break;
            }
            predicted = next;
            append(predicted);
        }
    }
    return result;
}

LaunchFlightStep updatePhysicalFlight(FlightRunState& flight,const PreparedLaunch& launch,
    const FlightInput& input,double dt,const MiningRunState* site)
{
    flight.handoff.elapsed=std::min(flight_landing::handoffSeconds,flight.handoff.elapsed+std::max(0.0,dt));
    flight.impactDisplaySeconds=std::max(0.0,flight.impactDisplaySeconds-std::max(0.0,dt));
    auto result=flight.mode==FlightMode::Landing ? updateLocalLandingFlight(flight,launch,input,dt,site)
        : updateSpaceFlight(flight,launch,input,dt,site);
    flight.hullDamageTaken=physicalFlightCampaignDamage(flight,launch.existingShipDamage);
    return result;
}

} // namespace

LaunchFlightStep updateLaunchFlight(
    FlightRunState& flight,
    const PreparedLaunch& launch,
    const Destination& destination,
    const FlightInput& input,
    double deltaSeconds,
    const MiningRunState* landingSite)
{
    if (flight.physicalFlight) {
        return updatePhysicalFlight(flight, launch, input, deltaSeconds, landingSite);
    }
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
    const double targetVelocity = poweredLaunchTargetVelocity(
        launch,
        destination,
        flight.selectedThrottle);

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
            : (flight.returningHome
                  ? launch.cruiseFuelCost * launchFuelUseMultiplier(flight.selectedThrottle)
                  : launchPoweredFuelCost(
                        launch.cruiseFuelCost,
                        flight.selectedThrottle,
                        launch.slingshotFuelSavings));
        flight.fuelRemaining = std::max(
            0.0,
            flight.fuelRemaining - traveledDistance * fuelUsePerProgress);
    }
    flight.currentMultiplier = 1.0 + targetSpan * flight.travelProgress;
    flight.peakMultiplier = std::max(flight.peakMultiplier, flight.currentMultiplier);
    const double projectedDistance = flight.returningHome
        ? flight.travelProgress
        : (launch.config.frontierTransfer ? 1.0 - flight.travelProgress : flight.travelProgress);
    const double projectedFuelPerProgress = flight.returningHome
        ? launch.cruiseFuelCost * launchFuelUseMultiplier(flight.selectedThrottle)
        : launchPoweredFuelCost(
              launch.cruiseFuelCost,
              flight.selectedThrottle,
              launch.slingshotFuelSavings);
    flight.projectedFuelRequired = std::max(0.0, projectedDistance) *
        projectedFuelPerProgress +
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

TelemetryEvent launchTelemetryAt(const PreparedLaunch& launch, const FlightRunState& flight)
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
    outcome.routeTransit = launch.config.routeTransit;
    outcome.crashMultiplier = launch.crashMultiplier;
    outcome.ejectMultiplier = std::max(1.0, burnMultiplier);
    outcome.pilotedFlight = resolution.pilotedFlight;
    outcome.failureCause = resolution.failureCause;
    outcome.fuelSurveyReturnTiming = resolution.fuelSurveyReturnTiming;
    outcome.minimumSafetyMargin = resolution.minimumSafetyMargin;
    outcome.slingshotFuelSavings = launch.slingshotFuelSavings;
    outcome.slingshotSpeedBoost = launch.slingshotSpeedBoost;
    outcome.slingshotInstabilityPenalty = launch.slingshotInstabilityPenalty;
    outcome.transferAssistId = launch.transferAssistId;

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
        if (launch.config.missionKind == LaunchMissionKind::StraylightApproach) {
            outcome.payout = 0.0;
            outcome.shipDamage = 0;
            outcome.blueprintGain = 0;
            return outcome;
        }
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
