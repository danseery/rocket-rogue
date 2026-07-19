#include "core/LaunchSimulation.h"
#include "core/GameMath.h"
#include "core/GameText.h"
#include "core/LaunchBalance.h"
#include "core/Telemetry.h"
#include "core/Tuning.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace rocket {

namespace {

double crewTrainingBonus(const GameState& state, const ContentCatalog& catalog)
{
    const Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        return 0.0;
    }

    double bonus = static_cast<double>(effectiveTrainingLevel(*astronaut)) * tuning::crew::effectiveTrainingPerformanceBonus;
    const double traitMultiplier = 1.0 + std::max(0.0, aggregateCrewUpgradeStats(state, catalog).traitModifier);

    if (astronaut->trait == tuning::traits::calmUnderHeat) {
        bonus += tuning::traits::calmUnderHeatBonus * traitMultiplier;
    } else if (astronaut->trait == tuning::traits::hardReboot) {
        bonus += tuning::traits::hardRebootPerformanceBonus * traitMultiplier;
    } else if (astronaut->trait == tuning::traits::readsTelemetryEarly) {
        bonus += tuning::traits::readsTelemetryEarlyBonus * traitMultiplier;
    } else if (astronaut->trait == tuning::traits::phaseShift) {
        bonus += tuning::traits::phaseShiftPerformanceBonus * traitMultiplier;
    } else if (astronaut->trait == tuning::traits::improvesEjectionOdds) {
        bonus += tuning::traits::improvesEjectionOddsPerformanceBonus * traitMultiplier;
    }

    return bonus;
}

double crewEscapeBonus(const GameState& state, const ContentCatalog& catalog)
{
    const Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        return 0.0;
    }

    double bonus = static_cast<double>(astronaut->training) * tuning::crew::escapeBonusPerTraining;
    if (astronaut->trait == tuning::traits::improvesEjectionOdds) {
        bonus += tuning::traits::improvesEjectionOddsEscapeBonus * (1.0 + std::max(0.0, aggregateCrewUpgradeStats(state, catalog).traitModifier));
    }
    return bonus;
}

std::vector<TelemetryEvent> buildTelemetry(const PreparedLaunch& launch)
{
    std::vector<TelemetryEvent> events;
    events.reserve(tuning::launch::telemetrySampleCount);

    const double maxSample = std::max(launch.config.burnGoalMultiplier, launch.crashMultiplier);
    for (int i = 0; i < tuning::launch::telemetrySampleCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(tuning::launch::telemetrySampleCount - 1);
        const double multiplier = 1.0 + (maxSample - 1.0) * t;
        events.push_back(telemetryAt(launch, multiplier));
    }

    return events;
}

TelemetryEvent peakTelemetryThrough(const PreparedLaunch& launch, double endMultiplier)
{
    TelemetryEvent peak;
    const double maxSample = std::max(1.0, endMultiplier);
    for (int i = 0; i < tuning::launch::peakTelemetrySampleCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(tuning::launch::peakTelemetrySampleCount - 1);
        const double multiplier = 1.0 + (maxSample - 1.0) * t;
        const TelemetryEvent sample = telemetryAt(launch, multiplier);
        if (sample.warning >= peak.warning) {
            peak = sample;
        }
        peak.abortRisk = std::max(peak.abortRisk, sample.abortRisk);
        peak.stress = std::max(peak.stress, sample.stress);
    }
    return peak;
}

std::string warningMessage(const TelemetryEvent& event)
{
    const TelemetryChannelSample worst = strongestTelemetrySample(event);

    if (worst.value > tuning::launch::warningCriticalThreshold) {
        return std::string(worst.warningCopy.critical);
    }
    if (worst.value > tuning::launch::warningCautionThreshold) {
        return std::string(worst.warningCopy.caution);
    }

    return std::string(text::telemetry::nominal);
}

bool isShallowRecovery(const Destination& destination, double multiplier)
{
    return multiplier < 1.0 + (destination.targetMultiplier - 1.0) * tuning::rewards::shallowRecoveryTargetShare;
}

bool isCleanShallowRecovery(const Destination& destination, const LaunchOutcome& outcome)
{
    return isShallowRecovery(destination, outcome.ejectMultiplier)
        && outcome.peakWarning < tuning::rewards::cleanShallowRecoveryWarningThreshold;
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
    return penalty;
}

double overburnJackpotMultiplier(const Destination& destination, double burnMultiplier)
{
    const double overGoal = std::max(0.0, burnMultiplier - destination.targetMultiplier);
    if (overGoal <= 0.0) {
        return 1.0;
    }

    const double normalizedOverburn = overGoal / std::max(tuning::launch::overburnMinimumDenominator, destination.targetMultiplier - 1.0);
    return std::clamp(std::exp(normalizedOverburn * tuning::launch::overburnExponent), 1.0, tuning::launch::overburnMaximumMultiplier);
}

void markDestroyed(LaunchOutcome& outcome, const PreparedLaunch& launch, const ContentCatalog& catalog, const Destination& destination, const GameState& state, Random& rng)
{
    outcome.type = LaunchResultType::Destroyed;
    outcome.shipDamage = tuning::damage::destroyedShipDamage;
    outcome.blueprintGain = std::max(0, destination.tier / 2);

    const double baseSurvival = outcome.recoveryMethod == RecoveryMethod::ManualEject
        ? tuning::outcomes::manualEjectSurvivalBase
        : tuning::outcomes::vehicleLossSurvivalBase;
    const double survivalChance = std::clamp(
        baseSurvival +
            launch.stats.escape * tuning::outcomes::survivalEscapeScale +
            crewEscapeBonus(state, catalog) -
            destination.hazard * tuning::outcomes::survivalHazardScale,
        tuning::outcomes::survivalMinimum,
        tuning::outcomes::survivalMaximum);
    const bool survived = rng.chance(survivalChance);
    outcome.crewKilled = !survived;
    outcome.crewInjured = survived && rng.chance(outcome.recoveryMethod == RecoveryMethod::ManualEject
        ? tuning::outcomes::manualEjectInjuryChance
        : tuning::outcomes::vehicleLossInjuryChance);

    if (!state.run.equippedModuleIds.empty() && rng.chance(tuning::damage::moduleLossChance)) {
        const int index = rng.rangeInt(0, static_cast<int>(state.run.equippedModuleIds.size()) - 1);
        outcome.moduleDestroyedId = state.run.equippedModuleIds[static_cast<std::size_t>(index)];
    }
}

