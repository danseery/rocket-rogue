#include "core/Content.h"
#include "core/ContentIds.h"
#include "core/CrewPresentation.h"
#include "core/FlightProgress.h"
#include "core/GameFormat.h"
#include "core/GameMath.h"
#include "core/GameState.h"
#include "core/HangarPresentation.h"
#include "core/InventoryPresentation.h"
#include "core/LaunchBalance.h"
#include "core/LaunchPresentation.h"
#include "core/LaunchReadinessPresentation.h"
#include "core/LaunchStatus.h"
#include "core/LaunchSimulation.h"
#include "core/MiningSystem.h"
#include "core/MiningPresentation.h"
#include "core/OutcomePresentation.h"
#include "core/PanelChromePresentation.h"
#include "core/ProgramPresentation.h"
#include "core/RefitPresentation.h"
#include "core/ResearchPresentation.h"
#include "core/ResearchSystem.h"
#include "core/SaveData.h"
#include "core/SaveSchema.h"
#include "core/ShipPresentation.h"
#include "core/Telemetry.h"
#include "core/Tuning.h"
#include "core/GameUi.h"
#include "game/GamePanel.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
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
const DetailPresentationRow* findDetailPresentationRow(const std::vector<DetailPresentationRow>& rows, std::string_view label);
bool hasDetailPresentationHeader(const std::vector<DetailPresentationRow>& rows, std::string_view label);
bool hasRefitChip(const RefitPresentation& presentation, std::string_view label, std::string_view value, bool positive);

std::size_t countOccurrences(std::string_view text, std::string_view needle)
{
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string_view::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

void activateOnlyCrew(GameState& state, std::string_view id)
{
    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = astronaut.id == id ? CrewStatus::Active : CrewStatus::Dead;
    }
}

void prepareMiningSiteForTest(GameState& state)
{
    state.run.surfaceExpedition.miningSitePrepared = true;
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
    require(doomed.shipDamage == tuning::damage::destroyedShipDamage, "destroyed launch should total the ship");
}

void shallowRecoveryCheeseEscalatesAndThenFails()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = configuredState(catalog, 0, catalog.destinations[0].targetMultiplier);
    const double shallowBurn = 1.0 + (catalog.destinations[0].targetMultiplier - 1.0) * 0.20;

    Random firstRng(77);
    const PreparedLaunch firstLaunch = prepareLaunch(state, catalog, firstRng);
    const LaunchOutcome first = resolveLaunch(firstLaunch, catalog, state, shallowBurn, RecoveryMethod::ReturnHome, firstRng);
    require(first.type != LaunchResultType::Destroyed, "first shallow clean return should survive");
    require(first.recoveryCost >= tuning::rewards::shallowRecoveryPenaltyBase - 0.001, "first shallow return should include base penalty");
    applyLaunchOutcome(state, catalog, first);
    require(state.run.shallowRecoveryStreak == 1, "first shallow return should start shallow recovery streak");
    require(state.run.cleanShallowRecoveryStreak == 1, "clean shallow return should start clean streak");

    Random secondRng(78);
    const PreparedLaunch secondLaunch = prepareLaunch(state, catalog, secondRng);
    const LaunchOutcome second = resolveLaunch(secondLaunch, catalog, state, shallowBurn, RecoveryMethod::ReturnHome, secondRng);
    require(second.type != LaunchResultType::Destroyed, "second shallow clean return should still survive");
    require(second.recoveryCost - first.recoveryCost >= tuning::rewards::shallowRecoveryPenaltyBase - 0.001, "second shallow return should double the cheese penalty");
    applyLaunchOutcome(state, catalog, second);
    require(state.run.cleanShallowRecoveryStreak == 2, "second clean shallow return should arm the cheese detector");

    Random thirdRng(79);
    const PreparedLaunch thirdLaunch = prepareLaunch(state, catalog, thirdRng);
    const LaunchOutcome third = resolveLaunch(thirdLaunch, catalog, state, shallowBurn, RecoveryMethod::ReturnHome, thirdRng);
    require(third.type == LaunchResultType::Destroyed, "third clean shallow return should destroy the vehicle");
    applyLaunchOutcome(state, catalog, third);
    require(state.statusLine == std::string(text::status::cleanShallowRecoveryDestroyed), "cheese destruction should explain what happened");
    require(state.run.shallowRecoveryStreak == 0 && state.run.cleanShallowRecoveryStreak == 0, "destruction should reset cheese counters");
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
    require(std::abs(boostedDelta - unscaledDelta * tuning::launch::baseTravelSpeedMultiplier) < 0.000001, "ship travel should apply the tuned base travel speed multiplier");

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

    context.arkKnown = true;
    require(launchStatusLine(context).find("Ark") != std::string::npos, "Ark drift return should name the Ark as the return destination");

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

void emergencyRecruitmentOffersAnimalCandidateChoice()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 102);
    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = CrewStatus::Dead;
    }
    state.run.credits = 0.0;
    syncLaunchConfig(state, catalog);

    const std::vector<const Astronaut*> candidates = recruitCandidateTemplates(state, catalog);
    require(candidates.size() == 3, "pilot intake should offer three candidate cards");
    require(candidates[0]->background.find(" - ") != std::string::npos, "candidate card should expose animal class and focus");
    const std::string pickedName = candidates[1]->name;
    const std::string pickedClass = candidates[1]->background;

    require(recruitCrew(state, catalog, 1), "indexed pilot intake should recruit the chosen card");
    const Astronaut* recruited = activeAstronaut(state);
    require(recruited != nullptr && recruited->name == pickedName, "indexed recruitment should preserve the selected animal pilot");
    require(recruited->background == pickedClass, "indexed recruitment should preserve the animal class text");
    require(state.run.credits == 0.0, "emergency pilot intake should remain free");
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
        require(std::find(state.meta.ownedModuleIds.begin(), state.meta.ownedModuleIds.end(), moduleId) != state.meta.ownedModuleIds.end(), "installed module should enter permanent shipyard inventory");
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

void specialShipComponentsRequireRecoveredMaterials()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 434);
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.run.credits = 200.0;
    state.meta.materials = {.common = 2};
    state.run.offerModuleIds = {content::module::deepReservoir, "", ""};
    state.run.offerCrewUpgradeIds = {};

    const ShipModule* module = catalog.findModule(content::module::deepReservoir);
    require(module != nullptr, "special component test needs deep reservoir module");
    require(module->materialCost.common == 2 && module->materialCost.rare == 1, "deep reservoir should require recovered materials");
    require(!canAffordModuleOffer(state, *module), "special ship components should check material affordability");
    require(!buyOffer(state, catalog, 0), "buying without required materials should fail");
    require(state.run.credits == 200.0, "failed material-gated refit should not spend credits");

    const RefitWindowPresentation blocked = refitWindowPresentation(state, catalog);
    require(!blocked.offers.empty(), "material-gated refit should still present the offer");
    require(!blocked.offers.front().affordable, "material-gated offer should expose unaffordable state");
    require(blocked.offers.front().action.label == std::string(text::panel::needMaterials), "material-gated offer should explain missing materials");
    require(blocked.offers.front().costSummary.find("rare") != std::string::npos, "material-gated offer should show material cost");

    state.meta.materials.rare = 1;
    require(canAffordModuleOffer(state, *module), "adding recovered materials should satisfy special component cost");
    require(buyOffer(state, catalog, 0), "buying with credits and materials should succeed");
    require(state.meta.materials.common == 0 && state.meta.materials.rare == 0, "buying special component should spend recovered materials");
    require(std::find(state.run.inventoryModuleIds.begin(), state.run.inventoryModuleIds.end(), content::module::deepReservoir) != state.run.inventoryModuleIds.end(), "bought special component should enter inventory");
    require(std::find(state.meta.ownedModuleIds.begin(), state.meta.ownedModuleIds.end(), content::module::deepReservoir) != state.meta.ownedModuleIds.end(), "bought special component should enter permanent shipyard inventory");
}

void preMiningRefitOffersAvoidMaterialCosts()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 441);
    state.run.credits = 400.0;
    state.meta.furthestTier = 1;
    state.meta.unlockKeys = {
        content::unlock::starter,
        content::unlock::thermal,
        content::unlock::recovery,
        content::unlock::deepSpace,
        content::unlock::ai,
        content::unlock::exotic
    };
    for (const ShipModule& module : catalog.modules) {
        if (module.materialCost.common == 0 && module.materialCost.rare == 0 && module.materialCost.exotic == 0) {
            state.meta.ownedModuleIds.push_back(module.id);
        }
    }

    Random rng(4410);
    generateModuleOffers(state, catalog, rng);

    bool sawOffer = false;
    for (const std::string& moduleId : state.run.offerModuleIds) {
        if (moduleId.empty()) {
            continue;
        }
        const ShipModule* module = catalog.findModule(moduleId);
        require(module != nullptr, "generated module offer should resolve");
        sawOffer = true;
        require(module->materialCost.common == 0 && module->materialCost.rare == 0 && module->materialCost.exotic == 0, "pre-mining refit offers should not require materials");
    }
    for (const std::string& upgradeId : state.run.offerCrewUpgradeIds) {
        if (!upgradeId.empty()) {
            sawOffer = true;
        }
    }
    require(sawOffer, "pre-mining refits should still offer an installable path");
}

void inventoryPresentationSummarizesResourcesAndPayload()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 516);
    state.run.credits = 321.0;
    state.meta.blueprintProgress = 7;
    state.meta.materials = {.common = 5, .rare = 2, .exotic = 1};
    state.meta.artifacts.push_back({"mars_artifact_1", content::destination::mars, true});
    state.meta.ownedModuleIds.push_back(content::module::cryoLoop);
    InventoryPresentation lockedInventory = inventoryPresentation(state, catalog);
    require(lockedInventory.sideSections.empty(), "locked inventory should not expose the side drone bay");

    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.droneBaySlots = 1;
    state.meta.ownedDroneIds.push_back(content::drone::miningDrone);
    state.meta.equippedDroneIds.push_back(content::drone::miningDrone);
    state.run.mining.active = true;
    state.run.mining.temporaryMaterials = {.common = 3, .rare = 1};
    state.run.mining.temporaryArtifacts.push_back({"moon_artifact_1", content::destination::moon, false});

    const InventoryPresentation inventory = inventoryPresentation(state, catalog);
    require(!inventory.summary.empty(), "inventory should expose a summary strip");
    require(inventory.summary[0].value == "321 credits", "inventory should summarize mission credits");

    const auto hasSection = [&](std::string_view title) {
        return std::find_if(inventory.sections.begin(), inventory.sections.end(), [&](const InventorySectionPresentation& section) {
            return section.title == title;
        }) != inventory.sections.end();
    };
    require(hasSection("Current payload"), "active mining should expose temporary payload inventory");
    require(hasSection("Recovered resources"), "inventory should expose banked materials");
    require(hasSection("Artifacts"), "inventory should expose artifacts");
    require(hasSection("Ship tech"), "inventory should expose permanent ship tech");
    require(!inventory.sideSections.empty(), "unlocked inventory should expose the side drone bay");
    require(inventory.sideSections.front().title == "Drone bay", "side inventory should label the drone bay");
    require(inventory.sideSections.front().items.size() == 6, "drone bay should show all six slot positions");
}

void shipModuleProgressSurvivesDestroyedVehicles()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 435);
    state.meta.unlockKeys.push_back(content::unlock::thermal);
    state.run.credits = 200.0;
    state.run.offerModuleIds = {content::module::cryoLoop, "", ""};
    state.run.offerCrewUpgradeIds = {};

    require(buyOffer(state, catalog, 0), "buying a thermal ship module should succeed");
    require(std::find(state.meta.ownedModuleIds.begin(), state.meta.ownedModuleIds.end(), content::module::cryoLoop) != state.meta.ownedModuleIds.end(), "ship upgrades should become permanent shipyard tech");
    require(std::find(state.meta.defaultEquippedModuleIds.begin(), state.meta.defaultEquippedModuleIds.end(), content::module::cryoLoop) != state.meta.defaultEquippedModuleIds.end(), "installed ship upgrades should become the default new-build loadout");

    LaunchOutcome destroyed;
    destroyed.type = LaunchResultType::Destroyed;
    destroyed.destinationId = currentDestination(state, catalog).id;
    destroyed.moduleDestroyedId = content::module::cryoLoop;
    destroyed.shipDamage = tuning::damage::destroyedShipDamage;
    applyLaunchOutcome(state, catalog, destroyed);
    startNewExpedition(state, catalog);

    require(std::find(state.meta.ownedModuleIds.begin(), state.meta.ownedModuleIds.end(), content::module::cryoLoop) != state.meta.ownedModuleIds.end(), "ship destruction should not erase permanent shipyard tech");
    require(std::find(state.run.inventoryModuleIds.begin(), state.run.inventoryModuleIds.end(), content::module::cryoLoop) != state.run.inventoryModuleIds.end(), "replacement ships should inherit permanent shipyard inventory");
    require(std::find(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), content::module::cryoLoop) != state.run.equippedModuleIds.end(), "replacement ships should keep the improved default loadout");
}

void destroyedTransferAttemptCostsOneProvingFlight()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 468);
    state.run.frontierReadiness = frontierReadinessRequired(state, catalog);
    const Destination* next = nextDestination(state, catalog);
    require(next != nullptr, "starter route should have a transfer target");

    LaunchOutcome destroyed;
    destroyed.type = LaunchResultType::Destroyed;
    destroyed.destinationId = next->id;
    destroyed.frontierTransfer = true;
    destroyed.shipDamage = tuning::damage::destroyedShipDamage;

    applyLaunchOutcome(state, catalog, destroyed);

    require(state.run.frontierReadiness == frontierReadinessRequired(state, catalog) - 1, "destroyed transfer attempts should cost one proving flight, not all flight data");
}

void deadCrewLosesTraining()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 457);
    Astronaut* pilot = activeAstronaut(state);
    require(pilot != nullptr, "starter pilot should exist");
    pilot->training = 7;

    LaunchOutcome destroyed;
    destroyed.type = LaunchResultType::Destroyed;
    destroyed.destinationId = currentDestination(state, catalog).id;
    destroyed.shipDamage = tuning::damage::destroyedShipDamage;
    destroyed.crewKilled = true;
    applyLaunchOutcome(state, catalog, destroyed);

    require(!state.run.crew.empty(), "dead pilot should remain in the memorial roster");
    require(state.run.crew.front().status == CrewStatus::Dead, "pilot should be marked dead");
    require(state.run.crew.front().training == 0, "dead pilot training should reset");
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
    state.meta.ownedModuleIds = state.run.inventoryModuleIds;

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
    require(pilot->stress == 45, "upgraded simulator should reduce training stress gain without eliminating stress");
    require(state.run.credits < creditsBeforeTraining, "training should still cost credits");
    require(crewTrainingStressGain(state, catalog) >= tuning::crew::stressPerStep, "crew training should always carry at least one stress step");
    pilot->training = tuning::crew::maxTraining;
    pilot->stress = 0;
    require(!trainCrew(state, catalog), "crew training should be blocked when no training benefit remains");
    pilot->training = 3;
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

void medicalRestEscalationResetsAfterSurvivedMission()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 809);
    state.run.credits = 300.0;
    Astronaut* pilot = activeAstronaut(state);
    require(pilot != nullptr, "medical rest reset test needs a pilot");
    pilot->stress = 80;

    const double baseRestCost = crewRestCost(state, catalog);
    require(restCrew(state, catalog), "first medical rest should succeed");
    pilot->stress = 80;
    require(crewRestCost(state, catalog) > baseRestCost, "medical rest should escalate before a flight");

    LaunchOutcome outcome;
    outcome.type = LaunchResultType::SafeEject;
    outcome.recoveryMethod = RecoveryMethod::ReturnHome;
    outcome.destinationId = catalog.destinations.front().id;
    outcome.ejectMultiplier = 1.3;
    applyLaunchOutcome(state, catalog, outcome);

    pilot = activeAstronaut(state);
    require(pilot != nullptr, "pilot should survive the recovered mission");
    pilot->stress = 80;
    require(state.run.restOpsThisExpedition == 0, "survived missions should reset medical rest escalation");
    require(std::abs(crewRestCost(state, catalog) - baseRestCost) < 0.001, "medical rest should return to base cost after a survived mission");
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

    pilot->training = tuning::crew::maxTraining;
    preview = hangarOperationPreview(state, catalog);
    require(!preview.trainingAvailable, "hangar preview should block training when the pilot is capped");
    pilot->training = 0;

    pilot->stress = tuning::crew::maxStress;
    preview = hangarOperationPreview(state, catalog);
    require(!preview.trainingAvailable, "hangar preview should block training at max stress");

    pilot->stress = 0;
    pilot->status = CrewStatus::Active;
    preview = hangarOperationPreview(state, catalog);
    require(!preview.restNeeded, "hangar preview should not need medical rest for a healthy calm crew");
    require(!preview.restAvailable, "hangar preview should block medical rest when there is no benefit");
    require(!restCrew(state, catalog), "medical rest should not spend credits on a healthy calm crew");
    require(state.statusLine == std::string(text::status::noRestNeeded), "medical rest should explain when no rest is needed");

    pilot->status = CrewStatus::Injured;
    preview = hangarOperationPreview(state, catalog);
    require(preview.restNeeded && preview.restAvailable, "injured crew should still be eligible for medical rest at zero stress");

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

    pilot->stress = 0;
    pilot->status = CrewStatus::Active;
    cards = hangarOperationCards(state, catalog);
    rest = findHangarOperationCard(cards, text::panel::ops::medicalRest);
    require(rest != nullptr && rest->detail == std::string(text::panel::noRestDetail), "rest card should explain when no medical rest is useful");
    require(rest != nullptr && rest->cost == std::string(text::panel::crewRested), "rest card should not show a payable rest cost when crew is already rested");
    require(rest != nullptr && !rest->available, "rest card should be disabled when crew is already rested");

    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = CrewStatus::Dead;
    }
    state.run.credits = 0.0;
    cards = hangarOperationCards(state, catalog);
    require(cards.size() == 2, "no-pilot hangar should show repair and one pilot intake card");
    const HangarOperationCardPresentation* crewIntake = findHangarOperationCard(cards, text::panel::ops::crewIntake);
    require(crewIntake != nullptr && crewIntake->detail == std::string(text::panel::messages::emergencyReplacement), "crew intake card should use emergency copy");
    require(crewIntake != nullptr && crewIntake->cost == display::credits(tuning::hangar::emergencyRecruitCost), "crew intake card should use emergency recruit cost");
    require(crewIntake != nullptr && crewIntake->available, "free emergency crew intake should be available");
}

void totaledShipCanAlwaysReachSalvageRepair()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 820);
    state.run.shipDamage = tuning::damage::destroyedShipDamage;
    state.run.credits = 5.0;

    const int repaired = repairShipAmount(state);
    const HangarOperationPreview preview = hangarOperationPreview(state, catalog);
    require(repaired == tuning::hangar::repairAmountCap, "salvage rebuild should still use the repair bay cap");
    require(preview.repairAvailable, "totaled ships should always have a repair path even when broke");
    require(preview.repairCost == 5.0, "salvage rebuild should consume remaining credits instead of blocking");

    const std::vector<HangarOperationCardPresentation> cards = hangarOperationCards(state, catalog);
    const HangarOperationCardPresentation* repair = findHangarOperationCard(cards, text::panel::ops::repairBay);
    require(repair != nullptr && repair->detail == text::panel::salvageRebuildDetail(repaired), "repair card should explain salvage rebuilds");
    require(repair != nullptr && repair->cost == std::string(text::panel::messages::salvageRebuildCost), "repair card should not present salvage as a normal invoice");

    require(repairShip(state), "salvage rebuild should repair a totaled ship");
    require(state.run.credits == 0.0, "salvage rebuild should consume remaining credits");
    require(state.run.shipDamage == tuning::damage::destroyedShipDamage - repaired, "salvage rebuild should make the ship launchable but damaged");
    require(state.statusLine == text::salvagedHull(repaired), "salvage rebuild should use first-class status copy");
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

void researchPhasesUnlockOnlyAfterMarsArrival()
{
    const ContentCatalog catalog = createDefaultContent();

    LaunchOutcome moonArrival;
    moonArrival.type = LaunchResultType::MissionComplete;
    moonArrival.frontierTransfer = true;
    moonArrival.destinationId = content::destination::moon;
    require(!shouldOpenPostArrivalPhases(moonArrival, catalog), "Moon arrival should not open the Mars research loop yet");

    LaunchOutcome marsArrival = moonArrival;
    marsArrival.destinationId = content::destination::mars;
    require(shouldOpenPostArrivalPhases(marsArrival, catalog), "Mars arrival should open post-arrival research and surface phases");
    const std::vector<PhaseStepPresentation> arrivalSteps = postArrivalPhaseSteps(Screen::Results);
    require(arrivalSteps.size() == 4, "arrival result should expose the full post-arrival phase track");
    require(arrivalSteps[0].label == std::string(text::panel::details::arrivalPhase), "arrival phase track should start with arrival");
    require(arrivalSteps[0].stateLabel == "Now" && arrivalSteps[0].stateClass == "active", "arrival phase track should mark arrival active on results");
    require(arrivalSteps[1].label == std::string(text::panel::details::researchPhase) && arrivalSteps[1].stateLabel == "Next", "arrival phase track should stage research next");
    const PhaseBriefingPresentation arrivalBriefing = postArrivalPhaseBriefing(Screen::Results);
    require(arrivalBriefing.title == std::string(text::panel::modals::arrivalBriefing), "arrival results should expose an arrival briefing");
    require(findDetailPresentationRow(arrivalBriefing.rows, text::panel::details::phaseIntent) != nullptr, "arrival briefing should explain phase intent");
    require(findDetailPresentationRow(arrivalBriefing.rows, text::panel::details::phaseNext) != nullptr, "arrival briefing should explain the research handoff");
}

void arrivalOperationsGateMoonButAllowMarsRisk()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 607);

    LaunchOutcome moonArrival;
    moonArrival.type = LaunchResultType::MissionComplete;
    moonArrival.frontierTransfer = true;
    moonArrival.destinationId = content::destination::moon;
    require(shouldOpenArrivalOps(moonArrival, catalog), "Moon transfer arrival should open arrival operations");

    LaunchOutcome outerPlanetsArrival;
    outerPlanetsArrival.type = LaunchResultType::MissionComplete;
    outerPlanetsArrival.frontierTransfer = true;
    outerPlanetsArrival.destinationId = content::destination::outerPlanets;
    require(shouldOpenArrivalOps(outerPlanetsArrival, catalog), "Outer Planets transfer arrival should open arrival operations");

    startArrivalOps(state, moonArrival);
    require(canRunArrivalFlyby(state, catalog), "Moon flyby should always be available after arrival");
    require(!canEnterArrivalOrbit(state, catalog), "Moon orbit should require a prior flyby");
    require(!canAttemptArrivalLanding(state, catalog), "Moon landing should require flyby and orbit clearance");
    require(arrivalOperationBlockReason(state, catalog, "landing") == std::string(text::status::moonFlybyRequired), "Moon landing should explain missing flyby");

    completeArrivalFlyby(state, catalog);
    startArrivalOps(state, moonArrival);
    require(canEnterArrivalOrbit(state, catalog), "Moon orbit should unlock after a flyby");
    require(!canAttemptArrivalLanding(state, catalog), "Moon landing should still require orbit");
    require(arrivalOperationBlockReason(state, catalog, "landing") == std::string(text::status::moonOrbitRequired), "Moon landing should explain missing orbit");

    completeArrivalOrbit(state, catalog);
    startArrivalOps(state, moonArrival);
    require(canAttemptArrivalLanding(state, catalog), "Moon landing should unlock after flyby and orbit");
    require(state.run.frontierReadiness == 0, "Moon frontier should start with no Mars flight data");
    require(bankArrivalLandingFlightData(state, catalog), "first cleared Moon landing should bank one flight-data rep");
    require(state.run.frontierReadiness == 1, "Moon landing should count as flight data toward Mars");

    LaunchOutcome marsArrival = moonArrival;
    marsArrival.destinationId = content::destination::mars;
    GameState mars = createNewGame(catalog, 608);
    startArrivalOps(mars, marsArrival);
    require(canAttemptArrivalLanding(mars, catalog), "Mars landing should allow a high-risk YOLO descent without prior recon");
    const Destination* marsDestination = catalog.findDestination(content::destination::mars);
    require(marsDestination != nullptr, "Mars destination should resolve");
    const double expectedNoReconPenalty = 0.20;
    startSurfaceExpedition(mars, catalog);
    require(mars.run.surfaceExpedition.active, "Mars YOLO landing should start surface operations");
    require(mars.run.surfaceExpedition.hazard >= tuning::research::baseHazard + marsDestination->tier * tuning::research::hazardPerTier + expectedNoReconPenalty - 0.001, "YOLO landing should carry extra surface hazard");
}

void arrivalFlybyMinigameRewardsProgressionAndSlingshot()
{
    const ContentCatalog catalog = createDefaultContent();
    LaunchOutcome moonArrival;
    moonArrival.type = LaunchResultType::MissionComplete;
    moonArrival.frontierTransfer = true;
    moonArrival.destinationId = content::destination::moon;

    GameState miss = createNewGame(catalog, 701);
    startArrivalOps(miss, moonArrival);
    startArrivalFlybyRun(miss, catalog);
    require(miss.screen == Screen::Flyby && miss.run.flyby.active, "starting arrival flyby should open the flyby minigame");
    require(miss.run.flyby.currentZone >= 1, "flyby should begin inside the single-pass corridor");
    require(miss.run.flyby.gravityStrength <= tuning::flyby::gravityEasy + 0.001, "Moon flyby should use easy gravity");
    Random flybyPanelRng(701);
    const PreparedLaunch flybyPanelLaunch = prepareLaunch(miss, catalog, flybyPanelRng);
    const std::string flybyPanelHtml = buildGamePanelHtml({miss, catalog, flybyPanelLaunch, flybyPanelLaunch});
    require(flybyPanelHtml.find("Speed") != std::string::npos && flybyPanelHtml.find(" m/s") != std::string::npos, "flyby panel should expose ship speed as a readout");
    const int missFlybysBefore = destinationHistoryValue(miss.meta.destinationFlybys, catalog, content::destination::moon);
    const int missBlueprintsBefore = miss.meta.blueprintProgress;
    const double missCreditsBefore = miss.run.credits;
    miss.run.flyby.completed = true;
    miss.run.flyby.result = FlybyGrade::Miss;
    completeFlybyRun(miss, catalog);
    require(miss.screen == Screen::ArrivalOps, "missed flyby should return to approach options");
    require(destinationHistoryValue(miss.meta.destinationFlybys, catalog, content::destination::moon) == missFlybysBefore, "missed flyby should not unlock Moon flyby history");
    require(miss.meta.blueprintProgress == missBlueprintsBefore, "missed flyby should not grant blueprint progress");
    require(std::abs(miss.run.credits - missCreditsBefore) < 0.001, "missed flyby should not grant credits");
    require(miss.meta.totalFlybyMisses == 1, "missed flyby should be counted for future achievement stats");

    GameState good = createNewGame(catalog, 702);
    startArrivalOps(good, moonArrival);
    startArrivalFlybyRun(good, catalog);
    const int goodFlybysBefore = destinationHistoryValue(good.meta.destinationFlybys, catalog, content::destination::moon);
    const int goodBlueprintsBefore = good.meta.blueprintProgress;
    const double goodCreditsBefore = good.run.credits;
    good.run.flyby.completed = true;
    good.run.flyby.result = FlybyGrade::Good;
    good.run.flyby.elapsedSeconds = tuning::flyby::durationSeconds - 1.0;
    completeFlybyRun(good, catalog);
    require(destinationHistoryValue(good.meta.destinationFlybys, catalog, content::destination::moon) == goodFlybysBefore + 1, "good flyby should bank destination flyby history");
    require(good.meta.blueprintProgress == goodBlueprintsBefore + tuning::flyby::goodBlueprintGain, "good flyby should grant blueprint progress");
    require(good.run.credits > goodCreditsBefore, "good flyby should grant credits");
    require(good.run.nextLaunchFuelBoost == 0.0 && good.run.nextLaunchSpeedBoost == 0.0, "good flyby should not grant slingshot boosts");
    require(good.meta.totalFlybyGoods == 1, "good flyby should be counted for future achievement stats");

    GameState perfect = createNewGame(catalog, 703);
    startArrivalOps(perfect, moonArrival);
    startArrivalFlybyRun(perfect, catalog);
    perfect.run.flyby.completed = true;
    perfect.run.flyby.result = FlybyGrade::Perfect;
    perfect.run.flyby.elapsedSeconds = tuning::flyby::durationSeconds - 1.0;
    completeFlybyRun(perfect, catalog);
    require(perfect.run.nextLaunchFuelBoost >= tuning::flyby::slingshotFuelBoost - 0.001, "perfect flyby should grant next-launch fuel boost");
    require(perfect.run.nextLaunchSpeedBoost >= tuning::flyby::slingshotSpeedBoost - 0.001, "perfect flyby should grant next-launch speed boost");
    require(perfect.meta.totalFlybyPerfects == 1, "perfect flyby should be counted for future achievement stats");
    const double baselineFuelBoost = perfect.run.nextLaunchFuelBoost;
    const double baselineSpeedBoost = perfect.run.nextLaunchSpeedBoost;
    const double baselinePerfectReward = perfect.run.credits;
    Random rng(704);
    const PreparedLaunch launch = prepareLaunch(perfect, catalog, rng);
    require(launch.slingshotFuelBoost >= tuning::flyby::slingshotFuelBoost - 0.001, "prepared launch should include pending slingshot fuel");
    require(launch.slingshotSpeedBoost >= tuning::flyby::slingshotSpeedBoost - 0.001, "prepared launch should include pending slingshot speed");

    GameState fastPerfect = createNewGame(catalog, 711);
    startArrivalOps(fastPerfect, moonArrival);
    startArrivalFlybyRun(fastPerfect, catalog);
    fastPerfect.run.flyby.completed = true;
    fastPerfect.run.flyby.result = FlybyGrade::Perfect;
    fastPerfect.run.flyby.elapsedSeconds = tuning::flyby::minimumFinishSeconds;
    fastPerfect.run.flyby.velocityX = tuning::flyby::maxSpeed;
    fastPerfect.run.flyby.velocityY = 0.0;
    completeFlybyRun(fastPerfect, catalog);
    require(fastPerfect.run.nextLaunchFuelBoost > baselineFuelBoost, "faster perfect flyby should grant a larger fuel slingshot bonus");
    require(fastPerfect.run.nextLaunchSpeedBoost > baselineSpeedBoost, "faster perfect flyby should grant a larger speed slingshot bonus");
    require(fastPerfect.run.credits > baselinePerfectReward, "faster perfect flyby should also amplify the credit reward");

    SaveData save = captureSaveData(fastPerfect);
    const std::optional<SaveData> restoredSave = deserializeSaveData(serializeSaveData(save));
    require(restoredSave.has_value(), "flyby stat counters should serialize");
    GameState restored = createNewGame(catalog, 717);
    restoreSaveData(restored, catalog, *restoredSave);
    require(restored.meta.totalFlybyPerfects == fastPerfect.meta.totalFlybyPerfects, "perfect flyby totals should survive save roundtrip");

    GameState fastGate = createNewGame(catalog, 712);
    startArrivalOps(fastGate, moonArrival);
    startArrivalFlybyRun(fastGate, catalog);
    const auto cubicPoint = [](double a, double b, double c, double d, double t) {
        const double u = 1.0 - t;
        return u * u * u * a + 3.0 * u * u * t * b + 3.0 * u * t * t * c + t * t * t * d;
    };
    fastGate.run.flyby.gravityStrength = 0.0;
    fastGate.run.flyby.elapsedSeconds = tuning::flyby::minimumFinishSeconds;
    fastGate.run.flyby.shipX = cubicPoint(tuning::flyby::startX, tuning::flyby::control1X, tuning::flyby::control2X, tuning::flyby::endX, 0.96);
    fastGate.run.flyby.shipY = cubicPoint(tuning::flyby::startY, tuning::flyby::control1Y, tuning::flyby::control2Y, tuning::flyby::endY, 0.96);
    const double gateDx = tuning::flyby::endX - fastGate.run.flyby.shipX;
    const double gateDy = tuning::flyby::endY - fastGate.run.flyby.shipY;
    const double gateDistance = std::hypot(gateDx, gateDy);
    fastGate.run.flyby.velocityX = gateDx / gateDistance * tuning::flyby::maxSpeed;
    fastGate.run.flyby.velocityY = gateDy / gateDistance * tuning::flyby::maxSpeed;
    updateFlybyRun(fastGate, tuning::launch::maxFrameStepSeconds);
    require(fastGate.run.flyby.completed, "fast flyby crossing the exit gate should complete in the crossing frame");
    require(fastGate.run.flyby.result == FlybyGrade::Perfect, "fast flyby should score the swept exit gate instead of missing after overshooting the sample");
    require(std::hypot(fastGate.run.flyby.velocityX, fastGate.run.flyby.velocityY) < 0.001, "flyby should physically stop the shuttle when the exit gate is reached");

    GameState fastOvershoot = createNewGame(catalog, 713);
    startArrivalOps(fastOvershoot, moonArrival);
    startArrivalFlybyRun(fastOvershoot, catalog);
    fastOvershoot.run.flyby.gravityStrength = 0.0;
    fastOvershoot.run.flyby.elapsedSeconds = tuning::flyby::minimumFinishSeconds;
    fastOvershoot.run.flyby.shipX = cubicPoint(tuning::flyby::startX, tuning::flyby::control1X, tuning::flyby::control2X, tuning::flyby::endX, 0.98);
    fastOvershoot.run.flyby.shipY = cubicPoint(tuning::flyby::startY, tuning::flyby::control1Y, tuning::flyby::control2Y, tuning::flyby::endY, 0.98);
    const double overshootDx = tuning::flyby::endX - fastOvershoot.run.flyby.shipX;
    const double overshootDy = tuning::flyby::endY - fastOvershoot.run.flyby.shipY;
    const double overshootDistance = std::hypot(overshootDx, overshootDy);
    fastOvershoot.run.flyby.velocityX = overshootDx / overshootDistance * tuning::flyby::maxSpeed;
    fastOvershoot.run.flyby.velocityY = overshootDy / overshootDistance * tuning::flyby::maxSpeed;
    updateFlybyRun(fastOvershoot, tuning::launch::maxFrameStepSeconds);
    require(fastOvershoot.run.flyby.completed, "max-speed flyby should complete even when it overshoots beyond the exit gate in one frame");
    require(fastOvershoot.run.flyby.result == FlybyGrade::Perfect, "max-speed flyby should grade the gate crossing, not post-finish overshoot");
    require(fastOvershoot.run.flyby.worstZone >= 2, "post-finish overshoot should not degrade a perfect gate crossing");

    GameState visibleGate = createNewGame(catalog, 715);
    startArrivalOps(visibleGate, moonArrival);
    startArrivalFlybyRun(visibleGate, catalog);
    visibleGate.run.flyby.gravityStrength = 0.0;
    visibleGate.run.flyby.elapsedSeconds = tuning::flyby::minimumFinishSeconds;
    const double finishDx = 3.0 * (tuning::flyby::endX - tuning::flyby::control2X);
    const double finishDy = 3.0 * (tuning::flyby::endY - tuning::flyby::control2Y);
    const double finishLength = std::hypot(finishDx, finishDy);
    const double finishTangentX = finishDx / finishLength;
    const double finishTangentY = finishDy / finishLength;
    visibleGate.run.flyby.shipX = tuning::flyby::endX - finishTangentX * (tuning::flyby::shipColliderHalfLength + 0.02);
    visibleGate.run.flyby.shipY = tuning::flyby::endY - finishTangentY * (tuning::flyby::shipColliderHalfLength + 0.02);
    visibleGate.run.flyby.velocityX = finishTangentX * tuning::flyby::maxSpeed;
    visibleGate.run.flyby.velocityY = finishTangentY * tuning::flyby::maxSpeed;
    visibleGate.run.flyby.pathProgress = tuning::flyby::finishProgress - 0.02;
    visibleGate.run.flyby.currentZone = 2;
    visibleGate.run.flyby.worstZone = 2;
    updateFlybyRun(visibleGate, tuning::launch::maxFrameStepSeconds);
    require(visibleGate.run.flyby.completed, "crossing the visible finish line at max speed should finish the flyby");
    require(visibleGate.run.flyby.result == FlybyGrade::Perfect, "crossing the finish line while still perfect should award perfect");

    FlybyRunState strictMiss = fastGate.run.flyby;
    strictMiss.result = FlybyGrade::Active;
    strictMiss.worstZone = 0;
    require(flybyGrade(strictMiss) == FlybyGrade::Miss, "touching the miss zone should make the whole flyby a miss");

    GameState instantMiss = createNewGame(catalog, 716);
    startArrivalOps(instantMiss, moonArrival);
    startArrivalFlybyRun(instantMiss, catalog);
    instantMiss.run.flyby.gravityStrength = 0.0;
    instantMiss.run.flyby.shipX = tuning::flyby::startX - tuning::flyby::goodBand * 2.8;
    instantMiss.run.flyby.shipY = tuning::flyby::startY + tuning::flyby::goodBand * 3.0;
    instantMiss.run.flyby.velocityX = tuning::flyby::startVelocityX;
    instantMiss.run.flyby.velocityY = tuning::flyby::startVelocityY;
    updateFlybyRun(instantMiss, 0.05);
    require(instantMiss.run.flyby.completed, "leaving the good corridor should end the flyby immediately");
    require(instantMiss.run.flyby.result == FlybyGrade::Miss, "leaving the good corridor should immediately grade as a miss");

    FlybyRunState strictGood = fastGate.run.flyby;
    strictGood.result = FlybyGrade::Active;
    strictGood.worstZone = 1;
    require(flybyGrade(strictGood) == FlybyGrade::Good, "leaving perfect but staying inside the good corridor should grade as good");

    FlybyRunState unfinished = fastGate.run.flyby;
    unfinished.result = FlybyGrade::Active;
    unfinished.pathProgress = tuning::flyby::finishProgress - 0.01;
    unfinished.worstZone = 2;
    require(flybyGrade(unfinished) == FlybyGrade::Miss, "a flyby should not score until the finish line is reached");

    LaunchOutcome outerArrival;
    outerArrival.type = LaunchResultType::MissionComplete;
    outerArrival.frontierTransfer = true;
    outerArrival.destinationId = content::destination::outerPlanets;
    GameState outer = createNewGame(catalog, 707);
    startArrivalOps(outer, outerArrival);
    startArrivalFlybyRun(outer, catalog);
    require(outer.run.flyby.gravityStrength > miss.run.flyby.gravityStrength, "large-planet flyby should apply stronger gravity than Moon/Mars");
    const double initialProgress = outer.run.flyby.pathProgress;
    updateFlybyRun(outer, 0.5);
    require(outer.run.flyby.pathProgress >= initialProgress, "single-pass flyby should advance along the corridor over time");

    GameState longTrail = createNewGame(catalog, 718);
    startArrivalOps(longTrail, moonArrival);
    startArrivalFlybyRun(longTrail, catalog);
    longTrail.run.flyby.gravityStrength = 0.0;
    longTrail.run.flyby.trailPoints.clear();
    for (int i = 0; i < 120; ++i) {
        longTrail.run.flyby.trailPoints.push_back({-1.2 + static_cast<double>(i) * 0.001, -0.9});
    }
    const FlybyTrailPoint oldestTrailPoint = longTrail.run.flyby.trailPoints.front();
    const std::size_t previousTrailSize = longTrail.run.flyby.trailPoints.size();
    updateFlybyRun(longTrail, 0.05);
    require(longTrail.run.flyby.trailPoints.size() > previousTrailSize, "flyby trail should keep growing instead of trimming older path points");
    require(longTrail.run.flyby.trailPoints.front().x == oldestTrailPoint.x && longTrail.run.flyby.trailPoints.front().y == oldestTrailPoint.y,
        "flyby trail should preserve the oldest path point through a long run");

    GameState impact = createNewGame(catalog, 708);
    startArrivalOps(impact, moonArrival);
    startArrivalFlybyRun(impact, catalog);
    impact.run.shipDamage = 7;
    impact.run.flyby.shipX = tuning::flyby::destinationX;
    impact.run.flyby.shipY = tuning::flyby::destinationY;
    updateFlybyRun(impact, 0.1);
    require(impact.run.flyby.completed && impact.run.flyby.collidedWithBody, "flyby should end immediately when the ship intersects the destination body");
    require(impact.run.flyby.result == FlybyGrade::Miss, "planet impact should grade as a missed flyby");
    require(impact.run.shipDamage == 7 + impact.run.flyby.impactHullDamage, "planet impact should add assisted hull damage to the ship");

    GameState outOfBounds = createNewGame(catalog, 714);
    startArrivalOps(outOfBounds, moonArrival);
    startArrivalFlybyRun(outOfBounds, catalog);
    outOfBounds.run.flyby.shipX = 1.05;
    outOfBounds.run.flyby.velocityX = tuning::flyby::maxSpeed;
    updateFlybyRun(outOfBounds, 0.2);
    require(outOfBounds.run.flyby.completed, "leaving the flyby playfield should end the run");
    require(outOfBounds.run.flyby.result == FlybyGrade::Miss, "leaving the flyby playfield should be a missed flyby");

    GameState controls = createNewGame(catalog, 709);
    startArrivalOps(controls, moonArrival);
    startArrivalFlybyRun(controls, catalog);
    controls.run.flyby.gravityStrength = 0.0;
    const double baseSpeed = std::hypot(controls.run.flyby.velocityX, controls.run.flyby.velocityY);
    setFlybyMove(controls, 0.0, 1.0);
    updateFlybyRun(controls, 0.2);
    const double fasterSpeed = std::hypot(controls.run.flyby.velocityX, controls.run.flyby.velocityY);
    require(fasterSpeed > baseSpeed, "flyby throttle input should increase speed");
    setFlybyMove(controls, 0.0, -1.0);
    updateFlybyRun(controls, 0.2);
    const double slowerSpeed = std::hypot(controls.run.flyby.velocityX, controls.run.flyby.velocityY);
    require(slowerSpeed < fasterSpeed, "flyby brake input should reduce speed");

    GameState turn = createNewGame(catalog, 710);
    startArrivalOps(turn, moonArrival);
    startArrivalFlybyRun(turn, catalog);
    turn.run.flyby.gravityStrength = 0.0;
    const double headingBefore = std::atan2(turn.run.flyby.velocityY, turn.run.flyby.velocityX);
    setFlybyMove(turn, 1.0, 0.0);
    updateFlybyRun(turn, 0.2);
    const double headingAfter = std::atan2(turn.run.flyby.velocityY, turn.run.flyby.velocityX);
    require(headingAfter > headingBefore, "positive flyby turn input should rotate counter-clockwise");
}

