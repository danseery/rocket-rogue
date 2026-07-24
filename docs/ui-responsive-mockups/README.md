# Zero-Overlap Responsive UI Mockups

These mockups define the layout families for OREBIT's responsive UI pass. They preserve the shipped dark cockpit identity and existing game artwork while reducing persistent information. They are design references, not replacement game art and not runtime golden captures.

The invariant is simple: persistent UI, its scroll region, and its hit targets stay entirely outside `sceneRect`. Only an intentional modal shown by the player or a mandatory paused-state dialog may cover the scene.

## Deliverables

| File | Logical viewport | Layout family |
| --- | ---: | --- |
| `01-management-rail-1280x800.png` | 1280x800 | Hangar, Navigation, Research, and Drone Ops management rail |
| `02-decision-rail-surface-ops-1280x800.png` | 1280x800 | Arrival Ops and Surface Ops decision rail |
| `03-live-flight-hud-1280x800.png` | 1280x800 | Launch, Flyby, and Orbit live HUD |
| `04-surface-scan-controls-1280x800.png` | 1280x800 | Surface Scan and Push controls |
| `05-mining-safe-playfield-hud-1280x800.png` | 1280x800 | Mining status rail, protected terrain, and command rail |
| `06-refit-selector-1280x800.png` | 1280x800 | Refit and Field Upgrade compact offer selectors |
| `07-results-and-modals-1280x800.png` | 1280x800 | Results, arrival stamp, pause, confirmations, and modal treatment |
| `08-surface-ops-1024x768.png` | 1024x768 | Surface Ops stress-case rail |
| `09-surface-ops-1280x720.png` | 1280x720 | Surface Ops minimum-supported rail |
| `10-surface-ops-1920x1080.png` | 1920x1080 | Surface Ops wide-screen rail |
| `11-annotated-layout-contract.png` | 1280x800 | Panel, gutter, scene, HUD-safe, and click-safe rectangles |

The corresponding built-in image-generation prompt set is recorded in `prompt-manifest.md`.

## Production approval captures

These are runtime captures rather than generated mockups. They exercise the same 1280x800 logical layout through native Vulkan/RmlUi, web RmlUi, and the forced DOM fallback:

| File | Runtime path | Screen |
| --- | --- | --- |
| `production/surface-scan-native-1280x800.png` | Native Vulkan/RmlUi | Surface Scan initial benchmark state |
| `production/surface-scan-rmlui-1280x800.png` | Web RmlUi | Surface Scan initial state |
| `production/surface-scan-dom-1280x800.png` | Forced DOM fallback | Surface Scan initial state |
| `production/mining-native-1280x800.png` | Native Vulkan/RmlUi | Mining active benchmark state |
| `production/mining-rmlui-1280x800.png` | Web RmlUi | Mining active state |
| `production/mining-dom-1280x800.png` | Forced DOM fallback | Mining active state |

The production UI bundles Source Code Pro Regular, Semibold, and Italic under `assets/fonts/`; web and native packages include the same files and SIL Open Font License without a CDN dependency.

## Screen-to-template map

`F0` is the full-screen presentation exception: the current Title and Story Briefing presentation remains full-screen, with constrained copy and control guides at short heights.

