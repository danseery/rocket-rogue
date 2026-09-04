# Underground landing and manual ascent — v20 feel pass

Landing physics and its forecast now query one Cartesian projection of the real
cached Mining layers. Layer offsets accumulate actual layer heights at four
metres per cell. Adjacent layers are prepared ahead of camera, swept collision,
and forecast range. Generation and excavation remain core-owned; rendering
cannot create terrain or passages. Intact bedrock boundaries remain physical.
The former -400 m failure is removed.

The mothership's layer/position is independent of geological entry. A surviving
upright contact still needs real supporting ground and rig staging clearance.
No underground bay is stamped. Touchdown and deployment keep their existing
two/three-second timing. Nearby ship illumination reveals terrain without
revealing protected objectives.

Packing settles payload once and retains the modified site. Ignition hands
control to local Landing physics at the parked ship. A grounded-support latch
lets the normal throttle ramp lift the ship without immediately retriggering
touchdown. Subsequent contact uses normal damage and landing rules. Departure
thrust consumes no fuel until the +60 m surface-relative, upward >=2 m/s Orbit exit;
heat, gravity, rotation, thrust strength, and collision damage remain active.

Ship services, return guidance, artifact delivery, and extraction eligibility
use the parked layer. Support Drone deliveries follow a terrain path through
cached layers; their manifests are not banked by an elapsed transit timer.
Unopened seam lips cannot be crossed by Mining actors.

Saves are v20 only. Local pose, fixed surface origin, mothership location,
departure/support state, drone transit depth, and exact cached terrain persist.
Prepared arrivals remain session-only. Arrival and packing save only at their
completed handoffs. Old saves are not migrated or overwritten on loading.

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

Build only the web game for this pass. No new automated suite or broad matrix.
The visual/handling constants remain provisional for player feedback.
