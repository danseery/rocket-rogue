#include "platform/sdl/SdlPlatform.h"

#include "game/RocketGameApp.h"
#include "render/OpenGlApi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
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

SdlPlatform::SdlPlatform(IPreferenceStore& preferences) : preferences_(preferences) {}
SdlPlatform::~SdlPlatform() { shutdown(); }

bool SdlPlatform::initialize()
{
    if (initialized_) return true;
    SDL_SetAppMetadata("Rocket Rogue", "0.1.0", "game.rocketrogue.native");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        log(PlatformLogLevel::Error, std::string("SDL initialization failed: ") + SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    window_ = SDL_CreateWindow(
        "Rocket Rogue",
        1280,
        800,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        log(PlatformLogLevel::Error, std::string("Window creation failed: ") + SDL_GetError());
        shutdown();
        return false;
    }
    glContext_ = SDL_GL_CreateContext(window_);
    if (!glContext_ || !SDL_GL_MakeCurrent(window_, glContext_)) {
        log(PlatformLogLevel::Error, std::string("OpenGL 3.3 context creation failed: ") + SDL_GetError());
        shutdown();
        return false;
    }
    if (!loadDesktopOpenGl(reinterpret_cast<OpenGlProcLoader>(SDL_GL_GetProcAddress))) {
        log(PlatformLogLevel::Error, "Unable to load required OpenGL 3.3 Core functions.");
        shutdown();
        return false;
    }
    SDL_GL_SetSwapInterval(1);

    int gamepadCount = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&gamepadCount)) {
        for (int i = 0; i < gamepadCount; ++i) openGamepad(ids[i]);
        SDL_free(ids);
    }

    initialized_ = true;
    const AppPreferences preferences = preferences_.load();
    if (preferences.fullscreen) setFullscreen(true);
    return true;
}

void SdlPlatform::shutdown()
{
    if (!window_ && !glContext_ && !initialized_) return;
    for (auto& [id, gamepad] : gamepads_) {
        (void)id;
        if (gamepad) SDL_CloseGamepad(gamepad);
    }
    gamepads_.clear();
    if (glContext_) SDL_GL_DestroyContext(glContext_);
    glContext_ = nullptr;
    if (window_) SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_Quit();
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
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
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
            focused_ = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            focused_ = false;
            releaseRealtimeInputs(app);
            break;
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_RESTORED:
            visible_ = true;
            break;
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_OCCLUDED:
            visible_ = false;
            releaseRealtimeInputs(app);
            break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
            fullscreen_ = true;
            break;
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            fullscreen_ = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            noteKeyboardPointerActivity();
            handleKeyDown(app, event.key);
            break;
        case SDL_EVENT_KEY_UP:
            noteKeyboardPointerActivity();
            if (event.key.key == SDLK_SPACE) app.miningDrill(false);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            noteKeyboardPointerActivity();
            mouseX_ = event.motion.x;
            mouseY_ = event.motion.y;
            app.uiMouseMove(static_cast<int>(mouseX_), static_cast<int>(mouseY_));
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            noteKeyboardPointerActivity();
            mouseX_ = event.button.x; mouseY_ = event.button.y;
            const bool overUi = app.uiMouseDown(static_cast<int>(mouseX_), static_cast<int>(mouseY_), rmlMouseButton(event.button.button));
            if (!overUi && event.button.button == SDL_BUTTON_LEFT && app.inputContext() == InputContext::MiningActive) app.miningDrill(true);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
            noteKeyboardPointerActivity();
            mouseX_ = event.button.x; mouseY_ = event.button.y;
            app.uiMouseUp(static_cast<int>(mouseX_), static_cast<int>(mouseY_), rmlMouseButton(event.button.button));
            if (event.button.button == SDL_BUTTON_LEFT) app.miningDrill(false);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            noteKeyboardPointerActivity();
            mouseX_ = event.wheel.mouse_x; mouseY_ = event.wheel.mouse_y;
            app.uiMouseWheel(static_cast<int>(mouseX_), static_cast<int>(mouseY_), -event.wheel.y * 90.0);
            break;
        default:
            break;
        }
    }
    return true;
}

