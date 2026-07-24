#include "performance/BenchmarkScenarioDriver.h"

#include "game/RocketGameApp.h"

namespace rocket::performance {

namespace {

Screen expectedScreen(NativeBenchmarkScenario scenario)
{
    switch (scenario) {
    case NativeBenchmarkScenario::Title:
    case NativeBenchmarkScenario::Hangar:
        return Screen::Hangar;
    case NativeBenchmarkScenario::Launch:
        return Screen::Launch;
    case NativeBenchmarkScenario::Flyby:
        return Screen::Flyby;
    case NativeBenchmarkScenario::Orbit:
        return Screen::Orbit;
    case NativeBenchmarkScenario::SurfaceOps:
        return Screen::SurfaceExpedition;
    case NativeBenchmarkScenario::SurfaceScan:
        return Screen::SurfaceScan;
    case NativeBenchmarkScenario::Mining:
        return Screen::Mining;
    }
    return Screen::Hangar;
}

} // namespace

BenchmarkScenarioSetupResult BenchmarkScenarioDriver::setup(
    RocketGameApp& app,
    const NativeBenchmarkOptions& options) const
{
    BenchmarkScenarioSetupResult result;
    result.scenario = options.scenario;
    result.screen = static_cast<Screen>(app.currentScreen());
    if (!options.enabled) {
        result.error = "Benchmark scenario setup requires enabled native benchmark options.";
        return result;
    }

    switch (options.scenario) {
    case NativeBenchmarkScenario::Title:
        app.debugShowTitle();
        break;
    case NativeBenchmarkScenario::Hangar:
        app.debugShowHangar();
        break;
    case NativeBenchmarkScenario::Launch:
        // Enter the debug sandbox first so any eventual launch completion is
        // save-suppressed for the complete benchmark lifetime.
        app.debugShowHangar();
        app.prepareForLaunch();
        app.startLaunch();
        // A reference/debug loadout may require the normal preflight transfer.
        // Advance only that bounded transition; the first active launch frame
        // remains at elapsed zero for a repeatable warm-up boundary.
        for (int step = 0; step < 256 && app.inputContext() == InputContext::Preflight; ++step) {
            app.tick(1.0 / 120.0);
        }
        break;
    case NativeBenchmarkScenario::Flyby:
        app.debugStartFlyby();
        break;
    case NativeBenchmarkScenario::Orbit:
        app.debugStartOrbit();
        break;
    case NativeBenchmarkScenario::SurfaceOps:
        app.debugShowSurfaceOps();
        break;
    case NativeBenchmarkScenario::SurfaceScan:
        app.debugStartSurfaceScan();
        break;
    case NativeBenchmarkScenario::Mining:
        // Act III level 10 exercises the largest terrain/combat presentation
        // budget with a deterministic caller-provided seed and reference
        // loadout. Gate selection remains deterministic for that seed.
        app.debugStartMiningArena(3, 10, options.seed, 0, -1);
        break;
    default:
        result.error = "Unsupported native benchmark scenario.";
        return result;
    }

    result.screen = static_cast<Screen>(app.currentScreen());
    const Screen expected = expectedScreen(options.scenario);
    if (result.screen != expected) {
        result.error = "Native benchmark scenario did not enter its expected gameplay screen.";
        return result;
    }
    if (options.scenario == NativeBenchmarkScenario::Launch
        && app.inputContext() != InputContext::Launch) {
        result.error = "Native launch benchmark did not complete its deterministic preflight transition.";
        return result;
    }
    result.gameplayStateHash = sampleGameplayStateHash(app);
    return result;
}

std::optional<std::uint64_t> BenchmarkScenarioDriver::sampleGameplayStateHash(
    const RocketGameApp& app) const noexcept
{
    return app.deterministicStateHash();
}

} // namespace rocket::performance
