# Rocket Rogue Documentation Map

Use this page to choose the right source before changing gameplay, presentation, platforms, or release behavior. Filenames that include `PLAN` are retained for stable links, but their documents describe the current implementation as well as explicitly marked future work.

## Consolidated design

- [Rocket Rogue Game Design Document](Rocket_Rogue_Game_Design_Document.docx) - formatted, shareable game design document.
- [Design Notes](DESIGN.md) - current gameplay loop, balance principles, architecture ownership, and persistence contracts.
- [Agent Design Context](AGENT_DESIGN_CONTEXT.md) - implementation priority order and condensed project direction; start here before extending a game system.
- [Ark Campaign and Navigation Spine](ARK_CAMPAIGN_NAVIGATION_PLAN.md) - numbered chapters, Straylight/Aaru Vale/Khepri Prime canon, and post-solar campaign structure.

## Playable systems

- [Post-Arrival Phases](POST_ARRIVAL_PHASES.md) - research, Surface Ops, extraction, and the boundary between the launch and surface loops.
- [Mini-Drone System](MINI_DRONE_SYSTEM.md) - Drone Bay roles, upgrades, capacity, and passive support/combat contract.
- [Mining Mini-Game](MINING_MINIGAME_PLAN.md) - current entry flow, shared-fuel tradeoff, controls, rewards, failure states, and implementation ownership.
- [Mining and Combat Progression](MINING_COMBAT_PROGRESSION.md) - deterministic Act/level rules, encounter budgets, campaign mapping, and persistence invariants.
- [Mining Lock-and-Key Sites](MINING_LOCK_AND_KEY_SITES.md) - artifact gate progression, capability forecasting, runtime state, and soft-lock prevention.

## Application, UI, and release

- [Native Desktop Builds](DESKTOP_BUILDS.md) - Windows/Linux architecture, build/package commands, saves, window behavior, and native verification.
- [Controller Support](CONTROLLER_SUPPORT.md) - shared native/web mappings, focus, pause safety, haptics, preferences, and physical test matrix.
- [RmlUi UX Polish Guide](RMLUI_UX_POLISH_GUIDE.md) - shared layout lanes, UI semantics, native/web parity, and visual verification.
- [Azure Static Web Apps](AZURE_STATIC_WEB_APPS.md) - web deployment workflow, static package contents, and local verification.
- [Art Asset Inventory](../assets/art/README.md) - registered runtime textures, unregistered authoring/future assets, dimensions, and import notes.

## Preserved design sources

The files under `reference/` are source extracts, not descriptions of current implementation. Preserve their original wording even when current code or canon differs.

- [USG Notes](reference/USG_NOTES.md) - primary extracted design direction; its retained PDF is under `reference/source-pdfs/`.
- [Roguelike Outline](reference/ROGUELIKE_OUTLINE.md) - supporting extracted source; its retained PDF is under `reference/source-pdfs/`.
- [Roguelite Elements](reference/ROGUELITE_ELEMENTS.md) - supporting extract whose original PDF was removed because it contained account-specific URLs.

When documents disagree, follow the priority order in [Agent Design Context](AGENT_DESIGN_CONTEXT.md), then reconcile the affected current-system document and the formatted GDD so implementation, UI copy, and design intent do not drift.
