# RmlUi Template and Component System

This document defines how Rocket Rogue builds reusable UI for native and WebAssembly RmlUi. The goal is one visual system with predictable extension points: future screens select shared geometry, contribute typed content, and preserve established behavior instead of recreating a nearly identical layout.

## Source of Truth

The complete runtime UI tree lives under `assets/ui` and is packaged unchanged for native and web builds. Native RmlUi and WebAssembly RmlUi load the same `panel.rml`, templates, and RCSS. The browser shell remains responsible for hosting the canvas and forwarding platform input; it does not maintain an alternate panel, modal, prompt, toast, focus stack, or realtime-HUD renderer.

`panel.rml` is the persistent runtime document. It loads every template and shared stylesheet in its head, then exposes stable hosts for:

- the current panel;
- scene overlays;
- the modal stack;
- controller prompts; and
- performance statistics.

The document remains loaded while gameplay changes screens. Screen changes and realtime updates replace or patch host content; they do not reload the document. This preserves focus, modal state, scroll position, and RmlUi template availability.

## Ownership Boundary

RmlUi templates and typed C++ presentation renderers solve different problems.

Templates own stable geometry and visual chrome:

- frame and content-lane boundaries;
- header, body, and footer regions;
- persistent overlay hosts;
- modal framing;
- screen-family geometry; and
- the one declared content target into which dynamic markup is inserted.

Typed C++ renderers own variable content and behavior:

- screen headers and activity text;
- objective strips and progress state;
- metric and chip strips;
- fixed-lane cards and card grids;
- detail stacks;
- action footers;
- dynamic lists;
- button and setting states;
- modal records; and
- semantic attributes used for action and focus binding.

RmlUi templates are not React-style components. Each template has one declared content target, so a template must not pretend to accept arbitrary named parameters. C++ builds one flattened content payload from typed data, and the selected template wraps that payload in reusable structure.

The presentation boundary is `buildGamePanelPresentation()`. Its result selects a `PanelTemplateKind` and `PanelVisualFamily`, carries explicit screen and surface metadata, provides flattened dynamic markup, and includes typed `ModalPresentation` records. Consumers must not infer structure or behavior by searching the generated markup.

## Template Catalog

All template names use the globally unique `rr-` prefix.

| Template | `PanelTemplateKind` | Use |
|---|---|---|
| `rr-document-shell` | Persistent document | Root hosts for panel, overlays, modals, prompts, and performance statistics. |
| `rr-workspace-shell` | `Workspace` | Management surfaces such as Hangar with header, objective/status, workspace body, and action lanes. |
| `rr-control-shell` | `ControlPanel` | Active control surfaces such as Flyby, with stable telemetry and command geometry. |
| `rr-surface-minigame-shell` | `SurfaceMinigame` | Surface Scan and related scene-plus-control arrangements. |
| `rr-mining-shell` | `Mining` | Mining HUD, viewport-relative status, controls, and mission overlays. |
| `rr-takeover-shell` | `Takeover` | Full-attention story briefings and other deliberate transition beats. |
| `rr-results-shell` | `Results` | Outcome-first debrief and result presentations. |
| `rr-modal-shell` | Modal presentation | Shared modal title, body, close affordance, and action/content region. |

`LegacyRaw` is a temporary migration adapter. It places an unconverted screen's existing flattened markup inside the persistent document without claiming that the screen follows a shared family contract. It must not be selected for new screens. Delete it after all remaining screen families are migrated.

First-wave screens whose migrated flattened content still owns the old board lane set `legacyContentOwnsLaneGeometry` explicitly. The runtime maps that metadata to `rr-legacy-content-owns-lane`, which neutralizes the template lane only for that presentation. The flag defaults to false and must not be inferred from `PanelTemplateKind`; new screens therefore inherit the shell's 736/704/16 geometry automatically.

