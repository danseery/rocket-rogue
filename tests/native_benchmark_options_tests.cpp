#include "performance/NativeBenchmarkOptions.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

rocket::performance::NativeBenchmarkParseResult parse(
    std::initializer_list<std::string_view> arguments)
{
    return rocket::performance::parseNativeBenchmarkOptions(
        std::span<const std::string_view>(arguments.begin(), arguments.size()));
}

rocket::performance::NativeBenchmarkParseResult minimalBenchmark(
    std::initializer_list<std::string_view> extra = {})
{
    std::vector<std::string_view> arguments {
        "--benchmark-scenario", "mining",
        "--benchmark-json", "results/mining.json",
        "--benchmark-profile-dir", "benchmark-profile",
    };
    arguments.insert(arguments.end(), extra.begin(), extra.end());
    return rocket::performance::parseNativeBenchmarkOptions(arguments);
}

rocket::performance::BenchmarkFrameSample sample(double cpu = 4.0)
{
    rocket::performance::BenchmarkFrameSample result;
    result.cpuMilliseconds = cpu;
    result.gpuMilliseconds = 2.0;
    result.presentIntervalMilliseconds = 16.667;
    result.limiterIdleMilliseconds = 10.0;
    result.sceneDrawCalls = 12;
    result.uploadedBytes = 4096;
    result.deviceMemoryBytes = 1024 * 1024;
    return result;
}

} // namespace

