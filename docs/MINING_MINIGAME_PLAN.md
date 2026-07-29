# Mining Mini-Game

Deterministic scenario-authored and optional protected-objective gates are specified in [MINING_LOCK_AND_KEY_SITES.md](MINING_LOCK_AND_KEY_SITES.md). Their reusable authoring contract is [SCENARIO_FRAMEWORK.md](SCENARIO_FRAMEWORK.md). They reuse the physical artifact, scanner, hazard, towing, terrain, EVA, and autonomous-combat systems described here.

See `docs/AGENT_DESIGN_CONTEXT.md` before extending this system. The current mining direction should follow `docs/reference/USG_NOTES.md` first: chunky/mobile, Straylight-inspired, fog-of-war, destructible terrain, excavation/logistics/endurance roles, and enemy combat only after Arkfall near Khepri Prime.

See `docs/MINI_DRONE_SYSTEM.md` for the persistent Support Drone Bay layer that modifies mining, scanner, logistics, oxygen, extraction, and later autonomous-defense behavior. The `MiniDrone*` names in C++ are legacy internal identifiers; UI and design copy use Support Drone.

This note describes the current playable mining phase layered onto the post-arrival Surface Ops flow. Mining does not replace launch, arrival, research, or surface-expedition architecture; it resolves back into the same `SurfaceActionOutcome` path so cargo, materials, hazards, artifacts, field upgrades, and log entries stay consistent.

## Mechanical Touchstone: Solar Jetman

