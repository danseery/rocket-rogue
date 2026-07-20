#include "input/ControllerInput.h"
#include "input/GameInputRouter.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::size_t index(rocket::ControllerButton button)
{
    return static_cast<std::size_t>(button);
}

rocket::RawControllerSnapshot controller(int controllerIndex = 0)
{
    rocket::RawControllerSnapshot result;
    result.connected = true;
    result.standardMapping = true;
    result.index = controllerIndex;
    result.id = "Xbox Wireless Controller";
    return result;
}

void familyDetectionAndOverrides()
{
    using namespace rocket;
    require(controllerFamilyFromId("Sony DualSense Wireless Controller (054c)") == ControllerFamily::PlayStation, "DualSense should use PlayStation prompts");
    require(controllerFamilyFromId("Valve Steam Deck (28de)") == ControllerFamily::SteamDeck, "Steam Deck should use Deck prompts");
    require(controllerFamilyFromId("Microsoft Xbox Controller") == ControllerFamily::Xbox, "Xbox should use Xbox prompts");
    require(controllerFamilyFromId("Mystery Pad") == ControllerFamily::Generic, "unknown pads should use generic prompts");

    ControllerPreferences preferences;
    preferences.promptFamily = ControllerPromptFamily::PlayStation;
    require(resolvedPromptFamily(preferences, ControllerFamily::Xbox) == ControllerFamily::PlayStation, "manual prompt override should win");
}

void radialDeadzoneRescales()
{
    using namespace rocket;
    const auto idle = applyRadialDeadzone(0.10, 0.10, 0.20);
    require(idle[0] == 0.0 && idle[1] == 0.0, "drift inside the radial deadzone should be zero");
    const auto full = applyRadialDeadzone(1.0, 0.0, 0.20);
    require(std::abs(full[0] - 1.0) < 0.000001 && full[1] == 0.0, "full stick should remain full after rescaling");
    const auto partial = applyRadialDeadzone(0.60, 0.0, 0.20);
    require(std::abs(partial[0] - 0.50) < 0.000001, "remaining stick range should be linearly rescaled");
}

void buttonEdgesHoldsAndDisconnect()
{
    using namespace rocket;
    ControllerTracker tracker;
    RawControllerSnapshot pad = controller();
    std::vector<RawControllerSnapshot> pads {pad};
    ControllerFrame frame = tracker.update(pads, 10.0);
    require(!frame.connected, "a neutral physical pad should not claim active ownership");

    pads[0].buttons[index(ControllerButton::South)] = 1.0;
    frame = tracker.update(pads, 10.1);
    require(frame.connected && frame.justConnected, "first meaningful physical input should connect the pad");
    require(frame.wasPressed(ControllerButton::South) && frame.isDown(ControllerButton::South), "button down should create a pressed edge");
    require(frame.meaningfulActivity, "a fresh button edge should claim controller input source activity");
    frame = tracker.update(pads, 10.7);
    require(!frame.wasPressed(ControllerButton::South) && frame.heldFor(ControllerButton::South) >= 0.59, "held duration should use real time");
    require(frame.meaningfulInput && !frame.meaningfulActivity, "a held button should not continuously reclaim the input source");

    pads.clear();
    frame = tracker.update(pads, 10.8);
    require(frame.justDisconnected && frame.wasReleased(ControllerButton::South), "disconnect should release every held button");
    require(tracker.activeControllerIndex() == -1, "disconnect should clear the active controller");
}

void activePadLossDoesNotFallBackToNeutralPad()
{
    using namespace rocket;
    ControllerTracker tracker;
    RawControllerSnapshot active = controller(0);
    active.buttons[index(ControllerButton::South)] = 1.0;
    RawControllerSnapshot replacement = controller(1);
    std::vector<RawControllerSnapshot> pads {active, replacement};

    ControllerFrame frame = tracker.update(pads, 1.0);
    require(frame.connected && frame.controllerIndex == 0, "meaningful input should establish the first active pad");

    pads.erase(pads.begin());
    frame = tracker.update(pads, 1.1);
    require(!frame.connected && frame.justDisconnected, "losing the active pad must produce a safety disconnect frame");
    require(frame.wasReleased(ControllerButton::South), "active-pad loss must release held controls");
    require(tracker.activeControllerIndex() == -1, "a neutral replacement must not silently inherit ownership");

    frame = tracker.update(pads, 1.2);
    require(!frame.connected && !frame.justDisconnected, "continued neutral polling must not repeatedly signal or claim ownership");

    pads[0].timestamp = 1.3;
    pads[0].buttons[index(ControllerButton::West)] = 1.0;
    frame = tracker.update(pads, 1.3);
    require(frame.connected && frame.justConnected && frame.controllerIndex == 1, "replacement should connect only after meaningful input");
    require(frame.wasPressed(ControllerButton::West), "replacement input should begin with a clean pressed edge");
}

