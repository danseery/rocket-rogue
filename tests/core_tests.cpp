#include "core/Content.h"
#include "core/ContentIds.h"
#include "core/CrewPresentation.h"
#include "core/FlightProgress.h"
#include "core/GameFormat.h"
#include "core/GameMath.h"
#include "core/GameState.h"
#include "core/HangarPresentation.h"
#include "core/LaunchBalance.h"
#include "core/LaunchPresentation.h"
#include "core/LaunchReadinessPresentation.h"
#include "core/LaunchStatus.h"
#include "core/LaunchSimulation.h"
#include "core/OutcomePresentation.h"
#include "core/PanelChromePresentation.h"
#include "core/ProgramPresentation.h"
#include "core/RefitPresentation.h"
#include "core/SaveData.h"
#include "core/SaveSchema.h"
#include "core/ShipPresentation.h"
#include "core/Telemetry.h"
#include "core/Tuning.h"
#include "core/GameUi.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace rocket;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::abort();
    }
}

const HangarOperationCardPresentation* findHangarOperationCard(const std::vector<HangarOperationCardPresentation>& cards, std::string_view title);

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
    require(doomed.shipDamage == tuning::damage::destroyedShipDamage, "destroyed launch should total the ship");
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
    for (const TelemetryChannelSample& sample : telemetrySamples(event)) {
        activeChannels += sample.value > 0.015 ? 1 : 0;
    }

    require(activeChannels >= 4, "early telemetry should show multiple non-zero system channels near the data goal");
    require(event.warning > 0.02, "early telemetry warning should not be completely flat near the data goal");
}

double telemetryLoad(const TelemetryEvent& event)
{
    return telemetryChannelLoad(event);
}

double unscaledBurnMultiplierDelta(const PreparedLaunch& launch, const Destination& destination, double elapsedSeconds, double deltaSeconds)
{
    const double dt = std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds);
    const double thrust = std::max(tuning::launch::minimumEffectiveThrust, launch.stats.thrust);
    const double cruiseRate =
        tuning::launch::cruiseBaseRate +
        thrust * tuning::launch::cruiseThrustScale +
        static_cast<double>(destination.tier) * tuning::launch::cruiseTierScale;
    const double acceleration = (tuning::launch::accelerationBaseRate + destination.hazard * tuning::launch::accelerationHazardScale) * launch.throttleFactor;
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
        TelemetryEvent incidentEvent;
        incidentEvent.heat = incident.heat;
        incidentEvent.pressure = incident.pressure;
        incidentEvent.vibration = incident.vibration;
        incidentEvent.fuelMix = incident.fuelMix;
        incidentEvent.guidance = incident.guidance;
        incidentEvent.abortRisk = incident.abortRisk;

        int activeChannels = 0;
        for (const TelemetryChannelSample& sample : telemetrySamples(incidentEvent)) {
            activeChannels += sample.value > 0.01 ? 1 : 0;
        }
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
    tensePilot->stress = tuning::crew::maxStress;

    require(crewStressStepCount(tuning::crew::stressPerStep - 1) == 0, "stress below one step should not create a stress step");
    require(crewStressStepCount(tuning::crew::stressPerStep) == 1, "each stress step threshold should create one stress step");
    require(crewStressStepCount(tuning::crew::maxStress) == tuning::crew::maxStressSteps, "max stress should create the tuned max stress steps");
    require(effectiveTrainingLevel(*tensePilot) == -4, "stress steps should cancel effective training in discrete chunks");
    require(crewAbortRiskMultiplierFromStress(tuning::crew::maxStress) >= 1.99, "max stress should effectively double ABORT growth");

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
    const double unscaledDelta = unscaledBurnMultiplierDelta(launch, destination, elapsed, delta);
    require(std::abs(boostedDelta - unscaledDelta * tuning::launch::travelSpeedMultiplier) < 0.000001, "ship travel should apply the tuned travel speed multiplier");

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

void flightActionsComposeThroughOneCoreHelper()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = configuredState(catalog, 0, catalog.destinations[0].targetMultiplier);
    Random rng(67);
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);

    FlightActionState actions;
    actions.cutEnginesActive = true;
    actions.pressureReliefOpen = true;
    actions.cargoJettisoned = true;
    const PreparedLaunch combined = applyFlightActions(launch, actions);
    require(combined.throttleFactor == tuning::launch::cutEngineThrottleFactor, "action composition should apply cut-engine throttle");
    require(combined.pressureRelief == tuning::launch::pressureReliefSuccessAmount, "action composition should apply relief valve pressure effect");
    require(combined.cargoReturnPenalty == tuning::launch::cargoReturnPenalty, "action composition should apply cargo return penalty");

    actions.returningHome = true;
    const PreparedLaunch returning = applyFlightActions(launch, actions);
    require(returning.throttleFactor == launch.throttleFactor, "return-home action composition should not keep cut-engine throttle");
    require(returning.pressureRelief == tuning::launch::pressureReliefSuccessAmount, "return-home composition should preserve open relief valve effects");
    require(returning.cargoReturnPenalty == tuning::launch::cargoReturnPenalty, "return-home composition should preserve cargo return penalty");
}

