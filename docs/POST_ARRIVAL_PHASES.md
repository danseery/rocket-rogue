# Post-Arrival Phases

For the latest agent-facing design priority order, see `docs/AGENT_DESIGN_CONTEXT.md`. USG Notes is the primary source for surface, mining, animal class, and drone direction.

This note extends the current launch/refit prototype with the next two layers: research and landed exploration. The goal is to keep the press-your-luck rocket loop as the spine, then make successful arrivals feel like new opportunities instead of just a bigger payout screen.

## Phase Structure

Earth Orbit and Moon stay focused on proving the agency can travel and return safely. The player should be collecting flight data, upgrading the vehicle, training crew, and deciding when a transfer attempt is worth the risk.

Mars is the first research frontier. After a successful Mars arrival, the game opens a post-arrival sequence:

1. Arrival summary: confirms the transfer succeeded and shows what the agency can investigate.
2. Research phase: spend recovered materials and blueprints to unlock new module families, crew facilities, surface tools, and artifact analysis threads.
3. Surface expedition: spend action kits to survey/push/extract and spend shared fuel to deploy the mining drone.
4. Recovery decision: return with the payload before hazard, cargo weight, low action kits, or low fuel makes extraction too risky.
5. Refit window: use credits, research unlocks, and recovered materials to improve the next launch cycle.

This gives Mars a distinct role: it is where the game stops being only "can we get there?" and starts asking "what do we dare do now that we made it?"

Each optional activity introduces its progression value at the moment the player first selects it. Flyby and Orbit explicitly frame blueprint progress as the route to permanent ship upgrades. Landing frames surface upgrades as improvements to the mining drone. These briefs are acknowledged only when the player starts the activity, survive save/load, and never name mini-drones before Drone Bay unlock; the first Drone Ops selection introduces that system after it becomes available.

## Research Phase

Research is a strategic menu phase, not an action scene. It should be quick, readable, and tied to long-term agency capability.

Research inputs:

- Blueprints from launch milestones and overburn gambles.
- Common, rare, and exotic materials from surface expeditions.
- Identified artifacts from deeper exploration.
- Frontier tier, so advanced projects appear only after the agency reaches the right scale.

Research outputs:

- Module families: thermal control, pressure control, recovery systems, guidance AI, deep-range power.
- Crew facilities: better simulators, medical bays, psychology/coaching rooms, mission analysis labs.
- Surface tools: better drills, cargo harnesses, suit supplies, probes, hazard scanners.
- Drone systems: Drone Bay, helper drones, Arkfall's emergency Attack/Defense kit, and post-Arkfall Perimeter Coordination research.
- Artifact threads: story and late-game build options, intentionally undefined until the narrative direction is clearer. For now, each identified artifact adds a small capped blueprint insight bonus to future research.

Design rule: research should mostly unlock variety and new decisions, not permanent raw stat inflation. Better parts can be stronger, but the reward should feel like a wider tool belt rather than a passive +10% forever.

The current POC treats research facilities and surface tools as small unlock layers:

- Mission Analysis Lab adds a small blueprint bonus to future research, representing better debriefs and sample processing.
- Field probes add action-kit margin and improve survey yield.
- Surface drills improve mining yield and rare-material odds.
- Cargo return rigs reduce extraction risk, especially when cargo gets heavy.
- Drone Bay unlocks persistent helper drones and the Drone Ops loadout screen.
- Arkfall grants Mk I Attack/Defense drones, at least three bay slots, and hostile-contact mitigation. Perimeter Drone Network research grants Perimeter Coordination for advanced tuning and synergies.

These are research unlocks, not refit cards, so the player has a reason to care about Mars research even before enemy encounters exist.

Special ship components can also require recovered materials at refit time. Credits still represent hangar labor and fabrication time; common, rare, and exotic materials represent the physical samples needed to build deep-range tanks, predictive guidance, exotic drives, or recovery pods. Early/starter parts remain credit-only so the first loops stay readable.

## Surface Expedition Phase

Surface expeditions are the grounded counterpart to launches. Launch asks whether the ship survives the trip. Surface exploration asks how much the crew risks before extracting.

Core resources:

- Site profile: each expedition rolls a site such as Survey Basin, Ore Shelf, or Fracture Field. The site changes action yield, hazard, extraction pressure, or artifact odds.
- Action kits: the surface action clock. Surveying, pushing deeper, and hazard responses spend it.
- Shared fuel: the shuttle and mining drone reserve. Mining spends 1 fuel on deploy and then 1 fuel per 15 seconds while oxygen remains; before the Ark, it is labeled shared fuel, and after Ark discovery it is framed as Ark fuel.
- Cargo: increases reward but raises extraction risk.
- Hazard: destination difficulty plus depth pressure.
- Materials: banked only if extraction succeeds enough to recover the payload.
- Artifacts: rare finds that require research to identify.

Core actions:

- Survey site: low-risk, low-reward; improves knowledge and finds common materials.
- Mine deposit: opens one direct-control drone mining run for the current surface loop. It spends shared fuel, not action kits.
- Push Deeper: raises hazard and potential reward; this is the surface version of overburning. It is disabled after mining because the mining run commits the field team to extracting or wrapping the current site.
- Extract payload: attempts to bring home the current cargo; risk rises with hazard, cargo, low action kits, and spent fuel.

