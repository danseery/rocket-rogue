#include "platform/web/WebGamepadSource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string_view>
#include <vector>

#include <emscripten.h>
#include <emscripten/html5.h>

namespace rocket {
namespace {

enum class PreferenceField {
    PromptFamily,
    StickDeadzone,
    InvertFlightY,
    SwapConfirmCancel,
    VibrationEnabled
};

ControllerPromptFamily promptFamilyFromStorageValue(int value)
{
    switch (value) {
    case 1:
        return ControllerPromptFamily::Generic;
    case 2:
        return ControllerPromptFamily::Xbox;
    case 3:
        return ControllerPromptFamily::PlayStation;
    case 4:
        return ControllerPromptFamily::SteamDeck;
    case 0:
    default:
        return ControllerPromptFamily::Auto;
    }
}

int promptFamilyStorageValue(ControllerPromptFamily family)
{
    switch (family) {
    case ControllerPromptFamily::Generic:
        return 1;
    case ControllerPromptFamily::Xbox:
        return 2;
    case ControllerPromptFamily::PlayStation:
        return 3;
    case ControllerPromptFamily::SteamDeck:
        return 4;
    case ControllerPromptFamily::Auto:
    default:
        return 0;
    }
}

ControllerPreferences sanitizedPreferences(ControllerPreferences preferences)
{
    if (!std::isfinite(preferences.stickDeadzone)) {
        preferences.stickDeadzone = controller_tuning::defaultStickDeadzone;
    }
    preferences.stickDeadzone = std::clamp(
        preferences.stickDeadzone,
        controller_tuning::minimumStickDeadzone,
        controller_tuning::maximumStickDeadzone);
    return preferences;
}

std::string_view familyName(ControllerFamily family)
{
    switch (family) {
    case ControllerFamily::Xbox:
        return "xbox";
    case ControllerFamily::PlayStation:
        return "playstation";
    case ControllerFamily::SteamDeck:
        return "steamdeck";
    case ControllerFamily::Generic:
    default:
        return "generic";
    }
}

std::string_view promptFamilyName(ControllerPromptFamily family)
{
    switch (family) {
    case ControllerPromptFamily::Generic:
        return "generic";
    case ControllerPromptFamily::Xbox:
        return "xbox";
    case ControllerPromptFamily::PlayStation:
        return "playstation";
    case ControllerPromptFamily::SteamDeck:
        return "steamdeck";
    case ControllerPromptFamily::Auto:
    default:
        return "auto";
    }
}

std::string_view inputSourceName(InputSource source)
{
    switch (source) {
    case InputSource::KeyboardPointer:
        return "keyboard-pointer";
    case InputSource::Controller:
        return "controller";
    case InputSource::None:
    default:
        return "none";
    }
}

std::string jsonEscaped(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) >= 0x20) {
                escaped += character;
            }
            break;
        }
    }
    return escaped;
}

EM_JS(double, rr_controller_preference_value_js, (int field), {
    const defaults = {
        promptFamily: "auto",
        stickDeadzone: 0.20,
        invertFlightY: false,
        swapConfirmCancel: false,
        vibrationEnabled: true
    };
    let stored = {};
    try {
        const raw = globalThis.localStorage.getItem("rocket_rogue_controller_preferences_v1");
        if (raw) {
            const parsed = JSON.parse(raw);
            if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) {
                stored = parsed;
            }
        }
    } catch (error) {
        stored = {};
    }

    const promptNames = ["auto", "generic", "xbox", "playstation", "steamdeck"];
    const promptFamily = typeof stored.promptFamily === "string" && promptNames.includes(stored.promptFamily)
        ? stored.promptFamily
        : defaults.promptFamily;
    const stickDeadzone = typeof stored.stickDeadzone === "number" && Number.isFinite(stored.stickDeadzone)
        ? Math.min(0.35, Math.max(0.10, stored.stickDeadzone))
        : defaults.stickDeadzone;
    const invertFlightY = typeof stored.invertFlightY === "boolean"
        ? stored.invertFlightY
        : defaults.invertFlightY;
    const swapConfirmCancel = typeof stored.swapConfirmCancel === "boolean"
        ? stored.swapConfirmCancel
        : defaults.swapConfirmCancel;
    const vibrationEnabled = typeof stored.vibrationEnabled === "boolean"
        ? stored.vibrationEnabled
        : defaults.vibrationEnabled;

    switch (field) {
    case 0:
        return promptNames.indexOf(promptFamily);
    case 1:
        return stickDeadzone;
    case 2:
        return invertFlightY ? 1 : 0;
    case 3:
        return swapConfirmCancel ? 1 : 0;
    case 4:
        return vibrationEnabled ? 1 : 0;
    default:
        return 0;
    }
});

