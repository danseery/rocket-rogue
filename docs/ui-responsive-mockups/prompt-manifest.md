# Built-in Image Generation Prompt Manifest

Mode: Codex built-in image generator (`image_gen`), one call per final asset. The generated source files remain under the Codex generated-images directory; the selected finals were copied into this project and normalized to the exact logical viewport dimensions in their filenames.

Reference images:

- `build/archive/benchmarks/golden-hangar.png`
- `build/archive/benchmarks/golden-surface-ops.png`
- `build/archive/benchmarks/golden-focus-fixed-launch.png`
- `build/archive/benchmarks/golden-focus-fixed-mining.png`

## Shared constraints supplied to every prompt

Use case: `ui-mockup`.

Create a polished, practical production-game UI mockup for OREBIT. Preserve the supplied game's existing dark cockpit visual identity, typography character, palette, and artwork. Redesign information hierarchy and geometry only. Do not invent or replace story, lore, characters, planets, spacecraft, logos, or art. Use an orthographic flat screen with exact rectangular geometry, not a device mockup or perspective view. Keep the complete game scene visible. Persistent panels, chrome, controls, and hit targets must never cross the protected scene boundary. Use exact short labels, legible typography, no watermark, and no extra decorative copy.

## Final prompt set

### 01 - Management rail, 1280x800

Using the Hangar capture as the visual reference, show a compact 280-340px left management rail and a separate complete hangar scene. Exact labels: `OREBIT`, `HANGAR`, `MAP`, `INV`, `MENU`, `Ready for launch`, `FUEL`, `KITS`, `CARGO`, `RISK`, `DESTINATION`, `DETAILS`, and `LAUNCH`. Keep one-line readiness, four metrics, destination/current task, concise actionable rows, and one sticky primary action. Remove duplicate summaries, histories, forecasts, long loadout copy, and full explanations from the persistent rail.

### 02 - Arrival/Surface decision rail, 1280x800

Using the Surface Ops capture as the visual reference, show a compact left decision rail and a separate complete planetary scene. Exact labels: `OREBIT`, `Surface Ops`, `MAP`, `INV`, `MENU`, `Choose one surface action`, `FUEL`, `KITS`, `CARGO`, `RISK`, `Mine deposit`, `SHARED FUEL`, `Survey site`, `READ +2`, `Push deeper`, `DEPTH +1`, `Return to Earth`, `RISK -16%`, `DRONE OPS`, and `RETURN TO EARTH`. Each choice is one compact row with title, cost or risk/reward cue, and `GO` action. No persistent briefing, log, forecast, or long description.

### 03 - Launch/Flyby/Orbit live HUD, 1280x800

Using the Launch capture as the visual reference, show a compact left live-flight rail and a separate complete launch scene. Exact labels: `OREBIT`, `LAUNCH`, `MAP`, `INV`, `MENU`, `Reach stable orbit`, `ALTITUDE`, `VELOCITY`, `PRESSURE`, `Stage 1`, `BOOST`, `VENT`, and `ABORT`. Keep one objective/progress line, exactly three live gauges, and no more than two primary immediate actions plus secondary Abort. Move global resources and extended telemetry out of persistent view.

### 04 - Surface Scan/Push controls, 1280x800

Using the existing planetary art as the visual reference, show a compact left scanner rail and a separate complete surface scene. Exact labels: `OREBIT`, `SURVEY SCAN`, `INV`, `MENU`, `Read the layer, then bank`, `PULSES`, `FORECAST`, `RISK`, `SIGNAL`, `NO LAYER READ`, `PULSE SCANNER`, `BANK FORECAST`, and `ABORT`. After a pulse, replace `NO LAYER READ` with a compact `LAYER +N: COMMON/RARE/EXOTIC/ARTIFACT +N` readout. `FORECAST` is mapped cargo value only; scanning never grants owned resources or progression. Keep Pulse Scanner and Bank Forecast immediately visible, with Abort secondary. No logs or explanatory paragraphs.

### 05 - Mining safe-playfield HUD, 1280x800

Using the Mining capture as the visual reference, preserve the terrain and route art. Reserve a compact top status rail and bottom command rail entirely outside the rectangular playable terrain. Exact labels: `ACT • LEVEL`, `Scan, drill, bank, return`, `OXYGEN`, `FUEL`, `DRILL`, `LOAD`, `DETAILS`, `INV`, `MENU`, `CARGO`, `BANKED`, `ARTIFACT`, `PULSE SCANNER`, `TETHER ARTIFACT`, and `EMERGENCY RECALL`. At the shuttle, replace the away actions with the current repair and bank/stow/leave actions. Keep the return route and all terrain click targets unobstructed. The center terrain must be visibly protected from every persistent UI element.

### 06 - Refit/Field Upgrade selector, 1280x800

Using the existing cockpit style and space scene, show a compact left selection rail with three small offer selectors and one selected-detail card, not three full-width detail cards. Exact labels: `OREBIT`, `REFIT`, `MAP`, `INV`, `MENU`, `Choose one upgrade`, `ENGINE`, `SHIELD`, `CARGO`, `SELECTED`, `COMPARE`, `CONFIRM REFIT`, and `SKIP`. Keep complete statistics behind Compare. Keep the scene complete and separate.

### 07 - Results and modals, 1280x800

Using the existing space artwork and cockpit style, show a content-sized centered result card with generous safe margins over a deliberately paused/dimmed scene. Exact labels: `MISSION COMPLETE`, `Outcome`, `Fuel -4`, `Cargo +2`, `Risk -8%`, `CONTINUE`, `FULL REPORT`, and `ACHIEVEMENTS`. Also show a small inset example of the same content-sized treatment labeled `PAUSED`, `RESUME`, `MAP`, `SETTINGS`, and `QUIT TO TITLE`. Keep one primary continuation action, three consequences, and secondary drill-ins. No full-screen wall of text.

### 08 - Surface Ops stress case, 1024x768

Adapt prompt 02 to a true 1024x768 viewport. Keep the rail between 280px and 340px and preserve at least 640px of complete scene when geometry permits. Tighten vertical spacing, keep the primary action sticky, and allow only internal rail scrolling. Do not clip text or controls and do not place anything over the scene.

### 09 - Surface Ops minimum supported, 1280x720

Adapt prompt 02 to a true 1280x720 viewport. Use a compact landscape rail, 12px outer margins and gutter, and a fully visible scene. Shorten vertical spacing while preserving readable targets, four metrics, choice rows, and the sticky Return action. Nothing may be cut off or overlap the scene.

### 10 - Surface Ops wide, 1920x1080

Adapt prompt 02 to a true 1920x1080 viewport. Cap the rail at 340px, use 16px outer margins and gutter, and center/scale the complete scene inside the larger protected region. Do not allow the rail or its content to expand just because more space is available.

### 11 - Annotated layout contract, 1280x800

Edit the supplied final Surface Ops mockup into an engineering-ready annotated responsive-layout template. Preserve its product UI and artwork. Add a cyan outline labeled exactly `PANEL RECT` around the rail; a dimension marker labeled exactly `12 px GUTTER`; a green outline labeled exactly `PROTECTED SCENE RECT`; a dashed amber inset labeled exactly `HUD SAFE RECT`; and a dashed blue inset labeled exactly `CLICK-SAFE RECT`. Add a small legend with `PANEL`, `SCENE`, `HUD SAFE`, and `CLICK SAFE`. Place labels away from important art. The only elements allowed over the scene are these deliberate thin engineering outlines and their tiny labels.
