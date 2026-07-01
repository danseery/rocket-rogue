# RmlUi UX Polish Guide

Use this checklist when changing Rocket Rogue UI. The game is a playable cockpit and mission-management surface, not a spreadsheet or a marketing page. Every screen should make the current state, available action, and consequence obvious at game speed.

## Core Rules

- Preserve the space-game feel: dark cockpit panels, restrained borders, readable warning color, and the Neuropol-style futuristic tone.
- Make layout intentional. Related controls share a row; different action classes move to their own row.
- Avoid percentage-based card widths in RmlUi when a screen has shown clipping or collapse. Prefer explicit widths that fit the current panel bounds.
- Keep buttons centered internally. Button text should not sit in the top-left corner.
- Give top-right utility buttons room. Inventory, Legacy, and Settings should not touch or overlap.
- Do not let panel content cover the game window more than necessary. The player should still see enough of the mission scene to understand context.
- Avoid repeated status copy. One mission update is useful; duplicate yellow paragraphs feel like a bug.

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
- Flight: controls should help the player act under pressure. Telemetry needs readable values and stable button rows that do not shift after choosing Return to Earth.
- Debrief: present the outcome first, then mission result, burn profile, peak telemetry, achievements, and one centered next action.
- Refit: cards should feel collectible and tactile. Preserve the Pokemon-card-like framing, art/glyph area, stat chips, and clear install/skip affordance.
- Settings: dropdowns must look intentional. Avoid stray arrow artifacts and expose the selected value clearly.

## RmlUi Implementation Checks

- Prefer explicit widths for `phase-board` cards, rows, and action groups.
- Keep fixed-format UI stable with fixed heights or minimum heights where content changes during play.
- Use `display: flex` with `flex-wrap: nowrap` for rows that must stay together.
- Avoid nested cards unless the inner element is a genuine repeated item or modal surface.
- Use one visual hierarchy: panel title, mission status, KPI chips, section title, cards, actions.
- Do not introduce decorative UI that competes with the game scene.

## Verification Path

- Build the web target after RmlUi changes.
- Reload the in-app browser with a cache-busting query string.
- Verify at least the changed screen and one adjacent flow into or out of it.
- Take screenshots at the actual in-app viewport and inspect for clipped right/bottom borders, wrapped button labels, overlap, and hidden click targets.
- Run `git diff --check`.
- Run the native test script when gameplay or presentation plumbing changed.