double telemetryWave(double multiplier, double crashMultiplier, double frequency, double phase)
{
    return 0.5 + std::sin(multiplier * frequency + crashMultiplier * phase) * 0.5;
}

double telemetryPulse(const tuning::telemetry::PulseProfile& profile, double multiplier, double crashMultiplier)
{
    return profile.base + telemetryWave(multiplier, crashMultiplier, profile.frequency, profile.phase) * profile.waveScale;
}

int destinationHistoryIndex(const ContentCatalog& catalog, std::string_view destinationId)
{
    for (std::size_t i = 0; i < catalog.destinations.size(); ++i) {
        if (catalog.destinations[i].id == destinationId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int destinationHistoryValue(const std::vector<int>& values, int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= values.size()) {
        return 0;
    }
    return values[static_cast<std::size_t>(index)];
}

double incidentPulse(const TelemetryIncident& incident, double multiplier)
{
    const double distance = std::abs(multiplier - incident.centerMultiplier);
    const double pulse = std::clamp(1.0 - distance / std::max(0.01, incident.width), 0.0, 1.0);
    return math::smoothStep(pulse);
}

double dampened(double amount, double mitigation)
{
    return std::clamp(amount - std::max(0.0, mitigation), tuning::launch::dampenedMinimum, tuning::launch::dampenedMaximum);
}

double returnHomeNetRewardFloor(const PreparedLaunch& launch, const Destination& destination, double burnMultiplier)
{
    if (launch.config.frontierTransfer) {
        return 0.0;
    }

    const double dataGoal = std::min(launch.config.burnGoalMultiplier, destination.targetMultiplier);
    if (burnMultiplier + 0.000001 < dataGoal) {
        return 0.0;
    }

    if (burnMultiplier + 0.000001 >= destination.targetMultiplier) {
        return static_cast<double>(moduleOfferCost(Rarity::Rare));
    }

    const double uncommonThreshold = dataGoal + (destination.targetMultiplier - dataGoal) * tuning::rewards::pushedProfileShelfShare;
    if (burnMultiplier + 0.000001 >= uncommonThreshold) {
        return static_cast<double>(moduleOfferCost(Rarity::Uncommon));
    }

    return static_cast<double>(moduleOfferCost(Rarity::Common));
}

} // namespace

PreparedLaunch prepareLaunch(const GameState& state, const ContentCatalog& catalog, Random& rng)
{
    PreparedLaunch launch;
    launch.config = state.launchConfig;
    launch.stats = aggregateShipStats(state, catalog);
    launch.slingshotFuelBoost = std::max(0.0, state.run.nextLaunchFuelBoost);
    launch.slingshotSpeedBoost = std::max(0.0, state.run.nextLaunchSpeedBoost);
    launch.stats.fuel += launch.slingshotFuelBoost;

    const Destination* configuredDestination = catalog.findDestination(launch.config.destinationId);
    const Destination& destination = configuredDestination == nullptr ? currentDestination(state, catalog) : *configuredDestination;
    launch.config.destinationId = destination.id;
    launch.config.frameId = state.run.frameId;
    launch.config.equippedModuleIds = state.run.equippedModuleIds;

    const double performance = launch_balance::performanceScore(launch.stats, crewTrainingBonus(state, catalog));

    const double missionPressure = missionPressureModifier(state, catalog, destination);
    const int historyIndex = destinationHistoryIndex(catalog, destination.id);
    const int attempts = destinationHistoryValue(state.meta.destinationAttempts, historyIndex);
    const int successes = destinationHistoryValue(state.meta.destinationSuccesses, historyIndex);
    const int requiredReadiness = frontierReadinessRequired(state, catalog);
    const int currentReadiness = std::max(0, state.run.frontierReadiness);
    const double readinessRatio = launch_balance::readinessRatio(currentReadiness, requiredReadiness);
    launch.overpreparedData = launch_balance::overpreparedData(currentReadiness, requiredReadiness);
    launch.provingPayoutBonus = launch_balance::provingPayoutBonus(launch.overpreparedData, launch.config.frontierTransfer);
    const double transferPrep = launch_balance::transferPreparation(readinessRatio, launch.overpreparedData, launch.config.frontierTransfer);
    const double transferHazard = launch_balance::transferHazard(destination, transferPrep, launch.config.frontierTransfer);
    const double unprovenHazard = launch_balance::unprovenHazard(attempts, successes);
    const double hazard = launch_balance::launchHazard(destination, launch.stats, state.run.shipDamage, transferHazard, unprovenHazard);
    const double safety = launch_balance::launchSafety(performance, hazard);
    const bool longShotBreak = successes == 0 && rng.chance(launch_balance::longShotChance(attempts, launch.stats.sensors));
    const double tail = std::pow(rng.next01(), 1.0 / safety);

    const double minCrash = destination.minCrashMultiplier;
    const double transferCeilingPenalty = launch_balance::transferCeilingPenalty(destination, performance, readinessRatio, launch.overpreparedData, launch.config.frontierTransfer);
    const double unprovenPrepRelief = launch_balance::unprovenPrepRelief(readinessRatio, launch.overpreparedData, launch.config.frontierTransfer);
    const double unprovenCeilingPenalty = launch_balance::unprovenCeilingPenalty(attempts, successes, unprovenPrepRelief);
    const double pressureCeilingPenalty = launch_balance::pressureCeilingPenalty(successes, missionPressure);
    const double longShotBonus = longShotBreak
        ? rng.range(tuning::launch::longShotBonusMinimum, tuning::launch::longShotBonusMaximum)
        : 0.0;
    const double transferReadinessCeilingBonus = launch_balance::transferReadinessCeilingBonus(readinessRatio, launch.overpreparedData, launch.config.frontierTransfer);
    const double overpreparedCeilingBonus = launch_balance::provingOverpreparedCeilingBonus(launch.overpreparedData, launch.config.frontierTransfer);
    const double maxCrash = launch_balance::maxCrashCeiling(destination, transferCeilingPenalty, unprovenCeilingPenalty, pressureCeilingPenalty, longShotBonus, transferReadinessCeilingBonus, overpreparedCeilingBonus, safety);
    launch.crashMultiplier = std::clamp(minCrash + (maxCrash - minCrash) * tail, minCrash, maxCrash);
    launch.sensorQuality = launch_balance::sensorQuality(launch.stats);
    const double transferHeatLoad = launch_balance::transferHeatLoad(destination, launch.config.frontierTransfer);
    launch.heatRate = launch_balance::heatRate(destination, launch.stats, transferHeatLoad);
    launch.pressureModifier = launch_balance::pressureModifier(missionPressure, launch.stats);
    launch.throttleFactor = 1.0;
    launch.cutHeatRelief = 0.0;
    launch.cutVibrationRelief = 0.0;
    launch.cutGuidancePenalty = 0.0;
    launch.pressureRelief = 0.0;
    launch.pressureReliefFailure = 0.0;
    launch.reliefGuidancePenalty = 0.0;
    launch.cargoFuelRelief = 0.0;
    launch.cargoGuidancePenalty = 0.0;
    launch.cargoVibrationPenalty = 0.0;
    launch.cargoReturnPenalty = 0.0;
    if (const Astronaut* astronaut = activeAstronaut(state)) {
        launch.crewStressSteps = crewStressStepCount(astronaut->stress);
        launch.crewGuidancePenalty = crewNavigationPenaltyFromStress(astronaut->stress);
        launch.crewAbortMultiplier = crewAbortRiskMultiplierFromStress(astronaut->stress);
    }
    launch.incidentCount = launch_balance::incidentCount(destination, launch.config.frontierTransfer, static_cast<int>(launch.incidents.size()));

    const double profileEnd = std::max(launch.config.burnGoalMultiplier, destination.targetMultiplier);
    const double profileSpan = launch_balance::incidentProfileSpan(profileEnd);
    const double incidentSeverity = launch_balance::incidentSeverity(destination, launch.stats, state.run.shipDamage, missionPressure);

    for (int i = 0; i < launch.incidentCount; ++i) {
        TelemetryIncident incident;
        const double lane = launch_balance::incidentLane(
            i,
            launch.incidentCount,
            rng.range(tuning::launch::incidentLaneJitterMinimum, tuning::launch::incidentLaneJitterMaximum));
        incident.centerMultiplier = launch_balance::incidentCenterMultiplier(profileSpan, lane, launch.crashMultiplier);
        incident.width = launch_balance::incidentWidth(
            destination,
            rng.range(tuning::launch::incidentWidthMinimum, tuning::launch::incidentWidthMaximum));

        const double amount = launch_balance::incidentAmount(
            incidentSeverity,
            rng.range(tuning::launch::incidentAmountMinimum, tuning::launch::incidentAmountMaximum));
        switch (rng.rangeInt(0, tuning::launch::incidentVariantCount - 1)) {
        case 0:
            incident.heat = dampened(amount + tuning::launch::incidentHeatOffset, launch.stats.cooling * tuning::launch::incidentHeatCoolingMitigation);
            incident.pressure = dampened(amount * tuning::launch::incidentHeatPressureScale, launch.stats.pressure * tuning::launch::incidentHeatPressureMitigation);
            break;
        case 1:
            incident.pressure = dampened(
                amount + tuning::launch::incidentPressureOffset,
                launch.stats.pressure * tuning::launch::incidentPressureControlMitigation +
                    launch.stats.fuel * tuning::launch::incidentPressureFuelMitigation);
            incident.fuelMix = dampened(amount * tuning::launch::incidentPressureFuelMixScale, launch.stats.fuel * tuning::launch::incidentPressureFuelMixMitigation);
            break;
        case 2:
            incident.vibration = dampened(amount + tuning::launch::incidentVibrationOffset, launch.stats.hull * tuning::launch::incidentVibrationHullMitigation);
            incident.guidance = dampened(amount * tuning::launch::incidentVibrationGuidanceScale, launch.stats.sensors * tuning::launch::incidentVibrationGuidanceMitigation);
            break;
        case 3:
            incident.fuelMix = dampened(amount + tuning::launch::incidentFuelMixOffset, launch.stats.fuel * tuning::launch::incidentFuelMixFuelMitigation);
            incident.pressure = dampened(amount * tuning::launch::incidentFuelMixPressureScale, launch.stats.pressure * tuning::launch::incidentFuelMixPressureMitigation);
            break;
        case 4:
            incident.guidance = dampened(amount + tuning::launch::incidentGuidanceOffset, launch.stats.sensors * tuning::launch::incidentGuidanceSensorMitigation);
            incident.vibration = dampened(amount * tuning::launch::incidentGuidanceVibrationScale, launch.stats.hull * tuning::launch::incidentGuidanceVibrationMitigation);
            break;
        default:
            incident.abortRisk = dampened(
                amount + tuning::launch::incidentAbortOffset,
                launch.stats.escape * tuning::launch::incidentAbortEscapeMitigation +
                    launch.stats.sensors * tuning::launch::incidentAbortSensorMitigation);
            incident.guidance = dampened(amount * tuning::launch::incidentAbortGuidanceScale, launch.stats.sensors * tuning::launch::incidentAbortGuidanceMitigation);
            break;
        }

        launch.incidents[static_cast<std::size_t>(i)] = incident;
    }

    return launch;
}

PreparedLaunch withCutEngines(const PreparedLaunch& launch)
{
    PreparedLaunch throttled = launch;
    throttled.throttleFactor = tuning::launch::cutEngineThrottleFactor;
    throttled.cutHeatRelief = tuning::launch::cutEngineHeatRelief;
    throttled.cutVibrationRelief = tuning::launch::cutEngineVibrationRelief;
    throttled.cutGuidancePenalty = tuning::launch::cutEngineGuidancePenalty;
    return throttled;
}

PreparedLaunch withPressureRelief(const PreparedLaunch& launch, bool failed)
{
    PreparedLaunch relieved = launch;
    relieved.pressureRelief = failed ? tuning::launch::pressureReliefFailedAmount : tuning::launch::pressureReliefSuccessAmount;
    relieved.pressureReliefFailure = failed ? tuning::launch::pressureReliefFailurePenalty : 0.0;
    relieved.reliefGuidancePenalty = failed ? tuning::launch::pressureReliefFailedGuidancePenalty : tuning::launch::pressureReliefGuidancePenalty;
    return relieved;
}

PreparedLaunch withJettisonedCargo(const PreparedLaunch& launch)
{
    PreparedLaunch lightened = launch;
    lightened.cargoFuelRelief = tuning::launch::cargoFuelRelief;
    lightened.cargoGuidancePenalty = tuning::launch::cargoGuidancePenalty;
    lightened.cargoVibrationPenalty = tuning::launch::cargoVibrationPenalty;
    lightened.cargoReturnPenalty = tuning::launch::cargoReturnPenalty;
    return lightened;
}

PreparedLaunch applyFlightActions(const PreparedLaunch& launch, const FlightActionState& actions)
{
    PreparedLaunch modified = launch;
    if (actions.pressureReliefOpen || actions.pressureReliefFailed) {
        modified = withPressureRelief(modified, actions.pressureReliefFailed);
    }
    if (actions.cargoJettisoned) {
        modified = withJettisonedCargo(modified);
    }
    if (actions.cutEnginesActive && !actions.returningHome) {
        modified = withCutEngines(modified);
    }
    return modified;
}

double burnMultiplierDelta(const PreparedLaunch& launch, const Destination& destination, double elapsedSeconds, double deltaSeconds)
{
    const double dt = std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds);
    const double thrust = std::max(tuning::launch::minimumEffectiveThrust, launch.stats.thrust);
    const double cruiseRate =
        tuning::launch::cruiseBaseRate +
        thrust * tuning::launch::cruiseThrustScale +
        static_cast<double>(destination.tier) * tuning::launch::cruiseTierScale;
    const double acceleration = (tuning::launch::accelerationBaseRate + destination.hazard * tuning::launch::accelerationHazardScale) * launch.throttleFactor;
    const double startRate = cruiseRate * launch.throttleFactor + std::max(0.0, elapsedSeconds) * acceleration;
    const double speedMultiplier = std::max(0.0, tuning::launch::baseTravelSpeedMultiplier + launch.slingshotSpeedBoost);
    return std::max(0.0, dt * startRate + 0.5 * dt * dt * acceleration) * speedMultiplier;
}