*Solar Jetman* is the mechanical touchstone for destination-sensitive gravity, inertia, distinct vehicle/pilot roles, towing burden, vulnerable recovery, and physically returning discoveries home. The reference establishes a gravity-sensitive Jetpod, limited fuel, infinite standard fire, reduced thrust while towing, and a vulnerable pilot after pod destruction: [original NES manual](https://www.gamingalexandria.com/highquality/NES/Solar%20Jetman/Solar%20Jetman%20-%20Manual%20%28Searchable%29.pdf) and [official Rare Replay manual](https://dlassets-ssl.xboxlive.com/public/content/367297b7-c6a3-4496-83ad-cb70c52ce8cd/GameManual/2e5e2560-e901-414b-87fa-081a07f24c6c/en-SA/index.html#SolarJetman).

OREBIT deliberately modernizes that foundation with voluntary EVA, twin-stick aiming, hand-drilled terrain, Support Drones that transfer between controlled actors, suit-only passages, explicit tether control, and an artifact-only exception to the suit's zero-cargo rule. This is an internal mechanical reference, not story or presentation canon.

## Design Goal

Mining is the landed version of the rocket launch loop:

- The player chooses whether to spend shared fuel, how much cargo to load, and when to return and leave or abort.
- Better crew, tools, Support Drones, and surface upgrades make risk more readable and controllable, but never remove it.
- Early solar-system mining is environmental: oxygen pressure, drill heat, hard-rock bounce, hazard pockets, low fuel, and extraction risk.
- Hostile terrain and enemies stay out of the solar system and Aaru Vale. Enemy pressure begins only after Arkfall near Khepri Prime, when the agency is stranded in a hostile system.
- The rig is durable, fast, and cargo-capable; the operator is slower but more agile, accelerates faster, and can enter suit-only passages.
- Recoveries stay physical. Gravity, tether mass, loose chunks, a disabled rig, and the need to return discoveries to the shuttle create the pressure.

## Entry Point

Current flow:

1. Reach a destination.
2. Choose Flyby, Orbit, or Landing.
3. Landing opens Research, then Surface Ops.
4. Surface Ops shows Survey, Mine deposit, Push Deeper, Return, and Drone Ops when unlocked. Mine deposit remains unavailable until Survey Site or Push Deeper prepares the site.
5. Push Deeper guarantees a bankable layer +1. Collapse risk begins on the attempt for layer +2; a scanned artifact becomes guaranteed when its mapped layer succeeds.
6. Pressing the prepared `Mine deposit` action spends 1 shared fuel and opens the direct-control Mining Rig screen at the selected start depth. The ship remains fixed at surface depth `0`.

Mining is one run per surface loop. Once it has been used, the yellow availability copy should say `Mining Rig offline` for the mining card and `Extract payload` for the field-action cards, with disabled buttons labeled `Unavailable`. `Survey site` and `Push Deeper` are both disabled after mining because the dig commits the field team to the current extraction window.

## Shared Fuel And Oxygen

Mining exists to make surface greed compete with route-home safety:

- Surface expeditions start with shared fuel capacity from `tuning::research::sharedFuelCapacity`.
- Mining spends 1 fuel on deployment.
- While oxygen remains, mining advances a normalized fuel-consumption cycle and spends another fuel when that cycle completes. Load and future efficiency modifiers change the authoritative cycle rate; the HUD shows percentage remaining rather than seconds.
- Returning rig cargo, Support Drone haul, loose chunks, or a physically delivered artifact to the shuttle banks that payload and replenishes oxygen to the current upgraded capacity. Entering the shuttle zone empty does not refill oxygen.
- The baseline oxygen tank is `tuning::mining::oxygenSeconds`, currently 30 seconds.
- Oxygen can improve through crew class, Resource Support Drone coverage, and surface upgrades such as Emergency Winch.
- If fuel runs dry mid-dig, the Mining Rig is recalled so the shuttle still has a route home.

Before Ark discovery, UI should call this `Shared fuel`. After Ark discovery, the same mechanic is framed as `Ark fuel`.

## Rig And EVA Core Loop

`MiningOperatorMode { Rig, Jetpack }` selects the controlled actor, and runs begin in `Rig`. The rig and operator are separate saved world actors; changing control does not transform or replace either entity.

1. Pilot the rig through chunked terrain, drill straight ahead, scan hidden seams, tow artifacts, and carry ore.
2. Exit into the jetpack suit from a safe adjacent cell to enter narrow passages, hand-drill, scan, tether an artifact, or defend yourself with the sidearm.
3. Allow autonomous Support Drones to follow, orbit, defend, mine, survey, treat hazards, and collect loose chunks around whichever actor is controlled.
4. Manage oxygen, shared fuel, gravity, inertia, drill heat, integrity, cargo, loose chunks, tether burden, and extraction risk.
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
| Earth Orbit | 0.15 | Moon | 0.35 |
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
- `R`: use the existing bank/leave action while inside the shuttle ring.
- Esc: abort or back out according to the current state.

### Standard controller

- Left stick: actor thrust.
- Right stick: independent operator aim; it does not steer the rig drill.
- Right trigger (R2/RT): operator sidearm; rig drill remains on the existing rig mapping.
- Left trigger (L2/LT): operator hand drill.
- West (X/Square): pulse scanner.
- North (Y/Triangle): tether or release an eligible artifact.
- Hold South (A/Cross) for `0.6` seconds: exit or enter the rig. Releasing sooner performs the existing bank/leave action when valid.
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

Exit requires a safe adjacent suit position. Entry requires the same layer and a distance no greater than `1.25` cells. If the rig is destroyed, the nearest safe cell receives an emergency-ejected operator, the rig becomes disabled, and the swarm transfers to the operator before the next combat update. Rig cargo remains with the wreck; previously banked payload remains safe; a tethered artifact stays with the suit.

Normal extraction requires a functioning rig and operator in the return zone. After emergency ejection, reaching the shuttle ends the deployment safely and may bank a physically delivered artifact. Suit integrity is separate from rig health: zero integrity releases the tether, freezes swarm behavior with the failure state, and ends the run. Banked Common material repairs the suit at the shuttle.

## Mining Resources

- Shared fuel: the shuttle/Mining Rig reserve. This is the central tradeoff and must stay visible in Surface Ops and Mining.
- Oxygen: short-run timer, currently 30 seconds before upgrades.
- Drill integrity: durability. Low integrity raises failure pressure; zero integrity disables drilling until the bit is repaired at the ship or the run ends.
- Ship service: while inside the shuttle ring, banked Common material can fully repair the rig drill, rig health, or suit integrity. Cost scales with missing integrity or health, and spent materials leave the recovered cargo.
- Drill heat: drilling and hard rock raise heat; overheated drilling slows and damages integrity.
- Cargo load: reward now, extraction risk later.
- Loose chunks: spatial ore and salvage created by suit drilling or suit kills. They are not carried by the suit and must be collected by the rig or Mining/Resource Support Drones.
- Hazard delta: mining-specific danger that feeds back into surface hazard and extraction risk.
- Scanner cooldown: limits how often the player can reveal hidden terrain.

## Terrain And Rewards

Mining terrain is generated from the destination, surface site profile, and depth:

- A fresh deployment depth is normal mining terrain. Once the player descends, the layer left behind receives a permanent two-cell-wide central return shaft from its ascent seam to its descent seam. Intermediate layers generated while climbing receive the same shaft, so every previously traversed route back to the surface stays open across cached layers and reloads without changing normal rig movement.
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
| Fox Ace | Navigation | Cleaner extraction risk and abort safety. |
| Prairie Dog Scout | Digging | Better survey/digging and stronger drilling role. |
| Squirrel Hoarder | Resource Gathering | Better rare-material odds and cargo payoff. |
| Chipmunk Speedster | Exploration | Faster Support Drone movement and traversal. |

## Research, Support Drones, And Field Upgrades

Research improves mining through specific tools:

- Field Probe Network: more action-kit margin and better survey support.
- Regolith Drill Rig: stronger mining yield and rare-material odds.
- Cargo Return Rig: lower extraction penalty from heavy payloads.
- Mission Analysis Lab: extra blueprint progress from recovered field notes.
- Moon mining contract: 30 safely delivered lunar Common Ore enters a saved ready-to-claim state; `Install Prospector Mk I` consumes the reserve, owns/equips the first Prospector Support Drone, and opens Slot 1.
- Mars bay contract: 40 safely delivered Mars Common Ore enters a saved ready-to-claim state; `Fabricate Slot 2` consumes the reserve and opens an empty specialist slot.
- Drone Support Program: adds the Resource and Survey Support Drones. Io separately commissions the first Hazard Support Drone Mk I into the open Mars slot. Open slots may also fabricate paid duplicate Support Drone frames.
- Current Io volcanic site: ordinary Regolith pays nothing, Thermal lava is the only ore source, treatment always exposes gray Common Ore, and a 60-second authored arena stages outer `0/4` and inner `0/4` lava layers around a protected Artifact. The same `MiningCocoonDefinition` can protect a different objective with any number of authored layers.
- Arkfall emergency kit: Mk I Attack and Defense Support Drones, hostile-contact mitigation, and at least three Drone Bay slots without replacing stronger existing equipment.
- Perimeter Drone Network: Perimeter Coordination for Mk II/Mk III combat tuning and advanced combat synergies.

Surface field upgrades are temporary ship-loop upgrades selected during surface play. Current examples include:

- Thermal Drill Jackets and Coolant Mist for heat control.
- Wideband Pulse and Deep Echo Mapper for scanner reach.
- Shock Mounts and Recoil Braces for hard-rock bounce and durability.
- Ore Hopper and Ore-Scent Array for ore yield.
- Cargo Skids and Emergency Winch for extraction/oxygen safety.

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
- `src/core/ResearchSystem.*` owns surface expedition state, shared fuel capacity, one-run-per-loop gating, field upgrades, Drone Bay state, and extraction risk.
- `src/core/ScenarioSystem.*` owns scenario actions/events, claims, rewards, route requirements, and state-derived objective presentation. Mining receives a generic scenario/site context and reports typed results; it does not branch on campaign, destination, or narrative IDs.
- `src/game/RocketGameApp.*` owns screen transitions and platform-neutral routed aim, fire, drill, scan, tether, operator-toggle, and bank/leave actions.
- `src/render/SceneComposer.*` turns mining snapshots into backend-neutral scene packets consumed by native Vulkan and browser WebGL2, including the parked rig, static operator, independently moving Support Drones, reticle, tracer, tether, thrust, and active-actor-centered shield/scanner effects. Rendering must not decide gameplay outcomes.
- Save version 9 retains version 8's rig/operator, loose-chunk, disabled-rig, purchased-frame, and Support Drone anchor/formation state while adding scenario instance/site context plus generic cocoon definition, protected-objective, layer, reveal, and cell-tag state. Older saves restore seated in the rig, migrate Support Drones to `ControlledActor`, de-duplicate repeated pre-v8 loadout IDs once, and synthesize v9 scenario/cocoon state without replaying rewards.

When changing mining, keep the fuel/oxygen tradeoff visible and test both Surface Ops availability and direct mining outcomes.