void shipUpgradesAssistFlybyAndOrbitMinigames()
{
    const ContentCatalog catalog = createDefaultContent();
    LaunchOutcome marsArrival;
    marsArrival.type = LaunchResultType::MissionComplete;
    marsArrival.destinationId = content::destination::mars;
    marsArrival.frontierTransfer = true;

    GameState baseline = createNewGame(catalog, 718);
    baseline.run.equippedModuleIds = {
        content::module::sparrowEngine,
        content::module::stableTank,
        content::module::patchworkHull,
        content::module::radiatorVanes,
        content::module::analogTelemetry,
        content::module::springCapsule
    };
    startArrivalOps(baseline, marsArrival);
    startArrivalFlybyRun(baseline, catalog);
    startArrivalOrbitRun(baseline, catalog);
    const FlybyRunState baselineFlyby = baseline.run.flyby;
    const OrbitRunState baselineOrbit = baseline.run.orbit;

    GameState assisted = createNewGame(catalog, 719);
    assisted.run.equippedModuleIds = {
        content::module::kestrelEngine,
        content::module::deepReservoir,
        content::module::titaniumRib,
        content::module::ablativeSkin,
        content::module::predictiveGuidance,
        content::module::abortTower
    };
    startArrivalOps(assisted, marsArrival);
    startArrivalFlybyRun(assisted, catalog);
    startArrivalOrbitRun(assisted, catalog);

    require(assisted.run.flyby.goodBand > baselineFlyby.goodBand, "sensor upgrades should widen the flyby good corridor");
    require(assisted.run.flyby.perfectBand > baselineFlyby.perfectBand, "sensor upgrades should widen the flyby perfect corridor");
    require(assisted.run.flyby.turnRateRadians > baselineFlyby.turnRateRadians, "thrust and escape upgrades should improve flyby steering response");
    require(assisted.run.flyby.impactHullDamage < baselineFlyby.impactHullDamage, "hull, cooling, and escape upgrades should reduce flyby impact damage");
    require(assisted.run.orbit.goodBand > baselineOrbit.goodBand, "sensor upgrades should widen the orbital research band");
    require(assisted.run.orbit.perfectBand > baselineOrbit.perfectBand, "sensor upgrades should widen the perfect orbit band");
    require(assisted.run.orbit.thrustAcceleration > baselineOrbit.thrustAcceleration, "thrust and cooling upgrades should improve orbit trim authority");
    require(assisted.run.orbit.durationSeconds > baselineOrbit.durationSeconds, "fuel and sensors should add a small orbit insertion buffer");
}

void activeFlybySaveResumesAtApproach()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 705);
    LaunchOutcome moonArrival;
    moonArrival.type = LaunchResultType::MissionComplete;
    moonArrival.frontierTransfer = true;
    moonArrival.destinationId = content::destination::moon;
    startArrivalOps(state, moonArrival);
    startArrivalFlybyRun(state, catalog);

    const SaveData save = captureSaveData(state);
    require(save.screen == Screen::ArrivalOps, "saving during flyby should persist the safe approach screen");
    GameState restored = createNewGame(catalog, 706);
    restoreSaveData(restored, catalog, save);
    require(restored.screen == Screen::ArrivalOps, "loading during flyby should resume at approach options");
    require(!restored.run.flyby.active, "loading during flyby should not restore transient flyby state");
    require(restored.run.arrivalOps.active && restored.run.arrivalOps.destinationId == content::destination::moon, "approach destination should survive flyby save/load");
}

void arrivalOrbitMinigameRewardsProgressionOnlyResearch()
{
    const ContentCatalog catalog = createDefaultContent();
    LaunchOutcome moonArrival;
    moonArrival.type = LaunchResultType::MissionComplete;
    moonArrival.frontierTransfer = true;
    moonArrival.destinationId = content::destination::moon;

    GameState miss = createNewGame(catalog, 721);
    startArrivalOps(miss, moonArrival);
    completeArrivalFlyby(miss, catalog);
    startArrivalOps(miss, moonArrival);
    startArrivalOrbitRun(miss, catalog);
    require(miss.screen == Screen::Orbit && miss.run.orbit.active, "starting arrival orbit should open the orbit minigame");
    require(miss.run.orbit.currentZone >= 1, "orbit should begin inside the scalable orbital band");
    require(miss.run.orbit.durationSeconds >= tuning::orbit::durationSeconds, "orbit should start from the tuned insertion timer");
    require(miss.run.orbit.durationSeconds <= tuning::orbit::durationSeconds + tuning::orbit::maxAssistDurationBonus + 0.001, "orbit assist should keep the insertion buffer bounded");
    const int missOrbitsBefore = destinationHistoryValue(miss.meta.destinationOrbits, catalog, content::destination::moon);
    const int missBlueprintsBefore = miss.meta.blueprintProgress;
    const double missCreditsBefore = miss.run.credits;
    miss.run.orbit.completed = true;
    miss.run.orbit.result = OrbitGrade::Miss;
    completeOrbitRun(miss, catalog);
    require(miss.screen == Screen::ArrivalOps, "missed orbit should return to approach options");
    require(destinationHistoryValue(miss.meta.destinationOrbits, catalog, content::destination::moon) == missOrbitsBefore, "missed orbit should not bank orbit history");
    require(miss.meta.blueprintProgress == missBlueprintsBefore, "missed orbit should not grant science");
    require(std::abs(miss.run.credits - missCreditsBefore) < 0.001, "missed orbit should not grant credits");

    GameState good = createNewGame(catalog, 722);
    startArrivalOps(good, moonArrival);
    completeArrivalFlyby(good, catalog);
    startArrivalOps(good, moonArrival);
    startArrivalOrbitRun(good, catalog);
    const int goodOrbitsBefore = destinationHistoryValue(good.meta.destinationOrbits, catalog, content::destination::moon);
    const int goodBlueprintsBefore = good.meta.blueprintProgress;
    const double goodCreditsBefore = good.run.credits;
    good.run.orbit.completed = true;
    good.run.orbit.result = OrbitGrade::Good;
    completeOrbitRun(good, catalog);
    require(destinationHistoryValue(good.meta.destinationOrbits, catalog, content::destination::moon) == goodOrbitsBefore + 1, "good orbit should bank destination orbit history");
    require(good.meta.blueprintProgress == goodBlueprintsBefore + tuning::orbit::goodBlueprintGain, "good orbit should grant science");
    require(good.run.credits > goodCreditsBefore, "good orbit should grant credits");
    require(good.run.nextLaunchFuelBoost == 0.0 && good.run.nextLaunchSpeedBoost == 0.0, "good orbit should not grant launch boosts");

    GameState perfect = createNewGame(catalog, 723);
    startArrivalOps(perfect, moonArrival);
    completeArrivalFlyby(perfect, catalog);
    startArrivalOps(perfect, moonArrival);
    startArrivalOrbitRun(perfect, catalog);
    const int perfectBlueprintsBefore = perfect.meta.blueprintProgress;
    const double perfectCreditsBefore = perfect.run.credits;
    perfect.run.orbit.completed = true;
    perfect.run.orbit.result = OrbitGrade::Perfect;
    Random perfectRng(723);
    const PreparedLaunch perfectLaunch = prepareLaunch(perfect, catalog, perfectRng);
    const std::string perfectOrbitHtml = buildGamePanelHtml({perfect, catalog, perfectLaunch, perfectLaunch});
    require(perfectOrbitHtml.find("data-orbit-tag-three=\"+") != std::string::npos, "perfect orbit stamp should expose the credit reward chip");
    require(perfectOrbitHtml.find(" credits\" hidden") != std::string::npos, "perfect orbit credit reward chip should label mission credits");
    completeOrbitRun(perfect, catalog);
    require(perfect.meta.blueprintProgress == perfectBlueprintsBefore + tuning::orbit::perfectBlueprintGain, "perfect orbit should grant stronger science");
    require(perfect.run.credits > perfectCreditsBefore, "perfect orbit should grant credits");
    require(perfect.run.nextLaunchFuelBoost == 0.0 && perfect.run.nextLaunchSpeedBoost == 0.0, "perfect orbit should still avoid launch boosts");
}

void activeOrbitSaveResumesAtApproach()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 724);
    LaunchOutcome moonArrival;
    moonArrival.type = LaunchResultType::MissionComplete;
    moonArrival.frontierTransfer = true;
    moonArrival.destinationId = content::destination::moon;
    startArrivalOps(state, moonArrival);
    completeArrivalFlyby(state, catalog);
    startArrivalOps(state, moonArrival);
    startArrivalOrbitRun(state, catalog);

    const SaveData save = captureSaveData(state);
    require(save.screen == Screen::ArrivalOps, "saving during orbit should persist the safe approach screen");
    GameState restored = createNewGame(catalog, 725);
    restoreSaveData(restored, catalog, save);
    require(restored.screen == Screen::ArrivalOps, "loading during orbit should resume at approach options");
    require(!restored.run.orbit.active, "loading during orbit should not restore transient orbit state");
    require(restored.run.arrivalOps.active && restored.run.arrivalOps.destinationId == content::destination::moon, "approach destination should survive orbit save/load");
}

void researchProjectsGenerateAndCompleteFromSharedRules()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 606);
    state.run.destinationIndex = 2;
    state.meta.materials = {.common = 4, .rare = 2};
    Random rng(606);

    generateResearchProjects(state, catalog, rng);
    const auto firstProject = std::find_if(state.run.researchProjectIds.begin(), state.run.researchProjectIds.end(), [](const std::string& id) {
        return !id.empty();
    });
    require(firstProject != state.run.researchProjectIds.end(), "Mars research should generate at least one available project");

    const auto index = static_cast<int>(std::distance(state.run.researchProjectIds.begin(), firstProject));
    const ResearchProject* project = catalog.findResearchProject(*firstProject);
    require(project != nullptr, "generated research project id should resolve");
    const int blueprintsBefore = state.meta.blueprintProgress;
    const int expectedBlueprintGain = researchBlueprintGain(state.meta, *project);
    const MaterialInventory materialsBefore = state.meta.materials;

    const ResearchOutcome outcome = completeResearchProject(state, catalog, index);
    require(outcome.completed, "affordable research project should complete");
    require(outcome.projectId == project->id, "research outcome should identify the project");
    require(outcome.blueprintGain == expectedBlueprintGain, "research outcome should report effective blueprint progress");
    require(state.meta.blueprintProgress == blueprintsBefore + expectedBlueprintGain, "research should grant effective blueprint progress");
    require(state.meta.materials.common == materialsBefore.common - project->materialCost.common, "research should spend common material cost");
    require(state.meta.materials.rare == materialsBefore.rare - project->materialCost.rare, "research should spend rare material cost");
    require(state.run.researchProjectIds[static_cast<std::size_t>(index)].empty(), "completed research slot should be consumed");
}

void materialResearchUnlocksModuleFamilies()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 616);
    state.run.destinationIndex = 2;
    state.meta.materials = {.common = 1, .rare = 1};
    state.run.researchProjectIds = {content::research::prototypeSchematic, "", ""};

    require(!hasUnlock(state.meta, content::unlock::thermal), "test starts before thermal research unlock");
    const ResearchOutcome outcome = completeResearchProject(state, catalog, 0);
    require(outcome.completed, "material-funded prototype research should complete");
    require(outcome.rewardUnlockKey == content::unlock::thermal, "prototype research should report its reward unlock");
    require(outcome.unlockedReward, "first material research completion should report a new unlock");
    require(hasUnlock(state.meta, content::unlock::thermal), "material-funded research should unlock the module family");
    require(catalog.findModule(content::module::slushTank) != nullptr, "test needs thermal module content");
    require(isModuleUnlocked(state.meta, *catalog.findModule(content::module::slushTank)), "new research unlock should affect module availability");

    state.meta.materials = {.common = 1, .rare = 1};
    state.run.researchProjectIds = {content::research::prototypeSchematic, "", ""};
    const ResearchOutcome repeated = completeResearchProject(state, catalog, 0);
    require(repeated.completed, "repeating an already-unlocked project should still complete if affordable");
    require(!repeated.unlockedReward, "repeating an already-unlocked project should not report a fresh unlock");
}

void artifactInsightImprovesFutureResearch()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 627);
    state.run.destinationIndex = 2;
    state.meta.materials = {.common = 4};
    state.meta.artifacts = {
        {"mars_artifact_1", content::destination::mars, true},
        {"mars_artifact_2", content::destination::mars, true},
        {"mars_artifact_3", content::destination::mars, false}
    };
    state.run.researchProjectIds = {content::research::appliedMaterialsLab, "", ""};

    const ResearchProject* project = catalog.findResearchProject(content::research::appliedMaterialsLab);
    require(project != nullptr, "artifact insight test needs materials research content");
    require(identifiedArtifactCount(state.meta) == 2, "artifact insight should count only decoded artifacts");
    require(artifactInsightBlueprintBonus(state.meta) == 2, "decoded artifacts should add blueprint insight");

    const ResearchOutcome outcome = completeResearchProject(state, catalog, 0);
    require(outcome.completed, "research should complete with artifact insight active");
    require(outcome.blueprintGain == project->blueprintGain + 2, "artifact insight should improve future research output");
    require(state.meta.blueprintProgress == project->blueprintGain + 2, "artifact insight should be added to meta blueprint progress");

    state.meta.artifacts = {
        {"a", content::destination::mars, true},
        {"b", content::destination::mars, true},
        {"c", content::destination::mars, true},
        {"d", content::destination::mars, true}
    };
    require(artifactInsightBlueprintBonus(state.meta) == tuning::research::artifactInsightBlueprintMaximum, "artifact insight should be capped");
}

void researchFacilitiesImproveFutureResearch()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 628);
    state.run.destinationIndex = 2;
    state.meta.materials = {.common = 3, .rare = 1};
    state.run.researchProjectIds = {content::research::missionAnalysisLab, "", ""};

    const ResearchOutcome labOutcome = completeResearchProject(state, catalog, 0);
    require(labOutcome.completed, "mission analysis lab should complete when funded");
    require(hasUnlock(state.meta, content::unlock::analysisLab), "mission analysis lab research should unlock the research facility");
    require(researchFacilityBlueprintBonus(state.meta) == tuning::research::analysisLabBlueprintBonus, "analysis lab should add future blueprint output");

    const ResearchProject* project = catalog.findResearchProject(content::research::blueprintSurvey);
    require(project != nullptr, "research facility test needs blueprint survey content");
    state.meta.materials = {};
    state.run.researchProjectIds = {content::research::blueprintSurvey, "", ""};
    const ResearchOutcome surveyOutcome = completeResearchProject(state, catalog, 0);
    require(surveyOutcome.completed, "no-cost blueprint survey should complete after lab research");
    require(surveyOutcome.blueprintGain == project->blueprintGain + tuning::research::analysisLabBlueprintBonus, "analysis lab should improve future research blueprint gain");
}

void artifactResearchIdentifiesRecoveredArtifacts()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 626);
    state.run.destinationIndex = 4;
    state.meta.unlockKeys.push_back(content::unlock::ai);
    state.meta.materials = {.rare = 2, .exotic = 1};
    state.meta.artifacts.push_back({"mars_artifact_3", content::destination::mars, false});
    state.run.researchProjectIds = {content::research::artifactDecoding, "", ""};
    const ResearchProject* project = catalog.findResearchProject(content::research::artifactDecoding);
    require(project != nullptr, "artifact research test needs artifact decoding content");

    const ResearchOutcome outcome = completeResearchProject(state, catalog, 0);
    require(outcome.completed, "artifact research should complete when affordable and unlocked");
    require(outcome.blueprintGain == project->blueprintGain, "newly decoded artifacts should improve future research, not the current decoding pass");
    require(outcome.identifiedArtifact, "artifact research should identify a recovered artifact");
    require(outcome.artifactId == "mars_artifact_3", "artifact research should report the identified artifact");
    require(state.meta.artifacts.front().identified, "identified artifact should persist in meta progress");

    state.meta.materials = {.rare = 2, .exotic = 1};
    state.run.researchProjectIds = {content::research::artifactDecoding, "", ""};
    const ResearchOutcome repeated = completeResearchProject(state, catalog, 0);
    require(repeated.completed, "artifact research should still complete when every artifact is already identified");
    require(!repeated.identifiedArtifact, "artifact research should not report a new artifact when none are unidentified");
}

void researchOutcomeSummaryShowsRewardsAndCosts()
{
    ResearchOutcome outcome;
    outcome.completed = true;
    outcome.blueprintGain = 3;
    outcome.materialCost = {.common = 2, .rare = 1};
    outcome.rewardUnlockKey = content::unlock::surfaceProbes;
    outcome.unlockedReward = true;
    outcome.identifiedArtifact = true;
    outcome.artifactId = "mars_artifact_1";

    const std::string summary = researchOutcomeSummary(outcome);
    require(summary.find(std::string(text::status::researchCompleted)) != std::string::npos, "research summary should include completion text");
    require(summary.find("+3 BP") != std::string::npos, "research summary should include blueprint gain");
    require(summary.find("Spent 2 common, 1 rare, 0 exotic") != std::string::npos, "research summary should include material cost");
    require(summary.find("Unlocks: Field probes") != std::string::npos, "research summary should include newly unlocked family");
    require(summary.find("Decoded mars_artifact_1") != std::string::npos, "research summary should include decoded artifact id");
}

void surfaceToolResearchImprovesExpeditions()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState baseline = createNewGame(catalog, 636);
    baseline.run.destinationIndex = 2;
    startSurfaceExpedition(baseline, catalog);
    const int baselineSupply = baseline.run.surfaceExpedition.supply;
    Random baselineRng(636);
    const SurfaceActionOutcome baselineSurvey = surveySurfaceSite(baseline, baselineRng);
    const SurfaceActionOutcome baselineMine = mineSurfaceDeposit(baseline, baselineRng);
    baseline.run.surfaceExpedition.cargo = 8;
    const double baselineRisk = surfaceExtractionRisk(baseline);

    GameState upgraded = createNewGame(catalog, 637);
    upgraded.run.destinationIndex = 2;
    upgraded.meta.unlockKeys.push_back(content::unlock::surfaceProbes);
    upgraded.meta.unlockKeys.push_back(content::unlock::surfaceDrills);
    upgraded.meta.unlockKeys.push_back(content::unlock::cargoRigs);
    startSurfaceExpedition(upgraded, catalog);
    require(upgraded.run.surfaceExpedition.supply > baselineSupply, "field probes should add surface expedition supply");

    Random upgradedRng(636);
    const SurfaceActionOutcome upgradedSurvey = surveySurfaceSite(upgraded, upgradedRng);
    const SurfaceActionOutcome upgradedMine = mineSurfaceDeposit(upgraded, upgradedRng);
    require(upgradedSurvey.materialDelta.common > baselineSurvey.materialDelta.common, "field probes should improve survey returns");
    require(upgradedMine.materialDelta.common > baselineMine.materialDelta.common, "surface drills should improve mine returns");

    upgraded.run.surfaceExpedition.cargo = 8;
    const double upgradedRisk = surfaceExtractionRisk(upgraded);
    require(upgradedRisk < baselineRisk, "cargo rigs should reduce extraction risk for matching cargo");

    const SurfaceExpeditionPresentation presentation = surfaceExpeditionPresentation(upgraded);
    const auto fieldKit = std::find_if(presentation.metrics.begin(), presentation.metrics.end(), [](const PanelMetricPresentation& metric) {
        return metric.label == std::string(text::labels::fieldKit);
    });
    require(fieldKit != presentation.metrics.end(), "surface presentation should expose active field kit");
    require(fieldKit->value.find("Field probes") != std::string::npos, "surface presentation should name field probe unlocks");
    require(!presentation.actions.empty() && presentation.actions.front().risk.find("%") != std::string::npos, "surface presentation should expose action hazard risk");
}

void animalCrewClassesModifySurfaceExpeditions()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState prairieDog = createNewGame(catalog, 638);
    activateOnlyCrew(prairieDog, content::astronaut::eli);
    const SurfaceCrewEffects prairieDogEffects = surfaceCrewEffects(prairieDog);
    require(prairieDogEffects.surveyCommonBonus > 0, "prairie dog scouts should improve surface surveying");
    require(prairieDogEffects.artifactChanceBonus > 0.0, "prairie dog scouts should improve anomaly reads");

    GameState squirrel = createNewGame(catalog, 639);
    activateOnlyCrew(squirrel, content::astronaut::jo);
    const SurfaceCrewEffects squirrelEffects = surfaceCrewEffects(squirrel);
    require(squirrelEffects.mineRareChanceBonus > 0.0, "squirrel hoarders should improve rare material odds");

    GameState fox = createNewGame(catalog, 640);
    activateOnlyCrew(fox, content::astronaut::nia);
    const SurfaceCrewEffects foxEffects = surfaceCrewEffects(fox);
    require(foxEffects.extractionRiskRelief > 0.0, "fox aces should improve extraction routing");

    GameState capybara = createNewGame(catalog, 641);
    capybara.run.destinationIndex = 2;
    startSurfaceExpedition(capybara, catalog);
    const SurfaceExpeditionPresentation presentation = surfaceExpeditionPresentation(capybara);
    require(findDetailPresentationRow(presentation.details, text::panel::details::fieldSpecialist) != nullptr, "surface details should show the active animal class effect");
}

void surfaceUpgradeOffersAreDistinctAndSelectable()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 642);
    state.run.destinationIndex = 2;
    state.screen = Screen::SurfaceExpedition;
    startSurfaceExpedition(state, catalog);

    Random rng(643);
    generateSurfaceUpgradeOffers(state, catalog, rng);
    require(state.run.surfaceExpedition.surfaceUpgradeOfferAvailable, "successful surface progress should expose a field upgrade offer");

    std::vector<std::string> offers;
    for (const std::string& offerId : state.run.surfaceExpedition.surfaceUpgradeOfferIds) {
        if (!offerId.empty()) {
            offers.push_back(offerId);
            require(catalog.findSurfaceUpgrade(offerId) != nullptr, "surface upgrade offer ids should resolve through content");
        }
    }
    require(offers.size() == 3, "surface upgrade offers should present three options when possible");
    std::sort(offers.begin(), offers.end());
    require(std::adjacent_find(offers.begin(), offers.end()) == offers.end(), "surface upgrade offers should be distinct");

    const std::string chosenId = state.run.surfaceExpedition.surfaceUpgradeOfferIds[0];
    require(chooseSurfaceUpgrade(state, catalog, 0), "selecting a valid surface upgrade should apply it");
    require(!state.run.surfaceExpedition.surfaceUpgradeOfferAvailable, "choosing a surface upgrade should consume the offer");
    require(state.run.surfaceUpgradeIds.size() == 1 && state.run.surfaceUpgradeIds.front() == chosenId, "selected surface upgrade should persist on the current ship");
    require(!state.run.surfaceExpedition.logEntries.empty() && state.run.surfaceExpedition.logEntries.back().find("Field upgrade installed") != std::string::npos, "surface upgrade selection should be logged");
}

void surfaceUpgradeOffersCanRerollAndKeepDraftState()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 6431);
    state.run.destinationIndex = 2;
    state.screen = Screen::SurfaceUpgrade;
    state.run.credits = 200.0;
    startSurfaceExpedition(state, catalog);

    Random rng(6432);
    generateSurfaceUpgradeOffers(state, catalog, rng);
    require(state.run.surfaceExpedition.surfaceUpgradeOfferAvailable, "surface upgrade draft should start with offers available");
    const auto originalOffers = state.run.surfaceExpedition.surfaceUpgradeOfferIds;
    const double originalCredits = state.run.credits;

    require(rerollSurfaceUpgradeOffers(state, catalog, rng), "affordable surface upgrade reroll should succeed");
    require(state.run.surfaceExpedition.surfaceUpgradeOfferAvailable, "rerolling should keep the draft active");
    require(state.run.credits < originalCredits, "field-upgrade reroll should spend credits");
    require(state.run.surfaceExpedition.surfaceUpgradeOfferIds != originalOffers, "rerolling should refresh the field-upgrade board when options remain");
}

void selectedSurfaceUpgradesModifyMiningAndSurfaceStats()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline = createNewGame(catalog, 644);
    baseline.run.destinationIndex = 2;
    startSurfaceExpedition(baseline, catalog);
    baseline.run.surfaceExpedition.cargo = 8;

    GameState upgraded = baseline;
    upgraded.run.surfaceUpgradeIds = {
        content::surfaceUpgrade::thermalDrillJackets,
        content::surfaceUpgrade::widebandPulse,
        content::surfaceUpgrade::cargoSkids,
        content::surfaceUpgrade::shockMounts,
        content::surfaceUpgrade::oreScentArray
    };

    const MiningDrillStats baselineStats = miningDrillStats(baseline, catalog);
    const MiningDrillStats upgradedStats = miningDrillStats(upgraded, catalog);
    require(upgradedStats.heatRiseScale < baselineStats.heatRiseScale, "thermal field upgrades should reduce mining heat rise");
    require(upgradedStats.scannerRadius > baselineStats.scannerRadius, "scanner field upgrades should widen pulse reveal radius");
    require(upgradedStats.integrityRelief > baselineStats.integrityRelief, "shock mounts should improve mining durability");
    require(upgradedStats.hardRockBounceRelief > baselineStats.hardRockBounceRelief, "shock mounts should reduce hard-rock recoil");
    require(upgradedStats.oreYieldChance > baselineStats.oreYieldChance, "ore field upgrades should improve yield odds");
    require(surfaceExtractionRisk(upgraded) < surfaceExtractionRisk(baseline), "cargo field upgrades should reduce extraction risk");

    const SurfaceExpeditionPresentation presentation = surfaceExpeditionPresentation(upgraded, catalog);
    require(!presentation.selectedUpgradeNames.empty(), "surface presentation should expose selected field upgrades");
    require(findDetailPresentationRow(presentation.details, "Field upgrades") != nullptr, "surface details should list field upgrades");
}

