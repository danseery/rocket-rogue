# OREBIT Design Notes

The [OREBIT Game Design Document](Rocket_Rogue_Game_Design_Document.docx) is the definitive design. This document supplies implementation detail and must agree with it. Story and progression decisions marked TBD are collected in GDD Section 8.

## Design pillars

- Physical flight mastery through readable momentum, fuel, heat and hull.
- Continuous terrain from orbital survey and excavation through descent, mining and ascent.
- Physical recovery: cargo belongs to its carrier until delivered to the ship.
- Permanent ship capability and expedition Drone buildcraft with explicit ownership and costs.
- A burrowing-critter space program that becomes the Straylight survival odyssey.

## Connected player journey

Depart the Earth orbital service dock and pilot the physical Flight simulation. Travel, Orbit and local Landing share authoritative flight state. Coast on a qualifying loop for two real seconds to capture orbit. Pulse Survey pauses flight and reads prepared terrain; Orbital Laser Dig cuts a heat-limited shaft inside Zone 1. Survey and Bore independently bound reach. Ore stays in the terrain as loose cargo.

Manual gate entry preserves momentum. The explicit Land command during surveyed inspection instead stops and aligns the ship before gravity resumes. The first supported terrain contact within 30 degrees of upright commits touchdown immediately, including small collision-integration drift; steeper contacts rebound and impact damage still decides survival. A restrained camera jolt settles within 0.36 seconds while the two-second celebration continues. Deploy Surface Team is available at touchdown; deployment takes three seconds and hands over to Mining only after staging and camera transfer complete.

Mine with separate Rig/EVA actors, independent oxygen, a visible powered-use Rig fuel tank, physical cargo and autonomous Support Drones. Ship services and drone delivery follow the actual parked ship layer. Packing settles payload once; ignition returns control to local flight. Manual ascent exits to Orbit at +60 m relative to the surface origin and at least 2 m/s upward speed. Departure burns are fuel-free until that exit.

See [Flight and Surface Flow](POST_ARRIVAL_PHASES.md), [Orbital Preparation](ORBITAL_PREPARATION_PROTOTYPE.md), [Landing and Ascent](UNDERGROUND_LANDING_PROTOTYPE.md), and [Mining](MINING_MINIGAME_PLAN.md) for implementation detail.

## Story and progression

The Moon, Mars, Jupiter/Io, Saturn, Uranus and Neptune lead into Straylight, friendly Aaru Vale, Arkfall, hostile Khepri Prime, Rift Belt, Ouroboros and Ascent. Preserve these identities and explicit campaign claims. The 20-ore lunar industrial contract activates the EVA anomaly; Mars retains its 8-Common-Ore bay-expansion objective. The fixed expedition design makes all planets approachable, with Earth as initial home and six unique batteries physically delivered to activate Straylight. Rank I ship research is available at Earth; ranks II/III require two/four distinct batteries ever banked or installed. Initialized expeditions use physical solar travel; scenario rewards supply recommended leads without restricting movement. Physical battery missions and Ark activation remain to integrate.

GDD Section 8 owns the unresolved design: S1 teaching; S2 economy tuning and physical ship supplies; S3 six-battery objective binding and out-of-order acknowledgements; S4 Ark sorties, repair and endgame; S5 crew development and loss; S6 creative story decisions. Existing content definitions are implementation evidence, not approval of a final progression redesign.

## State and architecture

Portable core systems own physics, deterministic generation, resources, cargo and progression. The shared application orchestrates input, inspection, ceremonies and save handoffs. Render snapshots and SceneComposer display state; they do not generate terrain or decide collisions. Native Vulkan and WebGL2 use the same RmlUi gameplay interfaces and semantic actions.

FlightSystem and LaunchSimulation own physical movement and forecasts. MiningSystem owns prepared/cached terrain and Rig/EVA interaction; RigFuelSystem and PayloadTransfer own powered consumption and cargo movement. Typed scenario definitions, events, requirements and explicit claims keep authored story separate from reusable mechanics. See [Scenario Framework](SCENARIO_FRAMEWORK.md).

## Persistence

Version 21 is the only accepted campaign schema. Incompatible or malformed data is rejected and preserved until explicit New Campaign confirmation. Preferences are independent. Validated stable states support checkpoint recovery.

Persist local flight pose, fixed surface origin, parked ship layer/position, departure/support state, exact cached terrain, physical objects, actor tanks, drone transit and progression. Prepared arrivals and orbital work are session-only until the completed deployment or undeployed-departure handoff. Touchdown commits in memory. Packing settles and saves at its completed handoff. SaveSchema and SaveData define field names and validation.

## Expedition state ownership

`ExpeditionProgressionState` owns XP, pending drafts, Rig/drone ranks, graft assignments/runtime and synergies outside `PlanetaryExpeditionState`. Site replacement cannot reset these fields. The XP thresholds, reward amounts and effects are unchanged. `SystemContent`, `ExpeditionSystem` and `ExpeditionPersistence` provide core contracts and v21 registry serialization. The application connects the spatial map, course/cruise actions, Earth docking, carried salvage, Rank I shipyard, wreck recovery, and saved return-or-continue decisions. Physical battery missions and Ark/post-solar campaign integration remain TBD. See [Persistent Expeditions](PERSISTENT_EXPEDITIONS.md) for operations, compatibility boundaries, and validation scope.
