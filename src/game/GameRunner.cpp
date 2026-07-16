#include "game/GameRunner.h"

#include <algorithm>

namespace rocket {

GameRunner::GameRunner(AppServices& services)
    : services_(services), app_(services)
{
}

bool GameRunner::initialize()
{
    if (initialized_) {
        return true;
    }
    if (!app_.initialize()) {
        return false;
    }
    lastFrameSeconds_ = services_.host.monotonicSeconds();
    initialized_ = true;
    return true;
}

void GameRunner::frame()
{
    if (!initialized_) {
        return;
    }

    const double now = services_.host.monotonicSeconds();
    const double delta = lastFrameSeconds_ <= 0.0 ? 1.0 / 60.0 : now - lastFrameSeconds_;
    lastFrameSeconds_ = now;

    const ControllerFrame controllerFrame = services_.controllers.sampleFrame(now);
    const AppPreferences preferences = services_.preferences.load();
    app_.setActiveInputSource(services_.controllers.activeSource());
    app_.setControllerPreferences(preferences.controller);
    app_.inputFrame(controllerFrame, now);
    if (const std::optional<ControllerFrame> preview = services_.controllers.syntheticPreviewFrame()) {
        app_.inputFrame(*preview, now);
    }

    double remaining = std::clamp(delta, 0.0, 0.25) * std::clamp(preferences.gameSpeed, 0.25, 8.0);
    int steps = 0;
    while (remaining > 0.0 && steps < 24) {
        const double step = std::min(remaining, 1.0 / 30.0);
        app_.tick(step);
        remaining -= step;
        ++steps;
    }

    dispatchPendingHaptic();
    app_.render();
    services_.host.present();
}

void GameRunner::shutdown()
{
    if (!initialized_) {
        return;
    }
    services_.controllers.reset();
    app_.shutdown();
    initialized_ = false;
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

} // namespace rocket