void surfaceUpgradesAndDronesModifyScanMiniGame()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline = createNewGame(catalog, 1933);
    baseline.run.destinationIndex = 2;
    startSurfaceExpedition(baseline, catalog);
    baseline.run.surfaceExpedition.hazard = 0.35;

    GameState upgraded = baseline;
    upgraded.run.surfaceUpgradeIds = {
        content::surfaceUpgrade::widebandPulse,
        content::surfaceUpgrade::oreScentArray,
        content::surfaceUpgrade::deepEchoMapper
    };
    upgraded.meta.unlockKeys.push_back(content::unlock::droneBay);
    upgraded.meta.droneBaySlots = 1;
    upgraded.meta.equippedDroneIds = {content::drone::surveyDrone};

    Random baselineRng(1934);
    Random upgradedRng(1934);
    require(startSurfaceScanRun(baseline, baselineRng).applied, "baseline scan should start");
    require(startSurfaceScanRun(upgraded, upgradedRng).applied, "upgraded scan should start");
    require(upgraded.run.surfaceScan.maxPulses > baseline.run.surfaceScan.maxPulses, "scanner support should add scan push-your-luck capacity");
    require(upgraded.run.surfaceScan.signal > baseline.run.surfaceScan.signal, "scanner support should improve starting scan signal");
    require(upgraded.run.surfaceScan.interference <= baseline.run.surfaceScan.interference, "scanner support should soften starting interference");
    require(upgraded.run.surfaceScan.bustRisk < baseline.run.surfaceScan.bustRisk, "scanner support should reduce scan bust risk");

    baseline.run.surfaceScan.bustRisk = 0.0;
    upgraded.run.surfaceScan.bustRisk = 0.0;
    require(pulseSurfaceScan(baseline, baselineRng).applied, "baseline scan pulse should resolve");
    require(pulseSurfaceScan(upgraded, upgradedRng).applied, "upgraded scan pulse should resolve");
    require(upgraded.run.surfaceScan.signal > baseline.run.surfaceScan.signal, "scanner support should continue improving signal after a pulse");
    require(upgraded.run.surfaceScan.hazardDelta <= baseline.run.surfaceScan.hazardDelta, "hazard-relief scanner upgrades should reduce scan hazard creep");
    require(upgraded.run.surfaceScan.bustRisk < baseline.run.surfaceScan.bustRisk, "scanner support should keep post-pulse bust risk lower");
}

void surfaceUpgradesAndDronesModifyDeepPushMiniGame()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline = createNewGame(catalog, 1935);
    baseline.run.destinationIndex = 2;
    startSurfaceExpedition(baseline, catalog);
    baseline.run.surfaceExpedition.hazard = 0.35;

    GameState upgraded = baseline;
    upgraded.run.surfaceUpgradeIds = {
        content::surfaceUpgrade::thermalDrillJackets,
        content::surfaceUpgrade::shockMounts,
        content::surfaceUpgrade::recoilBraces,
        content::surfaceUpgrade::oreHopper
    };
    upgraded.meta.unlockKeys.push_back(content::unlock::droneBay);
    upgraded.meta.droneBaySlots = 1;
    upgraded.meta.equippedDroneIds = {content::drone::stabilizerDrone};

    Random baselineRng(1936);
    Random upgradedRng(1936);
    require(startSurfacePushRun(baseline, baselineRng).applied, "baseline deep push should start");
    require(startSurfacePushRun(upgraded, upgradedRng).applied, "upgraded deep push should start");
    require(upgraded.run.surfacePush.maxSteps > baseline.run.surfacePush.maxSteps, "structural support should add deep-push capacity");
    require(upgraded.run.surfacePush.pressure <= baseline.run.surfacePush.pressure, "structural support should reduce starting deep-push pressure");
    require(upgraded.run.surfacePush.collapseRisk < baseline.run.surfacePush.collapseRisk, "structural support should reduce starting collapse risk");

    baseline.run.surfacePush.collapseRisk = 0.0;
    upgraded.run.surfacePush.collapseRisk = 0.0;
    require(pushSurfaceDepthStep(baseline, baselineRng).applied, "baseline deep-push step should resolve");
    require(pushSurfaceDepthStep(upgraded, upgradedRng).applied, "upgraded deep-push step should resolve");
    require(upgraded.run.surfacePush.pressure < baseline.run.surfacePush.pressure, "structural support should slow pressure growth");
    require(upgraded.run.surfacePush.hazardDelta <= baseline.run.surfacePush.hazardDelta, "field support should reduce deep-push hazard creep");
    require(upgraded.run.surfacePush.collapseRisk < baseline.run.surfacePush.collapseRisk, "structural support should keep post-step collapse risk lower");
}

void surfaceScanAndPushDepthLimitsStayInParity()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline = createNewGame(catalog, 1937);
    baseline.run.destinationIndex = 2;
    startSurfaceExpedition(baseline, catalog);
    baseline.run.surfaceExpedition.supply = 10;

    auto scanPushLimitPair = [](const GameState& state, int seed) {
        GameState scanState = state;
        GameState pushState = state;
        Random scanRng(seed);
        Random pushRng(seed + 1);
        require(startSurfaceScanRun(scanState, scanRng).applied, "scan should start for depth-limit parity");
        require(startSurfacePushRun(pushState, pushRng).applied, "push should start for depth-limit parity");
        return std::pair<int, int> {
            scanState.run.surfaceScan.maxPulses,
            pushState.run.surfacePush.maxSteps
        };
    };

    const auto baselineLimits = scanPushLimitPair(baseline, 1938);
    require(baselineLimits.first == baselineLimits.second + 1, "baseline scan should map current layer plus every reachable pushed layer");

    GameState scannerUpgraded = baseline;
    scannerUpgraded.run.surfaceUpgradeIds = {
        content::surfaceUpgrade::widebandPulse,
        content::surfaceUpgrade::deepEchoMapper
    };
    scannerUpgraded.meta.unlockKeys.push_back(content::unlock::droneBay);
    scannerUpgraded.meta.droneBaySlots = 1;
    scannerUpgraded.meta.equippedDroneIds = {content::drone::surveyDrone};
    const auto scannerLimits = scanPushLimitPair(scannerUpgraded, 1939);
    require(scannerLimits.first == scannerLimits.second + 1, "scanner support should keep scan and push depth limits in parity");
    require(scannerLimits.second > baselineLimits.second, "scanner support should also expand reachable push depth");

    GameState structureUpgraded = baseline;
    structureUpgraded.run.surfaceUpgradeIds = {
        content::surfaceUpgrade::thermalDrillJackets,
        content::surfaceUpgrade::shockMounts,
        content::surfaceUpgrade::recoilBraces
    };
    structureUpgraded.meta.unlockKeys.push_back(content::unlock::droneBay);
    structureUpgraded.meta.droneBaySlots = 1;
    structureUpgraded.meta.equippedDroneIds = {content::drone::stabilizerDrone};
    const auto structureLimits = scanPushLimitPair(structureUpgraded, 1940);
    require(structureLimits.first == structureLimits.second + 1, "structural support should keep scan and push depth limits in parity");
    require(structureLimits.first > baselineLimits.first, "structural support should also expand scan depth");
}

void surfaceUpgradesPersistUntilNewShipAndRoundTripSave()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 645);
    state.run.destinationIndex = 2;
    state.screen = Screen::SurfaceUpgrade;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceUpgradeIds = {content::surfaceUpgrade::shockMounts};
    state.run.surfaceExpedition.surfaceUpgradeOfferIds = {
        content::surfaceUpgrade::thermalDrillJackets,
        content::surfaceUpgrade::widebandPulse,
        content::surfaceUpgrade::shockMounts
    };
    state.run.surfaceExpedition.surfaceUpgradeOfferAvailable = true;
    state.run.surfaceExpedition.surfaceUpgradeOffersSeen = 1;

    const std::string serialized = serializeSaveData(captureSaveData(state));
    const auto save = deserializeSaveData(serialized);
    require(save.has_value(), "surface upgrade save should parse");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);
    require(restored.screen == Screen::SurfaceUpgrade, "active field-upgrade draft should round trip through save data");
    require(restored.run.surfaceUpgradeIds == state.run.surfaceUpgradeIds, "selected surface upgrades should round trip");
    require(restored.run.surfaceExpedition.surfaceUpgradeOfferIds == state.run.surfaceExpedition.surfaceUpgradeOfferIds, "surface upgrade offers should round trip");
    require(restored.run.surfaceExpedition.surfaceUpgradeOfferAvailable, "surface upgrade offer availability should round trip");

    Random rng(646);
    restored.run.surfaceExpedition.temporaryMaterials.common = 1;
    const SurfaceActionOutcome extracted = extractSurfacePayload(restored, rng);
    require(extracted.applied, "surface extraction should resolve while upgrades are active");
    require(restored.run.surfaceUpgradeIds == state.run.surfaceUpgradeIds, "surface upgrades should persist after successful extraction");
    require(!restored.run.surfaceExpedition.surfaceUpgradeOfferAvailable, "surface upgrade offers should clear after extraction");

    restored.run.surfaceUpgradeIds = state.run.surfaceUpgradeIds;
    LaunchOutcome destroyed;
    destroyed.type = LaunchResultType::Destroyed;
    destroyed.destinationId = currentDestination(restored, catalog).id;
    destroyed.shipDamage = tuning::damage::destroyedShipDamage;
    applyLaunchOutcome(restored, catalog, destroyed);
    require(restored.run.surfaceUpgradeIds.empty(), "surface upgrades should clear immediately when the current ship is destroyed");

    restored.run.surfaceUpgradeIds = state.run.surfaceUpgradeIds;
    startNewExpedition(restored, catalog);
    require(restored.run.surfaceUpgradeIds.empty(), "surface upgrades should clear when the current ship/run is replaced");
}

void surfaceUpgradesResetOnEmergencyRecallOnly()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState brokenBit = createNewGame(catalog, 648);
    brokenBit.run.destinationIndex = 2;
    startSurfaceExpedition(brokenBit, catalog);
    prepareMiningSiteForTest(brokenBit);
    brokenBit.run.surfaceUpgradeIds = {content::surfaceUpgrade::shockMounts};
    require(startMiningRun(brokenBit, catalog).applied, "mining should start for drill break upgrade test");
    brokenBit.run.mining.drillIntegrity = 0.0;
    updateMiningRun(brokenBit, catalog, 0.08);
    require(!brokenBit.run.mining.failurePending, "broken drill bit should not force emergency recall");
    require(brokenBit.run.surfaceUpgradeIds.size() == 1 && brokenBit.run.surfaceUpgradeIds.front() == content::surfaceUpgrade::shockMounts, "field upgrades should survive a broken drill bit");

    GameState recalled = createNewGame(catalog, 649);
    recalled.run.destinationIndex = 2;
    startSurfaceExpedition(recalled, catalog);
    prepareMiningSiteForTest(recalled);
    recalled.run.surfaceUpgradeIds = {
        content::surfaceUpgrade::shockMounts,
        content::surfaceUpgrade::oreHopper
    };
    require(startMiningRun(recalled, catalog).applied, "mining should start for emergency recall upgrade test");
    recalled.run.mining.droneHealth = 0.0;
    updateMiningRun(recalled, catalog, 0.08);
    require(recalled.run.mining.failurePending, "zero drone health should force emergency recall");
    require(finishMiningRun(recalled, catalog, true).applied, "emergency recall should be acknowledgeable");
    require(recalled.run.surfaceUpgradeIds.empty(), "field upgrades should reset on emergency recall");
}

void miningDepletionAtShipGracefullyEndsRun()
{
    const ContentCatalog catalog = createDefaultContent();
    auto startParkedAtShip = [&catalog](GameState& state, int seed) {
        state = createNewGame(catalog, seed);
        state.run.destinationIndex = 2;
        startSurfaceExpedition(state, catalog);
        prepareMiningSiteForTest(state);
        require(startMiningRun(state, catalog).applied, "mining run should start for depletion-at-ship test");
        state.run.mining.droneX = state.run.mining.returnZoneX;
        state.run.mining.droneY = state.run.mining.returnZoneY;
    };

    GameState oxygen;
    startParkedAtShip(oxygen, 650);
    oxygen.run.mining.oxygenSeconds = 0.01;
    updateMiningRun(oxygen, catalog, 0.08);
    require(oxygen.screen == Screen::SurfaceExpedition, "oxygen depletion at the ship should return to surface ops");
    require(!oxygen.run.mining.active, "oxygen depletion at the ship should finish the mining run");
    require(!oxygen.run.mining.failurePending, "oxygen depletion at the ship should not start failure recall");
    require(oxygen.statusLine.find("Emergency recall") == std::string::npos, "oxygen depletion at the ship should not report emergency recall");

    GameState fuel;
    startParkedAtShip(fuel, 651);
    fuel.run.surfaceExpedition.sharedFuel = 0;
    fuel.run.mining.oxygenSeconds = 10.0;
    fuel.run.mining.fuelBurnSeconds = tuning::mining::fuelSecondsPerUnit;
    updateMiningRun(fuel, catalog, 0.08);
    require(fuel.screen == Screen::SurfaceExpedition, "fuel depletion at the ship should return to surface ops");
    require(!fuel.run.mining.active, "fuel depletion at the ship should finish the mining run");
    require(!fuel.run.mining.failurePending, "fuel depletion at the ship should not start failure recall");
    require(fuel.statusLine.find("Emergency recall") == std::string::npos, "fuel depletion at the ship should not report emergency recall");
}

void droneBayUnlocksSlotsLoadoutsAndMiningEffects()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState locked = createNewGame(catalog, 647);
    ensureDroneBayState(locked, catalog);
    require(locked.meta.droneBaySlots == 0, "locked drone bay should not expose slots");
    require(locked.meta.ownedDroneIds.empty(), "locked drone bay should not seed owned drones");

    GameState state = createNewGame(catalog, 648);
    state.run.destinationIndex = 2;
    activateOnlyCrew(state, content::astronaut::marco);
    startSurfaceExpedition(state, catalog);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.materials.common = 4;
    ensureDroneBayState(state, catalog);
    require(state.meta.droneBaySlots == 1, "drone bay unlock should initialize one slot");
    require(state.meta.ownedDroneIds.size() == 4, "drone bay should seed environmental starter drones");
    require(catalog.findMiniDrone(content::drone::miningDrone) != nullptr, "mining drone id should resolve");
    require(catalog.findMiniDrone(content::drone::resourceDrone) != nullptr, "resource drone id should resolve");
    require(catalog.findMiniDrone(content::drone::surveyDrone) != nullptr, "survey drone id should resolve");
    require(catalog.findMiniDrone(content::drone::stabilizerDrone) != nullptr, "stabilizer drone id should resolve");

    const MiningDrillStats baseline = miningDrillStats(state, catalog);
    require(std::abs(baseline.oxygenSeconds - tuning::mining::oxygenSeconds) < 0.000001, "baseline mining oxygen should use the short starter tank");
    require(equipMiniDrone(state, catalog, 0), "first equipped drone should fit in the starter slot");
    require(!equipMiniDrone(state, catalog, 1), "equipped drones should not exceed slot count");
    const MiningDrillStats miningSupported = miningDrillStats(state, catalog);
    require(miningSupported.passiveDroneMiningRate > baseline.passiveDroneMiningRate, "mining drone should add passive mining support");

    state.meta.materials.common = 10;
    state.meta.materials.rare = 10;
    state.meta.materials.exotic = 10;
    require(upgradeDroneSlot(state, catalog), "material-funded slot upgrade should succeed");
    require(state.meta.droneBaySlots == 2, "slot upgrade should increase drone bay capacity");
    require(equipMiniDrone(state, catalog, 1), "second drone should fit after slot upgrade");
    const MiningDrillStats resourceSupported = miningDrillStats(state, catalog);
    require(resourceSupported.oxygenSeconds > miningSupported.oxygenSeconds, "resource drone should extend oxygen");
    const MiniDroneLoadoutEffects beforeTune = miniDroneLoadoutEffects(state, catalog);
    require(canUpgradeMiniDrone(state, catalog, 0), "funded owned drone should expose tuning");
    require(upgradeMiniDrone(state, catalog, 0), "material-funded drone tuning should succeed");
    require(miniDroneUpgradeLevel(state, content::drone::miningDrone) == 2, "tuned drone should advance to Mk II");
    const MiniDroneLoadoutEffects afterTune = miniDroneLoadoutEffects(state, catalog);
    require(afterTune.passiveMiningRate > beforeTune.passiveMiningRate, "tuned mining drone should scale passive mining output");

    for (int i = 0; i < 4; ++i) {
        state.meta.materials.common = 99;
        state.meta.materials.rare = 99;
        state.meta.materials.exotic = 99;
        upgradeDroneSlot(state, catalog);
    }
    require(state.meta.droneBaySlots == 6, "drone bay slots should cap at six");
    require(!upgradeDroneSlot(state, catalog), "maxed drone bay should reject further slot upgrades");

    const std::string serialized = serializeSaveData(captureSaveData(state));
    const auto save = deserializeSaveData(serialized);
    require(save.has_value(), "drone bay save should parse");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);
    require(restored.meta.droneBaySlots == state.meta.droneBaySlots, "drone bay slots should round trip");
    require(restored.meta.ownedDroneIds == state.meta.ownedDroneIds, "owned drones should round trip");
    require(restored.meta.equippedDroneIds == state.meta.equippedDroneIds, "equipped drones should round trip");
    require(miniDroneUpgradeLevel(restored, content::drone::miningDrone) == 2, "drone tuning should round trip");

    restored.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    ensureDroneBayState(restored, catalog);
    require(std::find(restored.meta.ownedDroneIds.begin(), restored.meta.ownedDroneIds.end(), content::drone::attackDrone) != restored.meta.ownedDroneIds.end(), "post-solar unlock should add attack drones to the owned pool");
    require(std::find(restored.meta.ownedDroneIds.begin(), restored.meta.ownedDroneIds.end(), content::drone::defenseDrone) != restored.meta.ownedDroneIds.end(), "post-solar unlock should add defense drones to the owned pool");
}

void droneOpsPresentationExposesPersistentLoadout()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 649);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.materials.common = 4;
    state.meta.materials.rare = 2;
    ensureDroneBayState(state, catalog);

    SurfaceExpeditionPresentation surface = surfaceExpeditionPresentation(state, catalog);
    require(surface.droneOpsAction.enabled, "surface ops should expose Drone Ops once the bay is unlocked");
    require(surface.droneOpsAction.actionId == std::string(ui::actions::droneOps), "Drone Ops surface action should use stable action id");

    DroneOpsPresentation drones = droneOpsPresentation(state, catalog);
    require(drones.drones.size() == catalog.miniDrones.size(), "Drone Ops should present the full drone roster");
    require(drones.drones.front().title == "Mining Drone", "Drone Ops should present mining support first");
    require(drones.drones.front().action.enabled, "owned starter drones should be equippable");
    require(drones.drones.front().upgradeAction.enabled, "funded starter drones should be upgradable");
    require(drones.drones.front().upgradeSummary.find("Mk 1") != std::string::npos, "drone cards should show current tuning level");
    require(drones.drones.front().upgradeSummary.find("-> Mk 2") != std::string::npos, "drone cards should preview the next tuning tier");
    require(drones.drones.front().upgradeSummary.find("auto-mine") != std::string::npos, "drone tuning previews should explain the next payoff");
    require(!drones.buildRecipes.empty(), "Drone Ops should expose build recipes");
    require(drones.loadoutSlots.size() == 6, "Drone Ops should present the full six-slot loadout bench");
    require(std::any_of(drones.loadoutSlots.begin(), drones.loadoutSlots.end(), [](const DroneLoadoutSlotPresentation& slot) {
        return slot.title == "Open slot" && slot.status == "Ready";
    }), "Drone Ops loadout bench should show open bay slots");
    require(std::any_of(drones.loadoutSlots.begin(), drones.loadoutSlots.end(), [](const DroneLoadoutSlotPresentation& slot) {
        return slot.title == "Locked slot" && slot.status.find("upgrade") != std::string::npos;
    }), "Drone Ops loadout bench should show the next locked bay slot upgrade");
    require(std::any_of(drones.buildRecipes.begin(), drones.buildRecipes.end(), [](const DroneBuildRecipePresentation& recipe) {
        return recipe.title == "Targeting Grid" && recipe.requirements.find("Attack") != std::string::npos;
    }), "Drone Ops recipes should explain required drone roles");
    require(std::any_of(drones.buildGuidanceChips.begin(), drones.buildGuidanceChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Recipe" && !chip.value.empty();
    }), "Drone Ops should expose a next build recipe target");
    require(std::any_of(drones.buildGuidanceChips.begin(), drones.buildGuidanceChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Upgrade" && !chip.value.empty();
    }), "Drone Ops should expose an upgrade priority for the active build");
    require(drones.upgradeSlotAction.enabled, "funded first slot upgrade should be offered when materials allow");
    require(std::any_of(drones.details.begin(), drones.details.end(), [](const DetailPresentationRow& row) {
        return row.label == "Passive combat plan" && row.value.find("auto-fire") != std::string::npos;
    }), "Drone Ops details should frame combat as passive mini-drone abilities");

    state.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    ensureDroneBayState(state, catalog);
    drones = droneOpsPresentation(state, catalog);
    const auto attackDrone = std::find_if(drones.drones.begin(), drones.drones.end(), [](const MiniDroneCardPresentation& drone) {
        return drone.title == "Attack Drone";
    });
    require(attackDrone != drones.drones.end(), "Drone Ops should show the attack drone card after perimeter drones unlock");
    require(attackDrone->detail.find("Auto-fires") != std::string::npos, "attack drone card should describe passive auto-fire behavior");
    require(attackDrone->upgradeSummary.find("shot power") != std::string::npos, "attack drone tuning preview should highlight combat damage payoff");
    require(std::any_of(attackDrone->effectChips.begin(), attackDrone->effectChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Crits";
    }), "attack drone chips should expose crit payoff");

    state.meta.droneBaySlots = 3;
    state.meta.equippedDroneIds = {content::drone::attackDrone, content::drone::defenseDrone, content::drone::surveyDrone};
    state.meta.droneUpgrades = {{content::drone::attackDrone, 2}, {content::drone::defenseDrone, 1}, {content::drone::surveyDrone, 1}};
    drones = droneOpsPresentation(state, catalog);
    require(std::any_of(drones.loadoutSlots.begin(), drones.loadoutSlots.end(), [](const DroneLoadoutSlotPresentation& slot) {
        return slot.title == "Attack Drone" && slot.role == "Attack" && slot.detail.find("Mk 2") != std::string::npos;
    }), "Drone Ops loadout bench should show equipped tuned attack drones");
    require(std::any_of(drones.loadoutSlots.begin(), drones.loadoutSlots.end(), [](const DroneLoadoutSlotPresentation& slot) {
        return slot.title == "Defense Drone" && slot.cssClass.find("role-defense") != std::string::npos;
    }), "Drone Ops loadout bench should style equipped drone roles");
    const MiniDroneLoadoutEffects synergyEffects = miniDroneLoadoutEffects(state, catalog);
    require(std::find(synergyEffects.synergyNames.begin(), synergyEffects.synergyNames.end(), "Targeting Grid") != synergyEffects.synergyNames.end(), "attack plus survey should unlock the Targeting Grid synergy");
    require(std::find(synergyEffects.synergyNames.begin(), synergyEffects.synergyNames.end(), "Killbox Screen") != synergyEffects.synergyNames.end(), "attack plus defense should unlock the Killbox Screen synergy");
    require(synergyEffects.sentryVolleyBonus >= 1, "Killbox Screen should add an extra sentry target");
    require(synergyEffects.alliedCritChanceBonus > 0.0, "Targeting Grid should improve drone crit chance");
    require(synergyEffects.signatureName == "Sentry Killbox", "attack defense survey should activate the Sentry Killbox signature");
    require(synergyEffects.signatureKind == MiniDroneSignatureKind::SentryKillbox, "Sentry Killbox should expose a matching render style identity");
    require(synergyEffects.signatureTier == 2, "signature builds should expose a visual strength tier");
    require(synergyEffects.sentryVolleyBonus == 2, "Sentry Killbox should add a signature volley on top of Killbox Screen");
    state.meta.equippedDroneIds = {content::drone::attackDrone, content::drone::miningDrone, content::drone::resourceDrone};
    const MiniDroneLoadoutEffects excavationEffects = miniDroneLoadoutEffects(state, catalog);
    require(excavationEffects.signatureKind == MiniDroneSignatureKind::ExcavationStorm, "Excavation Storm should expose a distinct render style identity");
    state.meta.equippedDroneIds = {content::drone::defenseDrone, content::drone::stabilizerDrone, content::drone::resourceDrone};
    const MiniDroneLoadoutEffects fortressEffects = miniDroneLoadoutEffects(state, catalog);
    require(fortressEffects.signatureKind == MiniDroneSignatureKind::FortressRig, "Fortress Rig should expose a distinct render style identity");
    state.meta.equippedDroneIds = {content::drone::miningDrone, content::drone::resourceDrone, content::drone::surveyDrone};
    const MiniDroneLoadoutEffects pathfinderEffects = miniDroneLoadoutEffects(state, catalog);
    require(pathfinderEffects.signatureKind == MiniDroneSignatureKind::RelicPathfinder, "Relic Pathfinder should expose a distinct render style identity");
    state.meta.droneBaySlots = 6;
    state.meta.equippedDroneIds = {
        content::drone::attackDrone,
        content::drone::defenseDrone,
        content::drone::surveyDrone,
        content::drone::miningDrone,
        content::drone::resourceDrone,
        content::drone::stabilizerDrone
    };
    const MiniDroneLoadoutEffects spectrumEffects = miniDroneLoadoutEffects(state, catalog);
    require(spectrumEffects.signatureKind == MiniDroneSignatureKind::FullSpectrumSwarm, "Full Spectrum Swarm should expose the capstone render style identity");
    state.meta.droneBaySlots = 3;
    state.meta.equippedDroneIds = {content::drone::attackDrone, content::drone::defenseDrone, content::drone::surveyDrone};
    drones = droneOpsPresentation(state, catalog);
    require(drones.buildTitle.find("Sentry Killbox") != std::string::npos, "Drone Ops build strip should name the active signature");
    require(std::any_of(drones.buildRecipes.begin(), drones.buildRecipes.end(), [](const DroneBuildRecipePresentation& recipe) {
        return recipe.title == "Sentry Killbox" && recipe.active && recipe.signature;
    }), "Drone Ops recipe board should mark active signature builds");
    require(std::any_of(drones.buildChips.begin(), drones.buildChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Active synergies" && chip.value == "2";
    }), "Drone Ops build strip should count active synergies");
    require(std::any_of(drones.buildChips.begin(), drones.buildChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Signature" && chip.value == "Sentry Killbox";
    }), "Drone Ops build strip should expose signature build identity");
    require(std::any_of(drones.buildChips.begin(), drones.buildChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Upgraded drones" && chip.value == "1";
    }), "Drone Ops build strip should count upgraded drones");
    require(std::any_of(drones.buildGuidanceChips.begin(), drones.buildGuidanceChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Recipe" && chip.value == "Excavation Barrage";
    }), "Drone Ops build guidance should name the closest inactive recipe");
    require(std::any_of(drones.buildGuidanceChips.begin(), drones.buildGuidanceChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Missing" && chip.value.find("Mining") != std::string::npos;
    }), "Drone Ops build guidance should explain the missing role for the next recipe");
    require(std::any_of(drones.details.begin(), drones.details.end(), [](const DetailPresentationRow& row) {
        return row.label == "Build guidance" && row.value.find("ore") != std::string::npos;
    }), "Drone Ops details should explain why the recommended build target matters");
    require(std::any_of(drones.forecastChips.begin(), drones.forecastChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Cadence" && chip.value.find("s") != std::string::npos;
    }), "Drone Ops combat forecast should expose passive shot cadence");
    require(std::any_of(drones.forecastChips.begin(), drones.forecastChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Sentry output" && chip.value.find("/s") != std::string::npos;
    }), "Drone Ops combat forecast should expose passive sentry output");
    require(std::any_of(drones.forecastChips.begin(), drones.forecastChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Shield relief" && chip.value != "None";
    }), "Drone Ops combat forecast should expose shield payoff for defense builds");
    require(std::any_of(drones.forecastChips.begin(), drones.forecastChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Build state" && chip.value == "Signature";
    }), "Drone Ops combat forecast should identify signature build state");
    require(std::any_of(drones.drones.begin(), drones.drones.end(), [](const MiniDroneCardPresentation& drone) {
        return drone.title == "Survey Drone" && drone.buildHook.find("Sentry Killbox") != std::string::npos;
    }), "Drone cards should explain which builds they help unlock");
    require(std::any_of(drones.drones.begin(), drones.drones.end(), [](const MiniDroneCardPresentation& drone) {
        return drone.title == "Attack Drone" && std::any_of(drone.effectChips.begin(), drone.effectChips.end(), [](const PanelMetricPresentation& chip) {
            return chip.label == "Upgrade" && chip.value == "Mk 2";
        });
    }), "tuned drone cards should show Mk-scaled chip presentation");

    state.meta.materials = {};
    drones = droneOpsPresentation(state, catalog);
    require(!drones.upgradeSlotAction.enabled, "unfunded slot upgrade should be disabled");
    require(drones.upgradeSlotAction.label.find("Need") != std::string::npos, "unfunded slot upgrade should show material need");
}

void surfaceSiteProfilesChangeExpeditionRules()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState survey = createNewGame(catalog, 1);
    survey.run.destinationIndex = 2;
    startSurfaceExpedition(survey, catalog);
    require(survey.run.surfaceExpedition.siteProfile == SurfaceSiteProfile::SurveyBasin, "seeded fallback should generate survey basin profile");
    require(surfaceSiteProfileName(survey.run.surfaceExpedition.siteProfile) == text::panel::surfaceSites::surveyBasin, "survey basin profile should have shared display text");
    Random surveyRng(1001);
    const SurfaceActionOutcome surveyOutcome = surveySurfaceSite(survey, surveyRng);
    require(surveyOutcome.materialDelta.common >= tuning::research::surveyCommonGain + tuning::research::siteSurveyBasinSurveyBonus, "survey basin should improve survey returns");

    GameState ore = createNewGame(catalog, 2);
    ore.run.destinationIndex = 2;
    startSurfaceExpedition(ore, catalog);
    require(ore.run.surfaceExpedition.siteProfile == SurfaceSiteProfile::OreShelf, "seeded fallback should generate ore shelf profile");
    Random oreRng(1002);
    const SurfaceActionOutcome mineOutcome = mineSurfaceDeposit(ore, oreRng);
    require(mineOutcome.materialDelta.common >= tuning::research::mineCommonGain + tuning::research::siteOreShelfMineBonus, "ore shelf should improve mining returns");

    GameState fracture = createNewGame(catalog, 3);
    fracture.run.destinationIndex = 2;
    startSurfaceExpedition(fracture, catalog);
    require(fracture.run.surfaceExpedition.siteProfile == SurfaceSiteProfile::FractureField, "seeded fallback should generate fracture field profile");
    GameState surveyRisk = createNewGame(catalog, 1);
    surveyRisk.run.destinationIndex = 2;
    startSurfaceExpedition(surveyRisk, catalog);
    fracture.run.surfaceExpedition.cargo = 6;
    surveyRisk.run.surfaceExpedition.cargo = 6;
    require(surfaceExtractionRisk(fracture) > surfaceExtractionRisk(surveyRisk), "fracture field should raise extraction pressure");
}

void surfaceHazardsCreateEnvironmentalSetbacks()
{
    const ContentCatalog catalog = createDefaultContent();

    auto triggerSurveyHazard = [&catalog]() {
        for (int seed = 1; seed < 400; ++seed) {
            GameState state = createNewGame(catalog, seed);
            state.run.destinationIndex = 2;
            startSurfaceExpedition(state, catalog);
            state.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::SurveyBasin;
            state.run.surfaceExpedition.hazard = 10.0;
            const int supplyBefore = state.run.surfaceExpedition.supply;
            Random rng(seed);
            const SurfaceActionOutcome outcome = surveySurfaceSite(state, rng);
            if (outcome.hazardTriggered) {
                require(outcome.hazardMessage == std::string(text::status::surfaceDustHazard), "survey hazard should report dust interference");
                require(outcome.hazardDelta > 0.0, "survey hazard should raise site hazard");
                require(outcome.supplyDelta == -(tuning::research::surveySupplyCost + tuning::research::dustHazardSupplyLoss), "survey hazard should spend extra supply when possible");
                require(state.run.surfaceExpedition.supply == supplyBefore + outcome.supplyDelta, "survey hazard supply delta should match expedition state");
                return true;
            }
        }
        return false;
    };

    auto triggerMineHazard = [&catalog]() {
        for (int seed = 1; seed < 400; ++seed) {
            GameState state = createNewGame(catalog, seed);
            state.run.destinationIndex = 2;
            startSurfaceExpedition(state, catalog);
            state.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::SurveyBasin;
            state.run.surfaceExpedition.hazard = 10.0;
            Random rng(seed);
            const SurfaceActionOutcome outcome = mineSurfaceDeposit(state, rng);
            if (outcome.hazardTriggered) {
                require(outcome.hazardMessage == std::string(text::status::surfaceDrillHazard), "mine hazard should report drill chatter");
                require(outcome.cargoDelta == 3, "mine hazard should reduce the net cargo delta");
                return true;
            }
        }
        return false;
    };

    auto triggerPushHazard = [&catalog]() {
        for (int seed = 1; seed < 400; ++seed) {
            GameState state = createNewGame(catalog, seed);
            state.run.destinationIndex = 2;
            startSurfaceExpedition(state, catalog);
            state.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::SurveyBasin;
            state.run.surfaceExpedition.hazard = 10.0;
            Random rng(seed);
            const SurfaceActionOutcome outcome = pushSurfaceDeeper(state, rng);
            if (outcome.hazardTriggered) {
                require(outcome.hazardMessage == std::string(text::status::surfaceTerrainHazard), "push hazard should report terrain instability");
                require(outcome.hazardDelta == tuning::research::unstableTerrainHazardIncrease, "push hazard should report the extra terrain hazard");
                return true;
            }
        }
        return false;
    };

    require(triggerSurveyHazard(), "high-hazard surveys should be able to trigger environmental setbacks");
    require(triggerMineHazard(), "high-hazard mining should be able to trigger environmental setbacks");
    require(triggerPushHazard(), "high-hazard deeper pushes should be able to trigger environmental setbacks");
}

