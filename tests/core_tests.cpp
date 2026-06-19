#include "core/Content.h"
#include "core/GameState.h"
#include "core/LaunchSimulation.h"
#include "core/SaveData.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace rocket;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::abort();
    }
}

GameState configuredState(const ContentCatalog& catalog, int destinationIndex, double targetMultiplier)
{
    GameState state = createNewGame(catalog, 12345);
    state.run.destinationIndex = destinationIndex;
    syncLaunchConfig(state, catalog);
    state.launchConfig.burnGoalMultiplier = targetMultiplier;
    return state;
}

void deterministicLaunchesMatch()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = configuredState(catalog, 2, catalog.destinations[2].targetMultiplier);

    Random rngA(991);
    Random rngB(991);
    const LaunchOutcome a = simulateLaunchToTarget(state, catalog, rngA);
    const LaunchOutcome b = simulateLaunchToTarget(state, catalog, rngB);

    require(a.type == b.type, "same seed should produce same outcome type");
    require(std::abs(a.crashMultiplier - b.crashMultiplier) < 0.000001, "same seed should produce same hidden crash");
    require(std::abs(a.payout - b.payout) < 0.000001, "same seed should produce same payout");
}

void safeAndDestroyedOutcomesResolve()
{
    const ContentCatalog catalog = createDefaultContent();

    const double safeBurn = catalog.destinations[0].minCrashMultiplier - 0.01;
    GameState safeState = configuredState(catalog, 0, safeBurn);
    Random safeRng(7);
    const PreparedLaunch safeLaunch = prepareLaunch(safeState, catalog, safeRng);
    const LaunchOutcome safe = resolveLaunch(safeLaunch, catalog, safeState, safeBurn, RecoveryMethod::ManualEject, safeRng);
    require(safe.type == LaunchResultType::SafeEject, "low target should safely eject before the minimum crash");
    require(safe.recoveryMethod == RecoveryMethod::ManualEject, "safe outcome should record manual eject");
    require(safe.payout > 0.0, "safe eject should pay fictional credits");
    require(safe.recoveryCost > 0.0, "manual eject should have rescue cost");

    GameState doomedState = configuredState(catalog, 0, 99.0);
    Random doomedRng(7);
    const LaunchOutcome doomed = simulateLaunchToTarget(doomedState, catalog, doomedRng);
    require(doomed.type == LaunchResultType::Destroyed, "extreme target should exceed hidden crash");
    require(doomed.shipDamage == 100, "destroyed launch should total the ship");
}

void moduleAggregationIncludesFrameAndDamage()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 1);
    const ModuleStats clean = aggregateShipStats(state, catalog);
    state.run.shipDamage = 50;
    const ModuleStats damaged = aggregateShipStats(state, catalog);

    require(clean.thrust > 0.0, "starter ship should have thrust");
    require(clean.escape > 0.0, "starter ship should have escape capability");
    require(damaged.hull < clean.hull, "damage should reduce effective hull");
    require(damaged.cooling < clean.cooling, "damage should reduce effective cooling");
}

void earlyTelemetryShowsSystemLoad()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = configuredState(catalog, 0, catalog.destinations[0].targetMultiplier);
    Random rng(42);
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
    const double burn = 1.0 + (catalog.destinations[0].targetMultiplier - 1.0) * 0.78;
    const TelemetryEvent event = telemetryAt(launch, burn);

    int activeChannels = 0;
    activeChannels += event.heat > 0.015 ? 1 : 0;
    activeChannels += event.pressure > 0.015 ? 1 : 0;
    activeChannels += event.vibration > 0.015 ? 1 : 0;
    activeChannels += event.fuelMix > 0.015 ? 1 : 0;
    activeChannels += event.guidance > 0.015 ? 1 : 0;
    activeChannels += event.abortRisk > 0.015 ? 1 : 0;

    require(activeChannels >= 4, "early telemetry should show multiple non-zero system channels near the data goal");
    require(event.warning > 0.02, "early telemetry warning should not be completely flat near the data goal");
}

double telemetryLoad(const TelemetryEvent& event)
{
    return event.heat + event.pressure + event.vibration + event.fuelMix + event.guidance + event.abortRisk;
}

double legacyBurnMultiplierDelta(const PreparedLaunch& launch, const Destination& destination, double elapsedSeconds, double deltaSeconds)
{
    const double dt = std::clamp(deltaSeconds, 0.0, 0.08);
    const double thrust = std::max(0.4, launch.stats.thrust);
    const double cruiseRate = 0.016 + thrust * 0.0017 + static_cast<double>(destination.tier) * 0.0008;
    const double acceleration = (0.00026 + destination.hazard * 0.00008) * launch.throttleFactor;
    const double startRate = cruiseRate * launch.throttleFactor + std::max(0.0, elapsedSeconds) * acceleration;
    return std::max(0.0, dt * startRate + 0.5 * dt * dt * acceleration);
}

bool firstRecoveredReturnAtBurn(const GameState& state, const ContentCatalog& catalog, double burnMultiplier, std::uint64_t seed, LaunchOutcome& recovered)
{
    for (int i = 0; i < 5000; ++i) {
        Random rng(seed + static_cast<std::uint64_t>(i));
        const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
        if (burnMultiplier >= launch.crashMultiplier) {
            continue;
        }

        const LaunchOutcome outcome = resolveLaunch(launch, catalog, state, burnMultiplier, RecoveryMethod::ReturnHome, rng);
        if (outcome.type != LaunchResultType::Destroyed) {
            recovered = outcome;
            return true;
        }
    }

    return false;
}

std::string offerKeyAt(const GameState& state, std::size_t index)
{
    if (!state.run.offerModuleIds[index].empty()) {
        return "module:" + state.run.offerModuleIds[index];
    }
    if (!state.run.offerCrewUpgradeIds[index].empty()) {
        return "crew:" + state.run.offerCrewUpgradeIds[index];
    }
    return "";
}

