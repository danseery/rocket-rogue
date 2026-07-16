#pragma once

#include "platform/AppServices.h"

#include <SDL3/SDL.h>
#include <filesystem>
#include <unordered_map>

namespace rocket {

class RocketGameApp;

class SdlPlatform final : public IPlatformHost, public IControllerSource {
public:
    explicit SdlPlatform(IPreferenceStore& preferences);
    ~SdlPlatform() override;

    bool initialize();
    void shutdown();
    bool processEvents(RocketGameApp& app);
    void applyKeyboardState(RocketGameApp& app);

    static std::filesystem::path preferenceDirectory();
    static std::filesystem::path executableDirectory();

    double monotonicSeconds() const override;
    ViewportMetrics viewportMetrics() override;
    bool focused() const override;
    bool visible() const override;
    bool fullscreenAvailable() const override;
    bool fullscreen() const override;
    bool setFullscreen(bool enabled) override;
    void log(PlatformLogLevel level, std::string_view message) override;
    bool haptic(double durationSeconds, double weakMagnitude, double strongMagnitude) override;
    void present() override;
    OpenGlDialect openGlDialect() const override;

    ControllerFrame sampleFrame(double realTimeSeconds) override;
    InputSource activeSource() const override;
    void reset() override;

private:
    bool openGamepad(SDL_JoystickID id);
    void closeGamepad(SDL_JoystickID id);
    void noteKeyboardPointerActivity();
    void releaseRealtimeInputs(RocketGameApp& app);
    void handleKeyDown(RocketGameApp& app, const SDL_KeyboardEvent& event);

    IPreferenceStore& preferences_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    std::unordered_map<SDL_JoystickID, SDL_Gamepad*> gamepads_;
    ControllerTracker controllerTracker_;
    InputSourceArbiter sourceArbiter_;
    ControllerFrame lastControllerFrame_;
    bool initialized_ = false;
    bool focused_ = true;
    bool visible_ = true;
    bool fullscreen_ = false;
    float mouseX_ = 0.0F;
    float mouseY_ = 0.0F;
};

} // namespace rocket