void activePadLossWinsOverSameFrameReplacementInput()
{
    using namespace rocket;
    ControllerTracker tracker;
    RawControllerSnapshot first = controller(0);
    first.buttons[index(ControllerButton::South)] = 1.0;
    std::vector<RawControllerSnapshot> pads {first};
    tracker.update(pads, 2.0);

    RawControllerSnapshot replacement = controller(1);
    replacement.timestamp = 2.1;
    replacement.buttons[index(ControllerButton::North)] = 1.0;
    pads = {replacement};
    ControllerFrame frame = tracker.update(pads, 2.1);
    require(frame.justDisconnected && !frame.connected, "active-pad loss must be reported before a live replacement can claim input");

    frame = tracker.update(pads, 2.2);
    require(!frame.connected, "an input held through active-pad loss must not become the replacement without a fresh press");

    pads[0].buttons.fill(0.0);
    frame = tracker.update(pads, 2.3);
    require(!frame.connected, "replacement should arm only after returning to neutral");
    pads[0].buttons[index(ControllerButton::North)] = 1.0;
    frame = tracker.update(pads, 2.4);
    require(frame.justConnected && frame.controllerIndex == 1, "a fresh post-loss input should establish the replacement cleanly");
}

void syntheticFramesRemainIdentifiableThroughDisconnect()
{
    using namespace rocket;
    ControllerTracker tracker;
    RawControllerSnapshot pad = controller(255);
    pad.synthetic = true;
    pad.id = "Controller Lab Synthetic Gamepad";
    std::vector<RawControllerSnapshot> pads {pad};
    ControllerFrame frame = tracker.update(pads, 1.0);
    require(frame.synthetic && frame.connected, "Controller Lab snapshots should remain marked synthetic");
    pads.clear();
    frame = tracker.update(pads, 1.1);
    require(frame.synthetic && frame.justDisconnected, "synthetic disconnect cleanup must not masquerade as a physical pad loss");
}

void physicalAndSyntheticTrackersRemainIndependent()
{
    using namespace rocket;
    ControllerTracker physicalTracker;
    ControllerTracker syntheticTracker;

    RawControllerSnapshot physical = controller(0);
    physical.leftX = 0.8;
    physical.buttons[index(ControllerButton::RightTrigger)] = 1.0;
    RawControllerSnapshot synthetic = controller(255);
    synthetic.synthetic = true;
    synthetic.id = "Controller Lab Synthetic Gamepad";
    synthetic.buttons[index(ControllerButton::South)] = 1.0;

    std::vector<RawControllerSnapshot> physicalPads {physical};
    std::vector<RawControllerSnapshot> syntheticPads {synthetic};
    ControllerFrame physicalFrame = physicalTracker.update(physicalPads, 3.0);
    ControllerFrame syntheticFrame = syntheticTracker.update(syntheticPads, 3.0);
    require(
        physicalFrame.controllerIndex == 0
            && physicalFrame.isDown(ControllerButton::RightTrigger)
            && physicalFrame.leftX > 0.0,
        "physical campaign input must remain intact while the Lab tracker is active");
    require(
        syntheticFrame.synthetic
            && syntheticFrame.controllerIndex == 255
            && syntheticFrame.wasPressed(ControllerButton::South),
        "synthetic preview input should retain independent edges and identity");

    physicalPads[0].leftX = 0.0;
    physicalPads[0].buttons.fill(0.0);
    physicalFrame = physicalTracker.update(physicalPads, 3.1);
    syntheticFrame = syntheticTracker.update(syntheticPads, 3.1);
    require(
        physicalFrame.wasReleased(ControllerButton::RightTrigger)
            && physicalFrame.leftX == 0.0,
        "physical release cleanup must still be sampled while synthetic input is held");
    require(
        syntheticFrame.isDown(ControllerButton::South)
            && !syntheticFrame.wasPressed(ControllerButton::South),
        "synthetic hold state must not be reset by physical campaign changes");
}

void triggerUsesHysteresis()
{
    using namespace rocket;
    ControllerTracker tracker;
    std::vector<RawControllerSnapshot> pads {controller()};
    tracker.update(pads, 0.0);
    pads[0].buttons[index(ControllerButton::RightTrigger)] = 0.36;
    ControllerFrame frame = tracker.update(pads, 0.1);
    require(frame.wasPressed(ControllerButton::RightTrigger), "trigger should engage at the press threshold");
    pads[0].buttons[index(ControllerButton::RightTrigger)] = 0.25;
    frame = tracker.update(pads, 0.2);
    require(frame.isDown(ControllerButton::RightTrigger), "trigger should remain held above the release threshold");
    pads[0].buttons[index(ControllerButton::RightTrigger)] = 0.19;
    frame = tracker.update(pads, 0.3);
    require(frame.wasReleased(ControllerButton::RightTrigger), "trigger should release below the release threshold");
}

