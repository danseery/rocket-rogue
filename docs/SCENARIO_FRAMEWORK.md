# Reusable Scenario, Objective, Unlock, and Cocoon Framework

This is the authoring and ownership contract for campaign beats, optional encounters, generated contracts, route gates, and protected mining objectives. It exists to keep narrative content out of reusable gameplay mechanics.

The current Moon, Mars, Io, and Jupiter-to-Saturn progression is content authored through this framework. It is an example configuration, not a special code path that future content must copy.

## Boundary

`src/core/Content.cpp` owns authored content. `src/core/ScenarioSystem.*` owns reusable instance state, events, actions, rewards, route requirements, and objective presentation. Mining, Flyby, rewards, route evaluation, and UI do not decide behavior from a scenario ID, destination ID, title, reward copy, or campaign order.

This division is deliberate:

- Content may say where a scenario is offered, what the player sees, what it rewards, and which existing activity it launches.
- Mechanics receive typed configuration and typed runtime context. They must not contain an `if` branch for a named campaign beat or destination.
- Save migration may recognize retired IDs and legacy fields. Compatibility code is the only exception to the no-story-branch rule.
- New content selects an existing mechanic shape first. Add a new mechanic type only when its geometry or behavior is genuinely new and reusable.

`tools/check-scenario-boundaries.mjs` enforces the important part of this boundary for reusable mining, Flyby, route, and reward code. Run it whenever scenario, mining-site, or navigation content changes.

## Catalog model

`ContentCatalog` contains three related typed definition families:

| Definition | Purpose | Saved runtime counterpart |
| --- | --- | --- |
| `ScenarioDefinition` | A versioned graph of player-facing steps, prerequisites, actions, events, claims, and rewards. | `ScenarioInstance` |
| `MiningSiteDefinition` | A reusable fixed mining activity: arena request, biome, gate, oxygen budget, and optional cocoon. | Mining run/site context and `MiningSiteProgress` |
| `ScenarioFactoryDefinition` | A versioned generator entry point that selects a non-default scenario template. | A procedural `ScenarioInstance` with a seed and resolved parameters |

Definitions use stable string IDs and versions. Do not change the behavior of an already shipped definition in place. Keep the prior meaning available for saves, introduce a versioned migration when necessary, and give materially new content a new stable ID.

### Scenario definitions

A `ScenarioDefinition` has an availability unlock, optional destination association for presentation, and a set of `ScenarioStepDefinition` records. Steps are dependency-linked rather than implicitly linear: a step becomes active only when each prerequisite is complete and, when required, explicitly claimed. This supports a staged encounter, optional branch, or later multi-phase challenge without making the runtime understand a narrative sequence.

Each step owns:

- stable step ID and prerequisite IDs;
- location, title, instruction, reward preview, action label, and optional first-failure explanation;
- a typed completion event plus origin/target IDs, progress target, and optional Flyby grade target;
- whether a briefing is mandatory, whether the reward requires an explicit claim, and the next typed action; and
- typed `ScenarioReward` records.

The valid actions are acknowledgement, claim, begin activity, retry activity, and first-failure acknowledgement. Activities emit `ScenarioEvent` records such as safe material delivery, protected-objective extraction, Flyby completion, a manual action, equipment assignment, or an abort. `performScenarioAction()` and `recordScenarioEvent()` are the authoritative mutation path. A threshold becomes `READY TO CLAIM`; it does not award anything until the authored claim action is pressed.

### Rewards and route requirements

Current reward kinds grant an unlock key, Drone Bay capacity, a Support Drone, a Drone Upgrade Credit, frontier readiness, banked material inventory, or route access. Rewards are idempotent: a saved per-instance reward ledger prevents a reload or repeated event from granting the same reward twice.

