#include "performance/NativeBenchmarkOptions.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace rocket::performance {
namespace {

constexpr double maximumWarmupSeconds = 600.0;
constexpr double maximumDurationSeconds = 3600.0;
constexpr std::uint64_t maximumFrameCount = 10'000'000;
constexpr int minimumResolutionWidth = 320;
constexpr int minimumResolutionHeight = 200;
constexpr int maximumResolutionDimension = 16'384;

struct ParsedArgument {
    std::string_view name;
    std::optional<std::string_view> attachedValue;
};

ParsedArgument splitArgument(std::string_view argument)
{
    const std::size_t separator = argument.find('=');
    if (separator == std::string_view::npos) {
        return {argument, std::nullopt};
    }
    return {argument.substr(0, separator), argument.substr(separator + 1)};
}

bool parseUnsigned(std::string_view text, std::uint64_t& value)
{
    if (text.empty() || text.front() == '+' || text.front() == '-') return false;
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
        if (text.empty()) return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return error == std::errc() && end == text.data() + text.size();
}

bool parseDouble(std::string_view text, double& value)
{
    if (text.empty()) return false;
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), value, std::chars_format::general);
    return error == std::errc() && end == text.data() + text.size() && std::isfinite(value);
}

bool parsePositiveInt(std::string_view text, int& value)
{
    std::uint64_t parsed = 0;
    if (!parseUnsigned(text, parsed)
        || parsed == 0
        || parsed > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

std::optional<NativeBenchmarkScenario> parseScenario(std::string_view text)
{
    if (text == "title") return NativeBenchmarkScenario::Title;
    if (text == "hangar") return NativeBenchmarkScenario::Hangar;
    if (text == "launch") return NativeBenchmarkScenario::Launch;
    if (text == "flyby") return NativeBenchmarkScenario::Flyby;
    if (text == "orbit") return NativeBenchmarkScenario::Orbit;
    if (text == "surface-ops") return NativeBenchmarkScenario::SurfaceOps;
    if (text == "mining") return NativeBenchmarkScenario::Mining;
    return std::nullopt;
}

std::optional<NativeBenchmarkRenderer> parseRenderer(std::string_view text)
{
    if (text == "active") return NativeBenchmarkRenderer::Active;
    if (text == "opengl") return NativeBenchmarkRenderer::OpenGl;
    if (text == "vulkan") return NativeBenchmarkRenderer::Vulkan;
    return std::nullopt;
}

std::optional<FrameLimitMode> parseFrameLimitMode(std::string_view text)
{
    if (text == "platform-default") return FrameLimitMode::PlatformDefault;
    if (text == "smooth60") return FrameLimitMode::Smooth60;
    if (text == "balanced") return FrameLimitMode::Balanced;
    if (text == "battery30") return FrameLimitMode::Battery30;
    if (text == "display") return FrameLimitMode::Display;
    return std::nullopt;
}

bool parseResolution(std::string_view text, NativeBenchmarkResolution& resolution)
{
    const std::size_t separator = text.find_first_of("xX");
    if (separator == std::string_view::npos
        || separator == 0
        || separator + 1 >= text.size()
        || text.find_first_of("xX", separator + 1) != std::string_view::npos) {
        return false;
    }
    int width = 0;
    int height = 0;
    if (!parsePositiveInt(text.substr(0, separator), width)
        || !parsePositiveInt(text.substr(separator + 1), height)
        || width < minimumResolutionWidth
        || height < minimumResolutionHeight
        || width > maximumResolutionDimension
        || height > maximumResolutionDimension) {
        return false;
    }
    resolution = {width, height};
    return true;
}

bool unsafeDirectoryPath(const std::filesystem::path& path)
{
    if (path.empty()) return true;
    const std::filesystem::path normalized = path.lexically_normal();
    if (normalized.empty() || normalized == std::filesystem::path(".")) return true;
    return normalized.has_root_path() && normalized.relative_path().empty();
}

bool pngOutputPath(const std::filesystem::path& path)
{
    if (path.empty() || path.filename().empty()) return false;
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension == ".png";
}

std::filesystem::path normalizedAbsolutePath(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
    if (!error) return normalized.lexically_normal();
    error.clear();
    normalized = std::filesystem::absolute(path, error);
    return error ? path.lexically_normal() : normalized.lexically_normal();
}

std::string comparablePathPart(const std::filesystem::path& part)
{
    std::string result = part.generic_string();
#if defined(_WIN32)
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    return result;
}

bool pathContains(const std::filesystem::path& parent, const std::filesystem::path& candidate)
{
    auto parentPart = parent.begin();
    auto candidatePart = candidate.begin();
    for (; parentPart != parent.end(); ++parentPart, ++candidatePart) {
        if (candidatePart == candidate.end()
            || comparablePathPart(*parentPart) != comparablePathPart(*candidatePart)) {
            return false;
        }
    }
    return true;
}

double resolvedSimulationFramesPerSecond(const BenchmarkRunDescriptor& descriptor)
{
    if (std::isfinite(descriptor.simulationFramesPerSecond)
        && descriptor.simulationFramesPerSecond > 0.0) {
        return std::max(1.0, descriptor.simulationFramesPerSecond);
    }
    return 60.0;
}

BenchmarkRunDescriptor normalizedDescriptor(
    const NativeBenchmarkOptions& options,
    BenchmarkRunDescriptor descriptor)
{
    descriptor.scenario = std::string(nativeBenchmarkScenarioName(options.scenario));
    if (options.renderer != NativeBenchmarkRenderer::Active || descriptor.renderer.empty()) {
        descriptor.renderer = std::string(nativeBenchmarkRendererName(options.renderer));
    }
    descriptor.seed = options.seed;
    descriptor.width = options.resolution.width;
    descriptor.height = options.resolution.height;
    descriptor.simulationFramesPerSecond = resolvedSimulationFramesPerSecond(descriptor);
    descriptor.warmupSeconds = options.warmupSeconds;
    descriptor.captureSeconds = options.durationSeconds.value_or(0.0);
    return descriptor;
}

NativeBenchmarkParseResult parseError(std::string message)
{
    NativeBenchmarkParseResult result;
    result.error = std::move(message);
    return result;
}

std::size_t fixedWarmupFrameCount(
    const NativeBenchmarkOptions& options,
    const BenchmarkRunDescriptor& descriptor)
{
    if (!options.frameCount.has_value() || options.warmupSeconds <= 0.0) {
        return 0;
    }
    return static_cast<std::size_t>(
        std::ceil(options.warmupSeconds * resolvedSimulationFramesPerSecond(descriptor)));
}

double frozenSimulationDeltaSeconds(const BenchmarkRunDescriptor& descriptor)
{
    return 1.0 / resolvedSimulationFramesPerSecond(descriptor);
}

} // namespace

NativeBenchmarkParseResult parseNativeBenchmarkOptions(
    std::span<const std::string_view> arguments)
{
    NativeBenchmarkParseResult result;
    bool sawBenchmarkArgument = false;
    bool scenarioSpecified = false;
    bool durationSpecified = false;
    bool frameLimitSpecified = false;
    bool frameCountSpecified = false;
    bool jsonSpecified = false;
    bool profileSpecified = false;
    bool screenshotSpecified = false;
    bool rendererSpecified = false;
    bool resolutionSpecified = false;
    bool seedSpecified = false;
    bool warmupSpecified = false;

    const auto takeValue = [&](const ParsedArgument& parsed, std::size_t& index, std::string_view& value) -> bool {
        if (parsed.attachedValue.has_value()) {
            value = *parsed.attachedValue;
            return !value.empty();
        }
        if (index + 1 >= arguments.size() || arguments[index + 1].starts_with("--")) {
            return false;
        }
        value = arguments[++index];
        return !value.empty();
    };
    const auto duplicateError = [](std::string_view name) {
        return parseError("Option '" + std::string(name) + "' may only be specified once.");
    };

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const ParsedArgument parsed = splitArgument(arguments[index]);
        if (parsed.name == "--help" || parsed.name == "--benchmark-help") {
            if (parsed.attachedValue.has_value()) {
                return parseError("Option '" + std::string(parsed.name) + "' does not accept a value.");
            }
            result.options.showHelp = true;
            continue;
        }

        std::string_view value;
        if (parsed.name == "--benchmark-scenario") {
            if (scenarioSpecified) return duplicateError(parsed.name);
            if (!takeValue(parsed, index, value)) return parseError("Option '--benchmark-scenario' requires a value.");
            const auto scenario = parseScenario(value);
            if (!scenario) {
                return parseError("Invalid benchmark scenario '" + std::string(value)
                    + "'. Expected title, hangar, launch, flyby, orbit, surface-ops, or mining.");
            }
            result.options.scenario = *scenario;
            scenarioSpecified = true;
        } else if (parsed.name == "--benchmark-seed") {
            if (seedSpecified) return duplicateError(parsed.name);
            if (!takeValue(parsed, index, value)) return parseError("Option '--benchmark-seed' requires a value.");
            if (!parseUnsigned(value, result.options.seed)) {
                return parseError("Invalid benchmark seed '" + std::string(value)
                    + "'. Use an unsigned decimal or 0x-prefixed hexadecimal integer.");
            }
            seedSpecified = true;
        } else if (parsed.name == "--benchmark-warmup-seconds") {
            if (warmupSpecified) return duplicateError(parsed.name);
            if (!takeValue(parsed, index, value)) return parseError("Option '--benchmark-warmup-seconds' requires a value.");
            if (!parseDouble(value, result.options.warmupSeconds)
                || result.options.warmupSeconds < 0.0
                || result.options.warmupSeconds > maximumWarmupSeconds) {
                return parseError("Invalid benchmark warm-up '" + std::string(value)
                    + "'. Expected a value from 0 through 600 seconds.");
            }
            warmupSpecified = true;
        } else if (parsed.name == "--benchmark-duration-seconds") {
            if (durationSpecified) return duplicateError(parsed.name);
            if (!takeValue(parsed, index, value)) return parseError("Option '--benchmark-duration-seconds' requires a value.");
            double duration = 0.0;
            if (!parseDouble(value, duration) || duration <= 0.0 || duration > maximumDurationSeconds) {
                return parseError("Invalid benchmark duration '" + std::string(value)
                    + "'. Expected a value greater than 0 and no more than 3600 seconds.");
            }
            result.options.durationSeconds = duration;
            durationSpecified = true;
        } else if (parsed.name == "--benchmark-frame-count") {
            if (frameCountSpecified) return duplicateError(parsed.name);
            if (!takeValue(parsed, index, value)) return parseError("Option '--benchmark-frame-count' requires a value.");
            std::uint64_t frameCount = 0;
            if (!parseUnsigned(value, frameCount) || frameCount == 0 || frameCount > maximumFrameCount) {
                return parseError("Invalid benchmark frame count '" + std::string(value)
                    + "'. Expected an integer from 1 through 10000000.");
            }
            result.options.frameCount = frameCount;
            frameCountSpecified = true;
        } else if (parsed.name == "--benchmark-renderer") {
            if (rendererSpecified) return duplicateError(parsed.name);
            if (!takeValue(parsed, index, value)) return parseError("Option '--benchmark-renderer' requires a value.");
            const auto renderer = parseRenderer(value);
            if (!renderer) {
                return parseError("Invalid benchmark renderer '" + std::string(value)
                    + "'. Expected active, opengl, or vulkan.");
            }
            result.options.renderer = *renderer;
            rendererSpecified = true;
        } else if (parsed.name == "--benchmark-frame-limit") {
            if (frameLimitSpecified) return duplicateError(parsed.name);
            if (!takeValue(parsed, index, value)) {
                return parseError("Option '--benchmark-frame-limit' requires a value.");
            }
            const auto frameLimit = parseFrameLimitMode(value);
            if (!frameLimit) {
                return parseError("Invalid benchmark frame limit '" + std::string(value)
                    + "'. Expected platform-default, smooth60, balanced, battery30, or display.");
            }
            result.options.frameLimitMode = *frameLimit;
            frameLimitSpecified = true;
        } else if (parsed.name == "--benchmark-resolution") {
            if (resolutionSpecified) return duplicateError(parsed.name);
            if (!takeValue(parsed, index, value)) return parseError("Option '--benchmark-resolution' requires a value.");
            if (!parseResolution(value, result.options.resolution)) {
                return parseError("Invalid benchmark resolution '" + std::string(value)
                    + "'. Expected WIDTHxHEIGHT from 320x200 through 16384x16384.");
            }
            resolutionSpecified = true;
        } else if (parsed.name == "--benchmark-json") {
            if (jsonSpecified) return duplicateError(parsed.name);
            if (!takeValue(parsed, index, value)) return parseError("Option '--benchmark-json' requires a path.");
            result.options.jsonPath = std::filesystem::path(value);
            if (result.options.jsonPath.filename().empty()) {
                return parseError("Option '--benchmark-json' must name a JSON output file, not a directory.");
            }
            jsonSpecified = true;
        } else if (parsed.name == "--benchmark-screenshot") {
            if (screenshotSpecified) return duplicateError(parsed.name);
            if (!takeValue(parsed, index, value)) {
                return parseError("Option '--benchmark-screenshot' requires a path.");
            }
            result.options.screenshotPath = std::filesystem::path(value);
            if (!pngOutputPath(result.options.screenshotPath)) {
                return parseError("Option '--benchmark-screenshot' must name a PNG output file.");
            }
            screenshotSpecified = true;
        } else if (parsed.name == "--benchmark-profile-dir") {
            if (profileSpecified) return duplicateError(parsed.name);
            if (!takeValue(parsed, index, value)) return parseError("Option '--benchmark-profile-dir' requires a path.");
            result.options.profileDirectory = std::filesystem::path(value);
            if (unsafeDirectoryPath(result.options.profileDirectory)) {
                return parseError("Option '--benchmark-profile-dir' must name a dedicated non-root directory.");
            }
            profileSpecified = true;
        } else {
            return parseError("Unknown command-line option '" + std::string(arguments[index])
                + "'. Run --help to list supported options.");
        }
        sawBenchmarkArgument = true;
    }

    result.options.enabled = sawBenchmarkArgument;
    if (result.options.showHelp) return result;
    if (!result.options.enabled) return result;
    if (!scenarioSpecified) {
        return parseError("Benchmark mode requires --benchmark-scenario.");
    }
    if (durationSpecified && frameCountSpecified) {
        return parseError("--benchmark-duration-seconds and --benchmark-frame-count are mutually exclusive.");
    }
    if (frameCountSpecified) result.options.durationSeconds.reset();
    if (!jsonSpecified) {
        return parseError("Benchmark mode requires --benchmark-json so results are saved as machine-readable data.");
    }
    if (!profileSpecified) {
        return parseError("Benchmark mode requires --benchmark-profile-dir so player saves and preferences remain isolated.");
    }
    return result;
}