void navigationRepeatUsesRealTime()
{
    using namespace rocket;
    ControllerTracker tracker;
    std::vector<RawControllerSnapshot> pads {controller()};
    pads[0].leftX = 0.8;
    ControllerFrame frame = tracker.update(pads, 2.0);
    require(frame.navigation == UiDirection::Right && !frame.navigationRepeated, "stick engage should navigate immediately");
    require(frame.meaningfulActivity, "initial stick engagement should be fresh controller activity");
    frame = tracker.update(pads, 2.34);
    require(!frame.navigation, "navigation should wait for the initial repeat delay");
    require(!frame.meaningfulActivity, "a held stick must not continuously reclaim the active source");
    frame = tracker.update(pads, 2.35);
    require(frame.navigation == UiDirection::Right && frame.navigationRepeated, "navigation should repeat at 350ms");
    frame = tracker.update(pads, 2.47);
    require(frame.navigation == UiDirection::Right && frame.navigationRepeated, "navigation should repeat every 120ms");
    pads[0].leftX = 0.2;
    frame = tracker.update(pads, 2.5);
    require(!frame.navigation, "releasing below hysteresis should stop repeat");
}

void mostRecentMeaningfulControllerWins()
{
    using namespace rocket;
    ControllerTracker tracker;
    RawControllerSnapshot first = controller(0);
    RawControllerSnapshot second = controller(2);
    first.timestamp = 10.0;
    first.buttons[index(ControllerButton::South)] = 1.0;
    second.timestamp = 12.0;
    second.buttons[index(ControllerButton::West)] = 1.0;
    std::vector<RawControllerSnapshot> pads {first, second};
    ControllerFrame frame = tracker.update(pads, 1.0);
    require(frame.controllerIndex == 2, "most recently active controller should claim input");

    second.buttons.fill(0.0);
    first.buttons.fill(0.0);
    frame = tracker.update(pads, 1.1);
    require(frame.controllerIndex == 2, "neutral pads should not steal active-controller ownership");
}

void staleHeldPadCannotReclaimOwnership()
{
    using namespace rocket;
    ControllerTracker tracker;
    RawControllerSnapshot first = controller(0);
    RawControllerSnapshot second = controller(1);

    first.timestamp = 1.0;
    first.buttons[index(ControllerButton::RightTrigger)] = 1.0;
    std::vector<RawControllerSnapshot> pads {first, second};
    ControllerFrame frame = tracker.update(pads, 1.0);
    require(frame.controllerIndex == 0 && frame.wasPressed(ControllerButton::RightTrigger),
        "the first active pad should establish its held trigger");

    pads[1].timestamp = 2.0;
    pads[1].buttons[index(ControllerButton::South)] = 1.0;
    frame = tracker.update(pads, 2.0);
    require(frame.controllerIndex == 1 && frame.wasPressed(ControllerButton::South),
        "newer meaningful activity should transfer ownership to the second pad");

    pads[1].timestamp = 3.0;
    pads[1].buttons.fill(0.0);
    frame = tracker.update(pads, 3.0);
    require(frame.controllerIndex == 1 && !frame.isDown(ControllerButton::RightTrigger),
        "an older trigger still held on the first pad must not reclaim ownership");
    require(!frame.wasPressed(ControllerButton::RightTrigger),
        "a stale non-active hold must never be recreated as a fresh trigger press");

    pads[0].timestamp = 4.0;
    pads[0].buttons.fill(0.0);
    tracker.update(pads, 4.0);
    pads[0].timestamp = 5.0;
    pads[0].buttons[index(ControllerButton::RightTrigger)] = 1.0;
    frame = tracker.update(pads, 5.0);
    require(frame.controllerIndex == 0 && frame.wasPressed(ControllerButton::RightTrigger),
        "a fresh newer press should allow the first pad to reclaim ownership cleanly");
}