EM_JS(void, rr_store_controller_preferences_js,
    (int promptFamily, double stickDeadzone, int invertFlightY, int swapConfirmCancel, int vibrationEnabled), {
        const promptNames = ["auto", "generic", "xbox", "playstation", "steamdeck"];
        const value = {
            promptFamily: promptNames[promptFamily] || "auto",
            stickDeadzone: Math.min(0.35, Math.max(0.10, Number(stickDeadzone) || 0.20)),
            invertFlightY: Boolean(invertFlightY),
            swapConfirmCancel: Boolean(swapConfirmCancel),
            vibrationEnabled: Boolean(vibrationEnabled)
        };
        try {
            globalThis.localStorage.setItem(
                "rocket_rogue_controller_preferences_v1",
                JSON.stringify(value));
        } catch (error) {
            // Storage can be unavailable in privacy-restricted browser contexts.
        }
    });

EM_JS(int, rr_browser_environment_flags_js, (), {
    if (!globalThis.__rocketRogueBrowserEnvironment) {
        const state = {
            hiddenSinceSample: document.visibilityState !== "visible",
            blurredSinceSample: !document.hasFocus(),
            keyboardPointerSinceSample: false,
            keyboardPointerActivitySeconds: 0
        };
        document.addEventListener("visibilitychange", () => {
            if (document.visibilityState !== "visible") {
                state.hiddenSinceSample = true;
            }
        });
        globalThis.addEventListener("blur", () => {
            state.blurredSinceSample = true;
        });
        const noteKeyboardPointer = () => {
            state.keyboardPointerSinceSample = true;
            state.keyboardPointerActivitySeconds = performance.now() / 1000;
        };
        globalThis.addEventListener("keydown", (event) => {
            // Browser key repeat is held state, not fresh source activity. A
            // newly pressed controller must be able to take ownership while a
            // keyboard key remains down.
            if (!event.repeat) {
                noteKeyboardPointer();
            }
        }, true);
        // Match native input arbitration: deliberate pointer movement hands
        // presentation back to mouse/keyboard, so a stale controller focus
        // ring cannot appear to select a different control under the cursor.
        globalThis.addEventListener("pointermove", noteKeyboardPointer, {capture: true, passive: true});
        globalThis.addEventListener("pointerdown", noteKeyboardPointer, true);
        globalThis.addEventListener("wheel", noteKeyboardPointer, {capture: true, passive: true});
        globalThis.__rocketRogueBrowserEnvironment = state;
    }

    const state = globalThis.__rocketRogueBrowserEnvironment;
    const visible = document.visibilityState === "visible";
    const focused = document.hasFocus();
    const flags = (visible ? 1 : 0)
        | (focused ? 2 : 0)
        | (state.hiddenSinceSample ? 4 : 0)
        | (state.blurredSinceSample ? 8 : 0)
        | (state.keyboardPointerSinceSample ? 16 : 0);
    state.hiddenSinceSample = false;
    state.blurredSinceSample = false;
    state.keyboardPointerSinceSample = false;
    return flags;
});

EM_JS(double, rr_browser_keyboard_pointer_activity_seconds_js, (), {
    const state = globalThis.__rocketRogueBrowserEnvironment;
    return state && Number.isFinite(state.keyboardPointerActivitySeconds)
        ? state.keyboardPointerActivitySeconds
        : 0;
});

EM_JS(int, rr_play_gamepad_haptic_js,
    (int controllerIndex, double durationMilliseconds, double weakMagnitude, double strongMagnitude), {
        try {
            const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
            const gamepad = gamepads && gamepads[controllerIndex];
            if (!gamepad || !gamepad.connected) {
                return 0;
            }
            const duration = Math.max(0, Math.min(2000, durationMilliseconds));
            const weak = Math.max(0, Math.min(1, weakMagnitude));
            const strong = Math.max(0, Math.min(1, strongMagnitude));
            if (gamepad.vibrationActuator && typeof gamepad.vibrationActuator.playEffect === "function") {
                const effect = gamepad.vibrationActuator.playEffect("dual-rumble", {
                    startDelay: 0,
                    duration,
                    weakMagnitude: weak,
                    strongMagnitude: strong
                });
                if (effect && typeof effect.catch === "function") {
                    effect.catch(() => {});
                }
                return 1;
            }
            const actuator = gamepad.hapticActuators && gamepad.hapticActuators[0];
            if (actuator && typeof actuator.pulse === "function") {
                const pulse = actuator.pulse(Math.max(weak, strong), duration);
                if (pulse && typeof pulse.catch === "function") {
                    pulse.catch(() => {});
                }
                return 1;
            }
        } catch (error) {
            // Haptics are optional and browser support varies by controller.
        }
        return 0;
    });

