# Mining and Combat Progression Contract

Artifact-site lock progression is defined in [MINING_LOCK_AND_KEY_SITES.md](MINING_LOCK_AND_KEY_SITES.md). Gate selection is part of the same Act/level rules request and never changes difficulty from the player's actual loadout.

Status: authoritative implementation contract for mining progression, procedural arenas, combat pacing, and rich-material availability.

This document defines which rules an arena may use. `resolveMiningArenaRules({act, difficulty, seed})` in `src/core/MiningProgression.cpp` is the executable source of truth. Campaign and debug entry points must both create a `MiningArenaRequest` and must not add enemies, rewards, hazards, or room features after resolving it.

## Player-facing progression

Each act has ten difficulty levels split into four teaching bands:

| Band | Levels | Purpose |
|---|---:|---|
| Learn | 1-3 | Introduce a small rule set in isolation. |
| Combine | 4-6 | Combine learned rules and expose the first meaningful build choices. |
| Pressure | 7-8 | Add counters and overlapping pressures. |
| Mastery | 9-10 | Test the act's complete rule set before the next act changes the problem. |

A tutorial callout appears at a band transition, not at every number. Combat remains passive: drones select and attack targets while the player pilots, drills, scans, manages endurance, and chooses routes. Mining remains the single fuel-only deployment opened by Survey Site or Push Deeper in a surface loop.

### Act 1: learn the mining rig

Act 1 never creates enemies or exotic minerals.

| Level | New rules introduced | Reference capability |
|---:|---|---|
| 1 | Open main route, movement, drilling, return zone, Regolith, Common Ore | Main rig |
| 2 | Fog/scanner and oxygen/shared-fuel endurance | Main rig |
| 3 | Hard Rock and branch routes | Main rig |
| 4 | Drill heat, integrity, rebound, field repairs, first Rare Ore | Mining/Resource Mk I, up to 2 slots |
| 5 | Cargo drag | Mining/Resource Mk I, up to 2 slots |
| 6 | Combine all heat, damage, cargo, and route decisions | Mining/Resource Mk I, up to 2 slots |
| 7 | Thermal and Cryo terrain pockets; site/depth variation | Survey/Hazard Mk I, up to 3 slots |
| 8 | Physical artifacts, recovery, and tethering | Survey/Hazard Mk I, up to 3 slots |
| 9 | Toxic terrain pockets | Environmental drones up to Mk II, up to 4 slots |
| 10 | Complete noncombat combinations | Environmental drones up to Mk II, up to 4 slots |

### Act 2: learn passive drone combat

Act 2 carries forward the complete Act 1 mining rules. It never creates Mammals, Radiation affinity, or Boss Chambers.

| Level | New rules introduced | Reference capability |
|---:|---|---|
| 1 | Ant melee contact and simple Encounter Zones | Attack + Defense Mk I, 3 slots minimum |
| 2 | More Ant positioning within the Learn cap | Attack + Defense Mk I, 3 slots minimum |
| 3 | Ant pressure combined with excavation decisions | Attack + Defense Mk I, 3 slots minimum |
| 4 | Flying ranged enemies and Treasure Vault routes | Attack + Defense + one utility role |
| 5 | Armored Beetles | Attack + Defense + one utility role |
| 6 | Ranged, armored, and melee combinations | Attack + Defense + one utility role |
| 7 | Thermal/Cryo Elementals, Hive Nests, first possible Exotic Vein | Combat/Hazard Mk II, up to 4 slots |
| 8 | Elemental and hive pressure combinations | Combat/Hazard Mk II, up to 4 slots |
| 9 | Toxic Elementals and Miniboss Lairs | Focused Mk II signature build, up to 5 slots |
| 10 | One reinforcement Spawner | Focused Mk II signature build, up to 5 slots |

### Act 3: overturn solved combat builds

Act 3 builds on the complete Act 2 roster, but its Learn band defers spawners and minibosses so Mammals and Radiation can be read separately. Reinforcement and miniboss pressure return in Combine.

| Level | New rules introduced | Reference capability |
|---:|---|---|
| 1 | Mammal burrowers and Organic Burrows | Hazard Mk III + Defense, 5 slots |
| 2 | Radiation affinity | Hazard Mk III + Defense, 5 slots |
| 3 | Mammals and Radiation combine after separate introductions | Hazard Mk III + Defense, 5 slots |
| 4 | Mixed affinities, spawners, and miniboss lanes | Mixed Mk II/Mk III signature build, 5 slots |
| 5 | Overlapping attack lanes | Mixed Mk II/Mk III signature build, 5 slots |
| 6 | Complete Combine-band formations | Mixed Mk II/Mk III signature build, 5 slots |
| 7 | Boss Chambers | Mk III specialization, up to 6 slots |
| 8 | Bosses plus combined spawner pressure | Mk III specialization, up to 6 slots |
| 9 | Multiple spawners and full-roster combinations | Full Spectrum or focused equivalent, 6 slots |
| 10 | Maximum full-spectrum pressure | Full Spectrum or focused equivalent, 6 slots |

## Difficulty and reward tables

Terrain toughness is derived only from act and difficulty; generation must not also multiply it by raw depth:

- Act 1: `0.75 + 0.05 * (difficulty - 1)`
- Act 2: `1.10 + 0.06 * (difficulty - 1)`
- Act 3: `1.45 + 0.07 * (difficulty - 1)`