void surfaceEventsCreateSmallRunVariation()
{
    const ContentCatalog catalog = createDefaultContent();

    auto triggerEvent = [&catalog](SurfaceEventType expected) {
        for (int seed = 1; seed < 8000; ++seed) {
            GameState state = createNewGame(catalog, seed);
            state.run.destinationIndex = 2;
            startSurfaceExpedition(state, catalog);
            state.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::SurveyBasin;
            state.run.surfaceExpedition.hazard = 0.0;
            const int supplyBefore = state.run.surfaceExpedition.supply;
            const int blueprintsBefore = state.meta.blueprintProgress;
            Random rng(seed);
            const SurfaceActionOutcome outcome = surveySurfaceSite(state, rng);
            if (outcome.eventType != expected) {
                continue;
            }

            require(!outcome.hazardTriggered, "surface events should not stack on top of hazards");
            require(!outcome.eventMessage.empty(), "surface event should report a player-facing message");
            if (expected == SurfaceEventType::EquipmentFailure) {
                require(outcome.eventMessage == std::string(text::status::surfaceEquipmentFailure), "equipment event should use shared status text");
                require(state.run.surfaceExpedition.supply == supplyBefore + outcome.supplyDelta, "equipment event supply delta should match expedition state");
                require(outcome.supplyDelta == -(tuning::research::surveySupplyCost + tuning::research::surfaceEquipmentFailureSupplyLoss), "equipment event should consume spare supply");
            } else if (expected == SurfaceEventType::UnexpectedDeposit) {
                require(outcome.eventMessage == std::string(text::status::surfaceUnexpectedDeposit), "deposit event should use shared status text");
                require(outcome.materialDelta.common >= tuning::research::surveyCommonGain + tuning::research::siteSurveyBasinSurveyBonus + tuning::research::surfaceDepositCommonGain, "deposit event should add material yield");
            } else if (expected == SurfaceEventType::CrewDiscovery) {
                require(outcome.eventMessage == std::string(text::status::surfaceCrewDiscovery), "crew discovery event should use shared status text");
                require(outcome.blueprintDelta == tuning::research::surfaceCrewDiscoveryBlueprintGain, "crew discovery should report blueprint gain");
                require(state.meta.blueprintProgress == blueprintsBefore + outcome.blueprintDelta, "crew discovery should bank blueprint progress");
            }
            return true;
        }
        return false;
    };

    require(triggerEvent(SurfaceEventType::EquipmentFailure), "surface actions should sometimes trigger equipment failure events");
    require(triggerEvent(SurfaceEventType::UnexpectedDeposit), "surface actions should sometimes trigger unexpected deposit events");
    require(triggerEvent(SurfaceEventType::CrewDiscovery), "surface actions should sometimes trigger crew discovery events");
}

void enemyContactStartsBeyondSolarSystemAndCanBeMitigated()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState mars = createNewGame(catalog, 1201);
    mars.run.destinationIndex = 2;
    startSurfaceExpedition(mars, catalog);
    require(!mars.run.surfaceExpedition.enemyEncountersEnabled, "Mars expedition should not enable enemy contact");
    require(surfaceEnemyEncounterChance(mars) == 0.0, "solar-system expeditions should have no contact risk");

    GameState nearbyStar = createNewGame(catalog, 1202);
    nearbyStar.run.destinationIndex = 4;
    nearbyStar.meta.unlockKeys.push_back(content::unlock::deepSpace);
    startSurfaceExpedition(nearbyStar, catalog);
    nearbyStar.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::SurveyBasin;
    nearbyStar.run.surfaceExpedition.hazard = 0.0;
    nearbyStar.run.surfaceExpedition.supply = 20;
    const double baselineRisk = surfaceEnemyEncounterChance(nearbyStar);
    require(nearbyStar.run.surfaceExpedition.enemyEncountersEnabled, "Nearby Star expedition should enable enemy contact");
    require(baselineRisk > 0.0, "Nearby Star expedition should expose contact risk");

    GameState defended = nearbyStar;
    defended.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    require(surfaceEnemyEncounterChance(defended) < baselineRisk, "perimeter drones should reduce enemy contact risk");

    auto triggerEnemyContact = [&catalog]() {
        for (int seed = 1; seed < 8000; ++seed) {
            GameState state = createNewGame(catalog, seed);
            state.run.destinationIndex = 4;
            state.meta.unlockKeys.push_back(content::unlock::deepSpace);
            startSurfaceExpedition(state, catalog);
            state.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::SurveyBasin;
            state.run.surfaceExpedition.hazard = 0.0;
            state.run.surfaceExpedition.supply = 20;
            const int supplyBefore = state.run.surfaceExpedition.supply;
            Random rng(seed);
            const SurfaceActionOutcome outcome = surveySurfaceSite(state, rng);
            if (outcome.eventType != SurfaceEventType::EnemyContact) {
                continue;
            }

            require(outcome.enemyEncounter, "enemy contact event should set the encounter flag");
            require(outcome.eventMessage == std::string(text::status::surfaceEnemyContact), "enemy contact should use shared status text");
            require(state.run.surfaceExpedition.supply == supplyBefore + outcome.supplyDelta, "enemy contact supply delta should match expedition state");
            require(outcome.supplyDelta == -(tuning::research::surveySupplyCost + tuning::research::surfaceEnemySupplyLoss), "enemy contact should consume supply in addition to the action");
            require(outcome.hazardDelta == tuning::research::surfaceEnemyHazardIncrease, "enemy contact should raise site hazard");
            return true;
        }
        return false;
    };

    require(triggerEnemyContact(), "post-solar expeditions should sometimes trigger enemy contact events");
}

void surfaceExpeditionBanksMaterialsAndDefersEnemies()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 707);
    state.run.destinationIndex = 2;
    Random rng(707);

    startSurfaceExpedition(state, catalog);
    require(state.run.surfaceExpedition.active, "Mars surface expedition should start");
    require(!state.run.surfaceExpedition.enemyEncountersEnabled, "solar-system expeditions should not enable enemies");

    const SurfaceActionOutcome survey = surveySurfaceSite(state, rng);
    const SurfaceActionOutcome mine = mineSurfaceDeposit(state, rng);
    const SurfaceActionOutcome push = pushSurfaceDeeper(state, rng);
    require(survey.applied && mine.applied && push.applied, "surface actions should consume supply while active");
    require(survey.extractionRiskDelta > 0.0, "surface survey should report extraction-risk pressure from added cargo");
    require(push.extractionRiskDelta > 0.0, "pushing deeper should report higher extraction risk");
    require(state.run.surfaceExpedition.cargo > 0, "surface actions should build a return cargo payload");
    require(surfaceExtractionRisk(state) > 0.0, "surface extraction should expose a nonzero recovery risk");

    const SurfaceActionOutcome extraction = extractSurfacePayload(state, rng);
    require(extraction.applied, "surface extraction should resolve");
    require(!state.run.surfaceExpedition.active, "extraction should end the active surface expedition");
    require(state.meta.materials.common > 0, "extraction should bank at least partial material progress");
    require(surfaceActionSummary(extraction).find("Common mats") != std::string::npos, "surface extraction summary should show banked materials");
    require(surfaceActionSummary(extraction).find("Extraction risk") != std::string::npos, "surface extraction summary should show resolved extraction risk");

    GameState deepSpace = createNewGame(catalog, 808);
    deepSpace.run.destinationIndex = 4;
    startSurfaceExpedition(deepSpace, catalog);
    require(deepSpace.run.surfaceExpedition.enemyEncountersEnabled, "enemy encounters should wait until the Nearby Star tier");
}

void surfaceExpeditionRoundTripsThroughSave()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 9090);
    state.run.destinationIndex = 2;
    state.screen = Screen::SurfaceExpedition;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.sharedFuel = 2;
    state.run.surfaceExpedition.prospectMaterials = {.common = 1, .rare = 2, .exotic = 1};
    state.run.surfaceExpedition.prospectArtifacts = 1;
    state.run.surfaceExpedition.depthProspects.push_back({1, 1, {.common = 0, .rare = 1, .exotic = 1}, 1});
    Random rng(9091);
    require(surveySurfaceSite(state, rng).applied, "test setup should gather a surface payload");

    const std::string text = serializeSaveData(captureSaveData(state));
    const auto save = deserializeSaveData(text);
    require(save.has_value(), "surface expedition save should parse");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);

    require(restored.screen == Screen::SurfaceExpedition, "active surface expedition screen should round trip");
    require(restored.run.surfaceExpedition.active, "active surface expedition state should round trip");
    require(restored.run.surfaceExpedition.destinationId == content::destination::mars, "surface destination should round trip");
    require(restored.run.surfaceExpedition.siteProfile == state.run.surfaceExpedition.siteProfile, "surface site profile should round trip");
    require(restored.run.surfaceExpedition.sharedFuel == 2, "surface shared fuel should round trip");
    require(restored.run.surfaceExpedition.sharedFuelCapacity == state.run.surfaceExpedition.sharedFuelCapacity, "surface shared fuel capacity should round trip");
    require(restored.run.surfaceExpedition.miningSitePrepared, "surface mining preparation should round trip");
    require(!restored.run.surfaceExpedition.miningRunUsed, "unused surface mining run should round trip");
    require(restored.run.surfaceExpedition.temporaryMaterials.common == state.run.surfaceExpedition.temporaryMaterials.common, "temporary surface materials should round trip");
    require(restored.run.surfaceExpedition.prospectMaterials.rare == 2, "surface material prospects should round trip");
    require(restored.run.surfaceExpedition.prospectArtifacts == 1, "surface artifact prospects should round trip");
    require(restored.run.surfaceExpedition.depthProspects.size() == 1, "surface depth prospects should round trip");
    require(restored.run.surfaceExpedition.depthProspects.front().absoluteDepth == 1, "surface depth prospect layer should round trip");
    require(restored.run.surfaceExpedition.depthProspects.front().possibleMaterials.exotic == 1, "surface depth prospect materials should round trip");
    require(restored.run.surfaceExpedition.depthProspects.front().possibleArtifacts == 1, "surface depth prospect artifacts should round trip");
    require(restored.run.surfaceExpedition.logEntries == state.run.surfaceExpedition.logEntries, "surface mission log should round trip");
}

void surfaceMiningUsesSharedFuelAndRunsOnce()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92928);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);

    const int supplyBeforeMining = state.run.surfaceExpedition.supply;
    const int fuelBeforeMining = state.run.surfaceExpedition.sharedFuel;
    const SurfaceActionOutcome started = startMiningRun(state, catalog);
    require(started.applied, "fresh surface mining should start when shared fuel is available");
    require(state.run.surfaceExpedition.supply == supplyBeforeMining, "starting mining should not spend action kits");
    require(state.run.surfaceExpedition.sharedFuel == fuelBeforeMining - 1, "starting mining should spend one shared fuel");
    require(state.run.surfaceExpedition.miningRunUsed, "starting mining should consume the one mining run for this loop");

    state.screen = Screen::SurfaceExpedition;
    state.run.mining.active = false;
    const int supplyAfterMining = state.run.surfaceExpedition.supply;
    const int depthAfterMining = state.run.surfaceExpedition.depth;
    Random rng(92928);
    const SurfaceActionOutcome pushAfterMining = pushSurfaceDeeper(state, rng);
    require(!pushAfterMining.applied, "pushing deeper should be blocked after the mining run is used");
    require(state.run.surfaceExpedition.supply == supplyAfterMining, "blocked post-mining push should not spend action kits");
    require(state.run.surfaceExpedition.depth == depthAfterMining, "blocked post-mining push should not increase depth");
    require(surfaceActionSummary(pushAfterMining).find("Extract before pushing deeper") != std::string::npos, "blocked post-mining push should explain extraction");

    const SurfaceActionOutcome surveyAfterMining = surveySurfaceSite(state, rng);
    require(!surveyAfterMining.applied, "surveying should be blocked after the mining run is used");
    require(state.run.surfaceExpedition.supply == supplyAfterMining, "blocked post-mining survey should not spend action kits");
    require(surfaceActionSummary(surveyAfterMining).find("Extract before surveying") != std::string::npos, "blocked post-mining survey should explain extraction");

    const SurfaceActionOutcome scanAfterMining = startSurfaceScanRun(state, rng);
    require(!scanAfterMining.applied, "surface scan should be blocked after the mining run is used");
    require(state.run.surfaceExpedition.supply == supplyAfterMining, "blocked post-mining scan should not spend action kits");
    require(surfaceActionSummary(scanAfterMining).find("Extract before surveying") != std::string::npos, "blocked post-mining scan should explain extraction");

    const SurfaceActionOutcome repeated = startMiningRun(state, catalog);
    require(!repeated.applied, "mining should only start once per surface loop");
    require(surfaceActionSummary(repeated).find("already used") != std::string::npos, "repeat mining should explain the one-run limit");
}

void physicalMiningArtifactsAreSingleAndDeliveryGated()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92929);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    state.run.surfaceExpedition.prospectArtifacts = 3;

    require(startMiningRun(state, catalog).applied, "prospected artifact should start a mining run");
    require(state.run.mining.artifact.present, "prospected artifact should create one physical artifact object");
    int artifactTiles = 0;
    for (const MiningCell& cell : state.run.mining.terrain.cells) {
        artifactTiles += cell.material == MiningCellMaterial::ArtifactCache ? 1 : 0;
    }
    require(artifactTiles <= 1, "mining terrain should contain at most one artifact cache tile");
    require(state.run.mining.temporaryArtifacts.empty(), "artifact should not enter payload before delivery");

    MiningArtifactObject& artifact = state.run.mining.artifact;
    artifact.revealed = true;
    state.run.mining.droneX = artifact.x;
    state.run.mining.droneY = artifact.y - 2.0;
    state.run.mining.aimDirX = 0.0;
    state.run.mining.aimDirY = 1.0;
    state.run.mining.drilling = true;
    const double healthBeforeDrill = artifact.health;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.artifact.health < healthBeforeDrill, "drilling an artifact cache should damage the artifact");
    require(state.run.mining.temporaryArtifacts.empty(), "drilling should still not recover the artifact directly");

    state.run.mining.drilling = false;
    state.run.mining.artifact.state = MiningArtifactState::Loose;
    state.run.mining.artifact.tethered = true;
    state.run.mining.artifact.x = state.run.mining.returnZoneX;
    state.run.mining.artifact.y = state.run.mining.returnZoneY;
    state.run.mining.droneX = state.run.mining.artifact.x;
    state.run.mining.droneY = state.run.mining.artifact.y;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.artifact.state == MiningArtifactState::Delivered, "tethered artifact should deliver at the ship zone");
    require(state.run.mining.temporaryArtifacts.empty(), "delivered artifact should not stay carried after auto-bank");
    require(state.run.mining.stowedArtifacts.size() == 1, "delivered artifact should bank at the ship");
    require(state.run.mining.stowedCargo >= tuning::mining::artifactCargo, "delivered artifact should add banked cargo weight");
}

void miningArtifactTetherAndDestructionRules()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92930);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    state.run.surfaceExpedition.prospectArtifacts = 1;
    require(startMiningRun(state, catalog).applied, "artifact tether test should start mining");

    MiningArtifactObject& artifact = state.run.mining.artifact;
    artifact.revealed = true;
    state.run.mining.droneX = artifact.x;
    state.run.mining.droneY = artifact.y - 1.0;
    toggleMiningTether(state);
    require(state.run.mining.artifact.tethered, "T should attach to a nearby exposed artifact");
    toggleMiningTether(state);
    require(!state.run.mining.artifact.tethered, "T should detach an attached artifact");

    state.run.mining.artifact.state = MiningArtifactState::Destroyed;
    state.run.mining.artifact.tethered = true;
    state.run.mining.artifact.x = state.run.mining.returnZoneX;
    state.run.mining.artifact.y = state.run.mining.returnZoneY;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.temporaryArtifacts.empty(), "destroyed artifact should not deliver");
}

void miningArtifactRewardsResolveOnExtraction()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState story = createNewGame(catalog, 92931);
    story.run.destinationIndex = 4;
    startSurfaceExpedition(story, catalog);
    story.run.surfaceExpedition.hazard = 0.0;
    story.run.surfaceExpedition.cargo = 0;
    story.run.surfaceExpedition.supply = 10;
    story.run.surfaceExpedition.sharedFuel = story.run.surfaceExpedition.sharedFuelCapacity;
    story.run.surfaceExpedition.temporaryArtifacts.push_back({"story_artifact", content::destination::nearbyStar, false, ArtifactKind::Story, ArtifactRewardType::None, 1.0, false});
    story.meta.ark.condition = ArkCondition::DamagedStranded;
    story.meta.ark.hullDamage = 72;
    Random storyRng(1);
    const SurfaceActionOutcome storyOutcome = extractSurfacePayload(story, storyRng);
    require(storyOutcome.cargoRecovered, "story artifact test should recover cargo");
    require(story.meta.ark.repairProgress == tuning::mining::artifactStoryArkRepair, "story artifact should add Ark repair progress");
    require(story.meta.ark.hullDamage == 72 - tuning::mining::artifactStoryHullRepair, "story artifact should repair Ark hull damage");
    require(!story.meta.artifacts.empty() && story.meta.artifacts.front().rewardApplied, "story artifact reward should be marked applied");

    GameState boost = createNewGame(catalog, 92932);
    boost.run.destinationIndex = 2;
    startSurfaceExpedition(boost, catalog);
    boost.run.surfaceExpedition.hazard = 0.0;
    boost.run.surfaceExpedition.cargo = 0;
    boost.run.surfaceExpedition.supply = 10;
    boost.run.surfaceExpedition.sharedFuel = boost.run.surfaceExpedition.sharedFuelCapacity;
    boost.run.surfaceExpedition.temporaryArtifacts.push_back({"boost_artifact", content::destination::mars, false, ArtifactKind::Boost, ArtifactRewardType::Credits, 1.0, false});
    const double creditsBefore = boost.run.credits;
    Random boostRng(2);
    const SurfaceActionOutcome boostOutcome = extractSurfacePayload(boost, boostRng);
    require(boostOutcome.cargoRecovered, "boost artifact test should recover cargo");
    require(boost.run.credits > creditsBefore, "credit artifact should grant credits after extraction");
    require(!boost.meta.artifacts.empty() && boost.meta.artifacts.front().rewardApplied, "boost artifact reward should be marked applied");
}

void miningArtifactSaveRoundTrips()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92933);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    state.run.surfaceExpedition.prospectArtifacts = 1;
    require(startMiningRun(state, catalog).applied, "artifact save test should start mining");
    state.run.mining.artifact.state = MiningArtifactState::Loose;
    state.run.mining.artifact.tethered = true;
    state.run.mining.artifact.health = 0.64;
    state.run.mining.artifact.velocityX = 1.2;
    state.run.mining.artifact.velocityY = -0.4;
    state.meta.ark.repairProgress = 2;

    const std::string saveText = serializeSaveData(captureSaveData(state));
    const std::optional<SaveData> save = deserializeSaveData(saveText);
    require(save.has_value(), "artifact save should deserialize");
    GameState restored = createNewGame(catalog, 92934);
    restoreSaveData(restored, catalog, *save);
    require(restored.run.mining.artifact.present, "active mining artifact should round trip");
    require(restored.run.mining.artifact.state == MiningArtifactState::Loose, "artifact state should round trip");
    require(restored.run.mining.artifact.tethered, "artifact tether state should round trip");
    require(std::abs(restored.run.mining.artifact.health - 0.64) < 0.0001, "artifact health should round trip");
    require(restored.meta.ark.repairProgress == 2, "Ark repair progress should round trip");
}

void surfaceScanMiniGameBanksSurveyPayload()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94101);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    Random rng(94102);

    const int supplyBefore = state.run.surfaceExpedition.supply;
    const SurfaceActionOutcome started = startSurfaceScanRun(state, rng);
    require(started.applied, "surface scan mini-game should start from Surface Ops");
    require(state.screen == Screen::SurfaceScan, "starting scan should move to the scan screen");
    require(state.run.surfaceExpedition.supply == supplyBefore - tuning::research::surveySupplyCost, "starting scan should spend the survey action kit");

    state.run.surfaceScan.bustRisk = 0.0;
    const SurfaceActionOutcome pulse = pulseSurfaceScan(state, rng);
    require(pulse.applied, "scan pulse should resolve while scan is active");
    require(state.run.surfaceScan.temporaryMaterials.common > 0, "scan pulse should stage common materials");

    const SurfaceActionOutcome banked = bankSurfaceScan(state);
    require(banked.applied, "banking scan should resolve");
    require(state.screen == Screen::SurfaceExpedition, "banking scan should return to Surface Ops");
    require(state.run.surfaceExpedition.miningSitePrepared, "banked scan should open the mining site");
    require(state.run.surfaceExpedition.temporaryMaterials.common == 0, "banked scan should not grant recovered materials before mining");
    require(state.run.surfaceExpedition.prospectMaterials.common > 0, "banked scan should tag common material prospects");

    require(startMiningRun(state, catalog).applied, "scan prospects should open the mining run");
    const bool foundProspectedOre = std::any_of(
        state.run.mining.terrain.cells.begin(),
        state.run.mining.terrain.cells.end(),
        [](const MiningCell& cell) {
            return cell.material == MiningCellMaterial::CommonOre && cell.revealed;
        });
    require(foundProspectedOre, "scan prospects should appear as revealed ore in mining terrain");
}

void surfacePushMiniGameBanksDepthRoute()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94201);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    Random rng(94202);

    const int supplyBefore = state.run.surfaceExpedition.supply;
    const SurfaceActionOutcome started = startSurfacePushRun(state, rng);
    require(started.applied, "surface push mini-game should start from Surface Ops");
    require(state.screen == Screen::SurfacePush, "starting push should move to the deep-push screen");
    require(state.run.surfaceExpedition.supply == supplyBefore - tuning::research::pushSupplyCost, "starting push should spend push action kits");

    state.run.surfacePush.collapseRisk = 0.0;
    const SurfaceActionOutcome step = pushSurfaceDepthStep(state, rng);
    require(step.applied, "deep-push step should resolve while push is active");
    require(state.run.surfacePush.depthGain > 0, "deep-push step should stage a depth gain");
    require(state.run.surfacePush.temporaryMaterials.rare > 0, "deep-push step should stage richer materials");
    require(!state.run.surfacePush.rewardMarkers.empty(), "deep-push step should record stable visual reward markers");

    const std::vector<MiningCellMaterial> markersAfterFirstStep = state.run.surfacePush.rewardMarkers;
    state.run.surfacePush.collapseRisk = 0.0;
    require(pushSurfaceDepthStep(state, rng).applied, "second deep-push step should resolve for marker stability");
    require(
        std::equal(markersAfterFirstStep.begin(), markersAfterFirstStep.end(), state.run.surfacePush.rewardMarkers.begin()),
        "later deep-push rewards should not rewrite previously displayed marker types");

    const int depthBeforeBank = state.run.surfaceExpedition.depth;
    const SurfaceActionOutcome banked = bankSurfacePush(state);
    require(banked.applied, "banking deep push should resolve");
    require(state.screen == Screen::SurfaceExpedition, "banking deep push should return to Surface Ops");
    require(state.run.surfaceExpedition.depth > depthBeforeBank, "banked deep push should increase expedition depth");
    require(state.run.surfaceExpedition.miningSitePrepared, "banked deep push should keep the mining site open");
    require(state.run.surfaceExpedition.temporaryMaterials.rare == 0, "banked deep push should not grant recovered materials before mining");
    require(state.run.surfaceExpedition.prospectMaterials.rare > 0, "banked deep push should tag richer material prospects");

    require(startMiningRun(state, catalog).applied, "deep-push prospects should open the mining run");
    const bool foundDeepProspect = std::any_of(
        state.run.mining.terrain.cells.begin(),
        state.run.mining.terrain.cells.end(),
        [](const MiningCell& cell) {
            return (cell.material == MiningCellMaterial::RareOre || cell.material == MiningCellMaterial::ArtifactCache) && cell.revealed;
        });
    require(foundDeepProspect, "deep-push prospects should appear as revealed rich targets in mining terrain");
}

void surfaceScanForecastsPushDepthLayers()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94301);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.supply = 10;
    Random rng(94302);

    require(startSurfaceScanRun(state, rng).applied, "scan forecast test should start scanning");
    state.run.surfaceScan.bustRisk = 0.0;
    require(pulseSurfaceScan(state, rng).applied, "first scan should map current layer");
    state.run.surfaceScan.bustRisk = 0.0;
    require(pulseSurfaceScan(state, rng).applied, "second scan should map first push layer");
    require(state.run.surfaceScan.depthProspects.size() == 2, "scan should record one forecast per pulse");
    require(state.run.surfaceScan.depthProspects[0].depthOffset == 0, "first scan forecast should describe the current mining layer");
    require(state.run.surfaceScan.depthProspects[1].depthOffset == 1, "second scan forecast should describe the first pushed layer");

    const SurfaceActionOutcome bankedScan = bankSurfaceScan(state);
    require(bankedScan.applied, "banked scan should preserve layer forecasts");
    require(state.run.surfaceExpedition.depthProspects.size() == 2, "banked scan should store mapped depth forecasts");
    require(state.run.surfaceExpedition.prospectMaterials.common > 0, "current-layer scan forecast should tag current mining prospects");

    const int startingDepth = state.run.surfaceExpedition.depth;
    require(startSurfacePushRun(state, rng).applied, "push should start after banked scan");
    state.run.surfacePush.collapseRisk = 0.0;
    require(pushSurfaceDepthStep(state, rng).applied, "first push should resolve against the +1 forecast");
    require(!state.run.surfacePush.rewardMarkers.empty(), "pushed layer should expose actual reward markers");
    require(
        std::all_of(state.run.surfacePush.rewardMarkerDepthOffsets.begin(), state.run.surfacePush.rewardMarkerDepthOffsets.end(), [](int offset) {
            return offset == 1;
        }),
        "actual push reward markers should line up with the pushed layer");

    const SurfaceActionOutcome bankedPush = bankSurfacePush(state);
    require(bankedPush.applied, "banked push should commit the pushed layer");
    require(state.run.surfaceExpedition.depth == startingDepth + 1, "banked push should move mining to the scanned +1 layer");
    require(state.run.surfaceExpedition.prospectMaterials.rare > 0, "banked push should replace forecast-only tags with actual pushed finds");

    require(startMiningRun(state, catalog).applied, "forecasted pushed layer should open mining");
    require(state.run.mining.depthZone == startingDepth + 1, "mining should start at the pushed layer depth");
    require(state.run.surfaceExpedition.depthProspects.empty(), "starting mining should consume banked layer forecasts");
}

void surfaceMissionLogIsBounded()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 9393);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    require(!state.run.surfaceExpedition.logEntries.empty(), "surface expedition should log the starting site profile");

    state.run.surfaceExpedition.supply = 20;
    Random rng(9394);
    for (int i = 0; i < tuning::research::surfaceLogEntryLimit + 3; ++i) {
        require(surveySurfaceSite(state, rng).applied, "surface survey should keep logging while supply remains");
    }

    require(static_cast<int>(state.run.surfaceExpedition.logEntries.size()) == tuning::research::surfaceLogEntryLimit, "surface mission log should keep only recent entries");
}

void miningTerrainIsDeterministicAndDepthScales()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91919);
    const Destination& mars = catalog.destinations[2];
    const MiningTerrain a = generateMiningTerrain(state, mars, SurfaceSiteProfile::OreShelf, 1);
    const MiningTerrain b = generateMiningTerrain(state, mars, SurfaceSiteProfile::OreShelf, 1);
    require(a.width == tuning::mining::terrainWidth && a.height == tuning::mining::terrainHeight, "mining terrain should use the active-zone dimensions");
    require(a.cells.size() == b.cells.size(), "matching mining terrain should have matching cell counts");
    for (std::size_t i = 0; i < a.cells.size(); ++i) {
        require(a.cells[i].material == b.cells[i].material, "mining terrain generation should be deterministic");
        require(std::abs(a.cells[i].maxToughness - b.cells[i].maxToughness) < 0.000001, "mining toughness should be deterministic");
    }
    require(
        miningMaterialToughness(MiningCellMaterial::CommonOre, 2) > miningMaterialToughness(MiningCellMaterial::CommonOre, 0),
        "deeper mining zones should increase terrain toughness");
    require(std::none_of(a.cells.begin(), a.cells.end(), [](const MiningCell& cell) {
        return cell.feature != MiningCellFeature::None || cell.enemy != MiningEnemyType::None;
    }), "solar-system mining terrain should not seed hostile tunnel metadata");
}

void hostileMiningTerrainGeneratesPreDugEnemyStructures()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91920);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    const Destination& nearbyGalaxy = catalog.destinations[5];

    const MiningTerrain terrain = generateMiningTerrain(state, nearbyGalaxy, SurfaceSiteProfile::OreShelf, 1);
    const MiningTerrain repeat = generateMiningTerrain(state, nearbyGalaxy, SurfaceSiteProfile::OreShelf, 1);

    int mainTunnel = 0;
    int branchTunnel = 0;
    int encounterZones = 0;
    int rooms = 0;
    int organicBurrows = 0;
    int bossChambers = 0;
    int enemyCells = 0;
    int mammalCells = 0;
    int rewardCells = 0;
    int bossRewardCells = 0;
    for (std::size_t i = 0; i < terrain.cells.size(); ++i) {
        const MiningCell& cell = terrain.cells[i];
        const MiningCell& repeated = repeat.cells[i];
        require(cell.material == repeated.material, "hostile terrain material generation should be deterministic");
        require(cell.feature == repeated.feature, "hostile tunnel features should be deterministic");
        require(cell.enemy == repeated.enemy, "hostile enemy zones should be deterministic");
        mainTunnel += cell.feature == MiningCellFeature::MainTunnel ? 1 : 0;
        branchTunnel += cell.feature == MiningCellFeature::BranchTunnel ? 1 : 0;
        encounterZones += cell.feature == MiningCellFeature::EncounterZone ? 1 : 0;
        rooms += cell.feature == MiningCellFeature::TreasureVault || cell.feature == MiningCellFeature::MinibossLair || cell.feature == MiningCellFeature::HiveNest || cell.feature == MiningCellFeature::BossChamber ? 1 : 0;
        organicBurrows += cell.feature == MiningCellFeature::OrganicBurrow ? 1 : 0;
        bossChambers += cell.feature == MiningCellFeature::BossChamber ? 1 : 0;
        enemyCells += cell.enemy != MiningEnemyType::None ? 1 : 0;
        mammalCells += cell.enemy == MiningEnemyType::Mammal ? 1 : 0;
        rewardCells += (cell.feature == MiningCellFeature::TreasureVault || cell.feature == MiningCellFeature::MinibossLair || cell.feature == MiningCellFeature::HiveNest || cell.feature == MiningCellFeature::BossChamber) && miningMaterialSolid(cell.material) ? 1 : 0;
        bossRewardCells += cell.feature == MiningCellFeature::BossChamber && miningMaterialSolid(cell.material) ? 1 : 0;
    }

    require(mainTunnel > 10, "hostile mining terrain should include pre-dug main tunnels");
    require(branchTunnel > 10, "hostile mining terrain should include branching tunnels");
    require(encounterZones > 0, "hostile mining terrain should include enemy encounter zones");
    require(rooms > 0, "hostile mining terrain should include specialized rooms");
    require(organicBurrows > 0, "hostile mammal lanes should carve organic burrows");
    require(bossChambers > 0, "hostile mammal lanes should create larger boss chambers");
    require(enemyCells > 0, "hostile mining terrain should assign enemy families to encounter structures");
    require(mammalCells > 0, "hostile boss chambers should assign mammal enemies");
    require(rewardCells > 0, "hostile specialized rooms should seed rich deposits");
    require(bossRewardCells > 0, "hostile boss chambers should seed advanced-tech deposits");
    require(miningCellFeatureName(MiningCellFeature::TreasureVault) == std::string_view("Treasure vault"), "mining feature names should describe special rooms");
    require(miningCellFeatureName(MiningCellFeature::OrganicBurrow) == std::string_view("Organic burrow"), "mining feature names should describe mammal burrows");
    require(miningEnemyTypeName(MiningEnemyType::Elemental) == std::string_view("Elemental monsters"), "mining enemy names should cover elemental threats");
}

void hostileMiningRunSpawnsEnemiesAndPassiveDefenses()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91921);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.run.destinationIndex = 4;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "hostile mining run should start");
    require(!state.run.mining.enemies.empty(), "hostile mining run should spawn enemies from encounter structures");

    MiningEnemy& attacker = state.run.mining.enemies.front();
    attacker.type = MiningEnemyType::Ant;
    attacker.x = state.run.mining.droneX + 0.2;
    attacker.y = state.run.mining.droneY;
    attacker.health = 100.0;
    attacker.maxHealth = 100.0;
    attacker.damagePerSecond = 1.0;
    attacker.speed = 0.0;
    const double healthBefore = state.run.mining.droneHealth;
    updateMiningRun(state, catalog, 1.0);
    require(state.run.mining.enemyDamageTaken > 0.0, "nearby enemies should damage the mining drone");
    require(state.run.mining.droneHealth < healthBefore, "enemy contact should reduce drone health");
    require(!state.run.mining.damageNumbers.empty(), "enemy melee hits should create rig damage numbers");

    GameState defended = createNewGame(catalog, 91922);
    defended.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    defended.meta.ark.condition = ArkCondition::DamagedStranded;
    defended.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    defended.meta.unlockKeys.push_back(content::unlock::deepSpace);
    defended.meta.unlockKeys.push_back(content::unlock::droneBay);
    defended.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    ensureDroneBayState(defended, catalog);
    defended.meta.droneBaySlots = 2;
    defended.meta.equippedDroneIds = {content::drone::attackDrone, content::drone::defenseDrone};
    defended.run.destinationIndex = 4;
    startSurfaceExpedition(defended, catalog);
    prepareMiningSiteForTest(defended);
    require(startMiningRun(defended, catalog).applied, "defended hostile mining run should start");
    defended.run.mining.enemies = {
        {MiningEnemyType::Beetle, MiningCellFeature::MinibossLair, defended.run.mining.droneX + 2.0, defended.run.mining.droneY, 0.0, 0.0, 0.5, 10.0, 0.20, 0.0, 0.0, 0.0, true}
    };
    for (int tick = 0; tick < 10 && defended.run.mining.enemiesDefeated == 0; ++tick) {
        updateMiningRun(defended, catalog, 0.08);
    }
    require(defended.run.mining.enemiesDefeated == 1, "passive sentry defenses should defeat weakened enemies");
    require(defended.run.mining.defenseDamageDealt > 0.0, "passive sentry defenses should report damage dealt");
    require(!defended.run.mining.combatProjectiles.empty(), "passive sentry defenses should create allied projectile visuals");
    const MiningRunPresentation defendedMining = miningRunPresentation(defended, catalog);
    require(defendedMining.combatTitle.find("Killbox Screen") != std::string::npos, "live mining combat strip should name the active drone build");
    require(std::any_of(defendedMining.combatMetrics.begin(), defendedMining.combatMetrics.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Allied shots" && chip.value != "0";
    }), "live mining combat strip should count active allied projectile visuals");
    require(std::any_of(defendedMining.combatMetrics.begin(), defendedMining.combatMetrics.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "KOs" && chip.value == "1";
    }), "live mining combat strip should show enemies defeated by the drone build");
    require(std::any_of(defendedMining.combatMetrics.begin(), defendedMining.combatMetrics.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == "Drone dmg" && chip.value != "0.0";
    }), "live mining combat strip should show passive drone damage dealt");
    require(std::any_of(defended.run.mining.damageNumbers.begin(), defended.run.mining.damageNumbers.end(), [](const MiningDamageNumber& number) {
        return number.team == MiningCombatTeam::Allied;
    }), "passive sentry defenses should create allied damage numbers");
    require(std::any_of(defended.run.mining.damageNumbers.begin(), defended.run.mining.damageNumbers.end(), [](const MiningDamageNumber& number) {
        return number.kind == MiningCombatTextKind::Defeat;
    }), "enemy defeats should create a distinct defeat popup");
    require(std::any_of(defended.run.mining.damageNumbers.begin(), defended.run.mining.damageNumbers.end(), [](const MiningDamageNumber& number) {
        return number.kind == MiningCombatTextKind::RareReward || number.kind == MiningCombatTextKind::ExoticReward;
    }), "enemy defeat rewards should create material popup text");
    require(defended.run.mining.temporaryMaterials.rare > 0 || defended.run.mining.temporaryMaterials.exotic > 0, "miniboss defeats should grant upgrade-grade rewards");
}

