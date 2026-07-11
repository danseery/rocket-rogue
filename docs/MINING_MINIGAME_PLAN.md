# Mining Mini-Game

See `docs/AGENT_DESIGN_CONTEXT.md` before extending this system. The current mining direction should follow `docs/reference/USG_NOTES.md` first: chunky/mobile, Straylight-inspired, fog-of-war, destructible terrain, excavation/logistics/endurance roles, and passive defense only after the solar system.

See `docs/MINI_DRONE_SYSTEM.md` for the persistent Drone Bay layer that modifies mining, scanner, logistics, oxygen, extraction, and later passive-defense behavior.

This note describes the current playable mining phase layered onto the post-arrival Surface Ops flow. Mining does not replace launch, arrival, research, or surface-expedition architecture; it resolves back into the same `SurfaceActionOutcome` path so cargo, materials, hazards, artifacts, field upgrades, and log entries stay consistent.

## Design Goal

Mining is the landed version of the rocket launch loop:

- The player chooses whether to spend shared fuel, how much cargo to load, and when to stow or abort.
- Better crew, tools, drones, and surface upgrades make risk more readable and controllable, but never remove it.
- Early solar-system mining is environmental: oxygen pressure, drill heat, hard-rock bounce, hazard pockets, low fuel, and extraction risk.
- Hostile terrain and enemies stay out of Moon and Mars mining. Enemy pressure begins after the solar-system tutorial when the agency is stranded in a hostile system.

## Entry Point

Current flow:

1. Reach a destination.
2. Choose Flyby, Orbit, or Landing.
3. Landing opens Research, then Surface Ops.
4. Surface Ops shows Survey, Mine deposit, Push Deeper, Return, and Drone Ops when unlocked.
5. Pressing `Mine deposit` spends 1 shared fuel and opens the direct-control Mining screen.

Mining is one run per surface loop. Once it has been used, the yellow availability copy should say `Mining drone offline` for the mining card and `Extract payload` for the field-action cards, with disabled buttons labeled `Unavailable`. `Survey site` and `Push Deeper` are both disabled after mining because the dig commits the field team to the current extraction window.

## Shared Fuel And Oxygen

Mining exists to make surface greed compete with route-home safety:

- Surface expeditions start with shared fuel capacity from `tuning::research::sharedFuelCapacity`.
- Mining spends 1 fuel on deployment.
- While oxygen remains, mining advances a normalized fuel-consumption cycle and spends another fuel when that cycle completes. Load and future efficiency modifiers change the authoritative cycle rate; the HUD shows percentage remaining rather than seconds.
- Returning carried cargo, materials, or artifacts to the ship banks that payload and replenishes oxygen to the rig's current upgraded capacity. Entering the ship zone empty does not refill oxygen.
- The baseline oxygen tank is `tuning::mining::oxygenSeconds`, currently 30 seconds.
- Oxygen can improve through crew class, Resource Drone support, and surface upgrades such as Emergency Winch.
- If fuel runs dry mid-dig, the mining drone is recalled so the shuttle still has a route home.

Before Ark discovery, UI should call this `Shared fuel`. After Ark discovery, the same mechanic is framed as `Ark fuel`.

## Current Core Loop

The first playable version is direct and short:

1. Move the drone through chunked terrain.
2. Turn the rig and drill straight ahead into regolith, hard rock, ore, exotic veins, or artifact caches.
3. Pulse the scanner to reveal fog-of-war and hidden seams.
4. Manage oxygen, fuel cadence, drill heat, drill integrity, cargo, and extraction risk.
5. Stow payload to bank the run, or abort/lose the run when oxygen, fuel, or drill integrity fails.

Controls:

- WASD/arrows: turn and thrust. The drill remains fixed forward.
- Space or mouse hold: drill.
- `E`: pulse scanner.
- `R`: stow payload.
- Esc: abort.

## Mining Resources

