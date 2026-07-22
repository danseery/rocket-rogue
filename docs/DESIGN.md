# Rocket Rogue Design Notes

For future design work, start with `docs/AGENT_DESIGN_CONTEXT.md`. It links the extracted PDF sources and marks `docs/reference/USG_NOTES.md` as the highest-priority direction.

## Pillars

- Hidden-risk launch tension with no real-money gambling mechanics.
- Ship-first management through readable permanent systems, expedition damage, and meaningful upgrade tracks.
- Light but painful crew consequences.
- Roguelite persistence through unlock variety, records, memorials, and blueprints.
- Asset-light proof of concept using backend-neutral procedural primitives, RmlUi mission-control panels, and replaceable arcade sprites across native Vulkan and WebGL2 builds.

## Core loop

1. Configure the ship in the hangar.
2. Launch a proving flight on the current frontier.
3. Watch multiplier, telemetry channels, return risk, and distance climb.
4. Return home to bank data, cut engines to cool the ship while risking navigation drift, or eject for an expensive rescue.
5. A flight that banks new Flight Data or reaches a destination earns one saved refit opportunity. Buy one permanent ship system or keep the credits; crashes, shallow returns, and capped proving data go directly to the next phase.
6. Return to hangar operations: repair damage, recruit crew, train, rest, and plan the next flight.
7. Repeat proving flights until enough frontier readiness is banked.
8. Commit the agency to the next frontier, then repeat the loop farther from home.

## Post-arrival research loop

Research and surface expeditions start at Mars. Earth Orbit and Moon remain about proving the rocket program; Mars is the first destination where the agency can land, investigate, and turn discoveries into better long-term capability.

See `docs/POST_ARRIVAL_PHASES.md` for the detailed phase breakdown and Unity prototype takeaways.
See `docs/MINI_DRONE_SYSTEM.md` for the persistent Drone Bay / helper-drone layer.

The first implemented phase model is:

1. Complete a frontier-transfer arrival at Mars or beyond.
2. Choose from generated research projects that convert blueprints and recovered materials into unlock variety.
3. Start a surface expedition with action kits, shared fuel, a rolled site profile, and a short mission log.
4. Survey, use Push Deeper, or deploy the mining drone for one fuel-gated mining run.
5. Extract the payload before hazard, cargo, low kits, or spent fuel make recovery too risky.

The first selection of Flyby, Orbit, Landing, and Mining opens a short, saved introduction before starting the activity. Flyby and Orbit connect blueprint progress to permanent shipyard upgrades; Landing connects the player to surface work. The first Mining brief presents the Prospector contract: safely recover 3 Common Ore to fabricate the first autonomous Mining Drone. The live objective separates ore already home from ore aboard or carried, and contract completion receives a saved, acknowledgment-focused modal before Drone Ops becomes part of later surface loops.

Surface exploration should stay distinct from the launch gamble. The launch loop asks "can we get there and back?" The surface loop asks "how much can we safely bring home before the expedition overextends?" The solar system and Aaru Vale do not have enemies. Enemy encounters begin only after Arkfall near Khepri Prime, when the game leaves familiar exploration and introduces hostile unknowns.

Shared fuel is intentional friction in the surface loop. The shuttle and mining drone draw from the same reserve, so mining should be visibly framed as spending route-home margin for payload. The current mining baseline is 30 seconds of oxygen. Oxygen tank improvements can come from crew class, Drone Bay loadouts, and surface upgrades, but mining remains a once-per-surface-loop commitment; after the run is used, the drone is offline and `Push Deeper` is unavailable.

Deterministic artifact sites turn those mining tools into forecastable keys without rubber-banding arena difficulty from the equipped loadout. Surface Ops and Drone Ops should name the upcoming gate, direct capability, current readiness, and systemic alternatives. Hazard treatment, Survey triangulation, careful excavation, heavy towing, endurance, passive combat, terrain cover, and route planning must all reuse the same saved Act/level/seed gate contract as generation and runtime validation. Story sites complete only when their specific artifact is delivered, banked, and survives Surface extraction.

Research rewards should primarily widen the roguelite possibility space: module families, research facilities, special components, artifact threads, and story leads. Material-funded projects can directly unlock new module or facility families. Artifact-tagged projects identify one recovered artifact when possible; the identified record is tracked now, while its specific story payload remains a later content pass. Raw permanent stat inflation should remain secondary.

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

## Permanent refit tracks

Ship modules are unique permanent installations, not replacement parts or a persistent hangar shop. An earned refit opportunity survives Results and Arrival Ops until the player buys one system or chooses `Keep credits`. Buying a module adds its deltas once to the Pathfinder's installed systems; it never evicts another module. Expedition damage can take a system offline for the current ship, but ownership survives and the next replacement ship restores every permanent installation.

The opening proving phase is a bounded, curated ladder. Every useful Earth flight presents the next unowned upgrade from each actionable track, and the board is allowed to show fewer than three cards when a track is exhausted:

- Reach improves propulsion, fuel, drilling depth, and hauling power.
- Control improves cooling, sensors, pressure stability, simulator systems, and telemetry.
- Recovery improves hull, escape, medical, storage, and extraction systems.

The three opening ranks in each track are pure benefits with explicit prerequisites. They have no reroll action and use practical general-flight wording; the shared preflight Confidence value communicates their combined result without Moon-specific sales copy. After the Moon, unlocked refits return to randomized boards drawn without replacement. Those boards prefer one Reach, one Control, and one Recovery candidate whenever their pools allow it, and rerolls become available again.

Later cards should explain the practical threat they mitigate, the strongest numeric impact, and any visible tradeoff:

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

Hangar operations should keep pressure visible next to readiness and transfer planning. Training, rest, repair, and recruitment are not pressure-control systems by themselves, but they should help the player understand whether the next launch is a first attempt, a retry, or a lower-pressure proving run. Ship Details and Inventory should show permanent systems as `Built in`, `Installed`, or `Offline this expedition`; Crew Details should show installed crew facilities and aggregate effects.

Refit economy should reward recovered risk in discrete shelves:

- Launch and outcome copy should frame the yellow marker as the mission brief: meeting it secures the requested profile, while safely pushing beyond it returns richer findings and stronger funding.
- Returning home at the current data goal guarantees enough net credits for a common refit.
- Pushing beyond the data goal far enough guarantees enough net credits for an uncommon refit if recovered.
- Returning from the full target guarantees enough net credits for a rare refit if recovered.
- Ejection remains rescue-first and should not be the primary upgrade economy.

## Architecture

- `rocket_core` owns deterministic rules: content, RNG, progression, save data, flight tuning, launch resolution, and balance tests.
- `rocket_app` and `src/game` own platform-neutral application orchestration. `GameRunner` samples input and advances fixed simulation steps; `RocketGameApp` handles screen transitions and live controls; `GamePanel` produces semantic mission-control markup from a read-only context; and `GameRmlUi` presents that markup on both targets.
- `src/render` owns backend-neutral `SceneComposer`/`ScenePacket` generation plus the direct Vulkan 1.3 native backend and WebGL2 browser backend. Render code must not decide gameplay outcomes or create platform windows; native Vulkan and RmlUi share the SDL-created surface, device, frame command buffer, and synchronization.
- `src/input` owns portable controller snapshots, preferences, source arbitration, deadzones, button edges, real-time holds/repeats, and semantic input routing. Controller difficulty never scales from the player's loadout or device.
- `src/platform/AppServices.h` defines the injected save, preference, host, controller, texture, renderer, UI, and UI-bridge contracts used by the shared app.
- `src/platform/sdl` owns native SDL window and Vulkan surface creation, filesystem storage, PNG decoding, keyboard/mouse events, gamepads, haptics, fullscreen, and shutdown.
- `src/platform/web` is the only C++ boundary allowed to own Emscripten APIs, browser storage, DOM mirroring, asynchronous browser textures, and web gamepads.
- `web/shell.html` remains the web-only DOM fallback and forwards browser actions into the Emscripten entry point. Native builds use RmlUi directly and do not ship the browser shell.

See `docs/CONTROLLER_SUPPORT.md` for the controller layout, spatial-focus contract, device-local preference schema, pause safety rules, and verification matrix. `GameRunner` requests one `ControllerFrame` per host frame before scaled fixed simulation steps; UI repeats and safety holds always use the platform host's unscaled monotonic time.

Keep new gameplay mechanics in core when they affect odds, telemetry, rewards, or progression. Keep app-layer code focused on when a player chooses a mechanic and how that state is presented.

Flight controls that modify the launch model should flow through `FlightActionState` and `applyFlightActions` in `src/core/LaunchSimulation.*`. `RocketGameApp` owns when an action is available or consumed during the session, but core owns how active actions compose into telemetry, pacing, and return risk.

Hangar operation cards should be driven by `HangarOperationPreview` from `src/core/GameState.*`. The preview is the shared source for repair amount/cost, simulator gain/stress/cost, rest recovery/cost, recruit cost, and availability so UI cards do not drift from the action functions.

Research and surface-expedition rules should flow through `src/core/ResearchSystem.*`: post-arrival gating, research project generation/completion, material accounting, surface action kits, shared fuel, cargo, extraction risk, surface upgrades, Drone Bay state, and progression-backed surface-contact pressure. Panels and app transitions should consume those helpers instead of duplicating tier checks or resource math.