void launchStatusLinesComeFromSharedSelector()
{
    TelemetryEvent nominal;
    nominal.message = "Tracking system margins below limits";

    LaunchStatusContext context;
    context.event = nominal;
    require(launchStatusLine(context) == std::string(text::status::provingBurnStable), "nominal proving flight should use shared proving status");

    context.pastDataGoal = true;
    require(launchStatusLine(context) == std::string(text::status::dataGoalReached), "past-goal proving flight should use shared goal status");

    context.actions.cutEnginesActive = true;
    require(launchStatusLine(context) == std::string(text::status::enginesCutAfterGoal), "cut engines after goal should use shared cut-engine status");

    context = {};
    context.event = nominal;
    context.frontierTransfer = true;
    require(launchStatusLine(context) == std::string(text::status::transferBurnStable), "nominal transfer should use shared transfer status");

    context = {};
    context.event = nominal;
    context.actions.returningHome = true;
    context.returnProgress = tuning::session::returnEarlyProgressThreshold - 0.01;
    require(launchStatusLine(context) == std::string(text::status::returnBurnRotating), "early return burn should use shared rotation status");

    context.returnProgress = tuning::session::returnEarlyProgressThreshold + 0.01;
    require(launchStatusLine(context) == std::string(text::status::returnBurnUnderway), "later return burn should use shared return status");

    context.returnDriftHome = true;
    require(launchStatusLine(context) == std::string(text::status::coastingHome), "drift return should use shared coast status");

    TelemetryEvent critical = nominal;
    critical.message = "TEMP: cooling runaway";
    critical.warning = tuning::launch::warningCriticalThreshold + 0.01;
    context = {};
    context.event = critical;
    require(launchStatusLine(context) == text::telemetryDecision(critical.message), "critical outbound warning should ask for a decision");

    context.actions.returningHome = true;
    context.returnDriftHome = true;
    require(launchStatusLine(context) == text::returnDriftWarning(critical.message), "critical drift return should use return warning copy");
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

    require(offerRerollCost(state) == tuning::hangar::rerollBaseCost, "first refit reroll should cost the tuned base credits");
    require(rerollOffers(state, catalog, rng), "affordable refit reroll should succeed");
    require(state.run.credits == 190.0, "first refit reroll should spend 10 credits");
    require(state.run.offerRerollsThisExpedition == 1, "first refit reroll should increment run reroll count");
    require(offerRerollCost(state) == tuning::hangar::rerollBaseCost * 2.0, "second refit reroll should cost twice the base credits");

    require(rerollOffers(state, catalog, rng), "second affordable refit reroll should succeed");
    require(state.run.credits == 170.0, "second refit reroll should spend 20 credits");
    require(state.run.offerRerollsThisExpedition == 2, "second refit reroll should increment run reroll count");
    require(offerRerollCost(state) == tuning::hangar::rerollBaseCost * 3.0, "third refit reroll should cost three times the base credits");

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
    state.meta.unlockKeys = {
        content::unlock::starter,
        content::unlock::thermal,
        content::unlock::recovery,
        content::unlock::deepSpace,
        content::unlock::ai
    };
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

    state.run.crewUpgradeIds = {
        content::crewUpgrade::analogSimBay,
        content::crewUpgrade::highGSimulator,
        content::crewUpgrade::medicalRecoveryWard,
        content::crewUpgrade::missionPsychOffice,
        content::crewUpgrade::traitCoachingLab
    };
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
    pilot->stress = tuning::crew::maxStress;
    require(!trainCrew(state, catalog), "crew training should be blocked at max stress");
    pilot->stress = tuning::crew::maxStress - crewTrainingStressGain(state, catalog) + 1;
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
    upgraded.run.crewUpgradeIds = {
        content::crewUpgrade::missionPsychOffice,
        content::crewUpgrade::traitCoachingLab
    };
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

void hangarOperationPreviewMatchesCoreMath()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 818);
    state.run.credits = 120.0;
    state.run.shipDamage = 48;
    Astronaut* pilot = activeAstronaut(state);
    require(pilot != nullptr, "hangar preview test needs a pilot");
    pilot->stress = 42;

    HangarOperationPreview preview = hangarOperationPreview(state, catalog);
    require(preview.repairAmount == repairShipAmount(state), "hangar preview should share repair amount");
    require(std::abs(preview.repairCost - repairShipCost(state)) < 0.001, "hangar preview should share repair cost");
    require(preview.trainingGain == crewTrainingGain(state, catalog), "hangar preview should share training gain");
    require(preview.trainingStressGain == crewTrainingStressGain(state, catalog), "hangar preview should share simulator stress");
    require(std::abs(preview.trainingCost - crewTrainingCost(state, catalog)) < 0.001, "hangar preview should share training cost");
    require(preview.restStressRecovery == crewRestStressRecovery(state, catalog), "hangar preview should share rest recovery");
    require(std::abs(preview.restCost - crewRestCost(state, catalog)) < 0.001, "hangar preview should share rest cost");
    require(preview.repairAvailable && preview.trainingAvailable && preview.restAvailable, "funded hangar preview should mark available ops");

    pilot->stress = tuning::crew::maxStress;
    preview = hangarOperationPreview(state, catalog);
    require(!preview.trainingAvailable, "hangar preview should block training at max stress");

    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = CrewStatus::Dead;
    }
    state.run.credits = 0.0;
    preview = hangarOperationPreview(state, catalog);
    require(preview.emergencyRecruitment, "hangar preview should flag emergency recruitment when no pilot is alive");
    require(preview.recruitCost == tuning::hangar::emergencyRecruitCost, "hangar preview should use emergency recruit cost");
    require(preview.recruitAvailable, "free emergency recruitment should be available when broke");
}

void hangarOperationCardsComeFromSharedPreview()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 819);
    state.run.credits = 120.0;
    state.run.shipDamage = 24;
    Astronaut* pilot = activeAstronaut(state);
    require(pilot != nullptr, "hangar card test needs a pilot");
    pilot->stress = 20;

    const HangarOperationPreview preview = hangarOperationPreview(state, catalog);
    std::vector<HangarOperationCardPresentation> cards = hangarOperationCards(state, catalog);
    require(cards.size() == 3, "active-crew hangar should show repair, simulator, and rest cards");

    const HangarOperationCardPresentation* repair = findHangarOperationCard(cards, text::panel::ops::repairBay);
    const HangarOperationCardPresentation* simulator = findHangarOperationCard(cards, text::panel::ops::simulatorBurn);
    const HangarOperationCardPresentation* rest = findHangarOperationCard(cards, text::panel::ops::medicalRest);
    require(repair != nullptr && repair->detail == text::panel::repairDetail(preview.repairAmount), "repair card should share preview repair detail");
    require(repair != nullptr && repair->cost == display::credits(preview.repairCost), "repair card should share preview repair cost");
    require(repair != nullptr && repair->actionId == ui::actions::repairShip && repair->available == preview.repairAvailable, "repair card should share action and availability");
    require(simulator != nullptr && simulator->detail == text::panel::simulatorDetail(preview.trainingGain, preview.trainingStressGain), "simulator card should share training detail");
    require(simulator != nullptr && simulator->cost == display::credits(preview.trainingCost), "simulator card should share training cost");
    require(simulator != nullptr && simulator->actionId == ui::actions::trainCrew && simulator->available == preview.trainingAvailable, "simulator card should share action and availability");
    require(rest != nullptr && rest->detail == text::panel::restDetail(preview.restStressRecovery), "rest card should share rest detail");
    require(rest != nullptr && rest->cost == display::credits(preview.restCost), "rest card should share rest cost");
    require(rest != nullptr && rest->actionId == ui::actions::restCrew && rest->available == preview.restAvailable, "rest card should share action and availability");

    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = CrewStatus::Dead;
    }
    state.run.credits = 0.0;
    cards = hangarOperationCards(state, catalog);
    require(cards.size() == 3, "no-pilot hangar should show repair and two recruit cards");
    const HangarOperationCardPresentation* crewIntake = findHangarOperationCard(cards, text::panel::ops::crewIntake);
    const HangarOperationCardPresentation* reserveRoster = findHangarOperationCard(cards, text::panel::ops::reserveRoster);
    require(crewIntake != nullptr && crewIntake->detail == std::string(text::panel::messages::emergencyReplacement), "crew intake card should use emergency copy");
    require(crewIntake != nullptr && crewIntake->cost == display::credits(tuning::hangar::emergencyRecruitCost), "crew intake card should use emergency recruit cost");
    require(crewIntake != nullptr && crewIntake->available, "free emergency crew intake should be available");
    require(reserveRoster != nullptr && reserveRoster->cost == display::credits(tuning::hangar::recruitCost), "reserve roster card should use standard recruit cost");
    require(reserveRoster != nullptr && !reserveRoster->available, "reserve roster card should respect credits");
}

