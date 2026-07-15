# Mining Lock-and-Key Sites

This is the implementation contract for deterministic artifact-site gates. `MiningProgression.cpp` is authoritative; campaign forecasts, Arena Lab, generation, runtime checks, saves, and tests consume the same typed rules.

## Progression Contract

| Gate | First legal arena | Direct key | Systemic alternatives |
|---|---:|---|---|
| Hazard Cocoon | Act 1 L8 | Hazard Mk I; Toxic uses Mk II; Radiation uses Mk III | None for story cocoons. Every marked shell tile must be treated. |
| Enemy-Sealed Chamber | Act 2 L2 | Passive combat strength | Any Attack, Defense, terrain, and utility combination that clears the assigned group and spawner descendants. |
| Survey Triangulation | Act 1 L8 | Survey support | Move the rig between all marked origins and spend scanner pulses. |
| Fragile Excavation | Act 1 L8 | Mining support and drill control | Excavate around the cache, scan first, and minimize rebound. Story progress survives damage; condition changes secondary value. |
| Heavy Tow | Act 1 L9 | Resource support or tow upgrades | Empty cargo and a wide, direct return tunnel. |
| Endurance Vault | Act 1 L9 | Resource support or oxygen upgrades | Shortcuts, disciplined fuel use, and route pre-clearance. |
| Shield Corridor | Act 2 L4 | Defense or Attack support | Terrain cover and an alternate extraction tunnel. |
| Burrow Breach | Act 3 L1 | Lure a Mammal through marked bedrock | Survey the longer natural route. The site replenishes its burrower until opened. |
| Compound Story Vault | Act 2 L7 | Hazard treatment plus assigned encounter clearance | Act 3 capstones add endurance and heavy towing as a third lock. |

Act 1 uses at most one lock, Act 2 uses at most two, and Act 3 uses at most three. A gate is legal only after its component mechanics and enemy families are available. Act 2 never selects Burrow Breach or Radiation.

## Architecture

- `MiningArenaRequest` carries Act, level, seed, and an optional gate override. Arena Lab offers Default rules, None, and every specific gate.
- `MiningArenaRules` declares legal gates, the fixed story gate, and maximum lock count alongside mechanics, roster, scaling, and reward budgets.
- `MiningGateDefinition` is the immutable contract: components, Hazard mark/affinity, player-facing key, and alternatives.
- `MiningGateRuntime` owns discovery, shell cells, assigned enemies, scan markers, open/completed state, artifact identity, and soft-lock modifiers.
- `MiningCapabilityProfile` derives role marks and rig capability from equipped drones, upgrades, crew, and ship. It is forecast-only and never changes arena difficulty.
- Gate-associated cells and enemies serialize with the active arena. `MiningStorySiteProgress` stores destination, Act/level, seed, gate, artifact identity, discovery, and completion in `MetaProgress`.

Fixed story sites reuse their saved request and artifact identity until the artifact is delivered, banked, and survives Surface extraction. Abort, rig loss, artifact destruction, emergency recall, and rough Surface extraction do not complete the site. Completion is credited by artifact identity, not merely by Story kind.

## Runtime Rules

- Hazard shell cells reject drill damage. Hazard treatment preserves gate association; refined minerals still use the unified rich-reward ledger.
- Hard-locked caches reject drill and tether bypass until every required component completes.
- Enemy seals count only gate-associated enemies. Children from an associated spawner inherit the association.
- Triangulation origins are visible world markers. Survey drones expand efficient coverage; manual repositioning remains valid.
- Heavy Tow increases tether cargo burden and reduces tether response. Endurance Vault uses a deep/far anchor. Shield Corridor stamps a ranged extraction lane.
- Burrow bedrock is immune to the rig but accepts Mammal burrow damage. A replacement Mammal spawns while the breach is closed.
- Story artifacts do not spend rare/exotic mineral budget. Mineral cells, enemy drops, and Hazard refinement still share the arena ledger.

## Player and Debug Communication

Surface Ops and Drone Ops show the upcoming gate, required capability, current loadout readiness, and alternatives. The mining HUD shows gate type/state. Gate cells pulse, locked artifacts use a seal ring, and triangulation origins use crosshair markers.

Arena Lab includes a gate override and prints the gate, requirement, and alternatives before launch. Debug HUD retains Act, level, seed, ruleset, and gate metadata. Debug runs remain save-ineligible.

## Verification

`mining_progression_tests` table-tests introductions and Act restrictions, Hazard mark escalation, illegal overrides, capability forecasts, story identity persistence, extraction credit, hard-lock behavior, assigned enemy clearance, and save/load. Existing core and economy suites cover artifact physics, rich caps, drone behavior, deterministic terrain, and campaign regression.
