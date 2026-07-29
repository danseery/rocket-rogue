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
4. Use the current recovery action to bank data, cut engines to cool the ship while risking navigation drift, or eject for an expensive rescue. Recovery is `Return to Earth` before Saturn, `Recover to Expedition` on the one-way outer route, and `Return to Ark` after the Straylight discovery.
5. A flight that banks new Flight Data or reaches a destination earns one saved refit opportunity. Buy one permanent ship system or keep the credits; crashes, shallow returns, and capped proving data go directly to the next phase.
6. Return to hangar operations: repair damage, recruit crew, train, rest, and plan the next flight.
7. Repeat proving flights until enough frontier readiness is banked.
8. Commit the agency to the next frontier, then repeat the loop farther from home.

## Post-arrival research loop

Surface expeditions start at the Moon with a narrow mining lesson; broader research still starts at Mars. Earth Orbit proves the rocket program, the Moon teaches inert regolith versus gray Common Ore and explicit contract delivery, and Mars turns that lesson into the second Drone Bay slot and wider long-term capability.

See `docs/POST_ARRIVAL_PHASES.md` for the detailed phase breakdown and Unity prototype takeaways.
See `docs/MINI_DRONE_SYSTEM.md` for the persistent Drone Bay / Support Drone layer.
See `docs/MINING_MINIGAME_PLAN.md` for the authoritative rig/EVA physics, controls, failure, loose-chunk, and tether contract.

The first implemented phase model is:

1. Complete a frontier-transfer arrival at the Moon or beyond.
2. Choose from generated research projects that convert blueprints and recovered materials into unlock variety.
3. Start a surface expedition with action kits, shared fuel, a rolled site profile, and a short mission log.
4. Survey, use Push Deeper, or deploy the player-controlled Mining Rig for one fuel-gated mining run.
5. Extract the payload before hazard, cargo, low kits, or spent fuel make recovery too risky.

The first selection of optional Flyby and Orbit activities retains saved introductions. Campaign-critical surface beats use mandatory, non-dismissible briefings and explicit claims: deliver 30 lunar Common Ore and install Prospector Mk I/Slot 1; deliver 40 Mars Common Ore and fabricate empty Slot 2; commission the Hazard Support Drone Mk I on Io, cool and mine two four-segment lava seals, and safely extract the minor artifact; then claim a Perfect Jupiter slingshot to open Saturn. Moon remains the simple excavation-and-return lesson; Mars makes repeated oxygen, heat, integrity, repair, cargo, and return decisions mandatory. Live objectives expose carried, aboard, delivered, ready-to-claim, and complete state rather than advancing silently.

Surface exploration should stay distinct from the launch gamble. Before Saturn, the launch loop asks "can we get there and back?" The surface loop asks "how much can we safely recover before the expedition overextends?" Claiming the Saturn course commits the expedition outward; later recovery copy says `Recover to Expedition`, never promises a return to Earth, and changes to `Return to Ark` only after the Straylight discovery. The solar system and Aaru Vale do not have enemies. Enemy encounters begin only after Arkfall near Khepri Prime, when the game leaves familiar exploration and introduces hostile unknowns.

Shared fuel is intentional friction in the surface loop. The shuttle and Mining Rig draw from the same reserve, so mining should be visibly framed as spending route-home margin for payload. Push Deeper guarantees and banks layer +1 before collapse risk begins on the layer +2 gamble; a scanned artifact is confirmed when its mapped layer succeeds. The rig deploys directly at the banked start depth while the ship remains fixed at surface depth 0, so extraction and service require ascending through the intervening layers. The current normal mining baseline is 30 seconds of oxygen; the fixed Io artifact introduction uses 60 seconds. Oxygen improvements can come from crew class, Support Drone loadouts, and surface upgrades, but mining remains a once-per-surface-loop commitment; after the run is used, the rig is offline and `Push Deeper` is unavailable.

Mining uses one extensible POI pointer with runtime labels. Safety pressure from oxygen, the controlled actor, or the drill overrides ordinary targets and leads toward `SHIP`; otherwise a revealed recoverable artifact receives `ARTIFACT` guidance, including ascent/descent boundary guidance when it is on another layer.