NativeBenchmarkParseResult parseNativeBenchmarkOptions(
    int argumentCount,
    const char* const* arguments)
{
    if (argumentCount <= 1 || arguments == nullptr) {
        return parseNativeBenchmarkOptions(std::span<const std::string_view> {});
    }
    std::vector<std::string_view> views;
    views.reserve(static_cast<std::size_t>(argumentCount - 1));
    for (int index = 1; index < argumentCount; ++index) {
        if (arguments[index] == nullptr) {
            return parseError("Command-line argument " + std::to_string(index) + " is null.");
        }
        views.emplace_back(arguments[index]);
    }
    return parseNativeBenchmarkOptions(views);
}

std::string nativeBenchmarkHelpText(std::string_view executableName)
{
    std::ostringstream out;
    out << "Usage:\n"
        << "  " << executableName << " [benchmark options]\n"
        << "  " << executableName << " --help\n\n"
        << "Required benchmark options:\n"
        << "  --benchmark-scenario <title|hangar|launch|flyby|orbit|surface-ops|mining>\n"
        << "  --benchmark-json <path>             Machine-readable report destination.\n"
        << "  --benchmark-profile-dir <path>      Dedicated save/preferences directory.\n\n"
        << "Capture options:\n"
        << "  --benchmark-seed <integer>          Decimal or 0x hexadecimal; default 0x0BEB17.\n"
        << "  --benchmark-warmup-seconds <value>  Range 0..600; default 10.\n"
        << "  --benchmark-duration-seconds <value> Range (0,3600]; default 60.\n"
        << "  --benchmark-frame-count <count>     Range 1..10000000; replaces duration.\n"
        << "  --benchmark-renderer <active|opengl|vulkan> Default active.\n"
        << "  --benchmark-frame-limit <platform-default|smooth60|balanced|battery30|display>\n"
        << "                                      Default smooth60.\n"
        << "  --benchmark-resolution <WIDTHxHEIGHT> Default 1280x800.\n"
        << "  --benchmark-screenshot <path.png>  Optional untimed final-state image.\n\n"
        << "Duration and frame count are mutually exclusive. Options accept either\n"
        << "'--option value' or '--option=value'. Benchmark mode never uses the\n"
        << "player profile directory.\n";
    return out.str();
}

