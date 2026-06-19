#include "core/Content.h"
#include "core/GameState.h"
#include "core/LaunchSimulation.h"
#include "core/SaveData.h"

#include <cassert>
#include <cstdlib>
#include <cmath>
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
    state.launchConfig.targetEjectMultiplier = targetMultiplier;
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

    GameState safeState = configuredState(catalog, 0, 1.10);
    Random safeRng(7);
    const PreparedLaunch safeLaunch = prepareLaunch(safeState, catalog, safeRng);
    const LaunchOutcome safe = resolveLaunch(safeLaunch, catalog, safeState, 1.10, RecoveryMethod::ManualEject, safeRng);
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

void saveRoundTripPreservesProgress()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 55);
    state.run.credits = 222.0;
    state.run.destinationIndex = 2;
    state.run.frontierReadiness = 3;
    state.run.shipDamage = 17;
    state.meta.unlockKeys.push_back("thermal");
    state.meta.blueprintProgress = 5;
    state.meta.shipsLost = 1;
    state.meta.memorials.push_back("Test Pilot lost during Mars");
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
    require(hasUnlock(restored.meta, "thermal"), "unlock keys should round trip");
    require(restored.meta.memorials.size() == 1, "memorials should round trip");
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
            Random rng(1000 + static_cast<std::uint64_t>(i));
            const LaunchOutcome outcome = simulateLaunchToTarget(state, catalog, rng);
            earthDestroyed += outcome.type == LaunchResultType::Destroyed ? 1 : 0;
        }
        {
            GameState state = configuredState(catalog, 5, catalog.destinations[5].targetMultiplier);
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
    constexpr int samples = 1000;

    for (int i = 0; i < samples; ++i) {
        GameState state = createNewGame(catalog, 333);
        state.launchConfig.frontierTransfer = true;
        state.launchConfig.destinationId = catalog.destinations[1].id;
        state.launchConfig.targetEjectMultiplier = catalog.destinations[1].targetMultiplier;
        Random rng(5000 + static_cast<std::uint64_t>(i));
        const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
        const LaunchOutcome outcome = resolveLaunch(launch, catalog, state, catalog.destinations[1].targetMultiplier, RecoveryMethod::TransferArrival, rng);
        destroyed += outcome.type == LaunchResultType::Destroyed ? 1 : 0;
    }

    require(destroyed > samples * 60 / 100, "starter ship should not reliably reach the Moon transfer burn");
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
    require(state.launchConfig.targetEjectMultiplier < catalog.destinations[1].targetMultiplier, "new frontier should default back to a proving return target");
}

} // namespace

int main()
{
    deterministicLaunchesMatch();
    safeAndDestroyedOutcomesResolve();
    moduleAggregationIncludesFrameAndDamage();
    earlyTelemetryShowsSystemLoad();
    saveRoundTripPreservesProgress();
    destinationRiskEscalates();
    starterMoonTransferIsNotReliable();
    frontierReadinessGatesProgression();
    transferAttemptAdvancesOnlyOnSuccess();

    std::cout << "rocket_core_tests passed\n";
    return 0;
}
