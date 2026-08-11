# Rocket Rogue Design Notes

For future design work, start with `docs/AGENT_DESIGN_CONTEXT.md`. It links the extracted PDF sources and marks `docs/reference/USG_NOTES.md` as the highest-priority direction.

## Pillars

- Skill-based launch tension with visible fuel, control, temperature, and hull tradeoffs and no real-money gambling mechanics.
- Ship-first management through readable permanent systems, expedition damage, and meaningful upgrade tracks.
- Light but painful crew consequences.
- Roguelite persistence through unlock variety, records, memorials, and blueprints.
- Asset-light proof of concept using backend-neutral procedural primitives, RmlUi mission-control panels, and replaceable arcade sprites across native Vulkan and WebGL2 builds.

## Core loop

1. Begin with the Moon fuel survey: fixed 60% throttle, a 10-unit tank, and only the Fuel gauge.
2. Turn around at the halfway light, return on the exact five-unit reserve, and spend the 22-credit survey reward on Fuel Tanks I.
3. Complete a second out-and-back calibration with visible corridor steering, persistent throttle, and deliberately loose controls; install Flight Controls I.
4. Reach the Moon using 10 transit fuel plus the displayed 5-unit insertion reserve, then complete its existing surface contract.
5. Add Temperature and `Engines Off` for the Mars thermal qualification. Engine cuts always cool and coasting cannot finish the route for free.
6. Add Hull and a guaranteed-steerable asteroid belt for Jupiter. Skilled no-hit play can bypass Hull Plating, while collisions make the upgrade valuable.
7. Use `Turn Around` to fly the same stateful ship home from surveys. Training failures are non-destructive rescues; frontier-transfer failures retain normal consequences.
8. Continue through the authored surface, Flyby, Orbit, and outer-system progression without changing those minigames.

## Post-arrival research loop

Surface expeditions start at the Moon with a narrow mining lesson; broader research still starts at Mars. The launch curriculum reaches for the Moon immediately, while Earth Orbit remains only as a hidden compatibility origin. The Moon teaches inert regolith versus gray Common Ore and explicit contract delivery, and Mars turns that lesson into the second Drone Bay slot and wider long-term capability.

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

Surface exploration should stay distinct from active launch piloting. Before Saturn, the launch loop asks "can we fly there and back?" The surface loop asks "how much can we safely recover before the expedition overextends?" Claiming the Saturn course commits the expedition outward; later recovery copy says `Recover to Expedition`, never promises a return to Earth, and changes to `Return to Ark` only after the Straylight discovery. The solar system and Aaru Vale do not have enemies. Enemy encounters begin only after Arkfall near Khepri Prime, when the game leaves familiar exploration and introduces hostile unknowns.

Shared fuel is intentional friction in the surface loop. The shuttle and Mining Rig draw from the same reserve, so mining should be visibly framed as spending route-home margin for payload. Push Deeper guarantees and banks layer +1 before collapse risk begins on the layer +2 gamble; a scanned artifact is confirmed when its mapped layer succeeds. The rig deploys directly at the banked start depth while the ship remains fixed at surface depth 0, so extraction and service require ascending through the intervening layers. The current normal mining baseline is 30 seconds of oxygen; the fixed Io artifact introduction uses 60 seconds. Oxygen improvements can come from crew class, Support Drone loadouts, and surface upgrades, but mining remains a once-per-surface-loop commitment; after the run is used, the rig is offline and `Push Deeper` is unavailable.

Mining uses one extensible POI pointer with runtime labels. Safety pressure from oxygen, the controlled actor, or the drill overrides ordinary targets and leads toward `SHIP`; otherwise a revealed recoverable artifact receives `ARTIFACT` guidance, including ascent/descent boundary guidance when it is on another layer.

Deterministic protected-objective sites turn those mining tools into forecastable keys without rubber-banding arena difficulty from the equipped loadout. Surface Ops and Drone Ops should name the upcoming gate, direct capability, current readiness, and systemic alternatives. Hazard treatment, Survey triangulation, careful excavation, heavy towing, endurance, autonomous swarm combat, EVA self-defense, terrain cover, and route planning must all reuse the same saved Act/level/seed gate contract as generation and runtime validation. A protected site completes only when its configured objective is delivered, banked, and survives Surface extraction. The scenario/content boundary is documented in [SCENARIO_FRAMEWORK.md](SCENARIO_FRAMEWORK.md).

