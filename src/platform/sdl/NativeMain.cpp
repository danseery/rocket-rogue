#include "game/GameRmlUi.h"
#include "game/GameRunner.h"
#include "performance/BenchmarkScenarioDriver.h"
#include "performance/NativeBenchmarkOptions.h"
#include "platform/sdl/NativeStorage.h"
#include "platform/sdl/NativeTextureSource.h"
#include "platform/sdl/NativeUiBridge.h"
#include "platform/sdl/SdlPlatform.h"
#include "platform/steam/SteamPlatformService.h"
#include "render/VulkanRmlRenderHost.h"
#include "render/vulkan/VulkanGraphicsBackend.h"

#include "lodepng.h"

#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return {};
    const std::streamsize byteCount = stream.tellg();
    if (byteCount <= 0 || byteCount % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) return {};
    std::vector<std::uint32_t> words(static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(words.data()), byteCount)) return {};
    return words;
}

bool writeBenchmarkReport(
    const std::filesystem::path& destination,
    std::string_view json,
    std::string& error)
{
    std::error_code fileError;
    if (!destination.parent_path().empty()) {
        std::filesystem::create_directories(destination.parent_path(), fileError);
        if (fileError) {
            error = "Unable to create benchmark report directory: " + fileError.message();
            return false;
        }
    }
    std::filesystem::path temporary = destination;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream || !stream.write(json.data(), static_cast<std::streamsize>(json.size()))) {
            error = "Unable to write temporary benchmark report: " + temporary.string();
            return false;
        }
    }
    std::filesystem::remove(destination, fileError);
    fileError.clear();
    std::filesystem::rename(temporary, destination, fileError);
    if (fileError) {
        error = "Unable to publish benchmark report: " + fileError.message();
        std::filesystem::remove(temporary, fileError);
        return false;
    }
    return true;
}

bool writeFrameCapturePng(
    const std::filesystem::path& destination,
    const rocket::FrameCapture& capture,
    std::string& error)
{
    if (capture.format != rocket::FrameCapturePixelFormat::Rgba8Unorm) {
        error = "Frame capture must be canonical RGBA8 before PNG encoding.";
        return false;
    }
    std::vector<unsigned char> png;
    const unsigned encodeError = lodepng::encode(
        png, capture.pixels, capture.width, capture.height, LCT_RGBA, 8);
    if (encodeError != 0) {
        error = "Unable to encode benchmark screenshot (LodePNG "
            + std::to_string(encodeError) + ": " + lodepng_error_text(encodeError) + ").";
        return false;
    }

    std::error_code fileError;
    if (!destination.parent_path().empty()) {
        std::filesystem::create_directories(destination.parent_path(), fileError);
        if (fileError) {
            error = "Unable to create benchmark screenshot directory: " + fileError.message();
            return false;
        }
    }
    std::filesystem::path temporary = destination;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream || !stream.write(
                reinterpret_cast<const char*>(png.data()),
                static_cast<std::streamsize>(png.size()))) {
            error = "Unable to write temporary benchmark screenshot: " + temporary.string();
            return false;
        }
    }
    std::filesystem::remove(destination, fileError);
    fileError.clear();
    std::filesystem::rename(temporary, destination, fileError);
    if (fileError) {
        error = "Unable to publish benchmark screenshot: " + fileError.message();
        std::filesystem::remove(temporary, fileError);
        return false;
    }
    return true;
}

bool captureBenchmarkFrame(
    rocket::RocketGameApp& app,
    rocket::VulkanGraphicsBackend& renderer,
    const std::filesystem::path& destination,
    std::string& error)
{
    if (!renderer.requestFrameCapture()) {
        error = std::string(renderer.frameCaptureError());
        return false;
    }

    // Capture is deliberately rendered after timed sampling has completed.
    // Retrying skipped WSI frames handles a resize/out-of-date boundary without
    // advancing simulation or contaminating the benchmark distributions.
    constexpr int maximumAttempts = 4;
    for (int attempt = 0; attempt < maximumAttempts; ++attempt) {
        app.renderScene();
        app.renderUi();
        const rocket::GraphicsFrameStatus status = renderer.endFrameAndPresent();
        if (status == rocket::GraphicsFrameStatus::Fatal) {
            error = std::string(renderer.lastError());
            return false;
        }
        if (std::optional<rocket::FrameCapture> capture = renderer.takeFrameCapture()) {
            return writeFrameCapturePng(destination, *capture, error);
        }
        if (!renderer.frameCaptureError().empty()) {
            error = std::string(renderer.frameCaptureError());
            return false;
        }
    }
    error = "Vulkan frame capture remained unavailable after four untimed presentation attempts.";
    return false;
}

} // namespace