void lowCreditRefitWindowIncludesAffordableOffer()
{
    const ContentCatalog catalog = createDefaultContent();

    for (int i = 0; i < 40; ++i) {
        GameState state = createNewGame(catalog, 220 + static_cast<std::uint64_t>(i));
        state.run.credits = 35.0;
        state.meta.unlockKeys = {
            content::unlock::starter,
            content::unlock::thermal,
            content::unlock::recovery
        };

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

    const PostLaunchCrewStress calmStress = postLaunchCrewStress(calmOutcome, {});
    const PostLaunchCrewStress tenseStress = postLaunchCrewStress(tenseOutcome, {});
    require(calmStress.baseStress == tuning::stress::survivedLaunchStress, "survived launch stress should expose the tuned base value");
    require(calmStress.warningStress == 0 && calmStress.abortStress == 0, "calm telemetry should not add danger stress");
    require(calmStress.total == tuning::stress::survivedLaunchStress, "calm stress helper should match baseline stress");
    require(tenseStress.warningStress > 0 && tenseStress.abortStress > 0, "high WARN and ABORT should both contribute stress");
    require(tenseStress.total > calmStress.total, "near-failure telemetry should increase helper stress");

    CrewUpgradeStats debriefSupport;
    debriefSupport.launchStressRelief = 999;
    require(postLaunchCrewStressGain(tenseOutcome, debriefSupport) == 0, "stress relief should clamp launch stress at zero");

    applyLaunchOutcome(calm, catalog, calmOutcome);
    applyLaunchOutcome(tense, catalog, tenseOutcome);

    require(calmPilot->stress == calmStress.total, "applied calm outcome should use shared stress helper");
    require(tensePilot->stress > calmPilot->stress, "near-failure telemetry should add crew stress");
    require(tensePilot->stress == tenseStress.total, "applied tense outcome should use shared stress helper");
}

void pressureControlModulesReducePressureTelemetry()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState bare = createNewGame(catalog, 404);
    bare.run.equippedModuleIds = {
        content::module::sparrowEngine,
        content::module::patchworkHull,
        content::module::radiatorVanes,
        content::module::springCapsule
    };
    syncLaunchConfig(bare, catalog);

    GameState controlled = bare;
    controlled.run.equippedModuleIds.push_back(content::module::stableTank);
    controlled.run.equippedModuleIds.push_back(content::module::analogTelemetry);
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
    state.meta.unlockKeys.push_back(content::unlock::thermal);
    state.meta.blueprintProgress = 5;
    state.meta.shipsLost = 1;
    state.meta.destinationAttempts = {2, 1, 0};
    state.meta.destinationSuccesses = {1, 0, 0};
    state.meta.memorials.push_back("Test Pilot lost during Mars");
    state.run.crewUpgradeIds = {
        content::crewUpgrade::analogSimBay,
        content::crewUpgrade::medicalRecoveryWard
    };
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
    require(hasUnlock(restored.meta, content::unlock::thermal), "unlock keys should round trip");
    require(restored.meta.destinationAttempts.size() >= 3 && restored.meta.destinationAttempts[0] == 2, "destination attempts should round trip");
    require(restored.meta.destinationSuccesses.size() >= 3 && restored.meta.destinationSuccesses[0] == 1, "destination successes should round trip");
    require(restored.meta.memorials.size() == 1, "memorials should round trip");
    require(restored.run.crewUpgradeIds.size() == 2 && restored.run.crewUpgradeIds[0] == content::crewUpgrade::analogSimBay, "crew upgrades should round trip");
    require(restored.run.crew.front().training == 7, "crew training should round trip");
    require(restored.run.crew.front().stress == 42, "crew stress should round trip");
    require(restored.run.crew.front().status == CrewStatus::Injured, "crew status should round trip");
}

void saveSchemaConstantsMatchSerializedFields()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 12);
    state.run.credits = 123.0;
    state.run.inventoryModuleIds = {content::module::sparrowEngine, content::module::cryoLoop};
    state.meta.memorials = {"Ada burned late", "Ben returned home"};

    const std::string text = serializeSaveData(captureSaveData(state));
    require(text.find(std::string(save_schema::header) + "\n") == 0, "save should start with shared schema header");
    require(text.find(std::string(save_schema::field::credits) + save_schema::keyValueDelimiter) != std::string::npos, "credits key should use shared schema name");
    require(text.find(std::string(save_schema::field::inventory) + save_schema::keyValueDelimiter) != std::string::npos, "inventory key should use shared schema name");
    require(text.find(std::string(1, save_schema::textListDelimiter)) != std::string::npos, "text list delimiter should be shared");

    const std::string minimalSave = std::string(save_schema::header) + "\n" +
        std::string(save_schema::field::credits) + save_schema::keyValueDelimiter + "321\n";
    const auto parsed = deserializeSaveData(minimalSave);
    require(parsed.has_value(), "minimal save with shared header should parse");
    require(std::abs(parsed->credits - 321.0) < 0.001, "shared credits key should parse");
    require(!deserializeSaveData("RR_SAVE_V0\ncredits=1\n").has_value(), "unknown save header should not parse");
}

void launchOutcomePresentationIsShared()
{
    LaunchOutcome destroyed;
    destroyed.type = LaunchResultType::Destroyed;
    destroyed.recoveryMethod = RecoveryMethod::ReturnHome;
    destroyed.moduleDestroyedId = content::module::sparrowEngine;
    destroyed.crewKilled = true;

    LaunchOutcomePresentation presentation = launchOutcomePresentation(destroyed);
    require(presentation.label == text::panel::outcomes::returnFailure, "return-home destruction should share return-failure label");
    require(presentation.nextActionLabel == text::buttons::startReplacementRefit, "destroyed outcomes should share replacement action label");
    require(presentation.notes.size() == 2, "destroyed outcomes should include module and crew notes");
    require(presentation.notes[0] == text::panel::lostModule(content::module::sparrowEngine), "lost module note should be shared");
    require(presentation.notes[1] == std::string(text::panel::messages::crewLossRecorded), "crew death note should be shared");

    LaunchOutcome transfer;
    transfer.type = LaunchResultType::MissionComplete;
    transfer.recoveryMethod = RecoveryMethod::TransferArrival;
    transfer.frontierTransfer = true;
    presentation = launchOutcomePresentation(transfer);
    require(presentation.label == text::panel::outcomes::transferComplete, "successful transfer should share transfer label");
    require(presentation.nextActionLabel == text::buttons::reviewRefitOptions, "survived outcomes should share refit review action label");
    require(presentation.notes.empty(), "clean transfer should not invent result notes");

    LaunchOutcome injuredEject;
    injuredEject.type = LaunchResultType::SafeEject;
    injuredEject.recoveryMethod = RecoveryMethod::ManualEject;
    injuredEject.crewInjured = true;
    presentation = launchOutcomePresentation(injuredEject);
    require(presentation.label == text::panel::outcomes::emergencyEject, "manual ejection should share emergency eject label");
    require(presentation.notes.size() == 1 && presentation.notes[0] == std::string(text::panel::messages::crewInjured), "crew injury note should be shared");
}

void enumDisplayLabelsComeFromSharedText()
{
    require(toString(SlotType::Engine) == text::enums::slot::engine, "slot labels should come from shared text");
    require(toString(SlotType::Cooling) == text::enums::slot::cooling, "cooling slot label should come from shared text");
    require(toString(Rarity::Prototype) == text::enums::rarity::prototype, "rarity labels should come from shared text");
    require(toString(CrewStatus::Injured) == text::enums::crewStatus::injured, "crew status labels should come from shared text");
    require(toString(LaunchResultType::MissionComplete) == text::enums::launchResult::missionComplete, "launch result labels should come from shared text");
    require(toString(RecoveryMethod::ReturnHome) == text::enums::recovery::returnHome, "recovery labels should come from shared text");
}

