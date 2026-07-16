#include "platform/sdl/NativeStorage.h"
#include "platform/sdl/NativeTextureSource.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

int main()
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("rocket-rogue-native-tests-" + std::to_string(suffix));
    std::filesystem::create_directories(root);

    rocket::NativeSaveStore saves(root);
    assert(saves.load().empty());
    assert(saves.storeAtomic("first"));
    assert(saves.load() == "first");
    std::filesystem::create_directory(saves.path().string() + ".tmp");
    assert(!saves.storeAtomic("second"));
    assert(saves.load() == "first");
    std::filesystem::remove_all(saves.path().string() + ".tmp");

    rocket::NativePreferenceStore preferences(root);
    rocket::AppPreferences expected;
    expected.controller.promptFamily = rocket::ControllerPromptFamily::PlayStation;
    expected.controller.stickDeadzone = 0.30;
    expected.controller.invertFlightY = true;
    expected.controller.swapConfirmCancel = true;
    expected.controller.vibrationEnabled = false;
    expected.resolutionPreset = "2560x1440";
    expected.gameSpeed = 1.5;
    expected.debugToolsEnabled = true;
    expected.helpDisabled = true;
    expected.cameraShakeDisabled = true;
    expected.fullscreen = true;
    expected.dismissedHelpTopics = {"surface", "mining"};
    assert(preferences.store(expected));

    rocket::NativePreferenceStore reloaded(root);
    const rocket::AppPreferences actual = reloaded.load();
    assert(actual.controller.promptFamily == expected.controller.promptFamily);
    assert(actual.controller.stickDeadzone == expected.controller.stickDeadzone);
    assert(actual.controller.invertFlightY);
    assert(actual.controller.swapConfirmCancel);
    assert(!actual.controller.vibrationEnabled);
    assert(actual.resolutionPreset == expected.resolutionPreset);
    assert(actual.gameSpeed == expected.gameSpeed);
    assert(actual.debugToolsEnabled && actual.helpDisabled && actual.cameraShakeDisabled && actual.fullscreen);
    assert(actual.dismissedHelpTopics == expected.dismissedHelpTopics);

    rocket::NativeTextureSource textures(root);
    textures.request("missing", "assets/art/does-not-exist.png");
    assert(textures.status("missing") == rocket::TextureStatus::Failed);
    assert(textures.lastError().find("missing or corrupt") != std::string::npos);

    std::filesystem::create_directories(root / "assets/art");
    {
        std::ofstream corrupt(root / "assets/art/corrupt.png", std::ios::binary);
        corrupt << "not a png";
    }
    textures.request("corrupt", "assets/art/corrupt.png");
    assert(textures.status("corrupt") == rocket::TextureStatus::Failed);

    std::filesystem::remove_all(root);
    return 0;
}