std::string_view nativeBenchmarkScenarioName(NativeBenchmarkScenario scenario)
{
    switch (scenario) {
    case NativeBenchmarkScenario::Title: return "title";
    case NativeBenchmarkScenario::Hangar: return "hangar";
    case NativeBenchmarkScenario::Launch: return "launch";
    case NativeBenchmarkScenario::Flyby: return "flyby";
    case NativeBenchmarkScenario::Orbit: return "orbit";
    case NativeBenchmarkScenario::SurfaceOps: return "surface-ops";
    case NativeBenchmarkScenario::Mining: return "mining";
    }
    return "mining";
}

std::string_view nativeBenchmarkRendererName(NativeBenchmarkRenderer renderer)
{
    switch (renderer) {
    case NativeBenchmarkRenderer::Active: return "active";
    case NativeBenchmarkRenderer::OpenGl: return "opengl";
    case NativeBenchmarkRenderer::Vulkan: return "vulkan";
    }
    return "active";
}

bool validateNativeBenchmarkStorageIsolation(
    const std::filesystem::path& benchmarkDirectory,
    const std::filesystem::path& playerProfileDirectory,
    std::string& error)
{
    error.clear();
    if (unsafeDirectoryPath(benchmarkDirectory)) {
        error = "Benchmark profile directory must be a dedicated non-root directory.";
        return false;
    }
    if (unsafeDirectoryPath(playerProfileDirectory)) {
        error = "Player profile directory is unavailable; benchmark isolation cannot be verified.";
        return false;
    }

    const std::filesystem::path benchmark = normalizedAbsolutePath(benchmarkDirectory);
    const std::filesystem::path player = normalizedAbsolutePath(playerProfileDirectory);
    if (pathContains(player, benchmark) || pathContains(benchmark, player)) {
        error = "Benchmark profile directory must not equal, contain, or be contained by the player profile directory.";
        return false;
    }
    return true;
}