bool hasRefitChip(const RefitPresentation& presentation, std::string_view label, std::string_view value, bool positive)
{
    return std::find_if(presentation.statChips.begin(), presentation.statChips.end(), [&](const RefitStatChip& chip) {
        return chip.label == label && chip.value == value && chip.positive == positive;
    }) != presentation.statChips.end();
}

const DetailPresentationRow* findDetailPresentationRow(const std::vector<DetailPresentationRow>& rows, std::string_view label)
{
    const auto found = std::find_if(rows.begin(), rows.end(), [&](const DetailPresentationRow& row) {
        return !row.heading && row.label == label;
    });
    return found == rows.end() ? nullptr : &(*found);
}

bool hasDetailPresentationHeader(const std::vector<DetailPresentationRow>& rows, std::string_view label)
{
    return std::find_if(rows.begin(), rows.end(), [&](const DetailPresentationRow& row) {
        return row.heading && row.label == label;
    }) != rows.end();
}

const HangarOperationCardPresentation* findHangarOperationCard(const std::vector<HangarOperationCardPresentation>& cards, std::string_view title)
{
    const auto found = std::find_if(cards.begin(), cards.end(), [&](const HangarOperationCardPresentation& card) {
        return card.title == title;
    });
    return found == cards.end() ? nullptr : &(*found);
}

const PanelMetricPresentation* findPanelMetric(const std::vector<PanelMetricPresentation>& metrics, std::string_view label)
{
    const auto found = std::find_if(metrics.begin(), metrics.end(), [&](const PanelMetricPresentation& metric) {
        return metric.label == label;
    });
    return found == metrics.end() ? nullptr : &(*found);
}

const FlightActionButtonPresentation* findFlightActionButton(const std::vector<FlightActionButtonPresentation>& buttons, std::string_view label)
{
    const auto found = std::find_if(buttons.begin(), buttons.end(), [&](const FlightActionButtonPresentation& button) {
        return button.label == label;
    });
    return found == buttons.end() ? nullptr : &(*found);
}

void refitPresentationComesFromSharedHelper()
{
    const ContentCatalog catalog = createDefaultContent();
    const ShipModule* engine = catalog.findModule(content::module::sparrowEngine);
    const ShipModule* tank = catalog.findModule(content::module::stableTank);
    const CrewUpgrade* simBay = catalog.findCrewUpgrade(content::crewUpgrade::analogSimBay);
    require(engine != nullptr && tank != nullptr && simBay != nullptr, "refit presentation test needs default content");

    RefitPresentation engineCard = moduleRefitPresentation(*engine);
    require(engineCard.slotClass == "engine", "module presentation should expose slot class");
    require(engineCard.category == std::string(text::enums::slot::engine), "module presentation should expose shared slot label");
    require(engineCard.rarity == std::string(text::enums::rarity::common), "module presentation should expose shared rarity label");
    require(engineCard.glyph == "E", "module presentation should expose a stable card glyph");
    require(engineCard.detail == std::string(text::moduleThreats::shortensExposure), "engine presentation should use shared threat copy");
    require(engineCard.primaryImpact == "+2.0 Speed", "module presentation should expose strongest stat impact");
    require(hasRefitChip(engineCard, text::moduleStats::speedChip, "+2.0", true), "module presentation should expose speed stat chip");

    RefitPresentation tankCard = moduleRefitPresentation(*tank);
    require(tankCard.detail == std::string(text::moduleThreats::stabilizesPressure), "fuel pressure modules should use pressure threat copy");
    require(hasRefitChip(tankCard, text::moduleStats::pressureChip, "+0.4", true), "module presentation should expose pressure stat chip");

    RefitPresentation crewCard = crewUpgradeRefitPresentation(*simBay);
    require(crewCard.slotClass == "sensors", "crew upgrade presentation should use shared card slot class");
    require(crewCard.category == std::string(text::panel::details::crew), "crew upgrade presentation should expose crew category");
    require(crewCard.glyph == "C", "crew upgrade presentation should expose a stable card glyph");
    require(crewCard.primaryImpact == text::panel::simulatorStressImpact(2), "crew upgrade presentation should expose strongest facility impact");
    require(hasRefitChip(crewCard, text::moduleStats::simStressChip, "+2.0", true), "crew upgrade presentation should expose simulator stress chip");
}

void refitWindowPresentationComesFromSharedHelper()
{
    const ContentCatalog catalog = createDefaultContent();
    const ShipModule* engine = catalog.findModule(content::module::sparrowEngine);
    const CrewUpgrade* simBay = catalog.findCrewUpgrade(content::crewUpgrade::analogSimBay);
    require(engine != nullptr && simBay != nullptr, "refit window presentation test needs default offers");

    GameState state = createNewGame(catalog, 448);
    state.run.credits = 100.0;
    state.run.offerModuleIds = {content::module::sparrowEngine, "", ""};
    state.run.offerCrewUpgradeIds = {"", content::crewUpgrade::analogSimBay, ""};

    const RefitWindowPresentation window = refitWindowPresentation(state, catalog);
    require(window.offers.size() == 2, "refit window presentation should expose resolved module and crew offers");

    const RefitOfferPresentation& moduleOffer = window.offers[0];
    require(moduleOffer.kind == RefitOfferPresentationKind::ShipModule, "module offers should be typed for future render variants");
    require(moduleOffer.index == 0, "module offers should retain their buy-offer index");
    require(moduleOffer.cost == moduleOfferCost(*engine), "module offer presentation should use shared module pricing");
    require(moduleOffer.affordable, "module offer should expose affordability");
    require(moduleOffer.card.title == engine->name, "module offer should include shared card presentation");
    require(moduleOffer.action.enabled, "affordable module offer should enable install action");
    require(moduleOffer.action.label == std::string(text::buttons::install), "module offer should use shared install label");
    require(moduleOffer.action.actionId == ui::actions::buyOffer(0), "module offer should use shared indexed buy action");
    require(moduleOffer.action.cssClass == "ok", "module offer should expose install button style");

    const RefitOfferPresentation& crewOffer = window.offers[1];
    require(crewOffer.kind == RefitOfferPresentationKind::CrewUpgrade, "crew offers should be typed for future render variants");
    require(crewOffer.index == 1, "crew offers should retain their buy-offer index");
    require(crewOffer.cost == crewUpgradeCost(*simBay), "crew offer presentation should use shared crew upgrade pricing");
    require(crewOffer.card.title == simBay->name, "crew offer should include shared card presentation");
    require(crewOffer.action.actionId == ui::actions::buyOffer(1), "crew offer should use shared indexed buy action");

    require(window.rerollCost == offerRerollCost(state), "refit window should expose shared reroll cost");
    require(window.rerollAction.enabled, "affordable reroll should be enabled");
    require(window.rerollAction.label == text::panel::rerollOffers(display::money(window.rerollCost)), "reroll action should use shared label formatting");
    require(window.rerollAction.actionId == std::string(ui::actions::rerollOffers), "reroll action should use shared action id");
    require(window.rerollAction.cssClass == "warn", "reroll action should expose warning button style");
    require(window.skipAction.enabled && window.skipAction.label == std::string(text::buttons::skipRefit), "skip refit action should always be available");
    require(window.skipAction.actionId == std::string(ui::actions::next), "skip refit action should advance through shared action id");

    state.run.credits = 0.0;
    const RefitWindowPresentation brokeWindow = refitWindowPresentation(state, catalog);
    require(!brokeWindow.offers.empty(), "broke refit window should still expose offers");
    require(!brokeWindow.offers[0].affordable, "broke module offer should expose unaffordable state");
    require(!brokeWindow.offers[0].action.enabled, "broke module offer should disable install action");
    require(brokeWindow.offers[0].action.label == text::needCredits(moduleOfferCost(*engine)), "broke module offer should use shared need-credits copy");
    require(!brokeWindow.rerollAction.enabled, "broke reroll should be disabled");
    require(brokeWindow.rerollAction.label == display::needCredits(brokeWindow.rerollCost), "broke reroll should use shared need-credits formatting");
}

