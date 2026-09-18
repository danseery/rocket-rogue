# OREBIT Design Notes

The [OREBIT Game Design Document](Rocket_Rogue_Game_Design_Document.docx) is the definitive design. This document supplies implementation detail and must agree with it. Story and progression decisions marked TBD are collected in GDD Section 8.

## Design pillars

- Physical flight mastery through readable momentum, fuel, heat and hull.
- Continuous terrain from orbital survey and excavation through descent, mining and ascent.
- Physical recovery: cargo belongs to its carrier until delivered to the ship.
- Permanent ship capability and expedition Drone buildcraft with explicit ownership and costs.
- A burrowing-critter space program that becomes the Straylight survival odyssey.

## Connected player journey

Launch from the Earth berth or depart its orbital service dock and pilot the physical Flight simulation. Travel, Orbit and local Landing share authoritative flight state. Coast on a qualifying loop for two real seconds to capture orbit. Pulse Survey pauses flight and reads prepared terrain; DRILL cuts a rank-limited shaft in the selected one of six persistent sectors, without laser heat. Survey and Bore independently bound reach. Ore stays in the terrain as loose cargo.

Manual gate entry preserves momentum. The explicit Land command during surveyed inspection instead stops and aligns the ship before gravity resumes. The first supported terrain contact within 30 degrees of upright commits touchdown immediately, including small collision-integration drift; steeper contacts rebound and impact damage still decides survival. Camera shake and rumble duration scale with impact speed while the two-second celebration continues. Deploy Surface Team is available at touchdown; deployment takes three seconds and hands over to Mining only after staging and camera transfer complete.

Mine with separate Rig/EVA actors, independent oxygen, a visible powered-use Rig fuel tank, physical cargo and autonomous Support Drones. Ship services and drone delivery follow the actual parked ship layer. Packing settles payload once; ignition returns control to local flight. Manual ascent exits to Orbit at +46 m relative to the surface origin and at least 2 m/s upward speed. Departure assistance starts upright, regulates climb toward 8 m/s, and ends at the orbital handoff or manual throttle takeover. Local ascent does not cause heat damage.

See [Flight and Surface Flow](POST_ARRIVAL_PHASES.md), [Orbital Preparation](ORBITAL_PREPARATION_PROTOTYPE.md), [Landing and Ascent](UNDERGROUND_LANDING_PROTOTYPE.md), and [Mining](MINING_MINIGAME_PLAN.md) for implementation detail.

## Story and progression

The Moon, Mars, Jupiter/Io, Saturn, Uranus and Neptune lead into Straylight, friendly Aaru Vale, Arkfall, hostile Khepri Prime, Rift Belt, Ouroboros and Ascent. Preserve these identities and explicit campaign claims. The 20-ore lunar industrial contract activates the EVA anomaly; one Incoming Message introduces the Prospector and explicitly grants Prospector MK I and Slot 1 through its Claim mission reward action, without a second reward popup. Its base Common Ore mining cycle is five seconds; Mars retains its 8-Common-Ore bay-expansion objective. The fixed expedition design makes all planets approachable, with Earth as initial home and six unique batteries physically delivered to activate Straylight. Rank I ship research is available at Earth; ranks II/III require two/four distinct batteries ever banked or installed. Initialized expeditions use physical solar travel; scenario rewards supply recommended leads without restricting movement. Physical battery missions and Ark activation remain to integrate.

GDD Section 8 owns the unresolved design: S1 teaching; S2 economy tuning and physical ship supplies; S3 six-battery objective binding and out-of-order acknowledgements; S4 Ark sorties, repair and endgame; S5 crew development and loss; S6 creative story decisions. Existing content definitions are implementation evidence, not approval of a final progression redesign.

## State and architecture

Portable core systems own physics, deterministic generation, resources, cargo and progression. The shared application orchestrates input, inspection, ceremonies and save handoffs. Render snapshots and SceneComposer display state; they do not generate terrain or decide collisions. Native Vulkan and WebGL2 use the same RmlUi gameplay interfaces and semantic actions.

