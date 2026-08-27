# Enemy Sprite Prompt Manifest

This manifest records the reproducible generation inputs for the first themed enemy library. Generated contact sheets are review inputs; the cleaned `2560x128` sheets under `assets/art/enemies/` are the runtime source of truth.

## Shared neutral prompt

Generate one transparent/chroma-backed pixel-art animation contact sheet for OREBIT, a **side-view** 2D mining game. The enemy is shown in a strict side-on profile facing right in every cell—never top-down, three-quarter overhead, or isometric. Use a fixed `4 columns x 5 rows` layout with exactly one full creature per cell. Rows are Idle, Move, Attack, Hit, and Defeat; columns are four sequential animation poses. Preserve one anatomy, scale, pivot, and baseline across all twenty cells. Use hard-edged 1990s arcade pixel art readable at 32 pixels. No labels, grid, border, terrain, shadow plate, UI, health bar, projectile, aura, or duplicate creature. Use transparent pixels or flat `#ff00aa` chroma outside the subject.

Append the archetype clause from [ENEMY_SPRITE_LIBRARY.md](ENEMY_SPRITE_LIBRARY.md#prompt-bible). Flying and Elemental use a stable centered hover pivot; all other archetypes use one shared ground baseline.

## Shared theme-edit prompt

Edit the attached approved Neutral contact sheet. Preserve every pose, anatomy, silhouette, dimensions, facing direction, pivot, framing, cell layout, and animation timing exactly. Change only the creature's material and palette to the requested theme. Keep the background transparent/chroma and do not add effects, projectiles, ground, labels, borders, or UI.

## Asset records

| Archetype | Theme sources | Generation mode | Runtime outputs |
|---|---|---|---|
| Ant | `ant-neutral.png`, then Neutral-reference edits | Neutral text-to-image + four image edits | `enemy-ant-{neutral,lava,ice,radioactive,toxic}-sheet.png` |
| Flying | `flying-neutral.png`, then Neutral-reference edits | Neutral text-to-image + four image edits | `enemy-flying-{neutral,lava,ice,radioactive,toxic}-sheet.png` |
| Beetle | `beetle-neutral.png`, then Neutral-reference edits | Neutral text-to-image + four image edits | `enemy-beetle-{neutral,lava,ice,radioactive,toxic}-sheet.png` |
| Elemental | `elemental-neutral.png`, then Neutral-reference edits | Neutral text-to-image + four image edits | `enemy-elemental-{neutral,lava,ice,radioactive,toxic}-sheet.png` |
| Mammal | `mammal-neutral.png`, then Neutral-reference edits | Neutral text-to-image + four image edits | `enemy-mammal-{neutral,lava,ice,radioactive,toxic}-sheet.png` |
| Spawner | `spawner-neutral.png`, then Neutral-reference edits | Neutral text-to-image + four image edits | `enemy-spawner-{neutral,lava,ice,radioactive,toxic}-sheet.png` |

Theme edits use the material clauses below:

- Lava: black basalt and charcoal shell with orange molten seams.
- Ice: pale crystalline armor, cyan frost, and deep navy joints.
- Radioactive: dark/olive mineral body with controlled lime-yellow emission.
- Toxic: corroded black-purple body with violet-magenta fluid seams.

All thirty sheets were processed by `tools/import-enemy-spritesheet.py`, which shares the repository's `#ff00aa` removal predicate. The importer validates twenty non-empty frames, fixed dimensions, alpha corners, the 112-pixel envelope, common pivot alignment, and residual chroma before writing a runtime asset.

## Rejected direction

An initial top-down neutral concept batch was rejected after in-game review because the mining scene uses a side view. Those concepts are not registered, imported, or packaged. The side-view/right-facing rule above is mandatory for replacements and future archetypes.
