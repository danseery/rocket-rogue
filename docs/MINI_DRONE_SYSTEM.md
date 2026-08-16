# Support Drone System

The filename and `MiniDrone*` C++ symbols are retained as legacy internal identifiers. Player-facing copy and current design terminology use Support Drone.

Support Drone roles also act as forecastable keys for protected-objective sites; see [MINING_LOCK_AND_KEY_SITES.md](MINING_LOCK_AND_KEY_SITES.md) and [SCENARIO_FRAMEWORK.md](SCENARIO_FRAMEWORK.md). Capability checks communicate readiness but never rubber-band arena difficulty.

This system follows [AGENT_DESIGN_CONTEXT.md](AGENT_DESIGN_CONTEXT.md). Treat USG Notes as the primary direction: Support Drones should enable exploration, excavation, logistics, endurance, engineering, and post-solar autonomous combat rather than feeling like generic stat pets. *Solar Jetman* is the mechanical touchstone for gravity, inertia, towing burden, and the difference between vehicle and pilot roles; OREBIT's transferable support swarm is a deliberate modernization beyond that reference.

## Role In The Loop

Drone Bay begins as the payoff for the lunar mining contract. Safely delivering 30 Moon Common Ore creates a ready-to-claim Prospector milestone; `Install Prospector Mk I` permanently owns and equips the first Prospector Support Drone (Mining role) in Slot 1. Mars then reserves 40 local Common Ore for an explicit `Fabricate Slot 2` action, leaving that slot empty. The extended Mars contract makes repeated oxygen, heat, integrity, repair, cargo, and return decisions part of the early loop. Io later commissions the first Hazard Support Drone.

Support Drone choices should be readable and chunky:

- Mining Support Drone: acquires a nearby revealed tile around the controlled actor, flies to it, physically drills it, and returns immediately when its hard leash or an anchor transfer fires.
- Resource Support Drone: collects loose chunks around the controlled actor and carries them until a valid shuttle-delivery window or rig collection is available.
- Survey Support Drone: scouts deeper than the controlled actor. Scanner pulses reveal from both the actor and the Survey Support Drone's position.
- Hazard Support Drone: flies directly through terrain to revealed environmental pockets and converts them into safe mineable terrain before the actor remains in their contact envelope. It never reveals or targets hidden cells.
- Attack Support Drone: acquires threats relative to the controlled actor, fires from its own position, and returns to the actor's orbit before engaging again.
- Defense Support Drone: orbits on the threat-facing side and intercepts incoming damage for the controlled actor before remaining damage reaches rig health or suit integrity.

Drone Ops is the buildcraft surface. Equipped Support Drones fight, shield, scan, haul, and support autonomously while the player controls either the Mining Rig or the EVA operator. The operator's sidearm adds direct self-defense without changing the swarm's autonomous targeting contract.

## Transferable Active-Actor Anchor

Support Drones belong to the player rather than the Mining Rig. Each `MiningMiniDroneAgent` stores `MiningAnchorTarget { ControlledActor, Rig, Operator }`; equipped units default to `ControlledActor`. Explicit `Rig` and `Operator` bindings exist for future scripted behavior and are not a player-facing setting.

Every fixed update resolves that binding through `resolveMiniDroneAnchor` into a transient `MiniDroneAnchorFrame` containing actor identity, position, velocity, facing, collider, depth, and validity. Home points, leashes, task searches, target scoring, field of view, area control, shield arcs, scanner origins, and collision clearance consume that frame. Support Drone coordinators and presentation effects must not read rig coordinates directly.

`transferMiniDroneSwarmAnchor` handles normal rig/EVA switching and emergency ejection:

- `ControlledActor` immediately resolves to the newly controlled actor.
- Cell assignments, layer-local enemy targets, and combat visuals are released.
- World position, velocity, haul, shield charge, recharge state, cooldowns, stable formation slot, and orbit phase are preserved.
- Noncombat roles enter `Returning`; Attack and Defense roles reacquire around the new anchor on their next update.
- Same-layer transfers never transform, teleport, or rotate the independent Support Drone entities.
- Cross-depth transfers recreate deterministic formation positions around the new anchor while preserving haul, shield state, cooldowns, and orbit phase.
- A parked or destroyed rig is never selected as the return target for a `ControlledActor` Support Drone.

Following and defense outrank finishing remote work. Mining, Resource, and Survey Support Drones make collision-aware bounded sorties, and a hard-leash or anchor-transfer event recalls them immediately. Hazard Support Drones instead cross terrain directly to valid revealed work and return directly to formation when no work remains. Shuttle unloading is a temporary task destination, never a parent transfer: a hauling Mining or Resource Support Drone detaches to unload only when the active actor is on the entry layer and near the shuttle. Otherwise it retains its haul and follows.

