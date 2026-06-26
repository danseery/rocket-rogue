# Mining Mini-Game Plan

See `docs/AGENT_DESIGN_CONTEXT.md` before extending this system. The current mining direction should follow `docs/reference/USG_NOTES.md` first: chunky/mobile, Straylight-inspired, fog-of-war, destructible terrain, excavation/logistics/endurance roles, and passive defense only after the solar system.

See `docs/MINI_DRONE_SYSTEM.md` for the persistent Drone Bay layer that modifies mining, scanner, logistics, and later passive-defense behavior.

This plan layers a playable mining phase onto the current post-arrival flow without replacing the existing launch, arrival, research, or surface-expedition architecture.

## Design Goal

Mining should be the landed version of the rocket launch loop:

- The player chooses how deep to dig, how much cargo to load, and when to extract.
- Better crew, tools, and research make risk more readable and controllable, but never remove it.
- Early solar-system mining is environmental: dust, drill chatter, pressure pockets, unstable terrain, low supply, and extraction risk.
- Enemies stay out of the Moon and Mars loop. Hostile encounters begin after the agency leaves the solar system.

## Entry Point

Current flow:

1. Reach a destination.
2. Choose Flyby, Orbit, or Landing.
3. Landing opens Surface Expedition.
4. Press Mine deposit.

Next playable step:

- Pressing Mine deposit can open a compact Mining Mini-Game instead of instantly resolving the mine action.
- Survey, Push deeper, and Extract can remain menu actions during the first pass.
- The mini-game resolves back into the existing `SurfaceActionOutcome`: materials gained, cargo gained/lost, hazard changes, supply spent, artifacts found, and log entries.

This keeps save data, research unlocks, surface actions, and refit rewards intact.

## Core Loop

The first version should be quick, readable, and button-driven:

1. Scan seam: reveal 2-3 ore pockets with danger/value hints.
2. Drill seam: hold or tap to extract material while TEMP/VIB/PRESS-style gauges climb.
3. Stabilize: choose one mitigation when a gauge spikes.
4. Stow cargo: bank part of the payload, increasing extraction risk.
5. Continue or extract: push for rare material/artifact odds or leave before the field team loses the payload.

Target duration: 20-45 seconds per mining attempt.

## Mining Resources

- Supply: shared expedition timer. Mining spends supply up front and can burn more through hazards.
- Drill integrity: mini-game durability. Low integrity raises cargo loss and equipment failure.
- Oxygen/time: survival pressure during deeper digs.
- Cargo load: reward now, extraction risk later.
- Seam instability: mining-specific hazard that feeds back into surface hazard.

## Player Actions

- Drill: extracts common material and slowly raises VIB/heat.
- Feather drill: slower extraction, lower VIB.
- Brace rig: lowers VIB, slightly raises drill wear.
- Vent pocket: lowers PRESS, can knock NAV/extraction alignment off course.
- Pulse scanner: reveals rare pockets or hidden instability, costs time/supply.
- Stow sample: banks cargo but raises extraction risk.
- Abort mine: leaves the mini-game with current cargo and no extra push.

## Failure Modes

- Drill chatter: cargo loss, hazard increase.
- Pressure pocket: emergency vent choice; failed vent can cause rapid decompression.
- Cave-in: supply loss and forced extraction check.
- Tool jam: drill integrity loss or action lockout.
- Toxic/radiation seam: post-Mars environmental modifier, mitigated by research gear.
- Hostile contact: only after Nearby Star and beyond.

## Crew Classes

Training still levels the active crewmember. Class traits give the player a reason to care who is cleared for the surface sortie.

| Class | Focus | Mini-game role |
| --- | --- | --- |
| Capybara Tank | Survival | More supply margin, lower cave-in and extraction injury risk. |
| Beaver Engineer | Resilience | Better drill integrity, fewer tool jams, stronger repair actions. |
| Fox Ace | Navigation | Safer extraction paths and better abort windows. |
| Prairie Dog Scout | Digging | Better seam scans, faster discovery of ore pockets and artifacts. |
| Squirrel Hoarder | Resource Gathering | Better rare-material odds and cargo handling. |
| Chipmunk Speedster | Exploration | Faster actions, better anomaly/shortcut discovery, lower field hazard exposure. |

## Research Hooks

Research should improve the mini-game through specific tools:

- Field Probe Network: better seam previews and fewer false positives.
- Regolith Drill Rig: faster drilling and lower chatter chance.
- Cargo Return Rig: lower extraction penalty from heavy payloads.
- Applied Materials Lab: turns common samples into practical fabrication unlocks.
- Mission Analysis Lab: converts field notes into extra blueprint progress.
- Perimeter Drone Network: passive defense for post-solar-system mining.

## First Playable Slice

Keep the first implementation small:

- One mining screen opened from Mine deposit.
- Three ore pockets: safe/common, unstable/common+rare chance, deep/rare+artifact chance.
- Three mitigation buttons: Feather drill, Brace rig, Vent pocket.
- One extraction decision.
- No enemies.
- No new art requirement; use the existing card/UI language and telemetry-style gauges.

The goal is to prove whether landing turns into an interesting second press-your-luck moment before building terrain, movement, or combat.
