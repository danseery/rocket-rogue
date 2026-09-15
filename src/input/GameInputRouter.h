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
    ToggleEngines,
    ToggleCruise,
    DeploySurfaceTeam,
    ResumeOrbitalFlight,
    DepartSurfaceUndeployed,
    Abort,
    MiningScan,
    MiningTether,
    MiningStow,
    MiningOperatorToggle,
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
    double strafe = 0.0;
    double moveY = 0.0;
    double aimX = 0.0;
    double aimY = 0.0;
    double operatorToggleProgress = 0.0;
    bool firing = false;
    bool drilling = false;
    bool orbitalHeld = false;

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
    // Pointer ownership is not a disconnected device. Keep observing button
    // releases and focus changes, but never activate or navigate anything.
    // A fresh Confirm may then reclaim the controller in one press, whereas
    // a hold that spans the ownership change must still be released first.
    void observeInactiveFrame(
        InputContext context,
        const ControllerFrame& frame,
        const ControllerPreferences& preferences,
        const FocusedControllerAction& focusedAction = {})
    {
        const ControllerButton confirmButton = preferences.swapConfirmCancel
            ? ControllerButton::East : ControllerButton::South;
        holdTriggered_ = frame.down;
        confirmFenced_ = frame.isDown(confirmButton);
        lastContinuousOutput_ = false;
        if (!lastContext_ || *lastContext_ != context) {
            contextualUiFocusActive_ = false;
        }
        lastContext_ = context;
        lastFocusedActionId_ = focusedAction.id;
        lastActivationHoldSeconds_ = std::max(0.0, focusedAction.holdSeconds);
        lastContinuousActivation_ = focusedAction.kind == ControllerActivationKind::ContinuousHold;
        lastConfirmButton_ = confirmButton;
    }

    RoutedGameInput route(
        InputContext context,
        const ControllerFrame& frame,
        const ControllerPreferences& preferences,
        double focusedActivationHoldSeconds = 0.0,
        std::string_view focusedActionId = {},
        bool continuousActivation = false)
    {
        RoutedGameInput result;
        const bool previouslyActivatedContinuously = lastContinuousOutput_;
        lastContinuousOutput_ = false;
        if (!frame.connected || frame.justDisconnected || !frame.pageVisible || !frame.browserFocused) {
            // Losing an input boundary releases continuous activation now and
            // never recreates a held action when that boundary becomes live.
            suspended_ = suspended_ || lastContext_.has_value();
            return result;
        }
        const ControllerButton confirmButton = preferences.swapConfirmCancel
            ? ControllerButton::East
            : ControllerButton::South;
        const ControllerButton cancelButton = preferences.swapConfirmCancel
            ? ControllerButton::South
            : ControllerButton::East;
        const double activationHoldSeconds = std::max(0.0, focusedActivationHoldSeconds);
        const std::bitset<controllerButtonCount> holdTriggeredBeforeUpdate = holdTriggered_;
        const auto fenceHeldInput = [&]() {
            holdTriggered_ |= frame.down;
            confirmFenced_ = confirmFenced_ || frame.isDown(confirmButton);
        };
        if (suspended_) {
            fenceHeldInput();
            suspended_ = false;
        }
        if (!lastContext_) {
            // Disconnected startup polling is not a lost live controller.
            // Accept its first real press, but never inherit a held snapshot
            // with no new edge as an activation on first connection.
            holdTriggered_ |= frame.down & ~frame.pressed;
            confirmFenced_ = frame.isDown(confirmButton) && !frame.wasPressed(confirmButton);
            lastContext_ = context;
        } else if (*lastContext_ != context) {
            // A held Scan must not become Drill, Land, or a confirmation in
            // another screen. The same fence protects mining tap/hold actions.
            const bool continuingOrbitalDrill = context == InputContext::OrbitalWork
                && (*lastContext_ == InputContext::Launch || *lastContext_ == InputContext::Paused)
                && previouslyActivatedContinuously && !confirmFenced_
                && continuousActivation && lastContinuousActivation_
                && !focusedActionId.empty() && focusedActionId == lastFocusedActionId_
                && confirmButton == lastConfirmButton_;
            // A deliberately started drill may establish orbital ownership on
            // its next frame without interrupting that same continuous action.
            // Scan-to-Drill, modal restoration and all other handoffs still fence.
            if (!continuingOrbitalDrill) {
                fenceHeldInput();
            }
            lastContext_ = context;
            contextualUiFocusActive_ = false;
        }
        if (context != InputContext::MiningActive && context != InputContext::MiningService
            && lastActivationHoldSeconds_ >= 0.0
            && (lastFocusedActionId_ != focusedActionId
                || lastContinuousActivation_ != continuousActivation
                || lastActivationHoldSeconds_ != activationHoldSeconds
                || lastConfirmButton_ != confirmButton)) {
            fenceHeldInput();
        }
        lastFocusedActionId_ = focusedActionId;
        lastContinuousActivation_ = continuousActivation;
        lastActivationHoldSeconds_ = activationHoldSeconds;
        lastConfirmButton_ = confirmButton;
        updateHoldLatches(frame);
        if (!frame.isDown(confirmButton)) {
            confirmFenced_ = false;
        }
        const auto add = [&](GameInputAction action) {
            result.actions.set(static_cast<std::size_t>(action));
        };

        // Focus boundaries win before any movement, trigger, hold or action
        // is emitted. This is important when Menu and thrust share a frame.
        if (frame.wasPressed(ControllerButton::Menu)) {
            fenceHeldInput();
            add(GameInputAction::OpenSystemMenu);
            return result;
        }
        if ((context == InputContext::Ui || context == InputContext::Launch || context == InputContext::OrbitalWork)
            && frame.wasPressed(ControllerButton::View)) {
            fenceHeldInput();
            add(GameInputAction::OpenMap);
            return result;
        }
        if (context == InputContext::Ui && frame.wasPressed(ControllerButton::North)) {
            fenceHeldInput();
            add(GameInputAction::OpenInventory);
            return result;
        }
        if ((context == InputContext::Launch || context == InputContext::MiningActive || context == InputContext::MiningService)
            && dpadPressed(frame)) {
            fenceHeldInput();
            enterUiFocusFromDpad(frame, result);
            return result;
        }

        const bool uiContext = context == InputContext::Ui || context == InputContext::Paused
            || context == InputContext::OrbitalWork || context == InputContext::Preflight
            || context == InputContext::Stamp || context == InputContext::SurfaceArrival
            || context == InputContext::MiningFailure;
        if (uiContext) {
            if (context == InputContext::SurfaceArrival) {
                // Taking off without deploying retains its deliberate hold;
                // Confirm/Cancel swapping changes the physical button only.
                if (holdCrossed(frame, cancelButton, 0.45)) {
                    fenceHeldInput();
                    add(GameInputAction::DepartSurfaceUndeployed);
                    return result;
                }
                if (frame.isDown(cancelButton)) {
                    confirmFenced_ = confirmFenced_ || frame.isDown(confirmButton);
                    if (frame.isDown(confirmButton)) {
                        holdTriggered_.set(static_cast<std::size_t>(confirmButton));
                    }
                    return result;
                }
            } else if (frame.wasPressed(cancelButton)) {
                fenceHeldInput();
                add(GameInputAction::CancelFocused);
                contextualUiFocusActive_ = false;
                return result;
            }
            result.navigation = frame.navigation;
            result.scroll = frame.rightY;
            if (frame.navigation) {
                fenceHeldInput();
                contextualUiFocusActive_ = true;
                return result;
            }
            if (confirmFenced_) {
                return result;
            }
            if (continuousActivation) {
                result.orbitalHeld = frame.isDown(confirmButton);
                lastContinuousOutput_ = result.orbitalHeld;
                return result;
            }
            if ((activationHoldSeconds <= 0.0 && frame.wasPressed(confirmButton))
                || (activationHoldSeconds > 0.0 && holdCrossed(frame, confirmButton, activationHoldSeconds))) {
                // Metadata makes the rendered control authoritative. Legacy
                // callers retain their primary action until they expose focus.
                GameInputAction action = GameInputAction::ActivateFocused;
                if (focusedActionId.empty() && !contextualUiFocusActive_) {
                    if (context == InputContext::Preflight || context == InputContext::Stamp) {
                        action = GameInputAction::StartOrContinue;
                    } else if (context == InputContext::SurfaceArrival) {
                        action = GameInputAction::DeploySurfaceTeam;
                    } else if (context == InputContext::MiningFailure) {
                        action = GameInputAction::MiningFailureAcknowledge;
                    }
                }
                add(action);
            }
            return result;
        }

        switch (context) {
        case InputContext::Launch:
            result.moveX = frame.rightX;
            result.strafe = frame.leftX;
            result.moveY = preferences.invertFlightY ? frame.leftY : -frame.leftY;
            result.orbitalHeld = frame.isDown(confirmButton) && !confirmFenced_;
            lastContinuousOutput_ = result.orbitalHeld;
            if (frame.wasPressed(ControllerButton::LeftStick)) add(GameInputAction::ToggleCruise);
            break;

        case InputContext::MiningActive:
        case InputContext::MiningService:
            result.moveX = frame.leftX;
            result.moveY = frame.leftY;
            result.aimX = frame.rightX;
            result.aimY = frame.rightY;
            result.firing = frame.isDown(ControllerButton::RightTrigger);
            result.drilling = frame.isDown(ControllerButton::LeftTrigger);
            result.operatorToggleProgress = frame.isDown(ControllerButton::South)
                ? std::clamp(frame.heldFor(ControllerButton::South) / operatorToggleHoldSeconds, 0.0, 1.0)
                : 0.0;
            if (frame.wasPressed(ControllerButton::West)) {
                add(GameInputAction::MiningScan);
            }
            if (frame.wasPressed(ControllerButton::North)) {
                add(GameInputAction::MiningTether);
            }
            if (holdCrossed(frame, ControllerButton::South, operatorToggleHoldSeconds)
                || (frame.wasReleased(ControllerButton::South)
                    && !holdTriggeredBeforeUpdate.test(static_cast<std::size_t>(ControllerButton::South))
                    && frame.heldFor(ControllerButton::South) >= operatorToggleHoldSeconds)) {
                add(GameInputAction::MiningOperatorToggle);
            } else if (frame.wasReleased(ControllerButton::South)
                && !holdTriggeredBeforeUpdate.test(static_cast<std::size_t>(ControllerButton::South))) {
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

        case InputContext::Preflight:
        case InputContext::Stamp:
        case InputContext::SurfaceArrival:
        case InputContext::MiningFailure:
        case InputContext::OrbitalWork:
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
        lastFocusedActionId_.clear();
        lastContinuousActivation_ = false;
        lastContinuousOutput_ = false;
        lastConfirmButton_ = ControllerButton::South;
        confirmFenced_ = false;
        suspended_ = false;
        contextualUiFocusActive_ = false;
    }

private:
    static constexpr double operatorToggleHoldSeconds = 0.6;

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
    std::string lastFocusedActionId_;
    bool lastContinuousActivation_ = false;
    bool lastContinuousOutput_ = false;
    ControllerButton lastConfirmButton_ = ControllerButton::South;
    bool confirmFenced_ = false;
    bool suspended_ = false;
    bool contextualUiFocusActive_ = false;
};

} // namespace rocket