void rangedMiningEnemiesShootAndCombatVisualsExpire()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91929);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.run.destinationIndex = 4;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "ranged enemy mining run should start");
    state.run.mining.enemies = {
        {MiningEnemyType::Flying, MiningCellFeature::HiveNest, state.run.mining.droneX + 5.0, state.run.mining.droneY, 0.0, 0.0, 40.0, 40.0, 0.0, 0.0, 1.0, 0.0, true}
    };
    const double healthBefore = state.run.mining.droneHealth;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.droneHealth < healthBefore, "ranged enemies should damage the drone from standoff range");
    require(!state.run.mining.combatProjectiles.empty(), "ranged enemies should create enemy projectile visuals");
    require(std::any_of(state.run.mining.damageNumbers.begin(), state.run.mining.damageNumbers.end(), [](const MiningDamageNumber& number) {
        return number.team == MiningCombatTeam::Enemy && number.rigDamage;
    }), "ranged enemy shots should create rig damage numbers");

    for (int tick = 0; tick < 30; ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(state.run.mining.combatProjectiles.size() <= static_cast<std::size_t>(tuning::mining::maxCombatProjectiles), "combat projectile visuals should stay capped");
    require(state.run.mining.damageNumbers.size() <= static_cast<std::size_t>(tuning::mining::maxDamageNumbers), "combat damage numbers should stay capped");
}

void attackDroneCombatCanCritAndEnemyCooldownPersists()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91930);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::attackDrone};
    state.run.destinationIndex = 4;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "attack drone crit mining run should start");
    state.run.mining.enemies = {
        {MiningEnemyType::Beetle, MiningCellFeature::EncounterZone, state.run.mining.droneX + 3.0, state.run.mining.droneY, 0.0, 0.0, 200.0, 200.0, 0.0, 0.0, 0.0, 0.0, true}
    };
    bool sawCrit = false;
    for (int tick = 0; tick < 80 && !sawCrit; ++tick) {
        updateMiningRun(state, catalog, 0.08);
        sawCrit = std::any_of(state.run.mining.damageNumbers.begin(), state.run.mining.damageNumbers.end(), [](const MiningDamageNumber& number) {
            return number.team == MiningCombatTeam::Allied && number.critical;
        });
    }
    require(sawCrit, "attack drone fire should be able to create critical damage text");

    state.run.mining.enemies.front().attackCooldownSeconds = 0.42;
    const SaveData save = captureSaveData(state);
    GameState restored = createNewGame(catalog, 91931);
    restoreSaveData(restored, catalog, save);
    require(!restored.run.mining.enemies.empty(), "enemy cooldown save test should restore active enemies");
    require(std::abs(restored.run.mining.enemies.front().attackCooldownSeconds - 0.42) < 0.000001, "enemy attack cooldown should round trip through saves");
}

void elementalMiningCombatAppliesAffinityAndAreaDefenses()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91923);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.run.destinationIndex = 4;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "elemental mining run should start");
    state.run.mining.drillHeat = 0.0;
    state.run.mining.enemies = {
        {MiningEnemyType::Elemental, MiningCellFeature::EncounterZone, state.run.mining.droneX + 0.2, state.run.mining.droneY, 0.0, 0.0, 40.0, 40.0, 0.0, 0.0, 1.0, tuning::mining::enemyElementalRadiusCells, true, MiningElementalAffinity::Thermal}
    };
    const double heatBefore = state.run.mining.drillHeat;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.elementalExposureSeconds > 0.0, "elemental contact should track exposure time");
    require(state.run.mining.drillHeat > heatBefore, "thermal elementals should heat the drill while in their area");
    require(state.run.mining.enemyDamageTaken > 0.0, "elemental contact should still damage the mining rig");
    require(miningElementalAffinityName(MiningElementalAffinity::Thermal) == std::string_view("Thermal"), "elemental affinity names should describe thermal threats");

    GameState cryo = createNewGame(catalog, 91924);
    cryo.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    cryo.meta.ark.condition = ArkCondition::DamagedStranded;
    cryo.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    cryo.meta.unlockKeys.push_back(content::unlock::deepSpace);
    cryo.run.destinationIndex = 4;
    startSurfaceExpedition(cryo, catalog);
    prepareMiningSiteForTest(cryo);
    require(startMiningRun(cryo, catalog).applied, "cryo elemental mining run should start");
    cryo.run.mining.moveX = 1.0;
    cryo.run.mining.enemies = {
        {MiningEnemyType::Elemental, MiningCellFeature::EncounterZone, cryo.run.mining.droneX + 0.2, cryo.run.mining.droneY, 0.0, 0.0, 40.0, 40.0, 0.0, 0.0, 0.0, tuning::mining::enemyElementalRadiusCells, true, MiningElementalAffinity::Cryo}
    };
    updateMiningRun(cryo, catalog, 0.08);
    require(cryo.run.mining.movementSlowSeconds > 0.0, "cryo elementals should apply a movement slow timer");
    require(cryo.run.mining.movementSlowScale < 1.0, "cryo elementals should reduce movement scale");

    GameState defended = createNewGame(catalog, 91925);
    defended.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    defended.meta.ark.condition = ArkCondition::DamagedStranded;
    defended.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    defended.meta.unlockKeys.push_back(content::unlock::deepSpace);
    defended.meta.unlockKeys.push_back(content::unlock::droneBay);
    defended.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    ensureDroneBayState(defended, catalog);
    defended.meta.droneBaySlots = 2;
    defended.meta.equippedDroneIds = {content::drone::attackDrone, content::drone::defenseDrone};
    defended.run.destinationIndex = 4;
    startSurfaceExpedition(defended, catalog);
    prepareMiningSiteForTest(defended);
    require(startMiningRun(defended, catalog).applied, "area-control mining run should start");
    defended.run.mining.enemies = {
        {MiningEnemyType::Ant, MiningCellFeature::EncounterZone, defended.run.mining.droneX + 0.2, defended.run.mining.droneY, 0.0, 0.0, 20.0, 20.0, 0.0, 0.0, 1.0, 0.0, true},
        {MiningEnemyType::Ant, MiningCellFeature::EncounterZone, defended.run.mining.droneX + 3.0, defended.run.mining.droneY, 0.0, 0.0, 20.0, 20.0, 0.0, 0.0, 0.0, 0.0, true}
    };
    updateMiningRun(defended, catalog, 0.08);
    require(defended.run.mining.areaControlDamageDealt > 0.0, "attack drones should apply area-control damage around the rig");
    require(defended.run.mining.reactiveArmorDamageDealt > 0.0, "defense drones should retaliate against contact enemies");
    require(defended.run.mining.environmentalShieldAbsorbed > 0.0, "environmental shields should absorb incoming enemy damage");
    require(defended.run.mining.enemies[1].health < defended.run.mining.enemies[1].maxHealth, "area-control fields should damage non-targeted nearby enemies");
}

void mammalBossChambersGrantAdvancedRewards()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91926);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::attackDrone};
    state.run.destinationIndex = 5;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "mammal boss mining run should start");
    const int blueprintBefore = state.meta.blueprintProgress;
    state.run.mining.enemies = {
        {MiningEnemyType::Mammal, MiningCellFeature::BossChamber, state.run.mining.droneX + 2.0, state.run.mining.droneY, 0.0, 0.0, 0.5, 35.0, 0.20, 0.0, 0.0, 0.0, true}
    };
    for (int tick = 0; tick < 10 && state.run.mining.enemiesDefeated == 0; ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(state.run.mining.enemiesDefeated == 1, "passive defenses should defeat weakened mammal boss test enemies");
    require(state.run.mining.temporaryMaterials.rare >= 5, "mammal boss chambers should grant significant rare materials");
    require(state.run.mining.temporaryMaterials.exotic >= 3, "mammal boss chambers should grant significant exotic materials");
    require(state.meta.blueprintProgress >= blueprintBefore + 2, "mammal boss chambers should recover advanced tech progress");
}

void enemyMovementTypesHaveDistinctBehavior()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState flying = createNewGame(catalog, 91927);
    flying.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    flying.meta.ark.condition = ArkCondition::DamagedStranded;
    flying.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    flying.meta.unlockKeys.push_back(content::unlock::deepSpace);
    flying.run.destinationIndex = 4;
    startSurfaceExpedition(flying, catalog);
    prepareMiningSiteForTest(flying);
    require(startMiningRun(flying, catalog).applied, "flying behavior mining run should start");
    flying.run.mining.enemies = {
        {MiningEnemyType::Flying, MiningCellFeature::EncounterZone, flying.run.mining.droneX + 4.0, flying.run.mining.droneY, 0.0, 0.0, 40.0, 40.0, 0.0, 3.1, 0.0, 0.0, true}
    };
    updateMiningRun(flying, catalog, 0.08);
    require(flying.run.mining.enemies.front().velocityX < 0.0, "flying enemies should still home toward the drone");
    require(std::abs(flying.run.mining.enemies.front().velocityY) > 0.05, "flying enemies should dart laterally while pursuing");

    GameState mammal = createNewGame(catalog, 91928);
    mammal.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    mammal.meta.ark.condition = ArkCondition::DamagedStranded;
    mammal.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    mammal.meta.unlockKeys.push_back(content::unlock::deepSpace);
    mammal.run.destinationIndex = 5;
    startSurfaceExpedition(mammal, catalog);
    prepareMiningSiteForTest(mammal);
    require(startMiningRun(mammal, catalog).applied, "mammal burrow behavior mining run should start");
    const int burrowX = static_cast<int>(std::floor(mammal.run.mining.droneX + 1.0));
    const int burrowY = static_cast<int>(std::floor(mammal.run.mining.droneY));
    if (MiningCell* current = miningCellAt(mammal.run.mining.terrain, burrowX + 1, burrowY)) {
        current->material = MiningCellMaterial::Empty;
        current->remainingToughness = 0.0;
        current->maxToughness = 0.0;
        current->revealed = true;
    }
    if (MiningCell* blocked = miningCellAt(mammal.run.mining.terrain, burrowX, burrowY)) {
        blocked->material = MiningCellMaterial::Regolith;
        blocked->remainingToughness = 0.2;
        blocked->maxToughness = 0.2;
        blocked->revealed = false;
        blocked->feature = MiningCellFeature::None;
        blocked->enemy = MiningEnemyType::None;
    }
    mammal.run.mining.enemies = {
        {MiningEnemyType::Mammal, MiningCellFeature::OrganicBurrow, static_cast<double>(burrowX) + 1.02, static_cast<double>(burrowY) + 0.5, 0.0, 0.0, 40.0, 40.0, 0.0, 1.45, 0.0, 0.0, true}
    };
    updateMiningRun(mammal, catalog, 0.08);
    const MiningCell* burrow = miningCellAt(mammal.run.mining.terrain, burrowX, burrowY);
    require(burrow != nullptr && burrow->material == MiningCellMaterial::Empty, "mammal enemies should burrow through weak non-bedrock cells");
    require(burrow != nullptr && burrow->feature == MiningCellFeature::OrganicBurrow, "mammal burrowing should mark organic tunnel tiles");
    require(burrow != nullptr && burrow->enemy == MiningEnemyType::Mammal, "mammal burrowing should seed mammal tunnel metadata");
}

void miningPresentationShowsActiveThreatMix()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91929);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.run.destinationIndex = 5;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "active threat presentation mining run should start");
    state.run.mining.enemies = {
        {MiningEnemyType::Ant, MiningCellFeature::EncounterZone, state.run.mining.droneX + 1.0, state.run.mining.droneY, 0.0, 0.0, 5.0, 5.0, 0.0, 2.0, 0.62, 0.0, true},
        {MiningEnemyType::Flying, MiningCellFeature::EncounterZone, state.run.mining.droneX + 2.0, state.run.mining.droneY, 0.0, 0.0, 4.0, 4.0, 0.0, 3.1, 0.48, 0.0, true},
        {MiningEnemyType::Beetle, MiningCellFeature::MinibossLair, state.run.mining.droneX + 3.0, state.run.mining.droneY, 0.0, 0.0, 10.0, 10.0, 0.45, 1.15, 0.82, 0.0, true},
        {MiningEnemyType::Elemental, MiningCellFeature::EncounterZone, state.run.mining.droneX + 4.0, state.run.mining.droneY, 0.0, 0.0, 8.0, 8.0, 0.18, 1.65, 0.58, tuning::mining::enemyElementalRadiusCells, true, MiningElementalAffinity::Toxic},
        {MiningEnemyType::Mammal, MiningCellFeature::BossChamber, state.run.mining.droneX + 5.0, state.run.mining.droneY, 0.0, 0.0, 15.0, 15.0, 0.28, 1.45, 0.95, 0.0, true}
    };

    const MiningRunPresentation presentation = miningRunPresentation(state, catalog);
    const DetailPresentationRow* threats = findDetailPresentationRow(presentation.details, "Active threats");
    require(threats != nullptr, "mining presentation should expose active threat composition");
    require(threats->value.find("Ant x1") != std::string::npos, "active threat summary should include ant threats");
    require(threats->value.find("Flying x1") != std::string::npos, "active threat summary should include flying threats");
    require(threats->value.find("Beetle x1") != std::string::npos, "active threat summary should include beetle threats");
    require(threats->value.find("Elemental x1") != std::string::npos, "active threat summary should include elemental threats");
    require(threats->value.find("Mammal x1") != std::string::npos, "active threat summary should include mammal threats");
    require(threats->value.find("Boss x2") != std::string::npos, "active threat summary should flag miniboss and boss-room enemies");
}

void miningDrillBreaksCellsAndMarksChunks()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92929);
    state.run.destinationIndex = 2;
    activateOnlyCrew(state, content::astronaut::marco);
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    const int supplyBefore = state.run.surfaceExpedition.supply;
    const int fuelBefore = state.run.surfaceExpedition.sharedFuel;
    const SurfaceActionOutcome started = startMiningRun(state, catalog);
    require(started.applied, "mining should start when the site is prepared");
    require(state.screen == Screen::Mining, "starting mining should move to the mining screen");
    require(state.run.surfaceExpedition.supply == supplyBefore, "starting mining should not spend action kits");
    require(started.fuelDelta == -1, "starting mining should report shared fuel spent");
    require(state.run.surfaceExpedition.sharedFuel == fuelBefore - 1, "starting mining should spend one shared fuel");

    MiningRunState& mining = state.run.mining;
    require(std::abs(mining.oxygenSeconds - tuning::mining::oxygenSeconds) < 0.000001, "starter mining run should begin with configured oxygen");
    require(mining.fuelSpent == 1, "active mining run should track the deployment fuel spend");
    MiningCell* ore = miningCellAt(mining.terrain, 33, 4);
    require(ore != nullptr, "test ore cell should exist");
    *ore = {MiningCellMaterial::CommonOre, 0.25, 0.25, true, false};
    MiningCell* farOre = miningCellAt(mining.terrain, 34, 4);
    require(farOre != nullptr, "test far ore cell should exist");
    *farOre = {MiningCellMaterial::CommonOre, 0.45, 0.45, true, false};
    std::fill(mining.terrain.dirtyChunks.begin(), mining.terrain.dirtyChunks.end(), 0);
    mining.droneX = 32.0;
    mining.droneY = 4.0;
    setMiningAim(state, 1.0, mining.droneY / static_cast<double>(mining.terrain.height - 1));
    setMiningDrilling(state, true);
    for (int i = 0; i < 8; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }

    require(ore->material == MiningCellMaterial::Empty, "drilling should break depleted terrain cells");
    require(farOre->material == MiningCellMaterial::Empty, "drilling should damage every solid tile under the drill footprint instead of one tile at a time");
    require(state.run.mining.temporaryMaterials.common > 0, "breaking common ore should add common material");
    require(state.run.mining.cargo > 0, "breaking ore should add cargo");
    require(
        std::any_of(mining.terrain.dirtyChunks.begin(), mining.terrain.dirtyChunks.end(), [](std::uint8_t value) { return value != 0; }),
        "drilling should mark the changed chunk dirty");
}

void miningUsesSharedFuelReserve()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState blocked = createNewGame(catalog, 92933);
    blocked.run.destinationIndex = 2;
    startSurfaceExpedition(blocked, catalog);
    prepareMiningSiteForTest(blocked);
    blocked.run.surfaceExpedition.sharedFuel = 0;
    const SurfaceActionOutcome blockedStart = startMiningRun(blocked, catalog);
    require(!blockedStart.applied, "mining should not start without shared fuel");
    require(surfaceActionSummary(blockedStart).find("Shared fuel is empty") != std::string::npos, "blocked mining should explain the shared fuel requirement");

    GameState state = createNewGame(catalog, 92934);
    state.run.destinationIndex = 2;
    state.run.surfaceUpgradeIds.push_back(content::surfaceUpgrade::emergencyWinch);
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    state.run.surfaceExpedition.sharedFuel = 1;
    require(startMiningRun(state, catalog).applied, "mining should start with one shared fuel");
    require(state.run.surfaceExpedition.sharedFuel == 0, "deployment should spend the available shared fuel");
    require(state.run.mining.oxygenSeconds > tuning::mining::fuelSecondsPerUnit, "oxygen upgrades should allow runs long enough to need another fuel draw");

    for (int i = 0; i < 190 && !state.run.mining.failurePending; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }

    require(state.run.mining.failurePending, "mining should recall when upgraded oxygen outlasts shared fuel");
    require(state.run.mining.failureMessage.find("Shared fuel is dry") != std::string::npos, "fuel recall should explain the shared reserve");
}

void miningDrillFootprintCapsWearToWorstContact()
{
    const ContentCatalog catalog = createDefaultContent();
    auto prepareState = [&](bool secondRock) {
        GameState state = createNewGame(catalog, 92932);
        state.run.destinationIndex = 2;
        startSurfaceExpedition(state, catalog);
        prepareMiningSiteForTest(state);
        require(startMiningRun(state, catalog).applied, "mining should start for footprint wear test");

        MiningRunState& mining = state.run.mining;
        for (MiningCell& cell : mining.terrain.cells) {
            cell = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
        }

        MiningCell* rock = miningCellAt(mining.terrain, 33, 4);
        require(rock != nullptr, "primary rock should exist");
        *rock = {MiningCellMaterial::HardRock, 40.0, 40.0, true, false};
        if (secondRock) {
            MiningCell* extraRock = miningCellAt(mining.terrain, 34, 4);
            require(extraRock != nullptr, "secondary rock should exist");
            *extraRock = {MiningCellMaterial::HardRock, 40.0, 40.0, true, false};
        }

        mining.droneX = 32.0;
        mining.droneY = 4.0;
        mining.drillHeat = 0.96;
        mining.drillIntegrity = 1.0;
        setMiningAim(state, 1.0, mining.droneY / static_cast<double>(mining.terrain.height - 1));
        setMiningDrilling(state, true);
        updateMiningRun(state, catalog, 0.08);
        return state;
    };

    GameState singleContact = prepareState(false);
    GameState doubleContact = prepareState(true);
    const double singleLoss = 1.0 - singleContact.run.mining.drillIntegrity;
    const double doubleLoss = 1.0 - doubleContact.run.mining.drillIntegrity;
    require(doubleLoss <= singleLoss + 0.000001, "multiple footprint contacts should not stack drill integrity wear");

    const MiningCell* first = miningCellAt(doubleContact.run.mining.terrain, 33, 4);
    const MiningCell* second = miningCellAt(doubleContact.run.mining.terrain, 34, 4);
    require(first != nullptr && first->remainingToughness < first->maxToughness, "first hard rock should still take drill damage");
    require(second != nullptr && second->remainingToughness < second->maxToughness, "second hard rock should still take drill damage");
}

void miningMovementGrindsSoftTerrainAndRecoilsFromHardTerrain()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92931);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "mining should start for movement feel test");

    MiningRunState& mining = state.run.mining;
    mining.droneX = 32.85;
    mining.droneY = 10.0;
    MiningCell* soft = miningCellAt(mining.terrain, 33, 10);
    require(soft != nullptr, "soft contact cell should exist");
    *soft = {MiningCellMaterial::Regolith, 3.0, 3.0, false, false};
    setMiningMove(state, 1.0, 0.0);
    setMiningDrilling(state, true);
    updateMiningRun(state, catalog, 0.08);

    require(mining.droneX > 32.85, "drilling into regolith should let the drone grind forward slowly");
    require(static_cast<int>(std::floor(mining.droneX)) == 32, "the drone should not occupy unbroken regolith before the drill clears it");
    require(mining.contactIntensity > 0.0, "soft contact should set mining feedback intensity");
    require(soft->remainingToughness < soft->maxToughness, "pushing into regolith while drilling should do terrain work");

    for (int i = 0; i < 12 && soft->material != MiningCellMaterial::Empty; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }

    require(soft->material == MiningCellMaterial::Empty, "continued drilling should visibly clear soft terrain before the drone passes through");

    mining.droneX = 32.85;
    mining.droneY = 12.0;
    MiningCell* hard = miningCellAt(mining.terrain, 33, 12);
    require(hard != nullptr, "hard contact cell should exist");
    *hard = {MiningCellMaterial::HardRock, 8.0, 8.0, false, false};
    mining.contactIntensity = 0.0;
    setMiningMove(state, 1.0, 0.0);
    setMiningDrilling(state, true);
    updateMiningRun(state, catalog, 0.08);

    require(mining.droneX <= 32.90, "hard rock should resist forward movement before it breaks");
    require(mining.recoilX < 0.0, "hard contact should push feedback opposite travel");
    require(mining.contactIntensity > 0.5, "hard contact should produce stronger mining feedback");
    require(mining.contactBounce > 0.0 || mining.contactBounceVelocity > 0.0, "hard contact should trigger a damped bounce impulse");
    require(hard->remainingToughness < hard->maxToughness, "hard contact should still drill the terrain");

    hard->material = MiningCellMaterial::HardRock;
    hard->maxToughness = miningMaterialToughness(MiningCellMaterial::HardRock, 0);
    hard->remainingToughness = hard->maxToughness;
    hard->revealed = false;
    mining.droneX = 32.85;
    mining.droneY = 12.0;
    setMiningMove(state, 1.0, 0.0);
    setMiningDrilling(state, true);
    for (int i = 0; i < 4; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(hard->material == MiningCellMaterial::HardRock, "hard rock should require several hard contacts before breaking");
    for (int i = 0; i < 14 && hard->material != MiningCellMaterial::Empty; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(hard->material == MiningCellMaterial::Empty, "default hard rock should clear in a short arcade burst");
}

void miningDrillTargetsFirstSolidCellOnRay()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92930);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "mining should start for targeting test");

    MiningRunState& mining = state.run.mining;
    mining.droneX = 32.9;
    mining.droneY = 10.0;
    MiningCell* nearOre = miningCellAt(mining.terrain, 33, 10);
    MiningCell* farOre = miningCellAt(mining.terrain, 34, 10);
    require(nearOre != nullptr && farOre != nullptr, "targeting test cells should exist");
    *nearOre = {MiningCellMaterial::CommonOre, 1.0, 1.0, true, false};
    *farOre = {MiningCellMaterial::RareOre, 1.0, 1.0, true, false};

    setMiningAim(state, 1.0, mining.droneY / static_cast<double>(mining.terrain.height - 1));

    require(mining.targetCellX == 33 && mining.targetCellY == 10, "drill targeting should stop at the first solid cell on the ray");
    require(mining.targetTipX < 34.0, "drill visual tip should stop before the far cell when terrain blocks the ray");
}

void miningCompletionFeedsSurfacePayload()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 93939);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "mining should start for completion test");
    state.run.mining.temporaryMaterials.common = 2;
    state.run.mining.temporaryMaterials.rare = 1;
    state.run.mining.cargo = 4;
    state.run.mining.hazardDelta = 0.05;
    state.run.mining.droneX = state.run.mining.returnZoneX;
    state.run.mining.droneY = state.run.mining.returnZoneY;

    const SurfaceActionOutcome finished = finishMiningRun(state, catalog, false);
    require(finished.applied, "finishing mining should produce a surface action outcome");
    require(state.screen == Screen::SurfaceExpedition, "finishing mining should return to surface expedition");
    require(!state.run.mining.active, "finishing mining should clear the active mining run");
    require(state.run.surfaceExpedition.temporaryMaterials.common == 2, "mined common material should move to surface payload");
    require(state.run.surfaceExpedition.temporaryMaterials.rare == 1, "mined rare material should move to surface payload");
    require(state.run.surfaceExpedition.cargo == 4, "mined cargo should move to surface payload");
    require(state.run.surfaceExpedition.hazard > tuning::research::baseHazard, "mining hazard should affect extraction pressure");
}

void miningBrokenDrillBitDisablesDrillingOnly()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94949);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "mining should start for drill failure test");

    state.run.mining.droneX = state.run.mining.returnZoneX + tuning::mining::returnZoneRadiusCells + 1.0;
    state.run.mining.droneY = state.run.mining.returnZoneY;
    state.run.mining.drillIntegrity = 0.0;
    state.run.mining.drilling = true;
    updateMiningRun(state, catalog, 0.08);

    require(state.screen == Screen::Mining, "broken drill bit should stay on mining screen");
    require(state.run.mining.active, "broken drill bit should keep the mining run active");
    require(!state.run.mining.failurePending, "broken drill bit should not force a recall");
    require(!state.run.mining.drilling, "broken drill bit should disable drilling");
    require(state.statusLine.find("Drill offline") != std::string::npos, "broken drill bit should explain the limited action set");
    pulseMiningScanner(state, catalog);
    require(state.run.mining.scannerPulseSeconds > 0.0, "broken drill bit should still allow scanner pulses");

    Random rng(94949);
    const PreparedLaunch prepared = prepareLaunch(state, catalog, rng);
    const std::string html = buildGamePanelHtml({state, catalog, prepared, prepared});
    require(html.find("Drill bit") != std::string::npos, "mining panel should show drill bit health separately");
    require(html.find("Drone health") != std::string::npos, "mining panel should show drone health separately");
    require(html.find("Emergency recall") != std::string::npos, "broken drill away from ship should still allow emergency recall");
    require(html.find("data-auto-modal=\"1\"") == std::string::npos, "broken drill bit should not open the failure modal");
}

void miningShipBankingLeaveAndEmergencyRecallRules()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 95959);
    state.run.destinationIndex = 2;
    state.run.surfaceUpgradeIds = {content::surfaceUpgrade::cargoSkids};
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "mining should start for ship banking test");

    state.run.mining.temporaryMaterials.common = 2;
    state.run.mining.cargo = 2;
    state.run.mining.droneX = state.run.mining.returnZoneX;
    state.run.mining.droneY = state.run.mining.returnZoneY;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.temporaryMaterials.common == 0, "ship zone should clear carried materials after banking");
    require(state.run.mining.cargo == 0, "ship zone should clear carried cargo after banking");
    require(state.run.mining.stowedMaterials.common == 2, "ship zone should stow carried materials");
    require(state.run.mining.stowedCargo == 2, "ship zone should stow carried cargo");

    MiningRunPresentation atShip = miningRunPresentation(state, catalog);
    require(std::any_of(atShip.actions.begin(), atShip.actions.end(), [](const PanelButtonPresentation& action) {
        return action.label == text::buttons::stowPayload;
    }), "Leave should appear inside the ship radius");
    require(std::none_of(atShip.actions.begin(), atShip.actions.end(), [](const PanelButtonPresentation& action) {
        return action.label == text::buttons::abortMining;
    }), "Emergency recall should not appear inside the ship radius");

    state.run.mining.droneX = state.run.mining.returnZoneX + tuning::mining::returnZoneRadiusCells + 1.0;
    state.run.mining.temporaryMaterials.rare = 1;
    state.run.mining.cargo = 2;
    MiningRunPresentation away = miningRunPresentation(state, catalog);
    require(std::any_of(away.actions.begin(), away.actions.end(), [](const PanelButtonPresentation& action) {
        return action.label == text::buttons::abortMining;
    }), "Emergency recall should appear away from the ship radius");
    require(std::none_of(away.actions.begin(), away.actions.end(), [](const PanelButtonPresentation& action) {
        return action.label == text::buttons::stowPayload;
    }), "Leave should not appear away from the ship radius");

    const SurfaceActionOutcome recalled = finishMiningRun(state, catalog, true);
    require(recalled.applied, "emergency recall should finish the mining run");
    require(state.run.surfaceExpedition.temporaryMaterials.common == 2, "emergency recall should preserve banked materials");
    require(state.run.surfaceExpedition.temporaryMaterials.rare == 0, "emergency recall should lose carried materials");
    require(state.run.surfaceExpedition.cargo == 2, "emergency recall should preserve only banked cargo");
    require(state.run.surfaceUpgradeIds.empty(), "emergency recall should clear temporary field upgrades");
    require(recalled.extractionRiskDelta >= tuning::mining::emergencyRecallHazardPenalty - 0.000001, "emergency recall should add the steep penalty");
}

void miningOxygenDrainsDroneHealthBeforeForcedRecall()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 95960);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "mining should start for oxygen drain test");

    state.run.mining.oxygenSeconds = 0.0;
    const double healthBefore = state.run.mining.droneHealth;
    updateMiningRun(state, catalog, 0.08);
    require(!state.run.mining.failurePending, "zero oxygen should not recall immediately");
    require(state.run.mining.droneHealth < healthBefore, "zero oxygen should drain drone health");
    require(state.statusLine.find("O2 depleted") != std::string::npos, "zero oxygen should report drone health drain");

    for (int i = 0; i < 260 && !state.run.mining.failurePending; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(state.run.mining.failurePending, "drone health reaching zero should force emergency recall");
}

void miningLoadBurdenAndUpgradeRelief()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState loaded = createNewGame(catalog, 95961);
    loaded.run.destinationIndex = 2;
    startSurfaceExpedition(loaded, catalog);
    prepareMiningSiteForTest(loaded);
    require(startMiningRun(loaded, catalog).applied, "mining should start for load burden test");

    MiningLoadStats emptyLoad = miningLoadStats(loaded, catalog);
    loaded.run.mining.cargo = 9;
    MiningLoadStats heavyLoad = miningLoadStats(loaded, catalog);
    require(emptyLoad.speedMultiplier == 1.0, "empty mining load should not slow the drone");
    require(heavyLoad.currentLoad == 9.0, "carried cargo should count as load");
    require(heavyLoad.speedMultiplier < 1.0, "carried load should slow the drone");
    require(heavyLoad.fuelMultiplier > 1.0, "carried load should increase fuel burn");
    require(heavyLoad.speedMultiplier >= tuning::mining::minLoadedSpeedMultiplier, "load slowdown should keep the minimum speed floor");

    GameState upgraded = loaded;
    upgraded.run.surfaceUpgradeIds = {content::surfaceUpgrade::expandablePanniers, content::surfaceUpgrade::vectorNozzles};
    upgraded.run.equippedModuleIds = {content::module::cargoSpine, content::module::haulerThrusters};
    MiningLoadStats upgradedLoad = miningLoadStats(upgraded, catalog);
    require(upgradedLoad.freeBuffer > heavyLoad.freeBuffer, "storage upgrades should increase the free carry buffer");
    require(upgradedLoad.speedMultiplier > heavyLoad.speedMultiplier, "engine upgrades should reduce load speed penalty");
    require(upgradedLoad.fuelMultiplier < heavyLoad.fuelMultiplier, "engine upgrades should reduce load fuel penalty");
}

void miningRefitModulesImproveDrillProfileIncrementally()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline = createNewGame(catalog, 96969);
    GameState upgraded = createNewGame(catalog, 96969);
    upgraded.meta.unlockKeys.push_back(content::unlock::surfaceProbes);
    upgraded.meta.unlockKeys.push_back(content::unlock::surfaceDrills);
    upgraded.meta.unlockKeys.push_back(content::unlock::cargoRigs);
    upgraded.run.equippedModuleIds = {
        content::module::surfaceMapper,
        content::module::regolithAuger,
        content::module::oreSorter,
        content::module::coolantSleeve,
        content::module::diamondBearings,
        content::module::deepBoreFrame,
        content::module::cargoSpine,
        content::module::haulerThrusters
    };

    const MiningDrillStats baseStats = miningDrillStats(baseline, catalog);
    const MiningDrillStats upgradedStats = miningDrillStats(upgraded, catalog);
    require(upgradedStats.power > baseStats.power, "mining drill modules should improve terrain break speed");
    require(upgradedStats.oreYieldChance > baseStats.oreYieldChance, "mining yield modules should add bonus ore chance");
    require(upgradedStats.heatRiseScale < baseStats.heatRiseScale, "mining cooling modules should reduce heat rise");
    require(upgradedStats.heatCoolingPerSecond > baseStats.heatCoolingPerSecond, "mining cooling modules should improve heat recovery");
    require(upgradedStats.integrityRelief > baseStats.integrityRelief, "durability modules should protect the mining drill");
    require(upgradedStats.hardRockBounceRelief > baseStats.hardRockBounceRelief, "durability modules should reduce hard-rock recoil");
    require(upgradedStats.terrainWidth > baseStats.terrainWidth, "survey modules should widen the mining terrain");
    require(upgradedStats.terrainHeight > baseStats.terrainHeight, "deep-bore modules should deepen the mining terrain");
    require(upgradedStats.storage > baseStats.storage, "cargo refits should increase mining free carry");
    require(upgradedStats.engineEfficiency > baseStats.engineEfficiency, "hauler refits should reduce load burden");

    upgraded.run.destinationIndex = 2;
    startSurfaceExpedition(upgraded, catalog);
    prepareMiningSiteForTest(upgraded);
    require(startMiningRun(upgraded, catalog).applied, "upgraded mining state should start mining");
    require(upgraded.run.mining.terrain.width == upgradedStats.terrainWidth, "mining terrain should use upgraded width");
    require(upgraded.run.mining.terrain.height == upgradedStats.terrainHeight, "mining terrain should use upgraded depth");

    const ShipModule* mapper = catalog.findModule(content::module::surfaceMapper);
    require(mapper != nullptr, "surface mapper module should exist");
    const RefitPresentation mapperCard = moduleRefitPresentation(*mapper);
    require(hasRefitChip(mapperCard, text::moduleStats::miningWidthChip, "+1.0", true), "mining refit cards should expose mining stat chips");
    const ShipModule* hauler = catalog.findModule(content::module::haulerThrusters);
    require(hauler != nullptr, "hauler thrusters module should exist");
    const RefitPresentation haulerCard = moduleRefitPresentation(*hauler);
    require(hasRefitChip(haulerCard, text::moduleStats::miningEngineEfficiencyChip, "+0.2", true), "hauler refit cards should expose load stat chips");
}

