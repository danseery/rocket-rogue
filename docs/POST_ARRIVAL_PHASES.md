# Post-Arrival Phases

For the latest agent-facing design priority order, see `docs/AGENT_DESIGN_CONTEXT.md`. USG Notes is the primary source for surface, mining, animal class, and Support Drone direction.

This note extends the current launch/refit prototype with the next two layers: research and landed exploration. The goal is to keep the press-your-luck rocket loop as the spine, then make successful arrivals feel like new opportunities instead of just a bigger payout screen.

## Phase Structure

The launch curriculum begins Moon-bound: Fuel Survey, Flight Controls Calibration, and the first true transfer all happen before surface play. Earth Orbit is only a hidden save-compatibility origin. After arrival, the Moon adds the first explicit surface objective: safely deliver 30 lunar Common Ore and claim Prospector Mk I plus Drone Bay Slot 1.

Every successful surface-destination arrival opens a committed approach decision:

1. Arrival summary: confirms the transfer succeeded and shows what the agency can investigate.
2. Approach commitment: Pass Through ends the visit; Orbit Capture opens mapped landing or science departure; Direct Descent starts Surface Ops with `+0.20` hazard.
3. Surface expedition after landing: spend action kits to survey/push/extract and arrival-derived rig fuel to deploy the player-controlled Mining Rig.
4. Recovery decision: return with the payload before hazard, cargo weight, low action kits, or low fuel makes extraction too risky.
5. Refit window: use credits, Research Data families, and recovered materials to improve the next launch cycle.

This gives Mars a distinct role: it is where the game stops being only "can we get there?" and starts asking "what do we dare do now that we made it?"

Arrival choices explain their exact progression value before commitment. Good or Perfect Pass Through banks one Research Data plus credits and closes Orbit/Landing; Perfect also stores `1.5-3.0` powered-fuel savings and up to `+40%` velocity from its actual finish speed for the next launch. It does not enlarge the tank. Orbit banks its tiered Research Data and credits, removes the unmapped descent penalty for that visit, and then offers Land or Depart. Misses and aborts do not commit. Campaign-critical mining beats use mandatory saved briefings and explicit claim actions. Moon, Mars, and Io may be skipped for a visit, but their objective remains incomplete and the authored onward route stays locked.

## Jupiter Transfer Window

The Mars Bay Expansion opens `THE JUPITER WINDOW`, an informational saved beat rather than a branch selection. Jupiter needs five fuel of calibrated transfer margin. The player may create it permanently with Fuel Tanks III, physically take it from Mars gravity with a Good-or-better departure Flyby, or stack both. Reviewing one option never locks, hides, or disables the other.

At calibrated `0.60` throttle, Jupiter's route burn is `20`. Fuel Tanks III costs `92` credits and raises permanent capacity from `20` to `25`. The Mars slingshot requires Good or Perfect, saves a fixed `5` powered fuel for one Jupiter attempt, and maps actual finish speed to a `+0-40%` travel rate; minimum-speed exits get `+0%` and maximum-speed exits get `+40%`. Grade is independent of speed, so Green can still be breakneck. Good also adds `+0.35` flight instability for that attempt, capped at the existing `1.00` maximum; it affects the normal seeded startup drift, steering variance, oversteer, throttle kicks, damping, and auto-trim. The finish side and zone-relative distance from center carry into the launch corridor, where greater distance adds stronger outward initial course velocity. Perfect avoids the Good grade penalty but does not erase that physical lateral handoff. The dedicated departure pass pays no credits or Research Data; Miss and abort remain retryable, while impact still deals `18` hull damage.

| Configuration | Tank | Powered burn | Arrival margin | Velocity |
|---|---:|---:|---:|---:|
| Neither | 20 | 20 | 0; Jupiter locked | Normal |
| Fuel Tanks III | 25 | 20 | +5 | Normal |
| Good Mars slingshot | 20 | 15 | +5 | +0-40% from finish speed; +35% flight instability |
| Perfect Mars slingshot | 20 | 15 | +5 | +0-40% from finish speed; normal grade stability |
| Tanks + Good slingshot | 25 | 15 | +10 | +0-40% from finish speed; +35% flight instability |
| Tanks + Perfect slingshot | 25 | 15 | +10 | +0-40% from finish speed; normal grade stability |

The powered-fuel equation is `max(0, route burn * throttle multiplier - slingshot savings)`. Savings are subtracted after throttle scaling, so high throttle can still exceed the available fuel. Good instability is `clamp(base Flight Controls instability + 0.35, 0, 1)`; Perfect adds zero. Gravity-provided movement consumes no propellant and produces no engine heat. Beginning the Jupiter segment consumes the active slingshot and its optional instability penalty; a failed Jupiter attempt returns without momentum and requires another Good-or-better Mars pass. Fuel Tanks III remains installed permanently.

