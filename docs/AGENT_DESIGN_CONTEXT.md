# Agent Design Context

This is the fast orientation file for future agents working on Rocket Rogue / Orebit. When design sources conflict, follow the priority order below.

## Design Priority Order

1. `docs/reference/USG_NOTES.md` is the primary direction.
2. Current playable Rocket Rogue behavior is next. Preserve the working launch, arrival, surface, and mining loop unless the user asks for a larger redesign.
3. `docs/reference/ROGUELIKE_OUTLINE.md` supports Ark/base, crew, survival, and long-run structure.
4. `docs/reference/ROGUELITE_ELEMENTS.md` supports roguelite vocabulary and digging/mining patterns.
5. Older docs in this repo are useful context, but USG Notes overrides generic roguelite boilerplate.

Source PDFs are preserved under `docs/reference/source-pdfs/` when they do not contain account-specific URLs. Markdown extracts are the agent-readable source of truth for repo work.

## Current Direction

The game is becoming a chunky, mobile-readable, Straylight-inspired space-mining roguelite. Rocket Rogue is still the spine: press-your-luck launch, survive the trip, reach a destination, then decide how much surface value to risk before returning to refit.

The desired aesthetic is:

- Chunky, blocky, geometric.
- Readable at game speed.
- Retro arcade with modern polish.
- Colorful but not noisy.
- Built around clear state changes and tactile feedback.

## Core Game Shape

- Launch loop: risk hidden failure, manage telemetry, return/eject/mitigate, earn flight data and credits.
- Arrival loop: reaching a destination should feel rewarding, then ask whether to flyby, orbit, or land.
- Surface loop: landing opens survey, one fuel-gated mining run, deeper excavation before mining, payload extraction, and drone upgrades that persist until the shuttle or mining drone is destroyed.
- Mining mini-game: direct drone control, destructible chunked terrain, fog of war, scanner pulses, drill friction, ore pockets, artifacts, 30s baseline oxygen, shared fuel draw, cargo, and extraction risk.
- Long-term loop: recovered materials, blueprints, artifacts, and research unlock better tools, drone bay options, ship parts, Ark/base systems, and future story threads.

## Planet And Resource Pillars

USG Notes frames planets around readable tradeoffs:

- Danger: hazards, hostile conditions, later enemies.
- Value: resource quality, artifacts, research payoff.
- Durability: how hard the terrain or target is to break through.

Resource and object pillars should also stay understandable:

- Weight affects extraction/cargo strain.
- Durability affects drill time and tool wear.
- Value affects reward and the reason to use Push Deeper.

## Crew Direction

Generic crew placeholders have been replaced with animal specialists. Training still levels them, stress still matters, and their class/focus should color both launch and surface play:

- Capybara Tank: survival, endurance, oxygen, safety.
- Beaver Engineer: resilience, repairs, drill integrity.
- Fox Ace: navigation, extraction, abort safety.
- Prairie Dog Scout: digging, scanning, tunnel efficiency.
- Squirrel Hoarder: resource gathering and rare material yield.
- Chipmunk Speedster: exploration, movement, traversal.

## Drones And Passive Defense

See [MINI_DRONE_SYSTEM.md](MINI_DRONE_SYSTEM.md) for the active Drone Bay design and current implementation slice.

Drone systems start as environmental mining support in the solar system. Combat drones stay locked until Perimeter Drone Network and post-solar hostile systems.

Early drone roles:

- Mining: passive nearby ore work.
- Resource: oxygen/fuel reserve and run extension.
- Survey: scanner radius, POI hints, fog-of-war reads.
- Stabilizer: lower chatter, bounce, and drill wear.

Post-solar drone roles:

- Attack: passive enemy fire.
- Defense: shielding and hazard/enemy damage relief.

This supports the Brotato / Vampire Survivors passive-defense direction without introducing enemies on Moon or Mars.

## Ark And Base Progression

See [ARK_CAMPAIGN_NAVIGATION_PLAN.md](ARK_CAMPAIGN_NAVIGATION_PLAN.md) for the active campaign spine. The Ark is discovered beyond Neptune as derelict but operable. The first Ark jump succeeds; the second scripted jump hits a gravity well and strands the Ark in a hostile system. After that, Navigation becomes the mission-selection layer and shuttle sorties return to the Ark instead of Earth.

Ship sections such as Bio Farm, Robotics, Medical, Living, Command, Engineering, Science, Cargo/Hangar, Environmental, and Cultural systems can become future unlock families.

## Implementation Bias

Prefer incremental systems that hook into the current C++/WebGL prototype:

- Add content types and presentation helpers before new architecture.
- Keep save compatibility with missing-field defaults.
- Make new systems visible through concise UI states.
- Avoid enemies in Moon/Mars mining.
- Treat shared fuel as an explicit tradeoff. The UI and docs should make clear that the shuttle and mining drone spend the same reserve.
- Use procedural or placeholder visuals unless the user provides assets.