void activeMiningRoundTripsThroughSave()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94949);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "mining should start before save");
    state.statusLine = std::string(text::status::miningStarted);
    state.run.mining.droneX = 21.5;
    state.run.mining.droneY = 9.25;
    state.run.mining.returnZoneX = 31.5;
    state.run.mining.returnZoneY = 4.25;
    state.run.mining.droneHealth = 0.72;
    state.run.mining.fuelBurnSeconds = 4.5;
    state.run.mining.fuelSpent = 2;
    state.run.mining.temporaryMaterials.exotic = 1;
    state.run.mining.stowedMaterials.common = 2;
    state.run.mining.stowedCargo = 3;
    state.run.mining.stowedArtifacts.push_back({"banked_artifact", content::destination::mars, false});
    state.run.mining.enemiesDefeated = 3;
    state.run.mining.defenseDamageDealt = 4.25;
    state.run.mining.enemyDamageTaken = 0.125;
    state.run.mining.areaControlDamageDealt = 1.5;
    state.run.mining.reactiveArmorDamageDealt = 0.75;
    state.run.mining.environmentalShieldAbsorbed = 0.25;
    state.run.mining.elementalExposureSeconds = 2.5;
    state.run.mining.movementSlowSeconds = 0.4;
    state.run.mining.movementSlowScale = 0.62;
    state.run.mining.enemies = {
        {MiningEnemyType::Elemental, MiningCellFeature::EncounterZone, 22.5, 10.5, 1.0, -0.5, 2.5, 4.0, 0.0, 3.1, 0.48, 1.8, true, MiningElementalAffinity::Radiation}
    };
    if (MiningCell* cell = miningCellAt(state.run.mining.terrain, 20, 10)) {
        cell->material = MiningCellMaterial::RareOre;
        cell->maxToughness = 7.0;
        cell->remainingToughness = 3.5;
        cell->revealed = true;
        cell->feature = MiningCellFeature::BossChamber;
        cell->enemy = MiningEnemyType::Mammal;
    }

    const std::string serialized = serializeSaveData(captureSaveData(state));
    const auto save = deserializeSaveData(serialized);
    require(save.has_value(), "active mining save should parse");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);
    require(restored.screen == Screen::Mining, "active mining screen should round trip");
    require(restored.run.surfaceExpedition.miningSitePrepared && restored.run.surfaceExpedition.miningRunUsed, "active mining restore should preserve the one-run surface state");
    require(restored.run.mining.active, "active mining state should round trip");
    require(std::abs(restored.run.mining.droneX - 21.5) < 0.000001, "mining drone x should round trip");
    require(std::abs(restored.run.mining.returnZoneX - 31.5) < 0.000001, "mining return zone x should round trip");
    require(std::abs(restored.run.mining.returnZoneY - 4.25) < 0.000001, "mining return zone y should round trip");
    require(std::abs(restored.run.mining.droneHealth - 0.72) < 0.000001, "mining drone health should round trip");
    require(std::abs(restored.run.mining.fuelBurnSeconds - 4.5) < 0.000001, "mining fuel timer should round trip");
    require(restored.run.mining.fuelSpent == 2, "mining fuel spend should round trip");
    require(restored.run.mining.temporaryMaterials.exotic == 1, "mining temporary materials should round trip");
    require(restored.run.mining.stowedMaterials.common == 2, "mining stowed materials should round trip");
    require(restored.run.mining.stowedCargo == 3, "mining stowed cargo should round trip");
    require(restored.run.mining.stowedArtifacts.size() == 1, "mining stowed artifacts should round trip");
    require(restored.run.mining.enemiesDefeated == 3, "mining defeated enemy count should round trip");
    require(std::abs(restored.run.mining.defenseDamageDealt - 4.25) < 0.000001, "mining defense damage should round trip");
    require(std::abs(restored.run.mining.enemyDamageTaken - 0.125) < 0.000001, "mining enemy damage should round trip");
    require(std::abs(restored.run.mining.areaControlDamageDealt - 1.5) < 0.000001, "mining area-control damage should round trip");
    require(std::abs(restored.run.mining.reactiveArmorDamageDealt - 0.75) < 0.000001, "mining reactive armor damage should round trip");
    require(std::abs(restored.run.mining.environmentalShieldAbsorbed - 0.25) < 0.000001, "mining shield absorption should round trip");
    require(std::abs(restored.run.mining.elementalExposureSeconds - 2.5) < 0.000001, "mining elemental exposure should round trip");
    require(std::abs(restored.run.mining.movementSlowSeconds - 0.4) < 0.000001, "mining slow timer should round trip");
    require(std::abs(restored.run.mining.movementSlowScale - 0.62) < 0.000001, "mining slow scale should round trip");
    require(restored.run.mining.enemies.size() == 1, "active mining enemies should round trip");
    require(restored.run.mining.enemies.front().type == MiningEnemyType::Elemental, "active mining enemy type should round trip");
    require(restored.run.mining.enemies.front().sourceFeature == MiningCellFeature::EncounterZone, "active mining enemy source feature should round trip");
    require(restored.run.mining.enemies.front().affinity == MiningElementalAffinity::Radiation, "active mining enemy affinity should round trip");
    require(std::abs(restored.run.mining.enemies.front().health - 2.5) < 0.000001, "active mining enemy health should round trip");
    const MiningCell* restoredCell = miningCellAt(restored.run.mining.terrain, 20, 10);
    require(restoredCell != nullptr && restoredCell->material == MiningCellMaterial::RareOre, "mining terrain material should round trip");
    require(restoredCell != nullptr && std::abs(restoredCell->remainingToughness - 3.5) < 0.000001, "mining terrain toughness should round trip");
    require(restoredCell != nullptr && restoredCell->feature == MiningCellFeature::BossChamber, "mining terrain feature metadata should round trip");
    require(restoredCell != nullptr && restoredCell->enemy == MiningEnemyType::Mammal, "mining terrain enemy metadata should round trip");
}

void surfaceActionSummaryShowsResourceDeltas()
{
    SurfaceActionOutcome outcome;
    outcome.applied = true;
    outcome.message = std::string(text::status::surfaceSurveyed);
    outcome.supplyDelta = -2;
    outcome.fuelDelta = -1;
    outcome.materialDelta = {.common = 2, .rare = 1, .exotic = 1};
    outcome.materialLost = {.common = 1, .rare = 1};
    outcome.cargoDelta = 8;
    outcome.blueprintDelta = 1;
    outcome.artifactFound = true;
    outcome.artifactsLost = 1;
    outcome.extractionRisk = 0.24;
    outcome.extractionRiskDelta = 0.06;
    outcome.hazardDelta = 0.05;

    const std::string summary = surfaceActionSummary(outcome);
    require(summary.find("-2 Action kits") != std::string::npos, "surface action summary should include action-kit deltas");
    require(summary.find("-1 Shared fuel") != std::string::npos, "surface action summary should include shared fuel deltas");
    require(summary.find("+2 Common mats") != std::string::npos, "surface action summary should include common material deltas");
    require(summary.find("+1 Rare mats") != std::string::npos, "surface action summary should include rare material deltas");
    require(summary.find("+1 Exotic mats") != std::string::npos, "surface action summary should include exotic material deltas");
    require(summary.find("Lost 1 Common mats") != std::string::npos, "surface action summary should include lost common materials");
    require(summary.find("Lost 1 Rare mats") != std::string::npos, "surface action summary should include lost rare materials");
    require(summary.find("+8 Cargo") != std::string::npos, "surface action summary should include cargo deltas");
    require(summary.find("+1 Blueprints") != std::string::npos, "surface action summary should include blueprint deltas");
    require(summary.find("+1 Artifacts") != std::string::npos, "surface action summary should include artifact deltas");
    require(summary.find("Lost 1 Artifacts") != std::string::npos, "surface action summary should include lost artifacts");
    require(summary.find("24% Extraction risk") != std::string::npos, "surface action summary should include extraction risk when present");
    require(summary.find("+6% Extraction risk") != std::string::npos, "surface action summary should include extraction-risk deltas");
    require(summary.find("+5% Hazard") != std::string::npos, "surface action summary should include hazard deltas");
}

void roughSurfaceExtractionReportsLostPayload()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 8181);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.supply = 0;
    state.run.surfaceExpedition.cargo = 30;
    state.run.surfaceExpedition.hazard = 1.0;
    state.run.surfaceExpedition.temporaryMaterials = {.common = 5, .rare = 3, .exotic = 1};
    state.run.surfaceExpedition.temporaryArtifacts.push_back({"mars_artifact_loss", content::destination::mars, false});

    for (int seed = 1; seed < 400; ++seed) {
        GameState candidate = state;
        Random rng(seed);
        const SurfaceActionOutcome outcome = extractSurfacePayload(candidate, rng);
        if (outcome.cargoRecovered) {
            continue;
        }

        require(outcome.applied, "rough extraction should still resolve");
        require(outcome.materialDelta.common > 0, "rough extraction should recover partial common materials");
        require(outcome.materialLost.common > 0, "rough extraction should report lost common materials");
        require(outcome.materialLost.rare > 0, "rough extraction should report lost rare materials");
        require(outcome.materialLost.exotic > 0, "rough extraction should report lost exotic materials");
        require(outcome.artifactsLost == 1, "rough extraction should report lost artifacts");
        const std::string summary = surfaceActionSummary(outcome);
        require(summary.find("Lost") != std::string::npos, "rough extraction summary should include lost payload text");
        require(candidate.meta.artifacts.empty(), "rough extraction should not bank lost artifacts");
        return;
    }

    require(false, "test should find a rough extraction seed");
}

void researchPresentationComesFromSharedHelper()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 9191);
    state.run.destinationIndex = 2;
    state.meta.materials = {.common = 4, .rare = 1};
    Random rng(9191);
    generateResearchProjects(state, catalog, rng);

    const ResearchPhasePresentation research = researchPhasePresentation(state, catalog);
    require(research.metrics.size() == 6, "research presentation should expose blueprint, insight, lab, and material metrics");
    require(research.phaseSteps.size() == 4, "research presentation should expose post-arrival phase steps");
    require(research.phaseSteps[0].label == std::string(text::panel::details::arrivalPhase), "research phase track should start after arrival");
    require(research.phaseSteps[0].stateLabel == "Done" && research.phaseSteps[0].stateClass == "done", "research phase track should mark arrival complete");
    require(research.phaseSteps[1].label == std::string(text::panel::details::researchPhase), "research phase track should include research");
    require(research.phaseSteps[1].stateLabel == "Now" && research.phaseSteps[1].stateClass == "active", "research phase track should mark research active");
    require(research.phaseSteps[2].label == std::string(text::panel::details::surfacePhase) && research.phaseSteps[2].stateLabel == "Next", "research phase track should stage surface next");
    require(research.phaseSteps[3].label == std::string(text::panel::details::refitPhase) && research.phaseSteps[3].stateLabel == "Next", "research phase track should stage refit next");
    require(research.briefing.title == std::string(text::panel::modals::researchBriefing), "research presentation should expose a briefing modal title");
    require(findDetailPresentationRow(research.briefing.rows, text::panel::details::phaseIntent) != nullptr, "research briefing should explain phase intent");
    require(findDetailPresentationRow(research.briefing.rows, text::panel::details::phaseNext) != nullptr, "research briefing should explain the next phase");
    require(research.advisory.title == std::string(text::panel::messages::researchAdvisoryReady), "funded research should present ready advisory");
    require(research.advisory.cssClass == "ok", "funded research advisory should use ok styling");
    require(!research.details.empty(), "research presentation should expose detail modal rows");
    require(hasDetailPresentationHeader(research.details, text::panel::details::researchRules), "research details should include rule guidance");
    require(findDetailPresentationRow(research.details, text::panel::details::blueprintUse) != nullptr, "research details should explain blueprint use");
    require(findDetailPresentationRow(research.details, text::panel::details::materialsUse) != nullptr, "research details should explain material costs");
    require(findDetailPresentationRow(research.details, text::panel::details::skippedResearch) != nullptr, "research details should explain skipped research");
    require(!research.projects.empty(), "research presentation should expose resolved project cards");
    require(research.skipAction.enabled && research.skipAction.actionId == std::string(ui::actions::skipResearch), "research presentation should expose shared skip action");

    const ResearchProjectCardPresentation& card = research.projects.front();
    require(!card.title.empty() && !card.detail.empty(), "research project card should expose content text");
    require(card.blueprintGain.find("BP") != std::string::npos, "research project card should expose blueprint gain");
    require(card.materialCost.find("Cost:") != std::string::npos && card.materialCost.find("Have:") != std::string::npos, "research project card should show cost and owned materials");
    require(!card.resourceChips.empty(), "research project card should expose resource chips");
    require(card.resourceChips.front().label == std::string(text::labels::blueprints), "research project resource chips should lead with blueprint output");
    require(card.resourceChips.front().value == card.blueprintGain, "research project blueprint chip should match card blueprint gain");
    require(card.action.actionId == ui::actions::researchProject(card.index), "research project card should use shared indexed research action");

    GameState broke = state;
    broke.meta.materials = {};
    broke.run.researchProjectIds = {content::research::appliedMaterialsLab, "", ""};
    const ResearchPhasePresentation brokeResearch = researchPhasePresentation(broke, catalog);
    require(!brokeResearch.projects.empty(), "unaffordable research should still present the project");
    require(!brokeResearch.projects.front().reward.empty(), "unowned reward unlock should be visible on research card");
    require(brokeResearch.projects.front().materialCost.find("Have: No materials") != std::string::npos, "unaffordable research should show empty owned material inventory");
    require(!brokeResearch.projects.front().affordable, "research project should expose unaffordable state");
    require(!brokeResearch.projects.front().action.enabled, "unaffordable research should disable its action");
    require(brokeResearch.projects.front().action.label == std::string(text::panel::needMaterials), "unaffordable research should use shared need-materials label");
    require(brokeResearch.advisory.title == std::string(text::panel::messages::researchAdvisoryMaterials), "unfunded research should explain material shortage");
    require(brokeResearch.advisory.cssClass == "caution", "unfunded research advisory should use caution styling");

    GameState emptyResearch = state;
    emptyResearch.run.researchProjectIds = {"", "", ""};
    const ResearchPhasePresentation emptyResearchPanel = researchPhasePresentation(emptyResearch, catalog);
    require(emptyResearchPanel.projects.empty(), "empty research board should expose no project cards");
    require(emptyResearchPanel.advisory.title == std::string(text::panel::messages::researchAdvisoryEmpty), "empty research board should explain missing projects");

    broke.meta.unlockKeys.push_back(content::unlock::recovery);
    const ResearchPhasePresentation ownedReward = researchPhasePresentation(broke, catalog);
    require(ownedReward.projects.front().reward.empty(), "already-owned reward unlock should not be advertised as new");

    state.meta.artifacts.push_back({"mars_artifact_4", content::destination::mars, true});
    state.meta.unlockKeys.push_back(content::unlock::analysisLab);
    state.run.researchProjectIds = {content::research::appliedMaterialsLab, "", ""};
    const ResearchPhasePresentation insightfulResearch = researchPhasePresentation(state, catalog);
    require(insightfulResearch.metrics[1].value == text::panel::blueprintGain(1), "research presentation should expose artifact insight bonus");
    require(insightfulResearch.metrics[2].value == text::panel::blueprintGain(tuning::research::analysisLabBlueprintBonus), "research presentation should expose lab bonus");
    const ResearchProject* insightProject = catalog.findResearchProject(content::research::appliedMaterialsLab);
    require(insightProject != nullptr, "presentation insight test needs materials research content");
    require(insightfulResearch.projects.front().blueprintGain == text::panel::blueprintGain(researchBlueprintGain(state.meta, *insightProject)), "research cards should show effective blueprint gain");
    require(std::find_if(insightfulResearch.projects.front().resourceChips.begin(), insightfulResearch.projects.front().resourceChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == std::string(text::labels::commonMaterials) && chip.value == "-2";
    }) != insightfulResearch.projects.front().resourceChips.end(), "research cards should expose material costs as resource chips");
}

void surfacePresentationComesFromSharedHelper()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 9292);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.temporaryMaterials = {.common = 2, .rare = 1, .exotic = 1};
    state.run.surfaceExpedition.temporaryArtifacts.push_back({"mars_artifact_surface", content::destination::mars, false});
    state.run.surfaceExpedition.prospectMaterials = {.common = 1, .rare = 1};

    SurfaceExpeditionPresentation surface = surfaceExpeditionPresentation(state);
    require(surface.metrics.size() == 13, "surface presentation should expose site, field kit, hazard, action kits, shared fuel, cargo, depth, risk, materials, artifacts, and prospects");
    require(surface.phaseSteps.size() == 4, "surface presentation should expose post-arrival phase steps");
    require(surface.phaseSteps[0].stateLabel == "Done" && surface.phaseSteps[0].stateClass == "done", "surface phase track should mark arrival complete");
    require(surface.phaseSteps[1].label == std::string(text::panel::details::researchPhase), "surface phase track should include research");
    require(surface.phaseSteps[1].stateLabel == "Done" && surface.phaseSteps[1].stateClass == "done", "surface phase track should mark research complete");
    require(surface.phaseSteps[2].label == std::string(text::panel::details::surfacePhase), "surface phase track should include surface");
    require(surface.phaseSteps[2].stateLabel == "Now" && surface.phaseSteps[2].stateClass == "active", "surface phase track should mark surface active");
    require(surface.phaseSteps[3].label == std::string(text::panel::details::refitPhase) && surface.phaseSteps[3].stateLabel == "Next", "surface phase track should stage refit next");
    require(surface.briefing.title == std::string(text::panel::modals::surfaceBriefing), "surface presentation should expose a briefing modal title");
    require(findDetailPresentationRow(surface.briefing.rows, text::panel::details::phaseRisk) != nullptr, "surface briefing should explain extraction risk");
    require(findDetailPresentationRow(surface.briefing.rows, text::panel::details::phaseNext) != nullptr, "surface briefing should explain the next phase");
    require(surface.postureTitle == std::string(text::panel::messages::surfacePostureStable), "loaded low-risk surface payload should present stable posture");
    require(surface.postureClass == "ok", "stable surface posture should use ok styling");
    require(surface.metrics.front().label == std::string(text::labels::site), "surface presentation should expose active site profile");
    require(std::find_if(surface.metrics.begin(), surface.metrics.end(), [](const PanelMetricPresentation& metric) {
        return metric.label == std::string(text::labels::exoticMaterials) && metric.value == "1";
    }) != surface.metrics.end(), "surface presentation should expose temporary exotic material cargo");
    require(std::find_if(surface.metrics.begin(), surface.metrics.end(), [](const PanelMetricPresentation& metric) {
        return metric.label == std::string(text::labels::artifacts) && metric.value == "1";
    }) != surface.metrics.end(), "surface presentation should expose temporary artifact cargo");
    require(std::find_if(surface.metrics.begin(), surface.metrics.end(), [](const PanelMetricPresentation& metric) {
        return metric.label == "Prospects" && metric.value == "2";
    }) != surface.metrics.end(), "surface presentation should expose tagged underground prospects");
    require(!surface.siteDetail.empty(), "surface presentation should expose active site detail");
    require(!surface.details.empty(), "surface presentation should expose field rule details");
    require(hasDetailPresentationHeader(surface.details, text::panel::details::fieldRules), "surface details should include field rules");
    require(findDetailPresentationRow(surface.details, text::panel::details::surveyRisk) != nullptr, "surface details should explain survey hazards");
    require(findDetailPresentationRow(surface.details, text::panel::details::miningRisk) != nullptr, "surface details should explain mining hazards");
    require(findDetailPresentationRow(surface.details, text::panel::details::extraction) != nullptr, "surface details should explain extraction risk");
    require(!surface.logEntries.empty(), "surface presentation should expose recent mission log entries");
    require(surface.actions.size() == 4, "surface presentation should expose the four action preview cards");
    require(surface.actions[0].title == std::string(text::buttons::surveySite), "surface survey preview should use shared title text");
    require(surface.actions[0].cost == text::panel::messages::supplyCost(tuning::research::surveySupplyCost), "surface survey preview should expose supply cost");
    require(surface.actions[0].riskLabel == std::string(text::labels::hazard), "surface field actions should label action hazard risk");
    require(std::find_if(surface.actions[0].payoffChips.begin(), surface.actions[0].payoffChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == std::string(text::labels::commonMaterials) && !chip.value.empty() && chip.value.front() == '+';
    }) != surface.actions[0].payoffChips.end(), "surface survey preview should expose material payoff chips");
    require(std::find_if(surface.actions[0].payoffChips.begin(), surface.actions[0].payoffChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == std::string(text::labels::extractionRisk) && !chip.value.empty() && chip.value.front() == '+';
    }) != surface.actions[0].payoffChips.end(), "surface survey preview should expose projected extraction-risk impact");
    require(surface.actions[0].action.actionId == std::string(ui::actions::surveySurface), "surface survey should use shared action id");
    require(surface.actions[1].action.enabled, "fresh surface mine should be available when shared fuel is available");
    require(surface.actions[1].cost == "1 " + std::string(text::fuel::reserveLabel(false)), "surface mine preview should expose fuel-only cost");
    require(std::find_if(surface.actions[1].payoffChips.begin(), surface.actions[1].payoffChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == std::string(text::labels::oxygen);
    }) != surface.actions[1].payoffChips.end(), "surface mine preview should expose oxygen runtime");
    require(std::find_if(surface.actions[1].payoffChips.begin(), surface.actions[1].payoffChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == std::string(text::labels::sharedFuel) && chip.value == "-1 deploy";
    }) != surface.actions[1].payoffChips.end(), "surface mine preview should expose deployment fuel spend");
    require(surface.actions[2].action.cssClass == "danger", "push deeper should expose danger styling");
    require(std::find_if(surface.actions[2].payoffChips.begin(), surface.actions[2].payoffChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == std::string(text::labels::depth) && chip.value == "+1";
    }) != surface.actions[2].payoffChips.end(), "push deeper preview should expose depth payoff");
    require(std::find_if(surface.actions[2].payoffChips.begin(), surface.actions[2].payoffChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == std::string(text::labels::extractionRisk) && !chip.value.empty() && chip.value.front() == '+';
    }) != surface.actions[2].payoffChips.end(), "push deeper preview should expose projected extraction-risk impact");
    require(surface.actions[3].action.actionId == std::string(ui::actions::extractSurface), "surface extraction should use shared action id");
    require(surface.actions[3].riskLabel == std::string(text::labels::extractionRisk), "surface extraction should label extraction risk instead of hazard");
    require(std::find_if(surface.actions[3].payoffChips.begin(), surface.actions[3].payoffChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == std::string(text::labels::artifacts) && chip.value == "+1";
    }) != surface.actions[3].payoffChips.end(), "surface extraction preview should expose loaded artifact payoff");
    state.meta.ark.condition = ArkCondition::DerelictOperable;
    surface = surfaceExpeditionPresentation(state);
    require(surface.actions[3].title == "Return to Ark", "Ark surface extraction should relabel the card title");
    require(surface.actions[3].action.label == "Return to Ark", "Ark surface extraction should relabel the button");
    require(surface.actions[3].action.actionId == std::string(ui::actions::extractSurface), "Ark surface extraction should keep the stable action id");
    state.meta.ark.condition = ArkCondition::NotFound;

    surface = surfaceExpeditionPresentation(state);
    require(surface.actions[1].action.actionId == std::string(ui::actions::mineSurface), "fresh surface mine should use shared action id");
    require(surface.actions[1].availability == text::fuel::availability(false), "fresh surface mine should report fuel readiness");
    state.run.surfaceExpedition.supply = 0;
    surface = surfaceExpeditionPresentation(state);
    require(!surface.actions[0].action.enabled && surface.actions[0].availability == text::panel::messages::needSupply(tuning::research::surveySupplyCost), "blocked surface survey should keep the detailed action-kit reason in status text");
    require(surface.actions[0].action.label == std::string(text::buttons::unavailable), "blocked surface survey button should stay compact for card footers");
    require(!surface.actions[2].action.enabled && surface.actions[2].availability == text::panel::messages::needSupply(tuning::research::pushSupplyCost), "blocked deep push should keep the detailed action-kit reason in status text");
    require(surface.actions[2].action.label == std::string(text::buttons::unavailable), "blocked deep push button should stay compact for card footers");
    state.run.surfaceExpedition.supply = 8;
    state.run.surfaceExpedition.sharedFuel = 0;
    surface = surfaceExpeditionPresentation(state);
    require(!surface.actions[1].action.enabled && surface.actions[1].availability == std::string(text::fuel::offline), "fuel-starved mine should report the unified offline state");
    require(surface.actions[1].action.label == std::string(text::buttons::unavailable), "fuel-starved mine button should use the neutral unavailable label");
    state.run.surfaceExpedition.sharedFuel = 1;
    state.run.surfaceExpedition.miningRunUsed = true;
    surface = surfaceExpeditionPresentation(state);
    require(!surface.actions[1].action.enabled && surface.actions[1].availability == std::string(text::fuel::offline), "used mining run should disable the mine action with the unified offline state");
    require(!surface.actions[0].action.enabled && surface.actions[0].availability == std::string(text::panel::messages::surfaceFieldworkClosed), "used mining run should disable surveying with extraction-focused copy");
    require(!surface.actions[2].action.enabled && surface.actions[2].availability == std::string(text::panel::messages::surfaceFieldworkClosed), "used mining run should disable pushing deeper with extraction-focused copy");
    require(surface.actions[1].action.label == std::string(text::buttons::unavailable), "used mining run mine button should use the neutral unavailable label");
    require(surface.actions[0].action.label == std::string(text::buttons::unavailable), "post-mining survey button should use the neutral unavailable label");
    require(surface.actions[2].action.label == std::string(text::buttons::unavailable), "post-mining push button should use the neutral unavailable label");

    state.run.surfaceExpedition.supply = 0;
    surface = surfaceExpeditionPresentation(state);
    require(surface.postureTitle == std::string(text::panel::messages::surfacePostureExtract), "zero-supply surface payload should tell the player to extract");
    require(surface.postureClass == "danger", "zero-supply surface posture should use danger styling");
    require(!surface.actions[0].action.enabled && !surface.actions[1].action.enabled && !surface.actions[2].action.enabled, "surface supply should disable field actions");
    require(surface.actions[3].action.enabled, "surface extraction should remain available at zero supply");

    GameState empty = createNewGame(catalog, 9293);
    empty.run.destinationIndex = 2;
    startSurfaceExpedition(empty, catalog);
    const SurfaceExpeditionPresentation emptySurface = surfaceExpeditionPresentation(empty);
    require(emptySurface.postureTitle == "Recommended: mine deposit", "empty fueled surface payload should recommend the drone mining choice");
    require(emptySurface.postureClass == "neutral", "empty surface posture should use neutral styling");

    GameState risky = state;
    risky.run.surfaceExpedition.active = true;
    risky.run.surfaceExpedition.supply = 1;
    risky.run.surfaceExpedition.cargo = 16;
    risky.run.surfaceExpedition.hazard = 0.55;
    const SurfaceExpeditionPresentation riskySurface = surfaceExpeditionPresentation(risky);
    require(riskySurface.postureTitle == std::string(text::panel::messages::surfacePostureGreedy), "high extraction risk should call out greed pressure");
    require(riskySurface.postureClass == "danger", "greed posture should use danger styling");

    GameState deepSpace = createNewGame(catalog, 9395);
    deepSpace.run.destinationIndex = 4;
    deepSpace.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    startSurfaceExpedition(deepSpace, catalog);
    const SurfaceExpeditionPresentation deepSurface = surfaceExpeditionPresentation(deepSpace);
    require(deepSurface.metrics.size() == 14, "post-solar surface presentation should expose prospects and contact risk");
    require(std::find_if(deepSurface.metrics.begin(), deepSurface.metrics.end(), [](const PanelMetricPresentation& metric) {
        return metric.label == std::string(text::labels::contactRisk);
    }) != deepSurface.metrics.end(), "post-solar surface presentation should label contact risk");
    require(deepSurface.metrics[1].value.find("Perimeter drones") != std::string::npos, "surface presentation should name passive defense unlocks");
    require(findDetailPresentationRow(deepSurface.details, text::panel::details::hostileContact) != nullptr, "post-solar surface details should explain hostile contact");
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
    state.meta.materials = {.common = 3, .rare = 2, .exotic = 1};
    state.meta.ownedModuleIds.push_back(content::module::cryoLoop);
    state.meta.defaultEquippedModuleIds.push_back(content::module::cryoLoop);
    state.meta.artifacts.push_back({"mars_signal_1", content::destination::mars, true});
    state.meta.shipsLost = 1;
    state.meta.closestSurvivalMargin = 0.04;
    state.meta.closestSurvivalBurn = 2.78;
    state.meta.closestSurvivalFailurePoint = 2.82;
    state.meta.maxBurnDepth = 3.48;
    state.meta.maxPeakWarning = 1.0;
    state.meta.maxPeakAbortRisk = 0.94;
    state.meta.bestCreditDelta = 524.0;
    state.meta.worstCreditDelta = -30.0;
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
    require(restored.meta.materials.common == 3 && restored.meta.materials.rare == 2 && restored.meta.materials.exotic == 1, "materials should round trip");
    require(std::find(restored.meta.ownedModuleIds.begin(), restored.meta.ownedModuleIds.end(), content::module::cryoLoop) != restored.meta.ownedModuleIds.end(), "permanent shipyard modules should round trip");
    require(std::find(restored.meta.defaultEquippedModuleIds.begin(), restored.meta.defaultEquippedModuleIds.end(), content::module::cryoLoop) != restored.meta.defaultEquippedModuleIds.end(), "default shipyard loadout should round trip");
    require(restored.meta.artifacts.size() == 1 && restored.meta.artifacts[0].identified, "artifacts should round trip");
    require(std::abs(restored.meta.closestSurvivalMargin - 0.04) < 0.001, "closest survival margin should round trip");
    require(std::abs(restored.meta.closestSurvivalBurn - 2.78) < 0.001, "closest survival burn should round trip");
    require(std::abs(restored.meta.closestSurvivalFailurePoint - 2.82) < 0.001, "closest survival failure point should round trip");
    require(std::abs(restored.meta.maxBurnDepth - 3.48) < 0.001, "max burn depth should round trip");
    require(std::abs(restored.meta.maxPeakWarning - 1.0) < 0.001, "max peak warning should round trip");
    require(std::abs(restored.meta.maxPeakAbortRisk - 0.94) < 0.001, "max peak abort should round trip");
    require(std::abs(restored.meta.bestCreditDelta - 524.0) < 0.001, "best credit delta should round trip");
    require(std::abs(restored.meta.worstCreditDelta + 30.0) < 0.001, "worst credit delta should round trip");
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
    require(text.find(std::string(save_schema::field::ownedModules) + save_schema::keyValueDelimiter) != std::string::npos, "owned modules key should use shared schema name");
    require(text.find(std::string(save_schema::field::defaultEquippedModules) + save_schema::keyValueDelimiter) != std::string::npos, "default equipped modules key should use shared schema name");
    require(text.find(std::string(save_schema::field::screen) + save_schema::keyValueDelimiter) != std::string::npos, "screen key should use shared schema name");
    require(text.find(std::string(save_schema::field::chapter) + save_schema::keyValueDelimiter) != std::string::npos, "chapter key should use shared schema name");
    require(text.find(std::string(save_schema::field::materials) + save_schema::keyValueDelimiter) != std::string::npos, "materials key should use shared schema name");
    require(text.find(std::string(save_schema::field::surfaceSite) + save_schema::keyValueDelimiter) != std::string::npos, "surface site key should use shared schema name");
    require(text.find(std::string(save_schema::field::surfaceLog) + save_schema::keyValueDelimiter) != std::string::npos, "surface log key should use shared schema name");
    require(text.find(std::string(save_schema::field::surfaceUpgrades) + save_schema::keyValueDelimiter) != std::string::npos, "surface upgrades key should use shared schema name");
    require(text.find(std::string(save_schema::field::surfaceUpgradeOffers) + save_schema::keyValueDelimiter) != std::string::npos, "surface upgrade offers key should use shared schema name");
    require(text.find(std::string(save_schema::field::droneBaySlots) + save_schema::keyValueDelimiter) != std::string::npos, "drone bay slots key should use shared schema name");
    require(text.find(std::string(save_schema::field::ownedDrones) + save_schema::keyValueDelimiter) != std::string::npos, "owned drones key should use shared schema name");
    require(text.find(std::string(save_schema::field::equippedDrones) + save_schema::keyValueDelimiter) != std::string::npos, "equipped drones key should use shared schema name");
    require(text.find(std::string(save_schema::field::droneUpgrades) + save_schema::keyValueDelimiter) != std::string::npos, "drone tuning key should use shared schema name");
    require(text.find(std::string(1, save_schema::textListDelimiter)) != std::string::npos, "text list delimiter should be shared");

    const std::string minimalSave = std::string(save_schema::header) + "\n" +
        std::string(save_schema::field::credits) + save_schema::keyValueDelimiter + "321\n";
    const auto parsed = deserializeSaveData(minimalSave);
    require(parsed.has_value(), "minimal save with shared header should parse");
    require(std::abs(parsed->credits - 321.0) < 0.001, "shared credits key should parse");
    require(!deserializeSaveData("RR_SAVE_V0\ncredits=1\n").has_value(), "unknown save header should not parse");
}