void SdlPlatform::applyKeyboardState(RocketGameApp& app)
{
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
        app.miningDrill(state && state[SDL_SCANCODE_SPACE]);
        break;
    default:
        break;
    }
}

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
        else if ((event.key == SDLK_RETURN || event.key == SDLK_SPACE) && !event.repeat) app.uiActivateFocused();
        else if (event.key == SDLK_ESCAPE && !event.repeat) app.uiCancel();
        return;
    }

    if (event.repeat) return;
    switch (context) {
    case InputContext::Preflight:
        if (event.key == SDLK_SPACE || event.key == SDLK_RETURN) app.startLaunch();
        break;
    case InputContext::Launch:
        if (event.key == SDLK_R || event.key == SDLK_SPACE) app.returnHome();
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
    app.miningDrill(false);
}

double SdlPlatform::monotonicSeconds() const { return static_cast<double>(SDL_GetTicksNS()) / 1'000'000'000.0; }
ViewportMetrics SdlPlatform::viewportMetrics()
{
    int width = 1280, height = 800, pixelsWidth = 1280, pixelsHeight = 800;
    if (window_) {
        SDL_GetWindowSize(window_, &width, &height);
        SDL_GetWindowSizeInPixels(window_, &pixelsWidth, &pixelsHeight);
    }
    return {std::max(1, width), std::max(1, height), std::max(1, pixelsWidth), std::max(1, pixelsHeight),
        static_cast<float>(pixelsWidth) / static_cast<float>(std::max(1, width)), -1.0F};
}
bool SdlPlatform::focused() const { return focused_; }
bool SdlPlatform::visible() const { return visible_; }
bool SdlPlatform::fullscreenAvailable() const { return window_ != nullptr; }
bool SdlPlatform::fullscreen() const { return fullscreen_; }
bool SdlPlatform::setFullscreen(bool enabled)
{
    if (!window_ || !SDL_SetWindowFullscreen(window_, enabled)) {
        log(PlatformLogLevel::Error, std::string("Fullscreen change failed: ") + SDL_GetError());
        return false;
    }
    fullscreen_ = enabled;
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
    return SDL_RumbleGamepad(found->second,
        static_cast<Uint16>(std::clamp(weak, 0.0, 1.0) * 65535.0),
        static_cast<Uint16>(std::clamp(strong, 0.0, 1.0) * 65535.0),
        static_cast<Uint32>(std::clamp(duration, 0.0, 2.0) * 1000.0));
}
void SdlPlatform::present() { if (window_) SDL_GL_SwapWindow(window_); }
OpenGlDialect SdlPlatform::openGlDialect() const { return OpenGlDialect::DesktopCore33; }

ControllerFrame SdlPlatform::sampleFrame(double realTimeSeconds)
{
    std::vector<RawControllerSnapshot> snapshots;
    snapshots.reserve(gamepads_.size());
    for (const auto& [id, gamepad] : gamepads_) {
        if (!gamepad || !SDL_GamepadConnected(gamepad)) continue;
        RawControllerSnapshot snapshot;
        snapshot.connected = true;
        snapshot.standardMapping = true;
        snapshot.index = static_cast<int>(id);
        snapshot.timestamp = realTimeSeconds;
        snapshot.id = SDL_GetGamepadName(gamepad) ? SDL_GetGamepadName(gamepad) : "SDL Gamepad";
        snapshot.family = controllerFamilyFromId(snapshot.id);
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
        snapshots.push_back(std::move(snapshot));
    }
    lastControllerFrame_ = controllerTracker_.update(snapshots, realTimeSeconds, preferences_.load().controller);
    lastControllerFrame_.pageVisible = visible_;
    lastControllerFrame_.browserFocused = focused_;
    sourceArbiter_.noteActivity(InputSource::Controller,
        lastControllerFrame_.activityTimestamp > 0.0 ? lastControllerFrame_.activityTimestamp : realTimeSeconds,
        lastControllerFrame_.meaningfulActivity);
    return lastControllerFrame_;
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
    gamepads_.emplace(id, gamepad);
    return true;
}

void SdlPlatform::closeGamepad(SDL_JoystickID id)
{
    const auto found = gamepads_.find(id);
    if (found == gamepads_.end()) return;
    SDL_CloseGamepad(found->second);
    gamepads_.erase(found);
}

void SdlPlatform::noteKeyboardPointerActivity()
{
    sourceArbiter_.noteActivity(InputSource::KeyboardPointer, monotonicSeconds(), true);
}

} // namespace rocket
