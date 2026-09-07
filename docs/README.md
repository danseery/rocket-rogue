# OREBIT Documentation Map

The [OREBIT Game Design Document](Rocket_Rogue_Game_Design_Document.docx) is the definitive design. Supporting documents must agree with it; code supplies implementation evidence and reference extracts supply inspiration. Specific unresolved story/progression relationships are marked TBD in GDD Section 8. Use this page to find implementation detail. Filenames that include `PLAN` are retained for stable links, but their documents describe the current implementation as well as explicitly marked future work.

## Consolidated design

- [Rocket Rogue Game Design Document](Rocket_Rogue_Game_Design_Document.docx) - definitive game design, including story and progression TBDs.
- [Design Notes](DESIGN.md) - current gameplay loop, balance principles, architecture ownership, and persistence contracts.
- [Agent Design Context](AGENT_DESIGN_CONTEXT.md) - implementation priority order and condensed project direction; start here before extending a game system.
- [Scenario Framework](SCENARIO_FRAMEWORK.md) - campaign goals, explicit claims, typed events, saved acknowledgements, and battery/Ark integration boundaries.

## Playable systems

- [Persistent Expeditions](PERSISTENT_EXPEDITIONS.md) - solar travel, spatial map, cruise, home shipyard, banking, recovery, and return-or-continue decisions.

- [Post-Arrival Phases](POST_ARRIVAL_PHASES.md) - connected Flight/Mining journey, simulation ownership and persistence handoffs.
- [Support Drone System](MINI_DRONE_SYSTEM.md) - Drone Bay roles, owned frames, paid duplicates, upgrades, capacity, and passive support/combat contract.
- [Mining Mini-Game](MINING_MINIGAME_PLAN.md) - deployment, Rig/EVA controls, independent resources, physical recovery and implementation ownership.
- [Mining and Combat Progression](MINING_COMBAT_PROGRESSION.md) - deterministic Act/level rules, encounter budgets, campaign mapping, and persistence invariants.
- [Themed Enemy Sprite Library](ENEMY_SPRITE_LIBRARY.md) - side-view animation contract, reusable GenAI prompt bible, deterministic importer, theme mechanics, and provenance.
- [Enemy Sprite Prompt Manifest](ENEMY_SPRITE_PROMPT_MANIFEST.md) - per-archetype generation records, shared prompt templates, and rejected-direction guardrails.
- [Mining Lock-and-Key Sites](MINING_LOCK_AND_KEY_SITES.md) - artifact gate progression, capability forecasting, runtime state, and soft-lock prevention.

- [Orbital Preparation](ORBITAL_PREPARATION_PROTOTYPE.md) - capture, Pulse Survey, Zone 1 laser work and explicit Land.
- [Surface Arrival](HEROIC_SURFACE_ARRIVAL.md) - touchdown, deployment, camera and cues.
- [Underground Landing and Ascent](UNDERGROUND_LANDING_PROTOTYPE.md) - cached terrain, parked ship services and manual departure.

## Application, UI, and release

- [Native Desktop Builds](DESKTOP_BUILDS.md) - Windows/Linux architecture, build/package commands, saves, window behavior, and native verification.
- [Steam Release Readiness](STEAM_RELEASE_READINESS.md) - Steam-native campaign gates, Windows/Linux/Deck acceptance, collaborative ownership, and the human-creative AI boundary.
- [Controller Support](CONTROLLER_SUPPORT.md) - shared native/web mappings, focus, pause safety, haptics, preferences, and physical test matrix.
- [RmlUi UX Polish Guide](RMLUI_UX_POLISH_GUIDE.md) - shared layout lanes, UI semantics, native/web parity, and visual verification.
- [Azure Static Web Apps](AZURE_STATIC_WEB_APPS.md) - web deployment workflow, static package contents, and local verification.
- [Art Asset Inventory](../assets/art/README.md) - registered runtime textures, unregistered authoring/future assets, dimensions, and import notes.

## Preserved design sources

The files under `reference/` are source extracts, not descriptions of current implementation. Preserve their original wording even when current code or canon differs.

- [USG Notes](reference/USG_NOTES.md) - preserved design inspiration; its retained PDF is under `reference/source-pdfs/`.
- [Roguelike Outline](reference/ROGUELIKE_OUTLINE.md) - supporting extracted source; its retained PDF is under `reference/source-pdfs/`.
- [Roguelite Elements](reference/ROGUELITE_ELEMENTS.md) - supporting extract whose original PDF was removed because it contained account-specific URLs.

When documents disagree, the GDD governs design. Reconcile supporting notes with it; use specific TBDs for unresolved story/progression conflicts rather than treating older reference material or retained code paths as overriding design.