- Shared fuel: the shuttle/drone reserve. This is the central tradeoff and must stay visible in Surface Ops and Mining.
- Oxygen: short-run timer, currently 30 seconds before upgrades.
- Drill integrity: durability. Low integrity raises failure pressure; zero integrity disables drilling until the bit is repaired at the ship or the run ends.
- Ship service: while inside the ship ring, banked common materials can fully repair the drill bit or mining drone. Cost scales with missing integrity or health, and spent materials leave the recovered cargo.
- Drill heat: drilling and hard rock raise heat; overheated drilling slows and damages integrity.
- Cargo load: reward now, extraction risk later.
- Hazard delta: mining-specific danger that feeds back into surface hazard and extraction risk.
- Scanner cooldown: limits how often the player can reveal hidden terrain.

## Terrain And Rewards

Mining terrain is generated from the destination, surface site profile, and depth:

- Regolith and hard rock define tunneling speed and bounce.
- Baseline hard-rock contact produces a broad, floaty rebound. Shock Mounts, Recoil Braces, and Stabilizer drones reduce that impulse so upgraded rigs can hold the drill on target.
- After a hard contact, thrust eases back to full speed instead of snapping forward immediately; bounce relief starts the recovery closer to full control.
- Common ore, rare ore, exotic veins, and artifact caches produce payload.
- Exposed artifacts can be tethered across a 6.8-cell recovery envelope, and the towline keeps a visible trailing length instead of collapsing the relic into the drone.
- Hazard pockets add integrity damage and extraction risk.
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
| Chipmunk Speedster | Exploration | Faster drone movement and traversal. |

## Research, Drones, And Field Upgrades

Research improves mining through specific tools:

- Field Probe Network: more action-kit margin and better survey support.
- Regolith Drill Rig: stronger mining yield and rare-material odds.
- Cargo Return Rig: lower extraction penalty from heavy payloads.
- Mission Analysis Lab: extra blueprint progress from recovered field notes.
- Drone Bay Program: persistent helper drones and Drone Ops loadout.
- Perimeter Drone Network: post-solar attack/defense drones and hostile-contact mitigation.

Surface field upgrades are temporary ship-loop upgrades selected during surface play. Current examples include:

- Thermal Drill Jackets and Coolant Mist for heat control.
- Wideband Pulse and Deep Echo Mapper for scanner reach.
- Shock Mounts and Recoil Braces for hard-rock bounce and durability.
- Ore Hopper and Ore-Scent Array for ore yield.
- Cargo Skids and Emergency Winch for extraction/oxygen safety.

## Hostile-System Layer

Solar-system mining should remain enemy-free. After the Ark gravity-well disaster and hostile-system transition, mining can include passive-defense combat pressure:

- Ant, flying, beetle, mammal, and elemental enemy types.
- Elemental affinity effects such as thermal, radiation, toxic, and cryo pressure.
- Hostile tunnel networks, encounter rooms, hives, miniboss lairs, and boss chambers.
- Passive base defense plus independent Attack drone targeting and Defense drone interception.

The player should survive through build planning, movement, stow timing, and drone loadout, not through a twitch weapon mode. Mini-drones execute their own role behavior automatically: Mining drones work revealed cells, Survey drones add remote scan origins, Resource drones stay close, Stabilizers dock to the rig, Attack drones hold targets, and Defense drones intercept fire.

## Implementation Boundaries

- `src/core/MiningSystem.*` owns terrain, drill stats, oxygen/fuel cadence, scanner pulses, mining enemies, stow/abort/failure outcomes, and payload conversion.
- `src/core/MiningPresentation.h` owns mining HUD copy, controls copy, metrics, and detail rows.
- `src/core/ResearchSystem.*` owns surface expedition state, shared fuel capacity, one-run-per-loop gating, field upgrades, Drone Bay state, and extraction risk.
- `src/game/RocketGameApp.*` owns screen transitions and browser input callbacks.
- `src/render/WebGLRenderer.*` draws mining snapshots but must not decide gameplay outcomes.

When changing mining, keep the fuel/oxygen tradeoff visible and test both Surface Ops availability and direct mining outcomes.