void launchIncidentsAreChunkyAndRecoverable()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = configuredState(catalog, 0, catalog.destinations[0].targetMultiplier);
    Random rng(4242);
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);

    require(launch.incidentCount >= 2, "launches should seed multiple transient telemetry incidents");

    bool foundRecoverableSpike = false;
    for (int i = 0; i < launch.incidentCount; ++i) {
        const TelemetryIncident& incident = launch.incidents[static_cast<std::size_t>(i)];
        int activeChannels = 0;
        activeChannels += incident.heat > 0.01 ? 1 : 0;
        activeChannels += incident.pressure > 0.01 ? 1 : 0;
        activeChannels += incident.vibration > 0.01 ? 1 : 0;
        activeChannels += incident.fuelMix > 0.01 ? 1 : 0;
        activeChannels += incident.guidance > 0.01 ? 1 : 0;
        activeChannels += incident.abortRisk > 0.01 ? 1 : 0;
        require(activeChannels >= 1 && activeChannels <= 2, "incidents should affect one or two systems, not every dial");

        const double before = std::max(1.0, incident.centerMultiplier - incident.width * 1.25);
        const double after = incident.centerMultiplier + incident.width * 1.25;
        const double centerLoad = telemetryLoad(telemetryAt(launch, incident.centerMultiplier));
        const double sideLoad = std::min(telemetryLoad(telemetryAt(launch, before)), telemetryLoad(telemetryAt(launch, after)));
        if (centerLoad > sideLoad + 0.035) {
            foundRecoverableSpike = true;
        }
    }

    require(foundRecoverableSpike, "at least one incident should produce a visible spike that can settle back down");
}

void crewStressUsesDiscreteStepsForPilotRisk()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState calm = configuredState(catalog, 0, catalog.destinations[0].targetMultiplier);
    Astronaut* calmPilot = activeAstronaut(calm);
    require(calmPilot != nullptr, "stress test needs a pilot");
    calmPilot->training = 3;
    calmPilot->stress = 0;

    GameState tense = calm;
    Astronaut* tensePilot = activeAstronaut(tense);
    require(tensePilot != nullptr, "stress test needs a tense pilot");
    tensePilot->stress = 100;

    require(crewStressStepCount(13) == 0, "stress below 14 should not create a stress step");
    require(crewStressStepCount(14) == 1, "each 14 stress should create one stress step");
    require(crewStressStepCount(100) == 7, "max stress should create seven stress steps");
    require(effectiveTrainingLevel(*tensePilot) == -4, "stress steps should cancel effective training in discrete chunks");
    require(crewAbortRiskMultiplierFromStress(100) >= 1.99, "100 stress should effectively double ABORT growth");

    Random calmRng(616);
    Random tenseRng(616);
    const PreparedLaunch calmLaunch = prepareLaunch(calm, catalog, calmRng);
    const PreparedLaunch tenseLaunch = prepareLaunch(tense, catalog, tenseRng);
    require(calmLaunch.crewStressSteps == 0, "calm launch should have no crew stress steps");
    require(tenseLaunch.crewStressSteps == 7, "tense launch should carry max crew stress steps");
    require(tenseLaunch.crewGuidancePenalty > calmLaunch.crewGuidancePenalty, "crew stress should add NAV penalty during prep");
    require(tenseLaunch.crewAbortMultiplier > calmLaunch.crewAbortMultiplier, "crew stress should add ABORT multiplier during prep");

    PreparedLaunch isolatedStress = calmLaunch;
    isolatedStress.crewStressSteps = tenseLaunch.crewStressSteps;
    isolatedStress.crewGuidancePenalty = tenseLaunch.crewGuidancePenalty;
    isolatedStress.crewAbortMultiplier = tenseLaunch.crewAbortMultiplier;

    const double burn = 1.0 + (catalog.destinations[0].targetMultiplier - 1.0) * 0.92;
    const TelemetryEvent calmTelemetry = telemetryAt(calmLaunch, burn);
    const TelemetryEvent tenseTelemetry = telemetryAt(isolatedStress, burn);
    require(tenseTelemetry.guidance > calmTelemetry.guidance + 0.08, "crew stress should visibly increase NAV drift");
    require(tenseTelemetry.abortRisk > calmTelemetry.abortRisk + 0.04, "crew stress should visibly increase ABORT pressure");
}

void cutEnginesTradeHeatForNavigation()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = configuredState(catalog, 0, catalog.destinations[0].targetMultiplier);
    Random rng(44);
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
    const PreparedLaunch throttled = withCutEngines(launch);
    const double burn = 1.0 + (catalog.destinations[0].targetMultiplier - 1.0) * 0.80;

    const TelemetryEvent powered = telemetryAt(launch, burn);
    const TelemetryEvent cut = telemetryAt(throttled, burn);
    require(cut.heat < powered.heat, "cut engines should lower heat");
    require(cut.vibration < powered.vibration, "cut engines should lower vibration");
    require(cut.guidance > powered.guidance, "cut engines should increase navigation drift");

    const double poweredDelta = burnMultiplierDelta(launch, catalog.destinations[0], 4.0, 1.0 / 60.0);
    const double cutDelta = burnMultiplierDelta(throttled, catalog.destinations[0], 4.0, 1.0 / 60.0);
    require(cutDelta < poweredDelta, "cut engines should slow burn progression");
}

void shipTravelIsFasterWithoutRetuningTelemetry()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = configuredState(catalog, 0, catalog.destinations[0].targetMultiplier);
    Random rng(444);
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
    const Destination& destination = catalog.destinations[0];
    const double elapsed = 5.25;
    const double delta = 1.0 / 60.0;

    const double boostedDelta = burnMultiplierDelta(launch, destination, elapsed, delta);
    const double oldDelta = legacyBurnMultiplierDelta(launch, destination, elapsed, delta);
    require(std::abs(boostedDelta - oldDelta * 1.10) < 0.000001, "ship travel should be ten percent faster than the previous burn step");

    const double burn = 1.0 + (destination.targetMultiplier - 1.0) * 0.88;
    const TelemetryEvent telemetry = telemetryAt(launch, burn);
    require(telemetry.multiplier == burn, "telemetry should still be sampled by burn depth, not wall-clock travel speed");
}

