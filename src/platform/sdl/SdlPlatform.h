#pragma once

#include "platform/AppServices.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace rocket {

class RocketGameApp;

enum class NativeFrameDisposition {
    Active,
    IdleTransition,
    IdlePaused,
    SuspendTransition,
    Suspended
};

inline constexpr int nativeIdleEventWaitMilliseconds = 100;

constexpr bool nativeFrameWaitsForEvents(NativeFrameDisposition disposition)
{
    return disposition == NativeFrameDisposition::IdlePaused
        || disposition == NativeFrameDisposition::Suspended;
}

constexpr bool nativeFrameRenders(NativeFrameDisposition disposition)
{
    return disposition != NativeFrameDisposition::Suspended;
}

constexpr bool nativeFrameAcceptsRealtimeInput(NativeFrameDisposition disposition)
{
    return disposition == NativeFrameDisposition::Active;
}

// Benchmark windows must not depend on whether the desktop window manager
// grants foreground focus. This policy is used only by the save-isolated CLI;
// ordinary gameplay retains focus-loss release and pause semantics.
constexpr bool nativeEffectiveFocus(bool windowFocused, bool benchmarkMode)
{
    return windowFocused || benchmarkMode;
}

class NativeFrameLifecycle {
public:
    void reset(bool visible = true, bool focused = true);
    void setVisible(bool visible);
    void setFocused(bool focused);
    NativeFrameDisposition disposition() const;
    void completeFrame();
    bool consumeFrameClockReset();
    bool visible() const;
    bool focused() const;

private:
    bool visible_ = true;
    bool focused_ = true;
    bool idleTransitionPending_ = false;
    bool suspendTransitionPending_ = false;
    bool frameClockResetPending_ = false;
};

struct NativeFramePacingDecision {
    std::uint64_t delayNanoseconds = 0;
};

class NativeFramePacer {
public:
    void configure(bool swapIntervalEnabled, double refreshRateHz);
    void resetDeadline();
    NativeFramePacingDecision next(std::uint64_t nowNanoseconds);
    bool active() const;
    std::uint64_t intervalNanoseconds() const;

private:
    bool active_ = false;
    std::uint64_t intervalNanoseconds_ = 0;
    std::uint64_t nextDeadlineNanoseconds_ = 0;
};

class SdlPlatform final : public IPlatformHost, public IControllerSource {
public:
    explicit SdlPlatform(IPreferenceStore& preferences, bool benchmarkMode = false);
    ~SdlPlatform() override;

    bool initialize(int initialWidth = 1280, int initialHeight = 800);
    void shutdown();
    bool processEvents(RocketGameApp& app);
    void applyKeyboardState(RocketGameApp& app);
    NativeFrameDisposition frameDisposition() const;
    void completeFrame();
    bool consumeFrameClockReset();
    bool consumeGraphicsRebuildRequest();
    // Keep the native window off-screen while Vulkan, assets, and RmlUi
    // initialize so players never see an unpainted OS/Vulkan surface.
    bool showWindowWhenReady();
    SDL_Window* window() const noexcept;

    static std::filesystem::path preferenceDirectory();
    static std::filesystem::path executableDirectory();

    double monotonicSeconds() const override;
    double displayRefreshRateHz() const override;
    ViewportMetrics viewportMetrics() override;
    bool focused() const override;
    bool visible() const override;
    bool fullscreenAvailable() const override;
    bool fullscreen() const override;
    bool setFullscreen(bool enabled) override;
    void log(PlatformLogLevel level, std::string_view message) override;
    bool haptic(double durationSeconds, double weakMagnitude, double strongMagnitude) override;
    void paceFrame() override;
    PlatformDiagnostics diagnostics() const override;

    ControllerFrame sampleFrame(double realTimeSeconds) override;
    void setPreferences(const ControllerPreferences& preferences) override;
    InputSource activeSource() const override;
    void reset() override;

private:
    struct OpenGamepad {
        SDL_Gamepad* handle = nullptr;
        std::string name;
        ControllerFamily family = ControllerFamily::Generic;
    };

    bool handleEvent(RocketGameApp& app, const SDL_Event& event);
    bool openGamepad(SDL_JoystickID id);
    void closeGamepad(SDL_JoystickID id);
    void rebuildControllerSnapshots();
    void noteKeyboardPointerActivity();
    void releaseRealtimeInputs(RocketGameApp& app);
    void handleKeyDown(RocketGameApp& app, const SDL_KeyboardEvent& event);
    void setWindowVisible(RocketGameApp& app, bool visible);
    void refreshViewportMetrics();
    void refreshDisplayTiming();
    void applySoftwareFramePacing();

    IPreferenceStore& preferences_;
    SDL_Window* window_ = nullptr;
    std::unordered_map<SDL_JoystickID, OpenGamepad> gamepads_;
    std::vector<RawControllerSnapshot> controllerSnapshots_;
    ControllerTracker controllerTracker_;
    InputSourceArbiter sourceArbiter_;
    ControllerFrame lastControllerFrame_;
    ControllerPreferences controllerPreferences_;
    ViewportMetrics viewportMetrics_ {1280, 800, 1280, 800, 1.0F};
    PlatformDiagnostics diagnostics_;
    NativeFrameLifecycle frameLifecycle_;
    NativeFramePacer framePacer_;
    std::uint64_t suspendedWakeWindowStartedNanoseconds_ = 0;
    int suspendedWakeWindowCount_ = 0;
    SDL_DisplayID displayId_ = 0;
    double displayRefreshRateHz_ = 0.0;
    bool initialized_ = false;
    bool vulkanLibraryLoaded_ = false;
    bool fullscreen_ = false;
    bool graphicsRebuildRequested_ = false;
    bool benchmarkMode_ = false;
    // Space begins a launch return. If it remains physically held while the
    // result board arrives, do not let that same press activate Continue.
    // Enter is guarded alongside it because both keys confirm focused UI.
    bool launchOutcomeConfirmReleaseGuard_ = false;
    float mouseX_ = 0.0F;
    float mouseY_ = 0.0F;
};

} // namespace rocket
