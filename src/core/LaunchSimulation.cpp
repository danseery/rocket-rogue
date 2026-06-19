#include "core/LaunchSimulation.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace rocket {

namespace {

double crewTrainingBonus(const GameState& state)
{
    const Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        return 0.0;
    }

    double bonus = static_cast<double>(astronaut->training) * 0.055;
    bonus -= static_cast<double>(astronaut->stress) * 0.004;

    if (astronaut->trait == "Calm under heat") {
        bonus += 0.12;
    } else if (astronaut->trait == "Reads telemetry early") {
        bonus += 0.06;
    } else if (astronaut->trait == "Improves ejection odds") {
        bonus += 0.04;
    }

    return bonus;
}

double crewEscapeBonus(const GameState& state)
{
    const Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        return 0.0;
    }

    double bonus = static_cast<double>(astronaut->training) * 0.025;
    if (astronaut->trait == "Improves ejection odds") {
        bonus += 0.16;
    }
    return bonus;
}

std::vector<TelemetryEvent> buildTelemetry(const PreparedLaunch& launch)
{
    std::vector<TelemetryEvent> events;
    events.reserve(12);

    const double maxSample = std::max(launch.config.targetEjectMultiplier, launch.crashMultiplier);
    for (int i = 0; i < 12; ++i) {
        const double t = static_cast<double>(i) / 11.0;
        const double multiplier = 1.0 + (maxSample - 1.0) * t;
        events.push_back(telemetryAt(launch, multiplier));
    }

    return events;
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

void markDestroyed(LaunchOutcome& outcome, const PreparedLaunch& launch, const Destination& destination, const GameState& state, Random& rng)
{
    outcome.type = LaunchResultType::Destroyed;
    outcome.shipDamage = 100;
    outcome.blueprintGain = std::max(0, destination.tier / 2);

    const double baseSurvival = outcome.recoveryMethod == RecoveryMethod::ManualEject ? 0.48 : 0.22;
    const double survivalChance = std::clamp(baseSurvival + launch.stats.escape * 0.07 + crewEscapeBonus(state) - destination.hazard * 0.035, 0.05, 0.90);
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
        crewTrainingBonus(state));

    const double transferHazard = launch.config.frontierTransfer ? 1.08 + static_cast<double>(destination.tier) * 0.12 : 0.0;
    const double hazard = destination.hazard * 0.26 + transferHazard + std::max(0.0, launch.stats.volatility) * 0.11 + static_cast<double>(state.run.shipDamage) * 0.008;
    const double safety = std::clamp(1.0 + performance - hazard, 0.55, 1.75);
    const double tail = std::pow(rng.next01(), 1.0 / safety);

    const double minCrash = destination.minCrashMultiplier;
    const double transferCeilingPenalty = launch.config.frontierTransfer ? std::max(0.35, 1.08 + static_cast<double>(destination.tier) * 0.12 - performance * 0.25) : 0.0;
    const double maxCrash = std::max(minCrash + 0.18, destination.maxCrashMultiplier - transferCeilingPenalty + safety * 0.14);
    launch.crashMultiplier = std::clamp(minCrash + (maxCrash - minCrash) * tail, minCrash, maxCrash);
    launch.sensorQuality = std::clamp(0.22 + launch.stats.sensors * 0.065, 0.15, 0.92);
    launch.heatRate = std::clamp(destination.hazard * 0.62 + (launch.config.frontierTransfer ? 0.18 : 0.0) + std::max(0.0, launch.stats.volatility) * 0.18 - launch.stats.cooling * 0.040, 0.34, 2.45);

    return launch;
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
        0.055 +
        destination->hazard * 0.040 +
        profileDepth * 0.115 +
        event.warning * 0.180 +
        event.heat * 0.095 +
        static_cast<double>(state.run.shipDamage) * 0.0025 +
        transferPenalty -
        systemsRelief;

    return std::clamp(risk, 0.02, 0.76);
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

    const Destination* destination = catalog.findDestination(launch.config.destinationId);
    if (destination == nullptr) {
        outcome.type = LaunchResultType::Destroyed;
        outcome.shipDamage = 100;
        return outcome;
    }

    if (method == RecoveryMethod::None || outcome.ejectMultiplier >= launch.crashMultiplier) {
        markDestroyed(outcome, launch, *destination, state, rng);
        return outcome;
    }

    const double payoutMultiplier = 1.0 + std::max(0.0, launch.stats.payout) * 0.045;
    const bool reachedDestination = outcome.ejectMultiplier >= destination->targetMultiplier;
    const TelemetryEvent event = telemetryAt(launch, outcome.ejectMultiplier);

    if (method == RecoveryMethod::ReturnHome && rng.chance(returnHomeRisk(launch, catalog, state, outcome.ejectMultiplier))) {
        markDestroyed(outcome, launch, *destination, state, rng);
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
    outcome.payout = destination->baseReward * outcome.ejectMultiplier * payoutMultiplier * (reachedDestination ? 1.12 : 0.68);
    outcome.recoveryCost = std::clamp(3.0 + static_cast<double>(destination->tier) * 2.0 + outcome.ejectMultiplier * 1.5, 2.0, 28.0);

    const double stressDamage = destination->hazard * 4.5 + outcome.ejectMultiplier * 1.7 + event.stress * 8.0 - launch.stats.hull * 0.70 - launch.stats.cooling * 0.58;
    outcome.shipDamage = std::clamp(static_cast<int>(std::round(stressDamage)), 0, reachedDestination ? 26 : 16);
    outcome.blueprintGain = reachedDestination ? 1 + destination->tier / 2 : (outcome.ejectMultiplier >= destination->targetMultiplier * 0.75 ? 1 : 0);

    return outcome;
}

LaunchOutcome simulateLaunchToTarget(const GameState& state, const ContentCatalog& catalog, Random& rng)
{
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
    return resolveLaunch(launch, catalog, state, state.launchConfig.targetEjectMultiplier, RecoveryMethod::ReturnHome, rng);
}

TelemetryEvent telemetryAt(const PreparedLaunch& launch, double multiplier)
{
    TelemetryEvent event;
    event.multiplier = multiplier;

    const double progressToCrash = std::clamp((multiplier - 1.0) / std::max(0.01, launch.crashMultiplier - 1.0), 0.0, 1.4);
    const double burnLoad = std::clamp((multiplier - 1.0) / std::max(0.10, launch.config.targetEjectMultiplier - 1.0), 0.0, 1.45);
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
    const double earlyPressure = burnLoad * (0.090 + thrustLoad * 0.014 + volatility * 0.035) * pressurePulse - fuelRelief * 0.22 - coolingRelief * 0.16;
    const double earlyVibration = burnLoad * (0.080 + std::max(0.0, launch.stats.thrust) * 0.012 + volatility * 0.050) * vibrationPulse + shipDamage * 0.35 - hullRelief * 0.22 - sensorRelief * 0.14;
    const double earlyFuelMix = burnLoad * (0.060 + std::max(0.0, std::abs(launch.stats.thrust - launch.stats.fuel)) * 0.014 + volatility * 0.030) * mixPulse - fuelRelief * 0.18 - sensorRelief * 0.08;

    event.heat = std::clamp(progressToCrash * launch.heatRate, 0.0, 1.25);
    event.pressure = std::clamp(earlyPressure + lateFlight * (0.31 + thrustLoad * 0.052 + volatility * 0.075) + event.heat * 0.15 - fuelRelief * 0.18 - coolingRelief * 0.08, 0.0, 1.0);
    event.vibration = std::clamp(earlyVibration + lateFlight * (0.24 + volatility * 0.15 + std::max(0.0, launch.stats.thrust) * 0.017) + shipDamage * 0.65 - hullRelief * 0.20 - sensorRelief * 0.08, 0.0, 1.0);
    event.fuelMix = std::clamp(earlyFuelMix + lateFlight * (0.23 + std::max(0.0, launch.stats.thrust - launch.stats.fuel) * 0.052 + volatility * 0.042) + event.pressure * 0.15 - fuelRelief * 0.22, 0.0, 1.0);
    event.guidance = std::clamp(burnLoad * (0.045 + event.vibration * 0.070 + event.pressure * 0.050) * guidancePulse + lateFlight * (0.28 + volatility * 0.055) + event.vibration * 0.20 + event.pressure * 0.10 - sensorRelief * 0.48, 0.0, 1.0);

    const double readableLoad = burnLoad * (0.040 + volatility * 0.014);
    event.pressure = std::max(event.pressure, std::clamp(readableLoad * (0.75 + pressurePulse * 0.38) - fuelRelief * 0.04 - coolingRelief * 0.03, 0.0, 0.28));
    event.vibration = std::max(event.vibration, std::clamp(readableLoad * (0.85 + vibrationPulse * 0.42) - hullRelief * 0.04 - sensorRelief * 0.03, 0.0, 0.28));
    event.fuelMix = std::max(event.fuelMix, std::clamp(readableLoad * (0.70 + mixPulse * 0.34) - fuelRelief * 0.04 - sensorRelief * 0.02, 0.0, 0.24));
    event.guidance = std::max(event.guidance, std::clamp(readableLoad * (0.62 + guidancePulse * 0.30) + event.vibration * 0.05 - sensorRelief * 0.04, 0.0, 0.22));

    const double earlyThermal = std::clamp((event.heat - 0.60) / 0.55, 0.0, 1.0) * 0.38;
    const double warningStart = 0.84 - launch.sensorQuality * 0.22;
    const double certaintyWindow = std::clamp((progressToCrash - warningStart) / std::max(0.05, 1.0 - warningStart), 0.0, 1.0);
    const double earlyAbortLoad = burnLoad * (0.040 + event.guidance * 0.080 + event.vibration * 0.050) + progressToCrash * 0.030 - escapeRelief * 0.10;
    event.abortRisk = std::clamp(earlyAbortLoad + certaintyWindow * (0.54 + event.guidance * 0.25 + event.vibration * 0.22) + event.heat * 0.10 - escapeRelief * 0.76, 0.0, 1.0);
    event.warning = std::clamp(std::max({earlyThermal, event.pressure, event.vibration, event.fuelMix, event.guidance, event.abortRisk}), 0.0, 1.0);
    event.stress = std::clamp(event.heat * 0.28 + event.pressure * 0.20 + event.vibration * 0.22 + event.guidance * 0.18 + event.abortRisk * 0.32 + progressToCrash * 0.08, 0.0, 1.0);
    event.message = warningMessage(event);

    return event;
}

} // namespace rocket
