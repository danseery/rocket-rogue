# Ark Campaign And Navigation Spine

USG Notes remain the primary design direction. This implementation adds the first playable campaign spine for the Ark progression without replacing the existing launch, landing, surface, mining, refit, or drone systems.

## Current Playable Mapping

- The mandatory solar route is Earth Orbit -> Moon -> Mars -> Jupiter -> Saturn -> Uranus -> Neptune. Successful arrival unlocks the next body; flyby, orbit, landing, and mining are optional.
- Jupiter, Saturn, Uranus, and Neptune are independent destinations with stable history and map states. The retired `outer_planets` id exists only to migrate older saves to Jupiter.
- Before Neptune succeeds, no Straylight name, contact, silhouette, art, signal, or Ark-return framing may appear anywhere.
- Successful Neptune arrival persists a blocking, full-screen discovery briefing. Its only action, `Approach the Straylight`, acknowledges the beat, discovers the derelict-but-operable Ark, saves, and resumes Neptune Arrival Ops.
- A saved Campaign Introduction after New Game explains the proving-flight objective, hidden failure point, and sequential frontier route.
- The first Ark jump succeeds and teaches that the Ark is a larger version of the press-your-luck shuttle loop.
- The second Ark jump is scripted to hit a gravity well, damaging and stranding the Ark in a hostile system.
- After the disaster, the Navigation screen becomes the mission-selection layer.
- Surface `Shared fuel` becomes `Ark fuel`, preserving the shuttle/mining-drone tradeoff while changing the fiction from Earth-return reserves to Ark sortie reserves.
- Current post-Neptune destinations reuse the existing physical tuning under canonical names:
  - Khepri Prime (tier 7): first hostile-system sortie target.
  - Rift Belt (tier 8): high-risk placeholder for later deep route content.

## Numbered Chapters

Chapter numbers are stable references for saves, tests, UI, and docs. Subtitles are provisional and can be renamed later without changing the chapter number.

- Chapter 1: Proving Ground - Earth Orbit tutorial/proving loop.
- Chapter 2: Lunar Program - Moon arrival, flyby, orbit, and landing phase.
- Chapter 3: Red Frontier - Mars research, surface, and mining loop.
- Chapter 4: Breakthrough - the individual Jupiter, Saturn, Uranus, and Neptune route, ending in the full-screen Straylight discovery beyond Neptune.
- Chapter 5: Straylight / Aaru Vale - operational Straylight as the Ark home, exploring friendly Aaru Vale for fuel, search, discovery, and mining without combat.
- Chapter 6: Arkfall - gravity-well disaster after leaving Aaru Vale; Straylight is damaged and stranded near Khepri Prime. The emergency perimeter system grants Mk I Attack/Defense drones and brings undersized bays to three slots before Act 2 combat mining begins.
- Chapter 7: Last Campfire - stranded hostile-system survival from damaged Straylight near Khepri Prime, with Perimeter Coordination research opening advanced combat-drone tuning and synergies.
- Chapter 8: Void Compass - Rift Belt and deeper-route placeholder content.
- Chapter 9: Ouroboros - future Ark repair loop.
- Chapter 10: Ascent - future repaired-Ark and New Earth route.

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

Mining/combat progression follows `docs/MINING_COMBAT_PROGRESSION.md`: Chapters 1-5 are enemy-free Act 1, Chapters 6-7 are Act 2, and Chapters 8-10 are Act 3. Campaign and debug arenas resolve the same Act/level contract.
