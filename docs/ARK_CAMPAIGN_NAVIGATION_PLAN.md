# Ark Campaign And Navigation Spine

USG Notes remain the primary design direction. This implementation adds the first playable campaign spine for the Ark progression without replacing the existing launch, landing, surface, mining, refit, or Support Drone systems.

## Current Playable Mapping

- The visible mandatory solar route is Moon -> Mars -> Jupiter/Io -> Saturn -> Uranus -> Neptune. `earth_orbit` remains an internal version-9 save origin only. The stable travel id remains `jupiter`, while its landed surface is presented as Io. `destinationIndex` is campaign frontier only; every actual transfer carries a saved route leg with source, target, and intent, so HUDs and scenes never infer Earth from a destination tier.
- Early route advancement is explicit: the claimed 30-Common lunar Prospector contract opens Mars readiness; the claimed 40-Common Mars bay expansion opens Jupiter readiness; safe Io artifact recovery enables the Saturn slingshot; and a claimed Perfect Jupiter Flyby permanently opens Saturn.
- Jupiter, Saturn, Uranus, and Neptune remain independent destinations with stable history and map states. The retired `outer_planets` id stays reserved and must not be reused; current version-13 saves use the individual destination IDs.
- A normal Good or Perfect Jupiter Pass Through keeps its recon rewards but cannot satisfy Io's authored Saturn gate. It presents `RECOVERY ROUTE REQUIRED`, then flies the playable `JUPITER -> MARS` recovery leg at the Mars-Jupiter leg's normal fuel demand and hazards. A successful recovery stages an explicit `REAPPROACH JUPITER`; aborting or failing never grants route clearance, credits, Research Data, or surface rewards.
- Locking the Saturn course begins the one-way outer expedition. From Saturn onward, recovery UI says `Recover to Expedition` instead of promising an Earth return; after the Straylight discovery, it says `Return to Ark`.
- Before Neptune succeeds, no Straylight name, contact, silhouette, art, signal, or Ark-return framing may appear anywhere.
- Successful Neptune arrival persists a blocking, full-screen discovery briefing. Its only action, `Approach the Straylight`, acknowledges the beat, discovers the derelict-but-operable Ark, saves, and resumes Neptune Arrival Ops.
- A saved Campaign Introduction after New Game explains the four launch lessons: the Moon route requires 15 units of transfer capacity; the first sortie collects fuel data and turns around; controls, temperature, and hull are then revealed in sequence.
- The first Ark jump succeeds and teaches that the Ark is a larger version of the press-your-luck shuttle loop.
- The second Ark jump is scripted to hit a gravity well, damaging and stranding the Ark in a hostile system.
- After the disaster, the Navigation screen becomes the mission-selection layer.
- Ark fuel loads up to 3 units into the expedition rig pack before landing. Transfer fuel preserved at touchdown is added to that `Rig fuel` pool, while the return stage remains reserved.
- Current post-Neptune destinations reuse the existing physical tuning under canonical names:
  - Khepri Prime (tier 7): first hostile-system sortie target.
  - Rift Belt (tier 8): high-risk placeholder for later deep route content.

## Numbered Chapters

Chapter numbers are stable references for saves, tests, UI, and docs. Subtitles are provisional and can be renamed later without changing the chapter number.

- Chapter 1: Proving Ground - Moon-bound Fuel Survey, Flight Controls Calibration, and the first true Moon transfer. Earth Orbit is never shown.
- Chapter 2: Lunar Program - Moon arrival, landing, the 30-Common Prospector contract, and the first Support Drone/Drone Bay slot.
- Chapter 3: Red Frontier - Mars research, repeated 30-second pressure mining with heat, integrity, repair, cargo, and return decisions, and explicit fabrication of the empty second Drone Bay slot from 40 local Common Ore.
- Chapter 4: Breakthrough - Io Hazard Support Drone commissioning, a two-layer lava-sealed artifact, the Perfect Jupiter-to-Saturn slingshot, and the individual Saturn/Uranus/Neptune route ending in the full-screen Straylight discovery beyond Neptune.
- Chapter 5: Straylight / Aaru Vale - operational Straylight as the Ark home, exploring friendly Aaru Vale for fuel, search, discovery, and mining without combat.
- Chapter 6: Arkfall - gravity-well disaster after leaving Aaru Vale; Straylight is damaged and stranded near Khepri Prime. The emergency perimeter system grants Mk I Attack/Defense Support Drones and brings undersized bays to three slots before Act 2 combat mining begins.
- Chapter 7: Last Campfire - stranded hostile-system survival from damaged Straylight near Khepri Prime, with Perimeter Coordination research opening advanced Support Drone tuning and synergies.
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
6. Load up to 3 Ark-fuel units into the expedition pack, add any transfer fuel preserved at touchdown, then mine materials and recover alien artifacts without exposing the reserved return stage.
7. Extract payload and return to the Ark.
8. Spend resources on Ark repair, fuel systems, Support Drone tech, shuttle upgrades, and artifact research.

Enemies and Attack/Defense Support Drones belong after the gravity-well disaster. Solar-system tutorial destinations should stay enemy-free.

Mining/combat progression follows `docs/MINING_COMBAT_PROGRESSION.md`: Chapters 1-5 are enemy-free Act 1, Chapters 6-7 are Act 2, and Chapters 8-10 are Act 3. Campaign and debug arenas resolve the same Act/level contract.

## Reusable Route And Flyby Gates

Route blocking is authored, not inferred from tier or destination copy. Add the required unlock key to `Destination::routeRequirementKeys`, then grant that exact key from a claim-required scenario step. `scenarioRouteRequirementStatus()` resolves the blocking key back to its scenario instance and step, so Navigation, buttons, objectives, and modal wording share one result. Add a `RouteLinkDefinition` for every physical solar leg: source, target, calibrated cruise-fuel profile, recovery availability, and one-way policy. `RouteTransitState` uses that definition for outbound, recovery, and reapproach legs; the save records both pending route transit and the incoming arrival leg. Future authored blocked routes choose recovery in content instead of adding planet checks. Use `Destination::oneWayExpedition` for outward-only recovery framing instead of branching on Saturn by name.

A required Flyby is a normal scenario activity: set the step action to `BeginActivity`, completion event to `FlybyFinished`, and `requiredGrade` to the gate threshold. The launched run retains its scenario-instance and step IDs; completion records a typed event against that instance. Failure may expose one saved explanation, while success becomes `READY TO CLAIM`; only the explicit claim grants the route key. Generic reconnaissance Flybys remain independent because they carry no scenario context.

See [SCENARIO_FRAMEWORK.md](SCENARIO_FRAMEWORK.md) for the complete definition, factory, reward, migration, and no-story-branch authoring contract.
