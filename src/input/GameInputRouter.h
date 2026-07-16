#pragma once

#include "input/ControllerInput.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>

namespace rocket {

enum class GameInputAction : std::size_t {
    ActivateFocused,
    CancelFocused,
    OpenSystemMenu,
    OpenMap,
    OpenInventory,
    StartOrContinue,
    ReturnHome,
    Eject,
    ToggleEngines,
    TogglePressureRelief,
    JettisonCargo,
    Abort,
    PrimarySurfaceAction,
    BankSurfaceAction,
    MiningScan,
    MiningTether,
    MiningStow,
    MiningRepairDrill,
    MiningRepairRig,
    MiningFailureAcknowledge,
    EnterUiFocus,
    Count
};

inline constexpr std::size_t gameInputActionCount = static_cast<std::size_t>(GameInputAction::Count);

struct RoutedGameInput {
    std::bitset<gameInputActionCount> actions;
    std::optional<UiDirection> navigation;
    double scroll = 0.0;
    double moveX = 0.0;
    double moveY = 0.0;
    bool drilling = false;

    bool has(GameInputAction action) const
    {
        return actions.test(static_cast<std::size_t>(action));
    }
};

// Converts the W3C positional controller layout into semantic game actions.
// It deliberately owns no game state: RocketGameApp remains responsible for
// validating actions against the authoritative screen and run state.
class GameInputRouter {
public:
    RoutedGameInput route(
        InputContext context,
        const ControllerFrame& frame,
        const ControllerPreferences& preferences,
        double focusedActivationHoldSeconds = 0.0)
    {
        RoutedGameInput result;
        if (!lastContext_) {
            lastContext_ = context;
        } else if (*lastContext_ != context) {
            // A button held across a screen/context transition must be released
            // before it can trigger a dangerous hold action in the new context.
            holdTriggered_ = frame.down;
            lastContext_ = context;
            contextualUiFocusActive_ = false;
        }
        updateHoldLatches(frame);

        const ControllerButton confirmButton = preferences.swapConfirmCancel
            ? ControllerButton::East
            : ControllerButton::South;
        const ControllerButton cancelButton = preferences.swapConfirmCancel
            ? ControllerButton::South
            : ControllerButton::East;
        const double activationHoldSeconds = std::max(0.0, focusedActivationHoldSeconds);
        if (lastActivationHoldSeconds_ < 0.0) {
            lastActivationHoldSeconds_ = activationHoldSeconds;
        } else if (lastActivationHoldSeconds_ != activationHoldSeconds) {
            // Moving focus onto a hold-to-confirm action while Confirm is
            // already down must not inherit time held on another control.
            if (activationHoldSeconds > 0.0 && frame.isDown(confirmButton)) {
                holdTriggered_.set(static_cast<std::size_t>(confirmButton));
            }
            lastActivationHoldSeconds_ = activationHoldSeconds;
        }
        const auto add = [&](GameInputAction action) {
            result.actions.set(static_cast<std::size_t>(action));
        };

        const bool uiContext = context == InputContext::Ui || context == InputContext::Paused;
        if (uiContext) {
            result.navigation = frame.navigation;
            result.scroll = frame.rightY;
            if ((activationHoldSeconds <= 0.0 && frame.wasPressed(confirmButton))
                || (activationHoldSeconds > 0.0 && holdCrossed(frame, confirmButton, activationHoldSeconds))) {
                add(GameInputAction::ActivateFocused);
            }
            if (frame.wasPressed(cancelButton)) {
                add(GameInputAction::CancelFocused);
            }
            if (frame.wasPressed(ControllerButton::Menu)) {
                add(GameInputAction::OpenSystemMenu);
            }
            if (context == InputContext::Ui && frame.wasPressed(ControllerButton::View)) {
                add(GameInputAction::OpenMap);
            }
            if (context == InputContext::Ui && frame.wasPressed(ControllerButton::North)) {
                add(GameInputAction::OpenInventory);
            }
            return result;
        }

        if (frame.wasPressed(ControllerButton::Menu)) {
            add(GameInputAction::OpenSystemMenu);
        }

        switch (context) {
        case InputContext::Preflight:
            if (frame.wasPressed(ControllerButton::South)) {
                add(GameInputAction::StartOrContinue);
            }
            break;

        case InputContext::Stamp:
            result.navigation = frame.navigation;
            result.scroll = frame.rightY;
            if (frame.navigation) {
                contextualUiFocusActive_ = true;
            }
            if (frame.wasPressed(ControllerButton::South)) {
                add(contextualUiFocusActive_
                    ? GameInputAction::ActivateFocused
                    : GameInputAction::StartOrContinue);
            }
            if (frame.wasPressed(ControllerButton::East)) {
                add(GameInputAction::CancelFocused);
                contextualUiFocusActive_ = false;
                holdTriggered_.set(static_cast<std::size_t>(ControllerButton::East));
            }
            break;

        case InputContext::Launch:
            if (frame.wasPressed(ControllerButton::South)) {
                add(GameInputAction::ReturnHome);
            }
            if (holdCrossed(frame, ControllerButton::East, 0.75)) {
                add(GameInputAction::Eject);
            }
            if (frame.wasPressed(ControllerButton::West)) {
                add(GameInputAction::ToggleEngines);
            }
            if (frame.wasPressed(ControllerButton::North)) {
                add(GameInputAction::TogglePressureRelief);
            }
            if (holdCrossed(frame, ControllerButton::RightBumper, 0.45)) {
                add(GameInputAction::JettisonCargo);
            }
            break;

        case InputContext::FlybyActive:
        case InputContext::OrbitActive:
            enterUiFocusFromDpad(frame, result);
            result.moveX = frame.leftX;
            result.moveY = preferences.invertFlightY ? frame.leftY : -frame.leftY;
            if (holdCrossed(frame, ControllerButton::East, 0.45)) {
                add(GameInputAction::Abort);
            }
            break;

        case InputContext::FlybyComplete:
        case InputContext::OrbitComplete:
            if (frame.wasPressed(ControllerButton::South)) {
                add(GameInputAction::StartOrContinue);
            }
            break;

        case InputContext::SurfaceScan:
        case InputContext::SurfacePush:
            result.navigation = frame.navigation;
            result.scroll = frame.rightY;
            if (frame.navigation) {
                contextualUiFocusActive_ = true;
            }
            if (frame.wasPressed(ControllerButton::South)) {
                add(contextualUiFocusActive_
                    ? GameInputAction::ActivateFocused
                    : GameInputAction::PrimarySurfaceAction);
            }
            if (frame.wasPressed(ControllerButton::West)) {
                add(GameInputAction::BankSurfaceAction);
            }
            if (contextualUiFocusActive_ && frame.wasPressed(ControllerButton::East)) {
                add(GameInputAction::CancelFocused);
                contextualUiFocusActive_ = false;
                holdTriggered_.set(static_cast<std::size_t>(ControllerButton::East));
            } else if (!contextualUiFocusActive_ && holdCrossed(frame, ControllerButton::East, 0.45)) {
                add(GameInputAction::Abort);
            }
            break;

        case InputContext::MiningActive:
        case InputContext::MiningService:
            enterUiFocusFromDpad(frame, result);
            result.moveX = frame.leftX;
            result.moveY = frame.leftY;
            result.drilling = frame.isDown(ControllerButton::RightTrigger);
            if (frame.wasPressed(ControllerButton::West)) {
                add(GameInputAction::MiningScan);
            }
            if (frame.wasPressed(ControllerButton::North)) {
                add(GameInputAction::MiningTether);
            }
            if (frame.wasPressed(ControllerButton::South)) {
                add(GameInputAction::MiningStow);
            }
            if (frame.wasPressed(ControllerButton::LeftBumper)) {
                add(GameInputAction::MiningRepairDrill);
            }
            if (frame.wasPressed(ControllerButton::RightBumper)) {
                add(GameInputAction::MiningRepairRig);
            }
            if (holdCrossed(frame, ControllerButton::East, 0.45)) {
                add(GameInputAction::Abort);
            }
            break;

        case InputContext::MiningFailure:
            if (frame.wasPressed(ControllerButton::South)) {
                add(GameInputAction::MiningFailureAcknowledge);
            }
            break;

        case InputContext::Ui:
        case InputContext::Paused:
            break;
        }
        return result;
    }