int main(int argumentCount, char** arguments)
{
    try {
        const rocket::performance::NativeBenchmarkParseResult parsed =
            rocket::performance::parseNativeBenchmarkOptions(argumentCount, arguments);
        if (!parsed) {
            std::cerr << "OREBIT: " << parsed.error << '\n'
                      << rocket::performance::nativeBenchmarkHelpText(
                             argumentCount > 0 && arguments && arguments[0] ? arguments[0] : "RocketRogue");
            return 2;
        }
        if (parsed.options.showHelp) {
            std::cout << rocket::performance::nativeBenchmarkHelpText(
                argumentCount > 0 && arguments && arguments[0] ? arguments[0] : "RocketRogue");
            return 0;
        }
        const rocket::performance::NativeBenchmarkOptions benchmarkOptions = parsed.options;
        if (benchmarkOptions.enabled
            && benchmarkOptions.renderer == rocket::performance::NativeBenchmarkRenderer::OpenGl) {
            std::cerr << "OREBIT native packages are Vulkan-only; --benchmark-renderer=opengl "
                         "must be run with the frozen baseline executable.\n";
            return 2;
        }

        const std::filesystem::path playerPreferenceDirectory = rocket::SdlPlatform::preferenceDirectory();
        std::filesystem::path preferenceDirectory = playerPreferenceDirectory;
        if (benchmarkOptions.enabled) {
            std::string isolationError;
            if (!rocket::performance::validateNativeBenchmarkStorageIsolation(
                    benchmarkOptions.profileDirectory, playerPreferenceDirectory, isolationError)) {
                std::cerr << "OREBIT: " << isolationError << '\n';
                return 2;
            }
            preferenceDirectory = benchmarkOptions.profileDirectory;
        }

        rocket::NativeSaveStore saves(preferenceDirectory);
        rocket::NativePreferenceStore preferences(preferenceDirectory);
        if (benchmarkOptions.enabled) {
            rocket::AppPreferences isolated = preferences.load();
            isolated.frameLimitMode = benchmarkOptions.frameLimitMode;
            isolated.fullscreen = false;
            isolated.performanceStatsEnabled = false;
            if (!preferences.store(isolated)) {
                std::cerr << "OREBIT: unable to initialize isolated benchmark preferences: "
                          << preferences.lastError() << '\n';
                return 2;
            }
        }
        // Steam must initialize before the Vulkan device so the Steam overlay
        // can install its hooks. Builds without an SDK keep this as a no-op.
        rocket::SteamPlatformService steam;
        const bool steamInitialized = steam.initialize();
        rocket::SdlPlatform platform(preferences, benchmarkOptions.enabled);
        const int initialWidth = benchmarkOptions.enabled ? benchmarkOptions.resolution.width : 1280;
        const int initialHeight = benchmarkOptions.enabled ? benchmarkOptions.resolution.height : 800;
        if (!platform.initialize(initialWidth, initialHeight)) return 1;
        if (!steamInitialized && !steam.lastError().empty()) {
            platform.log(rocket::PlatformLogLevel::Warning, steam.lastError());
        }

        const std::filesystem::path runtimeRoot = rocket::SdlPlatform::executableDirectory();
        rocket::NativeTextureSource textures(runtimeRoot);
        rocket::NativeUiBridge uiBridge;
        rocket::VulkanGraphicsBackend renderer(
            platform,
            textures,
            platform.window(),
            runtimeRoot,
            preferenceDirectory / "cache");
        renderer.setSteamDeckRuntimeDetector(steam.deckDetector());
        const std::vector<std::uint32_t> rmlVertexShader = readSpirv(runtimeRoot / "assets/shaders/rml_ui.vert.spv");
        const std::vector<std::uint32_t> rmlFragmentShader = readSpirv(runtimeRoot / "assets/shaders/rml_ui.frag.spv");
        rocket::VulkanRmlRenderHost rmlRenderHost(
            renderer,
            {rmlVertexShader, rmlFragmentShader});
        rocket::GameRmlUi ui(preferences, platform, uiBridge, rmlRenderHost);
        rocket::AppServices services {
            saves,
            preferences,
            platform,
            platform,
            textures,
            renderer,
            ui,
            uiBridge
        };
        rocket::GameRunner runner(services);
        if (!runner.initialize()) {
            platform.shutdown();
            return 1;
        }

        std::optional<rocket::performance::NativeBenchmarkController> benchmark;
        if (benchmarkOptions.enabled) {
            rocket::performance::BenchmarkScenarioDriver scenarioDriver;
            const rocket::performance::BenchmarkScenarioSetupResult setup =
                scenarioDriver.setup(runner.app(), benchmarkOptions);
            if (!setup) {
                platform.log(rocket::PlatformLogLevel::Error, setup.error);
                runner.shutdown();
                platform.shutdown();
                return 2;
            }
            rocket::performance::BenchmarkRunDescriptor descriptor;
            descriptor.scenario = std::string(
                rocket::performance::nativeBenchmarkScenarioName(benchmarkOptions.scenario));
            descriptor.renderer = "vulkan";
            descriptor.platform = SDL_GetPlatform();
            descriptor.gpu = std::string(renderer.deviceName());
            descriptor.seed = benchmarkOptions.seed;
            descriptor.width = benchmarkOptions.resolution.width;
            descriptor.height = benchmarkOptions.resolution.height;
            const rocket::RendererDiagnostics startupDiagnostics = renderer.diagnostics();
            descriptor.targetFramesPerSecond = startupDiagnostics.targetFramesPerSecond;
            descriptor.simulationFramesPerSecond =
                startupDiagnostics.nominalTargetFramesPerSecond > 0.0
                ? startupDiagnostics.nominalTargetFramesPerSecond
                : (descriptor.targetFramesPerSecond > 0.0
                    ? descriptor.targetFramesPerSecond
                    : 60.0);
            descriptor.activeRefreshRateHz = platform.displayRefreshRateHz();
            descriptor.warmupSeconds = benchmarkOptions.warmupSeconds;
            descriptor.captureSeconds = benchmarkOptions.durationSeconds.value_or(0.0);
            descriptor.initialGameplayStateHash = setup.gameplayStateHash;
            benchmark.emplace(benchmarkOptions, std::move(descriptor));
            const rocket::performance::NativeBenchmarkUpdate started =
                benchmark->start(platform.monotonicSeconds());
            if (!started) {
                platform.log(rocket::PlatformLogLevel::Error, started.error);
                runner.shutdown();
                platform.shutdown();
                return 2;
            }
        }

        bool running = true;
        bool benchmarkSucceeded = true;
        int previousMissedRefreshes = 0;
        while (running) {
            running = platform.processEvents(runner.app());
            if (!running) break;
            if (platform.consumeGraphicsRebuildRequest()) renderer.requestSwapchainRebuild();

            const rocket::NativeFrameDisposition disposition = platform.frameDisposition();
            if (!rocket::nativeFrameRenders(disposition)) continue;
            if (platform.consumeFrameClockReset()) runner.resetFrameClock();
            if (rocket::nativeFrameAcceptsRealtimeInput(disposition)) {
                platform.applyKeyboardState(runner.app());
            }
            const double frameStartedSeconds = platform.monotonicSeconds();
            if (benchmark) {
                runner.frameForBenchmark(benchmark->fixedSimulationDeltaSeconds());
            } else {
                runner.frame();
            }
            const double frameFinishedSeconds = platform.monotonicSeconds();
            platform.completeFrame();

            const rocket::GraphicsFrameStatus frameStatus = renderer.frameStatus();
            if (frameStatus == rocket::GraphicsFrameStatus::Fatal) {
                platform.log(rocket::PlatformLogLevel::Error, renderer.lastError());
                benchmarkSucceeded = false;
                break;
            }
            if (benchmark) {
                if (!rocket::performance::nativeBenchmarkAcceptsPresentedFrame(frameStatus)) {
                    platform.log(rocket::PlatformLogLevel::Error,
                        "Benchmark presentation was skipped; the run is invalid. "
                        "Repeat after the display, resize, and surface state has stabilized.");
                    benchmarkSucceeded = false;
                    break;
                }
                const rocket::RendererDiagnostics diagnostics = renderer.diagnostics();
                rocket::performance::BenchmarkFrameSample sample;
                sample.limiterIdleMilliseconds = diagnostics.limiterIdleMilliseconds;
                sample.cpuMilliseconds = std::max(
                    0.0,
                    (frameFinishedSeconds - frameStartedSeconds) * 1000.0
                        - sample.limiterIdleMilliseconds);
                sample.gpuMilliseconds = diagnostics.gpuFrameMilliseconds;
                sample.presentIntervalMilliseconds = diagnostics.presentIntervalMilliseconds;
                sample.sceneDrawCalls = static_cast<std::uint32_t>(
                    std::max(0, diagnostics.sceneDrawCalls));
                sample.uploadedBytes = diagnostics.uploadedBytes;
                sample.pipelineEvents = static_cast<std::uint32_t>(
                    std::max(0, diagnostics.pipelineCreationsThisFrame));
                sample.deviceMemoryBytes = diagnostics.deviceMemoryBytes;
                sample.missedRefresh = diagnostics.missedRefreshes > previousMissedRefreshes;
                previousMissedRefreshes = diagnostics.missedRefreshes;
                const rocket::performance::NativeBenchmarkUpdate update =
                    benchmark->recordFrame(frameFinishedSeconds, sample);
                if (!update) {
                    platform.log(rocket::PlatformLogLevel::Error, update.error);
                    benchmarkSucceeded = false;
                    break;
                }
                if (update.transitionedToComplete) {
                    const std::uint64_t finalGameplayStateHash =
                        runner.app().deterministicStateHash();
                    benchmark->setFinalGameplayStateHash(finalGameplayStateHash);
                    std::string writeError;
                    if (!benchmarkOptions.screenshotPath.empty()) {
                        benchmarkSucceeded = captureBenchmarkFrame(
                            runner.app(), renderer, benchmarkOptions.screenshotPath, writeError);
                        if (benchmarkSucceeded
                            && runner.app().deterministicStateHash() != finalGameplayStateHash) {
                            benchmarkSucceeded = false;
                            writeError = "Untimed screenshot rendering changed deterministic gameplay state.";
                        }
                        if (benchmarkSucceeded) {
                            platform.log(rocket::PlatformLogLevel::Info,
                                "Benchmark screenshot written to "
                                    + benchmarkOptions.screenshotPath.string());
                        }
                    }
                    if (benchmarkSucceeded) {
                        benchmarkSucceeded = writeBenchmarkReport(
                            benchmarkOptions.jsonPath, benchmark->reportJson(), writeError);
                    }
                    if (!benchmarkSucceeded) {
                        platform.log(rocket::PlatformLogLevel::Error, writeError);
                    } else {
                        platform.log(rocket::PlatformLogLevel::Info,
                            "Benchmark report written to " + benchmarkOptions.jsonPath.string());
                    }
                    running = false;
                }
            }
        }

        if (benchmark && !benchmark->complete()) {
            platform.log(rocket::PlatformLogLevel::Error,
                "Benchmark ended before the requested capture completed; no partial report was published.");
            benchmarkSucceeded = false;
        }

        runner.shutdown();
        platform.shutdown();
        steam.shutdown();
        return benchmarkSucceeded ? 0 : 1;
    } catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal startup error: %s", error.what());
        return 1;
    }
}
