#include "platform/sdl/SdlPlatform.h"

#include "game/RocketGameApp.h"

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace rocket {
namespace {

double normalizedAxis(Sint16 value)
{
    return value < 0 ? static_cast<double>(value) / 32768.0 : static_cast<double>(value) / 32767.0;
}

double normalizedTrigger(Sint16 value)
{
    return std::clamp(static_cast<double>(value) / 32767.0, 0.0, 1.0);
}

int rmlMouseButton(Uint8 button)
{
    if (button == SDL_BUTTON_LEFT) return 0;
    if (button == SDL_BUTTON_RIGHT) return 1;
    if (button == SDL_BUTTON_MIDDLE) return 2;
    return std::max(0, static_cast<int>(button) - 1);
}

void setButton(RawControllerSnapshot& snapshot, ControllerButton button, bool down)
{
    snapshot.buttons[static_cast<std::size_t>(button)] = down ? 1.0 : 0.0;
}

bool keyDown(const bool* state, SDL_Scancode first, SDL_Scancode second)
{
    return state && (state[first] || state[second]);
}

} // namespace

void NativeFrameLifecycle::reset(bool visible, bool focused)
{
    visible_ = visible;
    focused_ = focused;
    idleTransitionPending_ = visible_ && !focused_;
    suspendTransitionPending_ = !visible_;
    frameClockResetPending_ = false;
}

void NativeFrameLifecycle::setVisible(bool visible)
{
    if (visible == visible_) return;

    visible_ = visible;
    if (visible_) {
        suspendTransitionPending_ = false;
        idleTransitionPending_ = !focused_;
        frameClockResetPending_ = true;
    } else {
        idleTransitionPending_ = false;
        suspendTransitionPending_ = true;
    }
}

void NativeFrameLifecycle::setFocused(bool focused)
{
    if (focused == focused_) return;

    focused_ = focused;
    if (!visible_) {
        idleTransitionPending_ = false;
        return;
    }
    if (focused_) {
        idleTransitionPending_ = false;
        frameClockResetPending_ = true;
    } else {
        idleTransitionPending_ = true;
    }
}

NativeFrameDisposition NativeFrameLifecycle::disposition() const
{
    if (!visible_) {
        return suspendTransitionPending_
            ? NativeFrameDisposition::SuspendTransition
            : NativeFrameDisposition::Suspended;
    }
    if (!focused_) {
        return idleTransitionPending_
            ? NativeFrameDisposition::IdleTransition
            : NativeFrameDisposition::IdlePaused;
    }
    return NativeFrameDisposition::Active;
}

void NativeFrameLifecycle::completeFrame()
{
    if (!visible_) {
        suspendTransitionPending_ = false;
    } else if (!focused_) {
        idleTransitionPending_ = false;
    }
}

bool NativeFrameLifecycle::consumeFrameClockReset()
{
    const bool pending = frameClockResetPending_;
    frameClockResetPending_ = false;
    return pending;
}

bool NativeFrameLifecycle::visible() const { return visible_; }
bool NativeFrameLifecycle::focused() const { return focused_; }

void NativeFramePacer::configure(bool swapIntervalEnabled, double refreshRateHz)
{
    active_ = !swapIntervalEnabled;
    if (std::isfinite(refreshRateHz) && refreshRateHz >= 1.0) {
        intervalNanoseconds_ = static_cast<std::uint64_t>(
            std::llround(1'000'000'000.0 / refreshRateHz));
    } else {
        intervalNanoseconds_ = 0;
    }
    resetDeadline();
}

void NativeFramePacer::resetDeadline()
{
    nextDeadlineNanoseconds_ = 0;
}

NativeFramePacingDecision NativeFramePacer::next(std::uint64_t nowNanoseconds)
{
    if (!active_) return {};
    if (intervalNanoseconds_ == 0) {
        // A refresh rate is not always available (remote desktops are a common
        // example). Yield for a bounded interval without inventing a 60 Hz cap.
        return {1'000'000};
    }

    if (nextDeadlineNanoseconds_ == 0) {
        nextDeadlineNanoseconds_ = nowNanoseconds + intervalNanoseconds_;
    } else if (nowNanoseconds > nextDeadlineNanoseconds_
        && nowNanoseconds - nextDeadlineNanoseconds_ > intervalNanoseconds_ * 4) {
        // A debugger stop, window move, or another long stall must not schedule
        // a burst of catch-up presentations.
        nextDeadlineNanoseconds_ = nowNanoseconds + intervalNanoseconds_;
        return {};
    }

    if (nowNanoseconds < nextDeadlineNanoseconds_) {
        const std::uint64_t delay = nextDeadlineNanoseconds_ - nowNanoseconds;
        nextDeadlineNanoseconds_ += intervalNanoseconds_;
        return {delay};
    }

    const std::uint64_t missedIntervals =
        (nowNanoseconds - nextDeadlineNanoseconds_) / intervalNanoseconds_ + 1;
    nextDeadlineNanoseconds_ += missedIntervals * intervalNanoseconds_;
    return {};
}

bool NativeFramePacer::active() const { return active_; }
std::uint64_t NativeFramePacer::intervalNanoseconds() const { return intervalNanoseconds_; }

SdlPlatform::SdlPlatform(IPreferenceStore& preferences, bool benchmarkMode)
    : preferences_(preferences), benchmarkMode_(benchmarkMode)
{
}
SdlPlatform::~SdlPlatform() { shutdown(); }

bool SdlPlatform::initialize(int initialWidth, int initialHeight)
{
    if (initialized_) return true;
    SDL_SetAppMetadata("OREBIT", "0.1.0", "game.rocketrogue.native");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        log(PlatformLogLevel::Error, std::string("SDL initialization failed: ") + SDL_GetError());
        return false;
    }

    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        log(PlatformLogLevel::Error, std::string("Vulkan loader is unavailable: ") + SDL_GetError());
        shutdown();
        return false;
    }
    vulkanLibraryLoaded_ = true;

    window_ = SDL_CreateWindow(
        "OREBIT",
        std::max(320, initialWidth),
        std::max(200, initialHeight),
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!window_) {
        log(PlatformLogLevel::Error, std::string("Window creation failed: ") + SDL_GetError());
        shutdown();
        return false;
    }
    // FIFO presentation is owned by the Vulkan backend. SDL never introduces
    // an independent software limiter in the Vulkan-only native build.
    diagnostics_.verticalSyncActive = true;

    const SDL_WindowFlags windowFlags = SDL_GetWindowFlags(window_);
    const bool focused = nativeEffectiveFocus(
        (windowFlags & SDL_WINDOW_INPUT_FOCUS) != 0,
        benchmarkMode_);
    const bool visible = (windowFlags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)) == 0;
    fullscreen_ = (windowFlags & SDL_WINDOW_FULLSCREEN) != 0;
    frameLifecycle_.reset(visible, focused);
    refreshViewportMetrics();
    refreshDisplayTiming();

    int gamepadCount = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&gamepadCount)) {
        for (int i = 0; i < gamepadCount; ++i) openGamepad(ids[i]);
        SDL_free(ids);
    }

    const AppPreferences preferences = preferences_.load();
    controllerPreferences_ = preferences.controller;
    initialized_ = true;
    if (preferences.fullscreen) setFullscreen(true);
    return true;
}