Deterministic protected-objective sites turn those mining tools into forecastable keys without rubber-banding arena difficulty from the equipped loadout. Surface Ops and Drone Ops should name the upcoming gate, direct capability, current readiness, and systemic alternatives. Hazard treatment, Survey triangulation, careful excavation, heavy towing, endurance, autonomous swarm combat, EVA self-defense, terrain cover, and route planning must all reuse the same saved Act/level/seed gate contract as generation and runtime validation. A protected site completes only when its configured objective is delivered, banked, and survives Surface extraction. The scenario/content boundary is documented in [SCENARIO_FRAMEWORK.md](SCENARIO_FRAMEWORK.md).

### Mechanical touchstone and EVA identity

*Solar Jetman* is an internal mechanical touchstone for destination-sensitive gravity, inertia, vehicle-versus-pilot roles, towing burden, vulnerable recovery, and physically returning discoveries home: [original NES manual](https://www.gamingalexandria.com/highquality/NES/Solar%20Jetman/Solar%20Jetman%20-%20Manual%20%28Searchable%29.pdf) and [official Rare Replay manual](https://dlassets-ssl.xboxlive.com/public/content/367297b7-c6a3-4496-83ad-cb70c52ce8cd/GameManual/2e5e2560-e901-414b-87fa-081a07f24c6c/en-SA/index.html#SolarJetman). OREBIT differs through voluntary EVA, twin-stick aim, hand-drilled terrain, suit-only passages, Support Drones, and explicit tether control.

Mining runs start in the rig. The rig reaches `7.2 cells/s` with `14 cells/s²` acceleration, `20 cells/s²` braking, and a `0.48`-cell collider. The suit reaches `4.6 cells/s` with `28 cells/s²` acceleration, `24 cells/s²` braking, and a `0.25`-cell collider. Both use vector gravity with base strength `6 cells/s²` multiplied by destination scale: Earth Orbit `0.15`, Moon `0.35`, Mars `0.60`, Jupiter `1.15`, Saturn `0.95`, Uranus `0.80`, Neptune `1.05`, Khepri Prime `1.20`, and Rift Belt `0.25`.

The suit carries no ore. Its hand-drilled ore and enemy rewards become loose chunks, while a tethered artifact remains the sole cargo exception. The suit is slower and vulnerable but accelerates quickly, fits through narrow passages, and can change depth while the parked rig remains behind. A destroyed rig emergency-ejects the operator; a destroyed suit ends the run.

Support Drones belong to the player rather than the Mining Rig. They follow, orbit, and defend the controlled actor through a transferable logical anchor while retaining independent positions, velocities, haul, shield state, cooldowns, stable formation slots, and orbit phases. Same-layer transfers do not snap them; cross-depth transfers rebuild deterministic formations. Artifact tether ownership stays independent.

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
- `Jettison cargo` stabilizes fuel mix but adds debris/mass-shift penalties to `NAV` and `VIB`, plus a recovery-risk penalty because the ship has fewer reserves.
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
- Recovering at the current data goal guarantees enough net credits for a common refit.
- Pushing beyond the data goal far enough guarantees enough net credits for an uncommon refit if recovered.
- Returning from the full target guarantees enough net credits for a rare refit if recovered.
- Ejection remains rescue-first and should not be the primary upgrade economy.

## Architecture

- `rocket_core` owns deterministic rules: content, RNG, progression, save data, flight tuning, launch resolution, and balance tests.
- `src/core/ScenarioSystem.*` owns reusable scenario definitions and instances, prerequisite state, saved explicit actions, typed completion events, idempotent rewards, route requirements, and the shared `ScenarioObjectivePresentation`. A UI or activity may dispatch actions and events, but must not reimplement destination-specific story gates.
- `rocket_app` and `src/game` own platform-neutral application orchestration. `GameRunner` samples input and advances fixed simulation steps; `RocketGameApp` handles screen transitions and live controls; `GamePanel` produces semantic mission-control markup from a read-only context; and `GameRmlUi` presents that markup on both targets.
- `src/render` owns backend-neutral `SceneComposer`/`ScenePacket` generation plus the direct Vulkan 1.3 native backend and WebGL2 browser backend. Render code must not decide gameplay outcomes or create platform windows; native Vulkan and RmlUi share the SDL-created surface, device, frame command buffer, and synchronization.
- The static `JetpackCapybara` sprite is a required shared texture registered through the scene manifest and generated atlas. Its addition raises the runtime texture count from 34 to 35; a missing asset or registration is a validation failure, not permission to substitute another sprite.
- `src/input` owns portable controller snapshots, preferences, source arbitration, deadzones, button edges, real-time holds/repeats, and semantic input routing. Controller difficulty never scales from the player's loadout or device.
- `src/platform/AppServices.h` defines the injected save, preference, host, controller, texture, renderer, UI, and UI-bridge contracts used by the shared app.
- `src/platform/sdl` owns native SDL window and Vulkan surface creation, filesystem storage, PNG decoding, keyboard/mouse events, gamepads, haptics, fullscreen, and shutdown.
- `src/platform/web` is the only C++ boundary allowed to own Emscripten APIs, browser storage, DOM hosting, asynchronous browser textures, and web gamepads.
- `web/shell.html` hosts the web target and forwards platform events into the shared application. It must not define a visually or behaviorally divergent gameplay-UI path; native and web use the same semantic presentation and action contracts.

See `docs/CONTROLLER_SUPPORT.md` for the controller layout, spatial-focus contract, device-local preference schema, pause safety rules, and verification matrix. `GameRunner` requests one `ControllerFrame` per host frame before scaled fixed simulation steps; UI repeats and safety holds always use the platform host's unscaled monotonic time.

Keep new gameplay mechanics in core when they affect odds, telemetry, rewards, or progression. Keep app-layer code focused on when a player chooses a mechanic and how that state is presented.

Flight controls that modify the launch model should flow through `FlightActionState` and `applyFlightActions` in `src/core/LaunchSimulation.*`. `RocketGameApp` owns when an action is available or consumed during the session, but core owns how active actions compose into telemetry, pacing, and return risk.

Hangar operation cards should be driven by `HangarOperationPreview` from `src/core/GameState.*`. The preview is the shared source for repair amount/cost, simulator gain/stress/cost, rest recovery/cost, recruit cost, and availability so UI cards do not drift from the action functions.

Research and surface-expedition rules should flow through `src/core/ResearchSystem.*`: post-arrival gating, research project generation/completion, material accounting, surface action kits, shared fuel, cargo, extraction risk, surface upgrades, Drone Bay state, and progression-backed surface-contact pressure. Panels and app transitions should consume those helpers instead of duplicating tier checks or resource math.

`src/core/MiningProgression.*` is the authoritative Act/level resolver shared by campaign mapping, Surface Ops forecasts, debug requests, terrain/reward gates, and enemy generation. `src/core/MiningSystem.*` consumes those rules for terrain generation, vector gravity, independent rig/operator physics, loose chunks, oxygen/fuel/drill timers, scanner pulses, sidearm raycasts, artifact tether forces, unified ore/artifact rewards, hostile tunnel networks, finish/abort/failure outcomes, and conversion back into `SurfaceActionOutcome`. `src/core/MiniDroneCoordination.*` consumes a resolved `MiniDroneAnchorFrame`; these `MiniDrone*` names are legacy internal C++ identifiers, while Support Drone behavior and presentation must not reach directly for rig coordinates. Platform input adapters should call `RocketGameApp` mining methods or dispatch shared actions for aim, fire, drill, scan, tether, operator switching, and bank/leave. Rendering should consume snapshots rather than deciding mining outcomes.

### Scenario authoring and save compatibility

Author a reusable progression beat by adding a versioned `ScenarioDefinition`, unique local step IDs, prerequisite edges, player-facing presentation, a typed `ScenarioEventKind`, the explicit `ScenarioActionKind`, and typed rewards. Reward kinds can grant unlock keys, destination route access, Support Drones, bay capacity, upgrade credits, or banked Common/Rare/Exotic materials. A fixed mining activity references a versioned `MiningSiteDefinition`; a Flyby step uses `FlybyFinished` plus `requiredGrade`. Route gates are content data: a destination requires unlock keys, while a `RouteAccess` reward resolves that destination's configured keys without route evaluation recognizing a story destination. `validateScenarioCatalog()` rejects duplicate IDs, cyclic prerequisites, invalid rewards, unknown mining sites, invalid cocoons, and procedural factories whose template would instantiate by default.

Save version 9 persists authored and procedural `ScenarioInstance` identity, definition/factory versions, seed, resolved parameters, step state, first-failure acknowledgement, and the awarded-reward ledger. It also persists staged and active mining scenario/site context, generic `MiningSiteProgress`, cocoon definition and protected-objective identity, per-layer progress, reveal policy, and cell-layer tags across cached depths. Version 8 and older saves synthesize instances from the legacy Moon/Mars/Io/slingshot fields, preserve partial and ready-to-claim progress, mark older site records as migrated, and convert the fixed Io shell without replaying rewards. Optional fields were appended: a missing scenario definition ID defaults to the runtime ID, and a site record without provenance is treated as legacy.

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
- `src/core/MiningPresentation.h` owns mining HUD and detail presentation: mode, gravity, oxygen, shared fuel, rig health, suit integrity, drill heat, tether burden, loose-chunk count, `Suit carry: 0`, Support Drone anchor status, scanner/fuel cadence, hostile tunnel summaries, action buttons, and controls copy.
- `src/core/CrewPresentation.h` owns Crew Details rows and facility-effect value wording. Panels should render detail rows and headers from this helper instead of recomputing training, stress, facility, and trait modifier strings.
- `src/core/ShipPresentation.h` owns Ship Details rows, installed/offline module summaries, and inventory fallback wording. Panels should render those rows instead of recomputing ship stats and module inventory display.
- `src/core/ProgramPresentation.h` owns Frontier and Legacy detail rows: readiness, mission difficulty, next transfer target, blueprint progress, losses, and furthest tier. Panels should render these rows instead of rebuilding program-progress detail modals inline.
- Legacy details should include recovered surface resources and artifact counts so the research/resource loop is inspectable without adding a separate inventory screen too early.
- `src/core/HangarPresentation.h` owns Hangar Ops card presentation: operation titles, details, costs, action IDs, availability, and card classes derived from `HangarOperationPreview`. Panels should render these cards instead of branching on repair/training/rest/recruit state.
- `src/core/ContentIds.h` owns persistent content IDs and unlock keys for modules, crew facilities, frames, astronauts, and destinations. Content definitions, save migrations, tests, and scripted rewards should use these shared IDs instead of raw strings.
- `src/core/SaveSchema.h` owns the current save header, field keys, and line-format delimiters. Serializer, parser, and migration tests should use these shared constants instead of duplicating save strings.
- `src/core/Telemetry.h` owns telemetry channel metadata and helpers. Simulation, UI, and tests should iterate the shared channel list instead of hand-listing `TEMP`, `PRESS`, `VIB`, `NAV`, `MIX`, and `ABORT`.
- `src/core/GameUi.h` owns stable cross-platform panel action IDs and modal IDs. `GamePanel` emits these data-like IDs, and both native and web dispatch them through the shared app. Avoid embedding JavaScript snippets such as `rr.someAction()` in generated markup.

Telemetry equation constants live under `tuning::telemetry`: pulse profiles, early/late channel buildup, readable minimums, abort certainty, and telemetry-driven stress. Balance the feel of warning dials there before changing formula structure.

Outcome math should also stay tuned from one place. Survival odds, recovery risk, rescue costs, ship damage curves, useful-data thresholds, blueprint share thresholds, and post-flight crew stress all live under `tuning::outcomes` or `tuning::stress` so balance changes do not require spelunking through launch resolution branches.

Post-launch crew stress should flow through `postLaunchCrewStress` / `postLaunchCrewStressGain` in `src/core/GameState.*`. That helper exposes base stress, warning contribution, abort contribution, facility relief, and total stress so future events, facilities, and UI can share one model.

When adding a new mechanic, prefer adding the math knobs to `Tuning.h`, the visible wording to `GameText.h`, and any reusable channel/event metadata to a small core helper before wiring the behavior into `GameState`, `LaunchSimulation`, `RocketGameApp`, or `GamePanel`.

## Persistence

The campaign save format is versioned and line-based. Version 3 introduced the saved refit entitlement and generated offer IDs. Version 4 added acknowledged activity briefs and Prospector progress. Version 5 added active mining and Support Drone simulation. Version 6 persists independent operator/rig state, vector gravity, loose chunks, disabled-rig state, artifact tether state, and each Support Drone's anchor/formation simulation. Version 7 adds explicit Moon/Mars contract claims, Io/Hazard/artifact progression, layered lava-seal state, Drone Upgrade Credits, and the permanent Saturn slingshot gate. Version 8 adds purchased duplicate Support Drone frames. Version 9 replaces authoritative campaign-specific progress with saved generic scenario instances, mining-site provenance, layered cocoon/protected-objective state, and idempotent rewards while migrating all earlier progression without replaying it. Migration preserves existing Support Drones and upgrades, de-duplicates only legacy repeated loadout references, backfills unreachable earlier gates, and never rolls Saturn-or-later saves backward. `SaveSchema.h` remains authoritative for field names, defaults, and migrations.

Display, accessibility, debug, fullscreen, and controller settings are separate from campaign data behind `IPreferenceStore`. The web adapter persists browser-local preferences; native builds use `preferences_v1.txt` beside the native save.
