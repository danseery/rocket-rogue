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

- Launch loop: risk a per-run hidden failure, use the composite Instability warning plus system telemetry, return/eject/mitigate, then compare burn depth with the revealed failure point in the debrief. The yellow marker is a performance target, not a safety boundary.
- Arrival loop: reaching a destination should feel rewarding, then ask whether to flyby, orbit, or land.
- Surface loop: landing opens Survey Site or Push Deeper to prepare one fuel-gated mining run, followed by payload extraction. Mandatory Moon/Mars delivery contracts and their explicit claim actions create the first two Drone Bay slots; Io commissions the unique Hazard Support Drone and gates its artifact behind layered lava. Expedition damage can end a dig without deleting claimed unlocks.
- Mining mini-game: separate rig/operator actors, destination gravity and inertia, voluntary jetpack EVA, destructible chunked terrain, suit-only passages, fog of war, scanner pulses, drill friction, ore pockets, loose chunks, physical artifact tethering, transferable Support Drones, 30s baseline oxygen, shared fuel draw, cargo, and extraction risk. The rig carries ore; the suit carries none and may tow only an artifact. Keyboard rig drilling defaults to Toggle and can be set to Hold; mouse/controller EVA actions remain hold-based. Heat cuts drilling off at 100% and unlocks below 60%.
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

Support Drone systems start as environmental mining support in the solar system. Arkfall grants the emergency Mk I Attack/Defense kit and raises undersized bays to three slots. Perimeter Drone Network research then unlocks Perimeter Coordination for Mk II/Mk III combat tuning and advanced synergies.

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

See [ARK_CAMPAIGN_NAVIGATION_PLAN.md](ARK_CAMPAIGN_NAVIGATION_PLAN.md) for the active campaign spine. Earth Orbit, Moon, Mars, Jupiter/Io, Saturn, Uranus, and Neptune are mandatory individual arrivals. Moon and Mars mining claims, safe Io artifact recovery, and a claimed Perfect Jupiter slingshot are explicit early route gates. The Saturn launch begins the one-way outer expedition; from that point, recovery copy says `Recover to Expedition` rather than promising an Earth return. The Straylight is not named, drawn, tracked, or hinted at before Neptune. After successful Neptune arrival, a saved full-screen discovery briefing identifies the derelict-but-operable Ark; acknowledging its sole approach action enables Ark/home framing and resumes Neptune Arrival Ops.

Ship sections such as Bio Farm, Robotics, Medical, Living, Command, Engineering, Science, Cargo/Hangar, Environmental, and Cultural systems can become future unlock families.

## Implementation Bias

Prefer incremental systems that hook into the shared C++ application and preserve parity between native Vulkan 1.3 and WebGL2 builds:

- Add content types and presentation helpers before new architecture.
- Save version 7 retains separate rig/operator state, vector gravity, loose chunks, disabled-rig state, artifact tether state, and Support Drone simulation while adding explicit Moon/Mars claims, Io progression, upgrade credits, and the permanent Saturn slingshot gate. Older saves restore seated, migrate Support Drones to `ControlledActor`, and de-duplicate repeated loadout IDs.
- Make new systems visible through concise UI states.
- Avoid enemies throughout the solar system and Aaru Vale; enemy combat begins only after Arkfall near Khepri Prime.
- Treat shared fuel as an explicit tradeoff. The UI and docs should make clear that the shuttle and player-controlled Mining Rig spend the same reserve; autonomous bay units are Support Drones.
- Keep native and web presentation visually aligned through shared semantic UI and backend-neutral scene packets; do not add a divergent alternate gameplay-UI path.
- Register the static `JetpackCapybara` art in the required shared texture manifest. The runtime texture count is 35; missing artwork or registration is a validation error, not a fallback-sprite case.