void SdlPlatform::shutdown()
{
    if (!window_ && !vulkanLibraryLoaded_ && !initialized_) return;
    for (auto& [id, gamepad] : gamepads_) {
        (void)id;
        if (gamepad.handle) SDL_CloseGamepad(gamepad.handle);
    }
    gamepads_.clear();
    controllerSnapshots_.clear();
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    if (vulkanLibraryLoaded_) SDL_Vulkan_UnloadLibrary();
    vulkanLibraryLoaded_ = false;
    SDL_Quit();
    diagnostics_ = {};
    suspendedWakeWindowStartedNanoseconds_ = 0;
    suspendedWakeWindowCount_ = 0;
    displayId_ = 0;
    displayRefreshRateHz_ = 0.0;
    graphicsRebuildRequested_ = false;
    frameLifecycle_.reset();
    framePacer_.configure(true, 0.0);
    initialized_ = false;
}

std::filesystem::path SdlPlatform::preferenceDirectory()
{
    char* path = SDL_GetPrefPath("Rocket Rogue", "Rocket Rogue");
    if (!path) return std::filesystem::current_path();
    const std::filesystem::path result(path);
    SDL_free(path);
    return result;
}

std::filesystem::path SdlPlatform::executableDirectory()
{
    const char* path = SDL_GetBasePath();
    return path ? std::filesystem::path(path) : std::filesystem::current_path();
}

