# Mini-Drone System

Drone roles also act as forecastable keys for artifact sites; see [MINING_LOCK_AND_KEY_SITES.md](MINING_LOCK_AND_KEY_SITES.md). Capability checks communicate readiness but never rubber-band arena difficulty.

This system follows [AGENT_DESIGN_CONTEXT.md](AGENT_DESIGN_CONTEXT.md). Treat USG Notes as the primary direction: drones should support exploration, excavation, logistics, endurance, engineering, and post-solar passive combat rather than feeling like generic stat pets.

## Role In The Loop

Drone Bay is a permanent surface-tech unlock. Once researched, Surface Ops exposes a Drone Ops screen before mining. The player fills bay slots with helper drone copies, then enters the mining mini-game with that loadout active.

Drone choices should be readable and chunky:

- Mining Drone: acquires a nearby revealed tile, flies to it, and physically drills it. If the main rig moves beyond its leash, it finishes the current tile before returning and waits for the rig to come back into working range.
- Resource Drone: stays close to the main rig to collect and carry material while its reserve oxygen/fuel and extraction relief remain active.
- Survey Drone: scouts deeper than the main rig. Every scanner pulse reveals from both the main rig and each Survey drone position.
- Hazard Drone: acquires revealed environmental pockets, flies to them, and converts them into safe mineable terrain before the main rig remains in their contact envelope.
- Attack Drone: acquires the closest enemy, keeps that target until it is defeated, fires from its own position, and returns to the main rig before engaging again.
- Defense Drone: moves between the main rig and the nearest threat so ranged fire terminates at the Defense drone while its shields, reactive armor, and damage relief absorb the attack.

Drone Ops is the buildcraft surface. The player does not aim weapons during mining; equipped drones fight, shield, scan, and support automatically while the player mines, tethers artifacts, and decides when to extract.

## Slot Progression

Drone Bay starts with 1 slot and can grow to 6 slots. Slot upgrades are material-driven, so mining success feeds back into more mining build variety.

- Slot 2: common materials.
- Slot 3: common + rare materials.
- Slot 4: rare-heavy.
- Slot 5: rare + exotic.
- Slot 6: exotic-heavy capstone.

Slots persist across expeditions. Equipped loadouts can be changed before a mining run. A bay slot can hold another copy of a drone type already in the loadout, so a player can run focused builds such as all Attack drones instead of only one of each role.

Each owned drone type can also be tuned from Mk I to Mk III with materials. Tuning scales every equipped copy of that type, so the player can commit materials to a preferred build instead of only expanding slot count.

Drone Ops should present this as a build table, not a hidden ruleset: the active build strip summarizes the equipped loadout, the build guidance strip names the closest next recipe and tuning priority, the loadout bench shows filled/open/locked bay slots, the combat forecast shows the next run's passive swarm profile, the recipe board shows pair/signature requirements and missing roles, and each drone card shows the synergies it helps unlock.

## Current Prototype Slice

The current implementation supports persistent Drone Ops loadouts plus passive hostile-mining combat:

- Drone Bay Program research unlocks Drone Ops.
- Unlocking Drone Bay seeds the four environmental drones: Mining, Resource, Survey, Hazard.
- Arkfall grants Mk I Attack and Defense drones and raises undersized bays to three slots without erasing stronger equipment.
- Perimeter Drone Network research grants Perimeter Coordination, which gates Mk II/Mk III combat tuning and advanced combat synergies.
- Mining HUD summarizes active drone support, active synergies, rig health, threat roles, bullet colors, damage text, and the current build signature.
- Equipped drones affect mining stats, surface extraction/contact risk, oxygen, scanner reach, physical helper-drone excavation, hazard remediation, and hostile-system passive defense.
- Pair synergies add named build payoffs such as Targeting Grid, Killbox Screen, Excavation Barrage, Containment Screen, Long Haul Rig, and Pathfinder Loop.
- Three-role signature builds such as Sentry Killbox, Excavation Storm, Containment Rig, Relic Pathfinder, and Full Spectrum Swarm make slot expansion feel like a build decision rather than only a stat increase.
- Mk tuning gives individual favorite drones stronger output, shields, scanner reach, oxygen support, or combat pressure without adding manual combat controls.
- Drone controls add another copy of an owned drone type and upgrade that type's tuning. Unequip lives on Drone Loadout slots so removal is explicit per slot.
- The Drone Ops build guidance strip keeps buildcraft actionable by showing the closest inactive recipe, missing roles, next tuning target, and run posture for the current loadout.
- The Drone Ops loadout bench makes the six-slot build shape explicit, showing equipped drone copies with Mk level and role, open slots ready for another copy, and locked slots that need bay upgrades.
- The Drone Ops combat forecast previews passive shot cadence, volley size, crit chance, sentry output, field pulses, shield relief, counter-hits, slows, and auto-mining so loadout changes feel tactical before the run starts.
- Drone cards preview the next tuning payoff and material cost, so upgrading a preferred drone reads as a build choice instead of a blind stat purchase.
- The Drone Ops recipe board marks active recipes and calls out missing roles, giving players a clear reason to expand slots or swap drones before a hostile mining run.
- During mining, the Swarm command strip keeps the chosen build visible with active build name, threat count, allied/enemy shot counts, crit chance, volley size, shield relief, defeated enemies, drone damage, counter-hit damage, shield absorption, rig damage, and color rules.
- Hostile mining renders compact enemy silhouettes, allied blue/cyan projectiles, enemy red/orange or elemental projectiles, directional shield barriers during incoming fire, rig health bars, and floating damage/crit text without persistent aura disks.
- Every equipped mini-drone copy is an independent saved simulation agent with its own world position, velocity, behavior, target, and cooldown. The main rig supplies a moving home point but does not parent or rotate mini-drone sprites.
- Mining, Hazard, and Attack drones travel to world targets and brake smoothly as they settle into a task. Defense drones guard the threat-facing side, Survey drones scout deeper, and Resource drones keep a tight collection follow. Mining drones show compact chip-and-spark effects while drilling; Hazard drones project affinity-colored treatment beams and completion bursts. Their sprites stay upright when the main rig turns.
- Mk II and Mk III drones carry one or two attached gold rank lights on the drone body. Upgrade state should never appear as a detached rail or unexplained line.
- Pair synergies and signature builds deploy directly from the rig into role behavior. After deployment, the build reads through each drone's movement and contextual activity plus the named Swarm command strip instead of glows, brackets, or rails around the rig.
- Scanner pulses use a brief grid and compact prospect pips without concentric rings; partial terrain-edge rails should not sit around the rig during normal mining. Thruster trails appear only while moving, and the in-world rig-health gauge appears only during combat or after damage.
- Enemy intent should be readable before impact: melee threats show close-range windup slashes toward the rig, while ranged threats show converging aim chevrons that brighten as their next shot comes off cooldown.
- Drone kills pop distinct `DOWN` text and compact material reward callouts, so passive combat payoffs are visible without pulling the player out of mining.

Dedicated Mining, Resource, Survey, Hazard, Attack, and Defense sprites make each independent agent readable while ownership, enemy roles, bullets, crits, and rig health remain visible.

## Hazard Drone Ladder

Hazard treatment is an active world task rather than a passive rig buff. The drone only targets revealed pockets within its operating radius, prioritizes the most intense eligible affinity, and coordinates assignments with duplicate Hazard drones.

| Mk | Eligible conditions | Treatment | Batch | Refinement |
| --- | --- | --- | --- | --- |
| I | Thermal and Cryo | 3.0s | 1 tile | 5% |
| II | Adds Toxic | 2.25s | 2 adjacent tiles | 8% |
| III | Adds Radiation | 1.5s | 3 adjacent tiles | 12% |

Normal treatment converts a hazard pocket to revealed regolith. A deterministic refinement roll instead converts Thermal or Cryo to common ore, Toxic to rare ore, or Radiation to an exotic vein. The resulting tile still has to be mined and collected.

## Future Hooks

Future passes can add branching per-drone upgrade trees, drone repair, rarity-specific visual treatments, and more signature-specific effects. Keep combat passive and post-solar: the fun should come from choosing and upgrading the drone build before the run, then mining under pressure while that build executes automatically.