void sourceArbitrationRequiresMeaningfulActivity()
{
    using namespace rocket;
    InputSourceArbiter arbiter;
    arbiter.noteActivity(InputSource::KeyboardPointer, 1.0);
    require(arbiter.activeSource() == InputSource::KeyboardPointer, "keyboard activity should claim the active source");
    arbiter.noteActivity(InputSource::Controller, 2.0, false);
    require(arbiter.activeSource() == InputSource::KeyboardPointer, "neutral controller state should not steal input");
    arbiter.noteActivity(InputSource::Controller, 3.0, true);
    require(arbiter.activeSource() == InputSource::Controller && arbiter.shouldApply(InputSource::Controller), "meaningful controller activity should claim input");

    arbiter.noteActivity(InputSource::KeyboardPointer, 4.0, true);
    require(arbiter.activeSource() == InputSource::KeyboardPointer, "a newer keyboard event should take over while controller input remains held");
    arbiter.noteActivity(InputSource::Controller, 5.0, false);
    require(arbiter.activeSource() == InputSource::KeyboardPointer, "continuously held controller state should not reclaim the source");
    arbiter.noteActivity(InputSource::Controller, 6.0, true);
    require(arbiter.activeSource() == InputSource::Controller, "a fresh controller edge should take the source back");

    arbiter.noteActivity(InputSource::KeyboardPointer, 5.5, true);
    require(arbiter.activeSource() == InputSource::Controller, "late polling of an older activity timestamp must not change the source");
}

void sourceArbitrationTiesIgnoreCallOrder()
{
    using namespace rocket;
    InputSourceArbiter keyboardFirst;
    keyboardFirst.noteActivity(InputSource::KeyboardPointer, 10.0, true);
    keyboardFirst.noteActivity(InputSource::Controller, 10.0, true);

    InputSourceArbiter controllerFirst;
    controllerFirst.noteActivity(InputSource::Controller, 10.0, true);
    controllerFirst.noteActivity(InputSource::KeyboardPointer, 10.0, true);

    require(
        keyboardFirst.activeSource() == InputSource::KeyboardPointer
            && controllerFirst.activeSource() == InputSource::KeyboardPointer,
        "equal activity timestamps must resolve deterministically rather than by call order");
}

void resumeSafetyPredicateMatchesPauseContract()
{
    using namespace rocket;
    require(controllerResumeBlocked(PauseReason::ControllerDisconnected, false, false),
        "a disconnected-controller pause must remain blocked until a controller reconnects");
    require(controllerResumeBlocked(PauseReason::ControllerDisconnected, true, true),
        "a reconnected controller must return to neutral before Resume unlocks");
    require(!controllerResumeBlocked(PauseReason::ControllerDisconnected, true, false),
        "a connected neutral controller should unlock explicit Resume");

    require(controllerResumeBlocked(PauseReason::PageHidden, false, true),
        "a visibility-loss pause should wait for its neutral-release latch");
    require(!controllerResumeBlocked(PauseReason::PageHidden, false, false),
        "visibility recovery must not require a controller that was never active");
    require(!controllerResumeBlocked(PauseReason::SystemMenu, false, true)
            && !controllerResumeBlocked(PauseReason::BlockingModal, false, true)
            && !controllerResumeBlocked(PauseReason::ControllerUiFocus, false, true)
            && !controllerResumeBlocked(PauseReason::None, false, true),
        "ordinary pause reasons should not inherit disconnect resume restrictions");
}

void controllerUiFocusKeepsAutonomousLaunchMoving()
{
    using namespace rocket;
    require(!controllerPauseStopsSimulation(PauseReason::None, InputContext::Launch, false),
        "an unpaused launch should keep simulating");
    require(!controllerPauseStopsSimulation(PauseReason::ControllerUiFocus, InputContext::Launch, false),
        "D-pad flight-control focus must not silently freeze an autonomous launch");
    require(controllerPauseStopsSimulation(PauseReason::ControllerUiFocus, InputContext::FlybyActive, false)
            && controllerPauseStopsSimulation(PauseReason::ControllerUiFocus, InputContext::OrbitActive, false)
            && controllerPauseStopsSimulation(PauseReason::ControllerUiFocus, InputContext::MiningActive, false),
        "steering and drilling contexts should retain their safety pause while UI focus is active");
    require(controllerPauseStopsSimulation(PauseReason::ControllerUiFocus, InputContext::Launch, true)
            && controllerPauseStopsSimulation(PauseReason::SystemMenu, InputContext::Launch, true)
            && controllerPauseStopsSimulation(PauseReason::BlockingModal, InputContext::Launch, true)
            && controllerPauseStopsSimulation(PauseReason::ControllerDisconnected, InputContext::Launch, true)
            && controllerPauseStopsSimulation(PauseReason::PageHidden, InputContext::Launch, true),
        "visible and safety pauses must still stop launch simulation");
}

