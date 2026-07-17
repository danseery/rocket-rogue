#include "platform/sdl/NativeStorage.h"
#include "platform/sdl/NativeTextureSource.h"
#include "platform/sdl/SdlPlatform.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace {

void nativeFrameLifecycleIsBoundedAndResetsOnResume()
{
    rocket::NativeFrameLifecycle lifecycle;
    assert(lifecycle.disposition() == rocket::NativeFrameDisposition::Active);
    assert(rocket::nativeFrameAcceptsRealtimeInput(lifecycle.disposition()));

    lifecycle.setFocused(false);
    assert(lifecycle.disposition() == rocket::NativeFrameDisposition::IdleTransition);
    assert(rocket::nativeFrameRenders(lifecycle.disposition()));
    assert(!rocket::nativeFrameWaitsForEvents(lifecycle.disposition()));
    lifecycle.completeFrame();
    assert(lifecycle.disposition() == rocket::NativeFrameDisposition::IdlePaused);
    assert(rocket::nativeFrameWaitsForEvents(lifecycle.disposition()));
    assert(rocket::nativeFrameRenders(lifecycle.disposition()));
    assert(!rocket::nativeFrameAcceptsRealtimeInput(lifecycle.disposition()));
    assert(rocket::nativeIdleEventWaitMilliseconds >= 100);

    lifecycle.setFocused(true);
    assert(lifecycle.disposition() == rocket::NativeFrameDisposition::Active);
    assert(lifecycle.consumeFrameClockReset());
    assert(!lifecycle.consumeFrameClockReset());

    lifecycle.setVisible(false);
    assert(lifecycle.disposition() == rocket::NativeFrameDisposition::SuspendTransition);
    assert(rocket::nativeFrameRenders(lifecycle.disposition()));
    lifecycle.completeFrame();
    assert(lifecycle.disposition() == rocket::NativeFrameDisposition::Suspended);
    assert(rocket::nativeFrameWaitsForEvents(lifecycle.disposition()));
    assert(!rocket::nativeFrameRenders(lifecycle.disposition()));

    lifecycle.setVisible(true);
    assert(lifecycle.disposition() == rocket::NativeFrameDisposition::Active);
    assert(lifecycle.consumeFrameClockReset());

    lifecycle.setFocused(false);
    lifecycle.completeFrame();
    lifecycle.setVisible(false);
    lifecycle.completeFrame();
    lifecycle.setVisible(true);
    assert(lifecycle.disposition() == rocket::NativeFrameDisposition::IdleTransition);
    assert(lifecycle.consumeFrameClockReset());
    lifecycle.completeFrame();
    assert(lifecycle.disposition() == rocket::NativeFrameDisposition::IdlePaused);
}

void swapFailureEnablesDisplayAwareFallbackPacing()
{
    rocket::NativeFramePacer pacer;
    pacer.configure(true, 60.0);
    assert(!pacer.active());
    assert(pacer.next(1'000'000'000ULL).delayNanoseconds == 0);

    pacer.configure(false, 144.0);
    assert(pacer.active());
    assert(pacer.intervalNanoseconds() >= 6'900'000ULL);
    assert(pacer.intervalNanoseconds() <= 7'000'000ULL);
    const std::uint64_t start = 1'000'000'000ULL;
    const rocket::NativeFramePacingDecision first = pacer.next(start);
    assert(first.delayNanoseconds == pacer.intervalNanoseconds());
    const rocket::NativeFramePacingDecision second = pacer.next(start + first.delayNanoseconds + 1'000'000ULL);
    assert(second.delayNanoseconds > 0);
    assert(second.delayNanoseconds < pacer.intervalNanoseconds());

    const std::uint64_t stalled = start + pacer.intervalNanoseconds() * 20ULL;
    assert(pacer.next(stalled).delayNanoseconds == 0);
    const rocket::NativeFramePacingDecision afterStall = pacer.next(stalled + 1'000'000ULL);
    assert(afterStall.delayNanoseconds > 0);
    assert(afterStall.delayNanoseconds <= pacer.intervalNanoseconds());

    pacer.configure(false, 0.0);
    assert(pacer.active());
    assert(pacer.intervalNanoseconds() == 0);
    assert(pacer.next(start).delayNanoseconds == 1'000'000ULL);
}

} // namespace

int main()
{
    nativeFrameLifecycleIsBoundedAndResetsOnResume();
    swapFailureEnablesDisplayAwareFallbackPacing();

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
    const std::uint64_t preferenceRevisionBeforeStore = preferences.revision();
    assert(preferences.store(expected));
    assert(preferences.revision() == preferenceRevisionBeforeStore + 1);
    (void)preferences.load();
    assert(preferences.revision() == preferenceRevisionBeforeStore + 1);

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