void legacyRecordsTrackAchievementStats()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 909);

    LaunchOutcome first;
    first.type = LaunchResultType::MissionComplete;
    first.recoveryMethod = RecoveryMethod::ReturnHome;
    first.destinationId = content::destination::earthOrbit;
    first.ejectMultiplier = 2.78;
    first.crashMultiplier = 2.82;
    first.payout = 600.0;
    first.recoveryCost = 76.0;
    first.peakWarning = 1.0;
    first.peakAbortRisk = 0.99;
    applyLaunchOutcome(state, catalog, first);

    require(std::abs(state.meta.closestSurvivalMargin - 0.04) < 0.001, "closest survival margin should track close successful recoveries");
    require(std::abs(state.meta.closestSurvivalBurn - 2.78) < 0.001, "closest survival burn should track the recovered burn depth");
    require(std::abs(state.meta.closestSurvivalFailurePoint - 2.82) < 0.001, "closest survival failure point should track the hidden failure");
    require(std::abs(state.meta.maxBurnDepth - 2.78) < 0.001, "max burn should track launch depth");
    require(std::abs(state.meta.maxPeakWarning - 1.0) < 0.001, "max warning should track peak telemetry");
    require(std::abs(state.meta.maxPeakAbortRisk - 0.99) < 0.001, "max abort should track peak abort");
    require(std::abs(state.meta.bestCreditDelta - 584.0) < 0.001, "best credit delta should include close-call bonus rewards");
    require(std::abs(state.lastOutcome.payout - 660.0) < 0.001, "skin-of-your-teeth outcomes should add a ten percent mission credit bonus");

    LaunchOutcome later = first;
    later.ejectMultiplier = 3.20;
    later.crashMultiplier = 3.90;
    later.payout = 0.0;
    later.recoveryCost = 15.0;
    later.peakWarning = 0.50;
    later.peakAbortRisk = 0.45;
    applyLaunchOutcome(state, catalog, later);

    require(std::abs(state.meta.closestSurvivalMargin - 0.04) < 0.001, "wider recoveries should not replace the closest survival");
    require(std::abs(state.meta.maxBurnDepth - 3.20) < 0.001, "max burn should continue to update independently");
    require(std::abs(state.meta.worstCreditDelta + 15.0) < 0.001, "worst credit delta should track expensive recoveries");
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
    require(presentation.metricGroups.size() == 3, "result presentation should group post-flight metrics");
    require(presentation.metricGroups[0].title == text::panel::sections::missionResult, "mission result group should lead the result summary");
    require(presentation.metricGroups[0].metrics.size() == 3, "mission result group should show outcome, recovery, and credits");
    require(presentation.metricGroups[1].title == text::panel::sections::burnProfile, "burn profile group should be explicit");
    require(presentation.metricGroups[2].title == text::panel::sections::peakTelemetry, "peak telemetry group should be explicit");
    require(presentation.notes.size() == 2, "destroyed outcomes should include module and crew notes");
    require(presentation.notes[0] == text::panel::lostModule(content::module::sparrowEngine), "lost module note should be shared");
    require(presentation.notes[1] == std::string(text::panel::messages::crewLossRecorded), "crew death note should be shared");
    require(presentation.crewFate.active && presentation.crewFate.cssClass == std::string_view("lost"), "crew death should create a major memorial result beat");
    require(presentation.crewFate.title == text::panel::crewFate::lostTitle, "crew death result beat should use memorial copy");

    LaunchOutcome recoveredFailure;
    recoveredFailure.type = LaunchResultType::Destroyed;
    recoveredFailure.recoveryMethod = RecoveryMethod::ManualEject;
    presentation = launchOutcomePresentation(recoveredFailure);
    require(presentation.crewFate.active && presentation.crewFate.cssClass == std::string_view("recovered"), "surviving a vehicle loss should create a major rescue result beat");
    require(presentation.crewFate.title == text::panel::crewFate::recoveredTitle, "survived failure result beat should use rescue copy");

    LaunchOutcome transfer;
    transfer.type = LaunchResultType::MissionComplete;
    transfer.recoveryMethod = RecoveryMethod::TransferArrival;
    transfer.frontierTransfer = true;
    presentation = launchOutcomePresentation(transfer);
    require(presentation.label == text::panel::outcomes::transferComplete, "successful transfer should share transfer label");
    require(presentation.nextActionLabel == text::buttons::reviewRefitOptions, "survived outcomes should share refit review action label");
    require(presentation.notes.empty(), "clean transfer should not invent result notes");

    presentation = launchOutcomePresentation(transfer, true);
    require(presentation.nextActionLabel == text::buttons::arrivalOps, "post-arrival outcomes should route toward the approach choice");
    require(presentation.notes.size() == 1 && presentation.notes[0] == std::string(text::panel::messages::postArrivalResearchReady), "post-arrival outcomes should explain the research handoff");

    LaunchOutcome injuredEject;
    injuredEject.type = LaunchResultType::SafeEject;
    injuredEject.recoveryMethod = RecoveryMethod::ManualEject;
    injuredEject.crewInjured = true;
    presentation = launchOutcomePresentation(injuredEject);
    require(presentation.label == text::panel::outcomes::emergencyEject, "manual ejection should share emergency eject label");
    require(presentation.notes.size() == 1 && presentation.notes[0] == std::string(text::panel::messages::crewInjured), "crew injury note should be shared");

    LaunchOutcome closeCall;
    closeCall.type = LaunchResultType::MissionComplete;
    closeCall.recoveryMethod = RecoveryMethod::ReturnHome;
    closeCall.ejectMultiplier = 2.78;
    closeCall.crashMultiplier = 2.82;
    presentation = launchOutcomePresentation(closeCall);
    require(presentation.notes.empty(), "close recoveries should not be rendered as generic mission notes");
    require(presentation.achievements.size() == 1, "close recoveries should unlock a close-call achievement");
    require(presentation.achievements[0].id == content::achievement::skinOfYourTeeth, "close-call achievement should use a stable content id");
    require(presentation.achievements[0].title == text::panel::achievements::skinOfYourTeethTitle, "close-call achievement should expose a clear title");
    require(presentation.achievements[0].detail.find("x0.04") != std::string::npos, "close-call achievement should explain the survival margin");
    require(presentation.achievements[0].detail.find("+10% mission credits") != std::string::npos, "close-call achievement should explain the credit bonus");
}

void enumDisplayLabelsComeFromSharedText()
{
    require(toString(SlotType::Engine) == text::enums::slot::engine, "slot labels should come from shared text");
    require(toString(SlotType::Cooling) == text::enums::slot::cooling, "cooling slot label should come from shared text");
    require(toString(Rarity::Prototype) == text::enums::rarity::prototype, "rarity labels should come from shared text");
    require(toString(CrewStatus::Injured) == text::enums::crewStatus::injured, "crew status labels should come from shared text");
    require(toString(LaunchResultType::MissionComplete) == text::enums::launchResult::missionComplete, "launch result labels should come from shared text");
    require(toString(RecoveryMethod::ReturnHome) == text::enums::recovery::returnHome, "recovery labels should come from shared text");
    require(toString(GameChapter::Arkfall) == std::string_view("Arkfall"), "chapter labels should come from shared enum display");
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
    require(window.resourceChips.empty(), "refit window should not show an empty recovered resource bank");
    require(window.recoveryDetail.empty(), "refit window should not show recovery detail without resources or extraction");

    const RefitOfferPresentation& moduleOffer = window.offers[0];
    require(moduleOffer.kind == RefitOfferPresentationKind::ShipModule, "module offers should be typed for future render variants");
    require(moduleOffer.index == 0, "module offers should retain their buy-offer index");
    require(moduleOffer.cost == moduleOfferCost(*engine), "module offer presentation should use shared module pricing");
    require(moduleOffer.costSummary == display::credits(moduleOfferCost(*engine)), "credit-only module offer should expose shared cost summary");
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
    require(crewOffer.costSummary == display::credits(crewUpgradeCost(*simBay)), "crew offer should expose shared cost summary");
    require(crewOffer.card.title == simBay->name, "crew offer should include shared card presentation");
    require(crewOffer.action.actionId == ui::actions::buyOffer(1), "crew offer should use shared indexed buy action");

    require(window.rerollCost == offerRerollCost(state), "refit window should expose shared reroll cost");
    require(window.rerollAction.enabled, "affordable reroll should be enabled");
    require(window.rerollAction.label == text::panel::rerollOffers(display::money(window.rerollCost)), "reroll action should use shared label formatting");
    require(window.rerollAction.actionId == std::string(ui::actions::rerollOffers), "reroll action should use shared action id");
    require(window.rerollAction.cssClass == "warn", "reroll action should expose warning button style");
    require(window.skipAction.enabled && window.skipAction.label == std::string(text::buttons::skipRefit), "skip refit action should always be available");
    require(window.skipAction.actionId == std::string(ui::actions::next), "skip refit action should advance through shared action id");

    state.meta.blueprintProgress = 3;
    state.meta.materials = {.common = 2, .rare = 1};
    state.meta.artifacts.push_back({"mars_artifact_refit", content::destination::mars, false});
    const RefitWindowPresentation resourceWindow = refitWindowPresentation(state, catalog);
    require(resourceWindow.recoveryDetail == std::string(text::panel::messages::recoveredResourcesDetail), "refit resource bank should explain resource use");
    require(resourceWindow.resourceChips.size() == 4, "refit window should expose recovered blueprint, material, and artifact resources");
    require(std::find_if(resourceWindow.resourceChips.begin(), resourceWindow.resourceChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == std::string(text::labels::blueprints) && chip.value == "3";
    }) != resourceWindow.resourceChips.end(), "refit resource bank should expose blueprint progress");
    require(std::find_if(resourceWindow.resourceChips.begin(), resourceWindow.resourceChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == std::string(text::labels::commonMaterials) && chip.value == "2";
    }) != resourceWindow.resourceChips.end(), "refit resource bank should expose common materials");
    require(std::find_if(resourceWindow.resourceChips.begin(), resourceWindow.resourceChips.end(), [](const PanelMetricPresentation& chip) {
        return chip.label == std::string(text::labels::artifacts) && chip.value == "1";
    }) != resourceWindow.resourceChips.end(), "refit resource bank should expose recovered artifacts");

    state.meta.blueprintProgress = 0;
    state.meta.materials = {};
    state.meta.artifacts = {};
    state.statusLine = std::string(text::status::surfaceExtractionRough) + " (Lost 1 Artifacts; 42% Extraction risk)";
    const RefitWindowPresentation roughRecoveryWindow = refitWindowPresentation(state, catalog);
    require(roughRecoveryWindow.resourceChips.empty(), "rough empty extraction should not invent recovered resources");
    require(roughRecoveryWindow.recoveryDetail == state.statusLine, "rough empty extraction should still explain the recovery result");

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
    const DetailPresentationRow* crewClass = findDetailPresentationRow(rows, text::panel::details::crewClass);
    const DetailPresentationRow* training = findDetailPresentationRow(rows, text::panel::details::training);
    const DetailPresentationRow* stress = findDetailPresentationRow(rows, text::panel::details::stress);
    const DetailPresentationRow* simulatorStress = findDetailPresentationRow(rows, text::panel::details::simulatorStress);

    require(active != nullptr && active->value == pilot->name, "crew presentation should expose active astronaut name");
    require(crewClass != nullptr && crewClass->value == pilot->background, "crew presentation should expose the animal class");
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
    state.meta.materials = {.common = 3, .rare = 2, .exotic = 1};
    state.meta.artifacts = {
        {"mars_signal_1", content::destination::mars, true},
        {"mars_signal_2", content::destination::mars, false}
    };
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
    const DetailPresentationRow* commonMaterials = findDetailPresentationRow(legacyRows, text::panel::details::commonMaterials);
    const DetailPresentationRow* rareMaterials = findDetailPresentationRow(legacyRows, text::panel::details::rareMaterials);
    const DetailPresentationRow* exoticMaterials = findDetailPresentationRow(legacyRows, text::panel::details::exoticMaterials);
    const DetailPresentationRow* artifacts = findDetailPresentationRow(legacyRows, text::panel::details::artifacts);
    const DetailPresentationRow* shipsLost = findDetailPresentationRow(legacyRows, text::panel::details::shipsLost);
    const DetailPresentationRow* astronautsLost = findDetailPresentationRow(legacyRows, text::panel::details::astronautsLost);
    const DetailPresentationRow* furthestTier = findDetailPresentationRow(legacyRows, text::panel::details::furthestTier);
    const DetailPresentationRow* closestSurvival = findDetailPresentationRow(legacyRows, text::panel::details::closestSurvival);
    const DetailPresentationRow* maxBurnDepth = findDetailPresentationRow(legacyRows, text::panel::details::maxBurnDepth);
    const DetailPresentationRow* maxPeakWarning = findDetailPresentationRow(legacyRows, text::panel::details::maxPeakWarning);
    const DetailPresentationRow* maxPeakAbort = findDetailPresentationRow(legacyRows, text::panel::details::maxPeakAbort);
    require(blueprints != nullptr && blueprints->value == "9", "legacy presentation should expose blueprint progress");
    require(commonMaterials != nullptr && commonMaterials->value == "3", "legacy presentation should expose common materials");
    require(rareMaterials != nullptr && rareMaterials->value == "2", "legacy presentation should expose rare materials");
    require(exoticMaterials != nullptr && exoticMaterials->value == "1", "legacy presentation should expose exotic materials");
    require(artifacts != nullptr && artifacts->value == text::panel::artifactSummary(1, 2), "legacy presentation should expose artifact recovery summary");
    require(shipsLost != nullptr && shipsLost->value == "3", "legacy presentation should expose ship losses");
    require(astronautsLost != nullptr && astronautsLost->value == "2", "legacy presentation should expose astronaut losses");
    require(furthestTier != nullptr && furthestTier->value == "1", "legacy presentation should expose furthest tier");
    require(closestSurvival != nullptr, "legacy presentation should expose closest survival record");
    require(maxBurnDepth != nullptr, "legacy presentation should expose max burn record");
    require(maxPeakWarning != nullptr, "legacy presentation should expose peak warning record");
    require(maxPeakAbort != nullptr, "legacy presentation should expose peak abort record");

    const std::vector<DetailPresentationRow> archiveRows = artifactArchivePresentation(state, catalog);
    require(hasDetailPresentationHeader(archiveRows, text::panel::details::artifactArchive), "artifact archive should include a section header when artifacts exist");
    const DetailPresentationRow* firstArtifact = findDetailPresentationRow(archiveRows, "Mars artifact 1");
    const DetailPresentationRow* secondArtifact = findDetailPresentationRow(archiveRows, "Mars artifact 2");
    require(firstArtifact != nullptr && firstArtifact->value == std::string(text::panel::details::decoded), "artifact archive should show decoded artifact status");
    require(secondArtifact != nullptr && secondArtifact->value == std::string(text::panel::details::awaitingResearch), "artifact archive should show unidentified artifact status");

    const std::vector<DetailPresentationRow> combinedLegacyRows = legacyDetailsPresentation(state, catalog);
    require(hasDetailPresentationHeader(combinedLegacyRows, text::panel::details::artifactArchive), "catalog-aware legacy presentation should include artifact archive rows");

    GameState noArtifacts = createNewGame(catalog, 446);
    require(artifactArchivePresentation(noArtifacts, catalog).empty(), "artifact archive should stay hidden until artifacts are recovered");
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
    require(returnHome != nullptr && returnHome->enabled && returnHome->actionId == ui::actions::returnHome, "launch presentation should expose return-home action");
    require(eject != nullptr && eject->enabled && eject->cssClass == "danger", "launch presentation should expose eject danger action");
    require(findFlightActionButton(panel.primaryActions, text::buttons::arrivalOps) == nullptr, "launch presentation should not expose a manual approach button");
    require(panel.systemActions.empty(), "first Earth Orbit proving flights should teach only return/eject controls");

    GameState moonState = createNewGame(catalog, 908);
    moonState.run.destinationIndex = 1;
    syncLaunchConfig(moonState, catalog);
    Random moonRng(908);
    PreparedLaunch moonLaunch = prepareLaunch(moonState, catalog, moonRng);
    panel = launchPanelPresentation(
        moonState,
        catalog,
        moonLaunch,
        currentMultiplier,
        1.0,
        0.0,
        tuning::session::returnDefaultDuration,
        {},
        false);
    const FlightActionButtonPresentation* cutEngines = findFlightActionButton(panel.systemActions, text::buttons::cutEngines);
    const FlightActionButtonPresentation* reliefValve = findFlightActionButton(panel.systemActions, text::buttons::reliefValve);
    const FlightActionButtonPresentation* jettisonCargo = findFlightActionButton(panel.systemActions, text::buttons::jettisonCargo);
    require(cutEngines != nullptr && cutEngines->enabled && cutEngines->actionId == ui::actions::cutEngines, "Moon-tier launch presentation should expose cut-engines action");
    require(reliefValve != nullptr && reliefValve->enabled && reliefValve->actionId == ui::actions::pressureRelief, "Moon-tier launch presentation should expose relief-valve action");
    require(jettisonCargo != nullptr && jettisonCargo->enabled && jettisonCargo->actionId == ui::actions::jettisonCargo, "Moon-tier launch presentation should expose jettison-cargo action");

    FlightActionState returning;
    returning.returningHome = true;
    const double returnBurnMultiplier = 1.32;
    const double returnElapsed = 1.2;
    const double returnDuration = tuning::session::returnDefaultDuration;
    panel = launchPanelPresentation(
        moonState,
        catalog,
        moonLaunch,
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
    require(burn != nullptr && burn->value == display::multiplier(returnTelemetryMultiplier(returnBurnMultiplier, moonLaunch.crashMultiplier, returnElapsed, returnDuration)), "launch presentation should share return telemetry multiplier");
    require(returnProgress != nullptr && returnProgress->value == display::percent(flight_progress::returnCompletion(returnElapsed, returnDuration)), "launch presentation should share return progress math");
    require(returnHome != nullptr && !returnHome->enabled, "returning-home action should be disabled once committed");
    require(cutEngines != nullptr && !cutEngines->enabled, "system actions should be disabled during return home");

    GameState arkState = createNewGame(catalog, 909);
    discoverArk(arkState, catalog);
    Random arkRng(909);
    PreparedLaunch arkLaunch = prepareLaunch(arkState, catalog, arkRng);
    panel = launchPanelPresentation(
        arkState,
        catalog,
        arkLaunch,
        currentMultiplier,
        1.0,
        0.0,
        returnDuration,
        {},
        false);
    returnHome = findFlightActionButton(panel.primaryActions, "Return to Ark");
    require(returnHome != nullptr && returnHome->enabled && returnHome->actionId == ui::actions::returnHome, "Ark launch presentation should relabel the return action without changing its action id");

    FlightActionState reliefOpen;
    reliefOpen.pressureReliefOpen = true;
    panel = launchPanelPresentation(moonState, catalog, moonLaunch, currentMultiplier, 1.0, 0.0, returnDuration, reliefOpen, true);
    const FlightActionButtonPresentation* closeValve = findFlightActionButton(panel.systemActions, text::buttons::closeValve);
    require(closeValve != nullptr && closeValve->enabled && closeValve->actionId == ui::actions::closeReliefValve, "open relief valve should expose close-valve action");

    reliefOpen.pressureReliefFailed = true;
    panel = launchPanelPresentation(moonState, catalog, moonLaunch, currentMultiplier, 1.0, 0.0, returnDuration, reliefOpen, true);
    const FlightActionButtonPresentation* failedValve = findFlightActionButton(panel.systemActions, text::buttons::reliefValveFailed);
    require(failedValve != nullptr && !failedValve->enabled, "failed relief valve should be disabled in presentation");

    GameState marsState = createNewGame(catalog, 911);
    marsState.run.destinationIndex = 2;
    syncLaunchConfig(marsState, catalog);
    Random marsRng(911);
    PreparedLaunch marsLaunch = prepareLaunch(marsState, catalog, marsRng);
    panel = launchPanelPresentation(
        marsState,
        catalog,
        marsLaunch,
        catalog.destinations[2].targetMultiplier,
        1.0,
        0.0,
        returnDuration,
        {},
        false);
    require(findFlightActionButton(panel.primaryActions, text::buttons::arrivalOps) == nullptr, "Mars proving flights should auto-open approach instead of exposing an approach button");
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
    repairAction = findFlightActionButton(readiness.actions, text::buttons::assignRepairBay);
    const FlightActionButtonPresentation* recruitAction = findFlightActionButton(readiness.actions, text::buttons::recruitCrew);
    require(readiness.blocked && readiness.hullBlocked && readiness.crewBlocked, "destroyed hull and dead roster should both block launch readiness");
    require(readiness.messages.size() == 2, "combined launch hold should expose both hold messages");
    require(requiredAction != nullptr && requiredAction->value == text::panel::details::repairAndRecruitCrew, "combined launch hold should require repair and recruit");
    require(hangarOps.repairAvailable && hangarOps.repairCost == 0.0, "unfunded totaled hull should expose salvage repair");
    require(repairAction != nullptr && repairAction->enabled && repairAction->actionId == ui::actions::repairShip, "unfunded hull readiness should expose salvage repair action");
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
    const PanelMetricPresentation* chapter = findPanelMetric(metrics, text::labels::chapter);
    const PanelMetricPresentation* hullDamage = findPanelMetric(metrics, text::labels::hullDamage);
    const PanelMetricPresentation* currentFrontier = findPanelMetric(metrics, text::labels::currentFrontier);
    const PanelMetricPresentation* flightData = findPanelMetric(metrics, text::labels::flightData);
    const PanelMetricPresentation* difficulty = findPanelMetric(metrics, text::labels::missionDifficulty);
    const PanelMetricPresentation* crewStress = findPanelMetric(metrics, text::labels::crewStress);

    require(metrics.size() == 7, "panel chrome should expose seven top-level metrics");
    require(credits != nullptr && credits->value == display::money(state.run.credits), "panel chrome should format mission credits");
    require(chapter != nullptr && chapter->value == "Chapter 1: Proving Ground", "panel chrome should expose the current chapter");
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

void panelHtmlIncludesContextualTutorialLayer()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState launchState = createNewGame(catalog, 711);
    launchState.screen = Screen::Launch;
    Random launchRng(711);
    const PreparedLaunch launch = prepareLaunch(launchState, catalog, launchRng);
    PanelRenderContext launchContext {launchState, catalog, launch, launch};
    launchContext.currentMultiplier = 1.12;
    const std::string launchHtml = buildGamePanelHtml(launchContext);
    require(launchHtml.find("<h1>Flight</h1>") != std::string::npos, "launch panel should title the current phase instead of repeating the game title");
    require(launchHtml.find("class=\"cockpit-hud flight-hud\"") != std::string::npos, "launch controls should render in a cockpit HUD");
    require(launchHtml.find("data-help-topic=\"launch-controls\"") != std::string::npos, "launch panel should introduce return/eject/mitigation help");
    require(launchHtml.find("Return to Earth banks data") != std::string::npos, "launch help should mention returning to Earth");
    require(launchHtml.find("unlock the Moon route") != std::string::npos, "first launch help should keep the intro focused on return/eject");
    require(launchHtml.find("data-help-toggle") != std::string::npos, "settings should expose a help toggle");
    require(launchHtml.find("data-camera-shake-toggle") != std::string::npos, "settings should expose a camera shake toggle");
    require(launchHtml.find("data-debug-tools-toggle") != std::string::npos, "settings should expose a debug mini-games toggle");

    GameState moonLaunchState = createNewGame(catalog, 713);
    moonLaunchState.run.destinationIndex = 1;
    moonLaunchState.screen = Screen::Launch;
    syncLaunchConfig(moonLaunchState, catalog);
    Random moonLaunchRng(713);
    const PreparedLaunch moonLaunch = prepareLaunch(moonLaunchState, catalog, moonLaunchRng);
    PanelRenderContext moonLaunchContext {moonLaunchState, catalog, moonLaunch, moonLaunch};
    moonLaunchContext.currentMultiplier = 1.12;
    const std::string moonLaunchHtml = buildGamePanelHtml(moonLaunchContext);
    require(moonLaunchHtml.find("relief valve") != std::string::npos, "Moon-tier launch help should mention mitigation controls");

    GameState arkLaunchState = createNewGame(catalog, 714);
    discoverArk(arkLaunchState, catalog);
    arkLaunchState.screen = Screen::Launch;
    Random arkLaunchRng(714);
    const PreparedLaunch arkLaunch = prepareLaunch(arkLaunchState, catalog, arkLaunchRng);
    PanelRenderContext arkLaunchContext {arkLaunchState, catalog, arkLaunch, arkLaunch};
    arkLaunchContext.currentMultiplier = 1.12;
    const std::string arkLaunchHtml = buildGamePanelHtml(arkLaunchContext);
    require(arkLaunchHtml.find("Return to Ark banks data") != std::string::npos, "Ark launch help should relabel the return destination");
    require(arkLaunchHtml.find(">Return to Ark</button>") != std::string::npos, "Ark launch controls should show the Ark return label");

    launchContext.flightArmed = false;
    const std::string preflightHtml = buildGamePanelHtml(launchContext);
    require(preflightHtml.find("data-preflight-launch=\"1\"") != std::string::npos, "pre-flight panel should signal the scene launch overlay");
    require(preflightHtml.find(">Return to Earth</button>") == std::string::npos, "pre-flight panel should hide recovery controls until launch");
    require(preflightHtml.find("Start the burn when ready") != std::string::npos, "pre-flight panel should explain the launch hold");

    GameState arrivalState = createNewGame(catalog, 712);
    LaunchOutcome arrival;
    arrival.type = LaunchResultType::MissionComplete;
    arrival.frontierTransfer = true;
    arrival.destinationId = content::destination::moon;
    arrival.recoveryMethod = RecoveryMethod::TransferArrival;
    arrival.ejectMultiplier = 1.95;
    arrival.crashMultiplier = 2.4;
    arrival.payout = 120.0;
    arrival.peakWarning = 0.42;
    startArrivalOps(arrivalState, arrival);
    arrivalState.lastOutcome = arrival;
    arrivalState.screen = Screen::ArrivalOps;
    Random arrivalRng(712);
    const PreparedLaunch arrivalLaunch = prepareLaunch(arrivalState, catalog, arrivalRng);
    const std::string arrivalHtml = buildGamePanelHtml({arrivalState, catalog, arrivalLaunch, arrivalLaunch});
    require(arrivalHtml.find("data-help-topic=\"arrival-ops\"") != std::string::npos, "arrival ops panel should introduce flyby/orbit/land help");
    require(arrivalHtml.find("Flyby is the safest scan") != std::string::npos, "arrival help should explain flyby/orbit/landing progression");
    require(arrivalHtml.find("Arrival summary") != std::string::npos, "arrival ops panel should consolidate debrief summary");
    require(arrivalHtml.find("Mission result") != std::string::npos, "arrival ops panel should include outcome metrics");

    arrivalState.screen = Screen::ArrivalFanfare;
    const std::string fanfareHtml = buildGamePanelHtml({arrivalState, catalog, arrivalLaunch, arrivalLaunch});
    require(fanfareHtml.find("<h1>Arrival</h1>") != std::string::npos, "arrival fanfare should title the transient phase");
    require(fanfareHtml.find("data-arrival-fanfare=\"1\"") != std::string::npos, "arrival fanfare should signal the scene overlay");
    require(fanfareHtml.find("data-arrival-destination=\"Moon\"") != std::string::npos, "arrival fanfare should expose the destination to the overlay");
    require(fanfareHtml.find("Arrival lock") != std::string::npos, "arrival fanfare panel should present a compact confirmation readout");
    const SaveData fanfareSave = captureSaveData(arrivalState);
    require(fanfareSave.screen == Screen::ArrivalOps, "arrival fanfare should persist as approach so reloads do not resume a transient screen");
    GameState restoredArrival = createNewGame(catalog, 713);
    restoreSaveData(restoredArrival, catalog, fanfareSave);
    require(restoredArrival.screen == Screen::ArrivalOps, "restoring during arrival fanfare should resume at approach");

    GameState miningState = createNewGame(catalog, 713);
    miningState.run.destinationIndex = 2;
    startSurfaceExpedition(miningState, catalog);
    prepareMiningSiteForTest(miningState);
    require(startMiningRun(miningState, catalog).applied, "test mining run should start");
    Random miningRng(713);
    const PreparedLaunch miningLaunch = prepareLaunch(miningState, catalog, miningRng);
    const std::string miningHtml = buildGamePanelHtml({miningState, catalog, miningLaunch, miningLaunch});
    require(miningHtml.find("<h1>Mining</h1>") != std::string::npos, "mining panel should title the mining phase");
    require(miningHtml.find("data-panel-mode=\"mining-fullscreen\"") != std::string::npos, "mining should opt into the full-screen HUD mode");
    require(miningHtml.find("class=\"mining-fullscreen\"") != std::string::npos, "mining controls should render in the full-screen HUD");
    require(miningHtml.find("class=\"cockpit-hud mining-hud\"") == std::string::npos, "mining should not render the old compact cockpit HUD");
    require(miningHtml.find("class=\"mining-health-strip\"") != std::string::npos, "mining panel should expose a prominent drone health strip");
    require(miningHtml.find("Drone health") != std::string::npos, "mining panel should label drone health");
    require(miningHtml.find("Oxygen") != std::string::npos, "mining panel should expose oxygen");
    require(miningHtml.find("Next fuel") != std::string::npos, "mining panel should expose fuel cadence");
    require(miningHtml.find("Drill bit") != std::string::npos, "mining panel should label drill bit health");
    require(miningHtml.find("Drill heat") != std::string::npos, "mining panel should expose drill heat");
    require(miningHtml.find("Carried cargo") != std::string::npos, "mining panel should expose carried payload");
    require(miningHtml.find("Banked cargo") != std::string::npos, "mining panel should expose banked payload");
    require(miningHtml.find("Carried mats") != std::string::npos, "mining panel should expose carried materials");
    require(miningHtml.find("Banked mats") != std::string::npos, "mining panel should expose banked materials");
    require(miningHtml.find("Load") != std::string::npos, "mining panel should expose load burden");
    require(miningHtml.find("data-rr-action=\"mining_scanner\"") != std::string::npos, "mining HUD should keep scanner control");
    require(miningHtml.find("data-rr-action=\"mining_tether\"") != std::string::npos, "mining HUD should keep tether control");
    require(miningHtml.find("data-rr-action=\"mining_abort\"") != std::string::npos, "mining HUD should keep abort control away from the ship");
    require(miningHtml.find("data-help-topic=\"mining-basics\"") == std::string::npos, "mining panel should not show the old tutorial card inline");
    require(miningHtml.find("WASD/arrows move") != std::string::npos, "mining details should retain movement controls");
    require(miningHtml.find("Combat read") != std::string::npos, "mining details should include a combat readability legend");
    require(miningHtml.find("Blue numbers") != std::string::npos, "mining details should explain allied and enemy damage text colors");

    GameState droneState = createNewGame(catalog, 715);
    droneState.screen = Screen::DroneOps;
    droneState.meta.unlockKeys.push_back(content::unlock::droneBay);
    droneState.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    ensureDroneBayState(droneState, catalog);
    droneState.meta.droneBaySlots = 3;
    droneState.meta.equippedDroneIds = {content::drone::attackDrone, content::drone::defenseDrone, content::drone::surveyDrone};
    Random droneRng(715);
    const PreparedLaunch droneLaunch = prepareLaunch(droneState, catalog, droneRng);
    const std::string droneHtml = buildGamePanelHtml({droneState, catalog, droneLaunch, droneLaunch});
    require(droneHtml.find("drone-bay-strip") != std::string::npos, "Drone Ops HTML should include the Drone Bay strip");
    require(droneHtml.find("drone-build-guidance") == std::string::npos, "Drone Ops HTML should not render build guidance inline");
    require(droneHtml.find("drone-loadout-bench") != std::string::npos, "Drone Ops HTML should include the loadout bench");
    require(droneHtml.find("Slot 1") != std::string::npos, "Drone Ops loadout bench should label slot positions");
    require(droneHtml.find("drone-combat-forecast") != std::string::npos, "Drone Ops HTML should include the combat forecast strip");
    require(droneHtml.find("Sentry output") != std::string::npos, "Drone Ops combat forecast should show passive combat output");
    require(droneHtml.find("Drone controls") != std::string::npos, "Drone Ops HTML should include compact drone controls");
    require(droneHtml.find("drone-control-card") != std::string::npos, "Drone Ops HTML should use compact drone control cards");
    require(droneHtml.find("drone-control-status") != std::string::npos, "Drone Ops compact controls should expose drone status");
    require(droneHtml.find("drone-build-strip") == std::string::npos, "Drone Ops HTML should not render the old active build strip inline");
    require(droneHtml.find("drone-recipe-board") == std::string::npos, "Drone Ops HTML should not render the old build recipe board inline");
    require(droneHtml.find("Sentry Killbox") != std::string::npos, "Drone Ops HTML should name active signature builds");
    require(droneHtml.find("Targeting Grid") != std::string::npos, "Drone Ops HTML should name active loadout synergies");
}