void launchBindingsOverrideCockpitFocus()
{
    using namespace rocket;
    require(resolvedControllerInputContext(
                InputContext::Launch,
                PauseReason::ControllerUiFocus,
                false)
            == InputContext::Launch,
        "D-pad cockpit focus must not replace active launch bindings with generic UI controls");
    require(resolvedControllerInputContext(
                InputContext::Preflight,
                PauseReason::ControllerUiFocus,
                false)
            == InputContext::Preflight,
        "stale menu focus must not replace the preflight launch binding");
    require(resolvedControllerInputContext(
                InputContext::Launch,
                PauseReason::ControllerUiFocus,
                true)
            == InputContext::Paused,
        "a real launch modal must still take priority over gameplay bindings");
    require(resolvedControllerInputContext(
                InputContext::Launch,
                PauseReason::SystemMenu,
                true)
            == InputContext::Paused,
        "the explicit system menu must retain its blocking input context");

    GameInputRouter router;
    ControllerPreferences preferences;
    ControllerFrame frame;
    frame.connected = true;
    frame.meaningfulInput = true;
    frame.navigation = UiDirection::Left;
    frame.pressed.set(index(ControllerButton::DpadLeft));
    router.route(InputContext::Launch, frame, preferences);

    frame = {};
    frame.connected = true;
    frame.meaningfulInput = true;
    frame.pressed.set(index(ControllerButton::South));
    const RoutedGameInput input = router.route(InputContext::Launch, frame, preferences);
    require(input.has(GameInputAction::ReturnHome) && !input.has(GameInputAction::ActivateFocused),
        "Cross/South must return during launch even after cockpit focus navigation");

    frame = {};
    frame.connected = true;
    frame.meaningfulInput = true;
    frame.down.set(index(ControllerButton::South));
    const RoutedGameInput heldInput = router.route(InputContext::Launch, frame, preferences);
    require(!heldInput.has(GameInputAction::ReturnHome),
        "a held Cross/South button must not restart the return action every frame");

    const RoutedGameInput outcomeHeldInput = router.route(InputContext::Ui, frame, preferences);
    require(!outcomeHeldInput.has(GameInputAction::ActivateFocused),
        "a held Return confirm must be released before it can acknowledge a launch outcome");

    frame = {};
    frame.connected = true;
    router.route(InputContext::Ui, frame, preferences);
    frame.pressed.set(index(ControllerButton::South));
    const RoutedGameInput outcomeConfirmInput = router.route(InputContext::Ui, frame, preferences);
    require(outcomeConfirmInput.has(GameInputAction::ActivateFocused),
        "a fresh confirm press should acknowledge the launch outcome after release");

    router.reset();
    frame = {};
    frame.connected = true;
    frame.meaningfulInput = true;
    frame.navigation = UiDirection::Right;
    frame.pressed.set(index(ControllerButton::DpadRight));
    RoutedGameInput preflightInput = router.route(InputContext::Preflight, frame, preferences);
    require(!preflightInput.navigation && !preflightInput.has(GameInputAction::EnterUiFocus),
        "D-pad input must not enter menu focus during preflight");
    frame = {};
    frame.connected = true;
    frame.meaningfulInput = true;
    frame.pressed.set(index(ControllerButton::South));
    preflightInput = router.route(InputContext::Preflight, frame, preferences);
    require(preflightInput.has(GameInputAction::StartOrContinue) && !preflightInput.has(GameInputAction::ActivateFocused),
        "Cross/South must always start or queue launch during preflight");
}

void forcedMiningFailureRetainsItsDedicatedControllerContext()
{
    using namespace rocket;
    require(resolvedControllerInputContext(
                InputContext::MiningFailure,
                PauseReason::BlockingModal,
                true)
            == InputContext::MiningFailure,
        "an auto-open mining failure must keep its direct South-button recovery action");
    require(resolvedControllerInputContext(
                InputContext::MiningActive,
                PauseReason::BlockingModal,
                true)
            == InputContext::Paused,
        "ordinary realtime gameplay must remain paused behind a blocking modal");
    require(resolvedControllerInputContext(
                InputContext::MiningFailure,
                PauseReason::ControllerDisconnected,
                true)
            == InputContext::Paused,
        "the direct failure action must not bypass a controller-loss safety pause");
    require(resolvedControllerInputContext(InputContext::MiningActive, PauseReason::None, true)
            == InputContext::Ui,
        "an ordinary modal should route controller input through UI focus");
    require(resolvedControllerInputContext(InputContext::MiningActive, PauseReason::None, false)
            == InputContext::MiningActive,
        "unpaused mining should retain its realtime controller context");
}

rocket::ControllerFrame routedFrame()
{
    rocket::ControllerFrame frame;
    frame.connected = true;
    frame.meaningfulInput = true;
    return frame;
}