Surface actions should be presented as decision cards, not mystery buttons. Each card should show action-kit or fuel cost, current hazard/extraction risk, a short explanation of the payoff, and the action button. The player should understand why a field-kit unlock changed the odds without needing to inspect code or external notes.

Mars site profiles currently provide low-cost run variety:

- Survey Basin: safer open terrain with better survey yield.
- Ore Shelf: stronger mining yield and rare-material odds, with slightly higher site strain.
- Fracture Field: better artifact odds, but higher hazard and extraction pressure.

Solar-system rule: no enemies in Earth Orbit, Moon, or Mars content. Mars can have environmental hazards, limited action kits, shared fuel pressure, and extraction pressure, but not combat. Enemies start only after the agency reaches another star system.

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

The current mining layer is a compact direct-control mini-game opened from `Mine deposit`. The mining drone digs through chunked terrain, scans fog-of-war, recovers common/rare/exotic ore and artifacts, and then stows or aborts back into the same surface outcome model. See `docs/MINING_MINIGAME_PLAN.md` for implementation details and animal crew class hooks.

## Post-Solar Enemy Layer

Enemy encounters arrive as a tonal shift after the solar system. That keeps the early game focused on human ambition, fragile machinery, and exploration, then lets the galaxy become stranger later.

For enemy encounters, use the Unity prototype's passive defense direction rather than turning Rocket Rogue into a precision shooter:

- Drones, turrets, shields, and area fields are equipment choices.
- The player survives through build planning, positioning, and extraction timing.
- Enemy pressure competes with mining greed: stay longer for resources, or leave before the planet overwhelms the expedition.

Current POC implementation: post-solar-system expeditions can trigger hostile contact as a surface event, and hostile mining terrain grows from simple encounter rooms into hives, miniboss lairs, spawners, and Act 3 boss chambers. Contact costs action kits, can damage cargo, raises site hazard, or damages the mining drone. Arkfall grants passive Mk I Attack/Defense coverage without adding direct combat controls; Perimeter Coordination research unlocks advanced combat tuning and synergies.

## Unity Prototype Takeaways

Bring forward:

- Tool-forward exploration: jetpack, grappling, drones, mining tools, and special equipment all fit the fantasy of a small expedition crew improvising under pressure.
- Equipment inventory: unique permanent ship systems and bounded Reach, Control, and Recovery tracks fit Rocket Rogue's refit philosophy; drone loadouts may still use explicit equipment slots.
- Procedural chambers: generated corridors, rooms, vaults, and deposits are a strong fit for repeatable surface expeditions.
- Passive defense upgrades: drones, shields, and area control can make combat strategic without requiring twitch-shooter controls.
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

- Mars arrival opens research.
- Research projects spend materials and blueprints.
- Some research unlocks module or facility families.
- Mission Analysis Lab improves future research output.
- Some research unlocks field-kit tools that change future surface expedition math.
- Some advanced ship components require recovered materials as well as credits during refit.
- Artifact research identifies recovered artifacts but does not assign story lore yet.
- Identified artifacts provide capped blueprint insight for later research, giving recovery/decoding a mechanical reward before story content exists.
- The Legacy archive lists recovered artifacts by origin and decoded status without inventing final story lore.
- Surface expedition uses menu actions for survey, push, extract, Drone Ops, and the one-time mining deployment.
- Mining uses a direct-control drone screen with 30s baseline oxygen, shared fuel draw, scanner pulses, destructible terrain, drill integrity, stow/abort decisions, and payload conversion back into the surface expedition.
- Shared fuel is displayed as a shuttle/drone tradeoff; mining can become unavailable because the fuel reserve is empty or because the mining run was already used. Both cases should present as "Mining drone offline" with disabled button copy "Unavailable".
- Survey site and Push Deeper are unavailable after mining. The primary recommendation should move toward extraction once payload is loaded or the drone is offline.
- Solar-system surface expeditions have environmental risk only.
- Nearby Star and later surface expeditions can trigger hostile contact events.
- Nearby Star and later mining runs can include enemy tunnel networks and passive-defense combat pressure.
- Arkfall introduces Mk I Attack and Defense drones for those later expeditions; Perimeter Drone Network research advances their coordination.

This is enough to prove whether post-arrival phases improve the launch loop without building a second full game too early.

## Open Design Choices

These are the decisions that need taste, not just implementation:

- Surface presentation: keep both menu-driven Surface Ops and the compact mining screen, or push more of survey/depth/extraction into the playable 2D scene?
- Mars pacing: one short surface expedition after each successful Mars transfer, or multiple surface sorties before returning to the launch loop?
- Artifact tone: ancient alien mystery, lost human probes, cosmic horror, or grounded scientific anomaly?
- Player avatar: astronaut on foot, remote rover, mining drone, or abstract expedition team?
- Combat escalation: how quickly should enemy pressure grow after Nearby Star, and which artifact or Ark systems should explain it?