FlightSystem and LaunchSimulation own physical movement and forecasts. MiningSystem owns prepared/cached terrain and Rig/EVA interaction; RigFuelSystem and PayloadTransfer own powered consumption and cargo movement. Typed scenario definitions, events, requirements and explicit claims keep authored story separate from reusable mechanics. See [Scenario Framework](SCENARIO_FRAMEWORK.md).

## Persistence

Version 23 is the only accepted campaign schema. Older and malformed campaign data is rejected and routed to New Campaign. Preferences are independent. Validated stable states support checkpoint recovery.

Persist local flight pose, fixed surface origin, parked ship layer/position, departure/support state, exact cached terrain, physical objects, actor tanks, drone transit and progression. Each system/body/sector retains survey, excavation, cached layers and objective state, including sites scanned or drilled without landing. Revisit generation never restores consumed resources or artifacts. Touchdown commits in memory. Packing settles and saves at its completed handoff. SaveSchema and SaveData define field names and validation.

## Expedition state ownership

`ExpeditionProgressionState` owns XP, pending drafts, Rig/drone ranks, graft assignments/runtime and synergies outside `PlanetaryExpeditionState`. Site replacement, Rig loss, and Earth service cannot reset these fields. Main-ship loss stores the build in its wreck; recovery merges it once. `SolarMissionDefinition` is the canonical Moon-to-Triton route, including optional Mercury and Venus recoveries. `SystemContent`, `ExpeditionSystem` and `ExpeditionPersistence` provide continuous flight, physical battery ownership, Earth service, wreck recovery, explicit waypoints, and strict v23 serialization. Triton reveals the reachable Straylight derelict; installation and activation remain later work. See [Persistent Expeditions](PERSISTENT_EXPEDITIONS.md).

## Current recovery and interaction rules

The initial ship hold carries 60 ore; excess stays with its carrier or in the site. Loose ore is pulled toward the Rig within its upgradeable collection radius. Rig collision uses a circular body plus solid triangular drill; the scanner ring never blocks movement. The first Moon anomaly tunnel is offset six tiles from its old anchor. Artifact guidance hides while the artifact itself is tethered and returns when released.

Earth's dock is a separate marker. Enter its docking range at no more than 0.20 relative speed, then press Dock. Approaching Earth is not an instruction to hit or land on its surface. Distance-based camera and simulation timing ease across body-frame transitions.

Main-ship destruction or abandonment returns directly to the Earth dock with a fresh ship and a recoverable wreck. The opening flight instead presents one cause-aware **Retry launch** action that starts the next attempt. EVA death and Emergency Recall return the player to the surviving parked ship at the current site: stowed cargo and the expedition build remain safe, while unreturned payload remains loose where it was lost. These paths do not visit Navigation, Surface Ops, or Results menus.

Completed mission objectives expose an explicit claim in Mining, Flight, and the dock. Claiming grants its reward once and leaves control in the current activity. The objective then reads **MISSION COMPLETE**; old briefing, failure, and retry actions cannot reopen it. The dock separates the next mission from the selected waypoint and keeps departure prominent. Recovery and reload guardrails derive screens from the actual ship state and retire instructions for deployments that have ended.

The compact solar mission tracker lists separate read-only checkboxes for establishing orbit, delivering the required Common Ore to the ship, and bringing the artifact aboard. Active mining and saved survey/site progress count as evidence of the completed orbit. Ore counts advance on delivery, and exposing or tethering an artifact does not check off its recovery. A lost artifact becomes an incomplete wreck-recovery goal. Short contextual instructions remain below the checklist when needed; the Missions button opens the detailed log.

## Retired activity migration

The separate Flyby, Orbit, pulse-timing Scan and Push Deeper activities are removed from simulation, rendering, input, debug and scenario routing. Orbital work uses physical Flight; surface preparation leads directly to Rig/EVA mining without a survey or dig timing gate. Solar progression advances through explicit artifact mission claims and ordinary physical travel.
