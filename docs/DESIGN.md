# Rocket Rogue Design Notes

## Pillars

- Hidden-risk launch tension with no real-money gambling mechanics.
- Ship-first management through readable module slots and damage.
- Light but painful crew consequences.
- Roguelite persistence through unlock variety, records, memorials, and blueprints.
- Asset-light proof of concept using WebGL primitives, browser UI, and replaceable arcade sprites.

## Core loop

1. Configure the ship in the hangar.
2. Launch a proving flight on the current frontier.
3. Watch multiplier, telemetry channels, return risk, and distance climb.
4. Return home to bank data, cut engines to cool the ship while risking navigation drift, or eject for an expensive rescue.
5. Read the mission summary, then choose one of three refit cards. Ship modules and crew facilities share this window; the install takes the full hangar window and still costs mission credits.
6. Return to hangar operations: repair damage, recruit crew, train, rest, and plan the next flight.
7. Repeat proving flights until enough frontier readiness is banked.
8. Commit the agency to the next frontier, then repeat the loop farther from home.

## Risk model

Each launch creates a deterministic hidden crash point from:

- Frontier hazard and multiplier ceiling.
- Aggregated ship module stats.
- Assigned astronaut training, stress, and trait.
- Seeded random tail behavior.

Telemetry hints become more alarming as the current multiplier approaches the hidden crash point. Sensors improve warning quality but never reveal certainty.

Each prepared launch also seeds a few deterministic telemetry incidents. An incident is a short pulse on one or two channels, such as injector pressure, frame vibration, fuel mix, guidance, or abort margin. These pulses can rise and settle before the hidden crash point, creating mid-flight press-your-luck decisions instead of saving all danger for the final exponential cliff. Module stats damp related incidents, so upgrades change the shape of risk without making a launch guaranteed.

`Cut Engines` is modeled as a temporary flight-model transform in `rocket_core`: it lowers throttle, heat, and vibration while increasing guidance risk. The app layer only owns whether the control is active.

Emergency flight actions are also core flight-model transforms:

- `Relief valve` vents physical pressure, reducing the `PRESS` channel while adding navigation drift. It can fail, and rare rapid decompression destroys the vehicle.
- `Jettison cargo` stabilizes fuel mix but adds debris/mass-shift penalties to `NAV` and `VIB`, plus a return-home risk penalty because the ship has fewer reserves.
- These actions are single-use during outbound flight and should be presented as tactical risk swaps, not universal upgrades.

Mission pressure is a separate modifier on the `PRESS` telemetry channel:

- Never attempted a destination: +50% pressure.
- Attempted but never completed it: starts at +25% and decays with repeated attempts.
- Completed proving profiles: pressure drops to a lower nonzero floor so routine flights still carry tension.
- Pressure-control modules subtract from that modifier before telemetry is generated.
- Unproven routes also cap early hidden-crash ceilings. A first full Earth Orbit profile should be a long-shot, while shorter proving returns are the intended way to bank data, buy upgrades, and make the later full profile feel earned.

Crew stress is a separate human-performance modifier:

- Every 14 stress is one stress step.
- Each stress step cancels one effective training level for the hidden launch performance curve.
- Each stress step adds a small `NAV` penalty to represent piloting mistakes under load.
- `ABORT` scales by stress steps from x1.00 at calm to x2.00 at maximum stress.
- Simulator burns add training and stress; rest removes enough stress to erase at least one step in most practical cases.

## Refit cards

Module rewards are a post-mission choice, not a persistent hangar shop. Each reward screen shows three module cards and allows at most one install before hangar operations resume. Cards should explain the practical threat they mitigate, the strongest numeric impact, and any visible tradeoff:

- Engine modules shorten exposure time but can raise volatility, fuel pressure, or heat load.
- Fuel modules improve long-burn, return margin, and pressure stability.
- Hull modules absorb structural failures and reduce damage consequences.
- Cooling modules directly mitigate temperature runaway.
- Sensor modules improve warning luck, navigation confidence, and pressure uncertainty.
- Escape modules improve ejection and crew-survival outcomes.

Crew facilities are refit rewards too. They should sit in Crew Details and improve actual crew math, not just presentation:

- Simulator facilities increase training gain or lower simulator stress.
- Medical facilities improve rest and injury recovery.
- Psychology/coaching facilities reduce post-launch stress and improve astronaut trait modifiers.

Hangar operations should keep pressure visible next to readiness and transfer planning. Training, rest, repair, and recruitment are not pressure-control systems by themselves, but they should help the player understand whether the next launch is a first attempt, a retry, or a lower-pressure proving run. Ship Details should show equipped and stored ship upgrades; Crew Details should show installed crew facilities and aggregate effects.

Refit economy should reward recovered risk in discrete shelves:

- Returning home at the current data goal guarantees enough net credits for a common refit.
- Pushing beyond the data goal far enough guarantees enough net credits for an uncommon refit if recovered.
- Returning from the full target guarantees enough net credits for a rare refit if recovered.
- Ejection remains rescue-first and should not be the primary upgrade economy.

## Architecture

- `rocket_core` owns deterministic rules: content, RNG, progression, save data, flight tuning, launch resolution, and balance tests.
- `src/game` owns browser-session orchestration. `RocketGameApp` handles screen transitions and launch controls; `GamePanel` renders mission-control HTML from a read-only context.
- `src/render` owns WebGL2 drawing and texture upload. It should not decide gameplay outcomes.
- `src/platform` is the small web bridge for persistence and panel updates.
- `web/shell.html` owns browser DOM event routing and calls exported C++ functions.

Keep new gameplay mechanics in core when they affect odds, telemetry, rewards, or progression. Keep app-layer code focused on when a player chooses a mechanic and how that state is presented.

Flight controls that modify the launch model should flow through `FlightActionState` and `applyFlightActions` in `src/core/LaunchSimulation.*`. `RocketGameApp` owns when an action is available or consumed during the session, but core owns how active actions compose into telemetry, pacing, and return risk.

Hangar operation cards should be driven by `HangarOperationPreview` from `src/core/GameState.*`. The preview is the shared source for repair amount/cost, simulator gain/stress/cost, rest recovery/cost, recruit cost, and availability so UI cards do not drift from the action functions.

Shared game constants and player-facing copy should have one owner:

- `src/core/Tuning.h` owns balance values such as refit costs, crew stress steps, mission difficulty, action tradeoffs, launch pacing, warning thresholds, and reward shelves.
- `src/core/GameText.h` owns reusable display text: status lines, telemetry warning copy, core labels, enum display labels, button labels, module stat labels, and module threat wording.
- `src/core/GameFormat.h` owns reusable numeric display formatting such as credits, signed deltas, multipliers, percentages, readiness fractions, damage summaries, and crew stress/training summaries.
- `src/core/GameMath.h` owns reusable equation helpers such as clamped `smoothStep` shaping. Do not duplicate easing or shaping formulas inside app, panel, or simulation code.
- `src/core/FlightProgress.h` owns shared travel/return progress equations: burn-depth-to-route progress, return completion, return visual travel, and return duration. App, panel, and renderer-facing snapshots should use these helpers instead of retyping the same progress math.
- `src/core/LaunchBalance.h` owns pure launch-preparation equations: ship performance score, readiness/overprepared math, transfer hazard, hidden-crash ceiling penalties and bonuses, sensor quality, heat/pressure prep, and telemetry incident setup. `LaunchSimulation` should orchestrate these helpers instead of carrying raw balance coefficients inline.
- `src/core/DetailPresentation.h` owns reusable detail-row/header data for modal detail screens. Core presenters should return these rows, and `GamePanel` should only render them to HTML.
- `src/core/PanelPresentation.h` owns small reusable panel primitives such as metric and button presentation data. Screen-specific presenters should reuse these data shapes instead of inventing local copies.
- `src/core/PanelChromePresentation.h` owns shared panel chrome data: top-level mission metrics, active display destination, crew stress summary, and settings modal rows/actions. `GamePanel` should render this data instead of recomputing always-visible metrics.
- `src/core/LaunchPresentation.h` owns launch-screen presentation: active burn metrics, telemetry detail rows, and flight-control button labels/actions/states. `GamePanel` should render this prepared data rather than recomputing launch telemetry or branching on flight-control state.
- `src/core/LaunchReadinessPresentation.h` owns launch-hold presentation and readiness gating display: hull/crew blocked state, hold messages, required action detail, and repair/recruit actions. Panels should consume this object instead of recomputing launch-block rules inline.
- `src/core/LaunchStatus.h` owns launch/return status-line selection. App code should pass the current telemetry/action context into it instead of branching directly on warning thresholds for player-facing copy.
- `src/core/OutcomePresentation.h` owns result-screen labels, follow-up action labels, and outcome note copy derived from `LaunchOutcome`. Panels should render this presentation data instead of duplicating outcome/recovery branching.
- `src/core/RefitPresentation.h` owns refit-window presentation: resolved module and crew-facility offers, slot classes, glyphs, threat copy, primary impact, stat chips, prices, affordability, install actions, reroll action, and skip action. Panels should render this returned data instead of rebuilding offer rules inline.
- `src/core/CrewPresentation.h` owns Crew Details rows and facility-effect value wording. Panels should render detail rows and headers from this helper instead of recomputing training, stress, facility, and trait modifier strings.
- `src/core/ShipPresentation.h` owns Ship Details rows, equipped/stored module summaries, and inventory fallback wording. Panels should render those rows instead of recomputing ship stats and module inventory display.
- `src/core/ProgramPresentation.h` owns Frontier and Legacy detail rows: readiness, mission difficulty, next transfer target, blueprint progress, losses, and furthest tier. Panels should render these rows instead of rebuilding program-progress detail modals inline.
- `src/core/HangarPresentation.h` owns Hangar Ops card presentation: operation titles, details, costs, action IDs, availability, and card classes derived from `HangarOperationPreview`. Panels should render these cards instead of branching on repair/training/rest/recruit state.
- `src/core/ContentIds.h` owns persistent content IDs and unlock keys for modules, crew facilities, frames, astronauts, and destinations. Content definitions, save migrations, tests, and scripted rewards should use these shared IDs instead of raw strings.
- `src/core/SaveSchema.h` owns the current save header, field keys, and line-format delimiters. Serializer, parser, and migration tests should use these shared constants instead of duplicating save strings.
- `src/core/Telemetry.h` owns telemetry channel metadata and helpers. Simulation, UI, and tests should iterate the shared channel list instead of hand-listing `TEMP`, `PRESS`, `VIB`, `NAV`, `MIX`, and `ABORT`.
- `src/core/GameUi.h` owns stable browser panel action IDs and modal IDs. `GamePanel` should emit these data-like IDs, while `web/shell.html` maps them to exported C++ functions. Avoid embedding JavaScript snippets such as `rr.someAction()` in generated HTML.

Telemetry equation constants live under `tuning::telemetry`: pulse profiles, early/late channel buildup, readable minimums, abort certainty, and telemetry-driven stress. Balance the feel of warning dials there before changing formula structure.

Outcome math should also stay tuned from one place. Survival odds, return-home risk, rescue costs, ship damage curves, useful-data thresholds, blueprint share thresholds, and post-flight crew stress all live under `tuning::outcomes` or `tuning::stress` so balance changes do not require spelunking through launch resolution branches.

Post-launch crew stress should flow through `postLaunchCrewStress` / `postLaunchCrewStressGain` in `src/core/GameState.*`. That helper exposes base stress, warning contribution, abort contribution, facility relief, and total stress so future events, facilities, and UI can share one model.

When adding a new mechanic, prefer adding the math knobs to `Tuning.h`, the visible wording to `GameText.h`, and any reusable channel/event metadata to a small core helper before wiring the behavior into `GameState`, `LaunchSimulation`, `RocketGameApp`, or `GamePanel`.

## Persistence

The save format is versioned and line-based for a small dependency-free POC. `SaveSchema.h` defines the header, field keys, and delimiters so save/load code and tests stay synchronized as new fields are added. The web build stores saves in `localStorage` via `RocketBridge`. Future production builds should replace this with a JSON or binary schema plus migration tests once the content stabilizes.