void emergencyActionsTradeOneRiskForAnother()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = configuredState(catalog, 0, catalog.destinations[0].targetMultiplier);
    Random rng(66);
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
    const double burn = 1.0 + (catalog.destinations[0].targetMultiplier - 1.0) * 0.82;

    const TelemetryEvent baseline = telemetryAt(launch, burn);
    const PreparedLaunch relieved = withPressureRelief(launch, false);
    const TelemetryEvent relief = telemetryAt(relieved, burn);
    require(relief.pressure < baseline.pressure, "pressure relief valve should lower physical pressure");
    require(relief.guidance > baseline.guidance, "pressure relief valve should worsen navigation");

    const PreparedLaunch failedRelief = withPressureRelief(launch, true);
    const TelemetryEvent failed = telemetryAt(failedRelief, burn);
    require(failed.pressure > relief.pressure, "failed relief valve should leave more pressure than a clean vent");
    require(failed.guidance > baseline.guidance, "failed relief valve should still degrade navigation");

    const PreparedLaunch jettisoned = withJettisonedCargo(launch);
    const TelemetryEvent cargo = telemetryAt(jettisoned, burn);
    require(cargo.fuelMix < baseline.fuelMix, "cargo jettison should stabilize fuel mix");
    require(cargo.guidance > baseline.guidance, "cargo jettison should worsen navigation");
    require(cargo.vibration > baseline.vibration, "cargo jettison should increase vibration");
    require(returnHomeRisk(jettisoned, catalog, state, burn) > returnHomeRisk(launch, catalog, state, burn), "cargo jettison should make return home riskier");
}

void emergencyRecruitmentPreventsDeadRosterSoftLock()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 101);
    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = CrewStatus::Dead;
    }
    state.run.credits = 5.0;
    syncLaunchConfig(state, catalog);

    require(activeAstronaut(state) == nullptr, "test setup should have no living astronaut");
    require(recruitCrew(state, catalog), "emergency recruitment should work even with low credits");
    require(activeAstronaut(state) != nullptr, "recruitment should restore a launchable astronaut");
    require(state.run.credits == 5.0, "emergency recruitment should be free when the roster is dead");
    require(!state.launchConfig.astronautId.empty(), "recruitment should select the new astronaut");
}

void moduleOffersAreOneChoiceRefits()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 202);
    state.run.credits = 200.0;

    Random rng(909);
    generateModuleOffers(state, catalog, rng);

    require(!offerKeyAt(state, 0).empty(), "reward screen should receive offer one");
    require(!offerKeyAt(state, 1).empty(), "reward screen should receive offer two");
    require(!offerKeyAt(state, 2).empty(), "reward screen should receive offer three");
    require(offerKeyAt(state, 0) != offerKeyAt(state, 1), "offer one and two should be distinct when possible");
    require(offerKeyAt(state, 0) != offerKeyAt(state, 2), "offer one and three should be distinct when possible");
    require(offerKeyAt(state, 1) != offerKeyAt(state, 2), "offer two and three should be distinct when possible");

    const std::string picked = offerKeyAt(state, 1);
    require(buyOffer(state, catalog, 1), "player should be able to install one affordable reward module");
    require(state.run.offerModuleIds[0].empty() && state.run.offerModuleIds[1].empty() && state.run.offerModuleIds[2].empty(), "buying one reward should consume the refit window");
    require(state.run.offerCrewUpgradeIds[0].empty() && state.run.offerCrewUpgradeIds[1].empty() && state.run.offerCrewUpgradeIds[2].empty(), "buying one reward should consume crew refit offers");
    if (picked.find("module:") == 0) {
        const std::string moduleId = picked.substr(7);
        require(std::find(state.run.inventoryModuleIds.begin(), state.run.inventoryModuleIds.end(), moduleId) != state.run.inventoryModuleIds.end(), "installed module should enter inventory");
    } else {
        const std::string upgradeId = picked.substr(5);
        require(std::find(state.run.crewUpgradeIds.begin(), state.run.crewUpgradeIds.end(), upgradeId) != state.run.crewUpgradeIds.end(), "installed crew upgrade should enter facilities");
    }
}

void refitRerollsSpendAndEscalate()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 303);
    state.run.credits = 200.0;

    Random rng(3030);
    generateModuleOffers(state, catalog, rng);

    require(offerRerollCost(state) == 10.0, "first refit reroll should cost 10 credits");
    require(rerollOffers(state, catalog, rng), "affordable refit reroll should succeed");
    require(state.run.credits == 190.0, "first refit reroll should spend 10 credits");
    require(state.run.offerRerollsThisExpedition == 1, "first refit reroll should increment run reroll count");
    require(offerRerollCost(state) == 20.0, "second refit reroll should cost 20 credits");

    require(rerollOffers(state, catalog, rng), "second affordable refit reroll should succeed");
    require(state.run.credits == 170.0, "second refit reroll should spend 20 credits");
    require(state.run.offerRerollsThisExpedition == 2, "second refit reroll should increment run reroll count");
    require(offerRerollCost(state) == 30.0, "third refit reroll should cost 30 credits");

    state.run.credits = 29.0;
    require(!rerollOffers(state, catalog, rng), "reroll should be blocked when credits are short");
    require(state.run.offerRerollsThisExpedition == 2, "failed reroll should not increment run reroll count");

    startNewExpedition(state, catalog);
    require(state.run.offerRerollsThisExpedition == 0, "new expedition should reset reroll escalation");
}