void routerMapsEveryGameplayContext()
{
    using namespace rocket;
    GameInputRouter router;
    ControllerPreferences preferences;

    ControllerFrame frame = routedFrame();
    frame.pressed.set(index(ControllerButton::South));
    frame.navigation = UiDirection::Down;
    frame.rightY = 0.75;
    RoutedGameInput input = router.route(InputContext::Ui, frame, preferences);
    require(input.has(GameInputAction::ActivateFocused), "South should activate focused UI controls");
    require(input.navigation == UiDirection::Down && input.scroll == 0.75, "UI routing should preserve navigation and right-stick scrolling");

    router.reset();
    frame = routedFrame();
    frame.pressed.set(index(ControllerButton::South));
    input = router.route(InputContext::Preflight, frame, preferences);
    require(input.has(GameInputAction::StartOrContinue), "South should launch from preflight");

    router.reset();
    frame = routedFrame();
    frame.pressed.set(index(ControllerButton::West));
    frame.pressed.set(index(ControllerButton::North));
    input = router.route(InputContext::Launch, frame, preferences);
    require(input.has(GameInputAction::ToggleEngines), "West should toggle engines during launch");
    require(input.has(GameInputAction::TogglePressureRelief), "North should toggle pressure relief during launch");

    router.reset();
    frame = routedFrame();
    frame.leftX = 0.4;
    frame.leftY = -0.6;
    input = router.route(InputContext::FlybyActive, frame, preferences);
    require(std::abs(input.moveX - 0.4) < 0.000001 && std::abs(input.moveY - 0.6) < 0.000001, "flyby should route both left-stick axes");
    preferences.invertFlightY = true;
    input = router.route(InputContext::OrbitActive, frame, preferences);
    require(std::abs(input.moveY + 0.6) < 0.000001, "invert-flight preference should reverse routed vertical thrust");
    preferences.invertFlightY = false;

    router.reset();
    frame = routedFrame();
    frame.pressed.set(index(ControllerButton::South));
    frame.pressed.set(index(ControllerButton::West));
    input = router.route(InputContext::SurfaceScan, frame, preferences);
    require(input.has(GameInputAction::PrimarySurfaceAction) && input.has(GameInputAction::BankSurfaceAction), "surface contexts should route primary and bank actions");

    router.reset();
    frame = routedFrame();
    frame.leftX = -0.5;
    frame.leftY = 0.25;
    frame.down.set(index(ControllerButton::RightTrigger));
    frame.pressed.set(index(ControllerButton::West));
    frame.pressed.set(index(ControllerButton::North));
    frame.pressed.set(index(ControllerButton::LeftBumper));
    frame.pressed.set(index(ControllerButton::RightBumper));
    input = router.route(InputContext::MiningActive, frame, preferences);
    require(input.drilling && input.moveX == -0.5 && input.moveY == 0.25, "mining should route movement and held RT drilling");
    require(input.has(GameInputAction::MiningScan) && input.has(GameInputAction::MiningTether), "mining should route scanner and tether edges");
    require(input.has(GameInputAction::MiningRepairDrill) && input.has(GameInputAction::MiningRepairRig), "mining should route both service bumpers");

    router.reset();
    frame = routedFrame();
    frame.pressed.set(index(ControllerButton::South));
    input = router.route(InputContext::MiningFailure, frame, preferences);
    require(input.has(GameInputAction::MiningFailureAcknowledge), "South should acknowledge mining failure");
}

void routerTableCoversEveryInputContext()
{
    using namespace rocket;
    struct ContextExpectation {
        InputContext context;
        GameInputAction southAction;
        bool southShouldAct;
    };
    const std::array<ContextExpectation, 14> expectations {{
        {InputContext::Ui, GameInputAction::ActivateFocused, true},
        {InputContext::Preflight, GameInputAction::StartOrContinue, true},
        {InputContext::Launch, GameInputAction::ReturnHome, true},
        {InputContext::FlybyActive, GameInputAction::StartOrContinue, false},
        {InputContext::FlybyComplete, GameInputAction::StartOrContinue, true},
        {InputContext::OrbitActive, GameInputAction::StartOrContinue, false},
        {InputContext::OrbitComplete, GameInputAction::StartOrContinue, true},
        {InputContext::SurfaceScan, GameInputAction::PrimarySurfaceAction, true},
        {InputContext::SurfacePush, GameInputAction::PrimarySurfaceAction, true},
        {InputContext::MiningActive, GameInputAction::MiningStow, true},
        {InputContext::MiningService, GameInputAction::MiningStow, true},
        {InputContext::MiningFailure, GameInputAction::MiningFailureAcknowledge, true},
        {InputContext::Stamp, GameInputAction::StartOrContinue, true},
        {InputContext::Paused, GameInputAction::ActivateFocused, true},
    }};

    ControllerPreferences preferences;
    for (const ContextExpectation& expectation : expectations) {
        GameInputRouter router;
        ControllerFrame frame = routedFrame();
        frame.pressed.set(index(ControllerButton::South));
        RoutedGameInput input = router.route(expectation.context, frame, preferences);
        require(input.has(expectation.southAction) == expectation.southShouldAct,
            "the context table should preserve each South-button contract");

        router.reset();
        frame = routedFrame();
        frame.pressed.set(index(ControllerButton::Menu));
        input = router.route(expectation.context, frame, preferences);
        require(input.has(GameInputAction::OpenSystemMenu),
            "Menu should remain available in every input context");
    }
}