Idle and returning Support Drones continuously orbit at `0.45` radians per second. Role rings are Mining `1.6` cells, Resource `2.05`, Hazard `1.9`, Defense `2.7`, Attack `3.35`, and Survey `3.4`. Each equipped frame retains a stable formation slot; alternating rings counter-rotate. Defense rings reorient toward the closest threat.

Orbit centers lead the anchor by `0.18` seconds of its velocity. Support Drones return at `5.6` cells per second and use `8.5` cells per second catch-up speed when more than `4.5` cells behind, which lets them keep pace with the faster Mining Rig. A blocked orbit goal projects radially toward the anchor until a valid point is found. Mining, Resource, Survey, Attack, and Defense Support Drone colliders admit suit-only passages and avoid terrain. Hazard Support Drones deliberately ignore terrain collision, use distinct target-approach points, and show a cyan transit shimmer while crossing solid cells.

Artifact tether ownership is separate from swarm anchoring. Anchor transfer never attaches, releases, or modifies an artifact, and Support Drones do not collide with the tether line.

## Slot Progression

Claiming Prospector Mk I opens Drone Bay Slot 1; the bay can then grow to 6 slots. Slot upgrades are material-driven, so mining success feeds back into more mining build variety.

- Slot 2: common materials.
- Slot 3: common + rare materials.
- Slot 4: rare-heavy.
- Slot 5: rare + exotic.
- Slot 6: exotic-heavy capstone.

Slots persist across expeditions. Equipped loadouts can be changed before a mining run. Each unlocked Support Drone type starts with one owned frame; an open slot can use that spare frame or fabricate a paid duplicate. Duplicate costs scale by rarity: Common `20 Common`, Uncommon `30 Common`, Rare `40 Common + 1 Rare`, Prototype `60 Common + 2 Rare`. New slots remain visibly empty until the player assigns or builds a frame.

Each Support Drone type can also be tuned from Mk I to Mk III with materials. Tuning applies to every owned copy of that type, so a duplicate is a capacity and specialization decision rather than a free separate upgrade track.

Drone Ops should present this as a build table, not a hidden ruleset: the active build strip summarizes the equipped Support Drone loadout, the build guidance strip names the closest next recipe and tuning priority, the loadout bench shows filled/open/locked bay slots, the combat forecast shows the next run's autonomous swarm profile, the recipe board shows pair/signature requirements and missing roles, and each unit card shows the synergies it helps unlock.

## Current Prototype Slice

The current implementation supports persistent Support Drone loadouts, transferable active-actor support, and hostile-mining swarm combat:

- The Prospector contract unlocks Drone Ops and grants the first Prospector Support Drone.
- Drone Support Program research adds the Resource and Survey Support Drones; Io separately commissions the first Hazard Support Drone Mk I.
- Arkfall grants Mk I Attack and Defense Support Drones and raises undersized bays to three slots without erasing stronger equipment.
- Perimeter Drone Network research grants Perimeter Coordination, which gates Mk II/Mk III combat tuning and advanced combat synergies.
- Mining HUD summarizes active Support Drone coverage, active synergies, anchor status, rig health or suit integrity, threat roles, bullet colors, damage text, and the current build signature.
- Equipped Support Drones affect mining stats, surface extraction/contact risk, oxygen, scanner reach, physical excavation, hazard remediation, and hostile-system autonomous defense.
- Pair synergies add named build payoffs such as Targeting Grid, Killbox Screen, Excavation Barrage, Containment Screen, Long Haul Rig, and Pathfinder Loop.
- Three-role signature builds such as Sentry Killbox, Excavation Storm, Containment Rig, Relic Pathfinder, and Full Spectrum Swarm make slot expansion feel like a build decision rather than only a stat increase.
- Mk tuning gives individual favorite Support Drones stronger output, shields, scanner reach, oxygen support, or combat pressure without adding manual combat controls.
- Drone Ops controls equip or unequip owned Support Drone frames, fabricate paid duplicates into open slots, and upgrade a type's tuning. A recovered Io minor artifact contributes a persistent `FREE UPGRADE ×1` credit that the player explicitly applies to any eligible owned Mk I/Mk II Support Drone type.
- The Drone Ops build guidance strip keeps buildcraft actionable by showing the closest inactive recipe, missing roles, next tuning target, and run posture for the current loadout.
- The Drone Ops loadout bench makes the six-slot build shape explicit, showing equipped frames with Mk level and role, open slots ready for an owned frame or a paid duplicate, and locked slots that need bay upgrades.
- The Drone Ops combat forecast previews autonomous shot cadence, volley size, crit chance, sentry output, field pulses, shield relief, counter-hits, slows, and auto-mining so loadout changes feel tactical before the run starts.
- Support Drone cards preview the next tuning payoff and material cost, so upgrading a preferred unit reads as a build choice instead of a blind stat purchase.
- The Drone Ops recipe board marks active recipes and calls out missing roles, giving players a clear reason to expand slots or swap drones before a hostile mining run.
- During mining, the Swarm command strip keeps the chosen build visible with active build name, current anchor, threat count, allied/enemy shot counts, crit chance, volley size, shield relief, defeated enemies, Support Drone damage, counter-hit damage, shield absorption, and damage routed to the rig or suit.
- Hostile mining renders compact enemy silhouettes, allied blue/cyan projectiles, enemy red/orange or elemental projectiles, directional shield barriers during incoming fire, rig health bars, and floating damage/crit text without persistent aura disks.
- Every equipped Support Drone is an independent saved simulation agent with its own world position, velocity, behavior, target, cooldown, anchor target, stable formation slot, and orbit phase. Logical parenting resolves a home frame but never directly transforms a Support Drone sprite.
- Mining, Hazard, and Attack Support Drones travel to world targets and brake smoothly as they settle into a task. Defense units guard the active actor's threat-facing side, Survey units scout deeper, and Resource units collect spatial loose chunks nearby. Mining units show compact chip-and-spark effects while drilling; Hazard units cross terrain with a cyan transit shimmer and project one affinity-colored treatment beam per working unit. Their sprites stay upright when the controlled actor turns.
- Mk II and Mk III Support Drones carry one or two attached gold rank lights on the unit body. Upgrade state should never appear as a detached rail or unexplained line.
- Pair synergies and signature builds deploy from the active actor into role behavior. After deployment, the build reads through each Support Drone's movement and contextual activity plus the named Swarm command strip instead of glows, brackets, or rails around the actor.
- Scanner pulses use a brief grid and compact prospect pips without persistent concentric rings; partial terrain-edge rails should not sit around the actor during normal mining. Thruster trails appear only while moving, and the actor-health gauge appears only during combat or after damage.
- Enemy intent should be readable before impact: melee threats show close-range windup slashes toward the active actor, while ranged threats show converging aim chevrons that brighten as their next shot comes off cooldown.
- Support Drone kills pop distinct `DOWN` text and compact material reward callouts. Suit-killed enemy rewards become loose chunks that the rig or Mining/Resource Support Drones must collect.

