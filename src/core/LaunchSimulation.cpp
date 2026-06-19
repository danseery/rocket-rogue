#include "core/LaunchSimulation.h"

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

    double bonus = static_cast<double>(effectiveTrainingLevel(*astronaut)) * 0.055;
    const double traitMultiplier = 1.0 + std::max(0.0, aggregateCrewUpgradeStats(state, catalog).traitModifier);

    if (astronaut->trait == "Calm under heat") {
        bonus += 0.12 * traitMultiplier;
    } else if (astronaut->trait == "Reads telemetry early") {
        bonus += 0.06 * traitMultiplier;
    } else if (astronaut->trait == "Improves ejection odds") {
        bonus += 0.04 * traitMultiplier;
    }

    return bonus;
}

double crewEscapeBonus(const GameState& state, const ContentCatalog& catalog)
{
    const Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        return 0.0;
    }

    double bonus = static_cast<double>(astronaut->training) * 0.025;
    if (astronaut->trait == "Improves ejection odds") {
        bonus += 0.16 * (1.0 + std::max(0.0, aggregateCrewUpgradeStats(state, catalog).traitModifier));
    }
    return bonus;
}

std::vector<TelemetryEvent> buildTelemetry(const PreparedLaunch& launch)
{
    std::vector<TelemetryEvent> events;
    events.reserve(12);

    const double maxSample = std::max(launch.config.burnGoalMultiplier, launch.crashMultiplier);
    for (int i = 0; i < 12; ++i) {
        const double t = static_cast<double>(i) / 11.0;
        const double multiplier = 1.0 + (maxSample - 1.0) * t;
        events.push_back(telemetryAt(launch, multiplier));
    }

    return events;
}