void crewUpgradeOffersInstallAndModifyCrewOps()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 606);
    state.run.credits = 200.0;
    state.meta.unlockKeys = {"starter", "thermal", "recovery", "deep_space", "ai"};
    state.run.inventoryModuleIds.clear();
    for (const ShipModule& module : catalog.modules) {
        state.run.inventoryModuleIds.push_back(module.id);
    }

    Random rng(6060);
    generateModuleOffers(state, catalog, rng);

    int crewOfferIndex = -1;
    for (std::size_t i = 0; i < state.run.offerCrewUpgradeIds.size(); ++i) {
        if (!state.run.offerCrewUpgradeIds[i].empty()) {
            crewOfferIndex = static_cast<int>(i);
            break;
        }
    }
    require(crewOfferIndex >= 0, "refit offers should include crew upgrades when ship module pool is owned");
    const std::string upgradeId = state.run.offerCrewUpgradeIds[static_cast<std::size_t>(crewOfferIndex)];
    require(buyOffer(state, catalog, crewOfferIndex), "buying a crew upgrade refit should succeed");
    require(std::find(state.run.crewUpgradeIds.begin(), state.run.crewUpgradeIds.end(), upgradeId) != state.run.crewUpgradeIds.end(), "crew upgrade should be installed");

    state.run.crewUpgradeIds = {"analog_sim_bay", "high_g_simulator", "medical_recovery_ward", "mission_psych_office", "trait_coaching_lab"};
    CrewUpgradeStats stats = aggregateCrewUpgradeStats(state, catalog);
    require(stats.trainingGain == 1, "crew simulator upgrades should aggregate training gain");
    require(stats.trainingStressRelief == 5, "crew simulator upgrades should aggregate stress relief");
    require(stats.restStressBonus == 12, "medical upgrades should aggregate rest bonus");
    require(stats.launchStressRelief == 5, "psych upgrades should aggregate launch stress relief");
    require(stats.traitModifier > 0.34, "trait upgrades should aggregate trait modifiers");

    Astronaut* pilot = activeAstronaut(state);
    require(pilot != nullptr, "crew upgrade test needs a pilot");
    pilot->training = 1;
    pilot->stress = 20;
    const double creditsBeforeTraining = state.run.credits;
    require(trainCrew(state, catalog), "crew training should use facility upgrades");
    require(pilot->training == 3, "upgraded simulator should grant extra training");
    require(pilot->stress == 21, "upgraded simulator should reduce training stress gain");
    require(state.run.credits < creditsBeforeTraining, "training should still cost credits");
    pilot->stress = 100;
    require(!trainCrew(state, catalog), "crew training should be blocked at max stress");
    pilot->stress = 100 - crewTrainingStressGain(state, catalog) + 1;
    require(!trainCrew(state, catalog), "crew training should be blocked if it would overflow stress");

    pilot->stress = 60;
    pilot->status = CrewStatus::Injured;
    require(crewRestStressRecovery(state, catalog) == 18, "unproven frontier difficulty should reduce medical rest recovery");
    const double restCostAt60 = crewRestCost(state, catalog);
    pilot->stress = 90;
    require(crewRestCost(state, catalog) > restCostAt60, "medical rest should cost more for more-stressed crews");
    pilot->stress = 60;
    require(restCrew(state, catalog), "medical rest should use facility upgrades");
    require(pilot->status == CrewStatus::Active, "medical rest should clear injury");
    require(pilot->stress == 33, "medical rest should not fully reset stress on an unproven frontier");

    pilot->stress = 10;
    LaunchOutcome outcome;
    outcome.type = LaunchResultType::SafeEject;
    outcome.recoveryMethod = RecoveryMethod::ReturnHome;
    outcome.destinationId = catalog.destinations[0].id;
    outcome.ejectMultiplier = 1.2;
    applyLaunchOutcome(state, catalog, outcome);
    require(pilot->stress == 17, "psych facility should reduce post-launch stress gain");

    GameState baseline = createNewGame(catalog, 707);
    GameState upgraded = baseline;
    upgraded.run.crewUpgradeIds = {"mission_psych_office", "trait_coaching_lab"};
    Random baselineRng(7070);
    Random upgradedRng(7070);
    const PreparedLaunch baselineLaunch = prepareLaunch(baseline, catalog, baselineRng);
    const PreparedLaunch upgradedLaunch = prepareLaunch(upgraded, catalog, upgradedRng);
    require(upgradedLaunch.crashMultiplier > baselineLaunch.crashMultiplier, "trait facility upgrades should improve trait-driven launch performance");
}

void hangarOpsStartCheapAndEscalate()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 808);
    state.run.credits = 300.0;
    state.run.shipDamage = 80;

    const double firstRepairCost = repairShipCost(state);
    require(firstRepairCost < 30.0, "first repair assignment should be affordable after a rough run");
    require(repairShipAmount(state) == 35, "repair bay should still cap each assignment");
    require(repairShip(state), "first repair should succeed");
    const double secondRepairCost = repairShipCost(state);
    require(secondRepairCost > firstRepairCost, "second repair assignment should cost more this expedition");
    require(repairShip(state), "second repair should still be possible with enough credits");
    require(repairShipCost(state) > secondRepairCost, "third repair assignment should continue escalating");

    Astronaut* pilot = activeAstronaut(state);
    require(pilot != nullptr, "hangar op test needs a pilot");
    pilot->stress = 20;
    const double firstTrainingCost = crewTrainingCost(state, catalog);
    require(firstTrainingCost < 18.0, "first simulator burn should be cheaper than the old flat cost");
    require(trainCrew(state, catalog), "first simulator burn should succeed");
    require(crewTrainingCost(state, catalog) > firstTrainingCost, "training should escalate after use");

    pilot->stress = 60;
    const double firstRestCost = crewRestCost(state, catalog);
    require(firstRestCost < 20.0, "first medical rest should be cheaper for stressed crews");
    require(restCrew(state, catalog), "first medical rest should succeed");
    require(crewRestCost(state, catalog) > firstRestCost, "medical rest should escalate after use");

    startNewExpedition(state, catalog);
    require(state.run.repairOpsThisExpedition == 0, "new expedition should reset repair escalation");
    require(state.run.trainingOpsThisExpedition == 0, "new expedition should reset training escalation");
    require(state.run.restOpsThisExpedition == 0, "new expedition should reset rest escalation");
}

void lowCreditRefitWindowIncludesAffordableOffer()
{
    const ContentCatalog catalog = createDefaultContent();

    for (int i = 0; i < 40; ++i) {
        GameState state = createNewGame(catalog, 220 + static_cast<std::uint64_t>(i));
        state.run.credits = 35.0;
        state.meta.unlockKeys = {"starter", "thermal", "recovery"};

        Random rng(77000 + static_cast<std::uint64_t>(i));
        generateModuleOffers(state, catalog, rng);

        bool affordable = false;
        for (std::size_t offerIndex = 0; offerIndex < state.run.offerModuleIds.size(); ++offerIndex) {
            if (const ShipModule* module = catalog.findModule(state.run.offerModuleIds[offerIndex])) {
                affordable = affordable || state.run.credits >= static_cast<double>(moduleOfferCost(*module));
            }
            if (const CrewUpgrade* upgrade = catalog.findCrewUpgrade(state.run.offerCrewUpgradeIds[offerIndex])) {
                affordable = affordable || state.run.credits >= static_cast<double>(crewUpgradeCost(*upgrade));
            }
        }
        require(affordable, "a clean starter return should see at least one affordable early refit");
    }
}