void crewDetailsPresentationComesFromSharedHelper()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 441);
    Astronaut* pilot = activeAstronaut(state);
    require(pilot != nullptr, "crew presentation test needs an active astronaut");
    pilot->training = 3;
    pilot->stress = tuning::crew::stressPerStep * 2;
    state.run.crewUpgradeIds.push_back(content::crewUpgrade::analogSimBay);

    const std::vector<DetailPresentationRow> rows = crewDetailsPresentation(state, catalog);
    const DetailPresentationRow* active = findDetailPresentationRow(rows, text::panel::details::active);
    const DetailPresentationRow* training = findDetailPresentationRow(rows, text::panel::details::training);
    const DetailPresentationRow* stress = findDetailPresentationRow(rows, text::panel::details::stress);
    const DetailPresentationRow* simulatorStress = findDetailPresentationRow(rows, text::panel::details::simulatorStress);

    require(active != nullptr && active->value == pilot->name, "crew presentation should expose active astronaut name");
    require(training != nullptr && training->value == display::trainingWithEffective(pilot->training, effectiveTrainingLevel(*pilot)), "crew presentation should share training display format");
    require(stress != nullptr && stress->value == display::stressWithSteps(pilot->stress, crewStressStepCount(pilot->stress)), "crew presentation should share stress display format");
    require(hasDetailPresentationHeader(rows, text::panel::details::crewFacilities), "crew presentation should include crew facilities section");
    require(hasDetailPresentationHeader(rows, text::panel::details::facilityEffects), "crew presentation should include facility effects section");
    require(findDetailPresentationRow(rows, text::enums::rarity::common) != nullptr, "crew presentation should show installed facility rarity row");
    require(simulatorStress != nullptr && simulatorStress->value == text::panel::details::stressDelta(hangarOperationPreview(state, catalog).trainingStressGain), "crew presentation should share simulator stress wording");

    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = CrewStatus::Dead;
    }
    const std::vector<DetailPresentationRow> deadRosterRows = crewDetailsPresentation(state, catalog);
    active = findDetailPresentationRow(deadRosterRows, text::panel::details::active);
    require(active != nullptr && active->value == std::string(text::panel::noneCleared), "crew presentation should handle no cleared astronaut");
}

void shipDetailsPresentationComesFromSharedHelper()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 442);

    const std::vector<DetailPresentationRow> rows = shipDetailsPresentation(state, catalog);
    const DetailPresentationRow* thrust = findDetailPresentationRow(rows, text::moduleStats::thrustDetail);
    const DetailPresentationRow* damage = findDetailPresentationRow(rows, text::moduleStats::damage);
    const DetailPresentationRow* engine = findDetailPresentationRow(rows, text::enums::slot::engine);
    const DetailPresentationRow* inventory = findDetailPresentationRow(rows, text::panel::details::inventory);

    require(thrust != nullptr && !thrust->value.empty(), "ship presentation should expose formatted ship stats");
    require(damage != nullptr && damage->value == display::wholePercent(state.run.shipDamage), "ship presentation should expose damage row");
    require(hasDetailPresentationHeader(rows, text::panel::details::equippedShipUpgrades), "ship presentation should include equipped upgrades section");
    require(hasDetailPresentationHeader(rows, text::panel::details::storedShipUpgrades), "ship presentation should include stored upgrades section");
    require(engine != nullptr && engine->value.find("Sparrow Engine") != std::string::npos, "ship presentation should summarize equipped modules");
    require(inventory != nullptr && inventory->value == std::string(text::panel::noSpareModules), "ship presentation should show no-spares fallback");

    const ShipModule* spareModule = catalog.findModule(content::module::cryoLoop);
    require(spareModule != nullptr, "ship presentation test needs a non-starter spare module");
    state.run.inventoryModuleIds.push_back(spareModule->id);
    const std::vector<DetailPresentationRow> rowsWithSpare = shipDetailsPresentation(state, catalog);
    const std::string spareSummary = shipModuleSummary(*spareModule);
    require(std::find_if(rowsWithSpare.begin(), rowsWithSpare.end(), [&](const DetailPresentationRow& row) {
        return !row.heading && row.value == spareSummary;
    }) != rowsWithSpare.end(), "ship presentation should summarize stored spare modules");
}

void programDetailsPresentationComesFromSharedHelper()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 443);
    const int required = frontierReadinessRequired(state, catalog);
    state.run.frontierReadiness = 2;
    state.meta.blueprintProgress = 9;
    state.meta.shipsLost = 3;
    state.meta.astronautsLost = 2;
    state.meta.furthestTier = 1;

    const std::vector<DetailPresentationRow> frontierRows = frontierDetailsPresentation(state, catalog);
    const DetailPresentationRow* current = findDetailPresentationRow(frontierRows, text::panel::details::current);
    const DetailPresentationRow* flightData = findDetailPresentationRow(frontierRows, text::labels::flightData);
    const DetailPresentationRow* difficulty = findDetailPresentationRow(frontierRows, text::labels::missionDifficulty);
    const DetailPresentationRow* next = findDetailPresentationRow(frontierRows, text::panel::details::next);
    const DetailPresentationRow* transferBurn = findDetailPresentationRow(frontierRows, text::panel::details::transferBurn);

    require(current != nullptr && current->value == catalog.destinations[0].name, "frontier presentation should expose current frontier");
    require(flightData != nullptr && flightData->value == display::fraction(state.run.frontierReadiness, required), "frontier presentation should share readiness display format");
    require(difficulty != nullptr && difficulty->value == display::signedPercent(missionPressureModifier(state, catalog, catalog.destinations[0])), "frontier presentation should share mission difficulty format");
    require(next != nullptr && next->value == catalog.destinations[1].name, "frontier presentation should expose next frontier");
    require(transferBurn != nullptr && transferBurn->value == display::multiplier(catalog.destinations[1].targetMultiplier), "frontier presentation should expose next transfer burn");

    const std::vector<DetailPresentationRow> legacyRows = legacyDetailsPresentation(state);
    const DetailPresentationRow* blueprints = findDetailPresentationRow(legacyRows, text::panel::details::blueprints);
    const DetailPresentationRow* shipsLost = findDetailPresentationRow(legacyRows, text::panel::details::shipsLost);
    const DetailPresentationRow* astronautsLost = findDetailPresentationRow(legacyRows, text::panel::details::astronautsLost);
    const DetailPresentationRow* furthestTier = findDetailPresentationRow(legacyRows, text::panel::details::furthestTier);
    require(blueprints != nullptr && blueprints->value == "9", "legacy presentation should expose blueprint progress");
    require(shipsLost != nullptr && shipsLost->value == "3", "legacy presentation should expose ship losses");
    require(astronautsLost != nullptr && astronautsLost->value == "2", "legacy presentation should expose astronaut losses");
    require(furthestTier != nullptr && furthestTier->value == "1", "legacy presentation should expose furthest tier");
}