TelemetryEvent peakTelemetryThrough(const PreparedLaunch& launch, double endMultiplier)
{
    TelemetryEvent peak;
    const double maxSample = std::max(1.0, endMultiplier);
    for (int i = 0; i < 18; ++i) {
        const double t = static_cast<double>(i) / 17.0;
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
    struct Channel {
        double value;
        const char* critical;
        const char* caution;
    };

    const Channel channels[] = {
        {event.heat, "TEMP: cooling runaway", "TEMP: heat margin narrowing"},
        {event.pressure, "PRESS: chamber overpressure", "PRESS: injector pressure unstable"},
        {event.vibration, "VIB: structural oscillation", "VIB: frame resonance building"},
        {event.guidance, "NAV: guidance divergence", "NAV: tracking solution drifting"},
        {event.fuelMix, "MIX: fuel ratio out of range", "MIX: combustion efficiency falling"},
        {event.abortRisk, "ABORT: escape window collapsing", "ABORT: capsule margin thinning"}
    };

    const Channel* worst = &channels[0];
    for (const auto& channel : channels) {
        if (channel.value > worst->value) {
            worst = &channel;
        }
    }

    if (worst->value > 0.88) {
        return worst->critical;
    }
    if (worst->value > 0.62) {
        return worst->caution;
    }

    return "Tracking system margins below limits";
}

double overburnJackpotMultiplier(const Destination& destination, double burnMultiplier)
{
    const double overGoal = std::max(0.0, burnMultiplier - destination.targetMultiplier);
    if (overGoal <= 0.0) {
        return 1.0;
    }

    const double normalizedOverburn = overGoal / std::max(0.20, destination.targetMultiplier - 1.0);
    return std::clamp(std::exp(normalizedOverburn * 2.65), 1.0, 8.0);
}

void markDestroyed(LaunchOutcome& outcome, const PreparedLaunch& launch, const ContentCatalog& catalog, const Destination& destination, const GameState& state, Random& rng)
{
    outcome.type = LaunchResultType::Destroyed;
    outcome.shipDamage = 100;
    outcome.blueprintGain = std::max(0, destination.tier / 2);

    const double baseSurvival = outcome.recoveryMethod == RecoveryMethod::ManualEject ? 0.48 : 0.22;
    const double survivalChance = std::clamp(baseSurvival + launch.stats.escape * 0.07 + crewEscapeBonus(state, catalog) - destination.hazard * 0.035, 0.05, 0.90);
    const bool survived = rng.chance(survivalChance);
    outcome.crewKilled = !survived;
    outcome.crewInjured = survived && rng.chance(outcome.recoveryMethod == RecoveryMethod::ManualEject ? 0.42 : 0.58);

    if (!state.run.equippedModuleIds.empty() && rng.chance(0.62)) {
        const int index = rng.rangeInt(0, static_cast<int>(state.run.equippedModuleIds.size()) - 1);
        outcome.moduleDestroyedId = state.run.equippedModuleIds[static_cast<std::size_t>(index)];
    }
}

double telemetryWave(double multiplier, double crashMultiplier, double frequency, double phase)
{
    return 0.5 + std::sin(multiplier * frequency + crashMultiplier * phase) * 0.5;
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
    return std::clamp(amount - std::max(0.0, mitigation), 0.03, 0.52);
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

    const double uncommonThreshold = dataGoal + (destination.targetMultiplier - dataGoal) * 0.45;
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
    launch.provingPayoutBonus = launch.config.frontierTransfer ? 0.0 : std::clamp(static_cast<double>(launch.overpreparedData) * 0.20, 0.0, 0.60);
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
    throttled.throttleFactor = 0.58;
    throttled.cutHeatRelief = 0.18;
    throttled.cutVibrationRelief = 0.14;
    throttled.cutGuidancePenalty = 0.22;
    return throttled;
}

PreparedLaunch withPressureRelief(const PreparedLaunch& launch, bool failed)
{
    PreparedLaunch relieved = launch;
    relieved.pressureRelief = failed ? 0.05 : 0.24;
    relieved.pressureReliefFailure = failed ? 0.16 : 0.0;
    relieved.reliefGuidancePenalty = failed ? 0.08 : 0.14;
    return relieved;
}

PreparedLaunch withJettisonedCargo(const PreparedLaunch& launch)
{
    PreparedLaunch lightened = launch;
    lightened.cargoFuelRelief = 0.22;
    lightened.cargoGuidancePenalty = 0.16;
    lightened.cargoVibrationPenalty = 0.12;
    lightened.cargoReturnPenalty = 0.075;
    return lightened;
}

double burnMultiplierDelta(const PreparedLaunch& launch, const Destination& destination, double elapsedSeconds, double deltaSeconds)
{
    constexpr double travelSpeedMultiplier = 1.10;
    const double dt = std::clamp(deltaSeconds, 0.0, 0.08);
    const double thrust = std::max(0.4, launch.stats.thrust);
    const double cruiseRate = 0.016 + thrust * 0.0017 + static_cast<double>(destination.tier) * 0.0008;
    const double acceleration = (0.00026 + destination.hazard * 0.00008) * launch.throttleFactor;
    const double startRate = cruiseRate * launch.throttleFactor + std::max(0.0, elapsedSeconds) * acceleration;
    return std::max(0.0, dt * startRate + 0.5 * dt * dt * acceleration) * travelSpeedMultiplier;
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
    const double profileDepth = std::clamp((burnMultiplier - 1.0) / std::max(0.10, destination->targetMultiplier - 1.0), 0.0, 1.8);
    const double systemsRelief =
        std::max(0.0, launch.stats.hull) * 0.018 +
        std::max(0.0, launch.stats.cooling) * 0.018 +
        std::max(0.0, launch.stats.fuel) * 0.012 +
        std::max(0.0, launch.stats.sensors) * 0.010;
    const double transferPenalty = launch.config.frontierTransfer ? 0.08 + static_cast<double>(destination->tier) * 0.015 : 0.0;

    const double risk =
        0.022 +
        destination->hazard * 0.022 +
        profileDepth * 0.060 +
        event.warning * 0.105 +
        event.heat * 0.050 +
        static_cast<double>(state.run.shipDamage) * 0.0014 +
        transferPenalty -
        systemsRelief +
        launch.cargoReturnPenalty;

    return std::clamp(risk, 0.01, 0.42);
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
        outcome.shipDamage = 100;
        return outcome;
    }

    if (method == RecoveryMethod::None || outcome.ejectMultiplier >= launch.crashMultiplier) {
        markDestroyed(outcome, launch, catalog, *destination, state, rng);
        return outcome;
    }

    const double payoutMultiplier = 1.0 + std::max(0.0, launch.stats.payout) * 0.045 + launch.provingPayoutBonus;
    const bool reachedDestination = outcome.ejectMultiplier >= destination->targetMultiplier;
    const TelemetryEvent event = telemetryAt(launch, outcome.ejectMultiplier);

    if (method == RecoveryMethod::ReturnHome && rng.chance(returnHomeRisk(launch, catalog, state, outcome.ejectMultiplier))) {
        markDestroyed(outcome, launch, catalog, *destination, state, rng);
        return outcome;
    }

    if (method == RecoveryMethod::ManualEject) {
        outcome.type = LaunchResultType::SafeEject;
        outcome.payout = destination->baseReward * outcome.ejectMultiplier * payoutMultiplier * 0.26;
        outcome.recoveryCost = std::clamp(14.0 + static_cast<double>(destination->tier) * 7.0 + outcome.ejectMultiplier * 3.0 - launch.stats.escape * 1.2, 8.0, 64.0);
        const double ejectDamage = destination->hazard * 3.8 + outcome.ejectMultiplier * 2.2 + event.abortRisk * 10.0 - launch.stats.escape * 0.75;
        outcome.shipDamage = std::clamp(static_cast<int>(std::round(ejectDamage)), 4, 34);
        const double injuryChance = std::clamp(0.14 + event.abortRisk * 0.32 + static_cast<double>(state.run.shipDamage) * 0.002 - launch.stats.escape * 0.030, 0.02, 0.48);
        outcome.crewInjured = rng.chance(injuryChance);
        outcome.blueprintGain = outcome.ejectMultiplier >= destination->targetMultiplier * 0.82 ? 1 : 0;
        return outcome;
    }

    if (method == RecoveryMethod::TransferArrival) {
        outcome.type = LaunchResultType::MissionComplete;
        outcome.payout = destination->baseReward * outcome.ejectMultiplier * payoutMultiplier * 1.45;
        const double arrivalDamage = destination->hazard * 5.8 + outcome.ejectMultiplier * 1.9 + event.stress * 8.0 - launch.stats.hull * 0.72 - launch.stats.cooling * 0.54;
        outcome.shipDamage = std::clamp(static_cast<int>(std::round(arrivalDamage)), 4, 32);
        outcome.blueprintGain = 1 + destination->tier / 2;
        return outcome;
    }

    outcome.type = reachedDestination ? LaunchResultType::MissionComplete : LaunchResultType::SafeEject;
    const double jackpotMultiplier = reachedDestination ? overburnJackpotMultiplier(*destination, outcome.ejectMultiplier) : 1.0;
    outcome.payout = destination->baseReward * outcome.ejectMultiplier * payoutMultiplier * (reachedDestination ? 1.18 * jackpotMultiplier : 0.74);
    outcome.recoveryCost = std::clamp(3.0 + static_cast<double>(destination->tier) * 2.0 + outcome.ejectMultiplier * 1.5, 2.0, 28.0);
    const double netRewardFloor = returnHomeNetRewardFloor(launch, *destination, outcome.ejectMultiplier);
    if (netRewardFloor > 0.0) {
        outcome.payout = std::max(outcome.payout, outcome.recoveryCost + netRewardFloor);
    }

    const double stressDamage = destination->hazard * 4.5 + outcome.ejectMultiplier * 1.7 + event.stress * 8.0 - launch.stats.hull * 0.70 - launch.stats.cooling * 0.58;
    outcome.shipDamage = std::clamp(static_cast<int>(std::round(stressDamage)), 0, reachedDestination ? 26 : 16);
    outcome.blueprintGain = reachedDestination ? 1 + destination->tier / 2 : (outcome.ejectMultiplier >= destination->targetMultiplier * 0.75 ? 1 : 0);

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

    const double progressToCrash = std::clamp((multiplier - 1.0) / std::max(0.01, launch.crashMultiplier - 1.0), 0.0, 1.4);
    const double burnLoad = std::clamp((multiplier - 1.0) / std::max(0.10, launch.config.burnGoalMultiplier - 1.0), 0.0, 1.45);
    const double lateFlight = std::clamp((progressToCrash - 0.45) / 0.65, 0.0, 1.0);
    const double shipDamage = std::max(0.0, -std::min(0.0, launch.stats.hull)) * 0.16;
    const double volatility = std::max(0.0, launch.stats.volatility);
    const double thrustLoad = std::max(0.0, launch.stats.thrust - launch.stats.fuel * 0.48);
    const double coolingRelief = std::max(0.0, launch.stats.cooling) * 0.030;
    const double hullRelief = std::max(0.0, launch.stats.hull) * 0.035;
    const double fuelRelief = std::max(0.0, launch.stats.fuel) * 0.032;
    const double sensorRelief = std::max(0.0, launch.stats.sensors) * 0.040;
    const double escapeRelief = std::max(0.0, launch.stats.escape) * 0.052;
    const double pressurePulse = 0.75 + telemetryWave(multiplier, launch.crashMultiplier, 10.7, 2.1) * 0.55;
    const double vibrationPulse = 0.70 + telemetryWave(multiplier, launch.crashMultiplier, 15.9, 4.4) * 0.65;
    const double mixPulse = 0.78 + telemetryWave(multiplier, launch.crashMultiplier, 8.3, 5.6) * 0.48;
    const double guidancePulse = 0.72 + telemetryWave(multiplier, launch.crashMultiplier, 6.6, 3.2) * 0.52;
    const double pressureLoad = 1.0 + launch.pressureModifier;
    const double earlyPressure = burnLoad * (0.090 + thrustLoad * 0.014 + volatility * 0.035) * pressurePulse - fuelRelief * 0.22 - coolingRelief * 0.16;
    const double earlyVibration = burnLoad * (0.080 + std::max(0.0, launch.stats.thrust) * 0.012 + volatility * 0.050) * vibrationPulse + shipDamage * 0.35 - hullRelief * 0.22 - sensorRelief * 0.14;
    const double earlyFuelMix = burnLoad * (0.060 + std::max(0.0, std::abs(launch.stats.thrust - launch.stats.fuel)) * 0.014 + volatility * 0.030) * mixPulse - fuelRelief * 0.18 - sensorRelief * 0.08;

    event.heat = std::clamp(progressToCrash * launch.heatRate - launch.cutHeatRelief, 0.0, 1.25);
    event.pressure = std::clamp((earlyPressure + lateFlight * (0.31 + thrustLoad * 0.052 + volatility * 0.075) + event.heat * 0.15 - fuelRelief * 0.18 - coolingRelief * 0.08) * pressureLoad - launch.pressureRelief + launch.pressureReliefFailure, 0.0, 1.0);
    event.vibration = std::clamp(earlyVibration + lateFlight * (0.24 + volatility * 0.15 + std::max(0.0, launch.stats.thrust) * 0.017) + shipDamage * 0.65 - hullRelief * 0.20 - sensorRelief * 0.08 - launch.cutVibrationRelief + launch.cargoVibrationPenalty, 0.0, 1.0);
    event.fuelMix = std::clamp(earlyFuelMix + lateFlight * (0.23 + std::max(0.0, launch.stats.thrust - launch.stats.fuel) * 0.052 + volatility * 0.042) + event.pressure * 0.15 - fuelRelief * 0.22 - launch.cargoFuelRelief, 0.0, 1.0);
    const double crewNavLoad = launch.crewGuidancePenalty * (0.35 + burnLoad * 0.65);
    event.guidance = std::clamp(burnLoad * (0.045 + event.vibration * 0.070 + event.pressure * 0.050) * guidancePulse + lateFlight * (0.28 + volatility * 0.055) + event.vibration * 0.20 + event.pressure * 0.10 - sensorRelief * 0.48 + launch.cutGuidancePenalty + launch.reliefGuidancePenalty + launch.cargoGuidancePenalty + crewNavLoad, 0.0, 1.0);

    const double readableLoad = burnLoad * (0.040 + volatility * 0.014);
    event.pressure = std::max(event.pressure, std::clamp((readableLoad * (0.75 + pressurePulse * 0.38) - fuelRelief * 0.04 - coolingRelief * 0.03) * pressureLoad - launch.pressureRelief + launch.pressureReliefFailure, 0.0, 0.32));
    event.vibration = std::max(event.vibration, std::clamp(readableLoad * (0.85 + vibrationPulse * 0.42) - hullRelief * 0.04 - sensorRelief * 0.03 + launch.cargoVibrationPenalty * 0.48, 0.0, 0.36));
    event.fuelMix = std::max(event.fuelMix, std::clamp(readableLoad * (0.70 + mixPulse * 0.34) - fuelRelief * 0.04 - sensorRelief * 0.02 - launch.cargoFuelRelief * 0.35, 0.0, 0.24));
    event.guidance = std::max(event.guidance, std::clamp(readableLoad * (0.62 + guidancePulse * 0.30) + event.vibration * 0.05 - sensorRelief * 0.04 + launch.cutGuidancePenalty * 0.48 + launch.reliefGuidancePenalty * 0.52 + launch.cargoGuidancePenalty * 0.58 + crewNavLoad * 0.74, 0.0, 0.58));

    double incidentAbortRisk = 0.0;
    for (int i = 0; i < launch.incidentCount; ++i) {
        const TelemetryIncident& incident = launch.incidents[static_cast<std::size_t>(i)];
        const double pulse = incidentPulse(incident, multiplier);
        event.heat = std::clamp(event.heat + incident.heat * pulse, 0.0, 1.25);
        event.pressure = std::clamp(event.pressure + incident.pressure * pulse, 0.0, 1.0);
        event.vibration = std::clamp(event.vibration + incident.vibration * pulse, 0.0, 1.0);
        event.fuelMix = std::clamp(event.fuelMix + incident.fuelMix * pulse, 0.0, 1.0);
        event.guidance = std::clamp(event.guidance + incident.guidance * pulse, 0.0, 1.0);
        incidentAbortRisk = std::max(incidentAbortRisk, incident.abortRisk * pulse);
    }

    const double earlyThermal = std::clamp((event.heat - 0.60) / 0.55, 0.0, 1.0) * 0.38;
    const double warningStart = 0.84 - launch.sensorQuality * 0.22;
    const double certaintyWindow = std::clamp((progressToCrash - warningStart) / std::max(0.05, 1.0 - warningStart), 0.0, 1.0);
    const double earlyAbortLoad = burnLoad * (0.040 + event.guidance * 0.080 + event.vibration * 0.050) + progressToCrash * 0.030 - escapeRelief * 0.10;
    const double rawAbortRisk = earlyAbortLoad + certaintyWindow * (0.54 + event.guidance * 0.25 + event.vibration * 0.22) + event.heat * 0.10 + incidentAbortRisk - escapeRelief * 0.76;
    event.abortRisk = std::clamp(rawAbortRisk * launch.crewAbortMultiplier, 0.0, 1.0);
    event.warning = std::clamp(std::max({earlyThermal, event.pressure, event.vibration, event.fuelMix, event.guidance, event.abortRisk}), 0.0, 1.0);
    event.stress = std::clamp(event.heat * 0.28 + event.pressure * 0.20 + event.vibration * 0.22 + event.guidance * 0.18 + event.abortRisk * 0.32 + progressToCrash * 0.08, 0.0, 1.0);
    event.message = warningMessage(event);

    return event;
}

} // namespace rocket