Enemy movement speed remains archetype-defined. Health and damage use:

- Act 2 health: `0.70 + 0.08 * (difficulty - 1)`; damage: `0.65 + 0.07 * (difficulty - 1)`
- Act 3 health: `1.25 + 0.10 * (difficulty - 1)`; damage: `1.10 + 0.08 * (difficulty - 1)`

Active-enemy caps by Learn/Combine/Pressure/Mastery are `2/4/6/8` in Act 2 and `6/8/11/14` in Act 3. Act 2 level 10 permits one spawner. Act 3 levels 1-3 permit none, levels 4-8 permit one, and levels 9-10 permit two.

Rich-material values are `{first-clear guarantee / hard arena cap}`. All procedural ore, stamped prospects, room deposits, enemy drops, and Hazard Drone refinement share these caps.

| Act | Learn | Combine | Pressure | Mastery |
|---|---|---|---|---|
| 1 Rare | 0/0 | 1/1 | 1/2 | 2/3 |
| 1 Exotic | 0/0 | 0/0 | 0/0 | 0/0 |
| 2 Rare | 1/2 | 2/3 | 2/4 | 3/5 |
| 2 Exotic | 0/0 | 0/0 | 0/1 | 1/1 |
| 3 Rare | 3/5 | 4/6 | 5/7 | 6/8 |
| 3 Exotic | 1/1 | 1/2 | 2/3 | 3/4 |

The arena reward ledger must reserve or consume from one shared budget before creating any rich reward. Once a cap is committed, additional enemy rewards become common salvage. A guarantee is credited only when that material reaches the banked surface payload. Unbanked, dropped, or lost material does not advance first-clear progress.

First-clear progress is stored per act/band in `MetaProgress::miningFirstClearProgress`. Until both guarantees for the band are banked, later attempts retain the guarantee. After fulfillment, `effectiveMiningRewardBudget` removes guarantees, halves the Rare cap rounding up, and halves the Exotic cap rounding down.

## Campaign mapping and deterministic arenas

`resolveCampaignMiningProgression` maps campaign state into an act, allowed range, and current difficulty. Surface depth adds one difficulty per Push Deeper step and clamps inside the chapter range.

| Campaign chapter | Mining range |
|---|---|
| 1 - Proving Ground | Unavailable |
| 2 - Lunar Program / Moon | Act 1, levels 1-3 |
| 3 - Red Frontier / Mars | Act 1, levels 4-6 |
| 4 - Breakthrough / Outer Planets | Act 1, levels 7-8 |
| 5 - Straylight / Aaru Vale | Act 1, levels 9-10 |
| 6 - Arkfall / first Khepri sorties | Act 2, levels 1-3 |
| 7 - Last Campfire / Khepri survival | Act 2, levels 4-10 |
| 8 - Void Compass / Rift Belt | Act 3, levels 1-4 |
| 9 - Ouroboros | Act 3, levels 5-8 |
| 10 - Ascent | Act 3, levels 9-10 |

In Chapter 7 the base is level 4 after the first successful hostile sortie, level 5 after the second, and level 6 after the third. Surface depth then adds up to four levels, capped at 10.

`deriveMiningArenaSeed` combines the campaign seed, destination ID, landing ordinal, and surface depth. Identical inputs reproduce the same arena. A new landing ordinal or Randomize seed produces another deterministic arena. Difficulty never reads the equipped loadout, so upgrades cannot cause enemy rubber-banding.

## Core API and persistence

The stable shared types live in `GameTypes.h`:

- `MiningAct`, `MiningProgressionBand`
- `MiningArenaRequest { act, difficulty, seed }`
- `MiningRewardBudget { rareGuarantee, exoticGuarantee, rareCap, exoticCap }`
- `MiningArenaRules`, including mechanic gates, material/enemy/affinity/room whitelists, encounter limits, scaling, copy, and reference drones
- `MiningArenaMetadata { act, difficulty, seed, rulesVersion }`
- `MiningFirstClearProgress { rareBanked, exoticBanked }`

The stable resolver and query API lives in `MiningProgression.h`. Consumers should use the whitelist helpers instead of indexing the fixed arrays directly.

Active saves persist arena metadata under `miningArenaMetadata`; this metadata identifies the rules that produced serialized terrain and enemies. Restore must never reroll serialized terrain. If an active legacy arena has no metadata, save restoration derives metadata from its chapter, destination, surface depth, campaign seed, landing history, and hostile successes, then leaves the existing arena intact. Legacy saves default every first-clear record to zero, so no guarantee is silently marked complete.

The current `miningArenaRulesVersion` is `1`. Increment it only when a rule change can alter deterministic generation or reward allocation, and preserve old serialized active arenas during migration.

## Integration invariants

- Campaign and debug paths call the same resolver and initialization path.
- The Arena Lab may select Act 1-3, difficulty 1-10, and a seed; its preview reads `MiningArenaRules` directly.
- Surface threat forecasting reads the same enemy roster and caps used by mining generation.
- Debug arenas never write campaign save data or first-clear progress.
- Act 1 has no enemies or Exotic Veins. Act 2 has no Mammals, Radiation, or Boss Chambers.
- Player weapons are out of scope; mining combat remains drone-controlled.
- Baseline mining oxygen is 30 seconds and total upgraded capacity is capped at 120 seconds by mining stat integration.
