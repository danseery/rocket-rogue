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

## Persistence

The save format is versioned and line-based for a small dependency-free POC. The web build stores it in `localStorage` via `RocketBridge`. Future production builds should replace this with a JSON or binary schema plus migration tests once the content stabilizes.
