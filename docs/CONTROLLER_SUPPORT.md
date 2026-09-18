# Controller Support

The [OREBIT Game Design Document](Rocket_Rogue_Game_Design_Document.docx) is the definitive design. This document supplies implementation detail and must agree with it. Story and progression decisions marked TBD are collected in GDD Section 8.

Rocket Rogue's native Windows/Linux and web builds share one controller-complete player-facing input path. Web gamepads must expose the W3C `standard` mapping; native builds use SDL's standard gamepad mapping. Xbox pads, DualShock 4, DualSense, and Steam Deck controls resolve into the same portable snapshots, prompt families, and semantic actions. Steam Input, gyro, touchpads, rear paddles, multiplayer, and full binding remaps remain separate platform work.

## Architecture contract

- `src/input/ControllerInput.*` owns portable controller types, radial deadzones, trigger hysteresis, button edges, hold timing, navigation repeat, active-pad selection, prompt-family detection, and input-source arbitration.
- `src/platform/web/WebGamepadSource.*` maps Emscripten gamepads into `RawControllerSnapshot`; `src/platform/sdl/SdlPlatform.*` does the same for SDL gamepads and owns native connect/disconnect lifecycle and rumble.
- `GameRunner::frame` samples the active `IControllerSource` exactly once per host frame before scaled fixed simulation steps. `RocketGameApp::inputFrame` resolves the authoritative context and applies semantic actions. Hold and repeat clocks use unscaled platform monotonic time, so changing game speed never changes a controller gesture.
- `GameRmlUi` owns stable semantic focus IDs, spatial navigation, activation, cancellation, modal focus traps, scrolling, and focus restoration across document rebuilds. Native and web builds consume the same semantic focus/action model and presentation contract.
- Controller preferences are device-local and separate from campaign saves. Web stores them under `rocket_rogue_controller_preferences_v1`; native stores them in `preferences_v1.txt` beneath the SDL per-user preference path.

The shared controller preference values are shown below. Web serializes this JSON object; native stores the same fields with a `controller.` prefix in `preferences_v1.txt`.

```json
{
  "promptFamily": "auto",
  "stickDeadzone": 0.2,
  "invertFlightY": false,
  "swapConfirmCancel": false,
  "vibrationEnabled": true
}
```

`stickDeadzone` is clamped to 0.10-0.35. Malformed or missing values use the defaults above.

## Default layout

Controller names use positions so the same rule applies to every prompt family: South is A/Cross, East is B/Circle, West is X/Square, and North is Y/Triangle.

| Context | Controls |
|---|---|
| Menus, cards, drafts, settings, modals | Left stick or D-pad navigates; South confirms; East backs out; right stick scrolls; Menu opens the system menu; View opens Map; North opens Inventory outside real-time play. |
| Preflight, fanfare, results | South launches or continues. During Mining Rig transfer, South queues launch and the burn begins automatically when the bay seals. |
| Physical Flight | Right stick left/right rotates proportionally; right-stick vertical input and bumpers do not steer. Left stick left/right strafes relative to the ship; up/down applies proportional forward/reverse thrust. Left-stick click / C toggles cruise; View / M opens the paused system map. Manual steering/thrust cancels cruise. South starts Pulse Survey or holds Orbital Laser Dig when available; East resumes flight from inspection. |
| Touchdown | South deploys; hold East 0.45 s to depart undeployed. The first accepted command owns the sequence. |
| Mining - rig | Left stick moves in screen directions regardless of drill heading. Point the right stick in any direction to aim the drill; center it to hold the current heading. RT drills; West scans; North tethers; release South quickly to stow cargo or leave; hold South 0.6 s to exit; LB repairs the drill; RB repairs the rig; hold East 0.45 s for emergency recall. |
| Mining - EVA | Left stick thrusts; right stick aims independently; RT fires the sidearm; LT hand-drills; West scans; North tethers; release South quickly to stow cargo or leave when valid; hold South 0.6 s to enter the rig; hold East 0.45 s for emergency recall. |
| Real-time UI access | Preflight and active launch never enter D-pad UI focus: South follows the active launch/orbital-work context, and dedicated flight controls remain active outside inspection. Options/Menu explicitly opens the system menu. Steering and drilling contexts still use D-pad UI focus and pause. Gameplay input remains suppressed while a modal is open. |

The Mining Rig drill stays attached to its hull: right-stick aiming smoothly turns the whole rig along the shortest path, subject to existing turning speed, fuel, and terrain restrictions. A warm-yellow circular crosshair follows the requested direction with its center exactly on the rig's persistent scanner ring; centering the stick aligns it with the held heading. EVA uses the same crosshair centered on its own ring. Both are 25 percent smaller than the original EVA reticle and stay at the persistent ring radius during scan pulses; EVA firing briefly brightens the crosshair without changing aim or weapon range. Stick direction selects the heading; deflection does not separately scale turn speed. The existing radial deadzone applies, and an exactly opposite target turns clockwise. EVA uses twin-stick movement and independent aim: the right stick rotates the reticle while RT fires an immediate shot and continues at a 0.18-second cadence. LT is the suit hand drill. Support Drone targeting remains autonomous around the active actor.

