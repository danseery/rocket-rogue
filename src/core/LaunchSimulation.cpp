#include "core/LaunchSimulation.h"
#include "core/GameText.h"
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
    } else if (astronaut->trait == tuning::traits::readsTelemetryEarly) {
        bonus += tuning::traits::readsTelemetryEarlyBonus * traitMultiplier;
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

double smoothStep(double value)
{
    const double t = std::clamp(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
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
    return pulse * pulse * (3.0 - 2.0 * pulse);
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

    const Destination* configuredDestination = catalog.findDestination(launch.config.destinationId);
    const Destination& destination = configuredDestination == nullptr ? currentDestination(state, catalog) : *configuredDestination;
    launch.config.destinationId = destination.id;
    launch.config.frameId = state.run.frameId;
    launch.config.equippedModuleIds = state.run.equippedModuleIds;

    const double performance = std::max(0.0,
        launch.stats.thrust * 0.060 +
        launch.stats.fuel * 0.040 +
        launch.stats.hull * 0.055 +
        launch.stats.cooling * 0.050 +
        launch.stats.sensors * 0.035 +
        crewTrainingBonus(state, catalog));

    const double missionPressure = missionPressureModifier(state, catalog, destination);
    const int historyIndex = destinationHistoryIndex(catalog, destination.id);
    const int attempts = destinationHistoryValue(state.meta.destinationAttempts, historyIndex);
    const int successes = destinationHistoryValue(state.meta.destinationSuccesses, historyIndex);
    const int requiredReadiness = frontierReadinessRequired(state, catalog);
    const int currentReadiness = std::max(0, state.run.frontierReadiness);
    const double readinessRatio = requiredReadiness <= 0
        ? 1.0
        : std::clamp(static_cast<double>(currentReadiness) / static_cast<double>(requiredReadiness), 0.0, 1.0);
    launch.overpreparedData = requiredReadiness <= 0 ? 0 : std::max(0, currentReadiness - requiredReadiness);
    launch.provingPayoutBonus = launch.config.frontierTransfer
        ? 0.0
        : std::clamp(
              static_cast<double>(launch.overpreparedData) * tuning::rewards::provingPayoutPerExtraData,
              0.0,
              tuning::rewards::provingPayoutBonusMaximum);
    const double transferPrep = launch.config.frontierTransfer
        ? readinessRatio * 0.18 + static_cast<double>(std::min(launch.overpreparedData, 3)) * 0.050
        : 0.0;
    const double transferHazard = launch.config.frontierTransfer
        ? std::max(0.50, 1.00 + static_cast<double>(destination.tier) * 0.10 - transferPrep)
        : 0.0;
    const double unprovenHazard = successes == 0 ? std::max(0.0, 0.20 - static_cast<double>(attempts) * 0.035) : 0.0;
    const double hazard = destination.hazard * 0.26 + transferHazard + unprovenHazard + std::max(0.0, launch.stats.volatility) * 0.11 + static_cast<double>(state.run.shipDamage) * 0.008;
    const double safety = std::clamp(1.0 + performance - hazard, 0.55, 1.75);
    const bool longShotBreak = successes == 0 && rng.chance(std::clamp(0.012 + static_cast<double>(attempts) * 0.006 + launch.stats.sensors * 0.0015, 0.012, 0.055));
    const double tail = std::pow(rng.next01(), 1.0 / safety);

    const double minCrash = destination.minCrashMultiplier;
    const double transferCeilingPenalty = launch.config.frontierTransfer
        ? std::max(0.28, 0.90 + static_cast<double>(destination.tier) * 0.10 - performance * 0.32 - readinessRatio * 0.22 - static_cast<double>(std::min(launch.overpreparedData, 3)) * 0.080)
        : 0.0;
    const double unprovenPrepRelief = launch.config.frontierTransfer
        ? readinessRatio * 0.20 + static_cast<double>(std::min(launch.overpreparedData, 3)) * 0.075
        : 0.0;
    const double unprovenCeilingPenalty = successes == 0
        ? std::max(0.42, 0.86 - static_cast<double>(std::min(attempts, 8)) * 0.025 - unprovenPrepRelief)
        : 0.0;
    const double pressureCeilingPenalty = successes == 0 ? missionPressure * 0.38 : missionPressure * 0.12;
    const double longShotBonus = longShotBreak ? rng.range(0.24, 0.52) : 0.0;
    const double transferReadinessCeilingBonus = launch.config.frontierTransfer
        ? readinessRatio * 0.20 + static_cast<double>(std::min(launch.overpreparedData, 3)) * 0.12
        : 0.0;
    const double overpreparedCeilingBonus = launch.config.frontierTransfer
        ? 0.0
        : static_cast<double>(launch.overpreparedData) * 0.18;
    const double maxCrash = std::max(minCrash + 0.18, destination.maxCrashMultiplier - transferCeilingPenalty - unprovenCeilingPenalty - pressureCeilingPenalty + longShotBonus + transferReadinessCeilingBonus + overpreparedCeilingBonus + safety * 0.14);
    launch.crashMultiplier = std::clamp(minCrash + (maxCrash - minCrash) * tail, minCrash, maxCrash);
    launch.sensorQuality = std::clamp(0.22 + launch.stats.sensors * 0.065, 0.15, 0.92);
    const double transferHeatLoad = launch.config.frontierTransfer ? 0.04 + static_cast<double>(destination.tier) * 0.035 : 0.0;
    launch.heatRate = std::clamp(destination.hazard * 0.56 + transferHeatLoad + std::max(0.0, launch.stats.volatility) * 0.16 - launch.stats.cooling * 0.046, 0.34, 2.45);
    launch.pressureModifier = std::clamp(missionPressure - std::max(0.0, launch.stats.pressure) * 0.040, 0.03, 0.60);
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
    launch.incidentCount = std::clamp(2 + destination.tier / 2 + (launch.config.frontierTransfer ? 1 : 0), 2, static_cast<int>(launch.incidents.size()));

    const double profileEnd = std::max(launch.config.burnGoalMultiplier, destination.targetMultiplier);
    const double profileSpan = std::max(0.16, profileEnd - 1.0);
    const double incidentSeverity =
        0.12 +
        destination.hazard * 0.032 +
        missionPressure * 0.10 +
        std::max(0.0, launch.stats.volatility) * 0.035 +
        static_cast<double>(state.run.shipDamage) * 0.0012;

    for (int i = 0; i < launch.incidentCount; ++i) {
        TelemetryIncident incident;
        const double lane = (static_cast<double>(i) + rng.range(0.22, 0.82)) / static_cast<double>(launch.incidentCount + 1);
        incident.centerMultiplier = std::clamp(1.0 + profileSpan * lane, 1.04, std::max(1.05, launch.crashMultiplier - 0.035));
        incident.width = rng.range(0.045, 0.105) + destination.hazard * 0.006;

        const double amount = incidentSeverity * rng.range(0.78, 1.42);
        switch (rng.rangeInt(0, 5)) {
        case 0:
            incident.heat = dampened(amount + 0.08, launch.stats.cooling * 0.026);
            incident.pressure = dampened(amount * 0.34, launch.stats.pressure * 0.020);
            break;
        case 1:
            incident.pressure = dampened(amount + 0.07, launch.stats.pressure * 0.034 + launch.stats.fuel * 0.010);
            incident.fuelMix = dampened(amount * 0.42, launch.stats.fuel * 0.019);
            break;
        case 2:
            incident.vibration = dampened(amount + 0.06, launch.stats.hull * 0.030);
            incident.guidance = dampened(amount * 0.36, launch.stats.sensors * 0.024);
            break;
        case 3:
            incident.fuelMix = dampened(amount + 0.05, launch.stats.fuel * 0.034);
            incident.pressure = dampened(amount * 0.30, launch.stats.pressure * 0.024);
            break;
        case 4:
            incident.guidance = dampened(amount + 0.06, launch.stats.sensors * 0.036);
            incident.vibration = dampened(amount * 0.28, launch.stats.hull * 0.022);
            break;
        default:
            incident.abortRisk = dampened(amount + 0.05, launch.stats.escape * 0.040 + launch.stats.sensors * 0.018);
            incident.guidance = dampened(amount * 0.32, launch.stats.sensors * 0.026);
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
    return std::max(0.0, dt * startRate + 0.5 * dt * dt * acceleration) * tuning::launch::travelSpeedMultiplier;
}

double returnTelemetryMultiplier(double commitMultiplier, double crashMultiplier, double returnElapsed, double returnDuration)
{
    const double progress = std::clamp(returnElapsed / std::max(0.1, returnDuration), 0.0, 1.0);
    const double shaped = smoothStep(progress);
    const double headroom = std::max(0.04, crashMultiplier - commitMultiplier);
    const double overshoot = std::min(headroom * 0.22, 0.18 + headroom * 0.10);
    const double bump = std::sin(shaped * 3.1415926535) * overshoot;
    const double settle = shaped * std::min(headroom * 0.08, 0.06);
    return std::min(crashMultiplier - 0.02, commitMultiplier + bump - settle);
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
    const double netRewardFloor = returnHomeNetRewardFloor(launch, *destination, outcome.ejectMultiplier);
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
    event.warning = std::clamp(std::max({earlyThermal, event.pressure, event.vibration, event.fuelMix, event.guidance, event.abortRisk}), 0.0, 1.0);
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
