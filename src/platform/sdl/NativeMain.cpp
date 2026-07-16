#include "game/GameRmlUi.h"
#include "game/GameRunner.h"
#include "platform/sdl/NativeStorage.h"
#include "platform/sdl/NativeTextureSource.h"
#include "platform/sdl/NativeUiBridge.h"
#include "platform/sdl/SdlPlatform.h"
#include "render/OpenGlRenderer.h"

#include <SDL3/SDL_main.h>
#include <exception>

int main(int, char**)
{
    try {
        const std::filesystem::path preferenceDirectory = rocket::SdlPlatform::preferenceDirectory();
        rocket::NativeSaveStore saves(preferenceDirectory);
        rocket::NativePreferenceStore preferences(preferenceDirectory);
        rocket::SdlPlatform platform(preferences);
        if (!platform.initialize()) return 1;

        rocket::NativeTextureSource textures(rocket::SdlPlatform::executableDirectory());
        rocket::NativeUiBridge uiBridge;
        rocket::OpenGlRenderer renderer(platform, preferences, textures);
        rocket::GameRmlUi ui(preferences, platform, uiBridge);
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

        bool running = true;
        while (running) {
            running = platform.processEvents(runner.app());
            platform.applyKeyboardState(runner.app());
            runner.frame();
        }

        runner.shutdown();
        platform.shutdown();
        return 0;
    } catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal startup error: %s", error.what());
        return 1;
    }
}
