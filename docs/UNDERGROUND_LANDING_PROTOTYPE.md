# Underground Landing and Manual Ascent

The [OREBIT Game Design Document](Rocket_Rogue_Game_Design_Document.docx) is the definitive design. This document supplies implementation detail and must agree with it. Story and progression decisions marked TBD are collected in GDD Section 8.

Landing physics and its forecast now query one Cartesian projection of the real
cached Mining layers. Layer offsets accumulate actual layer heights at four
metres per cell. Adjacent layers are prepared ahead of camera, swept collision,
and forecast range. Generation and excavation remain core-owned; rendering
cannot create terrain or passages. Intact bedrock boundaries remain physical.
Depth itself is not a fatal altitude threshold.

The mothership's layer/position is independent of geological entry. A surviving
upright contact still needs real supporting ground and rig staging clearance.
No underground bay is stamped. Touchdown lasts two seconds; deployment lasts three seconds. Nearby ship illumination reveals terrain without
revealing protected objectives.

Packing settles payload once and retains the modified site. Ignition hands
control to local Landing physics at the parked ship. A grounded-support latch
lets the normal throttle ramp lift the ship without immediately retriggering
touchdown. Subsequent contact uses normal damage and landing rules. Departure
thrust consumes no fuel until the +46 m surface-relative, upward >=2 m/s Orbit exit;
gravity, rotation, thrust strength, and collision damage remain active. Local ascent causes no heat damage.

Ship services, return guidance, artifact delivery, and extraction eligibility
use the parked layer. Support Drone deliveries follow a terrain path through
cached layers; their manifests are not banked by an elapsed transit timer.
Unopened seam lips cannot be crossed by Mining actors.

Saves are v21 only. Local pose, fixed surface origin, mothership location,
departure/support state, drone transit depth, and exact cached terrain persist.
Prepared sector terrain and excavation persist independently. Arrival and packing save only at their
completed handoffs. Version 21 saves use safe defaults for missing optional fields. Unsupported save versions are preserved.

## Restartable browser presets

Use `?debug_tools=1&debug_surface_destination=moon` (or `mars`) and:

- `debug_surface_arrival=shaft`: approach an excavated layer seam.
- `debug_surface_arrival=underground`: settle at a real shaft floor.
- `debug_surface_arrival=undergroundimpact`: damaging floor approach.
- `debug_surface_arrival=shaftascent`: upward underground correction.
- `debug_surface_arrival=shaftwall`: angled wall contact.

Presets use real generation and excavation, not special landing bays. Debug
sandboxes do not write the player's save. Normal campaign Continue is required
to feel-check persisted Mining/ascent. Existing surface arrival presets remain.

## Verification

Check shaft seams, surviving and fatal floor/wall impacts, upright support and rig clearance, parked-layer services, physical drone delivery, packing once, ascent contacts, the +46 m Orbit exit, and save/Continue of underground Mining and departure. Handling and camera acceptance require direct player review.