std::vector<RawControllerSnapshot> sampleRawControllers()
{
    std::vector<RawControllerSnapshot> snapshots;
    if (emscripten_sample_gamepad_data() != EMSCRIPTEN_RESULT_SUCCESS) {
        return snapshots;
    }

    const int count = emscripten_get_num_gamepads();
    if (count <= 0) {
        return snapshots;
    }
    snapshots.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index) {
        EmscriptenGamepadEvent state {};
        if (emscripten_get_gamepad_status(index, &state) != EMSCRIPTEN_RESULT_SUCCESS || !state.connected) {
            continue;
        }

        RawControllerSnapshot snapshot;
        snapshot.connected = true;
        snapshot.standardMapping = std::string_view(state.mapping) == "standard";
        snapshot.index = state.index;
        snapshot.timestamp = state.timestamp / 1000.0;
        snapshot.id = state.id;
        snapshot.family = controllerFamilyFromId(snapshot.id);

        if (state.numAxes > 0) {
            snapshot.leftX = state.axis[0];
        }
        if (state.numAxes > 1) {
            snapshot.leftY = state.axis[1];
        }
        if (state.numAxes > 2) {
            snapshot.rightX = state.axis[2];
        }
        if (state.numAxes > 3) {
            snapshot.rightY = state.axis[3];
        }

        const std::size_t buttonCount = std::min(
            controllerButtonCount,
            static_cast<std::size_t>(std::max(0, state.numButtons)));
        for (std::size_t button = 0; button < buttonCount; ++button) {
            snapshot.buttons[button] = std::clamp(
                std::max(state.analogButton[button], state.digitalButton[button] ? 1.0 : 0.0),
                0.0,
                1.0);
        }
        snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}

} // namespace

ControllerPreferences loadWebControllerPreferences()
{
    ControllerPreferences preferences;
    preferences.promptFamily = promptFamilyFromStorageValue(static_cast<int>(
        rr_controller_preference_value_js(static_cast<int>(PreferenceField::PromptFamily))));
    preferences.stickDeadzone = rr_controller_preference_value_js(
        static_cast<int>(PreferenceField::StickDeadzone));
    preferences.invertFlightY = rr_controller_preference_value_js(
        static_cast<int>(PreferenceField::InvertFlightY)) != 0.0;
    preferences.swapConfirmCancel = rr_controller_preference_value_js(
        static_cast<int>(PreferenceField::SwapConfirmCancel)) != 0.0;
    preferences.vibrationEnabled = rr_controller_preference_value_js(
        static_cast<int>(PreferenceField::VibrationEnabled)) != 0.0;
    return sanitizedPreferences(preferences);
}

void storeWebControllerPreferences(const ControllerPreferences& preferences)
{
    const ControllerPreferences sanitized = sanitizedPreferences(preferences);
    rr_store_controller_preferences_js(
        promptFamilyStorageValue(sanitized.promptFamily),
        sanitized.stickDeadzone,
        sanitized.invertFlightY ? 1 : 0,
        sanitized.swapConfirmCancel ? 1 : 0,
        sanitized.vibrationEnabled ? 1 : 0);
}

WebGamepadSource::WebGamepadSource()
    : preferences_(loadWebControllerPreferences())
{
}

ControllerFrame WebGamepadSource::sampleFrame(double realTimeSeconds)
{
    std::vector<RawControllerSnapshot> snapshots = sampleRawControllers();
    ControllerFrame frame = tracker_.update(snapshots, realTimeSeconds, preferences_);
    const int environmentFlags = rr_browser_environment_flags_js();
    const bool hiddenSinceLastSample = (environmentFlags & 4) != 0;
    const bool blurredSinceLastSample = (environmentFlags & 8) != 0;
    frame.pageVisible = (environmentFlags & 1) != 0 && !hiddenSinceLastSample;
    frame.browserFocused = (environmentFlags & 2) != 0 && !blurredSinceLastSample;
    const bool keyboardPointerActivity = (environmentFlags & 16) != 0;
    const double keyboardPointerActivitySeconds = rr_browser_keyboard_pointer_activity_seconds_js();
    sourceArbiter_.noteActivity(
        InputSource::KeyboardPointer,
        keyboardPointerActivitySeconds > 0.0 ? keyboardPointerActivitySeconds : realTimeSeconds,
        keyboardPointerActivity);
    // Only fresh physical activity may claim the live input source; held
    // controller state is still routed without dominating newer UI activity.
    sourceArbiter_.noteActivity(
        InputSource::Controller,
        frame.activityTimestamp > 0.0 ? frame.activityTimestamp : realTimeSeconds,
        !frame.synthetic && frame.meaningfulActivity);

    // Controller Lab input is sampled through an entirely separate tracker.
    // It can exercise edges, holds, and repeats without ever replacing the
    // physical frame that drives campaign input or its release cleanup.
    if (syntheticSnapshot_) {
        syntheticSnapshot_->timestamp = realTimeSeconds;
        const std::array<RawControllerSnapshot, 1> syntheticSnapshots {*syntheticSnapshot_};
        syntheticPreviewFrame_ = syntheticTracker_.update(
            syntheticSnapshots,
            realTimeSeconds,
            preferences_);
    } else if (syntheticTracker_.activeControllerIndex() >= 0) {
        syntheticPreviewFrame_ = syntheticTracker_.update(
            std::span<const RawControllerSnapshot> {},
            realTimeSeconds,
            preferences_);
    } else {
        syntheticPreviewFrame_.reset();
    }
    if (syntheticPreviewFrame_) {
        syntheticPreviewFrame_->pageVisible = frame.pageVisible;
        syntheticPreviewFrame_->browserFocused = frame.browserFocused;
    }
    lastFrame_ = frame;
    return lastFrame_;
}

