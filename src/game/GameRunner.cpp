#include "game/GameRunner.h"

#include <algorithm>
#include <cmath>
#include <utility>

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
        && lhs.frameLimitMode == rhs.frameLimitMode
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
    frameWithDelta(std::nullopt);
}

void GameRunner::frameForBenchmark(double fixedDeltaSeconds)
{
    if (!std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds < 0.0) return;
    frameWithDelta(std::clamp(fixedDeltaSeconds, 0.0, 0.25));
}

void GameRunner::frameWithDelta(std::optional<double> fixedDeltaSeconds)
{
    if (!initialized_) {
        return;
    }

    const double frameStarted = services_.host.monotonicSeconds();
    const double now = frameStarted;
    const double measuredDelta = lastFrameSeconds_ <= 0.0 ? 1.0 / 60.0 : now - lastFrameSeconds_;
    const double delta = fixedDeltaSeconds.value_or(measuredDelta);
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
    services_.renderer.endFrameAndPresent();
    const double presentFinished = performanceEnabled ? services_.host.monotonicSeconds() : uiFinished;
    services_.host.paceFrame();

    if (performanceEnabled) {
        const double cpuMilliseconds = std::max(0.0, uiFinished - frameStarted) * 1000.0;
        performanceSamples_.record({delta * 1000.0, cpuMilliseconds});
        if (performanceVisibilityChanged || presentFinished - lastPerformancePublishSeconds_ >= 0.25) {
            const performance::FrameTimingSummary summary = performanceSamples_.summarize();
            PerformanceStats stats;
            stats.startupMilliseconds = startupMilliseconds_;
            stats.framesPerSecond = summary.frame.average > 0.0 ? 1000.0 / summary.frame.average : 0.0;
            stats.latestFrameTimeMilliseconds = std::max(0.0, std::min(delta, 1.0)) * 1000.0;
            stats.frameTimeMilliseconds = summary.frame.average;
            stats.medianFrameTimeMilliseconds = summary.frame.median;
            stats.p95FrameTimeMilliseconds = summary.frame.p95;
            stats.p99FrameTimeMilliseconds = summary.frame.p99;
            stats.cpuFrameMilliseconds = cpuMilliseconds;
            stats.medianCpuFrameMilliseconds = summary.cpu.median;
            stats.p95CpuFrameMilliseconds = summary.cpu.p95;
            stats.p99CpuFrameMilliseconds = summary.cpu.p99;
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
            // Publication occurs after this frame's sample. Its immediate work
            // contaminates the next start-to-start interval; deferred RmlUi work
            // contaminates the following render's CPU time and the interval after
            // it. Quarantine those metrics independently so the ordinary CPU work
            // in that latter frame remains represented.
            performanceSamples_.quarantineOverlayPublication();
            lastPerformancePublishSeconds_ = services_.host.monotonicSeconds();
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
    performanceSamples_.reset();
    lastPerformancePublishSeconds_ = 0.0;
}

} // namespace rocket
