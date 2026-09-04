# One Flight, One Surface

This document describes the current version-18 activity flow. It replaces the retired Arrival Ops, standalone Flyby/Orbit, Surface Ops, Survey, and Push phase-board designs.

## Player-facing flow

`Launch ritual -> physical Flight -> orbit or flyby -> deorbit and land -> touchdown celebration -> physical Mining/EVA -> takeoff ritual`

Flight is one continuous player-controlled simulation. Position, velocity, heading, fuel, heat, and hull carry through transfer, orbit capture, descent, and landing. The prediction line is guidance, not a rail. A/D rotates, W provides main thrust, S provides braking thrust, and coasting is free. Orbit is earned by maintaining a valid path through the visible annulus; landing grades come from actual vertical speed, lateral speed, and tilt.

The two-second touchdown celebration and the complete planetary takeoff animation are automatic ceremonies, not acknowledgement screens. They preserve impact and place without adding another decision.

## Continuous landed world

Mining, scanning, drilling, towing, EVA, Support Drones, cargo, and mothership service all occur in `Screen::Mining`. The service zone banks only payload that physically reaches the ship. Oxygen service never creates fuel. A disabled zero-fuel rig remains recoverable by EVA, towing, or a physical fuel cell.

The Mining Rig has a hard 24-mass payload cap. Load changes its movement and powered fuel cost immediately:

| Band | Mass | Speed | Fuel use |
|---|---:|---:|---:|
| Light | 0-5 | 100% | x1.00 |
| Standard | 6-11 | 90% | x1.15 |
| Laden | 12-17 | 72% | x1.40 |
| Packrat | 18-23 | 55% | x1.70 |
| Full | 24 | 50% | x1.75 |

Drilling always creates physical loose objects. At capacity, the intake reports `FULL` and excess ore stays in the world. Support Drone payload counts only after the drone returns and unloads. `Leave Now` remains available and names the drones and payload that will be lost.

## Lunar opening

The first Moon expedition teaches the complete game instead of preparatory calibration sorties:

1. Fly to the Moon, capture a physical orbit, deorbit, and land.
2. Mine and return 20 Common Ore in one intended haul.
3. Contract allocation accepts the 20 ore before permanent ship storage.
4. The twentieth ore activates an anomaly while the player remains landed.
5. Pulse the scanner, follow its world-space bearing, and reach the marked suit-only crevice.
6. Exit the rig, hand-drill, tether, and physically recover the artifact.
7. Claiming the artifact unlocks the Mars route; the ore contract alone does not.

Prompts appear only when their verb matters. Mission Control transmissions are at most two short sentences plus one concrete action. Heat stays hidden during the first Moon lesson; fuel, oxygen, and load are active immediately.

## Progression and storage

Permanent ship storage remains 12/16/20/24 mass and never exceeds 24. Contract allocation happens before ordinary storage, so mission cargo does not need to fit the persistent hold. Overflow remains with its physical owner. A single-haul contract may not exceed guaranteed Rig capacity; multi-haul contracts must be explicitly authored.

Mars currently requires 8 Common Ore until separately retuned. The first lunar industrial delivery requires 20 Common Ore.

Crew are authored characters with fixed species/class perks. Training, stress, rest, related facilities, and Psyche placeholders are not part of version 18.

## Architecture and save boundary

`GameState` is authoritative. `FlightRunState` lives in `RunState` and persists the active physical trajectory, finite resources, hull, orbit progress, and landing state. Core systems mutate state; typed presentation and render snapshots only display it. Native and web use the same gameplay actions and rules.

Version 18 is an intentional clean campaign boundary:

- Only v18 is accepted.
- There is no v17 migration or partial restoration.
- Incompatible campaign and checkpoint data is not loaded.
- The old campaign remains untouched until the player explicitly confirms `New Campaign`.
- A confirmed new campaign writes a clean v18 save and uses a v18-specific checkpoint key.

Current v18 output does not serialize Action Kits, shared surface fuel, fuel-cycle progress, Surface Scan/Push payloads, training chores, stress, or retired screen state.