    void reset()
    {
        holdTriggered_.reset();
        lastContext_.reset();
        lastActivationHoldSeconds_ = -1.0;
        contextualUiFocusActive_ = false;
    }

private:
    static bool dpadPressed(const ControllerFrame& frame)
    {
        return frame.wasPressed(ControllerButton::DpadUp)
            || frame.wasPressed(ControllerButton::DpadDown)
            || frame.wasPressed(ControllerButton::DpadLeft)
            || frame.wasPressed(ControllerButton::DpadRight);
    }

    static void enterUiFocusFromDpad(const ControllerFrame& frame, RoutedGameInput& result)
    {
        if (!dpadPressed(frame)) {
            return;
        }
        result.actions.set(static_cast<std::size_t>(GameInputAction::EnterUiFocus));
        result.navigation = frame.navigation;
    }

    bool holdCrossed(const ControllerFrame& frame, ControllerButton button, double threshold)
    {
        const std::size_t index = static_cast<std::size_t>(button);
        if (!frame.isDown(button) || holdTriggered_.test(index) || frame.heldFor(button) < threshold) {
            return false;
        }
        holdTriggered_.set(index);
        return true;
    }

    void updateHoldLatches(const ControllerFrame& frame)
    {
        for (std::size_t index = 0; index < controllerButtonCount; ++index) {
            if (!frame.down.test(index)) {
                holdTriggered_.reset(index);
            }
        }
    }

    std::bitset<controllerButtonCount> holdTriggered_;
    std::optional<InputContext> lastContext_;
    double lastActivationHoldSeconds_ = -1.0;
    bool contextualUiFocusActive_ = false;
};

} // namespace rocket