NativeBenchmarkController::NativeBenchmarkController(
    NativeBenchmarkOptions options,
    BenchmarkRunDescriptor descriptor)
    : options_(std::move(options)),
      fixedSimulationDeltaSeconds_(frozenSimulationDeltaSeconds(descriptor)),
      fixedWarmupFrameCount_(fixedWarmupFrameCount(options_, descriptor)),
      accumulator_(
          normalizedDescriptor(options_, std::move(descriptor)),
          options_.frameCount.has_value()
              ? static_cast<std::size_t>(*options_.frameCount)
              : 0)
{
}

NativeBenchmarkUpdate NativeBenchmarkController::start(double monotonicSeconds)
{
    if (phase_ != NativeBenchmarkPhase::NotStarted) {
        NativeBenchmarkUpdate result = update();
        result.error = "Benchmark controller has already been started.";
        return result;
    }
    if (!std::isfinite(monotonicSeconds) || monotonicSeconds < 0.0) {
        NativeBenchmarkUpdate result = update();
        result.error = "Benchmark start time must be a finite non-negative monotonic timestamp.";
        return result;
    }

    warmupStartedSeconds_ = monotonicSeconds;
    lastTimestampSeconds_ = monotonicSeconds;
    if (options_.warmupSeconds <= 0.0) {
        phase_ = NativeBenchmarkPhase::Capture;
        captureStartedSeconds_ = monotonicSeconds;
        NativeBenchmarkUpdate result = update();
        result.transitionedToCapture = true;
        return result;
    }
    phase_ = NativeBenchmarkPhase::Warmup;
    return update();
}

