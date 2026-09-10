# Orbital Preparation

The [OREBIT Game Design Document](Rocket_Rogue_Game_Design_Document.docx) governs design. This document describes the current physical-flight implementation.

## Capture and sector selection

Coast on a qualifying safe loop for two real seconds without thrust; rotation is allowed. Capture uses the shared gravity forecast and never relocates the ship. ORBIT HERE points to the outer ring before capture. Six fixed 60-degree sectors divide each body. Captured flight selects the nearest sector using the actual body-relative bearing and changes discretely at boundaries. SCAN HERE points to that sector; the wedge does not smoothly follow the player.

## Survey and drilling

SCAN runs for two seconds and radiates a pulse from the ship. Inspection pauses flight motion and its fuel, hull and engine heat. Survey Array exposes the entry layer plus one to four deeper layers, independently of Bore. Findings come from actual prepared terrain, not random rewards at scan time.

Hold DRILL after survey to cut the selected shaft; release to stop. The laser has no heat meter or cooldown. Survey and Bore both limit depth. The red beam is visible while firing and the completed shaft stays green. The ten-cell shaft cuts solid rows over three beam-seconds per full layer; empty rows require no work. Pad support, protected gates, artifact barriers, suit-only passages and supply objects remain protected. Excavated ore is loose physical cargo and is never credited from orbit. Falling laser debris disappears at the bottom instead of pooling there.

DRILL HERE and LAND HERE point to the surface, with their text offset beside the approach lane. LAND stops and aligns the ship over one second before ordinary local gravity resumes. Manual authorized gate crossing preserves momentum. Resume Flight, Escape/controller East, or steering/thrust exits inspection. A held action must be released before beginning a new orbital action.

## Site ownership and persistence

Each site has a stable system/body/sector identity and is generated from that identity and the campaign seed. Visit count never changes generation. Save the selected site's survey and excavation before switching; restore its own cached layers, loose resources and objective state on return. This includes scanned or partially drilled sites never landed on. Site restoration never rolls back player inventory, upgrades, expedition progression or campaign rewards.

The first scanned Moon sector receives the tutorial anomaly once per campaign. Other sectors receive ordinary mining content. Existing saves retain their recorded anomaly placement and completion; collected artifacts never regenerate. Optional v23 fields use the recorded sector or the original sector fallback for older records.

Descent, touchdown, mining and ascent retain the selected site's terrain and body ownership. The shared surface camera uses uniform scale and square tiles, preserving the ship and terrain anchors through deployment.

## Existing debug fixtures

- `/?debug_tools=1&debug_surface_arrival=orbital&debug_surface_destination=moon`
- `/?debug_tools=1&debug_surface_arrival=orbitaldeep&debug_surface_destination=mars`
- `/?debug_tools=1&debug_surface_arrival=orbitalzone&debug_surface_destination=mars`

These fixtures never write campaign progress. Use the ordinary capture, survey, drilling and landing actions to inspect them.