double returnTelemetryMultiplier(double commitMultiplier, double crashMultiplier, double returnElapsed, double returnDuration)
{
    const double progress = std::clamp(returnElapsed / std::max(tuning::session::returnTelemetryProgressDenominator, returnDuration), 0.0, 1.0);
    const double shaped = math::smoothStep(progress);
    const double headroom = std::max(tuning::session::returnTelemetryHeadroomMinimum, crashMultiplier - commitMultiplier);
    const double overshoot = std::min(
        headroom * tuning::session::returnTelemetryOvershootHeadroomScale,
        tuning::session::returnTelemetryOvershootBase + headroom * tuning::session::returnTelemetryOvershootExtraHeadroomScale);
    const double bump = std::sin(shaped * math::pi) * overshoot;
    const double settle = shaped * std::min(headroom * tuning::session::returnTelemetrySettleHeadroomScale, tuning::session::returnTelemetrySettleMaximum);
    return std::min(crashMultiplier - tuning::session::returnTelemetryCrashMargin, commitMultiplier + bump - settle);
}

double returnHomeRisk(const PreparedLaunch& launch, const ContentCatalog& catalog, const GameState& state, double burnMultiplier)
{
    const Destination* destination = catalog.findDestination(launch.config.destinationId);
    if (destination == nullptr) {
        return 1.0;
    }

    const TelemetryEvent event = telemetryAt(launch, burnMultiplier);
    const double profileDepth = std::clamp(
        (burnMultiplier - 1.0) / std::max(tuning::session::minTravelDenominator, destination->targetMultiplier - 1.0),
        0.0,
        tuning::outcomes::returnProfileDepthMaximum);
    const double systemsRelief =
        std::max(0.0, launch.stats.hull) * tuning::outcomes::returnSystemsHullRelief +
        std::max(0.0, launch.stats.cooling) * tuning::outcomes::returnSystemsCoolingRelief +
        std::max(0.0, launch.stats.fuel) * tuning::outcomes::returnSystemsFuelRelief +
        std::max(0.0, launch.stats.sensors) * tuning::outcomes::returnSystemsSensorsRelief;
    const double transferPenalty = launch.config.frontierTransfer
        ? tuning::outcomes::returnTransferBasePenalty + static_cast<double>(destination->tier) * tuning::outcomes::returnTransferTierPenalty
        : 0.0;

    const double risk =
        tuning::outcomes::returnRiskBase +
        destination->hazard * tuning::outcomes::returnRiskHazardScale +
        profileDepth * tuning::outcomes::returnRiskProfileDepthScale +
        event.warning * tuning::outcomes::returnRiskWarningScale +
        event.heat * tuning::outcomes::returnRiskHeatScale +
        static_cast<double>(state.run.shipDamage) * tuning::outcomes::returnRiskDamageScale +
        transferPenalty -
        systemsRelief +
        launch.cargoReturnPenalty;

    return std::clamp(risk, tuning::outcomes::returnRiskMinimum, tuning::outcomes::returnRiskMaximum);
}

