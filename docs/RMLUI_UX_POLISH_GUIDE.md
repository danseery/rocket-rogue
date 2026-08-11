# RmlUi UX Polish Guide

Use this checklist when changing Rocket Rogue UI. The game is a playable cockpit and mission-management surface, not a spreadsheet or a marketing page. Every screen should make the current state, available action, and consequence obvious at game speed.

For implementation ownership, template selection, shared assets, and the extension workflow, see [RmlUi Template and Component System](RMLUI_TEMPLATE_COMPONENT_SYSTEM.md).

## Core Rules

- Preserve the space-game feel: dark cockpit panels, restrained borders, readable warning color, and the Neuropol-style futuristic tone.
- Make layout intentional. Related controls share a row; different action classes move to their own row.
- Avoid percentage-based card widths in RmlUi when a screen has shown clipping or collapse. Prefer explicit widths that fit the current panel bounds.
- Keep buttons centered internally. Button text should not sit in the top-left corner.
- Give top-right utility buttons room. Inventory, Legacy, and Settings should not touch or overlap.
- Do not let panel content cover the game window more than necessary. The player should still see enough of the mission scene to understand context.
- Avoid repeated status copy. One mission update is useful; duplicate yellow paragraphs feel like a bug.

## UI Layout Contract

- Phase-board screens use one named content lane. The current canonical frame is `736px` wide with a `704px` content lane and `16px` side insets unless a future redesign updates the shared contract.
- Titlebar controls, KPI rows, section cards, card grids, footer lanes, and final actions must share the same left and right edges. Do not create a second local alignment grid for one row.
- Use shared lane primitives before adding local layout math: `.phase-lane`, `.phase-row`, `.phase-title-row`, `.phase-action-grid`, `.phase-card-slot`, `.phase-footer-lane`, and stable chip/button slots.
- Parent regions own alignment. Child cards fill fixed slots, and the last card or final action must not carry trailing margin.
- Fixed-format regions reserve lanes for chips, status, and footer actions. Prefer taller cards over squeezed content, clipped chips, or buttons pushed into dividers.
- Do not solve alignment with arbitrary one-off width, height, padding, or margin tweaks. If a screen needs a new lane width or slot size, update the shared RmlUi token or primitive used by both native and web.
- Do not copy a shared header, objective, KPI, card, or footer lane into screen-local RML or RCSS. Extend the shared primitive when the same geometry is useful elsewhere.

## Readability

- Primary numbers must be readable by a normal player. Telemetry values should feel like mission gauges, not tiny spreadsheet cells.
- Text must not touch borders, separators, or adjacent controls. Add padding before shrinking type.
- Use separators to distinguish description, cost, status, and action, especially on Hangar ops and Refit cards.
- Keep labels compact and values prominent. If a card has one important number, make the number easy to scan.
- Long copy belongs in a contained help or debrief card with a clear dismiss/continue action.

## Button Semantics

- Green buttons are positive progression or safe continuation.
- Red buttons are destructive, emergency, or high-risk actions.
- Yellow buttons are caution/mitigation/system actions.
- Disabled or unavailable actions should be visibly different and should not navigate to surprising screens.
- Do not let adjacent buttons fight for clicks. Leave visible gaps and verify the click target matches the label.
- If green and red are alternatives for the same decision, keep them on the same row. Put yellow mitigation buttons on the next row.

## Screen-Specific Notes

- Hangar: keep Hangar ops three-wide when possible. Cost/status and action controls need their own footer area.
- Flight: reveal only the current lesson's controls and gauges. Fuel Survey shows Fuel and `Turn Around`; Controls adds throttle/corridor; Mars adds Temperature and `Engines Off`; Jupiter adds Hull and asteroids. Stable button rows should not shift after turning around or entering later recovery contexts.
- Debrief: present the outcome first, use a red outlined surface for rescue/failure and a green outlined surface for safe return/arrival, then show the lesson result and one centered next action.
- Refit: cards should feel collectible and tactile. Preserve the Pokemon-card-like framing, art/glyph area, stat chips, and clear install/skip affordance.
- Settings: dropdowns must look intentional. Avoid stray arrow artifacts and expose the selected value clearly.

## RmlUi Implementation Checks

- Select an existing `PanelTemplateKind` and `PanelVisualFamily` before writing layout markup. Add a template only when the screen requires genuinely new geometry.
- Use `rr-`-prefixed RmlUi templates for stable shell structure. Keep repeated cards, lists, labels, action state, and other variable content in typed C++ presentation renderers.
- Prefer explicit widths for `phase-board` cards, rows, and action groups.
- Keep fixed-format UI stable with fixed heights or minimum heights where content changes during play.
- Use `display: flex` with `flex-wrap: nowrap` for rows that must stay together.
- Avoid nested cards unless the inner element is a genuine repeated item or modal surface.
- Use one visual hierarchy: panel title, mission status, KPI chips, section title, cards, actions.
- Do not introduce decorative UI that competes with the game scene.
- Preserve semantic action IDs, focus IDs, controller geometry classes, realtime patch IDs, modal IDs, and settings attributes. Template expansion must not create a second source of behavior.
- Keep one content target per template. A template supplies stable chrome around one flattened content payload; it is not a React-style parameterized component.
- Put shared RML and RCSS under `assets/ui`. Do not embed a private stylesheet or an alternate browser implementation for one screen.

## Verification Path

- Run the UI resource validator after changing RML, RCSS, links, or templates. Missing files, duplicate template names, missing content targets, dependency cycles, malformed resources, and obsolete modal pseudo-templates must fail.
- Build both native and web targets after shared RmlUi, presentation, or component changes. Both targets must load the same packaged `assets/ui` tree.
- Verify at least the changed screen and one adjacent flow into or out of it in a native window and the web build. Reload the browser with a cache-busting query string.
- Take screenshots at 1280x800, 1080p, 1440p, and 4K where the changed layout can vary. Inspect for clipped right/bottom borders, wrapped button labels, overlap, hidden click targets, and high-DPI drift.
- Verify shared RmlUi behavior on native and web. The web shell is an input and host boundary, not a hidden panel, modal, prompt, toast, or focus renderer.
- Exercise pointer actions, controller navigation, modal stacking, non-dismissible gates, and focus restoration. A steady-state realtime patch must preserve active modal, focus, and scroll state.
- Run `git diff --check`.
- Run native tests when gameplay, input, or presentation plumbing changed, and web tests whenever panel presentation or browser host behavior changed.