Add a template only for genuinely new geometry. A different color treatment, title, card count, copy length, warning state, or button arrangement is a visual-family or typed-content variation, not a reason for another shell.

## Component Catalog

Shared parameterized components remain typed C++ renderers and use shared RCSS primitives:

| Component | Stable responsibility | Variable inputs |
|---|---|---|
| Screen header | Title/activity alignment and utility-control lane | Location, title, subtitle, utility actions |
| Objective strip | Objective-state hierarchy and reward/next-action lanes | `LOCKED`, `ACTIVE`, `READY TO CLAIM`, `COMPLETE`, progress, reward |
| Metric/chip strip | Consistent scan order and chip sizing | Label, value, icon, warning state |
| Fixed-lane card | Card header/body/footer separation | Title, description, stats, status, action |
| Card grid | Shared gaps, columns, wrapping rules, and final-slot behavior | Ordered card presentations |
| Detail stack | Dense procedural information without nested-card drift | Rows, labels, values, emphasis |
| Action footer | Protected final-action lane | Primary, secondary, caution, destructive actions |
| Fixed action stack | Scrollable context above a viewport-safe final card/action lane | Context content, ordered cards, final actions |

These components may gain explicit variants when multiple screens need the same change. Screen-local replacements for shared headers, objectives, KPI rows, card lanes, or action footers are prohibited. A local exception is acceptable only when it describes screen-specific content inside the shared lane, not a second alignment system.

## Layout Contract

The canonical phase-board contract is:

- board frame: `736px`;
- content lane: `704px`; and
- left/right inset: `16px`.

The equation is intentional: `736 - (2 × 16) = 704`. Headers, objectives, KPI rows, cards, action groups, and footer lanes must terminate on those same content edges.

Shared tokens and primitives enforce this contract. C++ may calculate viewport-dependent `UiLayoutMetrics`, but it applies those values as inherited CSS custom properties. It does not construct a new stylesheet for each screen or encode one-off widths in generated markup.

When a layout must adapt:

1. Preserve the named lane and its edge alignment.
2. Change shared tokens, a shared primitive, or an explicit component variant.
3. Test every screen family that consumes that shared rule.
4. Validate 1280×800, compact, 1080p, 1440p, and 4K viewports.

Do not repair one row with arbitrary margins, duplicate width calculations, trailing-card spacing, or screen-specific copies of a shared primitive.

## RCSS Layers

The external RCSS tree separates concerns:

1. **Tokens** define color, typography, spacing, borders, and the 736/704/16 layout metrics.
2. **Primitives** define lanes, rows, cards, chips, buttons, and focus treatments.
3. **Shells** define geometry for each template family.
4. **Visual families** apply management, decision, live-HUD, mining, selection, result, and fullscreen treatments without changing semantic structure.
5. **Screen exceptions** contain the smallest unavoidable rules for truly screen-specific content.
6. **Legacy compatibility** contains only the extracted rules needed while staged `LegacyRaw` and first-wave typed renderers are being migrated. New shared rules do not belong there.

New rules belong in the earliest layer that accurately describes their reuse. A screen-exception stylesheet must not redefine a shared lane or copy an entire component.

Edit an authored layer, then regenerate and validate the runtime bundle:

```powershell
npm.cmd run ui:bundle
npm.cmd run ui:validate
```

`styles/all.rcss` is generated from the ordered layer manifest and must not be edited by hand. Validation fails when the bundle is stale, its layer order changes unexpectedly, or a dependency falls out of the graph.

## Semantic and Interaction Contract

The template system changes structure, not action semantics. Preserve:

- action IDs;
- modal IDs;
- stable and default focus IDs;
- controller geometry classes;
- settings attributes;
- realtime patch IDs;
- modal stacking order;
- non-dismissible gates; and
- return-focus behavior.

Button bindings are read directly from semantic attributes on loaded RmlUi elements. They are never paired by generated-markup position, so template expansion and unrelated element insertion cannot redirect an action.