void flightProgressHelpersShareTravelAndReturnMath()
{
    const ContentCatalog catalog = createDefaultContent();
    const Destination& earthOrbit = catalog.destinations[0];

    const double midpointBurn = 1.0 + (earthOrbit.targetMultiplier - 1.0) * 0.50;
    require(std::abs(flight_progress::travelProgressForBurn(midpointBurn, earthOrbit) - 0.50) < 0.000001, "travel progress helper should map burn depth to destination progress");
    require(flight_progress::travelProgressForBurn(0.80, earthOrbit) == 0.0, "travel progress helper should clamp low burn depth");
    require(flight_progress::travelProgressForBurn(earthOrbit.targetMultiplier + 5.0, earthOrbit) == tuning::session::maxTravelProgress, "travel progress helper should clamp high burn depth");

    const double returnDuration = 2.4;
    require(std::abs(flight_progress::returnCompletion(1.2, returnDuration) - math::smoothStep(0.5)) < 0.000001, "return completion should use shared smooth step");
    require(std::abs(flight_progress::returnTravelProgress(0.80, 1.2, returnDuration) - 0.40) < 0.000001, "return travel helper should move the visual ship back home");

    const double startTravel = 0.35;
    const double baseDuration = tuning::session::returnBaseDuration + startTravel * tuning::session::returnDurationPerProgress;
    require(std::abs(flight_progress::returnDuration(startTravel, false) - baseDuration) < 0.000001, "return duration helper should use tuned base duration");
    require(std::abs(flight_progress::returnDuration(startTravel, true) - baseDuration * tuning::session::returnDriftDurationMultiplier) < 0.000001, "return duration helper should apply drift multiplier");
}

void launchPanelPresentationComesFromSharedHelper()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 907);
    Random rng(907);
    PreparedLaunch launch = prepareLaunch(state, catalog, rng);

    const double currentMultiplier = 1.24;
    LaunchPanelPresentation panel = launchPanelPresentation(
        state,
        catalog,
        launch,
        currentMultiplier,
        1.0,
        0.0,
        tuning::session::returnDefaultDuration,
        {},
        false);

    const PanelMetricPresentation* burn = findPanelMetric(panel.metrics, text::labels::burnDepth);
    const PanelMetricPresentation* dataGoal = findPanelMetric(panel.metrics, text::labels::dataGoal);
    const PanelMetricPresentation* returnRisk = findPanelMetric(panel.metrics, text::labels::returnRisk);
    require(panel.sectionTitle == text::panel::sections::provingFlight, "launch presentation should select proving section title");
    require(burn != nullptr && burn->value == display::multiplier(currentMultiplier), "launch presentation should expose displayed burn depth");
    require(dataGoal != nullptr && dataGoal->value == display::multiplier(catalog.destinations[0].targetMultiplier), "launch presentation should expose data goal metric");
    require(returnRisk != nullptr && returnRisk->value == display::percent(returnHomeRisk(launch, catalog, state, currentMultiplier)), "launch presentation should share return risk math");
    require(panel.telemetry.size() == telemetrySamples(telemetryAt(launch, currentMultiplier)).size(), "launch presentation should expose all telemetry channel samples");
    require(findDetailPresentationRow(panel.telemetryDetails, text::labels::returnRisk) != nullptr, "launch presentation should expose return risk in telemetry details");
    require(findDetailPresentationRow(panel.telemetryDetails, text::labels::missionDifficulty) != nullptr, "launch presentation should expose mission difficulty in telemetry details");

    const FlightActionButtonPresentation* returnHome = findFlightActionButton(panel.primaryActions, text::buttons::returnHome);
    const FlightActionButtonPresentation* eject = findFlightActionButton(panel.primaryActions, text::buttons::eject);
    const FlightActionButtonPresentation* cutEngines = findFlightActionButton(panel.systemActions, text::buttons::cutEngines);
    const FlightActionButtonPresentation* reliefValve = findFlightActionButton(panel.systemActions, text::buttons::reliefValve);
    const FlightActionButtonPresentation* jettisonCargo = findFlightActionButton(panel.systemActions, text::buttons::jettisonCargo);
    require(returnHome != nullptr && returnHome->enabled && returnHome->actionId == ui::actions::returnHome, "launch presentation should expose return-home action");
    require(eject != nullptr && eject->enabled && eject->cssClass == "danger", "launch presentation should expose eject danger action");
    require(cutEngines != nullptr && cutEngines->enabled && cutEngines->actionId == ui::actions::cutEngines, "launch presentation should expose cut-engines action");
    require(reliefValve != nullptr && reliefValve->enabled && reliefValve->actionId == ui::actions::pressureRelief, "launch presentation should expose relief-valve action");
    require(jettisonCargo != nullptr && jettisonCargo->enabled && jettisonCargo->actionId == ui::actions::jettisonCargo, "launch presentation should expose jettison-cargo action");

    FlightActionState returning;
    returning.returningHome = true;
    const double returnBurnMultiplier = 1.32;
    const double returnElapsed = 1.2;
    const double returnDuration = tuning::session::returnDefaultDuration;
    panel = launchPanelPresentation(
        state,
        catalog,
        launch,
        currentMultiplier,
        returnBurnMultiplier,
        returnElapsed,
        returnDuration,
        returning,
        false);

    burn = findPanelMetric(panel.metrics, text::labels::burnDepth);
    const PanelMetricPresentation* returnProgress = findPanelMetric(panel.metrics, text::labels::returnProgress);
    returnHome = findFlightActionButton(panel.primaryActions, text::buttons::returningHome);
    cutEngines = findFlightActionButton(panel.systemActions, text::buttons::cutEngines);
    require(panel.sectionTitle == text::panel::sections::returnBurn, "launch presentation should select return section title");
    require(burn != nullptr && burn->value == display::multiplier(returnTelemetryMultiplier(returnBurnMultiplier, launch.crashMultiplier, returnElapsed, returnDuration)), "launch presentation should share return telemetry multiplier");
    require(returnProgress != nullptr && returnProgress->value == display::percent(flight_progress::returnCompletion(returnElapsed, returnDuration)), "launch presentation should share return progress math");
    require(returnHome != nullptr && !returnHome->enabled, "returning-home action should be disabled once committed");
    require(cutEngines != nullptr && !cutEngines->enabled, "system actions should be disabled during return home");

    FlightActionState reliefOpen;
    reliefOpen.pressureReliefOpen = true;
    panel = launchPanelPresentation(state, catalog, launch, currentMultiplier, 1.0, 0.0, returnDuration, reliefOpen, true);
    const FlightActionButtonPresentation* closeValve = findFlightActionButton(panel.systemActions, text::buttons::closeValve);
    require(closeValve != nullptr && closeValve->enabled && closeValve->actionId == ui::actions::closeReliefValve, "open relief valve should expose close-valve action");

    reliefOpen.pressureReliefFailed = true;
    panel = launchPanelPresentation(state, catalog, launch, currentMultiplier, 1.0, 0.0, returnDuration, reliefOpen, true);
    const FlightActionButtonPresentation* failedValve = findFlightActionButton(panel.systemActions, text::buttons::reliefValveFailed);
    require(failedValve != nullptr && !failedValve->enabled, "failed relief valve should be disabled in presentation");
}