### Target-Bound Transfer Assists

Physical gravity assists are content-defined `TransferAssistDefinition` records, rather than destination-specific runtime behavior. A definition names its source and target, the reviewed scenario step that makes it available, permitted curriculum stages, minimum Flyby grade, fuel saving, speed scaling, Good-grade instability, and impact damage. Completing one writes a saved `PendingTransferAssist` containing the earned values. It is applied only when the prepared launch targets that exact authored destination and is consumed when that launch begins; ordinary recon Perfect bonuses remain generic next-launch bonuses. Destination content can declare a calibrated-margin requirement, so a future assist and route can use the same margin evaluator without adding a new planet branch. The current Mars-to-Jupiter record remains the sole authored physical assist. Saturn's Perfect Jupiter Flyby stays a separate scenario route-unlock challenge.

Version 16 is a strict campaign boundary. The pending-assist record is saved only as current campaign state; older, malformed, or future-version campaigns are cleared rather than reconstructed. Preferences remain intact.

The Jupiter travel node lands on Io. Io's regolith is inert and only Thermal lava seams contain ore; the Hazard Support Drone Mk I cools those seams into gray Common Ore. The current authored site presents outer and inner four-segment lava layers, then requires protected-Artifact towing and safe Surface extraction. The Artifact grants the standard 75 Expedition XP on full return while the authored scenario preserves its story and route unlocks. Afterward, a dedicated Jupiter Flyby requires a Perfect gold-corridor pass to open Saturn, and its preflight brief states that the Saturn launch commits the expedition outward. Future protected sites configure the same reusable cocoon, objective, event, and reward boundaries rather than adding destination-specific mining code.

## Research Data And Deferred Research Board

The player-facing progression currency is `Research Data`; the internal/save field remains `blueprintProgress` in version 16. It is not spendable. At `2/8/12/18/24`, it automatically adds Thermal, Recovery, Deep-Space, Predictive Guidance, and Exotic families to future Refit offers. It does not grant or install an item. The UI always shows current/next milestone and uses saved `RESEARCH BREAKTHROUGH` reviews when a threshold is crossed.

The generated Research board is currently debug-only: normal campaign play neither generates its projects nor enters its screen. Its prototype projects spend recovered materials and grant additional Research Data. Orbit does not open the board. Restoring and redesigning it is deferred as a separate feature.

The deferred board's inputs are:

Research inputs:

- Research Data from launch milestones and later press-your-luck transfers.
- Common, rare, and exotic materials from surface expeditions.
- Identified artifacts from deeper exploration.
- Frontier tier, so advanced projects appear only after the agency reaches the right scale.

Research outputs:

- Module families beyond the direct Fuel Tanks, Flight Controls, Engine Cooling, and Hull Plating launch curriculum.
- Crew facilities: better simulators, medical bays, psychology/coaching rooms, mission analysis labs.
- Surface tools: better drills, cargo harnesses, suit supplies, probes, hazard scanners.
- Support Drone systems: Drone Bay, role frames and paid duplicate capacity, Arkfall's emergency Attack/Defense kit, and post-Arkfall Perimeter Coordination research.
- Artifact threads: story and late-game build options, intentionally undefined until the narrative direction is clearer. For now, each identified artifact adds a small capped blueprint insight bonus to future research.

Design rule: research should mostly unlock variety and new decisions, not permanent raw stat inflation. Better parts can be stronger, but the reward should feel like a wider tool belt rather than a passive +10% forever.

The current POC treats research facilities and surface tools as small unlock layers:

- Mission Analysis Lab adds a small Research Data bonus to future debug research, representing better debriefs and sample processing.
- Field probes add action-kit margin and improve survey yield.
- Surface drills improve mining yield and rare-material odds.
- Cargo rigs reduce Push Deeper hazard chances while retaining their visible hauling benefits.
- The Moon Prospector contract unlocks the first Prospector Support Drone and Slot 1; the Mars contract unlocks empty Slot 2; Drone Support Program research adds the Resource and Survey Support Drones; Io separately commissions the first Hazard Support Drone.
- Arkfall grants Mk I Attack/Defense Support Drones, at least three bay slots, and hostile-contact mitigation. Perimeter Drone Network research grants Perimeter Coordination for advanced tuning and synergies.

These are research unlocks, not refit cards, so the player has a reason to care about Mars research even before enemy encounters exist.

Special ship components can also require recovered materials at refit time. Credits still represent hangar labor and fabrication time; common, rare, and exotic materials represent the physical samples needed to build deep-range tanks, predictive guidance, exotic drives, or recovery pods. Early/starter parts remain credit-only so the first loops stay readable.

## Surface Expedition Phase

