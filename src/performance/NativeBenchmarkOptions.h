#pragma once

#include "performance/PerformanceMetrics.h"
#include "platform/AppServices.h"
#include "platform/FrameLimitPolicy.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rocket::performance {

enum class NativeBenchmarkScenario {
    Title,
    Hangar,
    Launch,
    Flyby,
    Orbit,
    SurfaceOps,
    SurfaceScan,
    Mining,
};

enum class NativeBenchmarkRenderer {
    Active,
    OpenGl,
    Vulkan,
};

struct NativeBenchmarkResolution {
    int width = 1280;
    int height = 800;
};

struct NativeBenchmarkOptions {
    bool enabled = false;
    bool showHelp = false;
    NativeBenchmarkScenario scenario = NativeBenchmarkScenario::Mining;
    NativeBenchmarkRenderer renderer = NativeBenchmarkRenderer::Active;
    FrameLimitMode frameLimitMode = FrameLimitMode::Smooth60;
    std::uint64_t seed = 0x0BEB17ULL;
    double warmupSeconds = 10.0;
    std::optional<double> durationSeconds = 60.0;
    std::optional<std::uint64_t> frameCount;
    NativeBenchmarkResolution resolution;
    std::filesystem::path jsonPath;
    std::filesystem::path screenshotPath;
    std::filesystem::path profileDirectory;
};

struct NativeBenchmarkParseResult {
    NativeBenchmarkOptions options;
    std::string error;

    explicit operator bool() const { return error.empty(); }
};

NativeBenchmarkParseResult parseNativeBenchmarkOptions(
    std::span<const std::string_view> arguments);
NativeBenchmarkParseResult parseNativeBenchmarkOptions(
    int argumentCount,
    const char* const* arguments);

std::string nativeBenchmarkHelpText(std::string_view executableName = "RocketRogue");
std::string_view nativeBenchmarkScenarioName(NativeBenchmarkScenario scenario);
std::string_view nativeBenchmarkRendererName(NativeBenchmarkRenderer renderer);

// A benchmark sample represents one successfully presented frame. WSI skips
// cannot be retried after simulation has advanced without changing the
// deterministic workload, so the native loop invalidates the entire run.
constexpr bool nativeBenchmarkAcceptsPresentedFrame(GraphicsFrameStatus status) noexcept
{
    return status == GraphicsFrameStatus::Ready;
}

// Call this after resolving the platform's real player-profile directory. It
// rejects equal or nested paths in either direction, preventing a benchmark
// cleanup or write from touching the player's saves and preferences.
bool validateNativeBenchmarkStorageIsolation(
    const std::filesystem::path& benchmarkDirectory,
    const std::filesystem::path& playerProfileDirectory,
    std::string& error);

enum class NativeBenchmarkPhase {
    NotStarted,
    Warmup,
    Capture,
    Complete,
};

struct NativeBenchmarkUpdate {
    NativeBenchmarkPhase phase = NativeBenchmarkPhase::NotStarted;
    bool transitionedToCapture = false;
    bool transitionedToComplete = false;
    bool sampleAccepted = false;
    std::size_t submittedSampleCount = 0;
    double phaseElapsedSeconds = 0.0;
    std::string error;

    explicit operator bool() const { return error.empty(); }
};

// Owns warm-up and capture boundaries but no platform, renderer, or filesystem
// state. The native frame loop supplies a monotonic timestamp and one completed
// frame sample; JSON persistence remains at the platform boundary.
class NativeBenchmarkController {
public:
    NativeBenchmarkController(
        NativeBenchmarkOptions options,
        BenchmarkRunDescriptor descriptor);

    NativeBenchmarkUpdate start(double monotonicSeconds);
    NativeBenchmarkUpdate recordFrame(
        double monotonicSeconds,
        const BenchmarkFrameSample& sample);

    NativeBenchmarkPhase phase() const;
    bool complete() const;
    std::size_t submittedSampleCount() const;
    // Frozen from the startup descriptor so adaptive presentation pacing cannot
    // change deterministic simulation cadence after the benchmark begins.
    double fixedSimulationDeltaSeconds() const;
    const NativeBenchmarkOptions& options() const;
    void setFinalGameplayStateHash(std::optional<std::uint64_t> hash);
    BenchmarkReport report() const;
    std::string reportJson() const;

private:
    NativeBenchmarkUpdate update(bool accepted = false) const;

    NativeBenchmarkOptions options_;
    double fixedSimulationDeltaSeconds_ = 1.0 / 60.0;
    std::size_t fixedWarmupFrameCount_ = 0;
    BenchmarkAccumulator accumulator_;
    NativeBenchmarkPhase phase_ = NativeBenchmarkPhase::NotStarted;
    double warmupStartedSeconds_ = 0.0;
    double captureStartedSeconds_ = 0.0;
    double lastTimestampSeconds_ = 0.0;
    std::size_t submittedWarmupFrameCount_ = 0;
    std::size_t submittedSampleCount_ = 0;
};

} // namespace rocket::performance
