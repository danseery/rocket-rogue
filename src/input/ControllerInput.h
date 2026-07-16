#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rocket {

enum class InputSource {
    None,
    KeyboardPointer,
    Controller
};

enum class ControllerFamily {
    Generic,
    Xbox,
    PlayStation,
    SteamDeck
};

enum class ControllerPromptFamily {
    Auto,
    Generic,
    Xbox,
    PlayStation,
    SteamDeck
};

// Values intentionally match the W3C Standard Gamepad button order.
enum class ControllerButton : std::size_t {
    South = 0,
    East,
    West,
    North,
    LeftBumper,
    RightBumper,
    LeftTrigger,
    RightTrigger,
    View,
    Menu,
    LeftStick,
    RightStick,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count
};

inline constexpr std::size_t controllerButtonCount = static_cast<std::size_t>(ControllerButton::Count);

enum class UiDirection : std::size_t {
    Up = 0,
    Down,
    Left,
    Right,
    Count
};

enum class InputContext {
    Ui,
    Preflight,
    Launch,
    FlybyActive,
    FlybyComplete,
    OrbitActive,
    OrbitComplete,
    SurfaceScan,
    SurfacePush,
    MiningActive,
    MiningService,
    MiningFailure,
    Stamp,
    Paused
};

enum class PauseReason {
    None,
    SystemMenu,
    BlockingModal,
    ControllerDisconnected,
    PageHidden,
    ControllerUiFocus
};

struct ControllerPreferences {
    ControllerPromptFamily promptFamily = ControllerPromptFamily::Auto;
    double stickDeadzone = 0.20;
    bool invertFlightY = false;
    bool swapConfirmCancel = false;
    bool vibrationEnabled = true;
};

struct RawControllerSnapshot {
    bool connected = false;
    bool standardMapping = false;
    bool synthetic = false;
    int index = -1;
    double timestamp = 0.0;
    ControllerFamily family = ControllerFamily::Generic;
    std::string id;
    double leftX = 0.0;
    double leftY = 0.0;
    double rightX = 0.0;
    double rightY = 0.0;
    std::array<double, controllerButtonCount> buttons {};
};

struct ControllerFrame {
    bool pageVisible = true;
    bool browserFocused = true;
    bool synthetic = false;
    bool connected = false;
    bool justConnected = false;
    bool justDisconnected = false;
    int controllerIndex = -1;
    ControllerFamily family = ControllerFamily::Generic;
    std::string id;
    double leftX = 0.0;
    double leftY = 0.0;
    double rightX = 0.0;
    double rightY = 0.0;
    std::bitset<controllerButtonCount> down;
    std::bitset<controllerButtonCount> pressed;
    std::bitset<controllerButtonCount> released;
    std::array<double, controllerButtonCount> heldSeconds {};
    std::optional<UiDirection> navigation;
    bool navigationRepeated = false;
    bool meaningfulInput = false;
    // True only when this frame contains fresh controller activity. A held
    // button or stick remains meaningful input without repeatedly claiming
    // the active input source.
    bool meaningfulActivity = false;
    double activityTimestamp = 0.0;

    bool isDown(ControllerButton button) const;
    bool wasPressed(ControllerButton button) const;
    bool wasReleased(ControllerButton button) const;
    double heldFor(ControllerButton button) const;
};

namespace controller_tuning {
inline constexpr double defaultStickDeadzone = 0.20;
inline constexpr double minimumStickDeadzone = 0.10;
inline constexpr double maximumStickDeadzone = 0.35;
inline constexpr double menuEngageThreshold = 0.55;
inline constexpr double menuReleaseThreshold = 0.35;
inline constexpr double menuInitialRepeatSeconds = 0.350;
inline constexpr double menuRepeatSeconds = 0.120;
inline constexpr double triggerPressThreshold = 0.35;
inline constexpr double triggerReleaseThreshold = 0.20;
inline constexpr double digitalButtonThreshold = 0.50;
} // namespace controller_tuning

ControllerFamily controllerFamilyFromId(std::string_view id);
ControllerFamily resolvedPromptFamily(const ControllerPreferences& preferences, ControllerFamily detectedFamily);
std::array<double, 2> applyRadialDeadzone(double x, double y, double deadzone);
bool meaningfulControllerInput(const RawControllerSnapshot& snapshot, double deadzone);
bool controllerResumeBlocked(PauseReason reason, bool controllerConnected, bool neutralRequired);
bool controllerPauseStopsSimulation(PauseReason reason, InputContext gameplayContext, bool modalOpen);
InputContext resolvedControllerInputContext(InputContext gameplayContext, PauseReason reason, bool modalOpen);

class InputSourceArbiter {
public:
    void noteActivity(InputSource source, double realTimeSeconds, bool meaningful = true);
    InputSource activeSource() const;
    bool shouldApply(InputSource source) const;
    void reset();

private:
    InputSource activeSource_ = InputSource::None;
    double lastActivitySeconds_ = 0.0;
    bool hasActivity_ = false;
};

class ControllerTracker {
public:
    ControllerFrame update(
        std::span<const RawControllerSnapshot> snapshots,
        double realTimeSeconds,
        const ControllerPreferences& preferences = {});
    void reset();
    int activeControllerIndex() const;

private:
    const RawControllerSnapshot* selectActive(std::span<const RawControllerSnapshot> snapshots, double deadzone) const;
    std::optional<UiDirection> navigationDirection(const RawControllerSnapshot& snapshot);

    int activeControllerIndex_ = -1;
    bool connected_ = false;
    bool activeControllerSynthetic_ = false;
    std::bitset<controllerButtonCount> down_;
    std::array<double, controllerButtonCount> downSinceSeconds_ {};
    std::optional<UiDirection> navigationDirection_;
    double navigationNextRepeatSeconds_ = 0.0;
    double lastLeftX_ = 0.0;
    double lastLeftY_ = 0.0;
    double lastRightX_ = 0.0;
    double lastRightY_ = 0.0;
    bool requiresFreshPhysicalClaim_ = false;
    bool physicalClaimArmed_ = false;
};

} // namespace rocket
