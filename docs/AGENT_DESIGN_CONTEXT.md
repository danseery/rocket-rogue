# Agent Design Context

This is the fast orientation file for future agents working on Rocket Rogue / Orebit. When design sources conflict, follow the priority order below.

## Design Priority Order

1. `docs/reference/USG_NOTES.md` is the primary direction.
2. Current playable Rocket Rogue behavior is next. Preserve the working launch, arrival, surface, and mining loop unless the user asks for a larger redesign.
3. `docs/reference/ROGUELIKE_OUTLINE.md` supports Ark/base, crew, survival, and long-run structure.
4. `docs/reference/ROGUELITE_ELEMENTS.md` supports roguelite vocabulary and digging/mining patterns.
5. Older docs in this repo are useful context, but USG Notes overrides generic roguelite boilerplate.

Source PDFs are preserved under `docs/reference/source-pdfs/` when they do not contain account-specific URLs. Markdown extracts are the agent-readable source of truth for repo work.

## Current Direction

The game is becoming a chunky, mobile-readable, Straylight-inspired space-mining roguelite. Rocket Rogue is still the spine: press-your-luck launch, survive the trip, reach a destination, then decide how much surface value to risk before returning to refit.

For mining physics and recovery, *Solar Jetman* is an internal mechanical touchstone: destination gravity, inertia, vehicle-versus-suit roles, towing burden, vulnerable recovery, and physically returning discoveries home. Primary/manual references are the [original NES manual](https://www.gamingalexandria.com/highquality/NES/Solar%20Jetman/Solar%20Jetman%20-%20Manual%20%28Searchable%29.pdf) and [official Rare Replay manual](https://dlassets-ssl.xboxlive.com/public/content/367297b7-c6a3-4496-83ad-cb70c52ce8cd/GameManual/2e5e2560-e901-414b-87fa-081a07f24c6c/en-SA/index.html#SolarJetman). OREBIT deliberately adds voluntary EVA, twin-stick aim, hand drilling, suit-only passages, Support Drones, and explicit tether control.

The desired aesthetic is:

- Chunky, blocky, geometric.
- Readable at game speed.
- Retro arcade with modern polish.
- Colorful but not noisy.
- Built around clear state changes and tactile feedback.

## Core Game Shape

- Launch loop: teach Fuel, Flight Controls, Temperature, and Hull one at a time. The opening 10-unit tank completes only the exact halfway Moon survey and return; 15 is required for 10-unit transit plus 5-unit insertion. Controls add seeded event-based overshoot without reversing input, Mars adds engine-cut cooling, and Jupiter adds a guaranteed-steerable asteroid belt. Jupiter's five calibrated fuel margin can come independently from permanent Fuel Tanks III, a one-attempt Good-or-better Mars departure slingshot, or both; neither option disables the other. Good supplies the full five-fuel saving and achieved velocity but adds `+0.35` flight instability for that attempt, while Perfect supplies the same transfer benefits without the penalty. There are no live hidden crash rolls, pressure controls, launch cargo jettison, or manual eject.
- Arrival loop: reaching a destination should feel rewarding, then ask whether to flyby, orbit, or land.
- Surface loop: landing opens Survey Site or Push Deeper to prepare one fuel-gated mining run, followed by payload extraction. Permanent Survey Array and Bore System ranks independently cap mapped and reachable depth; exact surveyed layers and a non-critical return estimate gate Dig while caution remains allowed. Permanent Rig Fuel Loop ranks extend the unloaded operating cycle from 15 seconds to 18/21/24 seconds without changing the exact 1-fuel deployment cost or oxygen. Push Deeper guarantees its first bankable layer; deeper attempts risk the route. The rig starts at the selected depth while the ship stays at surface depth 0. Mandatory Moon/Mars delivery contracts and their explicit claim actions create the first two Drone Bay slots; Io commissions the first Hazard Support Drone and gates its artifact behind layered lava. Open slots may fabricate paid duplicate frames. Expedition damage can end a dig without deleting claimed unlocks.
- Scenario boundary: named campaign beats, destinations, layer labels, presentation copy, and rewards live in typed content; generic scenario, mining, Flyby, route, reward, and UI code consumes typed definitions/events instead of branching on those names. See [SCENARIO_FRAMEWORK.md](SCENARIO_FRAMEWORK.md) before adding a progression beat, protected objective, or generated contract.
- Mining mini-game: separate rig/operator actors, destination gravity and inertia, voluntary jetpack EVA, destructible chunked terrain, suit-only passages, fog of war, scanner pulses, drill friction, ore pockets, loose chunks, physical artifact tethering, transferable Support Drones, 30s baseline oxygen, arrival-derived rig-fuel draw, protected return stage, cargo, and extraction risk. The rig carries ore; the suit carries none and may tow only an artifact. Keyboard rig drilling defaults to Toggle and can be set to Hold; mouse/controller EVA actions remain hold-based. Heat cuts drilling off at 100% and unlocks below 60%.
- Long-term loop: recovered materials, blueprints, artifacts, and research unlock better tools, Drone Bay options, permanent ship systems, Ark/base systems, and future story threads.

## Planet And Resource Pillars

USG Notes frames planets around readable tradeoffs:

- Danger: hazards, hostile conditions, later enemies.
- Value: resource quality, artifacts, research payoff.
- Durability: how hard the terrain or target is to break through.
- Gravity: how strongly the destination shapes thrust, inertia, towing, and recovery.

Gravity is a direction-plus-strength vector. Initial destinations pull downward at base `6 cells/s²` times the following scale: Earth Orbit `0.15`, Moon `0.35`, Mars `0.60`, Jupiter `1.15`, Saturn `0.95`, Uranus `0.80`, Neptune `1.05`, Khepri Prime `1.20`, and Rift Belt `0.25`.

Resource and object pillars should also stay understandable:

- Weight affects extraction/cargo strain.
- Durability affects drill time and tool wear.
- Value affects reward and the reason to use Push Deeper.

## Crew Direction

Generic crew placeholders have been replaced with animal specialists. Training still levels them, stress still matters, and their class/focus should color both launch and surface play:

- Capybara Tank: survival, endurance, oxygen, safety.
- Beaver Engineer: resilience, repairs, drill integrity.
- Fox Ace: navigation, extraction, abort safety.
- Prairie Dog Scout: digging, scanning, tunnel efficiency.
- Squirrel Hoarder: resource gathering and rare material yield.
- Chipmunk Speedster: exploration, movement, traversal.

## Support Drones And Active-Actor Defense

See [MINI_DRONE_SYSTEM.md](MINI_DRONE_SYSTEM.md) for the active Drone Bay design and current implementation slice.

Support Drone systems start as environmental mining support in the solar system. Arkfall grants the emergency Mk I Attack/Defense kit and raises undersized bays to three slots. Perimeter Drone Network research then unlocks Perimeter Coordination, making advanced combat grafts and named synergies eligible in Expedition Level Up drafts. Mk II/Mk III ranks are temporary Transport-run choices, not permanent material purchases.

Support Drones belong to the player, not the Mining Rig. Each independent unit resolves `MiningAnchorTarget { ControlledActor, Rig, Operator }` into an active `MiniDroneAnchorFrame` every fixed update. `MiniDrone*` remains a legacy internal C++ identifier; UI and design copy use Support Drone. Equipped units default to `ControlledActor`, follow/orbit/defend whichever actor is controlled, and transfer without same-layer snapping. Following and defense outrank finishing remote tasks; cross-depth transfers preserve haul, shields, cooldowns, stable formation slot, and orbit phase.

Early Support Drone roles:

- Mining: bounded nearby ore work and loose-chunk collection.
- Resource: oxygen/fuel reserve and run extension.
- Survey: scanner radius, POI hints, fog-of-war reads.
- Hazard Support Drone: commissioned on Io, where Mk I deterministically cools ore-bearing Thermal lava into gray Common Ore; outside Io it converts eligible Thermal/Cryo pockets at Mk I, Toxic at Mk II, and Radiation at Mk III into safe mineable terrain.

Post-solar Support Drone roles:

- Attack: autonomous enemy fire relative to the active actor.
- Defense: threat-facing orbit, interception, shielding, and hazard/enemy damage relief for rig health or suit integrity.

This retains loadout-driven autonomous swarm combat while allowing a vulnerable EVA operator to aim a fixed sidearm. It does not introduce enemies before Arkfall near Khepri Prime: the solar system and Aaru Vale remain enemy-free.

## Ark And Base Progression

See [SCENARIO_FRAMEWORK.md](SCENARIO_FRAMEWORK.md) for the active campaign spine. Moon, Mars, Jupiter/Io, Saturn, Uranus, and Neptune are mandatory visible arrivals; Earth Orbit remains only an internal origin. Moon and Mars mining claims, safe Io artifact recovery, a claimed Perfect Jupiter slingshot, the returned Saturn artifact, and the authored Uranus Neptune Vector are explicit route gates. The Saturn launch begins the one-way outer expedition; from that point, recovery copy says `Recover to Expedition` rather than promising an Earth return. At Uranus, a repeating carrier signal beyond Neptune foreshadows that something is there, but the Straylight is not named, drawn, or tracked. The first permanent Uranus artifact and one Good or Perfect Orbit supply the Neptune Vector's two Flight Data keys; repeat landings and repeat artifacts do not advance it. The player explicitly claims `Lock Neptune Course`; only a successful physical Uranus -> Neptune arrival emits the typed arrival event. Claiming that objective opens the saved full-screen discovery briefing that identifies the derelict-but-operable Ark. Acknowledging its sole approach action enables Ark/home framing and resumes Neptune Arrival Ops.

Ship sections such as Bio Farm, Robotics, Medical, Living, Command, Engineering, Science, Cargo/Hangar, Environmental, and Cultural systems can become future unlock families.

## Implementation Bias

Prefer incremental systems that hook into the shared C++ application and preserve parity between native Vulkan 1.3 and WebGL2 builds:

- Add content types and presentation helpers before new architecture.
- Save version 16 is the only accepted schema. It persists permanent Survey/Bore ranks plus current scenario, Arrival, Surface, Mining, Expedition XP, queued-choice, run-rank, graft, and synergy state; every non-v16 or malformed payload is cleared and immediately replaced with a fresh campaign while preferences remain intact. A stable-hub checkpoint is recovery-only and is never used to infer or repair progression.
- Make new systems visible through concise UI states.
- Avoid enemies throughout the solar system and Aaru Vale; enemy combat begins only after Arkfall near Khepri Prime.
- Treat rig fuel as an explicit tradeoff. The UI and docs should show the 3-unit expedition pack, transfer fuel recovered at touchdown, and the separate return stage; autonomous bay units are Support Drones.
- Keep native and web presentation visually aligned through shared semantic UI and backend-neutral scene packets; do not add a divergent alternate gameplay-UI path.
- Register static art in the required shared texture manifest. The runtime texture count is 37, including `JetpackCapybara`, the unlabeled `PoiGuidanceArrow`, and the reusable `Asteroid`; missing artwork or registration is a validation error, not a fallback-sprite case. Packaging validates every atlas page named by metadata.
