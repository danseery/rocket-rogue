# Mini-Drone System

This system follows [AGENT_DESIGN_CONTEXT.md](AGENT_DESIGN_CONTEXT.md). Treat USG Notes as the primary direction: drones should support exploration, excavation, logistics, endurance, engineering, and later passive combat rather than feeling like generic stat pets.

## Role In The Loop

Drone Bay is a permanent surface-tech unlock. Once researched, Surface Ops exposes a Drone Ops screen before mining. The player equips helper drones into a limited number of bay slots, then enters the mining mini-game with that loadout active.

Drone choices should be readable and chunky:

- Mining Drone: auto-mines nearby revealed ore pockets and adds a passive material trickle.
- Resource Drone: carries reserve oxygen/fuel and lowers extraction strain.
- Survey Drone: widens scanner pulses and helps identify silhouettes through fog of war.
- Stabilizer Drone: reduces hard-rock bounce, drill chatter, and drill integrity loss.
- Attack Drone: post-solar passive fire against hostile creatures.
- Defense Drone: post-solar shielding/armor for hostile and environmental damage.

## Slot Progression

Drone Bay starts with 1 slot and can grow to 6 slots. Slot upgrades are material-driven, so mining success feeds back into more mining build variety.

- Slot 2: common materials.
- Slot 3: common + rare materials.
- Slot 4: rare-heavy.
- Slot 5: rare + exotic.
- Slot 6: exotic-heavy capstone.

Slots persist across expeditions. Equipped loadouts can be changed before a mining run.

## Current Prototype Slice

The current implementation keeps the first pass intentionally modest:

- Drone Bay Program research unlocks Drone Ops.
- Unlocking Drone Bay seeds the four environmental drones: Mining, Resource, Survey, Stabilizer.
- Attack and Defense drones remain locked behind the post-solar Perimeter Drone unlock.
- Mining HUD summarizes active drone support.
- Equipped drones affect mining stats and surface extraction/contact risk.

No new art is required yet. Card and icon placeholders are acceptable until dedicated drone sprites exist.

## Future Hooks

Future passes can add drone rarity, drone upgrades, drone repair, mini-drone visuals in the mining scene, and enemy-facing behavior once planets beyond the solar system introduce hostile encounters.
