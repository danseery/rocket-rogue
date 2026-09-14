# Persistent Solar Expeditions

The solar campaign uses one continuous physical ship pose across launch, orbit, interplanetary travel, landing, ascent, docking, destruction, and wreck recovery. Menus and incoming messages pause simulation. Setting a waypoint changes only the marker, course preview, and fuel forecast; it never starts Cruise or moves the ship.

## Campaign loop

Each mission follows the same loop: receive a briefing, land and mine, pulse the artifact with the scanner, tether it to the ship, explicitly claim the completed mission, then return to Earth for service or continue manually. Claims grant rewards and discoveries once without changing the ship pose or opening another screen.

**READY TO CLAIM** remains actionable in Mining, Flight, or the Earth dock; a deferred claim is never hidden by departure, death, or servicing. Claimed objectives read **MISSION COMPLETE** and cannot restart a completed activity or old failure prompt. Completion messages follow the saved claim through ascent, travel, recovery, and reload and acknowledge once. Completed-mission briefings and mining instructions from an ended deployment are removed before the next applicable message.

The main route is Moon, Mars, Io, Titan, Titania, and Triton. Mercury and Venus are optional recoveries revealed after the Moon. Earth is the sole solar service home. Gas giants remain map landmarks while their named moons contain the mining missions.

## Artifacts and batteries

Every landable solar mining world owns one deterministic, persistent artifact. Its world, sector, terrain, position, scan state, and ownership survive sector changes, revisits, and reloads. A scanner pulse must physically reach it before it is revealed, and tethered delivery to the ship records recovery. Mission artifacts cannot be permanently destroyed and can be recovered with a full ore hold.

Legacy artifacts overlapping a laser shaft receive the same protected-placement repair on save loading as on site revisits, including the active mining layer. Safe repairs move the embedded artifact and its seal sideways into untouched terrain while retaining excavation, actor positions, resources, and partial recovery progress. Loose or tethered artifacts are not relocated; if no safe placement exists, the laser remains blocked.

The six main artifacts carry the Moon, Mars, Io, Titan, Titania, and Triton batteries. Capture moves a battery from its site to the ship. Earth docking moves carried batteries to Earth Storage and records research. Ship destruction moves carried batteries into the recoverable wreck. Mercury and Venus artifacts grant recovery rewards without batteries or route progress.

The dock displays an **ARTIFACT BANKED** confirmation with the names secured in Earth Storage. Straylight displays the same indicator for installed beacons. This indicator reads saved ownership, survives reloads, and excludes artifacts still carried by the player or held in wrecks. Docking at the dormant Ark alone does not bank a beacon; installation remains explicit.

## Expedition builds and wrecks

An expedition build lasts until the main ship is destroyed or abandoned. Earth service, planetary travel, sector changes, Mining Rig destruction, recovery, and replacement preserve level, XP, pending choices, Rig ranks, Support Drone ranks, grafts, and synergies.

Main-ship loss stores cargo, carried batteries, unbanked payout, and the earned build in the wreck. The replacement starts with a fresh build. Salvage restores build data even with a full hold and consumes that build payload once; leftover ore remains recoverable. Distinct upgrades and synergies merge, duplicate ranks keep the higher rank through III, and unspent choices combine. Conflicting grafts pause for an explicit slot choice. Transient combat targets and cooldowns do not transfer.

Normal main-ship destruction and Abandon Ship return directly to the Earth dock. One loss produces one wreck and one replacement; repeated loss or departure actions cannot duplicate them. The opening-flight exception presents one **Retry launch** action and immediately starts the next attempt. Neither path opens a legacy Results, Navigation, or remote Hangar screen.

The first ordinary ship loss opens a campaign-once salvage explanation with saved acknowledgement. Artifact-bearing wrecks use a purple diamond and `Artifact / Wreck N` guidance; recovered artifacts remove that distinction from any leftover ore wreck. Nearby salvage controls explain speed matching and full-hold recovery. Artifact-specific recovery messages continue to identify each new wreck that holds an unbanked battery.