int main()
{
    using namespace rocket::performance;

    assert(nativeBenchmarkAcceptsPresentedFrame(rocket::GraphicsFrameStatus::Ready));
    assert(!nativeBenchmarkAcceptsPresentedFrame(rocket::GraphicsFrameStatus::Skipped));
    assert(!nativeBenchmarkAcceptsPresentedFrame(rocket::GraphicsFrameStatus::Fatal));

    const NativeBenchmarkParseResult normalLaunch = parse({});
    assert(normalLaunch);
    assert(!normalLaunch.options.enabled);
    assert(!normalLaunch.options.showHelp);

    const NativeBenchmarkParseResult help = parse({"--help"});
    assert(help);
    assert(help.options.showHelp);
    const std::string helpText = nativeBenchmarkHelpText("OrebitTest");
    assert(helpText.find("OrebitTest --help") != std::string::npos);
    assert(helpText.find("--benchmark-profile-dir") != std::string::npos);
    assert(helpText.find("--benchmark-screenshot") != std::string::npos);
    assert(helpText.find("--benchmark-frame-limit") != std::string::npos);
    assert(helpText.find("surface-scan") != std::string::npos);
    assert(helpText.find("mutually exclusive") != std::string::npos);

    const NativeBenchmarkParseResult full = parse({
        "--benchmark-scenario=orbit",
        "--benchmark-seed", "0xBEB17",
        "--benchmark-warmup-seconds", "12.5",
        "--benchmark-duration-seconds=45",
        "--benchmark-renderer", "vulkan",
        "--benchmark-frame-limit", "balanced",
        "--benchmark-resolution", "2560X1440",
        "--benchmark-json", "captures/orbit.json",
        "--benchmark-screenshot", "captures/orbit.PNG",
        "--benchmark-profile-dir", "captures/profile",
    });
    assert(full);
    assert(full.options.enabled);
    assert(full.options.scenario == NativeBenchmarkScenario::Orbit);
    assert(full.options.seed == 0xBEB17ULL);
    assert(std::abs(full.options.warmupSeconds - 12.5) < 0.000001);
    assert(full.options.durationSeconds == 45.0);
    assert(!full.options.frameCount.has_value());
    assert(full.options.renderer == NativeBenchmarkRenderer::Vulkan);
    assert(full.options.frameLimitMode == rocket::FrameLimitMode::Balanced);
    assert(full.options.resolution.width == 2560 && full.options.resolution.height == 1440);
    assert(full.options.jsonPath == std::filesystem::path("captures/orbit.json"));
    assert(full.options.screenshotPath == std::filesystem::path("captures/orbit.PNG"));

    const NativeBenchmarkParseResult byFrames = minimalBenchmark({
        "--benchmark-frame-count", "3600",
    });
    assert(byFrames);
    assert(!byFrames.options.durationSeconds.has_value());
    assert(byFrames.options.frameCount == 3600);

    assert(!minimalBenchmark({
        "--benchmark-duration-seconds", "30",
        "--benchmark-frame-count", "1800",
    }));
    assert(!minimalBenchmark({"--benchmark-scenario", "orbit"}));
    const NativeBenchmarkParseResult title = parse({
        "--benchmark-scenario", "title",
        "--benchmark-json", "title.json",
        "--benchmark-profile-dir", "title-profile",
    });
    assert(title && title.options.scenario == NativeBenchmarkScenario::Title);
    const NativeBenchmarkParseResult surfaceOps = parse({
        "--benchmark-scenario", "surface-ops",
        "--benchmark-json", "surface-ops.json",
        "--benchmark-profile-dir", "surface-ops-profile",
    });
    assert(surfaceOps && surfaceOps.options.scenario == NativeBenchmarkScenario::SurfaceOps);
    const NativeBenchmarkParseResult surfaceScan = parse({
        "--benchmark-scenario", "surface-scan",
        "--benchmark-json", "surface-scan.json",
        "--benchmark-profile-dir", "surface-scan-profile",
    });
    assert(surfaceScan && surfaceScan.options.scenario == NativeBenchmarkScenario::SurfaceScan);
    assert(nativeBenchmarkScenarioName(surfaceScan.options.scenario) == "surface-scan");
    assert(!minimalBenchmark({"--benchmark-resolution", "1280"}));
    assert(!minimalBenchmark({"--benchmark-resolution", "200x100"}));
    assert(!minimalBenchmark({"--benchmark-renderer", "directx"}));
    assert(!minimalBenchmark({"--benchmark-frame-limit", "uncapped"}));
    assert(!minimalBenchmark({
        "--benchmark-frame-limit", "smooth60",
        "--benchmark-frame-limit", "battery30",
    }));
    assert(!minimalBenchmark({"--benchmark-seed", "-1"}));
    assert(!minimalBenchmark({"--benchmark-warmup-seconds", "nan"}));
    assert(!minimalBenchmark({"--benchmark-duration-seconds", "0"}));
    assert(!minimalBenchmark({"--benchmark-frame-count", "0"}));
    assert(!minimalBenchmark({"--benchmark-json", "folder/"}));
    assert(!minimalBenchmark({"--benchmark-screenshot", "captures/mining.jpg"}));
    assert(!minimalBenchmark({"--benchmark-screenshot", "folder/"}));
    assert(!minimalBenchmark({"--benchmark-profile-dir", "."}));
    assert(!parse({"--benchmark-scenario", "mining"}));
    assert(!parse({"--unknown"}));
    assert(!parse({"--benchmark-scenario"}));

    std::string isolationError;
    const std::filesystem::path base = std::filesystem::current_path();
    const std::filesystem::path playerProfile = base / "player-profile";
    assert(!validateNativeBenchmarkStorageIsolation(
        playerProfile, playerProfile, isolationError));
    assert(!isolationError.empty());
    assert(!validateNativeBenchmarkStorageIsolation(
        playerProfile / "benchmark", playerProfile, isolationError));
    assert(!validateNativeBenchmarkStorageIsolation(
        base, playerProfile, isolationError));
    assert(validateNativeBenchmarkStorageIsolation(
        base / "benchmark-profile", playerProfile, isolationError));
    assert(isolationError.empty());

    NativeBenchmarkOptions durationOptions = minimalBenchmark({
        "--benchmark-warmup-seconds", "5",
        "--benchmark-duration-seconds", "2",
    }).options;
    BenchmarkRunDescriptor descriptor;
    descriptor.platform = "windows";
    descriptor.gpu = "test gpu";
    descriptor.renderer = "vulkan-runtime";
    NativeBenchmarkController durationController(durationOptions, descriptor);
    NativeBenchmarkUpdate update = durationController.start(100.0);
    assert(update && update.phase == NativeBenchmarkPhase::Warmup);
    update = durationController.recordFrame(104.9, sample());
    assert(update && update.phase == NativeBenchmarkPhase::Warmup);
    update = durationController.recordFrame(105.0, sample(99.0));
    assert(update && update.transitionedToCapture && !update.sampleAccepted);
    assert(durationController.submittedSampleCount() == 0);
    update = durationController.recordFrame(105.1, sample(4.0));
    assert(update && update.sampleAccepted && update.phase == NativeBenchmarkPhase::Capture);
    update = durationController.recordFrame(107.0, sample(5.0));
    assert(update && update.sampleAccepted && update.transitionedToComplete);
    assert(durationController.complete());
    assert(durationController.report().acceptedSampleCount == 2);
    assert(durationController.report().run.scenario == "mining");
    assert(durationController.report().run.renderer == "vulkan-runtime");
    assert(durationController.reportJson().find("\"accepted\": 2") != std::string::npos);

    NativeBenchmarkOptions frameOptions = minimalBenchmark({
        "--benchmark-warmup-seconds", "0",
        "--benchmark-frame-count", "2",
        "--benchmark-renderer", "opengl",
    }).options;
    NativeBenchmarkController frameController(frameOptions, {});
    update = frameController.start(0.0);
    assert(update && update.transitionedToCapture);
    update = frameController.recordFrame(0.016, sample());
    assert(update && update.sampleAccepted && !update.transitionedToComplete);
    update = frameController.recordFrame(0.032, sample());
    assert(update && update.sampleAccepted && update.transitionedToComplete);
    assert(frameController.report().acceptedSampleCount == 2);
    assert(frameController.report().run.renderer == "opengl");
    assert(frameController.recordFrame(0.048, sample()).phase == NativeBenchmarkPhase::Complete);

    NativeBenchmarkOptions fixedWarmupOptions = minimalBenchmark({
        "--benchmark-warmup-seconds", "0.05",
        "--benchmark-frame-count", "1",
    }).options;
    BenchmarkRunDescriptor fixedWarmupDescriptor;
    fixedWarmupDescriptor.simulationFramesPerSecond = 60.0;
    fixedWarmupDescriptor.targetFramesPerSecond = 59.951;
    NativeBenchmarkController fixedWarmupController(
        fixedWarmupOptions, fixedWarmupDescriptor);
    assert(std::abs(fixedWarmupController.fixedSimulationDeltaSeconds() - 1.0 / 60.0)
        < 0.000000001);
    // Presentation may resolve to the physical refresh, but deterministic
    // simulation and fixed-frame warm-up retain the nominal 60 Hz cadence.
    fixedWarmupDescriptor.targetFramesPerSecond = 48.0;
    assert(std::abs(fixedWarmupController.fixedSimulationDeltaSeconds() - 1.0 / 60.0)
        < 0.000000001);
    NativeBenchmarkController refreshCadenceController(
        fixedWarmupOptions, fixedWarmupDescriptor);
    assert(std::abs(refreshCadenceController.fixedSimulationDeltaSeconds() - 1.0 / 60.0)
        < 0.000000001);
    fixedWarmupDescriptor.simulationFramesPerSecond = 0.0;
    NativeBenchmarkController unavailableCadenceController(
        fixedWarmupOptions, fixedWarmupDescriptor);
    assert(std::abs(unavailableCadenceController.fixedSimulationDeltaSeconds() - 1.0 / 60.0)
        < 0.000000001);
    update = fixedWarmupController.start(0.0);
    assert(update.phase == NativeBenchmarkPhase::Warmup);
    update = fixedWarmupController.recordFrame(10.0, sample());
    assert(update.phase == NativeBenchmarkPhase::Warmup);
    update = fixedWarmupController.recordFrame(20.0, sample());
    assert(update.phase == NativeBenchmarkPhase::Warmup);
    update = fixedWarmupController.recordFrame(30.0, sample());
    assert(update.transitionedToCapture && !update.sampleAccepted);
    update = fixedWarmupController.recordFrame(30.001, sample());
    assert(update.transitionedToComplete && update.sampleAccepted);

    NativeBenchmarkController invalidClock(frameOptions, {});
    assert(!invalidClock.recordFrame(1.0, sample()));
    assert(!invalidClock.start(-1.0));
    assert(invalidClock.start(1.0));
    assert(!invalidClock.recordFrame(0.5, sample()));

    return 0;
}