void WebGamepadSource::setPreferences(const ControllerPreferences& preferences)
{
    preferences_ = preferences;
}

std::optional<ControllerFrame> WebGamepadSource::syntheticPreviewFrame() const
{
    return syntheticPreviewFrame_;
}

InputSource WebGamepadSource::activeSource() const
{
    return sourceArbiter_.activeSource();
}

std::string WebGamepadSource::debugStatusJson() const
{
    const ControllerFrame& debugFrame = syntheticPreviewFrame_
        ? *syntheticPreviewFrame_
        : lastFrame_;
    std::ostringstream stream;
    stream << "{\"connected\":" << (debugFrame.connected ? "true" : "false")
           << ",\"index\":" << debugFrame.controllerIndex
           << ",\"family\":\"" << familyName(debugFrame.family) << "\""
           << ",\"id\":\"" << jsonEscaped(debugFrame.id) << "\""
           << ",\"leftX\":" << debugFrame.leftX
           << ",\"leftY\":" << debugFrame.leftY
           << ",\"rightX\":" << debugFrame.rightX
           << ",\"rightY\":" << debugFrame.rightY
           << ",\"pageVisible\":" << (debugFrame.pageVisible ? "true" : "false")
           << ",\"browserFocused\":" << (debugFrame.browserFocused ? "true" : "false")
           << ",\"meaningfulInput\":" << (debugFrame.meaningfulInput ? "true" : "false")
           << ",\"meaningfulActivity\":" << (debugFrame.meaningfulActivity ? "true" : "false")
           << ",\"synthetic\":" << (debugFrame.synthetic ? "true" : "false")
           << ",\"physicalConnected\":" << (lastFrame_.connected ? "true" : "false")
           << ",\"activeSource\":\"" << inputSourceName(sourceArbiter_.activeSource()) << "\""
           << ",\"promptFamily\":\"" << promptFamilyName(preferences_.promptFamily) << "\""
           << ",\"buttons\":[";
    for (std::size_t index = 0; index < controllerButtonCount; ++index) {
        if (index > 0) {
            stream << ',';
        }
        stream << (debugFrame.down.test(index) ? "true" : "false");
    }
    stream << "],\"pressed\":[";
    for (std::size_t index = 0; index < controllerButtonCount; ++index) {
        if (index > 0) {
            stream << ',';
        }
        stream << (debugFrame.pressed.test(index) ? "true" : "false");
    }
    stream << "]}";
    return stream.str();
}

void WebGamepadSource::reloadPreferences()
{
    preferences_ = loadWebControllerPreferences();
}

bool WebGamepadSource::playHaptic(double durationSeconds, double weakMagnitude, double strongMagnitude) const
{
    if (!preferences_.vibrationEnabled || tracker_.activeControllerIndex() < 0) {
        return false;
    }
    return rr_play_gamepad_haptic_js(
        tracker_.activeControllerIndex(),
        std::clamp(durationSeconds, 0.0, 2.0) * 1000.0,
        std::clamp(weakMagnitude, 0.0, 1.0),
        std::clamp(strongMagnitude, 0.0, 1.0)) != 0;
}

void WebGamepadSource::setSyntheticSnapshot(const RawControllerSnapshot& snapshot)
{
    syntheticSnapshot_ = snapshot;
    syntheticSnapshot_->connected = true;
    syntheticSnapshot_->standardMapping = true;
    syntheticSnapshot_->synthetic = true;
    syntheticSnapshot_->index = 255;
    if (syntheticSnapshot_->id.empty()) {
        syntheticSnapshot_->id = "Controller Lab Synthetic Gamepad";
    }
}

void WebGamepadSource::clearSyntheticSnapshot()
{
    syntheticSnapshot_.reset();
}

void WebGamepadSource::reset()
{
    tracker_.reset();
    syntheticTracker_.reset();
    sourceArbiter_.reset();
    lastFrame_ = {};
    syntheticPreviewFrame_.reset();
    syntheticSnapshot_.reset();
}

} // namespace rocket
