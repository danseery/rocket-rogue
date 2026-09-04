# Heroic surface arrival: feel prototype

Flight prepares the deterministic Mining world without committing campaign changes. The same terrain and service pad become visible during descent. The first 16 tile rows below pad height are revealed by default across the surface layer, displaying real rock and ore textures rather than a gray arrival mask. That knowledge remains after deployment; deeper unexplored terrain retains fog, while protected objectives, gated cells, and scanner-taught suit passages keep their discovery rules. Touchdown commits the landing in memory, but does not save.

After the two-second touchdown flourish, Space/Enter (controller South) deploys the team. A fresh press during touchdown is buffered. R (controller East hold) takes off without deployment. The first accepted command owns the sequence.

Deployment lasts three seconds: bay opening, rig drop and arrest, equipped-drone fan, staging movement, and camera/layout handoff. Mining simulation and resource clocks do not run until control transfers. Completed deployment saves Mining; completed ship-only takeoff saves the routed state. The normal 3.4-second extraction ritual is unchanged.

## Local feel presets

Use `http://localhost:8080/?debug_tools=1&debug_surface_arrival=command&debug_surface_destination=moon`.

- Destination: `moon` or `mars`.
- Phase: `approach`, `safe`, `hard`, `command`, `bay`, `rig`, `drones`, `staging`, `takeoff`, `correction`, `inverted`, `fast`, `touchdown`, `climb`, `gate`, or `fastgate`.
- `approach` starts at 60 m with a 4 m/s descent; `correction` starts at 35 m with upward and lateral drift; `inverted` points the nose down; `fast` retains a dangerous 24 m/s descent; `touchdown` starts 2 m above the pad; `climb` approaches the 120 m exit boundary from below. No inputs are automated.
- `gate` starts in Orbit just outside the real descent gate, carrying momentum for approximately 5 m/s downward and 4 m/s sideways after conversion. `fastgate` uses approximately 30 m/s downward: it still enters Landing and remains dangerous. Both exercise the actual 1.25-second handoff.
- These presets run in a save-isolated sandbox and equip a three-drone visual reference team. Campaign arrivals deploy only the actual equipped loadout.

The preset URL restarts that moment on refresh. Remove the debug parameters to use the campaign.

## Manual landing handling trial

Travel, Orbit, and Landing are authoritative modes within Screen::Flight. Travel enters Orbit when moving inward through 40% of the initial destination distance. The camera zoom starts there (60% traveled) and remains latched. Orbit uses 40% world speed and full controls. Outbound departure requires the 10% outer buffer; capture and rewards survive the transition.

An authorized inward crossing of the marked 60-degree descent gate at radius 0.40 commits to Landing. The fixed basis projects orbital momentum into local horizontal and vertical velocity using a constant 12:1 conversion, with no speed clamp. Entry altitude is 60 m. A 1.25-second handoff blends the camera into the exact prepared site. Gravity, controls, prediction and collision then use the local frame. The pad stays fixed while the ship moves sideways. Climb through 120 m with at least 2 m/s upward velocity to return to Orbit using the inverse momentum conversion. The gate must rearm outside radius 0.55 before another entry.

Point the nose up and pulse W to arrest descent. S thrusts opposite the nose; it accelerates downward only when the nose is up. Keyboard thrust ramps over 0.40 real seconds and release cuts immediately. Analog deflection is proportional. A rotates counterclockwise and D clockwise in every mode. Landing turns at a target 75 degrees/second with a 0.15-second response, damping rotation on release without leveling. In Landing, all physics and resource consumption use real seconds.

Landing gravity is 3 m/s² downward, forward thrust is 6 m/s², and reverse thrust is 3 m/s². Positions, velocities and collision use local metres (four metres per Mining cell). The real shuttle footprint is swept against the prepared terrain. Safe terrain and adjacent rig staging are required for a successful touchdown. The accepted location becomes the mothership service zone, without generating or carving replacement terrain on contact.

The physical-flight instruments report speed from the actual velocity vector (in the same units as landing telemetry), not the retired route multiplier. The existing thrust bar shows delivered partial power for both forward and reverse burns.

In the committed local Landing activity, collision damage is `max(0, impactSpeed - 8) * 5` hull points. Impact speed is inward contact-point velocity, including rotation; parallel motion is not a head-on impact. Terrain contacts are aggregated into one episode. Surviving contacts rebound at 15% inward speed, retain 85% tangential speed, and halve rotation. Only hull depletion destroys the ship during these terrain collisions. Supported contact settles with vertical speed at most 1 m/s, lateral speed at most 5 m/s, tilt at most 25 degrees, and adjacent rig clearance. Otherwise the pilot retains control. Planet-body contact in Fly or Orbit is always fatal, irrespective of speed or remaining hull, with no rebound. The actual movement mode, not camera blend, selects this rule; descent-gate authorization still applies. Asteroid damage remains unchanged.

Additional collision presets: `impact` gives an approximately 18 m/s contact with 85 hull; `fatal` repeats it with 40 hull; `scrape` starts tilted with sideways motion; `rest` checks quiet settling; `bodyimpact` hits the planet outside the landing gate. Impact speed, damage, and hull before/after are shown in the result modal and Flight Report, not the side panel. These display records are session-only; existing v19 hull persistence is unchanged.

These values are provisional until the browser feel pass is approved. No new automated tests accompany this tuning trial.

## Trajectory feel trial

Fly and Orbit show a rolling coasting forecast of up to 20 simulated seconds at 0.2-second intervals (100 future points plus the ship). Prediction uses midpoint integration with the existing gravity law and continues across Fly/Orbit boundaries without resetting visual smoothing. Only contact or entry into the local Landing frame ends the space forecast early. Landing forecasts up to 3.84 seconds of local gravity-only motion with 96 samples, stopping at terrain or upward departure. Burns affect these guides through actual momentum, not assumed future engine use.

All forecast points feed a centripetal spline with shared bounded tangents. Fixed presentation budgets prevent changing contact length from resetting interpolation; the visible curve is resampled by arc length to 101 points in space or 96 in Landing. Near/far damping remains 100/450 ms, with an exact ship anchor, 4.5-pixel purple/pink/teal stroke, and faded tail. This changes guidance only, not live physics or saves.

## Audio and validation boundary

Typed phase cues are connected to Web Audio and optional SDL3 audio. Supply approved WAV files listed in `assets/audio/surface/README.md`; absent cues log once and remain silent. No synthesized or AI-authored sounds are supplied.

This is a web feel pass, not release certification. Camera, staging arcs, timing subdivisions and feedback strength are provisional. Schema v19 persists explicit mode, local landing coordinates, site identity, gate rearm state and handoff state. Only v19 loads; older saves remain untouched until explicit New Campaign. No migration or new automated suite is included. Focused lifecycle tests follow feel approval; native/controller hardware and release validation remain deferred.