void launchReadinessPresentationComesFromSharedHelper()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 908);
    state.run.credits = 500.0;

    LaunchReadinessPresentation readiness = launchReadinessPresentation(state, catalog);
    const DetailPresentationRow* requiredAction = findDetailPresentationRow(readiness.details, text::panel::details::requiredAction);
    require(!readiness.blocked && !readiness.hullBlocked && !readiness.crewBlocked, "healthy vehicle and crew should clear launch readiness");
    require(readiness.messages.empty(), "clear launch readiness should not emit hold messages");
    require(readiness.actions.empty(), "clear launch readiness should not emit hold actions");
    require(requiredAction != nullptr && requiredAction->value == text::panel::details::clearForLaunch, "clear launch readiness should expose clear-for-launch detail");

    state.run.shipDamage = tuning::damage::destroyedShipDamage;
    readiness = launchReadinessPresentation(state, catalog);
    requiredAction = findDetailPresentationRow(readiness.details, text::panel::details::requiredAction);
    const FlightActionButtonPresentation* repairAction = findFlightActionButton(readiness.actions, text::buttons::assignRepairBay);
    require(readiness.blocked && readiness.hullBlocked && !readiness.crewBlocked, "destroyed hull should block launch readiness");
    require(readiness.messages.size() == 1 && readiness.messages[0] == text::panel::messages::totalHullBlocked, "hull readiness should expose hull hold message");
    require(requiredAction != nullptr && requiredAction->value == text::panel::details::repairVehicle, "hull readiness should require repair");
    require(repairAction != nullptr && repairAction->enabled && repairAction->actionId == ui::actions::repairShip, "funded hull readiness should expose repair action");

    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = CrewStatus::Dead;
    }
    state.run.credits = 0.0;
    readiness = launchReadinessPresentation(state, catalog);
    requiredAction = findDetailPresentationRow(readiness.details, text::panel::details::requiredAction);
    const HangarOperationPreview hangarOps = hangarOperationPreview(state, catalog);
    repairAction = findFlightActionButton(readiness.actions, display::needCredits(hangarOps.repairCost));
    const FlightActionButtonPresentation* recruitAction = findFlightActionButton(readiness.actions, text::buttons::recruitCrew);
    require(readiness.blocked && readiness.hullBlocked && readiness.crewBlocked, "destroyed hull and dead roster should both block launch readiness");
    require(readiness.messages.size() == 2, "combined launch hold should expose both hold messages");
    require(requiredAction != nullptr && requiredAction->value == text::panel::details::repairAndRecruitCrew, "combined launch hold should require repair and recruit");
    require(repairAction != nullptr && !repairAction->enabled, "unfunded hull readiness should expose disabled repair cost");
    require(recruitAction != nullptr && recruitAction->enabled && recruitAction->actionId == ui::actions::recruitCrew, "crew readiness should expose recruit action");
}

void panelChromePresentationComesFromSharedHelper()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 909);
    state.run.credits = 123.0;
    state.run.shipDamage = 11;
    state.run.frontierReadiness = 2;
    Astronaut* pilot = activeAstronaut(state);
    require(pilot != nullptr, "panel chrome test needs an active astronaut");
    pilot->stress = 28;

    PreparedLaunch launch;
    std::vector<PanelMetricPresentation> metrics = panelHeaderMetrics(state, catalog, launch, launch);
    const PanelMetricPresentation* credits = findPanelMetric(metrics, text::labels::missionCredits);
    const PanelMetricPresentation* hullDamage = findPanelMetric(metrics, text::labels::hullDamage);
    const PanelMetricPresentation* currentFrontier = findPanelMetric(metrics, text::labels::currentFrontier);
    const PanelMetricPresentation* flightData = findPanelMetric(metrics, text::labels::flightData);
    const PanelMetricPresentation* difficulty = findPanelMetric(metrics, text::labels::missionDifficulty);
    const PanelMetricPresentation* crewStress = findPanelMetric(metrics, text::labels::crewStress);

    require(metrics.size() == 6, "panel chrome should expose six top-level metrics");
    require(credits != nullptr && credits->value == display::money(state.run.credits), "panel chrome should format mission credits");
    require(hullDamage != nullptr && hullDamage->value == display::wholePercent(state.run.shipDamage), "panel chrome should format hull damage");
    require(currentFrontier != nullptr && currentFrontier->value == catalog.destinations[0].name, "panel chrome should show current frontier off launch");
    require(flightData != nullptr && flightData->value == display::fraction(state.run.frontierReadiness, frontierReadinessRequired(state, catalog)), "panel chrome should show readiness off launch");
    require(difficulty != nullptr && difficulty->value == display::signedPercent(missionPressureModifier(state, catalog, catalog.destinations[0])), "panel chrome should show mission difficulty off launch");
    require(crewStress != nullptr && crewStress->value == display::wholePercent(pilot->stress), "panel chrome should show active crew stress");

    state.screen = Screen::Launch;
    launch.config.frontierTransfer = true;
    launch.config.destinationId = catalog.destinations[1].id;
    launch.pressureModifier = 0.37;
    metrics = panelHeaderMetrics(state, catalog, launch, launch);
    const PanelMetricPresentation* transferTarget = findPanelMetric(metrics, text::labels::transferTarget);
    const PanelMetricPresentation* requiredBurn = findPanelMetric(metrics, text::labels::requiredBurn);
    difficulty = findPanelMetric(metrics, text::labels::missionDifficulty);
    require(transferTarget != nullptr && transferTarget->value == catalog.destinations[1].name, "panel chrome should show active transfer target on launch");
    require(requiredBurn != nullptr && requiredBurn->value == display::multiplier(catalog.destinations[1].targetMultiplier), "panel chrome should show transfer burn on launch");
    require(difficulty != nullptr && difficulty->value == display::signedPercent(launch.pressureModifier), "panel chrome should use prepared launch difficulty while flying");

    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = CrewStatus::Dead;
    }
    metrics = panelHeaderMetrics(state, catalog, launch, launch);
    crewStress = findPanelMetric(metrics, text::labels::crewStress);
    require(crewStress != nullptr && crewStress->value == text::panel::noActiveCrew, "panel chrome should handle missing active crew");

    const std::vector<DetailPresentationRow> settingsRows = settingsDetailsPresentation();
    require(findDetailPresentationRow(settingsRows, text::panel::details::keyboard) != nullptr, "settings presentation should expose keyboard row");
    require(findDetailPresentationRow(settingsRows, text::panel::details::save) != nullptr, "settings presentation should expose save row");
    require(findDetailPresentationRow(settingsRows, text::panel::details::build) != nullptr, "settings presentation should expose build row");

    const std::vector<PanelButtonPresentation> settingsActions = settingsActionPresentation();
    const FlightActionButtonPresentation* reset = findFlightActionButton(settingsActions, text::buttons::resetSave);
    require(settingsActions.size() == 1, "settings presentation should expose reset action");
    require(reset != nullptr && reset->enabled && reset->actionId == ui::actions::resetSave && reset->cssClass == "danger", "settings presentation should expose reset-save danger action");
}