void surfaceHtmlPromotesMiningAction()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 714);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    state.screen = Screen::SurfaceExpedition;

    Random rng(714);
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
    const std::string html = buildGamePanelHtml({state, catalog, launch, launch});
    require(html.find("<h1>Surface Ops</h1>") != std::string::npos, "surface panel should title the surface phase");
    require(html.find("surface-ops-screen") != std::string::npos, "surface panel should use the compact surface ops layout");
    require(html.find("phase-titlebar phase-title-row") != std::string::npos, "surface panel titlebar should opt into the shared phase lane");
    require(html.find("surface-quickbar phase-lane phase-row") != std::string::npos, "surface panel should expose compact mission context inside the shared lane");
    require(html.find("ops-grid phase-action-grid") != std::string::npos, "surface panel action grid should use the shared action lane");
    require(html.find("phase-card-slot surface-action-slot") != std::string::npos, "surface panel action cards should render inside stable phase card slots");
    require(html.find("surface-action-slot is-last") != std::string::npos, "surface panel action grid should mark the final slot with no trailing margin");
    require(countOccurrences(html, "phase-card-slot surface-action-slot") == countOccurrences(html, "surface-action-card"),
        "every surface action card should have a fixed slot wrapper");
    require(html.find("featured-action") != std::string::npos, "surface panel should promote the mining action in the action grid");
    require(html.find("Mine deposit") != std::string::npos, "surface panel should keep mining obvious");
    require(html.find("Mine deposit") < html.find("Survey site"), "surface panel should put the mining action before secondary field actions");

    state.run.surfaceExpedition.miningRunUsed = true;
    const std::string usedHtml = buildGamePanelHtml({state, catalog, launch, launch});
    const std::size_t usedMine = usedHtml.find("Mine deposit");
    const std::size_t surveyAction = usedHtml.find("Survey site");
    require(usedMine != std::string::npos, "used mining action should still be visible in the compact surface actions");
    require(surveyAction != std::string::npos, "surface panel should keep field actions in the compact action grid");
    require(usedMine < surveyAction, "disabled mining action should stay first in the compact action grid");
    require(usedHtml.find("class=\"disabled\" disabled>Unavailable</button>") != std::string::npos,
        "disabled unavailable buttons should carry the muted disabled style hook");
}

void postArrivalPhaseHtmlUsesPolishedBoardStructure()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState arrivalState = createNewGame(catalog, 716);
    LaunchOutcome arrival;
    arrival.type = LaunchResultType::MissionComplete;
    arrival.frontierTransfer = true;
    arrival.destinationId = content::destination::moon;
    arrival.recoveryMethod = RecoveryMethod::TransferArrival;
    arrival.ejectMultiplier = 1.95;
    arrival.crashMultiplier = 2.4;
    arrival.payout = 120.0;
    arrival.peakWarning = 0.42;
    startArrivalOps(arrivalState, arrival);
    arrivalState.lastOutcome = arrival;
    arrivalState.screen = Screen::ArrivalOps;
    Random arrivalRng(716);
    const PreparedLaunch arrivalLaunch = prepareLaunch(arrivalState, catalog, arrivalRng);
    const std::string arrivalHtml = buildGamePanelHtml({arrivalState, catalog, arrivalLaunch, arrivalLaunch});
    require(arrivalHtml.find("phase-board phase-board-arrival") != std::string::npos, "approach should render inside the dedicated arrival board");
    require(arrivalHtml.find("class=\"result-grid\"") != std::string::npos, "approach should keep arrival summary in the result grid");
    require(countOccurrences(arrivalHtml, "ops-card arrival-card") == 3, "approach should render flyby, orbit, and landing as three action cards");
    require(arrivalHtml.find("phase-status") == std::string::npos, "approach should not duplicate the yellow mission status inside the board");

    GameState researchState = createNewGame(catalog, 717);
    researchState.run.destinationIndex = 2;
    researchState.meta.furthestTier = 2;
    researchState.meta.materials = {.common = 6, .rare = 2};
    researchState.screen = Screen::Research;
    Random researchOfferRng(717);
    generateResearchProjects(researchState, catalog, researchOfferRng);
    Random researchRng(718);
    const PreparedLaunch researchLaunch = prepareLaunch(researchState, catalog, researchRng);
    const std::string researchHtml = buildGamePanelHtml({researchState, catalog, researchLaunch, researchLaunch});
    require(researchHtml.find("phase-board phase-board-research") != std::string::npos, "research should render inside the dedicated research board");
    require(researchHtml.find("Research options") != std::string::npos, "research should expose the project board");
    require(researchHtml.find("class=\"ops-card\"") != std::string::npos, "research projects should render as styled cards");
    require(researchHtml.find("class=\"phase-advisory") != std::string::npos, "research should include advisory styling");
    require(researchHtml.find("phase-status") == std::string::npos, "research should not duplicate the yellow mission status inside the board");

    GameState surfaceState = createNewGame(catalog, 719);
    surfaceState.run.destinationIndex = 2;
    surfaceState.meta.unlockKeys.push_back(content::unlock::droneBay);
    startSurfaceExpedition(surfaceState, catalog);
    prepareMiningSiteForTest(surfaceState);
    surfaceState.screen = Screen::SurfaceExpedition;
    Random surfaceRng(719);
    const PreparedLaunch surfaceLaunch = prepareLaunch(surfaceState, catalog, surfaceRng);
    const std::string surfaceHtml = buildGamePanelHtml({surfaceState, catalog, surfaceLaunch, surfaceLaunch});
    require(surfaceHtml.find("phase-board phase-board-surface") != std::string::npos, "surface ops should render inside the dedicated surface board");
    require(surfaceHtml.find("surface-ops-screen") != std::string::npos, "surface ops should use the compact first-screen action layout");
    require(surfaceHtml.find("surface-quickbar") != std::string::npos, "surface ops should expose compact mission context");
    require(surfaceHtml.find("featured-action") != std::string::npos, "surface ops should promote mining inside the action grid");
    require(surfaceHtml.find("surface-action-card") != std::string::npos, "surface ops should keep actions in styled cards");
    require(surfaceHtml.find("drone-ops-callout phase-lane phase-row") != std::string::npos, "surface ops drone callout should align to the shared phase lane");
    require(surfaceHtml.find("phase-status") == std::string::npos, "surface ops should not duplicate the yellow mission status inside the board");

    GameState droneState = surfaceState;
    droneState.screen = Screen::DroneOps;
    Random droneRng(720);
    const PreparedLaunch droneLaunch = prepareLaunch(droneState, catalog, droneRng);
    const std::string droneHtml = buildGamePanelHtml({droneState, catalog, droneLaunch, droneLaunch});
    require(droneHtml.find("phase-board phase-board-drone-ops") != std::string::npos, "drone ops should render inside the dedicated drone board");
    require(droneHtml.find("drone-bay-strip") != std::string::npos, "drone ops should use the compact bay strip");
    require(droneHtml.find("focus-metrics") == std::string::npos, "drone ops should avoid a separate metric block above the roster");
    require(droneHtml.find("drone-control-grid") != std::string::npos, "drone ops should use the compact control grid");
    require(droneHtml.find("drone-control-card") != std::string::npos, "drone ops should use compact drone control cards");
    require(droneHtml.find("module-art") == std::string::npos, "drone ops should not reserve large art space in compact controls");

    GameState upgradeState = surfaceState;
    upgradeState.screen = Screen::SurfaceUpgrade;
    upgradeState.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::SurveyBasin;
    Random upgradeRng(721);
    generateSurfaceUpgradeOffers(upgradeState, catalog, upgradeRng);
    const PreparedLaunch upgradeLaunch = prepareLaunch(upgradeState, catalog, upgradeRng);
    const std::string upgradeHtml = buildGamePanelHtml({upgradeState, catalog, upgradeLaunch, upgradeLaunch});
    require(upgradeHtml.find("phase-board-surface-upgrade") != std::string::npos, "surface upgrade should render inside the dedicated field-upgrade board");
    require(upgradeHtml.find("draft-hero") != std::string::npos, "surface upgrade should use the same draft hero pattern as refit");
    require(upgradeHtml.find("surface-upgrade-card") != std::string::npos, "surface upgrades should render as tactile draft cards");
    require(upgradeHtml.find("draft-actions") != std::string::npos, "surface upgrade should keep reroll and skip actions centered as a draft row");
    require(upgradeHtml.find("phase-status") == std::string::npos, "surface upgrade should not duplicate the yellow mission status inside the board");
    const std::size_t contextStart = upgradeHtml.find("draft-context");
    const std::size_t contextEnd = upgradeHtml.find("</div></section>", contextStart);
    require(contextStart != std::string::npos && contextEnd != std::string::npos, "surface upgrade should render a bounded field context chip row");
    const std::string contextHtml = upgradeHtml.substr(contextStart, contextEnd - contextStart);
    require(contextHtml.find("Risk ") != std::string::npos, "surface upgrade context should use compact risk chip text");
    require(contextHtml.find("Extraction risk") == std::string::npos, "surface upgrade context should avoid clipped long risk labels");
    require(contextHtml.find("Site Basin") != std::string::npos, "surface upgrade context should use compact site chip text");
    require(contextHtml.find("Survey Basin") == std::string::npos, "surface upgrade context should avoid clipped long site labels");
}

void hangarHtmlShowsPilotIntakeModal()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 715);
    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = CrewStatus::Dead;
    }
    syncLaunchConfig(state, catalog);

    Random rng(715);
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
    const std::string html = buildGamePanelHtml({state, catalog, launch, launch});
    require(html.find("data-modal=\"pilot_intake\"") != std::string::npos, "hangar should expose the pilot intake modal when no crew is active");
    require(html.find("pilot-card-grid") != std::string::npos, "pilot intake should render candidate cards");
    require(html.find("pilot-portrait-placeholder") != std::string::npos, "pilot intake should reserve portrait art slots");
    require(html.find("recruit_candidate:0") != std::string::npos, "pilot intake should expose indexed candidate action zero");
    require(html.find("recruit_candidate:1") != std::string::npos, "pilot intake should expose indexed candidate action one");
    require(html.find("recruit_candidate:2") != std::string::npos, "pilot intake should expose indexed candidate action two");
    require(html.find("Choose pilot") != std::string::npos, "dead roster hangar card should open the pilot chooser instead of instant-hiring");
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
    require(frontierReadinessRequired(state, catalog) == 3, "Moon frontier should require three data flights before Mars");
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

void arkDiscoveryAndScriptedJumpProgression()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 62001);

    LaunchOutcome outerPlanetsArrival;
    outerPlanetsArrival.type = LaunchResultType::MissionComplete;
    outerPlanetsArrival.frontierTransfer = true;
    outerPlanetsArrival.destinationId = content::destination::outerPlanets;
    outerPlanetsArrival.ejectMultiplier = 3.55;
    outerPlanetsArrival.crashMultiplier = 4.20;
    applyLaunchOutcome(state, catalog, outerPlanetsArrival);

    require(arkDiscovered(state), "reaching Outer Planets should discover the operable derelict Ark");
    require(state.meta.campaignMilestone == CampaignMilestone::ArkDiscovered, "Ark discovery should advance the campaign milestone");
    require(state.meta.chapter == GameChapter::Breakthrough, "Ark discovery should enter Breakthrough chapter");
    require(state.meta.ark.condition == ArkCondition::DerelictOperable, "discovered Ark should be derelict but operable");
    require(!navigationAvailable(state), "navigation should not become the main loop before the gravity-well disaster");

    require(performArkJump(state, catalog), "first Ark jump should resolve");
    require(state.meta.ark.firstJumpComplete, "first Ark jump should be recorded");
    require(state.meta.campaignMilestone == CampaignMilestone::FirstArkJumpComplete, "first Ark jump should have its own milestone");
    require(state.meta.chapter == GameChapter::Straylight, "first Ark jump should enter Straylight");
    require(state.meta.ark.condition == ArkCondition::DerelictOperable, "first Ark jump should not strand the Ark");

    require(performArkJump(state, catalog), "second Ark jump should resolve into the scripted disaster");
    require(hostileSystemActive(state), "second Ark jump should activate the hostile system loop");
    require(state.meta.chapter == GameChapter::Arkfall, "gravity-well disaster should enter Arkfall");
    require(navigationAvailable(state), "navigation should become available after the disaster");
    require(state.meta.ark.gravityWellDisaster, "gravity-well disaster should be recorded");
    require(state.meta.ark.condition == ArkCondition::DamagedStranded, "Ark should be damaged and stranded after the scripted disaster");
    require(hasUnlock(state.meta, content::unlock::deepSpace), "hostile system should unlock deep-space destinations");
    require(hasUnlock(state.meta, content::unlock::perimeterDrones), "hostile system should unlock combat-drone tech timing");
    require(state.screen == Screen::Navigation, "gravity-well disaster should land the player on Navigation");
}

void numberedChaptersAdvanceMonotonically()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 62005);
    require(state.meta.chapter == GameChapter::ProvingGround, "new game should start at Chapter 1");
    require(chapterLabel(state.meta.chapter) == "Chapter 1: Proving Ground", "chapter label should include stable number and provisional title");
    require(chapterGate(GameChapter::Straylight) == std::string_view("Leave the peaceful relay system with the Ark jump."), "Straylight gate should describe the calm-before-storm beat");

    auto completeTransfer = [&](std::string_view destinationId, double multiplier) {
        LaunchOutcome outcome;
        outcome.type = LaunchResultType::MissionComplete;
        outcome.recoveryMethod = RecoveryMethod::TransferArrival;
        outcome.frontierTransfer = true;
        outcome.destinationId = std::string(destinationId);
        outcome.ejectMultiplier = multiplier;
        outcome.crashMultiplier = multiplier + 0.65;
        applyLaunchOutcome(state, catalog, outcome);
    };

    completeTransfer(content::destination::moon, 1.95);
    require(state.meta.chapter == GameChapter::LunarProgram, "Moon advancement should enter Chapter 2");

    completeTransfer(content::destination::mars, 2.65);
    require(state.meta.chapter == GameChapter::RedFrontier, "Mars advancement should enter Chapter 3");

    completeTransfer(content::destination::outerPlanets, 3.55);
    require(state.meta.chapter == GameChapter::Breakthrough, "Outer Planets Ark discovery should enter Chapter 4");
    require(arkDiscovered(state), "Chapter 4 should coincide with Ark discovery");

    require(performArkJump(state, catalog), "first Ark jump should enter Straylight");
    require(state.meta.chapter == GameChapter::Straylight, "first Ark jump should enter Chapter 5");
    require(state.meta.navigation.currentSystemId == "relay_system", "Straylight should use the peaceful relay system");
    require(!hostileSystemActive(state), "Straylight should remain non-hostile");

    require(performArkJump(state, catalog), "second Ark jump should trigger Arkfall");
    require(state.meta.chapter == GameChapter::Arkfall, "gravity-well disaster should enter Chapter 6");
    require(hostileSystemActive(state), "Arkfall should activate the hostile-system loop");

    GameState legacyDisaster = createNewGame(catalog, 62007);
    legacyDisaster.meta.campaignMilestone = CampaignMilestone::GravityWellDisaster;
    syncChapterProgress(legacyDisaster, catalog);
    require(legacyDisaster.meta.chapter == GameChapter::Arkfall, "legacy gravity-well milestone should derive Arkfall");
    require(navigationAvailable(legacyDisaster), "legacy gravity-well milestone should make Navigation available");

    completeTransfer(content::destination::nearbyStar, 5.10);
    require(state.meta.chapter == GameChapter::LastCampfire, "first hostile-system sortie success should enter Chapter 7");

    completeTransfer(content::destination::nearbyGalaxy, 7.00);
    require(state.meta.chapter == GameChapter::VoidCompass, "Rift Belt success should enter Chapter 8");

    state.meta.campaignMilestone = CampaignMilestone::ArkRepairing;
    syncChapterProgress(state, catalog);
    require(state.meta.chapter == GameChapter::Ouroboros, "Ark repair milestone should enter Chapter 9");

    state.meta.chapter = GameChapter::Ascent;
    state.run.destinationIndex = 0;
    state.meta.campaignMilestone = CampaignMilestone::SolarTutorial;
    state.meta.ark = {};
    state.meta.navigation = {};
    syncChapterProgress(state, catalog);
    require(state.meta.chapter == GameChapter::Ascent, "chapter sync should never roll a later chapter backward");
}

void hostileNavigationSelectsShuttleSortie()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 62002);
    discoverArk(state, catalog);
    performArkJump(state, catalog);
    performArkJump(state, catalog);

    const std::vector<const Destination*> destinations = navigationDestinations(state, catalog);
    require(destinations.size() >= 2, "hostile navigation should expose multiple mapped destinations");
    require(destinations.front()->id == content::destination::nearbyStar, "Nearby Star should be the first hostile-system sortie target");

    const int fuelBefore = state.meta.ark.fuelReserve;
    const int expectedFuelCost = 2 + destinations.front()->tier;
    require(selectNavigationDestination(state, catalog, 0), "selecting a navigation destination should succeed");
    require(state.screen == Screen::Hangar, "selecting a destination should open shuttle prep in the Hangar");
    require(state.launchConfig.destinationId == content::destination::nearbyStar, "navigation should sync launch destination");
    require(state.launchConfig.frontierTransfer, "hostile navigation sorties should use transfer burn tuning");
    require(state.meta.ark.fuelReserve == fuelBefore - expectedFuelCost, "navigation sorties should spend shared fuel");

    state.screen = Screen::Navigation;
    state.meta.ark.fuelReserve = 0;
    require(!selectNavigationDestination(state, catalog, 0), "navigation should reject destinations the shared fuel reserve cannot afford");
    require(state.statusLine.find("Ark fuel reserve is short") != std::string::npos, "navigation fuel block should explain the shared reserve");

    startSurfaceExpedition(state, catalog);
    require(state.run.surfaceExpedition.enemyEncountersEnabled, "hostile-system surface expeditions should enable enemy contact");
}

void legacyDeepSpaceFrontierMigratesToArkFlow()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 62004);
    auto destinationIndex = [&](std::string_view id) {
        for (int index = 0; index < static_cast<int>(catalog.destinations.size()); ++index) {
            if (catalog.destinations[static_cast<std::size_t>(index)].id == id) {
                return index;
            }
        }
        return -1;
    };
    const int outerIndex = destinationIndex(content::destination::outerPlanets);
    const int legacyStarIndex = destinationIndex(content::destination::nearbyStar);
    require(outerIndex >= 0, "Outer Planets should resolve");
    require(legacyStarIndex > outerIndex, "legacy deep-space destination should still exist for save compatibility");

    state.run.destinationIndex = legacyStarIndex;
    state.meta.furthestTier = catalog.destinations[static_cast<std::size_t>(legacyStarIndex)].tier;
    state.launchConfig.frontierTransfer = true;
    state.launchConfig.destinationId = content::destination::nearbyStar;
    state.screen = Screen::Launch;

    require(migrateLegacyDeepSpaceFrontier(state, catalog), "legacy direct star launch should migrate");
    require(arkDiscovered(state), "migration should discover the Ark instead of preserving the retired ladder");
    require(!hostileSystemActive(state), "migration should not skip the scripted Ark jumps");
    require(state.screen == Screen::Hangar, "migration should return stale launch saves to the Hangar");
    require(state.run.destinationIndex == outerIndex, "migration should put the frontier back at Outer Planets");
    require(nextDestination(state, catalog) == nullptr, "Solar System ladder should stop at Outer Planets once Ark flow begins");
}

void arkCampaignStateRoundTripsThroughSave()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 62003);
    discoverArk(state, catalog);
    performArkJump(state, catalog);
    performArkJump(state, catalog);
    selectNavigationDestination(state, catalog, 1);

    const SaveData save = captureSaveData(state);
    const std::string serialized = serializeSaveData(save);
    const std::optional<SaveData> parsed = deserializeSaveData(serialized);
    require(parsed.has_value(), "Ark campaign save should deserialize");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *parsed);
    require(restored.meta.campaignMilestone == CampaignMilestone::HostileSystemStranded, "campaign milestone should round trip");
    require(restored.meta.chapter == GameChapter::Arkfall, "chapter should round trip and stay at Arkfall before hostile sortie success");
    require(restored.meta.ark.condition == ArkCondition::DamagedStranded, "Ark condition should round trip");
    require(restored.meta.ark.gravityWellDisaster, "gravity-well flag should round trip");
    require(restored.meta.navigation.currentSystemId == "hostile_system", "navigation system id should round trip");
    require(restored.meta.navigation.selectedDestinationId == content::destination::nearbyGalaxy, "selected navigation target should round trip");

    SaveData legacy = captureSaveData(createNewGame(catalog, 62006));
    legacy.destinationIndex = 3;
    legacy.furthestTier = 3;
    legacy.campaignMilestone = CampaignMilestone::ArkDiscovered;
    legacy.chapter = GameChapter::ProvingGround;
    legacy.ark.condition = ArkCondition::DerelictOperable;
    GameState migrated = createNewGame(catalog, 1);
    restoreSaveData(migrated, catalog, legacy);
    require(migrated.meta.chapter == GameChapter::Breakthrough, "old saves should derive the highest eligible chapter when no saved chapter existed");
}

void uiActionsUseStableSchemaIds()
{
    require(ui::actions::prepareLaunch == "prepare_launch", "prepare launch action should use a stable schema id");
    require(ui::actions::startLaunch == "start_launch", "start launch action should use a stable schema id");
    require(ui::actions::returnHome == "return_home", "return action should use a stable schema id");
    require(ui::actions::arrivalOps == "arrival_ops", "arrival ops action should use a stable schema id");
    require(ui::actions::skipArrivalFanfare == "skip_arrival_fanfare", "arrival fanfare skip action should use a stable schema id");
    require(ui::actions::openNavigation == "open_navigation", "navigation action should use a stable schema id");
    require(ui::actions::arkJump == "ark_jump", "Ark jump action should use a stable schema id");
    require(ui::actions::selectNavigationDestination(2) == "select_navigation:2", "indexed navigation actions should share one action family");
    require(ui::actions::researchProject(2) == "research_project:2", "indexed research actions should share one action family");
    require(ui::actions::surfaceUpgrade(2) == "surface_upgrade:2", "indexed surface upgrade actions should share one action family");
    require(ui::actions::droneOps == "drone_ops", "Drone Ops action should use a stable schema id");
    require(ui::actions::equipDrone(2) == "equip_drone:2", "indexed drone equipment actions should share one action family");
    require(ui::actions::upgradeDrone(2) == "upgrade_drone:2", "indexed drone tuning actions should share one action family");
    require(ui::actions::upgradeDroneSlot == "upgrade_drone_slot", "drone slot upgrade action should use a stable schema id");
    require(ui::actions::recruitCandidate(2) == "recruit_candidate:2", "indexed recruit actions should share one action family");
    require(ui::actions::extractSurface == "extract_surface", "surface extraction action should use a stable schema id");
    require(ui::actions::surfaceScanPulse == "surface_scan_pulse", "surface scan pulse action should use a stable schema id");
    require(ui::actions::surfaceScanBank == "surface_scan_bank", "surface scan bank action should use a stable schema id");
    require(ui::actions::surfacePushStep == "surface_push_step", "surface push step action should use a stable schema id");
    require(ui::actions::surfacePushBank == "surface_push_bank", "surface push bank action should use a stable schema id");
    require(ui::actions::miningTether == "mining_tether", "mining tether action should use a stable schema id");
    require(ui::actions::resetSave == "reset_save", "settings actions should use stable schema ids");
    require(ui::modals::launchBlocked == "launch_blocked", "modal ids should stay shared and data-like");

    const std::string buyOffer = ui::actions::buyOffer(2);
    require(buyOffer == "buy_offer:2", "indexed offer actions should encode the offer index in one reusable action family");
    require(buyOffer.find("rr.") == std::string::npos, "panel action ids should not embed JavaScript snippets");
}

void panelLayoutModeIsPortablePresentationData()
{
    require(panelLayoutMode(Screen::Launch) == PanelLayoutMode::ControlPanel, "launch should remain a compact action control panel");
    require(panelLayoutMode(Screen::ArrivalFanfare) == PanelLayoutMode::ControlPanel, "arrival fanfare should keep the scene open for the celebration overlay");
    require(panelLayoutMode(Screen::Orbit) == PanelLayoutMode::ControlPanel, "orbit should keep the scene open for the orbital minigame");
    require(panelLayoutMode(Screen::Hangar) == PanelLayoutMode::PhaseBoard, "hangar should use the management board layout");
    require(panelLayoutMode(Screen::Results) == PanelLayoutMode::PhaseBoard, "results should use the management board layout");
    require(panelLayoutMode(Screen::Research) == PanelLayoutMode::PhaseBoard, "research should use the management board layout");
    require(panelLayoutMode(Screen::SurfaceExpedition) == PanelLayoutMode::PhaseBoard, "surface expedition should use the management board layout");
    require(panelLayoutMode(Screen::SurfaceUpgrade) == PanelLayoutMode::PhaseBoard, "field upgrade draft should use the management board layout");
    require(panelLayoutMode(Screen::SurfaceScan) == PanelLayoutMode::PhaseBoard, "surface scan should use the phase board layout");
    require(panelLayoutMode(Screen::SurfacePush) == PanelLayoutMode::PhaseBoard, "deep push should use the phase board layout");
    require(panelLayoutMode(Screen::DroneOps) == PanelLayoutMode::PhaseBoard, "drone ops should use the management board layout");
    require(panelLayoutMode(Screen::Navigation) == PanelLayoutMode::PhaseBoard, "navigation should use the management board layout");
    require(panelLayoutMode(Screen::Upgrade) == PanelLayoutMode::PhaseBoard, "refit should use the management board layout");
    require(usesPhaseBoard(Screen::Research), "phase-board checks should go through a shared helper");
}

void contentIdsResolveAgainstDefaultCatalog()
{
    const ContentCatalog catalog = createDefaultContent();

    require(catalog.findModule(content::module::sparrowEngine) != nullptr, "starter module id should resolve");
    require(catalog.findModule(content::module::radiatorVanes) != nullptr, "cooling module id should resolve");
    require(catalog.findCrewUpgrade(content::crewUpgrade::analogSimBay) != nullptr, "crew upgrade id should resolve");
    require(catalog.findFrame(content::frame::pathfinder) != nullptr, "ship frame id should resolve");
    const Astronaut* startingCrew = catalog.findAstronaut(content::astronaut::ava);
    require(startingCrew != nullptr, "astronaut id should resolve");
    require(startingCrew->background.find("Capybara") != std::string::npos, "starter roster should use animal class themes");
    require(catalog.findDestination(content::destination::moon) != nullptr, "destination id should resolve");
    require(catalog.findResearchProject(content::research::blueprintSurvey) != nullptr, "research project id should resolve");
    require(catalog.findResearchProject(content::research::fieldProbeNetwork) != nullptr, "field probe research id should resolve");
    require(catalog.findResearchProject(content::research::regolithDrillRig) != nullptr, "drill research id should resolve");
    require(catalog.findResearchProject(content::research::cargoReturnRig) != nullptr, "cargo research id should resolve");
    require(catalog.findResearchProject(content::research::droneBayProgram) != nullptr, "drone bay research id should resolve");
    require(catalog.findResearchProject(content::research::perimeterDroneNetwork) != nullptr, "perimeter drone research id should resolve");
    const ResearchProject* arkProject = catalog.findResearchProject(content::research::arkScaffoldProgram);
    require(arkProject != nullptr, "ark scaffold research id should resolve");
    require(arkProject->requiredDestinationTier == 3, "ark scaffold should start at the outer-planets phase");
    require(arkProject->rewardUnlockKey == content::unlock::arkScaffold, "ark scaffold should unlock the future home-base hook");
    require(catalog.findSurfaceUpgrade(content::surfaceUpgrade::thermalDrillJackets) != nullptr, "surface upgrade ids should resolve");
    require(catalog.findSurfaceUpgrade(content::surfaceUpgrade::widebandPulse) != nullptr, "scanner surface upgrade id should resolve");
    require(catalog.findMiniDrone(content::drone::miningDrone) != nullptr, "mining drone id should resolve");
    require(catalog.findMiniDrone(content::drone::resourceDrone) != nullptr, "resource drone id should resolve");
    require(catalog.findMiniDrone(content::drone::surveyDrone) != nullptr, "survey drone id should resolve");
    require(catalog.findMiniDrone(content::drone::stabilizerDrone) != nullptr, "stabilizer drone id should resolve");

    MetaProgress meta;
    require(hasUnlock(meta, content::unlock::starter), "starter unlock should stay implicit");
    meta.unlockKeys.push_back(content::unlock::thermal);
    require(hasUnlock(meta, content::unlock::thermal), "named unlock key should resolve through shared ids");
    meta.unlockKeys.push_back(content::unlock::surfaceProbes);
    require(hasUnlock(meta, content::unlock::surfaceProbes), "surface unlock key should resolve through shared ids");
    meta.unlockKeys.push_back(content::unlock::analysisLab);
    require(hasUnlock(meta, content::unlock::analysisLab), "research facility unlock key should resolve through shared ids");
    meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    require(hasUnlock(meta, content::unlock::perimeterDrones), "passive defense unlock key should resolve through shared ids");
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
    shallowRecoveryCheeseEscalatesAndThenFails();
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
    emergencyRecruitmentOffersAnimalCandidateChoice();
    moduleOffersAreOneChoiceRefits();
    refitRerollsSpendAndEscalate();
    specialShipComponentsRequireRecoveredMaterials();
    preMiningRefitOffersAvoidMaterialCosts();
    inventoryPresentationSummarizesResourcesAndPayload();
    shipModuleProgressSurvivesDestroyedVehicles();
    deadCrewLosesTraining();
    crewUpgradeOffersInstallAndModifyCrewOps();
    hangarOpsStartCheapAndEscalate();
    medicalRestEscalationResetsAfterSurvivedMission();
    hangarOperationPreviewMatchesCoreMath();
    hangarOperationCardsComeFromSharedPreview();
    totaledShipCanAlwaysReachSalvageRepair();
    lowCreditRefitWindowIncludesAffordableOffer();
    destroyedTransferAttemptCostsOneProvingFlight();
    pressureTracksFrontierExperience();
    crewStressTracksPeakTelemetryDanger();
    pressureControlModulesReducePressureTelemetry();
    starterEarthOrbitIsProvingFirst();
    starterProvingEconomyFundsEarlyRefits();
    returnHomeRewardShelvesMatchRefitCosts();
    researchPhasesUnlockOnlyAfterMarsArrival();
    arrivalOperationsGateMoonButAllowMarsRisk();
    arrivalFlybyMinigameRewardsProgressionAndSlingshot();
    shipUpgradesAssistFlybyAndOrbitMinigames();
    activeFlybySaveResumesAtApproach();
    arrivalOrbitMinigameRewardsProgressionOnlyResearch();
    activeOrbitSaveResumesAtApproach();
    researchProjectsGenerateAndCompleteFromSharedRules();
    materialResearchUnlocksModuleFamilies();
    artifactInsightImprovesFutureResearch();
    researchFacilitiesImproveFutureResearch();
    artifactResearchIdentifiesRecoveredArtifacts();
    researchOutcomeSummaryShowsRewardsAndCosts();
    surfaceToolResearchImprovesExpeditions();
    animalCrewClassesModifySurfaceExpeditions();
    surfaceUpgradeOffersAreDistinctAndSelectable();
    surfaceUpgradeOffersCanRerollAndKeepDraftState();
    selectedSurfaceUpgradesModifyMiningAndSurfaceStats();
    surfaceUpgradesAndDronesModifyScanMiniGame();
    surfaceUpgradesAndDronesModifyDeepPushMiniGame();
    surfaceScanAndPushDepthLimitsStayInParity();
    surfaceUpgradesPersistUntilNewShipAndRoundTripSave();
    surfaceUpgradesResetOnEmergencyRecallOnly();
    miningDepletionAtShipGracefullyEndsRun();
    droneBayUnlocksSlotsLoadoutsAndMiningEffects();
    droneOpsPresentationExposesPersistentLoadout();
    surfaceSiteProfilesChangeExpeditionRules();
    surfaceHazardsCreateEnvironmentalSetbacks();
    surfaceEventsCreateSmallRunVariation();
    enemyContactStartsBeyondSolarSystemAndCanBeMitigated();
    surfaceExpeditionBanksMaterialsAndDefersEnemies();
    surfaceExpeditionRoundTripsThroughSave();
    surfaceMiningUsesSharedFuelAndRunsOnce();
    physicalMiningArtifactsAreSingleAndDeliveryGated();
    miningArtifactTetherAndDestructionRules();
    miningArtifactRewardsResolveOnExtraction();
    miningArtifactSaveRoundTrips();
    surfaceScanMiniGameBanksSurveyPayload();
    surfacePushMiniGameBanksDepthRoute();
    surfaceScanForecastsPushDepthLayers();
    surfaceMissionLogIsBounded();
    miningTerrainIsDeterministicAndDepthScales();
    hostileMiningTerrainGeneratesPreDugEnemyStructures();
    hostileMiningRunSpawnsEnemiesAndPassiveDefenses();
    rangedMiningEnemiesShootAndCombatVisualsExpire();
    attackDroneCombatCanCritAndEnemyCooldownPersists();
    elementalMiningCombatAppliesAffinityAndAreaDefenses();
    mammalBossChambersGrantAdvancedRewards();
    enemyMovementTypesHaveDistinctBehavior();
    miningPresentationShowsActiveThreatMix();
    miningDrillBreaksCellsAndMarksChunks();
    miningUsesSharedFuelReserve();
    miningDrillFootprintCapsWearToWorstContact();
    miningMovementGrindsSoftTerrainAndRecoilsFromHardTerrain();
    miningDrillTargetsFirstSolidCellOnRay();
    miningCompletionFeedsSurfacePayload();
    miningBrokenDrillBitDisablesDrillingOnly();
    miningShipBankingLeaveAndEmergencyRecallRules();
    miningOxygenDrainsDroneHealthBeforeForcedRecall();
    miningLoadBurdenAndUpgradeRelief();
    miningRefitModulesImproveDrillProfileIncrementally();
    activeMiningRoundTripsThroughSave();
    surfaceActionSummaryShowsResourceDeltas();
    roughSurfaceExtractionReportsLostPayload();
    researchPresentationComesFromSharedHelper();
    surfacePresentationComesFromSharedHelper();
    overburnRewardsBeatLinearScalingAfterGoal();
    saveRoundTripPreservesProgress();
    saveSchemaConstantsMatchSerializedFields();
    legacyRecordsTrackAchievementStats();
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
    panelHtmlIncludesContextualTutorialLayer();
    surfaceHtmlPromotesMiningAction();
    postArrivalPhaseHtmlUsesPolishedBoardStructure();
    hangarHtmlShowsPilotIntakeModal();
    launchBalanceHelpersDrivePreparedLaunch();
    destinationRiskEscalates();
    starterMoonTransferIsNotReliable();
    frontierReadinessGatesProgression();
    transferAttemptAdvancesOnlyOnSuccess();
    overpreparedReadinessRaisesProvingStakes();
    arkDiscoveryAndScriptedJumpProgression();
    numberedChaptersAdvanceMonotonically();
    hostileNavigationSelectsShuttleSortie();
    legacyDeepSpaceFrontierMigratesToArkFlow();
    arkCampaignStateRoundTripsThroughSave();
    uiActionsUseStableSchemaIds();
    panelLayoutModeIsPortablePresentationData();
    contentIdsResolveAgainstDefaultCatalog();
    displayFormatAndMathHelpersAreShared();

    std::cout << "rocket_core_tests passed\n";
    return 0;
}
