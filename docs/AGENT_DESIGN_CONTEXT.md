# Agent Design Context

This is the fast orientation file for future agents working on Rocket Rogue / Orebit. When design sources conflict, follow the priority order below.

## Design Priority Order

1. The [OREBIT Game Design Document](Rocket_Rogue_Game_Design_Document.docx) is the definitive design.
2. Supporting system notes explain its implementation and must remain consistent with it.
3. Code and tests establish actual implemented behavior. Reconcile discrepancies with the GDD; mark unresolved story/progression relationships TBD rather than inventing a redesign.
4. Files under `reference/` are preserved inspiration and historical context. They do not override the GDD.

GDD Section 8 identifies the pending story/progression decisions S1-S6. Preserve compatible premise, characters, chapters and progression while those specific decisions remain open.

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

- Physical Flight connects launch, Travel, Orbit and local Landing. Controls govern momentum; coast capture takes two real seconds without thrust on a qualifying loop.
- Pulse Survey and Orbital Laser Dig prepare real terrain in Flight. All six fixed sectors are enabled and persist independently. Survey reach and Bore reach independently constrain the shaft; ore remains physical cargo.
- Manual descent preserves momentum; the explicit Land command stops and aligns before resuming gravity. Real support and rig clearance permit surface or underground touchdown.
- Touchdown, deployment, packing and manual ascent share terrain, ship position and camera continuity. Mining clocks begin only after deployment.
- Mining uses separate Rig/EVA actors, independent oxygen and powered-use Rig fuel. Recover physical payload to the parked ship; drones cannot bank cargo by elapsed transit time.
- Keep campaign goals and rewards explicit. Opening teaching, economy, departure-grade/data gates, Ark/endgame integration and crew development have specific TBD entries in the GDD.
- Typed content owns named beats and route requirements; reusable systems consume typed state and events. See [SCENARIO_FRAMEWORK.md](SCENARIO_FRAMEWORK.md).

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
- Value affects the incentive to excavate and explore deeper.

## Crew Direction

Crew are authored animal specialists with fixed class perks. Training, stress and rest are not active mechanics. Expanded crew development is TBD S5 in the GDD:

- Capybara Tank: survival, endurance, oxygen, safety.
- Beaver Engineer: resilience, repairs, drill integrity.
- Fox Ace: navigation, extraction, abort safety.
- Prairie Dog Scout: digging, scanning, tunnel efficiency.
- Squirrel Hoarder: resource gathering and rare material yield.
- Chipmunk Speedster: exploration, movement, traversal.

## Support Drones And Active-Actor Defense

See [MINI_DRONE_SYSTEM.md](MINI_DRONE_SYSTEM.md) for the active Drone Bay design and current implementation slice.

Support Drone systems start as environmental mining support in the solar system. Arkfall grants the emergency Mk I Attack/Defense kit and raises undersized bays to three slots. Advanced combat choices also respect first-hostile-contact eligibility. Research/unlock pacing is TBD S2 in the GDD. Mk II/Mk III ranks are temporary Transport-run choices, not permanent material purchases.

Support Drones belong to the player, not the Mining Rig. Each independent unit resolves `MiningAnchorTarget { ControlledActor, Rig, Operator }` into an active `MiniDroneAnchorFrame` every fixed update. `MiniDrone*` remains a legacy internal C++ identifier; UI and design copy use Support Drone. Equipped units default to `ControlledActor`, follow/orbit/defend whichever actor is controlled, and transfer without same-layer snapping. Following and defense outrank finishing remote tasks; cross-depth transfers preserve haul, shields, cooldowns, stable formation slot, and orbit phase.

Early Support Drone roles:

- Mining: bounded nearby ore work and loose-chunk collection.
- Resource: physical loose-resource and fuel-cell hauling; no synthesized fuel.
- Survey: scanner radius, POI hints, fog-of-war reads.
- Hazard Support Drone: commissioned on Io, where Mk I deterministically cools ore-bearing Thermal lava into gray Common Ore; outside Io it converts eligible Thermal/Cryo pockets at Mk I, Toxic at Mk II, and Radiation at Mk III into safe mineable terrain.

Post-solar Support Drone roles:

- Attack: autonomous enemy fire relative to the active actor.
- Defense: threat-facing orbit, interception, shielding, and hazard/enemy damage relief for rig health or suit integrity.

This retains loadout-driven autonomous swarm combat while allowing a vulnerable EVA operator to aim a fixed sidearm. It does not introduce enemies before Arkfall near Khepri Prime: the solar system and Aaru Vale remain enemy-free.

## Ark And Base Progression

The GDD fixes open physical solar travel with Earth as initial service home. Moon, Mars, Io, Saturn, Uranus and Neptune supply six unique batteries; the derelict Straylight can be found early and becomes home only after explicit activation with all six installed. Batteries remain physical cargo at risk, can be stored at Earth, and must then be carried to the Ark; losses produce recoverable wreck ownership. Rank I ship tracks are available at Earth; ranks II/III require two/four distinct batteries ever banked or installed. Preserve the 20-ore Moon and 8-ore Mars contracts and Io Hazard Drone sequence. Initialized solar expeditions use real travel, recommended leads, Earth docking, carried salvage, deterministic Rank I installation, and wreck recovery. Preserve those shared operations; a scenario claim cannot create a replacement flight or reset its resources. Physical battery missions and Ark/post-solar integration remain GDD S3/S4 work. See [Persistent Expeditions](PERSISTENT_EXPEDITIONS.md).

Ship sections such as Bio Farm, Robotics, Medical, Living, Command, Engineering, Science, Cargo/Hangar, Environmental, and Cultural systems can become future unlock families.

## Implementation Bias

Prefer incremental systems that hook into the shared C++ application and preserve parity between native Vulkan 1.3 and WebGL2 builds:

- Add content types and presentation helpers before new architecture.
- Save version 21 is the only accepted schema. Older or malformed payloads are rejected and preserved until the player explicitly confirms New Campaign; no migration or automatic replacement is allowed. The v21 checkpoint uses its own storage key and cannot recover an older campaign.
- Make new systems visible through concise UI states.
- Avoid enemies throughout the solar system and Aaru Vale; enemy combat begins only after Arkfall near Khepri Prime.
- Treat Rig fuel as a visible finite physical tradeoff. The tank begins with the expedition allotment and can gain fuel only from physical fuel cells; there is no shared reserve, deployment fee, timed fuel cycle, or separate return stage. Autonomous bay units are Support Drones.
- Keep native and web presentation visually aligned through shared semantic UI and backend-neutral scene packets; do not add a divergent alternate gameplay-UI path.
- Register static art in the required shared texture manifest. The shared inventory includes `JetpackCapybara`, the unlabeled `PoiGuidanceArrow`, and the reusable `Asteroid`; missing artwork or registration is a validation error, not a fallback-sprite case. Packaging validates every atlas page named by metadata.

Incoming Messages are reusable content-driven modal cards. Register speakers and message variants in the content catalog, enqueue stable occurrence IDs through IncomingMessages, and consume typed acknowledgement results in the owning system. Do not add character/mission branches to the renderer. Keep portraits in the shared texture manifest, and preserve explicit saved acknowledgement, modal priority and simulation/input fences. See SCENARIO_FRAMEWORK.md for the lunar integration.
