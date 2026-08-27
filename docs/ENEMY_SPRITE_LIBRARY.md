# Themed Animated Enemy Sprite Library

## Runtime contract

Mining enemies use side-view, right-facing pixel sprites that match the mining scene's camera. The renderer mirrors mobile enemies horizontally when they move or attack left; it never rotates grounded bodies through the screen. Spawners remain upright.

Each archetype has Neutral, Lava, Ice, Radioactive, and Toxic sheets. A runtime sheet is a transparent `2560x128` PNG containing twenty `128x128` frames:

| Frames | Clip | Playback |
|---:|---|---:|
| 0-3 | Idle | 6 fps loop |
| 4-7 | Move | 10 fps loop |
| 8-11 | Attack | 12 fps one-shot |
| 12-15 | Hit | 16 fps one-shot |
| 16-19 | Defeat | 10 fps one-shot |

Grounded archetypes share a 120 px baseline. Flying and Elemental sheets use a centered hover pivot. The importer applies one scale to every frame in a sheet, fits the largest pose inside a `112x112` envelope, and aligns the actual opaque subject after scaling.

## Prompt bible

Neutral master prompt requirements:

- SIDE-VIEW 2D mining game; strict side-on profile, never top-down or isometric.
- Right-facing actor in every cell.
- Exactly four columns by five rows: Idle, Move, Attack, Hit, Defeat.
- Consistent anatomy, scale, ground baseline or hover pivot, and frame margins.
- Hard-edged 1990s arcade pixel art readable at 32 px.
- Flat `#ff00aa` chroma or transparent background.
- No labels, borders, grid, ground strip, UI, health bars, projectiles, aura fields, or duplicate subjects.

The archetype clause preserves the gameplay silhouette:

- Ant: low segmented six-legged melee unit.
- Flying: narrow winged ranged predator.
- Beetle: broad slow armored carapace and horn.
- Elemental: hovering asymmetric shard body with a forward wedge.
- Mammal: heavy mole/badger-like burrower with shovel foreclaws.
- Spawner: rooted armored brood nest with an opening facing right.

Theme edit prompts always reference the approved Neutral master and say: preserve every pose, anatomy, silhouette, dimensions, pivot, framing, layout, and timing; change only materials and palette.

| Theme | Material language |
|---|---|
| Neutral | Rust mineral, dark iron, muted organic brown |
| Lava | Black basalt, charcoal, orange molten seams |
| Ice | Pale crystalline armor, cyan frost, deep navy joints |
| Radioactive | Dark/olive mineral, lime-yellow emissions |
| Toxic | Corroded black-purple body, violet-magenta fluid seams |

## Import and provenance

Raw generated contact sheets are review inputs and are not packaged. Approved runtime sheets live under `assets/art/enemies/`. Import one source with:

```text
.venv/Scripts/python.exe tools/import-enemy-spritesheet.py source.png assets/art/enemies/enemy-ant-neutral-sheet.png
```

Use `--floating` for Flying and Elemental. The importer reuses the shared chroma predicate from `tools/import-chroma-sprite.py` and rejects empty frames, residual chroma, opaque corners, envelope clipping, invalid dimensions, and excessive packed pivot drift.

Generation mode for this library: six neutral text-to-image contact sheets, followed by twenty-four image-edit theme variants referencing the corresponding neutral master. Human review remains required before replacing any approved runtime sheet.

The per-archetype provenance and reusable generation templates are recorded in [ENEMY_SPRITE_PROMPT_MANIFEST.md](ENEMY_SPRITE_PROMPT_MANIFEST.md).

## Gameplay boundary

The site theme selects ordinary enemy cosmetics. Elementals and true elites (explicit swarm elites, Minibosses, and Bosses) receive the site's existing affinity mechanics. Health bars, attack tells, projectiles, elite marks, and affinity auras remain renderer-driven for readability.