### Mechanical touchstone and EVA identity

*Solar Jetman* is an internal mechanical touchstone for destination-sensitive gravity, inertia, vehicle-versus-pilot roles, towing burden, vulnerable recovery, and physically returning discoveries home: [original NES manual](https://www.gamingalexandria.com/highquality/NES/Solar%20Jetman/Solar%20Jetman%20-%20Manual%20%28Searchable%29.pdf) and [official Rare Replay manual](https://dlassets-ssl.xboxlive.com/public/content/367297b7-c6a3-4496-83ad-cb70c52ce8cd/GameManual/2e5e2560-e901-414b-87fa-081a07f24c6c/en-SA/index.html#SolarJetman). OREBIT differs through voluntary EVA, twin-stick aim, hand-drilled terrain, suit-only passages, Support Drones, and explicit tether control.

Mining runs start in the rig. The rig reaches `7.2 cells/s` with `14 cells/s²` acceleration, `20 cells/s²` braking, and a `0.48`-cell collider. The suit reaches `4.6 cells/s` with `28 cells/s²` acceleration, `24 cells/s²` braking, and a `0.25`-cell collider. Both use vector gravity with base strength `6 cells/s²` multiplied by destination scale: Earth Orbit `0.15`, Moon `0.35`, Mars `0.60`, Jupiter `1.15`, Saturn `0.95`, Uranus `0.80`, Neptune `1.05`, Khepri Prime `1.20`, and Rift Belt `0.25`.

The suit carries no ore. Its hand-drilled ore and enemy rewards become loose chunks, while a tethered artifact remains the sole cargo exception. The suit is slower and vulnerable but accelerates quickly, fits through narrow passages, and can change depth while the parked rig remains behind. A destroyed rig emergency-ejects the operator; a destroyed suit ends the run.

Support Drones belong to the player rather than the Mining Rig. They follow, orbit, and defend the controlled actor through a transferable logical anchor while retaining independent positions, velocities, haul, shield state, cooldowns, stable formation slots, and orbit phases. Same-layer transfers do not snap them; cross-depth transfers rebuild deterministic formations. Artifact tether ownership stays independent.

Research rewards should primarily widen the roguelite possibility space: module families, research facilities, special components, artifact threads, and story leads. Material-funded projects can directly unlock new module or facility families. Artifact-tagged projects identify one recovered artifact when possible; the identified record is tracked now, while its specific story payload remains a later content pass. Raw permanent stat inflation should remain secondary.

## Skill-based launch model

Active launches are deterministic stateful flights. A session-only `LaunchFlightState` carries mission kind, route leg/progress, absolute fuel and reserve, selected throttle, course dynamics, heat, hull, asteroid state, warning timers, and an explicit terminal cause. Version-9 hidden-crash fields remain readable for old saves and records but never decide live survival or rewards.

- Fuel Survey hides the corridor and ignores steering/throttle input. At fixed 60% throttle, Moon transit costs 10 fuel and insertion costs 5. The base 10-unit tank can complete the exact 5-out/5-back survey but cannot arrive.
- Controls Calibration reveals persistent throttle and steering. Input direction is always honored. With chaos `c`, right gain is `1 + 0.45c`, throttle increases kick laterally by up to `0.35c`, steering response varies by `0.20c`, damping rises from `0.55` to `1.10`, and auto-trim rises from `0.05` to `0.25`. Kicks are seeded, event-based, and cooldown-limited rather than random every frame.
- Mars reveals Temperature and the sole system action, `Engines Off` / `Engines On`. Engine-off thrust and fuel use are zero, selected throttle is preserved, heat always decreases, steering remains reduced, and coasting decays to zero in about 1.5 seconds. Heat cautions at 70%, becomes critical at 90%, and fails only after 1.5 seconds at 100%.
- Jupiter reveals Hull and ten session-only asteroids in five rows. Each row blocks two of three lanes, adjacent openings move by at most one lane, and seeded jitter/scale/rotation varies the belt without creating an impossible route. Swept impacts occur once per asteroid per leg with 0.75 seconds of invulnerability.
- `Turn Around` preserves fuel, course, heat, hull, and the asteroid layout across the leg change. Reaching home is resolved before exact-zero fuel failure so the taught reserve succeeds.
- Pressure, relief valves, valve drift, cargo jettison, manual eject, live telemetry incidents, and hidden launch odds are not part of the live launch model.
- Failure count never changes live difficulty. Installed ranks and player execution are the improvement path.

Crew stress is a separate human-performance modifier:

- Every 14 stress is one stress step.
- Each stress step cancels one effective training level for launch performance.
- Each stress step adds a small `NAV` penalty to represent piloting mistakes under load.
- `ABORT` scales by stress steps from x1.00 at calm to x2.00 at maximum stress.
- Simulator burns add training and stress; rest removes enough stress to erase at least one step in most practical cases.

## Direct Launch upgrades

The launch curriculum has four permanent, predictable tracks. Every rank costs 22 credits and Ship Details shows its current rank, next numerical effect, `Install`, and `Keep Credits`:

- Fuel Tanks: capacity `10 / 15 / 20 / 25`.
- Flight Controls: chaos `1.00 / 0.55 / 0.20 / 0.00`.
- Engine Cooling: powered heat `100% / 88% / 76% / 64%`, with engine-off recovery `10% / 14% / 18% / 22%` per second.
- Hull Plating: `100 / 125 / 150 / 175 HP` and impact multipliers `1.00 / 0.80 / 0.65 / 0.50`.

Lesson results expose only the newly taught Rank I card. Later ranks unlock at named route milestones rather than through rerolls: Fuel II at Mars and III at Jupiter; Controls II after Moon and III after Mars; Cooling II after Mars and III after Jupiter; Hull II after Jupiter and III after Saturn. Cooling and Hull add margin but never replace skill.

The retired Reach/Control/Recovery proving cards remain recognizable only during version-9 migration. Generic thrust, sensors, escape, pressure, volatility, payout, and unrelated modules continue serving Flyby, Orbit, Mining, crew, and other systems but do not alter live launch survival.

Crew facilities are refit rewards too. They should sit in Crew Details and improve actual crew math, not just presentation:

- Simulator facilities increase training gain or lower simulator stress.
- Medical facilities improve rest and injury recovery.
- Psychology/coaching facilities reduce post-launch stress and improve astronaut trait modifiers.

Paid repair, training, rest, and rerolls are hidden during the Fuel and Controls lessons so the taught 22-credit loop cannot be bypassed or buried in clutter. The normal 45-credit recovery floor resumes after Moon arrival. Ship Details exposes the four Launch rows separately from other permanent systems; Crew Details continues to show crew facilities and aggregate effects.

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

Flight controls flow through `LaunchControlInput`, `LaunchFlightState`, and `FlightActionState` in `src/core/LaunchSimulation.*`. `RocketGameApp` owns session input and staged action availability, while core owns absolute fuel, selected throttle, course momentum, stateful heat, hull impacts, asteroid layouts, warning timers, and terminal causes. Pressure, generic telemetry incidents, and manual launch ejection remain outside the live curriculum.

Hangar operation cards should be driven by `HangarOperationPreview` from `src/core/GameState.*`. The preview is the shared source for repair amount/cost, simulator gain/stress/cost, rest recovery/cost, recruit cost, and availability so UI cards do not drift from the action functions.

Research and surface-expedition rules should flow through `src/core/ResearchSystem.*`: post-arrival gating, research project generation/completion, material accounting, surface action kits, shared fuel, cargo, extraction risk, surface upgrades, Drone Bay state, and progression-backed surface-contact pressure. Panels and app transitions should consume those helpers instead of duplicating tier checks or resource math.

`src/core/MiningProgression.*` is the authoritative Act/level resolver shared by campaign mapping, Surface Ops forecasts, debug requests, terrain/reward gates, and enemy generation. `src/core/MiningSystem.*` consumes those rules for terrain generation, vector gravity, independent rig/operator physics, loose chunks, oxygen/fuel/drill timers, scanner pulses, sidearm raycasts, artifact tether forces, unified ore/artifact rewards, hostile tunnel networks, finish/abort/failure outcomes, and conversion back into `SurfaceActionOutcome`. `src/core/MiniDroneCoordination.*` consumes a resolved `MiniDroneAnchorFrame`; these `MiniDrone*` names are legacy internal C++ identifiers, while Support Drone behavior and presentation must not reach directly for rig coordinates. Platform input adapters should call `RocketGameApp` mining methods or dispatch shared actions for aim, fire, drill, scan, tether, operator switching, and bank/leave. Rendering should consume snapshots rather than deciding mining outcomes.

### Scenario authoring and save compatibility

Author a reusable progression beat by adding a versioned `ScenarioDefinition`, unique local step IDs, prerequisite edges, player-facing presentation, a typed `ScenarioEventKind`, the explicit `ScenarioActionKind`, and typed rewards. Reward kinds can grant unlock keys, destination route access, Support Drones, bay capacity, upgrade credits, or banked Common/Rare/Exotic materials. A fixed mining activity references a versioned `MiningSiteDefinition`; a Flyby step uses `FlybyFinished` plus `requiredGrade`. Route gates are content data: a destination requires unlock keys, while a `RouteAccess` reward resolves that destination's configured keys without route evaluation recognizing a story destination. `validateScenarioCatalog()` rejects duplicate IDs, cyclic prerequisites, invalid rewards, unknown mining sites, invalid cocoons, and procedural factories whose template would instantiate by default.

Save version 10 retains version 9's authored and procedural `ScenarioInstance` identity, mining-site provenance, protected-objective state, and reward ledger while adding four launch-upgrade ranks and four lesson-completion flags. Version 8 and older saves still synthesize scenario instances before the v10 launch migration runs. A pristine v9 campaign starts the zero-credit Moon tutorial; progressed saves retain credits and receive conservative lesson/rank credit based on their old readiness, location, and retired launch modules.

Shared game constants and player-facing copy should have one owner:

- `src/core/Tuning.h` owns balance values such as refit costs, crew stress steps, mission difficulty, action tradeoffs, launch pacing, warning thresholds, and reward shelves.
- `src/core/GameText.h` owns reusable display text: status lines, telemetry warning copy, core labels, enum display labels, button labels, module stat labels, and module threat wording.
- `src/core/GameFormat.h` owns reusable numeric display formatting such as credits, signed deltas, multipliers, percentages, readiness fractions, damage summaries, and crew stress/training summaries.
- `src/core/GameMath.h` owns reusable equation helpers such as clamped `smoothStep` shaping. Do not duplicate easing or shaping formulas inside app, panel, or simulation code.
- `src/core/FlightProgress.h` owns shared travel/return progress equations: burn-depth-to-route progress, return completion, return visual travel, and return duration. App, panel, and renderer-facing snapshots should use these helpers instead of retyping the same progress math.
- `src/core/SaveData.*` and legacy record fields retain only the data needed to read version-9 launch history. The retired hidden-crash and generic telemetry helper code is removed; `LaunchSimulation` owns the deterministic Fuel, Controls, Temperature, and Hull curriculum.
- `src/core/DetailPresentation.h` owns reusable detail-row/header data for modal detail screens. Core presenters should return these rows, and `GamePanel` should only render them to HTML.
- `src/core/PanelPresentation.h` owns small reusable panel primitives such as metric and button presentation data. Screen-specific presenters should reuse these data shapes instead of inventing local copies.
- `src/core/PanelChromePresentation.h` owns shared panel chrome data: top-level mission metrics, active display destination, crew stress summary, and settings modal rows/actions. `GamePanel` should render this data instead of recomputing always-visible metrics.
- `src/core/LaunchPresentation.h` owns the staged launch-screen presentation: lesson objective, absolute fuel and insertion reserve, optional throttle/corridor, optional Temperature and engine toggle, optional Hull and asteroid state, and Turn Around availability. `GamePanel` should render this prepared data rather than recomputing lesson visibility or flight-control state.
- `src/core/LaunchReadinessPresentation.h` owns launch-hold presentation and readiness gating display: hull/crew blocked state, hold messages, required action detail, and repair/recruit actions. Panels should consume this object instead of recomputing launch-block rules inline.
- `src/core/OutcomePresentation.h` owns result-screen labels, follow-up action labels, and outcome note copy derived from `LaunchOutcome`. Panels should render this presentation data instead of duplicating outcome/recovery branching.
- `src/core/RefitPresentation.h` owns refit-window presentation. Launch lessons expose one direct upgrade card, while Ship Details exposes the four compact deterministic Launch Upgrade rows with current rank, next numerical effect, flat cost, install state, and Keep Credits. Non-launch module and crew-facility presentation retains its existing typed offer rules.
- `src/core/ResearchPresentation.h` owns research and surface-expedition presentation: blueprint/material metrics, research project cards, surface supply/cargo/risk metrics, and field action availability. Panels should render this returned data instead of rebuilding research/resource rules inline.
- `src/core/MiningPresentation.h` owns mining HUD and detail presentation: mode, gravity, oxygen, shared fuel, rig health, suit integrity, drill heat, tether burden, loose-chunk count, `Suit carry: 0`, Support Drone anchor status, scanner/fuel cadence, hostile tunnel summaries, action buttons, and controls copy.
- `src/core/CrewPresentation.h` owns Crew Details rows and facility-effect value wording. Panels should render detail rows and headers from this helper instead of recomputing training, stress, facility, and trait modifier strings.
- `src/core/ShipPresentation.h` owns Ship Details rows, installed/offline module summaries, and inventory fallback wording. Panels should render those rows instead of recomputing ship stats and module inventory display.
- `src/core/ProgramPresentation.h` owns Frontier and Legacy detail rows: readiness, mission difficulty, next transfer target, blueprint progress, losses, and furthest tier. Panels should render these rows instead of rebuilding program-progress detail modals inline.
- Legacy details should include recovered surface resources and artifact counts so the research/resource loop is inspectable without adding a separate inventory screen too early.
- `src/core/HangarPresentation.h` owns Hangar Ops card presentation: operation titles, details, costs, action IDs, availability, and card classes derived from `HangarOperationPreview`. Panels should render these cards instead of branching on repair/training/rest/recruit state.
- `src/core/ContentIds.h` owns persistent content IDs and unlock keys for modules, crew facilities, frames, astronauts, and destinations. Content definitions, save migrations, tests, and scripted rewards should use these shared IDs instead of raw strings.
- `src/core/SaveSchema.h` owns the current save header, field keys, and line-format delimiters. Serializer, parser, and migration tests should use these shared constants instead of duplicating save strings.
- `src/core/GameUi.h` owns stable cross-platform panel action IDs and modal IDs. `GamePanel` emits these data-like IDs, and both native and web dispatch them through the shared app. Avoid embedding JavaScript snippets such as `rr.someAction()` in generated markup.

Telemetry equation constants live under `tuning::telemetry`: pulse profiles, early/late channel buildup, readable minimums, abort certainty, and telemetry-driven stress. Balance the feel of warning dials there before changing formula structure.

Outcome math should also stay tuned from one place. Survival odds, recovery risk, rescue costs, ship damage curves, useful-data thresholds, blueprint share thresholds, and post-flight crew stress all live under `tuning::outcomes` or `tuning::stress` so balance changes do not require spelunking through launch resolution branches.

Post-launch crew stress should flow through `postLaunchCrewStress` / `postLaunchCrewStressGain` in `src/core/GameState.*`. That helper exposes base stress, warning contribution, abort contribution, facility relief, and total stress so future events, facilities, and UI can share one model.

When adding a new mechanic, prefer adding the math knobs to `Tuning.h`, the visible wording to `GameText.h`, and any reusable channel/event metadata to a small core helper before wiring the behavior into `GameState`, `LaunchSimulation`, `RocketGameApp`, or `GamePanel`.

## Persistence

The campaign save format is versioned and line-based. Version 3 introduced the saved refit entitlement and generated offer IDs. Version 4 added acknowledged activity briefs and Prospector progress. Version 5 added active mining and Support Drone simulation. Version 6 persists independent operator/rig state, vector gravity, loose chunks, disabled-rig state, artifact tether state, and each Support Drone's anchor/formation simulation. Version 7 adds explicit Moon/Mars contract claims, Io/Hazard/artifact progression, layered lava-seal state, Drone Upgrade Credits, and the permanent Saturn slingshot gate. Version 8 adds purchased duplicate Support Drone frames. Version 9 replaces authoritative campaign-specific progress with saved generic scenario instances, mining-site provenance, layered cocoon/protected-objective state, and idempotent rewards. Version 10 adds the four launch-upgrade ranks and lesson milestones, hides Earth Orbit without shifting destination indices, and converts retired launch modules before unknown IDs are pruned. Migration preserves progressed credits, existing Support Drones and upgrades, de-duplicates only legacy repeated loadout references, backfills unreachable earlier gates, and never rolls later saves backward. `SaveSchema.h` remains authoritative for field names, defaults, and migrations.

Display, accessibility, debug, fullscreen, and controller settings are separate from campaign data behind `IPreferenceStore`. The web adapter persists browser-local preferences; native builds use `preferences_v1.txt` beside the native save.