In physical Flight, A/D (or Left/Right) rotate and W/S (or Up/Down) apply forward/reverse thrust. Hold either Shift to use A/D (or Left/Right) to strafe without mouse steering; releasing Shift restores keyboard rotation. Q/E have no flight binding. Reverse thrust is not automatic braking. Lateral thrusters have half the main engine acceleration and consume fuel while firing. Space/Enter starts survey or holds the orbital laser when available; Escape resumes flight from inspection. The Land UI action during completed-survey inspection in Zone 1 stops and aligns the ship before gravity resumes. At touchdown, Space/Enter deploys and R departs undeployed. The mining rig turns toward the mouse without holding Shift. WASD or arrows move in screen directions regardless of drill heading; Shift does not change mining movement. Its rotation remains damped and collision-checked. EVA retains WASD/arrows movement and mouse aim; left click fires, right click drills, E scans, T tethers, F switches actor and R performs the contextual ship action. Space uses the Rig drill Toggle/Hold preference. Packing leads into manually controlled ascent.

Holding South displays an `EXIT` or `ENTER` progress ring around the rig. The threshold is exactly 0.6 seconds; releasing before it fires routes through the existing tap action. `F` switches immediately and produces a confirmation pulse.

Native and web input adapters must use the same mining viewport transform for pointer aim. A click consumed by RmlUi cannot also fire or drill, and the browser context menu is suppressed during mining.

## Focus and pause rules

- Navigation uses rendered control rectangles in four directions without wrapping. Hidden, disabled, and decorative controls are skipped.
- Stable focus identity comes from the action and item ID, never from visible copy or an element pointer.
- Focus survives per-frame Rml document rebuilds. If the prior target disappears, focus moves to the nearest enabled control in its region, then to the screen default.
- Modals trap focus and return it to their opener. Mining Failure behaves as a blocking modal even when it opens automatically; South acknowledges it directly even if a document rebuild has not restored focus yet.
- Select controls change with left/right; South toggles checkboxes. Focus automatically scrolls into view.
- System menus, blocking modals, host focus/visibility loss, input-source switching, and active-controller loss pause real-time simulation and immediately clear gameplay axes, aim, thrust, fire, drilling, and operator-toggle progress. Reconnection requires an observed neutral frame followed by explicit Resume.
- Entering preflight clears prior menu focus. Flight and touchdown interpret the primary action by active context; a modal or system menu pauses simulation and takes input priority.
- A neutral connected controller does not steal the active source or release a held keyboard input.

## Input tuning

- Radial stick deadzone: configurable, default 0.20.
- Menu engage/release hysteresis: 0.55 / 0.35.
- Navigation repeat: 350 ms initial delay, then 120 ms.
- Trigger press/release hysteresis: 0.35 / 0.20.
- Mining operator-toggle hold: 0.60 s, with a progress ring and no tap-action dispatch after the hold completes.
- Launch reset-save confirmation: 0.75 s hold, with Cancel focused by default.
- Optional haptics: confirmation, hard mining contact, damage, and failure. Unsupported SDL devices and browser actuators are silent no-ops.

## Verification matrix

Existing input tests cover routing, deadzones, edges/holds, source arbitration, pause and disconnect behavior. Journey verification must cover preflight, physical Flight, orbit survey/laser, Land/manual descent, touchdown, deployment, Mining/EVA, ship service, packing and ascent, plus management and blocking modals. Verify each claimed behavior against the relevant tests; hardware and visual acceptance require direct checks.

Before release, perform physical passes with Xbox, DualShock 4, DualSense, and Steam Deck on native Windows/Linux plus the web build at localhost and production HTTPS. Include disconnect/reconnect and source switching while moving, aiming, firing, drilling, and holding the operator-toggle action. Check RmlUi prompt and focus layout at 1280x800, 1080p, 1440p, and 4K on native and web.

Developer forms remain mouse/keyboard tools. The debug Controller Lab is for inspecting devices, axes, buttons, resolved context, focus, actions, pause state, and deterministic synthetic input. Synthetic frames use a separate preview-only router: they may move focus and report the semantic action that would fire, but they never dispatch gameplay actions or touch campaign saves.

## Missions and first scan

Open Map with View, then select the Missions tab; the HUD also has a labeled Missions button. Use normal menu navigation, Confirm, Back, and right-stick scrolling. Track one discovered mission, with completed entries behind Show completed. Manual exploration waypoints remain selected until Return to mission; tracking never engages cruise. During the first Moon scan, the paused result names the mission sector and explains 20 Common Ore delivered to the ship before locating and recovering the artifact. The correct sector emphasizes Land at mission site, the wrong sector emphasizes Resume flight to mission sector, and orbital drilling remains optional. A held Scan confirm cannot accept the next action: release and press again.

## Incoming Message controls

Incoming Messages use the shared modal focus scope: Accept activates the single acknowledgement button; Cancel/Escape cannot dismiss the message. The card shows context-appropriate controls: scanner E / West, Exit Rig F / hold South, hand drill Space or right mouse / LT, tether T / North. Movement and tool inputs held across acknowledgement must return to neutral before they resume. Simulation and oxygen pause while the card is open; XP selections and physical transitions retain priority.

## Earth launch and dock departure

The first-flight Mission Control card shows current steering and thrust bindings and requires an explicit Ready to launch acknowledgement. The ship remains held at Earth afterward. Activate the focused Launch button with Accept (Enter / controller South), or click it, to begin flight; fresh steering and thrust inputs are required. Earth appears below-left and Moon above-right. Right-stick horizontal input steers; the left stick supplies strafe and forward/reverse thrust with the configured Y inversion. After Depart dock, forward thrust releases the dock; plotting a course never steers or undocks. The selected-target marker, coast trajectory and next-action hint are shared with keyboard/web play.
