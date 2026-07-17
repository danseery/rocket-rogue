#include "game/GameRunner.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>
#include <vector>

namespace rocket {
namespace {

bool sameControllerPreferences(const ControllerPreferences& lhs, const ControllerPreferences& rhs)
{
    return lhs.promptFamily == rhs.promptFamily
        && lhs.stickDeadzone == rhs.stickDeadzone
        && lhs.invertFlightY == rhs.invertFlightY
        && lhs.swapConfirmCancel == rhs.swapConfirmCancel
        && lhs.vibrationEnabled == rhs.vibrationEnabled;
}

bool sameAppPreferences(const AppPreferences& lhs, const AppPreferences& rhs)
{
    return sameControllerPreferences(lhs.controller, rhs.controller)
        && lhs.resolutionPreset == rhs.resolutionPreset
        && lhs.gameSpeed == rhs.gameSpeed
        && lhs.debugToolsEnabled == rhs.debugToolsEnabled
        && lhs.performanceStatsEnabled == rhs.performanceStatsEnabled
        && lhs.helpDisabled == rhs.helpDisabled
        && lhs.cameraShakeDisabled == rhs.cameraShakeDisabled
        && lhs.fullscreen == rhs.fullscreen
        && lhs.dismissedHelpTopics == rhs.dismissedHelpTopics;
}

} // namespace

GameRunner::GameRunner(AppServices& services)
    : services_(services), app_(services)
{
}

bool GameRunner::initialize()
{
    if (initialized_) {
        return true;
    }
    const double startupBegan = services_.host.monotonicSeconds();
    // Push the authoritative cached preferences before renderer/UI startup so
    // those services never need to pull preferences for themselves.
    refreshPreferences(true);
    if (!app_.initialize()) {
        return false;
    }
    startupMilliseconds_ = std::max(
        0.0,
        services_.host.monotonicSeconds() - startupBegan) * 1000.0;
    lastFrameSeconds_ = services_.host.monotonicSeconds();
    initialized_ = true;
    return true;
}

void GameRunner::frame()
{
    if (!initialized_) {
        return;
    }

    const double frameStarted = services_.host.monotonicSeconds();
    const double now = frameStarted;
    const double delta = lastFrameSeconds_ <= 0.0 ? 1.0 / 60.0 : now - lastFrameSeconds_;
    lastFrameSeconds_ = now;

    refreshPreferences(false);
    const AppPreferences& preferences = cachedPreferences_;
    const ControllerFrame controllerFrame = services_.controllers.sampleFrame(now);
    const bool performanceEnabled = preferences.performanceStatsEnabled;
    const bool performanceVisibilityChanged = performanceEnabled != performanceStatsEnabled_;
    if (performanceEnabled && performanceVisibilityChanged) {
        resetPerformanceSamples();
    }
    app_.setActiveInputSource(services_.controllers.activeSource());
    app_.inputFrame(controllerFrame, now);
    if (const std::optional<ControllerFrame> preview = services_.controllers.syntheticPreviewFrame()) {
        app_.inputFrame(*preview, now);
    }
    const double inputFinished = performanceEnabled ? services_.host.monotonicSeconds() : frameStarted;

    double remaining = std::clamp(delta, 0.0, 0.25) * std::clamp(preferences.gameSpeed, 0.25, 8.0);
    int steps = 0;
    while (remaining > 0.0 && steps < 24) {
        const double step = std::min(remaining, 1.0 / 30.0);
        app_.tick(step);
        remaining -= step;
        ++steps;
    }

    dispatchPendingHaptic();
    const double simulationFinished = performanceEnabled ? services_.host.monotonicSeconds() : inputFinished;
    app_.renderScene();
    const double sceneFinished = performanceEnabled ? services_.host.monotonicSeconds() : simulationFinished;
    app_.renderUi();
    const double uiFinished = performanceEnabled ? services_.host.monotonicSeconds() : sceneFinished;
    services_.host.present();
    const double presentFinished = performanceEnabled ? services_.host.monotonicSeconds() : uiFinished;
    services_.host.paceFrame();

    if (performanceEnabled) {
        const double cpuMilliseconds = std::max(0.0, uiFinished - frameStarted) * 1000.0;
        recordPerformanceSample(delta, cpuMilliseconds);
        if (performanceVisibilityChanged || presentFinished - lastPerformancePublishSeconds_ >= 0.25) {
            const SampleSummary frameSummary = summarizeSamples(performanceFrameSamples_);
            const SampleSummary cpuSummary = summarizeSamples(performanceCpuSamples_);
            PerformanceStats stats;
            stats.startupMilliseconds = startupMilliseconds_;
            stats.framesPerSecond = frameSummary.average > 0.0 ? 1000.0 / frameSummary.average : 0.0;
            stats.latestFrameTimeMilliseconds = std::max(0.0, std::min(delta, 1.0)) * 1000.0;
            stats.frameTimeMilliseconds = frameSummary.average;
            stats.medianFrameTimeMilliseconds = frameSummary.median;
            stats.p95FrameTimeMilliseconds = frameSummary.p95;
            stats.p99FrameTimeMilliseconds = frameSummary.p99;
            stats.cpuFrameMilliseconds = cpuMilliseconds;
            stats.medianCpuFrameMilliseconds = cpuSummary.median;
            stats.p95CpuFrameMilliseconds = cpuSummary.p95;
            stats.p99CpuFrameMilliseconds = cpuSummary.p99;
            stats.inputMilliseconds = std::max(0.0, inputFinished - frameStarted) * 1000.0;
            stats.simulationMilliseconds = std::max(0.0, simulationFinished - inputFinished) * 1000.0;
            stats.sceneRenderMilliseconds = std::max(0.0, sceneFinished - simulationFinished) * 1000.0;
            stats.uiRenderMilliseconds = std::max(0.0, uiFinished - sceneFinished) * 1000.0;
            stats.presentMilliseconds = std::max(0.0, presentFinished - uiFinished) * 1000.0;
            stats.simulationSteps = steps;
            stats.simulationDeltaClamped = delta < 0.0 || delta > 0.25;
            stats.viewport = services_.host.viewportMetrics();
            stats.renderer = services_.renderer.diagnostics();
            stats.textures = services_.textures.diagnostics();
            stats.ui = services_.ui.diagnostics();
            stats.platform = services_.host.diagnostics();
            services_.ui.setPerformanceStats(stats, true);
            lastPerformancePublishSeconds_ = presentFinished;
        }
    } else if (performanceVisibilityChanged) {
        services_.ui.setPerformanceStats({}, false);
    }
    performanceStatsEnabled_ = performanceEnabled;
}

void GameRunner::resetFrameClock()
{
    if (!initialized_) return;
    lastFrameSeconds_ = services_.host.monotonicSeconds();
}

void GameRunner::shutdown()
{
    if (!initialized_) {
        return;
    }
    services_.controllers.reset();
    if (performanceStatsEnabled_) {
        services_.ui.setPerformanceStats({}, false);
    }
    app_.shutdown();
    cachedPreferences_ = {};
    startupMilliseconds_ = 0.0;
    preferenceRevision_ = 0;
    preferenceCacheInitialized_ = false;
    performanceStatsEnabled_ = false;
    resetPerformanceSamples();
    initialized_ = false;
}

void GameRunner::refreshPreferences(bool force)
{
    const std::uint64_t revision = services_.preferences.revision();
    if (!force && preferenceCacheInitialized_ && revision == preferenceRevision_) {
        return;
    }

    AppPreferences latest = services_.preferences.load();
    const bool controllerChanged = !preferenceCacheInitialized_
        || !sameControllerPreferences(cachedPreferences_.controller, latest.controller);
    const bool rendererChanged = !preferenceCacheInitialized_
        || !sameAppPreferences(cachedPreferences_, latest);

    cachedPreferences_ = std::move(latest);
    // A store may normalize while loading; sample the token again after the
    // authoritative value has been captured.
    preferenceRevision_ = services_.preferences.revision();
    preferenceCacheInitialized_ = true;

    if (force || controllerChanged) {
        services_.controllers.setPreferences(cachedPreferences_.controller);
        app_.setControllerPreferences(cachedPreferences_.controller);
    }
    if (force || rendererChanged) {
        services_.renderer.setPreferences(cachedPreferences_);
    }
}

RocketGameApp& GameRunner::app()
{
    return app_;
}

const RocketGameApp& GameRunner::app() const
{
    return app_;
}

void GameRunner::dispatchPendingHaptic()
{
    if (!app_.controllerPreferences().vibrationEnabled) {
        (void)app_.consumePendingControllerHapticCue();
        return;
    }

    switch (app_.consumePendingControllerHapticCue()) {
    case ControllerHapticCue::Confirmation:
        services_.host.haptic(0.045, 0.12, 0.16);
        break;
    case ControllerHapticCue::MiningHardContact:
        services_.host.haptic(0.080, 0.25, 0.38);
        break;
    case ControllerHapticCue::Damage:
        services_.host.haptic(0.140, 0.42, 0.68);
        break;
    case ControllerHapticCue::Failure:
        services_.host.haptic(0.300, 0.70, 1.00);
        break;
    case ControllerHapticCue::None:
    default:
        break;
    }
}

void GameRunner::resetPerformanceSamples()
{
    performanceFrameSamples_.fill(0.0);
    performanceCpuSamples_.fill(0.0);
    performanceSampleCount_ = 0;
    performanceSampleCursor_ = 0;
    lastPerformancePublishSeconds_ = 0.0;
}

void GameRunner::recordPerformanceSample(double deltaSeconds, double cpuMilliseconds)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0 || deltaSeconds > 1.0
        || !std::isfinite(cpuMilliseconds) || cpuMilliseconds < 0.0) {
        return;
    }
    performanceFrameSamples_[performanceSampleCursor_] = deltaSeconds * 1000.0;
    performanceCpuSamples_[performanceSampleCursor_] = cpuMilliseconds;
    performanceSampleCursor_ = (performanceSampleCursor_ + 1) % performanceSampleCapacity_;
    performanceSampleCount_ = std::min(performanceSampleCount_ + 1, performanceSampleCapacity_);
}

GameRunner::SampleSummary GameRunner::summarizeSamples(
    const std::array<double, performanceSampleCapacity_>& samples) const
{
    SampleSummary result;
    if (performanceSampleCount_ == 0) {
        return result;
    }
    std::vector<double> sorted(
        samples.begin(),
        samples.begin() + static_cast<std::ptrdiff_t>(performanceSampleCount_));
    result.average = std::accumulate(sorted.begin(), sorted.end(), 0.0) /
        static_cast<double>(sorted.size());
    std::sort(sorted.begin(), sorted.end());
    const auto percentile = [&sorted](double quantile) {
        const std::size_t index = std::min(
            sorted.size() - 1,
            static_cast<std::size_t>(std::ceil(static_cast<double>(sorted.size()) * quantile)) - 1);
        return sorted[index];
    };
    result.median = percentile(0.50);
    result.p95 = percentile(0.95);
    result.p99 = percentile(0.99);
    return result;
}

} // namespace rocket
