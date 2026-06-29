# Ark Campaign And Navigation Spine

USG Notes remain the primary design direction. This implementation adds the first playable campaign spine for the Ark progression without replacing the existing launch, landing, surface, mining, refit, or drone systems.

## Current Playable Mapping

- Earth, Moon, Mars, and Outer Planets are still the solar-system tutorial.
- Reaching Outer Planets discovers the Ark beyond Neptune. It is derelict and under-equipped, but operable.
- The first Ark jump succeeds and teaches that the Ark is a larger version of the press-your-luck shuttle loop.
- The second Ark jump is scripted to hit a gravity well, damaging and stranding the Ark in a hostile system.
- After the disaster, the Navigation screen becomes the mission-selection layer.
- Surface `Shared fuel` becomes `Ark fuel`, preserving the shuttle/mining-drone tradeoff while changing the fiction from Earth-return reserves to Ark sortie reserves.
- Current hostile-system destinations reuse the existing deep-space destinations:
  - Nearby Star: first hostile-system sortie target.
  - Nearby Galaxy: high-risk placeholder for later deep route content.

## Future Expansion

Navigation should eventually become a proper local system map with planets, moons, asteroid fields, anomalies, fuel costs, danger, terrain durability, artifact leads, and discovered enemy presence.

Post-disaster loop:

1. Open Navigation from the stranded Ark.
2. Choose a local planet, moon, asteroid, or anomaly.
3. Prep shuttle launch from the Ark.
4. Fly the press-your-luck transfer.
5. Choose flyby, orbit, or landing.
6. Mine fuel/materials and recover alien artifacts while deciding how much Ark fuel to spend on the drone versus keeping a route home.
7. Extract payload and return to the Ark.
8. Spend resources on Ark repair, fuel systems, drone tech, shuttle upgrades, and artifact research.

Enemies and combat drones belong after the gravity-well disaster. Solar-system tutorial destinations should stay enemy-free.