bool SdlPlatform::processEvents(RocketGameApp& app)
{
    diagnostics_.idleMilliseconds = 0.0;
    bool mouseMotionPending = false;
    Uint64 lowCadenceWaitStarted = 0;

    const auto flushMouseMotion = [&]() {
        if (!mouseMotionPending) return;
        mouseMotionPending = false;
        if (!frameLifecycle_.visible()) return;
        noteKeyboardPointerActivity();
        app.uiMouseMove(static_cast<int>(mouseX_), static_cast<int>(mouseY_));
    };

    const auto dispatchEvent = [&](const SDL_Event& event) {
        if (benchmarkMode_
            && (event.type == SDL_EVENT_MOUSE_MOTION
                || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                || event.type == SDL_EVENT_MOUSE_BUTTON_UP
                || event.type == SDL_EVENT_MOUSE_WHEEL
                || event.type == SDL_EVENT_KEY_DOWN
                || event.type == SDL_EVENT_KEY_UP)) {
            return true;
        }
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            mouseX_ = event.motion.x;
            mouseY_ = event.motion.y;
            mouseMotionPending = true;
            return true;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
            || event.type == SDL_EVENT_MOUSE_BUTTON_UP
            || event.type == SDL_EVENT_MOUSE_WHEEL) {
            // Preserve ordering at pointer-button/scroll boundaries while
            // collapsing high-frequency motion bursts to their final point.
            flushMouseMotion();
        }
        return handleEvent(app, event);
    };

    SDL_Event event;
    const NativeFrameDisposition disposition = frameDisposition();
    if (disposition != NativeFrameDisposition::Suspended) {
        suspendedWakeWindowStartedNanoseconds_ = 0;
        suspendedWakeWindowCount_ = 0;
        diagnostics_.suspendedWakeupsPerSecond = 0.0;
    }
    if (nativeFrameWaitsForEvents(disposition)) {
        lowCadenceWaitStarted = SDL_GetTicksNS();
        const bool eventAvailable = SDL_WaitEventTimeout(&event, nativeIdleEventWaitMilliseconds);
        diagnostics_.idleMilliseconds = static_cast<double>(SDL_GetTicksNS() - lowCadenceWaitStarted) / 1'000'000.0;
        if (disposition == NativeFrameDisposition::Suspended) {
            ++diagnostics_.suspendedWakeups;
            const Uint64 now = SDL_GetTicksNS();
            if (suspendedWakeWindowStartedNanoseconds_ == 0) {
                suspendedWakeWindowStartedNanoseconds_ = now;
            }
            ++suspendedWakeWindowCount_;
            const Uint64 windowElapsed = now - suspendedWakeWindowStartedNanoseconds_;
            if (windowElapsed >= 1'000'000'000ULL) {
                diagnostics_.suspendedWakeupsPerSecond =
                    static_cast<double>(suspendedWakeWindowCount_) * 1'000'000'000.0 /
                    static_cast<double>(windowElapsed);
                suspendedWakeWindowStartedNanoseconds_ = now;
                suspendedWakeWindowCount_ = 0;
            }
        }
        if (!eventAvailable) return true;
        if (!dispatchEvent(event)) return false;
    }

    while (SDL_PollEvent(&event)) {
        if (!dispatchEvent(event)) return false;
    }
    flushMouseMotion();
    if (lowCadenceWaitStarted != 0 && nativeFrameWaitsForEvents(frameDisposition())) {
        const Uint64 targetWaitNanoseconds =
            static_cast<Uint64>(nativeIdleEventWaitMilliseconds) * 1'000'000ULL;
        const Uint64 elapsed = SDL_GetTicksNS() - lowCadenceWaitStarted;
        if (elapsed < targetWaitNanoseconds) {
            SDL_DelayNS(targetWaitNanoseconds - elapsed);
        }
        diagnostics_.idleMilliseconds =
            static_cast<double>(SDL_GetTicksNS() - lowCadenceWaitStarted) / 1'000'000.0;
    }
    return true;
}