Dedicated Mining, Resource, Survey, Hazard, Attack, and Defense Support Drone sprites make each independent agent readable while ownership, enemy roles, bullets, crits, and rig health remain visible.

## Hazard Support Drone Ladder

Hazard treatment is a spatial world task rather than an always-on rig buff. `MiningCell::revealed` is the hard targeting gate: line-of-sight discovery, the Mining Rig scanner, and Survey Support Drone pulses can make work eligible, while Hazard Support Drones never reveal terrain themselves. Revealed active cocoon-layer segments are valid across the active arena and outrank ordinary hazards; ordinary hazards remain limited to the `8.35`-cell operating radius and are ordered closest to the controlled actor, then by intensity and stable terrain index. Hidden later cocoon layers remain ineligible until their authored prerequisites are met.

The Hazard squad assigns all equipped frames together. Drones split across distinct eligible targets when possible; surplus frames assist the highest-priority target from separate approach positions. Shared treatment advances once per target at an exact linear rate—two active workers provide `2x`, three provide `3x`—and all workers reacquire on the first update after conversion. Stable role indices retain identity and partial treatment ownership across saves.

| Mk | Eligible conditions | Treatment | Batch | Refinement |
| --- | --- | --- | --- | --- |
| I | Thermal and Cryo | 1.5s | 1 tile | 5% |
| II | Adds Toxic | 1.125s | 2 adjacent tiles | 8% |
| III | Adds Radiation | 0.75s | 3 adjacent tiles | 12% |

Standard Hazard Support Drone treatment converts a hazard pocket to revealed regolith. A deterministic refinement roll instead converts Thermal or Cryo to common ore, Toxic to rare ore, or Radiation to an exotic vein. The resulting tile still has to be mined and collected.

Io is the current deliberate exception and tutorial: its soil never pays, it generates no direct ore deposits or Cryo pockets, and every mineable Thermal lava seam deterministically cools into gray Common Ore. Its authored layered-recovery site uses two staged four-segment lava layers and a 60-second oxygen budget before its protected Artifact can be towed and safely extracted. The Hazard squad's generic cocoon priority works for any revealed active layer; it is not specific to this site.

## Future Hooks

Save version 11 retains version 10's paid-frame, anchor, formation, position, haul, shield, recharge, cooldown, scenario, generic-cocoon, and launch-curriculum context while adding Arrival Ops transfer fuel and Surface Ops rig fuel. Older units migrate to `ControlledActor`, derive stable slots from equipped order and deterministic phases, and repeated pre-v8 loadout IDs are de-duplicated once.

Future passes can add branching per-unit upgrade trees, Support Drone repair, rarity-specific visual treatments, and more signature-specific effects. Keep enemy combat post-solar and swarm execution autonomous: the EVA sidearm protects the vulnerable operator, while buildcraft remains the source of sustained combat strength.