NativeBenchmarkUpdate NativeBenchmarkController::recordFrame(
    double monotonicSeconds,
    const BenchmarkFrameSample& sample)
{
    if (phase_ == NativeBenchmarkPhase::NotStarted) {
        NativeBenchmarkUpdate result = update();
        result.error = "Benchmark controller must be started before recording frames.";
        return result;
    }
    if (phase_ == NativeBenchmarkPhase::Complete) {
        return update();
    }
    if (!std::isfinite(monotonicSeconds) || monotonicSeconds < lastTimestampSeconds_) {
        NativeBenchmarkUpdate result = update();
        result.error = "Benchmark frame timestamp moved backwards or was not finite.";
        return result;
    }
    lastTimestampSeconds_ = monotonicSeconds;

    if (phase_ == NativeBenchmarkPhase::Warmup) {
        const bool fixedWarmupComplete = fixedWarmupFrameCount_ > 0
            && ++submittedWarmupFrameCount_ >= fixedWarmupFrameCount_;
        const bool timedWarmupComplete = fixedWarmupFrameCount_ == 0
            && monotonicSeconds - warmupStartedSeconds_ >= options_.warmupSeconds;
        if (!fixedWarmupComplete && !timedWarmupComplete) {
            return update();
        }
        phase_ = NativeBenchmarkPhase::Capture;
        captureStartedSeconds_ = monotonicSeconds;
        NativeBenchmarkUpdate result = update();
        result.transitionedToCapture = true;
        // Discard the boundary frame: part of its interval belongs to warm-up.
        return result;
    }

    ++submittedSampleCount_;
    const bool accepted = accumulator_.addSample(sample);
    bool finished = false;
    if (options_.frameCount.has_value()) {
        finished = submittedSampleCount_ >= *options_.frameCount;
    } else if (options_.durationSeconds.has_value()) {
        finished = monotonicSeconds - captureStartedSeconds_ >= *options_.durationSeconds;
    }
    if (finished) phase_ = NativeBenchmarkPhase::Complete;

    NativeBenchmarkUpdate result = update(accepted);
    result.transitionedToComplete = finished;
    return result;
}