A destination declares its travel prerequisites with `Destination::routeRequirementKeys`. `scenarioRouteRequirementStatus()` matches a missing key to the scenario step that can award it, so Navigation, Hangar, Solar Map, objective strips, modal copy, and route buttons share one blocker and next action. A reward may grant the key directly, or a `RouteAccess` reward may grant every configured key for a destination. Neither route evaluator needs to recognize a named world, tier, or story beat.

Scenario keys supplement the direct Launch Upgrade route gates. Navigation evaluates the content-defined scenario requirement first, then the destination's required Fuel, Controls, Cooling, or Hull rank. Legacy `FrontierReadiness` rewards remain readable for old scenarios and saves but do not replace the four lesson milestones.

## Authored and procedural scenarios

Campaign content is currently typed C++ data in `createDefaultContent()`. An authored definition uses `instantiateByDefault = true`; `ensureScenarioInstances()` creates one saved instance and its step records when it becomes available.

Procedural content uses the same runtime shape:

1. Add a typed scenario template with `instantiateByDefault = false`.
2. Add a `ScenarioFactoryDefinition` that refers to that template and has its own stable ID, version, and seed salt.
3. Generate a `ScenarioInstance` with `makeProceduralScenarioInstance()`, a deterministic seed, and validated resolved `key=value` parameters.
4. Save the factory/version, template definition/version, seed, resolved parameters, steps, acknowledgement/failure state, and awarded reward IDs before the activity begins.

The template is never silently instantiated as a live campaign objective. The runtime resolves the saved parameters against the typed template on reload, so an active generated contract never rerolls. Supported parameter keys can tune destination availability and a step's presentation, event origin/target, completion and grade targets, briefing/claim/failure policies, activity, mining-site reference, and typed rewards. Validation rejects unknown fields, invalid reward kinds, bad site IDs, and malformed values.

## Mining sites and layered cocoons

`MiningSiteDefinition` has no campaign or destination semantics. A scenario chooses when to offer it. This allows a hand-authored encounter and a generated encounter to launch the same type of site.

`MiningCocoonDefinition` is the reusable layered protection component. It has its own stable ID/version, a `ProtectedObjectiveRef`, and an ordered list of layer definitions. A layer contains a stable local ID, player-facing label, relative cell offsets, reveal policy, completion rule, hazard affinity, and required Hazard Drone mark. There is no fixed number of layers or segments.

The generic runtime rules are:

1. A configured discovery policy can reveal an entire active layer when one of its cells is discovered.
2. A later layer stays hidden until its declared predecessor is complete.
3. Hidden layer cells and their protected objective cannot render, scan, attract a Hazard Support Drone, receive treatment progress, tether, or otherwise accept interaction.
4. Each required cell must satisfy its layer completion rule. `TreatAndExcavate` requires cooling/treatment and drilling; treatment alone is not enough.
5. When the last required layer completes, the protected objective becomes visible and interactable. Its safe extraction emits a typed `ProtectedObjectiveExtracted` event with the protected-objective identity.

Hazard Support Drones use the earliest incomplete revealed cocoon layer as the highest-priority work. They can still move collisionlessly through terrain, but discovery remains player-owned: line of sight, the Mining Rig scanner, or a Survey Support Drone pulse must reveal a tile before a Hazard unit can select it. This is a cocoon rule, not an Io rule.

The first protected-objective adapter is an Artifact payload. New objective adapters should translate their own safe-completion interaction into the same generic protected-objective completion boundary; they must not change cocoon discovery, layer progress, or Hazard Drone coordination.

## Current authored campaign configuration

The following belongs in content and presentation, not in reusable mechanics:

| Scenario content | Steps and explicit reward | Route effect |
| --- | --- | --- |
| Moon: Lunar Prospector Contract | Mandatory mining briefing; safely deliver 30 Moon Common Ore; explicitly claim Prospector Mk I, Slot 1, and the Mining Support Drone. | Grants the Mars route key and readiness. |
| Mars: Bay Expansion | Mandatory bay-expansion briefing; safely deliver 40 Mars Common Ore; explicitly claim empty Slot 2. | Grants the Jupiter route key and readiness. |
| Io: Volcanic Descent | Commission Hazard Support Drone; launch the Thermal layered-recovery site; complete its cocoon, tether its protected Artifact, and extract safely; safe extraction grants one Drone Upgrade Credit. | Grants the slingshot scenario's availability key. |
| Jupiter departure: Perfect Slingshot | Mandatory one-way briefing; run a scenario Flyby that requires Perfect; first non-Perfect outcome explains the failure once; explicitly claim its reward. | Grants the Saturn route key and readiness permanently. |

The Io mining-site configuration happens to use a Thermal biome, an inert Regolith field, four cardinal outer segments, four diagonal inner segments, a 60-second oxygen budget, and an Artifact objective. Those facts are configuration for this site, not invariants for every cocoon, artifact, or destination.

## UI and action contract

`ScenarioObjectivePresentation` is the only state-derived source for objective strips, modal content, activity HUDs, map checklists, Drone Ops status, and claim affordances. It exposes `LOCKED`, `ACTIVE`, `READY TO CLAIM`, and `COMPLETE` with text, progress, reward preview, action, modal flags, and any pending first-failure explanation.

Native RmlUi and WebAssembly use the same `assets/ui` templates and RCSS. A scenario action is emitted with semantic scenario-instance ID, step ID, and `ScenarioActionKind` attributes. Templates may choose layout and visual family, but must not infer a claim, route gate, or mandatory-modal behavior from text, a route name, or a markup query. See [RmlUi Template and Component System](RMLUI_TEMPLATE_COMPONENT_SYSTEM.md) for the shared template/focus rules.

## Save version 10 and migration

Save version 10 retains version 9's scenario instances, definition/factory versions, procedural seed and resolved parameters, per-step progress, briefing/failure acknowledgements, claims, completion, reward ledger entries, mining-site provenance, and protected-objective state. It additionally persists the four launch-upgrade ranks and Fuel, Controls, Temperature, and Asteroids lesson completion.

Migration maps the old Moon/Mars/Io/slingshot fields into their authored scenario steps without losing delivered progress, ready-to-claim state, or already awarded rewards. Existing active mining terrain is retained: legacy site records keep stable site IDs/seeds and are marked as migration provenance; partial treatment and drilling survive; a discovered outer layer remains revealed; later layers and embedded payloads remain hidden until their generic prerequisites complete. A loose, tethered, delivered, or completed payload is never relocked. Saturn-or-later saves are backfilled without retroactive gates or briefings.

`SaveSchema.h` and `SaveData.*` are authoritative for wire keys, defaults, and migration order. Test v10 round trips plus representative pristine/progressed v9 and v8 states whenever scenario, site, cocoon, or launch-progression fields change.

## Authoring checklist

1. Reuse an existing event, action, reward, requirement, activity, site, or cocoon mechanic whenever it fits.
2. Add stable content IDs to `ContentIds.h` only when the identity must be shared by content, migrations, or tests.
3. Add a versioned typed definition to `Content.cpp`; keep narrative copy there.
4. Connect any route with keys and scenario rewards, not destination-specific navigation branches.
5. Use a `MiningSiteDefinition` and `MiningCocoonDefinition` for protected mining rather than adding encounter flags to generic terrain or drone code.
6. Route every player action through the scenario dispatcher and every result through a typed event.
7. Render from `ScenarioObjectivePresentation` on native and web; keep semantic action and focus IDs stable.
8. Add authored, procedural, event/claim, route, cocoon-layer, save-migration, and native/web presentation coverage as applicable.
9. Run catalog validation, `node tools/check-scenario-boundaries.mjs`, relevant core/mining/UI tests, and `git diff --check`.

If a proposed feature requires a code comparison against a campaign ID, destination ID, title, or reward copy outside content/migration code, stop and express the needed capability in a typed definition instead.
