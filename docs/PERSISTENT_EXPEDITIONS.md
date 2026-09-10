# Persistent Solar Expeditions

The solar campaign uses one continuous physical ship pose across launch, orbit, interplanetary travel, landing, ascent, docking, destruction, and wreck recovery. Menus and incoming messages pause simulation. Setting a waypoint changes only the marker, course preview, and fuel forecast; it never starts Cruise or moves the ship.

## Campaign loop

Each mission follows the same loop: receive a briefing, land and mine, pulse the artifact with the scanner, tether it to the ship, explicitly claim the completed mission, then return to Earth for service or continue manually. Claims grant rewards and discoveries once without changing the ship pose or opening another screen.

The main route is Moon, Mars, Io, Titan, Titania, and Triton. Mercury and Venus are optional recoveries revealed after the Moon. Earth is the sole solar service home. Gas giants remain map landmarks while their named moons contain the mining missions.

## Artifacts and batteries

Every landable solar mining world owns one deterministic, persistent artifact. Its world, sector, terrain, position, scan state, and ownership survive sector changes, revisits, and reloads. A scanner pulse must physically reach it before it is revealed, and tethered delivery to the ship records recovery. Mission artifacts cannot be permanently destroyed and can be recovered with a full ore hold.

The six main artifacts carry the Moon, Mars, Io, Titan, Titania, and Triton batteries. Capture moves a battery from its site to the ship. Earth docking moves carried batteries to Earth Storage and records research. Ship destruction moves carried batteries into the recoverable wreck. Mercury and Venus artifacts grant recovery rewards without batteries or route progress.

## Expedition builds and wrecks

An expedition build lasts until the main ship is destroyed or abandoned. Earth service, planetary travel, sector changes, Mining Rig destruction, recovery, and replacement preserve level, XP, pending choices, Rig ranks, Support Drone ranks, grafts, and synergies.

Main-ship loss stores cargo, carried batteries, unbanked payout, and the earned build in the wreck. The replacement starts with a fresh build. Salvage restores build data even with a full hold and consumes that build payload once; leftover ore remains recoverable. Distinct upgrades and synergies merge, duplicate ranks keep the higher rank through III, and unspent choices combine. Conflicting grafts pause for an explicit slot choice. Transient combat targets and cooldowns do not transfer.

## Drill progression

The starter Mining Rig cuts 20 percent faster than the prior baseline. High-Torque Motor adds 25 percent of improved starter power per rank. Wide Drill Head adds 25 percent of original head width per rank. Side Cutters add 0.5 cells on each side per rank at 60 percent power. Hard-Rock Teeth add 25 percent power against Hard Rock per rank. Coolant Mist reduces drilling heat generation by 15 percentage points per rank. All have three ranks.

The physical drill, rendered drill, terrain contact, movement collision, and clearance checks share the upgraded geometry. Overlapping contacts damage a cell once at the stronger value. Fuel and heat exposure remain time based. Every draft includes an eligible drill choice while ranks remain; the first draft includes High-Torque Motor and one width choice, and the other width choice appears by the third draft unless already owned.

The Rig starts at 24 cargo capacity. Cargo Skids, Ore Hopper, and Expandable Panniers add 2, 1, and 3 capacity per rank. Collection, available space, load display, Full state, and quarter-capacity load penalties read the same effective capacity.

## Discovery and waypoints

New campaigns chart only the Sun, Earth, and Moon. Claiming the Moon reveals Mercury, Venus, and Mars. Each later main claim reveals the next gas giant and its mission moon. Only the claimed Triton artifact reveals Straylight.

The map keeps preview selection separate from the saved waypoint. Planet artwork and labels update the preview while the modal remains open. **Set waypoint: [world]** commits the choice. Closing preserves the previous waypoint. At Earth the dock shows the recommended next mission, the selected waypoint, and a prominent departure action. A deliberate valid waypoint remains selected until the player changes it or completes it.

## Save boundary

Save schema v23 is the only accepted campaign format. Older and malformed campaign saves go directly to the new-campaign flow with clear copy. There is no migration, progression inference, retired activity repair, or remote-Hangar reconciliation. All v23 mission, artifact, battery, discovery, message, waypoint, physical flight, site, drill, draft-guarantee, and recoverable-wreck-build state persists directly.