void pressureTracksFrontierExperience()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 303);
    const Destination& earthOrbit = catalog.destinations[0];

    require(std::abs(missionPressureModifier(state, catalog, earthOrbit) - 0.50) < 0.000001, "unattempted frontier should start at +50 pressure");

    LaunchOutcome failed;
    failed.type = LaunchResultType::SafeEject;
    failed.recoveryMethod = RecoveryMethod::ManualEject;
    failed.destinationId = earthOrbit.id;
    failed.ejectMultiplier = 1.10;
    applyLaunchOutcome(state, catalog, failed);
    require(std::abs(missionPressureModifier(state, catalog, earthOrbit) - 0.25) < 0.000001, "attempted but unsucceeded frontier should drop to +25 pressure");

    applyLaunchOutcome(state, catalog, failed);
    require(missionPressureModifier(state, catalog, earthOrbit) < 0.25, "repeated failed attempts should reduce pressure");
    require(missionPressureModifier(state, catalog, earthOrbit) > 0.0, "failed-attempt pressure should never reach zero");

    LaunchOutcome succeeded;
    succeeded.type = LaunchResultType::MissionComplete;
    succeeded.recoveryMethod = RecoveryMethod::ReturnHome;
    succeeded.destinationId = earthOrbit.id;
    succeeded.ejectMultiplier = earthOrbit.targetMultiplier;
    applyLaunchOutcome(state, catalog, succeeded);
    require(missionPressureModifier(state, catalog, earthOrbit) < 0.25, "successful proving runs should reduce pressure below failed-attempt pressure");
    require(missionPressureModifier(state, catalog, earthOrbit) >= 0.05, "successful proving pressure should keep a nonzero floor");
}

void crewStressTracksPeakTelemetryDanger()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState calm = createNewGame(catalog, 909);
    GameState tense = createNewGame(catalog, 909);

    Astronaut* calmPilot = activeAstronaut(calm);
    Astronaut* tensePilot = activeAstronaut(tense);
    require(calmPilot != nullptr && tensePilot != nullptr, "peak danger stress test needs pilots");

    calmPilot->stress = 0;
    tensePilot->stress = 0;

    LaunchOutcome calmOutcome;
    calmOutcome.type = LaunchResultType::SafeEject;
    calmOutcome.recoveryMethod = RecoveryMethod::ReturnHome;
    calmOutcome.destinationId = catalog.destinations[0].id;
    calmOutcome.ejectMultiplier = 1.20;
    calmOutcome.peakWarning = 0.20;
    calmOutcome.peakAbortRisk = 0.05;

    LaunchOutcome tenseOutcome = calmOutcome;
    tenseOutcome.peakWarning = 0.92;
    tenseOutcome.peakAbortRisk = 0.78;

    applyLaunchOutcome(calm, catalog, calmOutcome);
    applyLaunchOutcome(tense, catalog, tenseOutcome);

    require(calmPilot->stress == 12, "calm survived launch should keep baseline stress gain");
    require(tensePilot->stress > calmPilot->stress, "near-failure telemetry should add crew stress");
    require(tensePilot->stress >= 24, "high WARN and ABORT should create a noticeable stress penalty");
}

void pressureControlModulesReducePressureTelemetry()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState bare = createNewGame(catalog, 404);
    bare.run.equippedModuleIds = {"sparrow_engine", "patchwork_hull", "radiator_vanes", "spring_capsule"};
    syncLaunchConfig(bare, catalog);

    GameState controlled = bare;
    controlled.run.equippedModuleIds.push_back("stable_tank");
    controlled.run.equippedModuleIds.push_back("analog_telemetry");
    syncLaunchConfig(controlled, catalog);

    Random bareRng(505);
    Random controlledRng(505);
    const PreparedLaunch bareLaunch = prepareLaunch(bare, catalog, bareRng);
    const PreparedLaunch controlledLaunch = prepareLaunch(controlled, catalog, controlledRng);
    const double burn = 1.0 + (catalog.destinations[0].targetMultiplier - 1.0) * 0.78;

    require(controlledLaunch.pressureModifier < bareLaunch.pressureModifier, "pressure-control modules should lower mission pressure modifier");
    require(telemetryAt(controlledLaunch, burn).pressure < telemetryAt(bareLaunch, burn).pressure, "pressure-control modules should lower PRESS telemetry");
}

void starterEarthOrbitIsProvingFirst()
{
    const ContentCatalog catalog = createDefaultContent();
    constexpr int samples = 900;
    int firstAttemptFullProfileSuccesses = 0;
    int secondAttemptFullProfileSuccesses = 0;
    int thirdAttemptFullProfileSuccesses = 0;
    int provingLosses = 0;

    for (int i = 0; i < samples; ++i) {
        {
            GameState state = configuredState(catalog, 0, catalog.destinations[0].targetMultiplier);
            Random rng(88000 + static_cast<std::uint64_t>(i));
            const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
            const LaunchOutcome outcome = resolveLaunch(launch, catalog, state, catalog.destinations[0].targetMultiplier, RecoveryMethod::ReturnHome, rng);
            firstAttemptFullProfileSuccesses += outcome.type == LaunchResultType::MissionComplete ? 1 : 0;
        }
        {
            GameState state = configuredState(catalog, 0, catalog.destinations[0].targetMultiplier);
            state.meta.destinationAttempts[0] = 1;
            Random rng(89000 + static_cast<std::uint64_t>(i));
            const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
            const LaunchOutcome outcome = resolveLaunch(launch, catalog, state, catalog.destinations[0].targetMultiplier, RecoveryMethod::ReturnHome, rng);
            secondAttemptFullProfileSuccesses += outcome.type == LaunchResultType::MissionComplete ? 1 : 0;
        }
        {
            GameState state = configuredState(catalog, 0, catalog.destinations[0].targetMultiplier);
            state.meta.destinationAttempts[0] = 2;
            Random rng(90000 + static_cast<std::uint64_t>(i));
            const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
            const LaunchOutcome outcome = resolveLaunch(launch, catalog, state, catalog.destinations[0].targetMultiplier, RecoveryMethod::ReturnHome, rng);
            thirdAttemptFullProfileSuccesses += outcome.type == LaunchResultType::MissionComplete ? 1 : 0;
        }
        {
            GameState state = createNewGame(catalog, 99000 + static_cast<std::uint64_t>(i));
            Random rng(99000 + static_cast<std::uint64_t>(i));
            const LaunchOutcome outcome = simulateLaunchToTarget(state, catalog, rng);
            provingLosses += outcome.type == LaunchResultType::Destroyed ? 1 : 0;
        }
    }

    require(firstAttemptFullProfileSuccesses < samples * 12 / 100, "first Earth Orbit full profile should be a long-shot, not the default opening move");
    require(secondAttemptFullProfileSuccesses < samples * 40 / 100, "second Earth Orbit full profile should still be risky without upgrades");
    require(thirdAttemptFullProfileSuccesses < samples * 60 / 100, "third Earth Orbit full profile should still be far from guaranteed without upgrades");
    require(provingLosses < samples * 42 / 100, "first Earth Orbit proving runs should still be relatively survivable");
}

