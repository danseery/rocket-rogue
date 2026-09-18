# Mining Mini-Game

Deterministic scenario-authored and optional protected-objective gates are specified in [MINING_LOCK_AND_KEY_SITES.md](MINING_LOCK_AND_KEY_SITES.md). Their reusable authoring contract is [SCENARIO_FRAMEWORK.md](SCENARIO_FRAMEWORK.md). They reuse the physical artifact, scanner, hazard, towing, terrain, EVA, and autonomous-combat systems described here.

The [OREBIT Game Design Document](Rocket_Rogue_Game_Design_Document.docx) is the definitive design. This document supplies implementation detail and must agree with it. Story and progression decisions marked TBD are collected in GDD Section 8.

See `docs/MINI_DRONE_SYSTEM.md` for the persistent Support Drone Bay layer that modifies mining, scanner, logistics, oxygen, extraction, and later autonomous-defense behavior. The `MiniDrone*` names in C++ are legacy internal identifiers; UI and design copy use Support Drone.

Mining is the continuous landed expedition entered after physical touchdown and deployment. Core outcomes reconcile cargo, materials, artifacts and Expedition XP with the expedition; supporting type names do not imply a separate phase board.

## Mechanical Touchstone: Solar Jetman

*Solar Jetman* is the mechanical touchstone for destination-sensitive gravity, inertia, distinct vehicle/pilot roles, towing burden, vulnerable recovery, and physically returning discoveries home. The reference establishes a gravity-sensitive Jetpod, limited fuel, infinite standard fire, reduced thrust while towing, and a vulnerable pilot after pod destruction: [original NES manual](https://www.gamingalexandria.com/highquality/NES/Solar%20Jetman/Solar%20Jetman%20-%20Manual%20%28Searchable%29.pdf) and [official Rare Replay manual](https://dlassets-ssl.xboxlive.com/public/content/367297b7-c6a3-4496-83ad-cb70c52ce8cd/GameManual/2e5e2560-e901-414b-87fa-081a07f24c6c/en-SA/index.html#SolarJetman).

OREBIT deliberately modernizes that foundation with voluntary EVA, twin-stick aiming, hand-drilled terrain, Support Drones that transfer between controlled actors, suit-only passages, explicit tether control, and an artifact-only exception to the suit's zero-cargo rule. This is an internal mechanical reference, not story or presentation canon.

## Design Goal

Mining is the landed version of the rocket launch loop:

- The player chooses how much Rig fuel and oxygen to spend, how much cargo to load, and when to return, recover equipment, or depart.
- Better crew, tools, Support Drones, and surface upgrades make risk more readable and controllable, but never remove it.
- Early solar-system mining is environmental: oxygen pressure, drill heat, hard-rock bounce, hazard pockets, low fuel, and physical cargo pressure.
- Hostile terrain and enemies stay out of the solar system and Aaru Vale. Enemy pressure begins only after Arkfall near Khepri Prime, when the agency is stranded in a hostile system.
- The rig is durable, fast, and cargo-capable; the operator is slower but more agile, accelerates faster, and can enter suit-only passages.
- Recoveries stay physical. Gravity, tether mass, loose chunks, a disabled rig, and the need to return discoveries to the shuttle create the pressure.

## Entry Point

Fly to the destination, capture an orbit where required, optionally survey and excavate from Zone 1, then enter local descent. The ship collides with the actual prepared/cached terrain and may park underground. A two-second touchdown celebration accepts deploy or undeployed departure; a three-second deployment places the Rig and equipped drones beside the parked ship before Mining control and resource clocks begin.

Prepared orbital excavation becomes the exact Mining site. Packing settles payload once and ignition resumes manual local ascent from the parked ship. See [Flight and Surface Flow](POST_ARRIVAL_PHASES.md) for simulation and persistence boundaries.

## Rig Fuel And Oxygen

Mining exists to make surface greed compete with the physical trip home:

- The expedition owns one visible home-supplied Rig tank retained across planetary visits, supplemented by physically recovered fuel cells.
- Thrust and drilling consume fuel while powered; simultaneous use stacks, load increases thrust cost, and coasting/idling are free.
- Fuel cells are persistent physical loose objects and add exactly one unit only after contacting the Rig. Support Drones may carry them but never synthesize fuel.
- Rig oxygen drains while operating away from ship service; suit oxygen drains during EVA. The inactive actor's tank pauses. Ship service refills oxygen, never fuel.
- A zero-fuel Rig remains in the world. EVA may recover a cell, tow the disabled Rig, rescue cargo, or return alone and depart.
- Rig Fuel Loop ranks reduce powered consumption by 10/20/30 percent.

The HUD shows the visible Rig tank, active actor oxygen, integrity, Rig load band, contract allocation, and ship hold. There is no shared reserve, timed whole-unit cycle, deployment fee, or protected return fuel.

## Rig And EVA Core Loop

`MiningOperatorMode { Rig, Jetpack }` selects the controlled actor, and runs begin in `Rig`. The rig and operator are separate saved world actors; changing control does not transform or replace either entity.

1. Pilot the rig through chunked terrain, drill straight ahead, scan hidden seams, tow artifacts, and carry ore.
2. Exit into the jetpack suit from a safe adjacent cell to enter narrow passages, hand-drill, scan, tether an artifact, or defend yourself with the sidearm.
3. Allow autonomous Support Drones to follow, orbit, defend, mine, survey, treat hazards, and collect loose chunks around whichever actor is controlled.
4. Manage oxygen, rig fuel, gravity, inertia, drill heat, integrity, cargo, loose chunks, tether burden, and field hazards. Normal leave recovers all Rig, intact Support Drone, and Ship manifests.
5. Re-enter the rig within `1.25` cells on the same layer, or return to the shuttle under the failure rules below.

The HUD distinguishes `SURFACE`, `START DEPTH +N`, and `SHIP ↑ N`. A single unlabeled arrow asset accepts a runtime POI kind, label, target depth, coordinate, and direction. Revealed recoverable artifacts use `ARTIFACT`; safety pressure at the existing caution threshold overrides them with `SHIP`. Below the surface it points to ascent, while on the surface it points directly to the ship and disappears inside the service zone. The arrow uses a one-second sine bounce and keeps its runtime label upright.

### Mobility and destination gravity

Both actors experience inertia and vector-valued gravity. The initial content pulls downward, but state stores gravity direction and strength so inverted gravity and local anomalies do not require another redesign.

| Actor | Maximum speed | Acceleration | Braking | Collider |
| --- | ---: | ---: | ---: | ---: |
| Rig | 7.2 cells/s | 14 cells/s² | 20 cells/s² | 0.48 cell |
| Suit | 4.6 cells/s | 28 cells/s² | 24 cells/s² | 0.25 cell |

Base gravity is `6 cells/s²` multiplied by destination scale:

| Destination | Scale | Destination | Scale |
| --- | ---: | --- | ---: |
| Earth Orbit (internal compatibility only) | 0.15 | Moon | 0.35 |
| Mars | 0.60 | Jupiter | 1.15 |
| Saturn | 0.95 | Uranus | 0.80 |
| Neptune | 1.05 | Khepri Prime | 1.20 |
| Rift Belt | 0.25 |  |  |

Tether mass swings under gravity and transfers bounded force to the active actor. The speed and fuel penalty is capped, and available thrust must always exceed gravity plus the bounded tether load so towing cannot create a softlock.

### Keyboard and mouse

- WASD/arrows: thrust.
- Mouse: independent operator aim. The rig remains forward-facing.
- Left click: operator sidearm, immediate first shot and automatic fire while held.
- Right click: operator hand drill while held.
- Space: rig drill using the existing toggle/hold preference.
- `E`: pulse scanner.
- `T`: tether or release an eligible artifact.
- `F`: immediately exit or enter the rig when the placement rules pass.
- `R`: stow all eligible cargo or leave while inside the shuttle ring.
- Esc: abort or back out according to the current state.

### Standard controller

- Left stick: actor thrust.
- Right stick: screen-direction aiming for the rig drill, or independent operator aim in EVA. Center the stick to hold the rig's current heading.
- Right trigger (R2/RT): operator sidearm; rig drill remains on the existing rig mapping.
- Left trigger (L2/LT): operator hand drill.
- West (X/Square): pulse scanner.
- North (Y/Triangle): tether or release an eligible artifact.
- Hold South (A/Cross) for `0.6` seconds: exit or enter the rig. Releasing sooner stows eligible cargo or leaves when valid.
- Left/right bumper: existing ship-service repair actions while available.
- Hold East (B/Circle) for `0.45` seconds: emergency recall.

An `EXIT` or `ENTER` progress ring appears around the rig during the South-button hold; `F` produces an immediate confirmation pulse. D-pad UI-focus mode pauses gameplay while HUD controls are active. Pause, modal entry, focus loss, controller disconnect, or input-source switching clears held aim, fire, drill, thrust, and toggle progress.

Native and web builds use one mining viewport transform for pointer aim. UI-consumed clicks never fire or drill, and the browser context menu is suppressed during mining.

### Fixed operator equipment

- Hand drill: `1.2`-cell reach, `45%` of base rig power, and the existing heat/lockout behavior.
- Scanner: base scan radius without rig upgrade bonuses.
- Tether: existing tether range without rig tow-efficiency bonuses.
- Sidearm: infinite fire, `2.4` damage, `8`-cell range, immediate first shot, `0.18`-second cadence, deterministic first-hit raycast, no piercing, and no critical hits.
- The sidearm damages enemies, spawners, and terrain. It never damages the rig, shuttle, artifact, or Support Drones. Terrain output is capped at `30%` of hand-drill output.

There is no suit upgrade tree, ammunition inventory, propellant inventory, or fall damage.

## Cargo, Passages, Depth, And Failure

The suit has zero ore capacity. Hand-drilled ore and suit-killed enemy rewards become `MiningLooseChunk` world objects collected by the rig or Mining/Resource Support Drones. The artifact is the sole suit-cargo exception: existing normal/heavy weights `4` and `7.2` apply with zero suit free buffer and a `55%` minimum speed clamp.

Generation may create explicit suit-only passages and pockets. These block the rig and artifact but admit the operator and Support Drones. The operator may change depth: the parked rig remains on its layer, while the operator, a validly tethered artifact, and the complete Support Drone swarm travel together.

Exit requires a safe adjacent suit position. Entry requires the same layer and a distance no greater than `1.25` cells. If the rig is destroyed, the nearest safe cell receives an emergency-ejected operator, the rig becomes disabled, and the swarm transfers to the operator before the next combat update. Rig cargo remains with the wreck; previously stowed ship payload remains safe; a tethered artifact stays with the suit.

The controlled actor must reach the parked ship service zone to depart: Rig in Rig mode, operator in EVA. The operator may leave without a functioning Rig. Cargo and drones that have not physically returned are not teleported or credited. Suit integrity is separate from rig health: zero integrity releases the tether, freezes swarm behavior with the failure state, and ends the run. Stowed Common material repairs the suit at the shuttle.

## Mining Resources

- Rig fuel: a separate home-supplied tank retained across visits plus recovered physical cells, consumed by powered thrust and drilling; idle/coasting are free.
- Oxygen: short-run timer, currently 30 seconds before upgrades.
- Drill integrity: durability. Low integrity raises failure pressure; zero integrity disables drilling until the bit is repaired at the ship or the run ends.
- Ship service: while inside the shuttle ring, stowed Common material can fully repair the rig drill, rig health, or suit integrity. Cost scales with missing integrity or health, and spent materials leave the recovered cargo.
- Drill heat: drilling and hard rock raise heat; overheated drilling slows and damages integrity.
- Cargo load: reward now; it is secure once loaded onto the Ship.
- Loose chunks: spatial ore and salvage created by suit drilling or suit kills. They are not carried by the suit and must be collected by the rig or Mining/Resource Support Drones.
- Environmental hazards: revealed physical terrain and encounter threats; protected objectives retain their authored reveal rules.
- Scanner cooldown: limits how often the player can reveal hidden terrain.

Drilling feedback uses the active Rig or EVA footprint. Hard rock reports `HARD ROCK / DRILLABLE`; bedrock reports `BEDROCK / GO AROUND`; ordinary hazards report `HAZARD / TREAT OR AVOID`; protected hazards report `SEALED / HAZARD DRONE`. A contact symbol and limited-rate mechanical, rejection, or warning sound reinforce the label. Sealed cells do not emit successful cutting particles, and hidden protected layers remain concealed.

## Terrain And Rewards

Mining terrain is generated from the destination, surface site profile, and depth:

- Cached layers preserve actual generation and excavation. Underground traversal, collision and delivery use real open seams; unopened seam lips remain physical barriers. The ship stays at its actual parked layer and position.
- Regolith and hard rock define tunneling speed and bounce.
- Baseline hard-rock contact produces a broad, floaty rebound. Shock Mounts and Recoil Braces reduce that impulse so upgraded rigs can hold the drill on target.
- After a hard contact, thrust eases back to full speed instead of snapping forward immediately; bounce relief starts the recovery closer to full control.
- Common ore, rare ore, exotic veins, and artifact caches produce payload.
- Exposed artifacts can be tethered across a 6.8-cell recovery envelope, and the towline keeps a visible trailing length instead of collapsing the relic into an actor. Artifact tether ownership is independent of the Support Drone anchor.
- Hazard pockets use the same Thermal, Cryo, Toxic, and Radiation language as elemental threats. Their effects apply while an actor is drilling or within the pocket's visible contact envelope: Thermal adds heat and actor damage, Cryo slows movement, Toxic damages drill/suit integrity, and Radiation raises extraction hazard.
- Hazard Support Drones fly directly through terrain to convert revealed pockets into safe regolith, with Mk II and Mk III treating larger adjacent clusters and unlocking Toxic and Radiation remediation. They never reveal or target hidden cells. Treatment is active remediation, not immunity: route away from a pocket until the unit finishes.
- Bedrock blocks excavation.
- Deeper or post-solar terrain can add rooms, vaults, hives, miniboss lairs, and boss chambers.

The mining run converts recovered payload back into surface expedition state: temporary materials, cargo, artifacts, hazard delta, extraction-risk pressure, and log entries.

## Crew Classes

Training still levels the active crewmember. Animal class traits affect both menu-side surface odds and direct mining stats.

| Class | Focus | Current mining role |
| --- | --- | --- |
| Capybara Tank | Survival | Extra oxygen and safer endurance windows. |
| Beaver Engineer | Resilience | Better drill integrity and fewer hard failures. |
| Fox Ace | Navigation | Field-action hazard relief and cargo-engine efficiency. |
| Prairie Dog Scout | Digging | Better survey/digging and stronger drilling role. |
| Squirrel Hoarder | Resource Gathering | Better rare-material odds and cargo payoff. |
| Chipmunk Speedster | Exploration | Faster Support Drone movement and traversal. |

## Research, Support Drones, And Expedition Upgrades

Research and contract content supplies these progression hooks. Economy and unlock pacing are TBD S2 in the GDD; the deferred research board is not a campaign phase:

- TBD S2: reconcile research/facility unlocks and effects with orbital survey and the physical expedition; retained project definitions do not establish active field-action mechanics.
- Regolith Drill Rig: stronger mining yield and rare-material odds.
- Cargo Return Rig: lower extraction penalty from heavy payloads.
- Mission Analysis Lab: extra Research Data from recovered field notes in the deferred debug Research board.
- Moon mining contract: 20 safely delivered lunar Common Ore enters a saved ready-to-claim state; `Install Prospector Mk I` consumes the reserve, owns/equips the first Prospector Support Drone, and opens Slot 1.
- Mars bay contract: 8 safely delivered Mars Common Ore enters a saved ready-to-claim state; `Fabricate Slot 2` consumes the reserve and opens an empty specialist slot.
- Drone Support Program: adds the Resource and Survey Support Drones. Io separately commissions the first Hazard Support Drone Mk I into the open Mars slot. Open slots may also fabricate paid duplicate Support Drone frames.
- Current Io volcanic site: ordinary Regolith pays nothing, Thermal lava is the only ore source, treatment always exposes gray Common Ore, and a 60-second authored arena stages one four-segment thermal seal around a protected Artifact. The same `MiningCocoonDefinition` can protect a different objective with any number of authored layers.
- Arkfall emergency kit: Mk I Attack and Defense Support Drones, hostile-contact mitigation, and at least three Drone Bay slots without replacing stronger existing equipment.
- Perimeter Drone Network: Perimeter Coordination makes advanced combat grafts and named synergies eligible in Level Up drafts.

Expedition Level Up offers can rank the Rig build through Rank III. The build survives docking, planet changes, and Rig loss; only main-ship loss moves it into a recoverable wreck. Current drill tracks are:

- High-Torque Motor for additive cutting power.
- Wide Drill Head for main-head width and Side Cutters for lower-power lateral reach.
- Hard-Rock Teeth for hard-rock cutting power and Coolant Mist for heat generation.
- Wideband Pulse and Deep Echo Mapper for scanner reach.
- Shock Mounts and Recoil Braces for hard-rock bounce and durability.
- Ore Hopper and Ore-Scent Array for ore yield.
- Cargo Skids, Ore Hopper, and Expandable Panniers for real Rig capacity.

## Hostile-System Layer

The solar system and Aaru Vale remain enemy-free. Enemies begin only after Arkfall near Khepri Prime and the hostile-system transition:

- Ant, flying, beetle, mammal, and elemental enemy types.
- Elemental affinity effects such as thermal, radiation, toxic, and cryo pressure.
- Hostile tunnel networks, encounter rooms, hives, miniboss lairs, and boss chambers.
- Autonomous base defense plus independent Attack Support Drone targeting and Defense Support Drone interception around the active actor.

The player's operator sidearm is a vulnerable recovery tool rather than the primary combat build. Sustained survival still comes from build planning, movement, return timing, and the Support Drone loadout. Support Drones execute their own role behavior automatically around the active actor: Mining units work revealed cells, Survey units add remote scan origins, Resource units collect loose chunks, Hazard units cross terrain to coordinate on player-revealed dangerous terrain, Attack units hold targets, and Defense units intercept fire before it reaches rig health or suit integrity. Multiple Hazard units split across available targets and assist at exact linear treatment speed when fewer targets are available.

## Implementation Boundaries

- `src/core/MiningSystem.*` owns terrain, actor physics, destination gravity, rig/operator state, loose chunks, drills, sidearm raycasts, tether forces, oxygen/fuel cadence, scanner pulses, mining enemies, finish/abort/failure outcomes, and payload conversion.
- `src/core/MiniDroneCoordination.*` consumes `MiniDroneAnchorFrame` for all home, orbit, task, targeting, shield, scanner, and clearance decisions. These `MiniDrone*` names are legacy internal identifiers; `resolveMiniDroneAnchor` and `transferMiniDroneSwarmAnchor` are the only authoritative binding helpers.
- `src/core/MiningPresentation.h` owns mining HUD copy, controls copy, mode, gravity, suit integrity, drill heat, tether burden, loose-chunk count, Support Drone anchor status, `Suit carry: 0`, metrics, and detail rows.
- `src/core/ResearchSystem.*` owns transfer-to-rig fuel conversion, surface expedition state, one-run-per-loop gating, Expedition XP and temporary run upgrades, Drone Bay state, and deterministic return allocation.
- `src/core/ScenarioSystem.*` owns scenario actions/events, claims, rewards, route requirements, and state-derived objective presentation. Mining receives a generic scenario/site context and reports typed results; it does not branch on campaign, destination, or narrative IDs.
- `src/game/RocketGameApp.*` owns screen transitions and platform-neutral routed aim, fire, drill, scan, tether, operator-toggle, and stow/leave actions.
- `src/render/SceneComposer.*` turns mining snapshots into backend-neutral scene packets consumed by native Vulkan and browser WebGL2, including the parked rig, static operator, independently moving Support Drones, reticle, tracer, tether, thrust, and active-actor-centered shield/scanner effects. Rendering must not decide gameplay outcomes.
- Save version 23 persists the current physical Mining runtime, loose objects, fuel cells, actor tanks, Support Drones and payload ownership, plus drill geometry, draft guarantees, expedition progression, and recoverable wreck builds. Every non-v23 or malformed campaign is rejected and preserved until explicit New Campaign confirmation; no legacy progression migration runs.

When changing mining, keep the fuel/oxygen tradeoff visible and test deployment handoffs, direct mining outcomes, parked-layer service and manual ascent.