NativeBenchmarkPhase NativeBenchmarkController::phase() const
{
    return phase_;
}

bool NativeBenchmarkController::complete() const
{
    return phase_ == NativeBenchmarkPhase::Complete;
}

std::size_t NativeBenchmarkController::submittedSampleCount() const
{
    return submittedSampleCount_;
}

double NativeBenchmarkController::fixedSimulationDeltaSeconds() const
{
    return fixedSimulationDeltaSeconds_;
}

const NativeBenchmarkOptions& NativeBenchmarkController::options() const
{
    return options_;
}

void NativeBenchmarkController::setFinalGameplayStateHash(std::optional<std::uint64_t> hash)
{
    accumulator_.setFinalGameplayStateHash(hash);
}

BenchmarkReport NativeBenchmarkController::report() const
{
    return accumulator_.report();
}

std::string NativeBenchmarkController::reportJson() const
{
    return accumulator_.reportJson();
}

NativeBenchmarkUpdate NativeBenchmarkController::update(bool accepted) const
{
    NativeBenchmarkUpdate result;
    result.phase = phase_;
    result.sampleAccepted = accepted;
    result.submittedSampleCount = submittedSampleCount_;
    if (phase_ == NativeBenchmarkPhase::Warmup) {
        result.phaseElapsedSeconds = std::max(0.0, lastTimestampSeconds_ - warmupStartedSeconds_);
    } else if (phase_ == NativeBenchmarkPhase::Capture || phase_ == NativeBenchmarkPhase::Complete) {
        result.phaseElapsedSeconds = std::max(0.0, lastTimestampSeconds_ - captureStartedSeconds_);
    }
    return result;
}

} // namespace rocket::performance