| Reachable screen or overlay | Primary template | Active, completed, and failure treatment |
| --- | --- | --- |
| Title screen | F0 | Constrain the title stack and guide; never create a persistent side panel. |
| Story Briefing | F0 | Keep one readable copy column and the continuation action inside safe margins. |
| Hangar | 01 | Readiness, resources, destination, and actionable items only; histories and loadouts move to Details. |
| Navigation | 01 | Current destination and available routes remain; forecasts and route explanations move to Details. |
| Research | 01 | Current research, resources, and actionable unlocks remain; history and full tree explanation move to Details. |
| Drone Ops | 01 | Current task, readiness, and immediate commands remain; loadout and mission history move to Details. |
| Arrival Ops | 02 | Compact title, cost, risk/reward, and action rows. |
| Surface Expedition / Surface Ops | 02 | Compact action rows; briefing, logs, and forecasts move to drill-ins. |
| Launch | 03 | Objective/progress, three live gauges, and at most two immediate actions. Failure becomes template 07. |
| Flyby | 03 | Active run uses the live HUD; completion becomes a compact template-07 summary. |
| Orbit | 03 | Active run uses the live HUD; completion becomes a compact template-07 summary. |
| Surface Scan | 04 | Pulses, mapped forecast, bust risk, signal, and the latest layer read. Pulse Scanner maps prospect data only; Bank Forecast prepares the Mining site and returns to Surface Ops. Scan never grants owned resources or progression. |
| Surface Push | 04 | Progress, current reward, current risk, Continue, Bank/Return, and secondary Abort. Completion/failure uses 07. |
| Mining | 05 | Fuel, oxygen, cargo, and risk occupy reserved HUD rails outside the terrain and return route. Failure uses 07. |
| Surface Upgrade | 06 | Three compact selectors plus one selected detail card; complete statistics open in Compare. |
| Upgrade / Refit | 06 | Three compact selectors plus one selected detail card; complete statistics open in Compare. |
| Results | 07 | Outcome, three consequences, and one continuation action; full report and achievements are secondary. |
| Arrival Fanfare | 07 | Content-sized centered arrival stamp with one continuation action. |
| Legacy | 07 | Content-sized drill-in with safe margins rather than persistent chrome. |
| Pause, Map, Inventory, Settings | 07 | Content-sized modal with focus trap and debug overlay suppressed. |
| Confirmations and failure dialogs | 07 | Smallest useful centered dialog, safe margins, one primary and one secondary action. |

## Shared responsive contract

Every renderer and interaction layer consumes the same `UiViewportLayout` result:

- `sceneRect`: the complete renderable game scene.
- `panelRect`: the reserved information rail, bottom dock, or Mining command rail.
- `hudSafeRect`: the inset region for scene-attached controls and hit targets.
- `layoutClass`: `Fullscreen`, `LandscapeRail`, `BottomDock`, or `MiningHud`.

For persistent panels, use a landscape rail when the resulting scene is at least 640 logical pixels wide:

- Rail width: `clamp(round(24vw), 280px, 340px)`.
- Outer margin and gutter: 12px below 1600 logical pixels; 16px at 1600 and above.
- Scene: the remainder of the viewport after margins, rail, and gutter.

If the rail would leave less than 640px, use a bottom dock:

- Dock height: `clamp(round(28vh), 168px, 220px)`.
- Scene: the complete region above the dock and gutter.
- The dock scrolls internally; the scene never moves behind it.

Mining reserves a compact status rail above the terrain and a command rail below it. Both are outside the terrain and its click-safe return path.

## Persistent-information budget

Persistent chrome contains only:

- Screen title and one-line objective.
- Up to four critical metrics.
- Immediate choices and their compact cost/risk/reward cues.
- Compact Map, Inventory, and Menu controls.
- A sticky primary action.

Histories, forecasts, extended telemetry, complete comparisons, full reports, achievements, logs, long explanations, and loadout detail belong in Details or content-sized modals.

## Reference and verification notes

The mockups use the existing Hangar, Surface Ops, Launch, and Mining captures under `build/archive/benchmarks/` as visual references. They do not add story, lore, characters, branding, or replacement art.

The retained July captures predate later UI changes. Refresh native RmlUi, web RmlUi, and forced DOM captures before treating any image as a golden comparison. Required verification viewports are 1024x768, 1280x720, 1280x800, 1920x1080, 2560x1440, and 3840x2160.

For browser fallback verification, append `?force_dom_fallback=1` (or `?ui_renderer=dom`) to `rocket_rogue.html`. The ordinary URL continues to use web RmlUi.