void routerContextualFocusPreservesDedicatedBindings()
{
    using namespace rocket;
    GameInputRouter router;
    ControllerPreferences preferences;
    preferences.swapConfirmCancel = true;

    ControllerFrame frame = routedFrame();
    frame.pressed.set(index(ControllerButton::South));
    RoutedGameInput input = router.route(InputContext::Preflight, frame, preferences);
    require(input.has(GameInputAction::StartOrContinue) && !input.has(GameInputAction::ActivateFocused),
        "preflight South should keep its fixed launch binding before UI navigation");

    frame = routedFrame();
    frame.navigation = UiDirection::Right;
    frame.pressed.set(index(ControllerButton::DpadRight));
    router.route(InputContext::Preflight, frame, preferences);
    frame = routedFrame();
    frame.pressed.set(index(ControllerButton::South));
    input = router.route(InputContext::Preflight, frame, preferences);
    require(input.has(GameInputAction::StartOrContinue) && !input.has(GameInputAction::ActivateFocused),
        "preflight South should keep its launch binding after D-pad input");

    router.reset();
    frame = routedFrame();
    frame.navigation = UiDirection::Down;
    frame.pressed.set(index(ControllerButton::DpadDown));
    frame.pressed.set(index(ControllerButton::South));
    input = router.route(InputContext::Stamp, frame, preferences);
    require(input.has(GameInputAction::ActivateFocused) && !input.has(GameInputAction::StartOrContinue),
        "mission-stamp D-pad navigation should make South activate the focused control");

    router.reset();
    frame = routedFrame();
    frame.pressed.set(index(ControllerButton::South));
    input = router.route(InputContext::SurfaceScan, frame, preferences);
    require(input.has(GameInputAction::PrimarySurfaceAction) && !input.has(GameInputAction::ActivateFocused),
        "surface scan South should keep its fixed pulse binding before UI navigation");

    frame = routedFrame();
    frame.navigation = UiDirection::Left;
    frame.pressed.set(index(ControllerButton::DpadLeft));
    router.route(InputContext::SurfaceScan, frame, preferences);
    frame = routedFrame();
    frame.pressed.set(index(ControllerButton::South));
    input = router.route(InputContext::SurfaceScan, frame, preferences);
    require(input.has(GameInputAction::ActivateFocused) && !input.has(GameInputAction::PrimarySurfaceAction),
        "surface scan South should activate the D-pad-focused action");

    router.reset();
    frame = routedFrame();
    frame.pressed.set(index(ControllerButton::South));
    input = router.route(InputContext::SurfacePush, frame, preferences);
    require(input.has(GameInputAction::PrimarySurfaceAction) && !input.has(GameInputAction::ActivateFocused),
        "surface push South should keep its fixed push binding before UI navigation");

    frame = routedFrame();
    frame.navigation = UiDirection::Right;
    frame.pressed.set(index(ControllerButton::DpadRight));
    router.route(InputContext::SurfacePush, frame, preferences);
    frame = routedFrame();
    frame.down.set(index(ControllerButton::East));
    frame.pressed.set(index(ControllerButton::East));
    input = router.route(InputContext::SurfacePush, frame, preferences);
    require(input.has(GameInputAction::CancelFocused) && !input.has(GameInputAction::Abort),
        "East should leave surface UI focus without aborting the run");
    frame.pressed.reset();
    frame.heldSeconds[index(ControllerButton::East)] = 0.45;
    input = router.route(InputContext::SurfacePush, frame, preferences);
    require(!input.has(GameInputAction::Abort),
        "the East press used to leave UI focus must stay suppressed until release");
    frame.down.reset(index(ControllerButton::East));
    frame.heldSeconds[index(ControllerButton::East)] = 0.0;
    router.route(InputContext::SurfacePush, frame, preferences);
    frame.down.set(index(ControllerButton::East));
    frame.heldSeconds[index(ControllerButton::East)] = 0.45;
    input = router.route(InputContext::SurfacePush, frame, preferences);
    require(input.has(GameInputAction::Abort),
        "a fresh East hold should retain the fixed surface abort binding outside UI focus");
}