LaunchOutcome resolveLaunch(const PreparedLaunch& launch, const ContentCatalog& catalog, const GameState& state, double burnMultiplier, RecoveryMethod method, Random& rng)
{
    LaunchOutcome outcome;
    outcome.recoveryMethod = method;
    outcome.destinationId = launch.config.destinationId;
    outcome.assignedAstronautId = launch.config.astronautId;
    outcome.frontierTransfer = launch.config.frontierTransfer;
    outcome.crashMultiplier = launch.crashMultiplier;
    outcome.ejectMultiplier = std::max(1.0, burnMultiplier);
    outcome.telemetry = buildTelemetry(launch);
    const TelemetryEvent peak = peakTelemetryThrough(launch, outcome.ejectMultiplier);
    outcome.peakWarning = peak.warning;
    outcome.peakAbortRisk = peak.abortRisk;

    const Destination* destination = catalog.findDestination(launch.config.destinationId);
    if (destination == nullptr) {
        outcome.type = LaunchResultType::Destroyed;
        outcome.shipDamage = tuning::damage::destroyedShipDamage;
        return outcome;
    }

    if (method == RecoveryMethod::None || outcome.ejectMultiplier >= launch.crashMultiplier) {
        markDestroyed(outcome, launch, catalog, *destination, state, rng);
        return outcome;
    }

    const double payoutMultiplier = 1.0 + std::max(0.0, launch.stats.payout) * tuning::outcomes::payoutStatScale + launch.provingPayoutBonus;
    const bool reachedDestination = outcome.ejectMultiplier >= destination->targetMultiplier;
    const TelemetryEvent event = telemetryAt(launch, outcome.ejectMultiplier);

    if (method == RecoveryMethod::ReturnHome && rng.chance(returnHomeRisk(launch, catalog, state, outcome.ejectMultiplier))) {
        markDestroyed(outcome, launch, catalog, *destination, state, rng);
        return outcome;
    }

    if (method == RecoveryMethod::ManualEject) {
        if (isCleanShallowRecovery(*destination, outcome)
            && state.run.cleanShallowRecoveryStreak + 1 >= tuning::rewards::cleanShallowRecoveryDestructionStreak) {
            markDestroyed(outcome, launch, catalog, *destination, state, rng);
            outcome.recoveryCost = 0.0;
            return outcome;
        }

        outcome.type = LaunchResultType::SafeEject;
        outcome.payout = destination->baseReward * outcome.ejectMultiplier * payoutMultiplier * tuning::rewards::manualEjectPayoutFactor;
        outcome.recoveryCost = std::clamp(
            tuning::outcomes::manualEjectRecoveryBase +
                static_cast<double>(destination->tier) * tuning::outcomes::manualEjectRecoveryTierScale +
                outcome.ejectMultiplier * tuning::outcomes::manualEjectRecoveryBurnScale -
                launch.stats.escape * tuning::outcomes::manualEjectRecoveryEscapeRelief,
            tuning::outcomes::manualEjectRecoveryMinimum,
            tuning::outcomes::manualEjectRecoveryMaximum);
        const double ejectDamage =
            destination->hazard * tuning::outcomes::manualEjectDamageHazardScale +
            outcome.ejectMultiplier * tuning::outcomes::manualEjectDamageBurnScale +
            event.abortRisk * tuning::outcomes::manualEjectDamageAbortScale -
            launch.stats.escape * tuning::outcomes::manualEjectDamageEscapeRelief;
        outcome.shipDamage = std::clamp(
            static_cast<int>(std::round(ejectDamage)),
            tuning::outcomes::manualEjectDamageMinimum,
            tuning::outcomes::manualEjectDamageMaximum);
        const double injuryChance = std::clamp(
            tuning::outcomes::manualEjectCrewInjuryBase +
                event.abortRisk * tuning::outcomes::manualEjectCrewInjuryAbortScale +
                static_cast<double>(state.run.shipDamage) * tuning::outcomes::manualEjectCrewInjuryDamageScale -
                launch.stats.escape * tuning::outcomes::manualEjectCrewInjuryEscapeRelief,
            tuning::outcomes::manualEjectCrewInjuryMinimum,
            tuning::outcomes::manualEjectCrewInjuryMaximum);
        outcome.crewInjured = rng.chance(injuryChance);
        outcome.blueprintGain = outcome.ejectMultiplier >= destination->targetMultiplier * tuning::outcomes::manualEjectBlueprintTargetShare ? 1 : 0;
        if (isShallowRecovery(*destination, outcome.ejectMultiplier)) {
            outcome.recoveryCost += shallowRecoveryPenalty(state.run.shallowRecoveryStreak);
        }
        return outcome;
    }

    if (method == RecoveryMethod::TransferArrival) {
        outcome.type = LaunchResultType::MissionComplete;
        outcome.payout = destination->baseReward * outcome.ejectMultiplier * payoutMultiplier * tuning::rewards::transferArrivalPayoutFactor;
        const double arrivalDamage =
            destination->hazard * tuning::outcomes::transferArrivalDamageHazardScale +
            outcome.ejectMultiplier * tuning::outcomes::transferArrivalDamageBurnScale +
            event.stress * tuning::outcomes::transferArrivalDamageStressScale -
            launch.stats.hull * tuning::outcomes::transferArrivalDamageHullRelief -
            launch.stats.cooling * tuning::outcomes::transferArrivalDamageCoolingRelief;
        outcome.shipDamage = std::clamp(
            static_cast<int>(std::round(arrivalDamage)),
            tuning::outcomes::transferArrivalDamageMinimum,
            tuning::outcomes::transferArrivalDamageMaximum);
        outcome.blueprintGain = 1 + destination->tier / 2;
        return outcome;
    }

    outcome.type = reachedDestination ? LaunchResultType::MissionComplete : LaunchResultType::SafeEject;
    const double jackpotMultiplier = reachedDestination ? overburnJackpotMultiplier(*destination, outcome.ejectMultiplier) : 1.0;
    outcome.payout = destination->baseReward * outcome.ejectMultiplier * payoutMultiplier *
        (reachedDestination ? tuning::rewards::returnHomeReachedGoalFactor * jackpotMultiplier : tuning::rewards::returnHomeBasePayoutFactor);
    outcome.recoveryCost = std::clamp(
        tuning::outcomes::returnHomeRecoveryBase +
            static_cast<double>(destination->tier) * tuning::outcomes::returnHomeRecoveryTierScale +
            outcome.ejectMultiplier * tuning::outcomes::returnHomeRecoveryBurnScale,
        tuning::outcomes::returnHomeRecoveryMinimum,
        tuning::outcomes::returnHomeRecoveryMaximum);
    if (isCleanShallowRecovery(*destination, outcome)
        && state.run.cleanShallowRecoveryStreak + 1 >= tuning::rewards::cleanShallowRecoveryDestructionStreak) {
        markDestroyed(outcome, launch, catalog, *destination, state, rng);
        outcome.recoveryCost = 0.0;
        return outcome;
    }
    if (isShallowRecovery(*destination, outcome.ejectMultiplier)) {
        outcome.recoveryCost += shallowRecoveryPenalty(state.run.shallowRecoveryStreak);
    }
    const double netRewardFloor = isShallowRecovery(*destination, outcome.ejectMultiplier)
        ? 0.0
        : returnHomeNetRewardFloor(launch, *destination, outcome.ejectMultiplier);
    if (netRewardFloor > 0.0) {
        outcome.payout = std::max(outcome.payout, outcome.recoveryCost + netRewardFloor);
    }

    const double stressDamage =
        destination->hazard * tuning::outcomes::returnHomeDamageHazardScale +
        outcome.ejectMultiplier * tuning::outcomes::returnHomeDamageBurnScale +
        event.stress * tuning::outcomes::returnHomeDamageStressScale -
        launch.stats.hull * tuning::outcomes::returnHomeDamageHullRelief -
        launch.stats.cooling * tuning::outcomes::returnHomeDamageCoolingRelief;
    outcome.shipDamage = std::clamp(
        static_cast<int>(std::round(stressDamage)),
        tuning::outcomes::returnHomeDamageMinimum,
        reachedDestination ? tuning::outcomes::returnHomeDamageCompleteMaximum : tuning::outcomes::returnHomeDamageEarlyMaximum);
    outcome.blueprintGain = reachedDestination
        ? 1 + destination->tier / 2
        : (outcome.ejectMultiplier >= destination->targetMultiplier * tuning::outcomes::returnHomeBlueprintTargetShare ? 1 : 0);

    return outcome;
}

