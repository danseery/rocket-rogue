#include "input/ControllerInput.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>

namespace rocket {
namespace {

std::size_t buttonIndex(ControllerButton button)
{
    return static_cast<std::size_t>(button);
}

std::string lowerCopy(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

bool triggerButton(ControllerButton button)
{
    return button == ControllerButton::LeftTrigger || button == ControllerButton::RightTrigger;
}

int sourceTiePriority(InputSource source)
{
    // Simultaneous timestamps are inherently unordered. Prefer direct UI
    // activity so the result is deterministic and independent of polling call
    // order instead of letting the controller sample overwrite a DOM event.
    switch (source) {
    case InputSource::KeyboardPointer:
        return 2;
    case InputSource::Controller:
        return 1;
    case InputSource::None:
    default:
        return 0;
    }
}

} // namespace

bool ControllerFrame::isDown(ControllerButton button) const
{
    return down.test(buttonIndex(button));
}

bool ControllerFrame::wasPressed(ControllerButton button) const
{
    return pressed.test(buttonIndex(button));
}

bool ControllerFrame::wasReleased(ControllerButton button) const
{
    return released.test(buttonIndex(button));
}

double ControllerFrame::heldFor(ControllerButton button) const
{
    return heldSeconds[buttonIndex(button)];
}

ControllerFamily controllerFamilyFromId(std::string_view id)
{
    const std::string lowered = lowerCopy(id);
    if (lowered.find("steam deck") != std::string::npos || lowered.find("valve") != std::string::npos || lowered.find("28de") != std::string::npos) {
        return ControllerFamily::SteamDeck;
    }
    if (lowered.find("playstation") != std::string::npos || lowered.find("dualshock") != std::string::npos || lowered.find("dualsense") != std::string::npos || lowered.find("sony") != std::string::npos || lowered.find("054c") != std::string::npos) {
        return ControllerFamily::PlayStation;
    }
    if (lowered.find("xbox") != std::string::npos || lowered.find("xinput") != std::string::npos || lowered.find("microsoft") != std::string::npos || lowered.find("045e") != std::string::npos) {
        return ControllerFamily::Xbox;
    }
    return ControllerFamily::Generic;
}

ControllerFamily resolvedPromptFamily(const ControllerPreferences& preferences, ControllerFamily detectedFamily)
{
    switch (preferences.promptFamily) {
    case ControllerPromptFamily::Generic:
        return ControllerFamily::Generic;
    case ControllerPromptFamily::Xbox:
        return ControllerFamily::Xbox;
    case ControllerPromptFamily::PlayStation:
        return ControllerFamily::PlayStation;
    case ControllerPromptFamily::SteamDeck:
        return ControllerFamily::SteamDeck;
    case ControllerPromptFamily::Auto:
    default:
        return detectedFamily;
    }
}

std::array<double, 2> applyRadialDeadzone(double x, double y, double deadzone)
{
    const double normalizedDeadzone = std::clamp(
        deadzone,
        controller_tuning::minimumStickDeadzone,
        controller_tuning::maximumStickDeadzone);
    const double magnitude = std::hypot(x, y);
    if (magnitude <= normalizedDeadzone || magnitude <= std::numeric_limits<double>::epsilon()) {
        return {0.0, 0.0};
    }
    const double clampedMagnitude = std::min(1.0, magnitude);
    const double scaledMagnitude = (clampedMagnitude - normalizedDeadzone) / (1.0 - normalizedDeadzone);
    return {
        std::clamp(x / magnitude * scaledMagnitude, -1.0, 1.0),
        std::clamp(y / magnitude * scaledMagnitude, -1.0, 1.0)
    };
}

bool meaningfulControllerInput(const RawControllerSnapshot& snapshot, double deadzone)
{
    if (!snapshot.connected || !snapshot.standardMapping) {
        return false;
    }
    if (std::hypot(snapshot.leftX, snapshot.leftY) > deadzone || std::hypot(snapshot.rightX, snapshot.rightY) > deadzone) {
        return true;
    }
    return std::any_of(snapshot.buttons.begin(), snapshot.buttons.end(), [](double value) {
        return value >= controller_tuning::triggerPressThreshold;
    });
}

bool controllerResumeBlocked(PauseReason reason, bool controllerConnected, bool neutralRequired)
{
    if (reason == PauseReason::ControllerDisconnected) {
        return !controllerConnected || neutralRequired;
    }
    if (reason == PauseReason::PageHidden) {
        return neutralRequired;
    }
    return false;
}

bool controllerPauseStopsSimulation(PauseReason reason, InputContext gameplayContext, bool modalOpen)
{
    // Modal visibility is authoritative even before a controller-specific
    // pause reason has been assigned. Results and other non-realtime screens
    // can auto-open a modal while PauseReason is still None; allowing that
    // early return first keeps their scene animation running underneath the
    // input boundary.
    if (modalOpen) {
        return true;
    }
    if (reason == PauseReason::None) {
        return false;
    }
    // Launch is an autonomous burn. A stale controller-focus pause from the
    // prior screen must not freeze it. Steering/drilling contexts still pause
    // for safety.
    return reason != PauseReason::ControllerUiFocus || gameplayContext != InputContext::Launch;
}

InputContext resolvedControllerInputContext(InputContext gameplayContext, PauseReason reason, bool modalOpen)
{
    // A visible modal is the authoritative controller focus scope. Route every
    // modal, including forced mining recovery, through the same focused UI
    // actions so no gameplay or background-panel binding can win because of a
    // stale pause reason.
    if (modalOpen) {
        return InputContext::Paused;
    }
    // Preflight and launch are hard gameplay handoffs. Stale menu focus must
    // not replace Cross/South with generic UI activation. Actual modals and
    // safety pauses still take precedence.
    if ((gameplayContext == InputContext::Preflight || gameplayContext == InputContext::Launch)
        && reason == PauseReason::ControllerUiFocus
        && !modalOpen) {
        return gameplayContext;
    }
    if (reason != PauseReason::None) {
        return InputContext::Paused;
    }
    return gameplayContext;
}

void InputSourceArbiter::noteActivity(InputSource source, double realTimeSeconds, bool meaningful)
{
    if (!meaningful || source == InputSource::None || !std::isfinite(realTimeSeconds)) {
        return;
    }
    if (hasActivity_) {
        if (realTimeSeconds < lastActivitySeconds_) {
            return;
        }
        if (realTimeSeconds == lastActivitySeconds_
            && sourceTiePriority(source) <= sourceTiePriority(activeSource_)) {
            return;
        }
    }
    activeSource_ = source;
    lastActivitySeconds_ = realTimeSeconds;
    hasActivity_ = true;
}

InputSource InputSourceArbiter::activeSource() const
{
    return activeSource_;
}

bool InputSourceArbiter::shouldApply(InputSource source) const
{
    return activeSource_ == InputSource::None || activeSource_ == source;
}

void InputSourceArbiter::reset()
{
    activeSource_ = InputSource::None;
    lastActivitySeconds_ = 0.0;
    hasActivity_ = false;
}

const RawControllerSnapshot* ControllerTracker::selectActive(std::span<const RawControllerSnapshot> snapshots, double deadzone) const
{
    const RawControllerSnapshot* current = nullptr;
    const RawControllerSnapshot* mostRecentMeaningful = nullptr;
    const RawControllerSnapshot* synthetic = nullptr;
    for (const RawControllerSnapshot& snapshot : snapshots) {
        if (!snapshot.connected || !snapshot.standardMapping) {
            continue;
        }
        if (snapshot.synthetic && !synthetic) {
            synthetic = &snapshot;
        }
        if (snapshot.index == activeControllerIndex_) {
            current = &snapshot;
        }
        const bool eligibleClaim = snapshot.synthetic
            || !requiresFreshPhysicalClaim_
            || physicalClaimArmed_;
        if (eligibleClaim
            && meaningfulControllerInput(snapshot, deadzone)
            && (!mostRecentMeaningful || snapshot.timestamp >= mostRecentMeaningful->timestamp)) {
            mostRecentMeaningful = &snapshot;
        }
    }
    if (mostRecentMeaningful && !current) {
        return mostRecentMeaningful;
    }
    if (mostRecentMeaningful && current && mostRecentMeaningful->index != current->index) {
        // A held input on a previously active pad remains "meaningful", but
        // its Gamepad timestamp is stale. Only a candidate with activity newer
        // than the current pad's latest state may take ownership; otherwise a
        // stale RT/button hold could be recreated as a fresh press as soon as
        // the current controller returned to neutral.
        const double candidateTimestamp = std::isfinite(mostRecentMeaningful->timestamp)
            ? mostRecentMeaningful->timestamp
            : 0.0;
        const double currentTimestamp = std::isfinite(current->timestamp)
            ? current->timestamp
            : 0.0;
        if (candidateTimestamp > currentTimestamp) {
            return mostRecentMeaningful;
        }
    }
    if (current) {
        return current;
    }
    if (mostRecentMeaningful) {
        return mostRecentMeaningful;
    }
    // Physical pads do not become active from neutral polling alone. This is
    // especially important after an active pad disappears: a second idle pad
    // must not conceal the loss from gameplay safety handling. Synthetic Lab
    // pads remain inspectable even while neutral.
    return synthetic;
}

std::optional<UiDirection> ControllerTracker::navigationDirection(const RawControllerSnapshot& snapshot)
{
    const auto dpad = [&](ControllerButton button) {
        return snapshot.buttons[buttonIndex(button)] >= controller_tuning::digitalButtonThreshold;
    };
    if (dpad(ControllerButton::DpadUp)) {
        return UiDirection::Up;
    }
    if (dpad(ControllerButton::DpadDown)) {
        return UiDirection::Down;
    }
    if (dpad(ControllerButton::DpadLeft)) {
        return UiDirection::Left;
    }
    if (dpad(ControllerButton::DpadRight)) {
        return UiDirection::Right;
    }

    const double x = snapshot.leftX;
    const double y = snapshot.leftY;
    if (navigationDirection_) {
        const bool stillEngaged = (*navigationDirection_ == UiDirection::Left || *navigationDirection_ == UiDirection::Right)
            ? std::abs(x) >= controller_tuning::menuReleaseThreshold
            : std::abs(y) >= controller_tuning::menuReleaseThreshold;
        if (stillEngaged) {
            return navigationDirection_;
        }
    }
    if (std::max(std::abs(x), std::abs(y)) < controller_tuning::menuEngageThreshold) {
        return std::nullopt;
    }
    if (std::abs(x) > std::abs(y)) {
        return x < 0.0 ? UiDirection::Left : UiDirection::Right;
    }
    return y < 0.0 ? UiDirection::Up : UiDirection::Down;
}

ControllerFrame ControllerTracker::update(
    std::span<const RawControllerSnapshot> snapshots,
    double realTimeSeconds,
    const ControllerPreferences& preferences)
{
    ControllerFrame frame;
    const double deadzone = std::clamp(
        preferences.stickDeadzone,
        controller_tuning::minimumStickDeadzone,
        controller_tuning::maximumStickDeadzone);

    if (connected_) {
        const bool activeStillPresent = std::any_of(
            snapshots.begin(),
            snapshots.end(),
            [&](const RawControllerSnapshot& snapshot) {
                return snapshot.connected
                    && snapshot.standardMapping
                    && snapshot.index == activeControllerIndex_;
            });
        if (!activeStillPresent) {
            // Always surface the active pad loss for one frame before another
            // pad can claim ownership. This guarantees release cleanup and
            // gives realtime gameplay a reliable disconnect safety signal.
            const bool lostSyntheticController = activeControllerSynthetic_;
            frame.synthetic = lostSyntheticController;
            frame.justDisconnected = true;
            frame.released = down_;
            reset();
            if (!lostSyntheticController) {
                requiresFreshPhysicalClaim_ = true;
                // A neutral replacement is armed immediately. A pad held
                // through the loss must first return to neutral, preventing
                // stale input from becoming the new campaign controller.
                physicalClaimArmed_ = std::none_of(
                    snapshots.begin(),
                    snapshots.end(),
                    [&](const RawControllerSnapshot& snapshot) {
                        return !snapshot.synthetic
                            && snapshot.connected
                            && snapshot.standardMapping
                            && meaningfulControllerInput(snapshot, deadzone);
                    });
            }
            return frame;
        }
    }

    if (requiresFreshPhysicalClaim_ && !physicalClaimArmed_) {
        const bool anyHeldPhysicalInput = std::any_of(
            snapshots.begin(),
            snapshots.end(),
            [&](const RawControllerSnapshot& snapshot) {
                return !snapshot.synthetic
                    && snapshot.connected
                    && snapshot.standardMapping
                    && meaningfulControllerInput(snapshot, deadzone);
            });
        if (!anyHeldPhysicalInput) {
            physicalClaimArmed_ = true;
        }
    }

    const RawControllerSnapshot* selected = selectActive(snapshots, deadzone);
    if (!selected) {
        frame.synthetic = activeControllerSynthetic_;
        frame.justDisconnected = connected_;
        frame.released = down_;
        if (connected_) {
            reset();
        }
        return frame;
    }

    const bool controllerChanged = selected->index != activeControllerIndex_;
    frame.synthetic = selected->synthetic;
    frame.connected = true;
    frame.justConnected = !connected_ || controllerChanged;
    frame.justDisconnected = false;
    frame.controllerIndex = selected->index;
    frame.family = selected->family == ControllerFamily::Generic ? controllerFamilyFromId(selected->id) : selected->family;
    frame.id = selected->id;
    frame.meaningfulInput = meaningfulControllerInput(*selected, deadzone);

    const auto left = applyRadialDeadzone(selected->leftX, selected->leftY, deadzone);
    const auto right = applyRadialDeadzone(selected->rightX, selected->rightY, deadzone);
    frame.leftX = left[0];
    frame.leftY = left[1];
    frame.rightX = right[0];
    frame.rightY = right[1];

    if (controllerChanged) {
        down_.reset();
        downSinceSeconds_.fill(0.0);
        navigationDirection_.reset();
        navigationNextRepeatSeconds_ = 0.0;
        lastLeftX_ = 0.0;
        lastLeftY_ = 0.0;
        lastRightX_ = 0.0;
        lastRightY_ = 0.0;
    }

    for (std::size_t index = 0; index < controllerButtonCount; ++index) {
        const ControllerButton button = static_cast<ControllerButton>(index);
        const double value = std::clamp(selected->buttons[index], 0.0, 1.0);
        const bool wasDown = down_.test(index);
        const double pressThreshold = triggerButton(button)
            ? controller_tuning::triggerPressThreshold
            : controller_tuning::digitalButtonThreshold;
        const double releaseThreshold = triggerButton(button)
            ? controller_tuning::triggerReleaseThreshold
            : controller_tuning::digitalButtonThreshold;
        const bool nowDown = wasDown ? value > releaseThreshold : value >= pressThreshold;
        frame.down.set(index, nowDown);
        frame.pressed.set(index, nowDown && !wasDown);
        frame.released.set(index, !nowDown && wasDown);
        if (nowDown && !wasDown) {
            downSinceSeconds_[index] = realTimeSeconds;
        }
        frame.heldSeconds[index] = nowDown ? std::max(0.0, realTimeSeconds - downSinceSeconds_[index]) : 0.0;
    }

    constexpr double sourceActivityAxisDelta = 0.08;
    const bool leftAxisActive = std::hypot(frame.leftX, frame.leftY) > 0.0;
    const bool rightAxisActive = std::hypot(frame.rightX, frame.rightY) > 0.0;
    const bool leftAxisMoved = leftAxisActive
        && std::hypot(frame.leftX - lastLeftX_, frame.leftY - lastLeftY_) >= sourceActivityAxisDelta;
    const bool rightAxisMoved = rightAxisActive
        && std::hypot(frame.rightX - lastRightX_, frame.rightY - lastRightY_) >= sourceActivityAxisDelta;
    frame.meaningfulActivity = (controllerChanged && frame.meaningfulInput)
        || frame.pressed.any()
        || leftAxisMoved
        || rightAxisMoved;
    if (frame.meaningfulActivity) {
        frame.activityTimestamp = std::isfinite(selected->timestamp) && selected->timestamp > 0.0
            ? selected->timestamp
            : realTimeSeconds;
    }

    const std::optional<UiDirection> direction = navigationDirection(*selected);
    if (!direction) {
        navigationDirection_.reset();
        navigationNextRepeatSeconds_ = 0.0;
    } else if (!navigationDirection_ || *navigationDirection_ != *direction) {
        frame.navigation = direction;
        navigationDirection_ = direction;
        navigationNextRepeatSeconds_ = realTimeSeconds + controller_tuning::menuInitialRepeatSeconds;
    } else if (realTimeSeconds >= navigationNextRepeatSeconds_) {
        frame.navigation = direction;
        frame.navigationRepeated = true;
        do {
            navigationNextRepeatSeconds_ += controller_tuning::menuRepeatSeconds;
        } while (navigationNextRepeatSeconds_ <= realTimeSeconds);
    }

    activeControllerIndex_ = selected->index;
    connected_ = true;
    activeControllerSynthetic_ = selected->synthetic;
    if (!selected->synthetic) {
        requiresFreshPhysicalClaim_ = false;
        physicalClaimArmed_ = false;
    }
    down_ = frame.down;
    lastLeftX_ = frame.leftX;
    lastLeftY_ = frame.leftY;
    lastRightX_ = frame.rightX;
    lastRightY_ = frame.rightY;
    return frame;
}

void ControllerTracker::reset()
{
    activeControllerIndex_ = -1;
    connected_ = false;
    activeControllerSynthetic_ = false;
    down_.reset();
    downSinceSeconds_.fill(0.0);
    navigationDirection_.reset();
    navigationNextRepeatSeconds_ = 0.0;
    lastLeftX_ = 0.0;
    lastLeftY_ = 0.0;
    lastRightX_ = 0.0;
    lastRightY_ = 0.0;
    requiresFreshPhysicalClaim_ = false;
    physicalClaimArmed_ = false;
}

int ControllerTracker::activeControllerIndex() const
{
    return activeControllerIndex_;
}

} // namespace rocket