bool SdlPlatform::handleEvent(RocketGameApp& app, const SDL_Event& event)
{
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return false;
    case SDL_EVENT_GAMEPAD_ADDED:
        openGamepad(event.gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        closeGamepad(event.gdevice.which);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        frameLifecycle_.setFocused(nativeEffectiveFocus(true, benchmarkMode_));
        framePacer_.resetDeadline();
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        frameLifecycle_.setFocused(nativeEffectiveFocus(false, benchmarkMode_));
        framePacer_.resetDeadline();
        releaseRealtimeInputs(app);
        break;
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
        setWindowVisible(app, true);
        refreshViewportMetrics();
        graphicsRebuildRequested_ = true;
        break;
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_OCCLUDED:
        setWindowVisible(app, false);
        break;
    case SDL_EVENT_WINDOW_MOVED:
        if (window_ && SDL_GetDisplayForWindow(window_) != displayId_) {
            refreshViewportMetrics();
            refreshDisplayTiming();
            graphicsRebuildRequested_ = true;
        }
        break;
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        refreshViewportMetrics();
        refreshDisplayTiming();
        graphicsRebuildRequested_ = true;
        break;
    case SDL_EVENT_WINDOW_RESIZED:
        viewportMetrics_.logicalWidth = std::max(1, static_cast<int>(event.window.data1));
        viewportMetrics_.logicalHeight = std::max(1, static_cast<int>(event.window.data2));
        viewportMetrics_.densityRatio = static_cast<float>(viewportMetrics_.drawableWidth)
            / static_cast<float>(viewportMetrics_.logicalWidth);
        graphicsRebuildRequested_ = true;
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        viewportMetrics_.drawableWidth = std::max(1, static_cast<int>(event.window.data1));
        viewportMetrics_.drawableHeight = std::max(1, static_cast<int>(event.window.data2));
        viewportMetrics_.densityRatio = static_cast<float>(viewportMetrics_.drawableWidth)
            / static_cast<float>(viewportMetrics_.logicalWidth);
        graphicsRebuildRequested_ = true;
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        fullscreen_ = true;
        refreshViewportMetrics();
        refreshDisplayTiming();
        graphicsRebuildRequested_ = true;
        break;
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        fullscreen_ = false;
        refreshViewportMetrics();
        refreshDisplayTiming();
        graphicsRebuildRequested_ = true;
        break;
    case SDL_EVENT_KEY_DOWN:
        noteKeyboardPointerActivity();
        handleKeyDown(app, event.key);
        break;
    case SDL_EVENT_KEY_UP:
        noteKeyboardPointerActivity();
        if (event.key.key == SDLK_SPACE) app.miningKeyboardDrill(false);
        if (event.key.key == SDLK_SPACE || event.key.key == SDLK_RETURN) {
            launchOutcomeConfirmReleaseGuard_ = false;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        noteKeyboardPointerActivity();
        mouseX_ = event.button.x;
        mouseY_ = event.button.y;
        const bool overUi = app.uiMouseDown(
            static_cast<int>(mouseX_),
            static_cast<int>(mouseY_),
            rmlMouseButton(event.button.button));
        if (!overUi
            && event.button.button == SDL_BUTTON_LEFT
            && app.inputContext() == InputContext::MiningActive) {
            app.miningDrill(true);
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        noteKeyboardPointerActivity();
        mouseX_ = event.button.x;
        mouseY_ = event.button.y;
        app.uiMouseUp(
            static_cast<int>(mouseX_),
            static_cast<int>(mouseY_),
            rmlMouseButton(event.button.button));
        if (event.button.button == SDL_BUTTON_LEFT) app.miningDrill(false);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        noteKeyboardPointerActivity();
        mouseX_ = event.wheel.mouse_x;
        mouseY_ = event.wheel.mouse_y;
        app.uiMouseWheel(
            static_cast<int>(mouseX_),
            static_cast<int>(mouseY_),
            -event.wheel.y * 90.0);
        break;
    default:
        break;
    }
    return true;
}

void SdlPlatform::applyKeyboardState(RocketGameApp& app)
{
    if (benchmarkMode_) return;
    const bool* state = SDL_GetKeyboardState(nullptr);
    const bool left = keyDown(state, SDL_SCANCODE_A, SDL_SCANCODE_LEFT);
    const bool right = keyDown(state, SDL_SCANCODE_D, SDL_SCANCODE_RIGHT);
    const bool up = keyDown(state, SDL_SCANCODE_W, SDL_SCANCODE_UP);
    const bool down = keyDown(state, SDL_SCANCODE_S, SDL_SCANCODE_DOWN);
    switch (app.inputContext()) {
    case InputContext::FlybyActive:
        app.flybyMove((left ? 1.0 : 0.0) - (right ? 1.0 : 0.0), (up ? 1.0 : 0.0) - (down ? 1.0 : 0.0));
        break;
    case InputContext::OrbitActive:
        app.orbitMove((right ? 1.0 : 0.0) - (left ? 1.0 : 0.0), (up ? 1.0 : 0.0) - (down ? 1.0 : 0.0));
        break;
    case InputContext::MiningActive:
    case InputContext::MiningService:
        app.miningMove((right ? 1.0 : 0.0) - (left ? 1.0 : 0.0), (down ? 1.0 : 0.0) - (up ? 1.0 : 0.0));
        app.miningKeyboardDrill(state && state[SDL_SCANCODE_SPACE]);
        break;
    default:
        break;
    }
}

NativeFrameDisposition SdlPlatform::frameDisposition() const
{
    return frameLifecycle_.disposition();
}

void SdlPlatform::completeFrame()
{
    frameLifecycle_.completeFrame();
}

bool SdlPlatform::consumeFrameClockReset()
{
    return frameLifecycle_.consumeFrameClockReset();
}

bool SdlPlatform::consumeGraphicsRebuildRequest()
{
    const bool requested = graphicsRebuildRequested_;
    graphicsRebuildRequested_ = false;
    return requested;
}

bool SdlPlatform::showWindowWhenReady()
{
    if (!window_) return false;
    if (!SDL_ShowWindow(window_)) {
        log(PlatformLogLevel::Error, std::string("Unable to show native window: ") + SDL_GetError());
        return false;
    }

    frameLifecycle_.setVisible(true);
    framePacer_.resetDeadline();
    refreshViewportMetrics();
    refreshDisplayTiming();
    graphicsRebuildRequested_ = true;
    return true;
}

SDL_Window* SdlPlatform::window() const noexcept { return window_; }

void SdlPlatform::handleKeyDown(RocketGameApp& app, const SDL_KeyboardEvent& event)
{
    if (event.key == SDLK_F11 || (event.key == SDLK_RETURN && (event.mod & SDL_KMOD_ALT) != 0)) {
        if (!event.repeat) setFullscreen(!fullscreen());
        return;
    }

    const InputContext context = app.inputContext();
    if (context == InputContext::Ui || context == InputContext::Paused) {
        if (event.key == SDLK_UP || event.key == SDLK_W) app.uiNavigate(UiDirection::Up);
        else if (event.key == SDLK_DOWN || event.key == SDLK_S || event.key == SDLK_TAB) app.uiNavigate(UiDirection::Down);
        else if (event.key == SDLK_LEFT || event.key == SDLK_A) app.uiNavigate(UiDirection::Left);
        else if (event.key == SDLK_RIGHT || event.key == SDLK_D) app.uiNavigate(UiDirection::Right);
        else if ((event.key == SDLK_RETURN || event.key == SDLK_SPACE) && !event.repeat) {
            // The launch Return shortcut is also a UI confirm key. A result
            // can appear while it is still held, so wait for a physical key-up
            // before allowing it to acknowledge Continue.
            if (!launchOutcomeConfirmReleaseGuard_) app.uiActivateFocused();
        }
        else if (event.key == SDLK_ESCAPE && !event.repeat) app.uiCancel();
        return;
    }

    if (event.repeat) return;
    switch (context) {
    case InputContext::Preflight:
        if (event.key == SDLK_SPACE || event.key == SDLK_RETURN) app.startLaunch();
        break;
    case InputContext::Launch:
        if (event.key == SDLK_R || event.key == SDLK_SPACE) {
            if (event.key == SDLK_SPACE) launchOutcomeConfirmReleaseGuard_ = true;
            app.returnHome();
        }
        else if (event.key == SDLK_E) app.ejectNow();
        else if (event.key == SDLK_C) app.cutEngines();
        else if (event.key == SDLK_V) app.pressureReliefValve();
        break;
    case InputContext::FlybyActive:
        if (event.key == SDLK_ESCAPE) app.flybyAbort();
        break;
    case InputContext::FlybyComplete:
        if (event.key == SDLK_SPACE || event.key == SDLK_RETURN) app.flybyContinue();
        break;
    case InputContext::OrbitActive:
        if (event.key == SDLK_ESCAPE) app.orbitAbort();
        break;
    case InputContext::OrbitComplete:
        if (event.key == SDLK_SPACE || event.key == SDLK_RETURN) app.orbitContinue();
        break;
    case InputContext::SurfaceScan:
        if (event.key == SDLK_SPACE) app.scanSurfacePulse();
        else if (event.key == SDLK_X) app.scanSurfaceBank();
        else if (event.key == SDLK_ESCAPE) app.scanSurfaceAbort();
        break;
    case InputContext::SurfacePush:
        if (event.key == SDLK_SPACE) app.pushSurfaceStep();
        else if (event.key == SDLK_X) app.pushSurfaceBank();
        else if (event.key == SDLK_ESCAPE) app.pushSurfaceAbort();
        break;
    case InputContext::MiningActive:
    case InputContext::MiningService:
        if (event.key == SDLK_E) app.miningScanner();
        else if (event.key == SDLK_T) app.miningTether();
        else if (event.key == SDLK_R) app.miningStow();
        else if (event.key == SDLK_ESCAPE) app.miningAbort();
        break;
    case InputContext::MiningFailure:
        if (event.key == SDLK_RETURN || event.key == SDLK_SPACE) app.miningFailureAck();
        break;
    case InputContext::Stamp:
        if (event.key == SDLK_RETURN || event.key == SDLK_SPACE) app.next();
        break;
    case InputContext::Ui:
    case InputContext::Paused:
        break;
    }
}

void SdlPlatform::releaseRealtimeInputs(RocketGameApp& app)
{
    app.flybyMove(0.0, 0.0);
    app.orbitMove(0.0, 0.0);
    app.miningMove(0.0, 0.0);
    app.miningKeyboardDrill(false);
    app.miningDrill(false);
    // Focus loss cannot reliably deliver a key-up event. There is no
    // carry-over confirmation once the window is no longer receiving input.
    launchOutcomeConfirmReleaseGuard_ = false;
}

void SdlPlatform::setWindowVisible(RocketGameApp& app, bool visible)
{
    if (visible == frameLifecycle_.visible()) {
        if (!visible) releaseRealtimeInputs(app);
        return;
    }

    frameLifecycle_.setVisible(visible);
    framePacer_.resetDeadline();
    if (!visible) releaseRealtimeInputs(app);
}

void SdlPlatform::refreshViewportMetrics()
{
    if (!window_) return;

    int logicalWidth = viewportMetrics_.logicalWidth;
    int logicalHeight = viewportMetrics_.logicalHeight;
    int drawableWidth = viewportMetrics_.drawableWidth;
    int drawableHeight = viewportMetrics_.drawableHeight;
    if (!SDL_GetWindowSize(window_, &logicalWidth, &logicalHeight)) {
        log(PlatformLogLevel::Warning, std::string("Unable to query logical window size: ") + SDL_GetError());
    }
    if (!SDL_GetWindowSizeInPixels(window_, &drawableWidth, &drawableHeight)) {
        log(PlatformLogLevel::Warning, std::string("Unable to query drawable window size: ") + SDL_GetError());
    }

    logicalWidth = std::max(1, logicalWidth);
    logicalHeight = std::max(1, logicalHeight);
    drawableWidth = std::max(1, drawableWidth);
    drawableHeight = std::max(1, drawableHeight);
    viewportMetrics_ = {
        logicalWidth,
        logicalHeight,
        drawableWidth,
        drawableHeight,
        static_cast<float>(drawableWidth) / static_cast<float>(logicalWidth),
        -1.0F
    };
}

void SdlPlatform::refreshDisplayTiming()
{
    if (!window_) return;

    displayId_ = SDL_GetDisplayForWindow(window_);
    const SDL_DisplayMode* mode = displayId_ != 0 ? SDL_GetCurrentDisplayMode(displayId_) : nullptr;
    if (!mode && displayId_ != 0) mode = SDL_GetDesktopDisplayMode(displayId_);

    double refreshRate = 0.0;
    if (mode) {
        if (mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0) {
            refreshRate = static_cast<double>(mode->refresh_rate_numerator)
                / static_cast<double>(mode->refresh_rate_denominator);
        } else {
            refreshRate = static_cast<double>(mode->refresh_rate);
        }
    }

    displayRefreshRateHz_ = refreshRate;
    framePacer_.configure(diagnostics_.verticalSyncActive, refreshRate);
    diagnostics_.softwareFrameLimiterActive = framePacer_.active();
}

void SdlPlatform::applySoftwareFramePacing()
{
    diagnostics_.frameLimiterMilliseconds = 0.0;
    if (!framePacer_.active() || !frameLifecycle_.visible()) return;

    const Uint64 limiterStarted = SDL_GetTicksNS();
    const NativeFramePacingDecision decision = framePacer_.next(limiterStarted);
    if (decision.delayNanoseconds > 0) {
        SDL_DelayPrecise(decision.delayNanoseconds);
    }
    diagnostics_.frameLimiterMilliseconds =
        static_cast<double>(SDL_GetTicksNS() - limiterStarted) / 1'000'000.0;
}

double SdlPlatform::monotonicSeconds() const { return static_cast<double>(SDL_GetTicksNS()) / 1'000'000'000.0; }
double SdlPlatform::displayRefreshRateHz() const { return displayRefreshRateHz_; }
ViewportMetrics SdlPlatform::viewportMetrics()
{
    return viewportMetrics_;
}
bool SdlPlatform::focused() const { return frameLifecycle_.focused(); }
bool SdlPlatform::visible() const { return frameLifecycle_.visible(); }
bool SdlPlatform::fullscreenAvailable() const { return window_ != nullptr; }
bool SdlPlatform::fullscreen() const { return fullscreen_; }
bool SdlPlatform::setFullscreen(bool enabled)
{
    if (!window_ || !SDL_SetWindowFullscreen(window_, enabled)) {
        log(PlatformLogLevel::Error, std::string("Fullscreen change failed: ") + SDL_GetError());
        return false;
    }
    fullscreen_ = enabled;
    graphicsRebuildRequested_ = true;
    AppPreferences preferences = preferences_.load();
    preferences.fullscreen = enabled;
    if (!preferences_.store(preferences)) log(PlatformLogLevel::Warning, preferences_.lastError());
    return true;
}
void SdlPlatform::log(PlatformLogLevel level, std::string_view message)
{
    const std::string copy(message);
    if (level == PlatformLogLevel::Error) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", copy.c_str());
    else if (level == PlatformLogLevel::Warning) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", copy.c_str());
    else SDL_Log("%s", copy.c_str());
}
bool SdlPlatform::haptic(double duration, double weak, double strong)
{
    const int activeIndex = controllerTracker_.activeControllerIndex();
    auto found = gamepads_.find(static_cast<SDL_JoystickID>(activeIndex));
    if (found == gamepads_.end()) return false;
    return SDL_RumbleGamepad(found->second.handle,
        static_cast<Uint16>(std::clamp(weak, 0.0, 1.0) * 65535.0),
        static_cast<Uint16>(std::clamp(strong, 0.0, 1.0) * 65535.0),
        static_cast<Uint32>(std::clamp(duration, 0.0, 2.0) * 1000.0));
}
void SdlPlatform::paceFrame()
{
}
PlatformDiagnostics SdlPlatform::diagnostics() const { return diagnostics_; }

ControllerFrame SdlPlatform::sampleFrame(double realTimeSeconds)
{
    if (benchmarkMode_) {
        lastControllerFrame_ = {};
        lastControllerFrame_.pageVisible = frameLifecycle_.visible();
        lastControllerFrame_.browserFocused = true;
        return lastControllerFrame_;
    }
    for (RawControllerSnapshot& snapshot : controllerSnapshots_) {
        const auto found = gamepads_.find(static_cast<SDL_JoystickID>(snapshot.index));
        if (found == gamepads_.end() || !found->second.handle) {
            snapshot.connected = false;
            continue;
        }
        SDL_Gamepad* gamepad = found->second.handle;
        snapshot.connected = SDL_GamepadConnected(gamepad);
        snapshot.timestamp = realTimeSeconds;
        if (!snapshot.connected) continue;
        snapshot.leftX = normalizedAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX));
        snapshot.leftY = normalizedAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY));
        snapshot.rightX = normalizedAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
        snapshot.rightY = normalizedAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY));
        setButton(snapshot, ControllerButton::South, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH));
        setButton(snapshot, ControllerButton::East, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST));
        setButton(snapshot, ControllerButton::West, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST));
        setButton(snapshot, ControllerButton::North, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH));
        setButton(snapshot, ControllerButton::LeftBumper, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        setButton(snapshot, ControllerButton::RightBumper, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        snapshot.buttons[static_cast<std::size_t>(ControllerButton::LeftTrigger)] = normalizedTrigger(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
        snapshot.buttons[static_cast<std::size_t>(ControllerButton::RightTrigger)] = normalizedTrigger(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        setButton(snapshot, ControllerButton::View, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK));
        setButton(snapshot, ControllerButton::Menu, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START));
        setButton(snapshot, ControllerButton::LeftStick, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK));
        setButton(snapshot, ControllerButton::RightStick, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK));
        setButton(snapshot, ControllerButton::DpadUp, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP));
        setButton(snapshot, ControllerButton::DpadDown, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN));
        setButton(snapshot, ControllerButton::DpadLeft, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT));
        setButton(snapshot, ControllerButton::DpadRight, SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    }
    lastControllerFrame_ = controllerTracker_.update(
        controllerSnapshots_,
        realTimeSeconds,
        controllerPreferences_);
    lastControllerFrame_.pageVisible = frameLifecycle_.visible();
    lastControllerFrame_.browserFocused = frameLifecycle_.focused();
    sourceArbiter_.noteActivity(InputSource::Controller,
        lastControllerFrame_.activityTimestamp > 0.0 ? lastControllerFrame_.activityTimestamp : realTimeSeconds,
        lastControllerFrame_.meaningfulActivity);
    return lastControllerFrame_;
}