`src/core/MiningProgression.*` is the authoritative Act/level resolver shared by campaign mapping, Surface Ops forecasts, debug requests, terrain/reward gates, and enemy generation. `src/core/MiningSystem.*` consumes those rules for terrain generation, oxygen/fuel/drill timers, scanner pulses, unified ore/artifact rewards, hostile tunnel networks, passive drone effects, finish/abort/failure outcomes, and conversion back into `SurfaceActionOutcome`. Platform input adapters should call `RocketGameApp` mining methods or dispatch shared UI actions; rendering should consume snapshots rather than deciding mining outcomes.

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
- `src/core/RefitPresentation.h` owns refit-window presentation: resolved module and crew-facility offers, track/rank classes, glyphs, practical copy, primary impact, correctly signed stat chips, prices, affordability, permanent-install actions, conditional reroll action, and keep-credits action. Panels should render this returned data instead of rebuilding offer rules inline.
- `src/core/ResearchPresentation.h` owns research and surface-expedition presentation: blueprint/material metrics, research project cards, surface supply/cargo/risk metrics, and field action availability. Panels should render this returned data instead of rebuilding research/resource rules inline.
- `src/core/MiningPresentation.h` owns mining HUD and detail presentation: oxygen, shared fuel, drill integrity, scanner/fuel cadence, drone support, hostile tunnel summaries, action buttons, and controls copy.
- `src/core/CrewPresentation.h` owns Crew Details rows and facility-effect value wording. Panels should render detail rows and headers from this helper instead of recomputing training, stress, facility, and trait modifier strings.
- `src/core/ShipPresentation.h` owns Ship Details rows, installed/offline module summaries, and inventory fallback wording. Panels should render those rows instead of recomputing ship stats and module inventory display.
- `src/core/ProgramPresentation.h` owns Frontier and Legacy detail rows: readiness, mission difficulty, next transfer target, blueprint progress, losses, and furthest tier. Panels should render these rows instead of rebuilding program-progress detail modals inline.
- Legacy details should include recovered surface resources and artifact counts so the research/resource loop is inspectable without adding a separate inventory screen too early.
- `src/core/HangarPresentation.h` owns Hangar Ops card presentation: operation titles, details, costs, action IDs, availability, and card classes derived from `HangarOperationPreview`. Panels should render these cards instead of branching on repair/training/rest/recruit state.
- `src/core/ContentIds.h` owns persistent content IDs and unlock keys for modules, crew facilities, frames, astronauts, and destinations. Content definitions, save migrations, tests, and scripted rewards should use these shared IDs instead of raw strings.
- `src/core/SaveSchema.h` owns the current save header, field keys, and line-format delimiters. Serializer, parser, and migration tests should use these shared constants instead of duplicating save strings.
- `src/core/Telemetry.h` owns telemetry channel metadata and helpers. Simulation, UI, and tests should iterate the shared channel list instead of hand-listing `TEMP`, `PRESS`, `VIB`, `NAV`, `MIX`, and `ABORT`.
- `src/core/GameUi.h` owns stable cross-platform panel action IDs and modal IDs. `GamePanel` emits these data-like IDs, RmlUi dispatches them through the shared app, and the web fallback maps them to exported C++ functions. Avoid embedding JavaScript snippets such as `rr.someAction()` in generated markup.

Telemetry equation constants live under `tuning::telemetry`: pulse profiles, early/late channel buildup, readable minimums, abort certainty, and telemetry-driven stress. Balance the feel of warning dials there before changing formula structure.

Outcome math should also stay tuned from one place. Survival odds, return-home risk, rescue costs, ship damage curves, useful-data thresholds, blueprint share thresholds, and post-flight crew stress all live under `tuning::outcomes` or `tuning::stress` so balance changes do not require spelunking through launch resolution branches.

Post-launch crew stress should flow through `postLaunchCrewStress` / `postLaunchCrewStressGain` in `src/core/GameState.*`. That helper exposes base stress, warning contribution, abort contribution, facility relief, and total stress so future events, facilities, and UI can share one model.

When adding a new mechanic, prefer adding the math knobs to `Tuning.h`, the visible wording to `GameText.h`, and any reusable channel/event metadata to a small core helper before wiring the behavior into `GameState`, `LaunchSimulation`, `RocketGameApp`, or `GamePanel`.

## Persistence

The campaign save format is versioned and line-based. Version 3 introduced the saved refit entitlement and generated offer IDs so an earned shipyard choice survives reload and delayed Arrival Ops. Version 4 adds the acknowledged activity briefs and Prospector Common Ore objective while preserving persistent Drone Bay ownership. Version-2 migration installs every previously owned or stored module once, while its legacy equipped list remains the operational/offline state for the current expedition. `SaveSchema.h` defines the header, field keys, and delimiters so serialization, missing-field defaults, and migration tests stay synchronized as fields are added. `ISaveStore` keeps the shared app independent of the storage medium: the web adapter uses browser `localStorage`, while native builds use atomic replacement of `save_v1.txt` beneath the SDL per-user preference path. Native builds intentionally start with fresh native data and do not import browser saves.

Display, accessibility, debug, fullscreen, and controller settings are separate from campaign data behind `IPreferenceStore`. The web adapter persists browser-local preferences; native builds use `preferences_v1.txt` beside the native save.