EVA death and Emergency Recall recover the player at the surviving parked ship on the same site. Stowed ore, claimed mission progress, and the expedition build survive. Rig and drone payload that has not reached the ship becomes loose cargo at the loss location; released artifacts remain recoverable. The player may resume mining or depart using the ordinary ship actions. Recall at the ship is rejected instead of creating an extraction loop.

## Drill progression

The starter Mining Rig cuts 20 percent faster than the prior baseline. High-Torque Motor adds 25 percent of improved starter power per rank. Wide Drill Head adds 25 percent of original head width per rank. Side Cutters add 0.5 cells on each side per rank at 60 percent power. Hard-Rock Teeth add 25 percent power against Hard Rock per rank. Coolant Mist reduces drilling heat generation by 15 percentage points per rank. All have three ranks.

The physical drill, rendered drill, terrain contact, movement collision, and clearance checks share the upgraded geometry. Overlapping contacts damage a cell once at the stronger value. Fuel and heat exposure remain time based. Every draft includes an eligible drill choice while ranks remain; the first draft includes High-Torque Motor and one width choice, and the other width choice appears by the third draft unless already owned.

The Rig starts at 24 cargo capacity. Cargo Skids, Ore Hopper, and Expandable Panniers add 2, 1, and 3 capacity per rank. Collection, available space, load display, Full state, and quarter-capacity load penalties read the same effective capacity.

## Discovery and waypoints

New campaigns chart only the Sun, Earth, and Moon. Claiming the Moon reveals Mercury, Venus, and Mars. Each later main claim reveals the next gas giant and its mission moon. Physical delivery of Triton's artifact to the ship immediately reveals Straylight and sets its waypoint, before a claim or Earth drop-off.

## Straylight sequence

Straylight is eighteen units beyond Neptune along the Sun-to-Neptune direction. Before discovery it has no visible or selectable contact. Its operational hull has independent presentation scale and a physical docking corridor; damaged art belongs to later Ark damage.

The saved sequence stages own first contact, beacon retrieval, activation, evacuation, and departure. A twelve-second reveal follows the normal Triton departure; the existing unidentified-AI transmission then returns manual control. First docking plays a ten-second bay approach and explains that the recovered objects are beacons left by Straylight's crew. Their power sources can restart the Ark so it can charge again.

Accepting the retrieval mission sets Earth as the waypoint. Collect stored beacons loads every Earth-owned beacon in one action, regardless of ore capacity; missing wreck beacons retain their ownership and recovery markers. Install carried beacons accepts partial deliveries at Straylight. Only six installed beacons enable the explicit point-of-no-return confirmation.

Confirming activation ends solar exploration, sets Straylight as home, and begins a fifteen-second awakening. The dying-Sun briefing then leads through explicit evacuation coordination, a ten-second boarding montage, securing the Ark, and a twelve-second departure. Arrival moves the expedition into generated Aaru Vale at the Ark dock and waits for acknowledgement. The current endpoint is a safe arrival tableau; further Aaru Vale objectives remain separate work. No passenger counts, hidden deadline, or rescue-management system are implied.

Animations freeze flight, hazards, and resource use. Skip finishes only the current animation. Stage boundaries persist; reload restarts an unfinished animation without repeating completed transfers or accepting dialogue. Existing current-schema saves default safely into unfinished contact, while activated and departed saves retain their progress.

The map keeps preview selection separate from the saved waypoint. Planet artwork and labels update the preview while the modal remains open. **Set waypoint: [world]** commits the choice. Closing preserves the previous waypoint. At Earth the dock shows the recommended next mission, the selected waypoint, and a prominent departure action. A deliberate valid waypoint remains selected until the player changes it, including revisits to completed missions. Recommendations follow physical mission bodies rather than reused geology: after Mars, the next mission is Io.

## Save boundary

Save schema v23 is the only accepted campaign format. Older and malformed campaign saves go directly to the new-campaign flow with clear copy. There is no version migration or progression inference. Current-campaign screen guards use the authoritative physical ship state to keep interrupted recovery in Flight, Mining, or an actual dock. All v23 mission, artifact, battery, discovery, message, waypoint, physical flight, site, drill, draft-guarantee, and recoverable-wreck-build state persists directly.
