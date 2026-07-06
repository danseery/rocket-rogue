# Mini-Drone System

This system follows [AGENT_DESIGN_CONTEXT.md](AGENT_DESIGN_CONTEXT.md). Treat USG Notes as the primary direction: drones should support exploration, excavation, logistics, endurance, engineering, and post-solar passive combat rather than feeling like generic stat pets.

## Role In The Loop

Drone Bay is a permanent surface-tech unlock. Once researched, Surface Ops exposes a Drone Ops screen before mining. The player equips helper drones into a limited number of bay slots, then enters the mining mini-game with that loadout active.

Drone choices should be readable and chunky:

- Mining Drone: auto-mines nearby revealed ore pockets and adds a passive material trickle.
- Resource Drone: carries reserve oxygen/fuel and lowers extraction strain.
- Survey Drone: widens scanner pulses and helps identify silhouettes through fog of war.
- Stabilizer Drone: reduces hard-rock bounce, drill chatter, and drill integrity loss.
- Attack Drone: post-solar passive fire, area control, and enemy slowdown.
- Defense Drone: post-solar shielding, reactive armor, and hostile/environmental damage relief.

Drone Ops is the buildcraft surface. The player does not aim weapons during mining; equipped drones fight, shield, scan, and support automatically while the player mines, tethers artifacts, and decides when to extract.

## Slot Progression

Drone Bay starts with 1 slot and can grow to 6 slots. Slot upgrades are material-driven, so mining success feeds back into more mining build variety.

- Slot 2: common materials.
- Slot 3: common + rare materials.
- Slot 4: rare-heavy.
- Slot 5: rare + exotic.
- Slot 6: exotic-heavy capstone.

Slots persist across expeditions. Equipped loadouts can be changed before a mining run.

Each owned drone can also be tuned from Mk I to Mk III with materials. Tuning scales that drone's passive stats while equipped, so the player can commit materials to a preferred build instead of only expanding slot count.

Drone Ops should present this as a build table, not a hidden ruleset: the active build strip summarizes the equipped loadout, the build guidance strip names the closest next recipe and tuning priority, the loadout bench shows filled/open/locked bay slots, the combat forecast shows the next run's passive swarm profile, the recipe board shows pair/signature requirements and missing roles, and each drone card shows the synergies it helps unlock.

## Current Prototype Slice

The current implementation supports persistent Drone Ops loadouts plus passive hostile-mining combat:

- Drone Bay Program research unlocks Drone Ops.
- Unlocking Drone Bay seeds the four environmental drones: Mining, Resource, Survey, Stabilizer.
- Attack and Defense drones remain locked behind the post-solar Perimeter Drone Network unlock.
- Mining HUD summarizes active drone support, active synergies, rig health, threat roles, bullet colors, damage text, and the current build signature.
- Equipped drones affect mining stats, surface extraction/contact risk, oxygen, scanner reach, passive ore trickle, drill stability, and hostile-system passive defense.
- Pair synergies add named build payoffs such as Targeting Grid, Killbox Screen, Excavation Barrage, Bulwark Harness, Long Haul Rig, and Pathfinder Loop.
- Three-role signature builds such as Sentry Killbox, Excavation Storm, Fortress Rig, Relic Pathfinder, and Full Spectrum Swarm make slot expansion feel like a build decision rather than only a stat increase.
- Mk tuning gives individual favorite drones stronger output, shields, scanner reach, oxygen support, or combat pressure without adding manual combat controls.
- The Drone Ops build guidance strip keeps buildcraft actionable by showing the closest inactive recipe, missing roles, next tuning target, and run posture for the current loadout.
- The Drone Ops loadout bench makes the six-slot build shape explicit, showing equipped drones with Mk level and role, open slots ready for a swap, and locked slots that need bay upgrades.
- The Drone Ops combat forecast previews passive shot cadence, volley size, crit chance, sentry output, field pulses, shield relief, counter-hits, slows, and auto-mining so loadout changes feel tactical before the run starts.
- Drone cards preview the next tuning payoff and material cost, so upgrading a preferred drone reads as a build choice instead of a blind stat purchase.
- The Drone Ops recipe board marks active recipes and calls out missing roles, giving players a clear reason to expand slots or swap drones before a hostile mining run.
- During mining, the Swarm command strip keeps the chosen build visible with active build name, threat count, allied/enemy shot counts, crit chance, volley size, shield relief, defeated enemies, drone damage, counter-hit damage, shield absorption, rig damage, and color rules.
- Hostile mining renders allied blue/cyan projectiles, enemy red/orange or elemental projectiles, shield arcs, rig health bars, and floating damage/crit text.
- Equipped mini-drones orbit the rig with role-specific colors and compact role marks, while Mk II/III drones carry small gold tuning rails so the chosen loadout remains visible during mining without placeholder halos.
- Signature builds add distinct in-world rig brackets, rails, and style-specific marks, so full builds read differently from single-drone support without large circular auras.
- Enemy intent should be readable before impact: melee threats show close-range windup slashes toward the rig, while ranged threats show reticles that brighten as their next shot comes off cooldown.
- Drone kills pop distinct `DOWN` text and compact material reward callouts, so passive combat payoffs are visible without pulling the player out of mining.

Dedicated drone sprites can still replace placeholders later, but the current scene should already make ownership, enemy roles, bullets, crits, and rig health readable.

## Future Hooks

Future passes can add branching per-drone upgrade trees, drone repair, rarity-specific visual treatments, and more signature-specific effects. Keep combat passive and post-solar: the fun should come from choosing and upgrading the drone build before the run, then mining under pressure while that build executes automatically.