Typed `ModalPresentation` records define modal identity, title, body, auto-open behavior, dismissibility, close visibility, and close action. Do not encode a modal as a custom `<template data-modal>` block or parse it back out of raw markup.

The browser receives only `UiHostContext`: screen, surface kind, title state, and whether the current interaction is actively realtime. It never receives panel or HUD markup. Keyboard and pointer shortcuts must require that authoritative activity flag, and transitions to results, modal ownership, shutdown, or failed initialization must release held realtime input. This keeps named RmlUi actions authoritative for every claim and continuation.

## Adding a Screen

Use this sequence for every new screen:

1. Identify the closest existing screen family and select its `PanelTemplateKind`.
2. Select the `PanelVisualFamily` that expresses the screen's tone without changing geometry.
3. Build the screen from typed shared components.
4. Emit semantic action, modal, focus, controller, settings, and realtime-patch attributes from the typed presentation.
5. Add only content-specific classes or an explicit shared component variant.
6. Add a new `rr-` template only if no existing shell can represent the geometry without violating its contract.
7. Ensure the new RML/RCSS is reachable from `panel.rml` and therefore packaged for native and preloaded for WebAssembly.
8. Add tests for template selection, visual-family classes, pointer actions, controller navigation, focus restoration, adjacent transitions, and relevant modal behavior.
9. Capture native and web RmlUi at the applicable reference viewports.

A code review should reject a new screen that starts by copying another screen's shell, RCSS lane, or modal frame.

## Runtime Assets and Packaging

The runtime resolves `assets/ui/panel.rml` from the real asset root, including installations whose paths contain spaces. Every resource reachable from that document is part of the application:

- native build outputs copy the complete `assets/ui` directory;
- Windows and Linux packages include the same tree;
- Emscripten preloads the tree into its filesystem;
- template-only changes participate in web link dependencies; and
- native package staging and Azure package preparation revalidate the copied dependency graph.

Missing or malformed UI assets are fatal initialization errors. The application logs the exact failed path and exits the UI initialization cleanly rather than showing a partially styled or blank interface.

## Resource Validation

The UI resource validator walks the complete RML/RCSS dependency graph. It must reject:

- missing linked resources;
- duplicate template names;
- templates without exactly one content target;
- dependency cycles;
- malformed RML or RCSS;
- missing persistent host IDs; and
- obsolete `template[data-modal]` markup.

Run `npm.cmd run ui:validate` in local verification and package validation. It is not sufficient for a source build to succeed if the staged package omits a runtime UI dependency.

Node.js is a required build tool because native, web, and package targets all run the same graph validator. The validator requires every template to be linked directly from the initial document head, ignores commented-out markup, requires each persistent host exactly once, and checks the generated RCSS bundle against its authored layer manifest.

## Migration Rules

Migration proceeds by screen family:

1. Keep the persistent document, external assets, structured presentation API, typed modals, and direct semantic binding active for all screens.
2. Move representative screens to their shared shells.
3. Keep unconverted screens in `LegacyRaw` inside the same document.
4. Convert remaining screens in family batches.
5. Delete `LegacyRaw`, raw shell-building helpers, structure inference, modal extraction, generated blanket template removal, and obsolete browser state mirrors after the final family migrates.

`LegacyRaw` is compatibility containment, not a place to add features. Any behavior fix made while a legacy screen remains unconverted must preserve semantic IDs and avoid deepening the raw-markup dependency.

## Review Checklist

Before merging a UI extension, confirm:

- native and web load the same `assets/ui` resources;
- the screen uses an existing template unless its geometry is genuinely new;
- variable content remains in typed C++;
- no shared lane was copied or locally redefined;
- the 736/704/16 edges are intact;
- action and focus bindings come from loaded element attributes;
- modal rules and return focus survive template expansion;
- realtime patches do not reload the document;
- resource and package validation pass; and
- native/web screenshots show the same component hierarchy.
