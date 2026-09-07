# Orbital Preparation

The [OREBIT Game Design Document](Rocket_Rogue_Game_Design_Document.docx) is the definitive design. This document supplies implementation detail and must agree with it. Story and progression decisions marked TBD are collected in GDD Section 8.

## Player flow

Shape a safe coasting loop, then release thrust for two real seconds. Rotation
is allowed. Capture checks up to 60 simulated seconds with the same midpoint
gravity integrator used by the visible 20-second forecast. Precision rings
grade Perfect; they no longer gate Good capture. The ship is never repositioned.

After capture, Space/Enter, controller South, or the primary button starts the
two-second Pulse Survey. Flight pauses with its momentum, fuel, hull and engine
heat intact. Survey Array rank reveals the entry layer plus 1–4 deeper layers,
independent of Bore. Findings summarize actual prepared terrain, not quantities
or exact underground positions. The first lunar anomaly stays hidden.

After Survey, fly into Zone 1, then a fresh press-and-hold fires Orbital Laser
Dig. Survey may start from any qualifying captured orbit; excavation requires
being inside that zone. Outside, inspection offers Resume Flight and the flying
action reads Enter Zone 1 to Dig. Holding the action across the boundary never
starts the beam. Its aimed mount targets the shaft without turning the ship.
Release to stop.
Laser heat rises 25 points/second and cools 35 points/second. At 100 it cuts out;
cool to 25 and press again. Laser work costs no ship fuel or hull. Resume Flight,
Escape/controller East, or steering/thrust resumes normal flight. Findings,
partial excavation and cooling persist for this visit.

## Terrain ownership

Six fixed 60-degree sectors share core geometry with landing and prediction.
Only Zone 1 (the original inbound gate) is enabled. Its stable ID participates
in the session cache key, but its original terrain seed is unchanged. No five
extra sites are generated. Each future enabled zone will own its prepared site,
findings, excavation and loose ore independently; selection will not move the
ship. A crossed enabled gate determines the landing site.

The cutaway clips depth bands to the selected sector. Pad, actual shaft offset,
laser beam and excavation frontier use that same camera-transformed bearing.
Site offsets fit within the central 30 degrees rather than pretending Mining
grid coordinates are literal planetary circumference. Inactive sectors have
only subtle divisions, never resource findings or selectable controls.

The prepared site owns the fixed shaft and cached depth layers. Maximum depth
is the lesser of surveyed reach and Bore rating. The red laser cuts a ten-cell
shaft (twice the original width). Solid rows require three beam
seconds per full layer; empty rows require no work. Pad support, protected gates,
artifact barriers, suit-only passages, fuel and oxygen pockets stop the beam.
Ore is loose physical cargo in the modified layer, never credited from orbit.

After survey, the Land action is available during inspection inside Zone 1. It zeros linear/angular velocity and holds position while rotating the nose inward over one second; gravity and control then resume. Manual gate crossing remains available and preserves momentum. Both paths enter the same prepared terrain.

Landing commits these exact layers through the arrival flow. Mining
remains frozen through touchdown and deployment. Orbital work is session-only;
after beginning work, saving waits until deployment or undeployed takeoff ends.
Closing earlier returns to the preceding flight save.

## Debug inspection starts

- Starter Moon: `/?debug_tools=1&debug_surface_arrival=orbital&debug_surface_destination=moon&build=orbital-work`
- Upgraded Mars: `/?debug_tools=1&debug_surface_arrival=orbitaldeep&debug_surface_destination=mars&build=orbital-work`
- Inside Zone 1: `/?debug_tools=1&debug_surface_arrival=orbitalzone&debug_surface_destination=mars&build=aligned-zone`

The first two starts use a broad coasting ellipse and exercise confirmation naturally.
`orbital` uses Survey/Bore rank 0; `orbitaldeep` uses rank 3. Swap `moon`/`mars`
as needed. Existing arrival presets remain available. Debug sessions do not save.
`orbitalzone` begins captured inside the active sector with starter equipment.

## Verification

Exercise capture, survey pause/resume, input release across the zone boundary, heat cutoff/restart, protected terrain, partial excavation, exact-site descent and physical ore recovery. Include keyboard/controller source changes and both manual descent and the explicit Land action. End-to-end feel and hardware acceptance require direct play checks.