void starterProvingEconomyFundsEarlyRefits()
{
    const ContentCatalog catalog = createDefaultContent();
    constexpr int samples = 700;
    int recovered = 0;
    double netCredits = 0.0;

    for (int i = 0; i < samples; ++i) {
        GameState state = createNewGame(catalog, 120000 + static_cast<std::uint64_t>(i));
        Random rng(120000 + static_cast<std::uint64_t>(i));
        const LaunchOutcome outcome = simulateLaunchToTarget(state, catalog, rng);
        if (outcome.type != LaunchResultType::Destroyed) {
            recovered += 1;
            netCredits += outcome.payout - outcome.recoveryCost;
        }
    }

    require(recovered > samples / 2, "most starter proving flights should return enough data to keep playing");
    require(netCredits / static_cast<double>(std::max(1, recovered)) > 5.0, "starter proving returns should average enough credits to fund early refits");
    require(moduleOfferCost(catalog.modules[1]) <= 35, "early uncommon refits should be reachable after a clean starter return");
}

void returnHomeRewardShelvesMatchRefitCosts()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 515);
    const Destination& destination = currentDestination(state, catalog);
    const double dataGoal = state.launchConfig.burnGoalMultiplier;
    const double uncommonGoal = dataGoal + (destination.targetMultiplier - dataGoal) * 0.45;

    LaunchOutcome commonReturn;
    require(firstRecoveredReturnAtBurn(state, catalog, dataGoal, 150000, commonReturn), "test should find a recovered data-goal return");
    require(commonReturn.payout - commonReturn.recoveryCost >= static_cast<double>(moduleOfferCost(Rarity::Common)) - 0.001, "returning at data goal should guarantee a common refit");

    LaunchOutcome uncommonReturn;
    require(firstRecoveredReturnAtBurn(state, catalog, uncommonGoal, 160000, uncommonReturn), "test should find a recovered pushed return");
    require(uncommonReturn.payout - uncommonReturn.recoveryCost >= static_cast<double>(moduleOfferCost(Rarity::Uncommon)) - 0.001, "pushing past data goal should guarantee an uncommon refit when recovered");

    GameState proven = state;
    proven.meta.destinationAttempts[0] = 5;
    proven.meta.destinationSuccesses[0] = 1;
    if (Astronaut* pilot = activeAstronaut(proven)) {
        pilot->training = 8;
        pilot->stress = 0;
    }

    LaunchOutcome rareReturn;
    require(firstRecoveredReturnAtBurn(proven, catalog, destination.targetMultiplier, 170000, rareReturn), "test should find a recovered full-profile return");
    require(rareReturn.payout - rareReturn.recoveryCost >= static_cast<double>(moduleOfferCost(Rarity::Rare)) - 0.001, "surviving the full target should guarantee a rare refit");
}

void overburnRewardsBeatLinearScalingAfterGoal()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 909);
    state.meta.destinationAttempts[0] = 5;
    state.meta.destinationSuccesses[0] = 1;
    if (Astronaut* pilot = activeAstronaut(state)) {
        pilot->training = 8;
        pilot->stress = 0;
    }
    syncLaunchConfig(state, catalog);

    Random launchRng(909);
    PreparedLaunch launch = prepareLaunch(state, catalog, launchRng);
    launch.stats.hull += 20.0;
    launch.stats.cooling += 20.0;
    launch.stats.fuel += 20.0;
    launch.stats.sensors += 20.0;

    const Destination& destination = catalog.destinations[0];
    const double atGoalBurn = destination.targetMultiplier;
    const double overGoalBurn = destination.targetMultiplier + 0.30;
    launch.crashMultiplier = overGoalBurn + 0.40;

    LaunchOutcome atGoal;
    LaunchOutcome overGoal;
    for (int i = 0; i < 5000; ++i) {
        Random resolveRng(180000 + static_cast<std::uint64_t>(i));
        const LaunchOutcome outcome = resolveLaunch(launch, catalog, state, atGoalBurn, RecoveryMethod::ReturnHome, resolveRng);
        if (outcome.type != LaunchResultType::Destroyed) {
            atGoal = outcome;
            break;
        }
    }
    for (int i = 0; i < 5000; ++i) {
        Random resolveRng(185000 + static_cast<std::uint64_t>(i));
        const LaunchOutcome outcome = resolveLaunch(launch, catalog, state, overGoalBurn, RecoveryMethod::ReturnHome, resolveRng);
        if (outcome.type != LaunchResultType::Destroyed) {
            overGoal = outcome;
            break;
        }
    }

    require(atGoal.type != LaunchResultType::Destroyed, "test should find a recovered target return");
    require(overGoal.type != LaunchResultType::Destroyed, "test should find a recovered overburn return");
    const double linearRatio = overGoalBurn / atGoalBurn;
    require(overGoal.payout > atGoal.payout * linearRatio * 1.25, "overburn payout should beat linear reward scaling after the goal");
}