void SdlPlatform::setPreferences(const ControllerPreferences& preferences)
{
    controllerPreferences_ = preferences;
}

InputSource SdlPlatform::activeSource() const { return sourceArbiter_.activeSource(); }
void SdlPlatform::reset()
{
    controllerTracker_.reset();
    sourceArbiter_.reset();
    lastControllerFrame_ = {};
}

bool SdlPlatform::openGamepad(SDL_JoystickID id)
{
    if (gamepads_.contains(id)) return true;
    SDL_Gamepad* gamepad = SDL_OpenGamepad(id);
    if (!gamepad) {
        log(PlatformLogLevel::Warning, std::string("Unable to open gamepad: ") + SDL_GetError());
        return false;
    }
    const char* gamepadName = SDL_GetGamepadName(gamepad);
    OpenGamepad openGamepad;
    openGamepad.handle = gamepad;
    openGamepad.name = gamepadName ? gamepadName : "SDL Gamepad";
    openGamepad.family = controllerFamilyFromId(openGamepad.name);
    gamepads_.emplace(id, std::move(openGamepad));
    rebuildControllerSnapshots();
    return true;
}

void SdlPlatform::closeGamepad(SDL_JoystickID id)
{
    const auto found = gamepads_.find(id);
    if (found == gamepads_.end()) return;
    SDL_CloseGamepad(found->second.handle);
    gamepads_.erase(found);
    rebuildControllerSnapshots();
}

void SdlPlatform::rebuildControllerSnapshots()
{
    controllerSnapshots_.clear();
    controllerSnapshots_.reserve(gamepads_.size());
    for (const auto& [id, gamepad] : gamepads_) {
        RawControllerSnapshot snapshot;
        snapshot.connected = gamepad.handle && SDL_GamepadConnected(gamepad.handle);
        snapshot.standardMapping = true;
        snapshot.index = static_cast<int>(id);
        snapshot.family = gamepad.family;
        snapshot.id = gamepad.name;
        controllerSnapshots_.push_back(std::move(snapshot));
    }
}

void SdlPlatform::noteKeyboardPointerActivity()
{
    sourceArbiter_.noteActivity(InputSource::KeyboardPointer, monotonicSeconds(), true);
}

} // namespace rocket
