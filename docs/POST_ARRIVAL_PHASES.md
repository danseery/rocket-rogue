# Flight and Surface Flow

The [OREBIT Game Design Document](Rocket_Rogue_Game_Design_Document.docx) is the definitive design. This document supplies implementation detail and must agree with it. Story and progression decisions marked TBD are collected in GDD Section 8.

## Activity ownership

`Earth dock -> physical system/body flight -> local Landing -> touchdown -> deployment -> Mining/EVA -> packing -> manual ascent -> return home or continue travel`

Orbit survey and laser excavation are optional preparation within Flight. A qualifying coasting loop held for two seconds earns capture. Survey pauses flight; excavation uses the selected sector and a fresh held input. Manual descent crosses an authorized gate with momentum intact. The explicit Land action from surveyed inspection stops and aligns the ship before normal gravity resumes.

## Handoffs

| Transition | Control and simulation | Persistence |
| --- | --- | --- |
| Travel to Orbit | Approach zoom; close flight uses 40% world speed and full controls. | Authoritative Flight state. |
| Survey and laser | Flight pauses; inspection and beam progress advances. | Sector findings and excavation persist independently. |
| Orbit to Landing | 1.25-second camera blend; local controls and collisions use real terrain. | Prepared terrain is not yet committed as a deployed expedition. |
| Touchdown | Two-second flourish; fresh deploy input may buffer; first accepted deploy/depart command wins. | Landing commits in memory. |
| Deployment | Three-second bay/rig/drone staging and camera handoff; Mining/resource clocks stopped. | Completed deployment saves Mining. |
| Undeployed departure | Ship-only sequence then flight routing. | Save at completed handoff. |
| Packing and ignition | Settle payload once; resume local flight at parked ship. | Completed packing handoff saves the retained site and departure state. |
| Ascent to Orbit | Exit at +46 m surface-relative altitude and >=2 m/s upward; inverse momentum conversion. | Persist physical flight and retained site. |

## Continuous landed world

Mining, EVA, scanning, drilling, towing, Support Drones, cargo and ship services occupy one physical site. Excavated and cached layers survive handoffs. Ship location is independent of geological entry. Underground services, extraction and drone delivery use the parked layer; unopened seam lips remain barriers.

Rig fuel uses an independent home-supplied tank retained across visits and gains fuel from physical cells. Thrust and drilling consume independently; idle/coast consume neither. Rig oxygen and Suit oxygen are separate; ship oxygen service does not create fuel. A disabled Rig remains recoverable. The Rig holds at most 24 mass, and excess ore remains loose. Contract allocation precedes ordinary ship storage; drone manifests are credited only after physical delivery.

Packing does not teleport the ship to orbit. Pilot the ascent through the same terrain. Departure thrust spends no fuel until the Orbit exit, while gravity and collision remain active. Local ascent causes no heat damage.

## Campaign connection

The lunar 20-Common-Ore delivery activates the anomaly during the expedition. Scanner discovery, EVA access, physical artifact recovery and explicit claim establish the Mars lead. Mars requires 8 Common Ore. Each later main mission recovers the artifact from the named landing moon. After claim and ascent, Earth service is recommended while manual onward travel remains available. Docking banks and services once; continuing preserves resource pressure and XP. Claiming the Triton artifact reveals Straylight. See [Persistent Expeditions](PERSISTENT_EXPEDITIONS.md).

## Detailed references

- [Orbital Preparation](ORBITAL_PREPARATION_PROTOTYPE.md)
- [Surface Arrival](HEROIC_SURFACE_ARRIVAL.md)
- [Underground Landing and Ascent](UNDERGROUND_LANDING_PROTOTYPE.md)
- [Mining and EVA](MINING_MINIGAME_PLAN.md)

## Opening and return continuity

The first expedition starts with explicit Launch from the Earth berth after a saved Incoming Message acknowledgement. Earth is framed below-left and the Moon above-right. The initial departure impulse is applied once; all subsequent motion follows physical flight. Later travel retains physical position and resources. Home departure waits attached until forward thrust. Target guidance stays distinct from actual-body orbit bands, and Moon ascent retains its committed site until physical frame exit.