void routerHonorsConfirmSwapAndRealTimeHolds()
{
    using namespace rocket;
    GameInputRouter router;
    ControllerPreferences preferences;
    preferences.swapConfirmCancel = true;

    ControllerFrame frame = routedFrame();
    frame.pressed.set(index(ControllerButton::East));
    RoutedGameInput input = router.route(InputContext::Ui, frame, preferences);
    require(input.has(GameInputAction::ActivateFocused), "confirm/cancel swap should make East activate in UI");
    frame = routedFrame();
    frame.pressed.set(index(ControllerButton::South));
    input = router.route(InputContext::Ui, frame, preferences);
    require(input.has(GameInputAction::CancelFocused), "confirm/cancel swap should make South cancel in UI");

    preferences.swapConfirmCancel = false;
    router.reset();
    frame = routedFrame();
    frame.down.set(index(ControllerButton::South));
    frame.heldSeconds[index(ControllerButton::South)] = 0.74;
    input = router.route(InputContext::Ui, frame, preferences, 0.75);
    require(!input.has(GameInputAction::ActivateFocused), "hold-confirm controls should ignore an early South press");
    frame.heldSeconds[index(ControllerButton::South)] = 0.75;
    input = router.route(InputContext::Ui, frame, preferences, 0.75);
    require(input.has(GameInputAction::ActivateFocused), "reset confirmation should activate after 750ms of unscaled hold time");
    frame.heldSeconds[index(ControllerButton::South)] = 1.50;
    input = router.route(InputContext::Ui, frame, preferences, 0.75);
    require(!input.has(GameInputAction::ActivateFocused), "hold-confirm should fire only once before release");

    router.reset();
    frame = routedFrame();
    frame.down.set(index(ControllerButton::East));
    frame.heldSeconds[index(ControllerButton::East)] = 0.44;
    input = router.route(InputContext::FlybyActive, frame, preferences);
    require(!input.has(GameInputAction::Abort), "abort hold should not fire before 450ms");
    frame.heldSeconds[index(ControllerButton::East)] = 0.45;
    input = router.route(InputContext::FlybyActive, frame, preferences);
    require(input.has(GameInputAction::Abort), "abort hold should fire at 450ms of unscaled hold time");
    frame.heldSeconds[index(ControllerButton::East)] = 4.50;
    input = router.route(InputContext::FlybyActive, frame, preferences);
    require(!input.has(GameInputAction::Abort), "a held action should fire only once");

    input = router.route(InputContext::Launch, frame, preferences);
    require(!input.has(GameInputAction::Eject), "a hold carried into another context must remain suppressed until release");
    frame.down.reset(index(ControllerButton::East));
    frame.heldSeconds[index(ControllerButton::East)] = 0.0;
    router.route(InputContext::Launch, frame, preferences);
    frame.down.set(index(ControllerButton::East));
    frame.heldSeconds[index(ControllerButton::East)] = 0.75;
    input = router.route(InputContext::Launch, frame, preferences);
    require(input.has(GameInputAction::Eject), "a fresh 750ms East hold should eject during launch");
}

} // namespace

int main()
{
    familyDetectionAndOverrides();
    radialDeadzoneRescales();
    buttonEdgesHoldsAndDisconnect();
    activePadLossDoesNotFallBackToNeutralPad();
    activePadLossWinsOverSameFrameReplacementInput();
    syntheticFramesRemainIdentifiableThroughDisconnect();
    physicalAndSyntheticTrackersRemainIndependent();
    triggerUsesHysteresis();
    navigationRepeatUsesRealTime();
    mostRecentMeaningfulControllerWins();
    staleHeldPadCannotReclaimOwnership();
    sourceArbitrationRequiresMeaningfulActivity();
    sourceArbitrationTiesIgnoreCallOrder();
    resumeSafetyPredicateMatchesPauseContract();
    controllerUiFocusKeepsAutonomousLaunchMoving();
    launchBindingsOverrideCockpitFocus();
    forcedMiningFailureRetainsItsDedicatedControllerContext();
    routerMapsEveryGameplayContext();
    routerTableCoversEveryInputContext();
    routerContextualFocusPreservesDedicatedBindings();
    routerHonorsConfirmSwapAndRealTimeHolds();
    std::cout << "Controller input tests passed.\n";
    return 0;
}