Surface expeditions are the grounded counterpart to launches. Launch asks whether the ship survives the trip. Surface exploration asks how much the crew risks before extracting.

Core resources:

- Site profile: each expedition rolls a site such as Survey Basin, Ore Shelf, or Fracture Field. The site changes action yield, hazard, extraction pressure, or artifact odds.
- Action kits: the surface action clock. Surveying, pushing deeper, and hazard responses spend it.
- Rig fuel: a protected 3-unit expedition pack plus transfer fuel preserved at touchdown. Mining always spends exactly 1 fuel on deploy. While oxygen remains, operating draw is the current load multiplier divided by the permanent Rig Fuel Loop cycle: `15 / 18 / 21 / 24` seconds at Base / Rank I / II / III. The separate return stage is always reserved; Ark-era expeditions load up to 3 pack units from Ark fuel.
- Cargo: increases reward and is guaranteed once loaded onto the Ship.
- Hazard: destination difficulty plus depth pressure.
- Materials: normal departure returns every material on the Rig, intact Support Drones, and the Ship; Emergency Recall and disabled-rig recovery retain only Ship cargo.
- Artifacts: rare finds that require research to identify.

Core actions:

- Survey site: low-risk, low-reward; improves knowledge and finds common materials.
- Mine deposit: opens one direct-control Mining Rig run at the selected start depth for the current surface loop. The ship remains at surface depth 0, and deployment spends rig fuel rather than action kits.
- Push Deeper: its first layer is guaranteed and becomes a stable Mining Rig start depth. Attempting layer two or farther risks collapsing the unfinished tunnel; successful mapped artifact layers confirm the artifact for mining. It is disabled after mining because the run commits the field team to extracting or wrapping the current site.
- Return: atomically recalls intact Support Drones, loads every remaining manifest onto the Ship, and settles the exact objective allocation plus spendable surplus.

Surface actions should be presented as decision cards, not mystery buttons. Each card should show action-kit or fuel cost, current hazard/extraction risk, a short explanation of the payoff, and the action button. The player should understand why a field-kit unlock changed the odds without needing to inspect code or external notes.

Mars site profiles currently provide low-cost run variety:

- Survey Basin: safer open terrain with better survey yield.
- Ore Shelf: stronger mining yield and rare-material odds, with slightly higher site strain.
- Fracture Field: better artifact odds, but higher hazard and extraction pressure.

Solar-system rule: no enemies in Moon or Mars content. Mars can have environmental hazards, limited action kits, rig-fuel pressure, and extraction pressure, but not combat. Earth Orbit has no visible mission content; enemies start only after the agency reaches another star system.

Implemented Mars hazards are environmental setbacks attached to surface actions:

- Surveying can suffer dust interference, spending an extra action kit and making the site slightly more dangerous.
- Mining can suffer drill chatter, damaging cargo canisters and raising hazard.
- Pushing deeper can hit unstable terrain, costing an action kit and making extraction riskier.

Field-kit research mitigates these in theme: probes reduce survey trouble, drills reduce mining trouble, and cargo rigs reduce terrain/extraction pain. Hazards should create texture and pressure, not a separate combat layer.

Surface events add lighter run texture when hazards do not fire:

- Equipment failure consumes a spare action kit and nudges hazard upward.
- Unexpected deposits add a small material bonus to the current payload.
- Crew discoveries add a blueprint lead without defining artifact story lore yet.

These events should remain short, readable pulses attached to menu actions. They are not intended to become a second event-log screen.

The surface screen keeps a short recent mission log. It should preserve the last few site/action/hazard/event summaries so the player can understand why action kits, fuel, cargo, hazard, or blueprints changed after several clicks. Keep it bounded and lightweight; it is a memory aid, not a full journal.

The current mining layer is a compact direct-control mini-game opened from a prepared `Mine deposit`. The Mining Rig begins at the selected pushed depth, digs through chunked terrain, scans fog-of-war, recovers common/rare/exotic ore and artifacts, and ascends to the surface ship to stow cargo, service, and leave. Safety warnings override the normal `ARTIFACT` POI pointer with `SHIP` guidance; cross-layer targets lead toward the correct depth boundary. Autonomous Mining, Resource, Survey, Hazard, Attack, and Defense units are consistently labeled Support Drones. See `docs/MINING_MINIGAME_PLAN.md` for implementation details and animal crew class hooks.

## Post-Solar Enemy Layer

Enemy encounters arrive as a tonal shift after the solar system. That keeps the early game focused on human ambition, fragile machinery, and exploration, then lets the galaxy become stranger later.

For enemy encounters, use the Unity prototype's passive defense direction rather than turning Rocket Rogue into a precision shooter:

- Support Drones, turrets, shields, and area fields are equipment choices.
- The player survives through build planning, positioning, and extraction timing.
- Enemy pressure competes with mining greed: stay longer for resources, or leave before the planet overwhelms the expedition.

Current POC implementation: post-solar-system expeditions can trigger hostile contact as a surface event, and hostile mining terrain grows from simple encounter rooms into hives, miniboss lairs, spawners, and Act 3 boss chambers. Contact costs action kits, can damage cargo, raises site hazard, or damages the Mining Rig. Arkfall grants passive Mk I Attack/Defense Support Drone coverage without adding direct combat controls; Perimeter Coordination research unlocks advanced combat tuning and synergies.

## Unity Prototype Takeaways

Bring forward:

- Tool-forward exploration: jetpack, grappling, Support Drones, mining tools, and special equipment all fit the fantasy of a small expedition crew improvising under pressure.
- Equipment inventory: four explicit Launch Upgrade rows sit alongside unique permanent ship systems; Support Drone loadouts use explicit equipment slots, owned frames, and paid duplicates when a repeated specialist role is worth the capacity.
- Procedural chambers: generated corridors, rooms, vaults, and deposits are a strong fit for repeatable surface expeditions.
- Passive defense upgrades: Support Drones, shields, and area control can make combat strategic without requiring twitch-shooter controls.
- Artifact goals: the prototype's artifact collection idea fits the research/story loop cleanly.

Adapt carefully:

- Destructible terrain is exciting, but full free-form mesh destruction is risky for the shared native/web renderer. Keep generated cells, rooms, deposits, and dig actions authoritative before attempting real-time terrain slicing.
- Grappling is compelling, but it may belong in the later surface action prototype rather than the first Mars research slice.
- Weapon wheels and dual weapons may be overkill if combat is not present until another star system.
- Physics-heavy movement needs careful camera and collision handling. The Unity summary already calls out bouncing, ghosting, and visual artifacts.

Avoid for now:

- Shipping Destructible2D-style free-form mesh destruction in the shared C++ Vulkan/WebGL2 application before the scene-packet and terrain-revision costs are measured.
- Adding enemies to Mars just because the exploration prototype has enemies.
- Turning research into a large tech tree before the basic arrival -> research -> expedition -> extraction loop feels good.
- Creating story artifact details before we know what artifacts mean.

## Current POC Scope

The current shared C++ native/web application should keep this scope focused:

- The Research board is debug-only and is not opened by Mars or Orbit.
- Debug research projects spend materials and grant Research Data; redesign is deferred.
- Some research unlocks module or facility families.
- Mission Analysis Lab improves future research output.
- Some research unlocks field-kit tools that change future surface expedition math.
- Some advanced ship components require recovered materials as well as credits during refit.
- Artifact research identifies recovered artifacts but does not assign story lore yet.
- Identified artifacts provide capped blueprint insight for later research, giving recovery/decoding a mechanical reward before story content exists.
- The Legacy archive lists recovered artifacts by origin and decoded status without inventing final story lore.
- Surface expedition uses menu actions for survey, push, extract, Drone Ops, and the one-time mining deployment.
- Mining uses a direct-control Mining Rig screen with a normal 30s oxygen baseline, the current authored Io site's 60s baseline, arrival-derived rig-fuel draw, scanner pulses, destructible terrain, drill integrity, return/abort decisions, and payload conversion back into the surface expedition.
- Rig fuel is displayed separately from the protected return stage; mining can become unavailable because the rig pool is empty or because the mining run was already used. Both cases should present as `Mining Rig offline` with disabled button copy `Unavailable`.
- Survey site and Push Deeper are unavailable after mining. The primary recommendation should move toward extraction once payload is loaded or the rig is offline.
- Solar-system surface expeditions have environmental risk only.
- Khepri Prime and later post-Arkfall surface expeditions can trigger hostile contact events.
- Khepri Prime and later mining runs can include enemy tunnel networks and passive-defense combat pressure.
- Arkfall introduces Mk I Attack and Defense Support Drones for those later expeditions; Perimeter Drone Network research advances their coordination.

This is enough to prove whether post-arrival phases improve the launch loop without building a second full game too early.

## Open Design Choices

These are the decisions that need taste, not just implementation:

- Surface presentation: keep both menu-driven Surface Ops and the compact mining screen, or push more of survey/depth/extraction into the playable 2D scene?
- Mars pacing: one short surface expedition after each successful Mars transfer, or multiple surface sorties before returning to the launch loop?
- Artifact tone: ancient alien mystery, lost human probes, cosmic horror, or grounded scientific anomaly?
- EVA emphasis: how often should suit-only routes pull control away from the Mining Rig without weakening the vehicle-first identity?
- Combat escalation: how quickly should enemy pressure grow after Arkfall at Khepri Prime, and which artifact or Ark systems should explain it?
