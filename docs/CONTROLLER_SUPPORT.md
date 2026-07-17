# Controller Support

Rocket Rogue's native Windows/Linux and web builds share one controller-complete player-facing input path. Web gamepads must expose the W3C `standard` mapping; native builds use SDL's standard gamepad mapping. Xbox pads, DualShock 4, DualSense, and Steam Deck controls resolve into the same portable snapshots, prompt families, and semantic actions. Steam Input, gyro, touchpads, rear paddles, multiplayer, and full binding remaps remain separate platform work.

## Architecture contract

- `src/input/ControllerInput.*` owns portable controller types, radial deadzones, trigger hysteresis, button edges, hold timing, navigation repeat, active-pad selection, prompt-family detection, and input-source arbitration.
- `src/platform/web/WebGamepadSource.*` maps Emscripten gamepads into `RawControllerSnapshot`; `src/platform/sdl/SdlPlatform.*` does the same for SDL gamepads and owns native connect/disconnect lifecycle and rumble.
- `GameRunner::frame` samples the active `IControllerSource` exactly once per host frame before scaled fixed simulation steps. `RocketGameApp::inputFrame` resolves the authoritative context and applies semantic actions. Hold and repeat clocks use unscaled platform monotonic time, so changing game speed never changes a controller gesture.
- `GameRmlUi` owns stable semantic focus IDs, spatial navigation, activation, cancellation, modal focus traps, scrolling, and focus restoration across document rebuilds. The browser fallback mirrors the same operations.
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
| Preflight, fanfare, results | South launches or continues. During drone transfer, South queues launch and the burn begins automatically when the bay seals. |
| Active launch | South immediately starts Return; hold East 0.75 s to Eject; West toggles engines; North opens or closes pressure relief; hold RB 0.45 s to jettison cargo. |
| Flyby | Left stick steers and throttles; South continues after completion; hold East 0.45 s to abort. |
| Orbit | Left stick supplies radial and tangential thrust; South continues after completion; hold East 0.45 s to abort. |
| Surface Scan / Push Deeper | South pulses or pushes; West banks; hold East 0.45 s to abort. Screen actions remain spatially focusable. |
| Mining | Left stick moves and faces the rig; RT drills; West scans; North tethers; South stows at the ship or acknowledges failure; LB repairs the drill; RB repairs the rig; hold East 0.45 s for emergency recall. |
| Real-time UI access | Preflight and active launch never enter D-pad UI focus: South launches or returns, and dedicated flight buttons stay authoritative. Options/Menu explicitly opens the system menu. Steering and drilling contexts still use D-pad UI focus and pause. Gameplay input remains suppressed while a modal is open. |

The mining drill stays forward-facing and drone combat remains passive. There is no weapon aim or twin-stick drill aim.

## Focus and pause rules

- Navigation uses rendered control rectangles in four directions without wrapping. Hidden, disabled, and decorative controls are skipped.
- Stable focus identity comes from the action and item ID, never from visible copy or an element pointer.
- Focus survives per-frame Rml document rebuilds. If the prior target disappears, focus moves to the nearest enabled control in its region, then to the screen default.
- Modals trap focus and return it to their opener. Mining Failure behaves as a blocking modal even when it opens automatically; South acknowledges it directly even if a document rebuild has not restored focus yet.
- Select controls change with left/right; South toggles checkboxes. Focus automatically scrolls into view.
- System menus, blocking modals, host focus/visibility loss, and active-controller loss pause real-time simulation and clear gameplay axes, thrust, and drilling immediately. Reconnection requires an observed neutral frame followed by explicit Resume.
- Entering preflight clears prior menu focus. Preflight and active launch do not expose D-pad UI focus: Cross/South always launches, queues launch, or returns according to the flight phase. Opening a modal or the system menu still pauses and takes input priority.
- A neutral connected controller does not steal the active source or release a held keyboard input.

## Input tuning

- Radial stick deadzone: configurable, default 0.20.
- Menu engage/release hysteresis: 0.55 / 0.35.
- Navigation repeat: 350 ms initial delay, then 120 ms.
- Trigger press/release hysteresis: 0.35 / 0.20.
- Launch reset-save confirmation: 0.75 s hold, with Cancel focused by default.
- Optional haptics: confirmation, hard mining contact, damage, and failure. Unsupported SDL devices and browser actuators are silent no-ops.

## Verification matrix

Automated checks cover deadzones, trigger hysteresis, button edges, holds, repeats, active-pad selection, source arbitration, prompt detection/override, and disconnect release. Player-route verification should cover Hangar, Navigation, Launch, Results, Arrival, Flyby, Orbit, Research, Surface Ops, Scan, Push Deeper, Mining, Drone Ops, both drafts, Settings, Map, Inventory, and failure/confirmation modals.

Before release, perform physical passes with Xbox, DualShock 4, DualSense, and Steam Deck on native Windows/Linux plus the web build at localhost and production HTTPS. Include disconnect/reconnect while moving and while holding RT to drill. Check RmlUi prompt and focus layout at 1280x800, 1080p, 1440p, and 4K on native and web; run the forced DOM fallback matrix on web only.

Developer forms remain mouse/keyboard tools. The debug Controller Lab is for inspecting devices, axes, buttons, resolved context, focus, actions, pause state, and deterministic synthetic input. Synthetic frames use a separate preview-only router: they may move focus and report the semantic action that would fire, but they never dispatch gameplay actions or touch campaign saves.