void saveRoundTripPreservesProgress()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 55);
    state.run.credits = 222.0;
    state.run.destinationIndex = 2;
    state.run.frontierReadiness = 3;
    state.run.shipDamage = 17;
    state.run.offerRerollsThisExpedition = 2;
    state.run.repairOpsThisExpedition = 1;
    state.run.trainingOpsThisExpedition = 2;
    state.run.restOpsThisExpedition = 3;
    state.meta.unlockKeys.push_back("thermal");
    state.meta.blueprintProgress = 5;
    state.meta.shipsLost = 1;
    state.meta.destinationAttempts = {2, 1, 0};
    state.meta.destinationSuccesses = {1, 0, 0};
    state.meta.memorials.push_back("Test Pilot lost during Mars");
    state.run.crewUpgradeIds = {"analog_sim_bay", "medical_recovery_ward"};
    state.run.crew.front().training = 7;
    state.run.crew.front().stress = 42;
    state.run.crew.front().status = CrewStatus::Injured;

    const std::string text = serializeSaveData(captureSaveData(state));
    const auto save = deserializeSaveData(text);
    require(save.has_value(), "serialized save should parse");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);

    require(std::abs(restored.run.credits - 222.0) < 0.001, "credits should round trip");
    require(restored.run.destinationIndex == 2, "destination index should round trip");
    require(restored.run.frontierReadiness == 3, "frontier readiness should round trip");
    require(restored.run.shipDamage == 17, "ship damage should round trip");
    require(restored.run.offerRerollsThisExpedition == 2, "refit reroll count should round trip");
    require(restored.run.repairOpsThisExpedition == 1, "repair escalation should round trip");
    require(restored.run.trainingOpsThisExpedition == 2, "training escalation should round trip");
    require(restored.run.restOpsThisExpedition == 3, "rest escalation should round trip");
    require(hasUnlock(restored.meta, "thermal"), "unlock keys should round trip");
    require(restored.meta.destinationAttempts.size() >= 3 && restored.meta.destinationAttempts[0] == 2, "destination attempts should round trip");
    require(restored.meta.destinationSuccesses.size() >= 3 && restored.meta.destinationSuccesses[0] == 1, "destination successes should round trip");
    require(restored.meta.memorials.size() == 1, "memorials should round trip");
    require(restored.run.crewUpgradeIds.size() == 2 && restored.run.crewUpgradeIds[0] == "analog_sim_bay", "crew upgrades should round trip");
    require(restored.run.crew.front().training == 7, "crew training should round trip");
    require(restored.run.crew.front().stress == 42, "crew stress should round trip");
    require(restored.run.crew.front().status == CrewStatus::Injured, "crew status should round trip");
}

void destinationRiskEscalates()
{
    const ContentCatalog catalog = createDefaultContent();
    int earthDestroyed = 0;
    int galaxyDestroyed = 0;
    constexpr int samples = 2000;

    for (int i = 0; i < samples; ++i) {
        {
            GameState state = configuredState(catalog, 0, catalog.destinations[0].targetMultiplier);
            state.meta.destinationAttempts[0] = 4;
            state.meta.destinationSuccesses[0] = 1;
            Random rng(1000 + static_cast<std::uint64_t>(i));
            const LaunchOutcome outcome = simulateLaunchToTarget(state, catalog, rng);
            earthDestroyed += outcome.type == LaunchResultType::Destroyed ? 1 : 0;
        }
        {
            GameState state = configuredState(catalog, 5, catalog.destinations[5].targetMultiplier);
            state.meta.destinationAttempts[5] = 4;
            state.meta.destinationSuccesses[5] = 1;
            Random rng(1000 + static_cast<std::uint64_t>(i));
            const LaunchOutcome outcome = simulateLaunchToTarget(state, catalog, rng);
            galaxyDestroyed += outcome.type == LaunchResultType::Destroyed ? 1 : 0;
        }
    }

    require(galaxyDestroyed > earthDestroyed, "farther destination should be more dangerous at target multiplier");
}

void starterMoonTransferIsNotReliable()
{
    const ContentCatalog catalog = createDefaultContent();
    int destroyed = 0;
    int heatDominant = 0;
    double requiredPrepCrashTotal = 0.0;
    double overpreparedCrashTotal = 0.0;
    constexpr int samples = 1000;

    for (int i = 0; i < samples; ++i) {
        GameState state = createNewGame(catalog, 333);
        state.run.frontierReadiness = frontierReadinessRequired(state, catalog);
        state.launchConfig.frontierTransfer = true;
        state.launchConfig.destinationId = catalog.destinations[1].id;
        state.launchConfig.burnGoalMultiplier = catalog.destinations[1].targetMultiplier;
        Random rng(5000 + static_cast<std::uint64_t>(i));
        const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
        requiredPrepCrashTotal += launch.crashMultiplier;
        const LaunchOutcome outcome = resolveLaunch(launch, catalog, state, catalog.destinations[1].targetMultiplier, RecoveryMethod::TransferArrival, rng);
        destroyed += outcome.type == LaunchResultType::Destroyed ? 1 : 0;
        const double sampleBurn = std::min(catalog.destinations[1].targetMultiplier, launch.crashMultiplier - 0.02);
        const TelemetryEvent event = telemetryAt(launch, sampleBurn);
        const double worstNonHeat = std::max({event.pressure, event.vibration, event.fuelMix, event.guidance, event.abortRisk});
        heatDominant += event.heat > worstNonHeat ? 1 : 0;

        GameState overprepared = createNewGame(catalog, 333);
        overprepared.run.frontierReadiness = frontierReadinessCap(overprepared, catalog);
        overprepared.launchConfig.frontierTransfer = true;
        overprepared.launchConfig.destinationId = catalog.destinations[1].id;
        overprepared.launchConfig.burnGoalMultiplier = catalog.destinations[1].targetMultiplier;
        Random overpreparedRng(5000 + static_cast<std::uint64_t>(i));
        const PreparedLaunch overpreparedLaunch = prepareLaunch(overprepared, catalog, overpreparedRng);
        overpreparedCrashTotal += overpreparedLaunch.crashMultiplier;
    }

    require(destroyed > samples * 60 / 100, "starter ship should not reliably reach the Moon transfer burn");
    require(destroyed < samples * 97 / 100, "prepared Moon transfer should not be a guaranteed early explosion");
    require(heatDominant < samples * 70 / 100, "Moon transfer failures should not be overwhelmingly thermal");
    require(overpreparedCrashTotal > requiredPrepCrashTotal + 80.0, "extra Earth Orbit data should meaningfully improve Moon transfer odds");
}