LaunchOutcome simulateLaunchToTarget(const GameState& state, const ContentCatalog& catalog, Random& rng)
{
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
    return resolveLaunch(launch, catalog, state, state.launchConfig.burnGoalMultiplier, RecoveryMethod::ReturnHome, rng);
}

TelemetryEvent telemetryAt(const PreparedLaunch& launch, double multiplier)
{
    TelemetryEvent event;
    event.multiplier = multiplier;

    const double progressToCrash = std::clamp(
        (multiplier - 1.0) / std::max(tuning::telemetry::progressDenominator, launch.crashMultiplier - 1.0),
        0.0,
        tuning::telemetry::progressMaximum);
    const double burnLoad = std::clamp(
        (multiplier - 1.0) / std::max(tuning::telemetry::burnLoadDenominator, launch.config.burnGoalMultiplier - 1.0),
        0.0,
        tuning::telemetry::burnLoadMaximum);
    const double lateFlight = std::clamp(
        (progressToCrash - tuning::telemetry::lateFlightStart) / tuning::telemetry::lateFlightRange,
        0.0,
        1.0);
    const double shipDamage = std::max(0.0, -std::min(0.0, launch.stats.hull)) * tuning::telemetry::damagedHullScale;
    const double volatility = std::max(0.0, launch.stats.volatility);
    const double thrustLoad = std::max(0.0, launch.stats.thrust - launch.stats.fuel * tuning::telemetry::thrustFuelOffset);
    const double coolingRelief = std::max(0.0, launch.stats.cooling) * tuning::telemetry::coolingReliefScale;
    const double hullRelief = std::max(0.0, launch.stats.hull) * tuning::telemetry::hullReliefScale;
    const double fuelRelief = std::max(0.0, launch.stats.fuel) * tuning::telemetry::fuelReliefScale;
    const double sensorRelief = std::max(0.0, launch.stats.sensors) * tuning::telemetry::sensorReliefScale;
    const double escapeRelief = std::max(0.0, launch.stats.escape) * tuning::telemetry::escapeReliefScale;
    const double pressurePulse = telemetryPulse(tuning::telemetry::pressurePulse, multiplier, launch.crashMultiplier);
    const double vibrationPulse = telemetryPulse(tuning::telemetry::vibrationPulse, multiplier, launch.crashMultiplier);
    const double mixPulse = telemetryPulse(tuning::telemetry::fuelMixPulse, multiplier, launch.crashMultiplier);
    const double guidancePulse = telemetryPulse(tuning::telemetry::guidancePulse, multiplier, launch.crashMultiplier);
    const double pressureLoad = 1.0 + launch.pressureModifier;
    const double earlyPressure =
        burnLoad *
            (tuning::telemetry::earlyPressureBase +
                thrustLoad * tuning::telemetry::earlyPressureThrustScale +
                volatility * tuning::telemetry::earlyPressureVolatilityScale) *
            pressurePulse -
        fuelRelief * tuning::telemetry::earlyPressureFuelRelief -
        coolingRelief * tuning::telemetry::earlyPressureCoolingRelief;
    const double earlyVibration =
        burnLoad *
            (tuning::telemetry::earlyVibrationBase +
                std::max(0.0, launch.stats.thrust) * tuning::telemetry::earlyVibrationThrustScale +
                volatility * tuning::telemetry::earlyVibrationVolatilityScale) *
            vibrationPulse +
        shipDamage * tuning::telemetry::earlyVibrationDamageScale -
        hullRelief * tuning::telemetry::earlyVibrationHullRelief -
        sensorRelief * tuning::telemetry::earlyVibrationSensorRelief;
    const double earlyFuelMix =
        burnLoad *
            (tuning::telemetry::earlyFuelMixBase +
                std::max(0.0, std::abs(launch.stats.thrust - launch.stats.fuel)) * tuning::telemetry::earlyFuelMixImbalanceScale +
                volatility * tuning::telemetry::earlyFuelMixVolatilityScale) *
            mixPulse -
        fuelRelief * tuning::telemetry::earlyFuelMixFuelRelief -
        sensorRelief * tuning::telemetry::earlyFuelMixSensorRelief;

    event.heat = std::clamp(progressToCrash * launch.heatRate - launch.cutHeatRelief, 0.0, tuning::telemetry::heatMaximum);
    event.pressure = std::clamp(
        (earlyPressure +
            lateFlight *
                (tuning::telemetry::pressureLateBase +
                    thrustLoad * tuning::telemetry::pressureLateThrustScale +
                    volatility * tuning::telemetry::pressureLateVolatilityScale) +
            event.heat * tuning::telemetry::pressureHeatScale -
            fuelRelief * tuning::telemetry::pressureFuelRelief -
            coolingRelief * tuning::telemetry::pressureCoolingRelief) *
                pressureLoad -
            launch.pressureRelief +
            launch.pressureReliefFailure,
        0.0,
        1.0);
    event.vibration = std::clamp(
        earlyVibration +
            lateFlight *
                (tuning::telemetry::vibrationLateBase +
                    volatility * tuning::telemetry::vibrationLateVolatilityScale +
                    std::max(0.0, launch.stats.thrust) * tuning::telemetry::vibrationLateThrustScale) +
            shipDamage * tuning::telemetry::vibrationDamageScale -
            hullRelief * tuning::telemetry::vibrationHullRelief -
            sensorRelief * tuning::telemetry::vibrationSensorRelief -
            launch.cutVibrationRelief +
            launch.cargoVibrationPenalty,
        0.0,
        1.0);
    event.fuelMix = std::clamp(
        earlyFuelMix +
            lateFlight *
                (tuning::telemetry::fuelMixLateBase +
                    std::max(0.0, launch.stats.thrust - launch.stats.fuel) * tuning::telemetry::fuelMixLateThrustOverFuelScale +
                    volatility * tuning::telemetry::fuelMixLateVolatilityScale) +
            event.pressure * tuning::telemetry::fuelMixPressureScale -
            fuelRelief * tuning::telemetry::fuelMixFuelRelief -
            launch.cargoFuelRelief,
        0.0,
        1.0);
    const double crewNavLoad = launch.crewGuidancePenalty * (tuning::telemetry::crewNavBase + burnLoad * tuning::telemetry::crewNavBurnScale);
    event.guidance = std::clamp(
        burnLoad *
            (tuning::telemetry::guidanceBase +
                event.vibration * tuning::telemetry::guidanceVibrationLoadScale +
                event.pressure * tuning::telemetry::guidancePressureLoadScale) *
            guidancePulse +
            lateFlight * (tuning::telemetry::guidanceLateBase + volatility * tuning::telemetry::guidanceLateVolatilityScale) +
            event.vibration * tuning::telemetry::guidanceVibrationScale +
            event.pressure * tuning::telemetry::guidancePressureScale -
            sensorRelief * tuning::telemetry::guidanceSensorRelief +
            launch.cutGuidancePenalty +
            launch.reliefGuidancePenalty +
            launch.cargoGuidancePenalty +
            crewNavLoad,
        0.0,
        1.0);

    const double readableLoad = burnLoad * (tuning::telemetry::readableLoadBase + volatility * tuning::telemetry::readableLoadVolatilityScale);
    event.pressure = std::max(event.pressure, std::clamp(
        (readableLoad * (tuning::telemetry::readablePressureBase + pressurePulse * tuning::telemetry::readablePressurePulseScale) -
            fuelRelief * tuning::telemetry::readablePressureFuelRelief -
            coolingRelief * tuning::telemetry::readablePressureCoolingRelief) *
                pressureLoad -
            launch.pressureRelief +
            launch.pressureReliefFailure,
        0.0,
        tuning::telemetry::readablePressureMaximum));
    event.vibration = std::max(event.vibration, std::clamp(
        readableLoad * (tuning::telemetry::readableVibrationBase + vibrationPulse * tuning::telemetry::readableVibrationPulseScale) -
            hullRelief * tuning::telemetry::readableVibrationHullRelief -
            sensorRelief * tuning::telemetry::readableVibrationSensorRelief +
            launch.cargoVibrationPenalty * tuning::telemetry::readableVibrationCargoScale,
        0.0,
        tuning::telemetry::readableVibrationMaximum));
    event.fuelMix = std::max(event.fuelMix, std::clamp(
        readableLoad * (tuning::telemetry::readableFuelMixBase + mixPulse * tuning::telemetry::readableFuelMixPulseScale) -
            fuelRelief * tuning::telemetry::readableFuelMixFuelRelief -
            sensorRelief * tuning::telemetry::readableFuelMixSensorRelief -
            launch.cargoFuelRelief * tuning::telemetry::readableFuelMixCargoRelief,
        0.0,
        tuning::telemetry::readableFuelMixMaximum));
    event.guidance = std::max(event.guidance, std::clamp(
        readableLoad * (tuning::telemetry::readableGuidanceBase + guidancePulse * tuning::telemetry::readableGuidancePulseScale) +
            event.vibration * tuning::telemetry::readableGuidanceVibrationScale -
            sensorRelief * tuning::telemetry::readableGuidanceSensorRelief +
            launch.cutGuidancePenalty * tuning::telemetry::readableGuidanceCutPenaltyScale +
            launch.reliefGuidancePenalty * tuning::telemetry::readableGuidanceReliefPenaltyScale +
            launch.cargoGuidancePenalty * tuning::telemetry::readableGuidanceCargoPenaltyScale +
            crewNavLoad * tuning::telemetry::readableGuidanceCrewNavScale,
        0.0,
        tuning::telemetry::readableGuidanceMaximum));

    double incidentAbortRisk = 0.0;
    for (int i = 0; i < launch.incidentCount; ++i) {
        const TelemetryIncident& incident = launch.incidents[static_cast<std::size_t>(i)];
        const double pulse = incidentPulse(incident, multiplier);
        event.heat = std::clamp(event.heat + incident.heat * pulse, 0.0, tuning::telemetry::heatMaximum);
        event.pressure = std::clamp(event.pressure + incident.pressure * pulse, 0.0, 1.0);
        event.vibration = std::clamp(event.vibration + incident.vibration * pulse, 0.0, 1.0);
        event.fuelMix = std::clamp(event.fuelMix + incident.fuelMix * pulse, 0.0, 1.0);
        event.guidance = std::clamp(event.guidance + incident.guidance * pulse, 0.0, 1.0);
        incidentAbortRisk = std::max(incidentAbortRisk, incident.abortRisk * pulse);
    }

    const double earlyThermal = std::clamp(
        (event.heat - tuning::telemetry::earlyThermalStart) / tuning::telemetry::earlyThermalRange,
        0.0,
        1.0) * tuning::telemetry::earlyThermalScale;
    const double warningStart = tuning::telemetry::warningStartBase - launch.sensorQuality * tuning::telemetry::warningStartSensorScale;
    const double certaintyWindow = std::clamp(
        (progressToCrash - warningStart) / std::max(tuning::telemetry::certaintyWindowMinimumRange, 1.0 - warningStart),
        0.0,
        1.0);
    const double earlyAbortLoad =
        burnLoad *
            (tuning::telemetry::earlyAbortBase +
                event.guidance * tuning::telemetry::earlyAbortGuidanceScale +
                event.vibration * tuning::telemetry::earlyAbortVibrationScale) +
        progressToCrash * tuning::telemetry::earlyAbortProgressScale -
        escapeRelief * tuning::telemetry::earlyAbortEscapeRelief;
    const double rawAbortRisk =
        earlyAbortLoad +
        certaintyWindow *
            (tuning::telemetry::certaintyAbortBase +
                event.guidance * tuning::telemetry::certaintyAbortGuidanceScale +
                event.vibration * tuning::telemetry::certaintyAbortVibrationScale) +
        event.heat * tuning::telemetry::abortHeatScale +
        incidentAbortRisk -
        escapeRelief * tuning::telemetry::abortEscapeRelief;
    event.abortRisk = std::clamp(rawAbortRisk * launch.crewAbortMultiplier, 0.0, 1.0);
    if (progressToCrash <= tuning::telemetry::instabilityStart) {
        event.instability = 0.0;
    } else if (progressToCrash <= tuning::telemetry::instabilityHighProximity) {
        event.instability = tuning::telemetry::instabilityHighWarning
            * (progressToCrash - tuning::telemetry::instabilityStart)
            / (tuning::telemetry::instabilityHighProximity - tuning::telemetry::instabilityStart);
    } else if (progressToCrash <= tuning::telemetry::instabilityCriticalProximity) {
        event.instability = tuning::telemetry::instabilityHighWarning
            + (tuning::telemetry::instabilityCriticalWarning - tuning::telemetry::instabilityHighWarning)
                * (progressToCrash - tuning::telemetry::instabilityHighProximity)
                / (tuning::telemetry::instabilityCriticalProximity - tuning::telemetry::instabilityHighProximity);
    } else {
        event.instability = tuning::telemetry::instabilityCriticalWarning
            + (1.0 - tuning::telemetry::instabilityCriticalWarning)
                * (progressToCrash - tuning::telemetry::instabilityCriticalProximity)
                / (1.0 - tuning::telemetry::instabilityCriticalProximity);
    }
    event.instability = std::clamp(event.instability, 0.0, 1.0);
    event.warning = std::clamp(std::max({earlyThermal, event.pressure, event.vibration, event.fuelMix, event.guidance, event.abortRisk, event.instability}), 0.0, 1.0);
    event.stress = std::clamp(
        event.heat * tuning::telemetry::stressHeatScale +
            event.pressure * tuning::telemetry::stressPressureScale +
            event.vibration * tuning::telemetry::stressVibrationScale +
            event.guidance * tuning::telemetry::stressGuidanceScale +
            event.abortRisk * tuning::telemetry::stressAbortScale +
            progressToCrash * tuning::telemetry::stressProgressScale,
        0.0,
        1.0);
    event.message = warningMessage(event);

    return event;
}

} // namespace rocket