void launchBalanceHelpersDrivePreparedLaunch()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 818);
    const Destination& moon = catalog.destinations[1];
    const int required = frontierReadinessRequired(state, catalog);
    state.run.frontierReadiness = required + 2;
    state.run.shipDamage = 12;
    state.launchConfig.frontierTransfer = true;
    state.launchConfig.destinationId = moon.id;
    state.launchConfig.burnGoalMultiplier = moon.targetMultiplier;

    const ModuleStats stats = aggregateShipStats(state, catalog);
    const double pressure = missionPressureModifier(state, catalog, moon);
    Random rng(818);
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);

    const int expectedOverprepared = launch_balance::overpreparedData(state.run.frontierReadiness, required);
    require(launch.overpreparedData == expectedOverprepared, "prepared launch should use shared overprepared-data math");
    require(std::abs(launch.provingPayoutBonus - launch_balance::provingPayoutBonus(expectedOverprepared, true)) < 0.000001, "transfer launch should use shared proving-payout helper");
    require(std::abs(launch.sensorQuality - launch_balance::sensorQuality(stats)) < 0.000001, "prepared launch should use shared sensor quality helper");
    require(std::abs(launch.heatRate - launch_balance::heatRate(moon, stats, launch_balance::transferHeatLoad(moon, true))) < 0.000001, "prepared launch should use shared heat-rate helper");
    require(std::abs(launch.pressureModifier - launch_balance::pressureModifier(pressure, stats)) < 0.000001, "prepared launch should use shared pressure modifier helper");
    require(launch.incidentCount == launch_balance::incidentCount(moon, true, static_cast<int>(launch.incidents.size())), "prepared launch should use shared incident-count helper");

    const double ratio = launch_balance::readinessRatio(state.run.frontierReadiness, required);
    const double transferPrep = launch_balance::transferPreparation(ratio, expectedOverprepared, true);
    const double transferRisk = launch_balance::transferHazard(moon, transferPrep, true);
    const double unprovenRisk = launch_balance::unprovenHazard(0, 0);
    const double hazard = launch_balance::launchHazard(moon, stats, state.run.shipDamage, transferRisk, unprovenRisk);
    require(hazard > 0.0, "shared launch hazard helper should expose a usable risk value");
    require(launch_balance::launchSafety(0.0, hazard) >= tuning::launch::safetyMinimum, "shared launch safety should honor tuned lower bound");
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
        const double worstNonHeat = strongestNonHeatTelemetryValue(event);
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

void uiActionsUseStableSchemaIds()
{
    require(ui::actions::startLaunch == "start_launch", "start launch action should use a stable schema id");
    require(ui::actions::returnHome == "return_home", "return action should use a stable schema id");
    require(ui::actions::resetSave == "reset_save", "settings actions should use stable schema ids");
    require(ui::modals::launchBlocked == "launch_blocked", "modal ids should stay shared and data-like");

    const std::string buyOffer = ui::actions::buyOffer(2);
    require(buyOffer == "buy_offer:2", "indexed offer actions should encode the offer index in one reusable action family");
    require(buyOffer.find("rr.") == std::string::npos, "panel action ids should not embed JavaScript snippets");
}

void contentIdsResolveAgainstDefaultCatalog()
{
    const ContentCatalog catalog = createDefaultContent();

    require(catalog.findModule(content::module::sparrowEngine) != nullptr, "starter module id should resolve");
    require(catalog.findModule(content::module::radiatorVanes) != nullptr, "cooling module id should resolve");
    require(catalog.findCrewUpgrade(content::crewUpgrade::analogSimBay) != nullptr, "crew upgrade id should resolve");
    require(catalog.findFrame(content::frame::pathfinder) != nullptr, "ship frame id should resolve");
    require(catalog.findAstronaut(content::astronaut::ava) != nullptr, "astronaut id should resolve");
    require(catalog.findDestination(content::destination::moon) != nullptr, "destination id should resolve");

    MetaProgress meta;
    require(hasUnlock(meta, content::unlock::starter), "starter unlock should stay implicit");
    meta.unlockKeys.push_back(content::unlock::thermal);
    require(hasUnlock(meta, content::unlock::thermal), "named unlock key should resolve through shared ids");
}

void displayFormatAndMathHelpersAreShared()
{
    require(display::money(34.2) == "34", "display money should use whole mission-credit values");
    require(display::signedMoney(12.0) == "+12", "signed money should show positive deltas explicitly");
    require(display::multiplier(1.456) == "x1.46", "multiplier formatting should be consistent across panels");
    require(display::percent(1.4) == "100%", "unit percent displays should clamp high values");
    require(display::signedPercent(0.25) == "+25%", "signed percent should preserve modifier sign");
    require(display::damage(12) == "12% damage", "damage summary should share wording and percent format");
    require(display::trainingWithEffective(3, -2) == "3 (-2 effective)", "crew training summary should use one formatter");
    require(display::stressWithSteps(100, 7) == "100% / 7 steps", "crew stress steps should use one formatter");
    require(display::crewStressEffects(0.15, 2.0) == "NAV +15%, ABORT x2.00", "crew stress effects should share telemetry labels");

    require(math::smoothStep(-0.5) == 0.0, "smoothStep should clamp below zero");
    require(math::smoothStep(1.5) == 1.0, "smoothStep should clamp above one");
    require(std::abs(math::smoothStep(0.5) - 0.5) < 0.000001, "smoothStep midpoint should stay stable");
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
    flightActionsComposeThroughOneCoreHelper();
    launchStatusLinesComeFromSharedSelector();
    emergencyRecruitmentPreventsDeadRosterSoftLock();
    moduleOffersAreOneChoiceRefits();
    refitRerollsSpendAndEscalate();
    crewUpgradeOffersInstallAndModifyCrewOps();
    hangarOpsStartCheapAndEscalate();
    hangarOperationPreviewMatchesCoreMath();
    hangarOperationCardsComeFromSharedPreview();
    lowCreditRefitWindowIncludesAffordableOffer();
    pressureTracksFrontierExperience();
    crewStressTracksPeakTelemetryDanger();
    pressureControlModulesReducePressureTelemetry();
    starterEarthOrbitIsProvingFirst();
    starterProvingEconomyFundsEarlyRefits();
    returnHomeRewardShelvesMatchRefitCosts();
    overburnRewardsBeatLinearScalingAfterGoal();
    saveRoundTripPreservesProgress();
    saveSchemaConstantsMatchSerializedFields();
    launchOutcomePresentationIsShared();
    enumDisplayLabelsComeFromSharedText();
    refitPresentationComesFromSharedHelper();
    refitWindowPresentationComesFromSharedHelper();
    crewDetailsPresentationComesFromSharedHelper();
    shipDetailsPresentationComesFromSharedHelper();
    programDetailsPresentationComesFromSharedHelper();
    flightProgressHelpersShareTravelAndReturnMath();
    launchPanelPresentationComesFromSharedHelper();
    launchReadinessPresentationComesFromSharedHelper();
    panelChromePresentationComesFromSharedHelper();
    launchBalanceHelpersDrivePreparedLaunch();
    destinationRiskEscalates();
    starterMoonTransferIsNotReliable();
    frontierReadinessGatesProgression();
    transferAttemptAdvancesOnlyOnSuccess();
    overpreparedReadinessRaisesProvingStakes();
    uiActionsUseStableSchemaIds();
    contentIdsResolveAgainstDefaultCatalog();
    displayFormatAndMathHelpersAreShared();

    std::cout << "rocket_core_tests passed\n";
    return 0;
}