void frontierReadinessGatesProgression()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 77);
    require(state.run.destinationIndex == 0, "new program should start at Earth Orbit frontier");
    require(!canCommitToNextFrontier(state, catalog), "new program should not immediately commit to Moon");

    LaunchOutcome outcome;
    outcome.type = LaunchResultType::MissionComplete;
    outcome.destinationId = catalog.destinations[0].id;
    outcome.ejectMultiplier = catalog.destinations[0].targetMultiplier;
    outcome.payout = 10.0;

    applyLaunchOutcome(state, catalog, outcome);
    require(state.run.destinationIndex == 0, "mission complete should not auto-cycle destination");
    require(state.run.frontierReadiness == 1, "mission complete should bank frontier readiness");

    state.run.frontierReadiness = frontierReadinessRequired(state, catalog);
    require(canCommitToNextFrontier(state, catalog), "enough readiness should unlock next frontier commit");
    require(commitToNextFrontier(state, catalog), "commit should advance exactly one frontier");
    require(state.run.destinationIndex == 1, "commit should move from Earth Orbit to Moon");
    require(state.run.frontierReadiness == 0, "commit should reset readiness for the new frontier");
}

void transferAttemptAdvancesOnlyOnSuccess()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 88);
    state.run.frontierReadiness = frontierReadinessRequired(state, catalog);

    LaunchOutcome aborted;
    aborted.type = LaunchResultType::SafeEject;
    aborted.recoveryMethod = RecoveryMethod::ReturnHome;
    aborted.destinationId = catalog.destinations[1].id;
    aborted.frontierTransfer = true;
    aborted.ejectMultiplier = 1.2;
    aborted.payout = 12.0;
    applyLaunchOutcome(state, catalog, aborted);
    require(state.run.destinationIndex == 0, "aborted transfer should not advance frontier");
    require(state.meta.furthestTier == 0, "aborted transfer should not count as reaching next frontier");

    state.run.frontierReadiness = frontierReadinessRequired(state, catalog);
    LaunchOutcome transfer;
    transfer.type = LaunchResultType::MissionComplete;
    transfer.recoveryMethod = RecoveryMethod::TransferArrival;
    transfer.destinationId = catalog.destinations[1].id;
    transfer.frontierTransfer = true;
    transfer.ejectMultiplier = catalog.destinations[1].targetMultiplier;
    transfer.payout = 20.0;
    applyLaunchOutcome(state, catalog, transfer);

    require(state.run.destinationIndex == 1, "successful transfer should advance to next frontier");
    require(state.run.frontierReadiness == 0, "successful transfer should reset readiness");
    require(state.launchConfig.burnGoalMultiplier < catalog.destinations[1].targetMultiplier, "new frontier should default back to a proving return target");
}

void overpreparedReadinessRaisesProvingStakes()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline = createNewGame(catalog, 919);
    GameState overprepared = baseline;
    const int required = frontierReadinessRequired(baseline, catalog);
    const int cap = frontierReadinessCap(baseline, catalog);
    require(cap > required, "frontier readiness should allow over-preparation past the transfer gate");

    baseline.run.frontierReadiness = required;
    overprepared.run.frontierReadiness = cap;
    baseline.meta.destinationAttempts[0] = 4;
    baseline.meta.destinationSuccesses[0] = 2;
    overprepared.meta.destinationAttempts[0] = 4;
    overprepared.meta.destinationSuccesses[0] = 2;

    Random baselineRng(2222);
    Random overpreparedRng(2222);
    const PreparedLaunch baselineLaunch = prepareLaunch(baseline, catalog, baselineRng);
    const PreparedLaunch overpreparedLaunch = prepareLaunch(overprepared, catalog, overpreparedRng);

    require(baselineLaunch.overpreparedData == 0, "required data should not count as over-prepared");
    require(overpreparedLaunch.overpreparedData == cap - required, "extra data should feed the proving-flight bonus");
    require(overpreparedLaunch.crashMultiplier > baselineLaunch.crashMultiplier, "extra data should raise the hidden failure curve");
    require(overpreparedLaunch.provingPayoutBonus > baselineLaunch.provingPayoutBonus, "extra data should increase proving flight reward");
}

} // namespace

int main()
{
    deterministicLaunchesMatch();
    safeAndDestroyedOutcomesResolve();
    moduleAggregationIncludesFrameAndDamage();
    earlyTelemetryShowsSystemLoad();
    launchIncidentsAreChunkyAndRecoverable();
    crewStressUsesDiscreteStepsForPilotRisk();
    cutEnginesTradeHeatForNavigation();
    shipTravelIsFasterWithoutRetuningTelemetry();
    emergencyActionsTradeOneRiskForAnother();
    emergencyRecruitmentPreventsDeadRosterSoftLock();
    moduleOffersAreOneChoiceRefits();
    refitRerollsSpendAndEscalate();
    crewUpgradeOffersInstallAndModifyCrewOps();
    hangarOpsStartCheapAndEscalate();
    lowCreditRefitWindowIncludesAffordableOffer();
    pressureTracksFrontierExperience();
    crewStressTracksPeakTelemetryDanger();
    pressureControlModulesReducePressureTelemetry();
    starterEarthOrbitIsProvingFirst();
    starterProvingEconomyFundsEarlyRefits();
    returnHomeRewardShelvesMatchRefitCosts();
    overburnRewardsBeatLinearScalingAfterGoal();
    saveRoundTripPreservesProgress();
    destinationRiskEscalates();
    starterMoonTransferIsNotReliable();
    frontierReadinessGatesProgression();
    transferAttemptAdvancesOnlyOnSuccess();
    overpreparedReadinessRaisesProvingStakes();

    std::cout << "rocket_core_tests passed\n";
    return 0;
}
