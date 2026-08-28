# Mining Tile ImageGen Prompt Manifest

The runtime mining terrain uses eight generated `1216x64` sheets under
`assets/art/mining`. The selected sources were created with the built-in ImageGen
workflow, cleaned and downsampled to `64x64`, and assembled with
`tools/assemble-mining-tiles.py`.

Post-solar content is body-owned rather than system-paletted. Its canonical manifest is
`assets/art/mining/post-solar-geology-profiles.json`: 32 generated geology families are
assembled into individual `1216x64` inspection sheets and the runtime
`1216x2048` `mining-tiles-post-solar.png` library. Aaru Vale and Khepri Prime generate
saved planet/moon rosters from the nine outer-body portraits; Rift Belt generates saved
minor bodies. Sol continues using the authored destination sheets above.

Build and verify the post-solar library with:

```text
.venv/Scripts/python.exe tools/build-post-solar-mining-library.py \
  assets/art/mining/post-solar-geology-profiles.json assets/art/mining \
  --contact-sheet assets/art/mining/post-solar-contact-28px.png
```

The post-solar assembler checks every left/right and top/bottom pairing among the three
variants in each material band. Its shared overlays preserve the silver Common, gold
Rare, violet Exotic, cyan Artifact/Oxygen, orange Fuel/Thermal, cyan Cryo, lime
Radiation, and magenta Toxic language across every generated body.

## Fixed frame contract

| Frames | Meaning |
|---:|---|
| 0-2 | Regolith variants |
| 3-5 | Hard Rock variants |
| 6-8 | Bedrock variants |
| 9 | Common Ore: silver hexagonal cluster |
| 10 | Rare Ore: gold diamond/star cluster |
| 11 | Exotic Vein: violet triangular crystal |
| 12 | Artifact Cache: cyan octagonal device |
| 13 | Fuel Pocket: orange contained mineral pocket |
| 14 | Oxygen Pocket: cyan circular geode ring |
| 15 | Thermal Hazard: black basalt and orange molten seams |
| 16 | Cryo Hazard: pale crystals, cyan frost, navy joints |
| 17 | Radiation Hazard: dark olive mineral and lime-yellow emission |
| 18 | Toxic Hazard: black-purple mineral and violet-magenta fluid seams |

The sheets are registered as one atlas frame rather than nineteen atlas frames. This
keeps every internal tile on the same atlas page; the renderer selects an internal
frame with normalized source UVs.

## Shared generation contract

Use case: `stylized-concept`. Asset type: production game terrain tile source.
Match the supplied planet/body palette and the existing side-view enemy sprite library.
Use crisp hand-authored-looking 1990s arcade pixel art, a limited palette, chunky
clusters, orthographic rock faces, and consistent top-left lighting. The result must
remain readable when reduced to `64x64` and displayed near `28px`.

Geology boards contain an exact `3x3` grid with no labels, borders, gutters,
perspective, ore, resource icons, hazard colors, transparency, or watermark:

| Destination | Top row / Regolith | Middle row / Hard Rock | Bottom row / Bedrock |
|---|---|---|---|
| Moon | Powder-gray lunar dust | Blue-gray anorthosite and mare rock | Dark charcoal lunar basalt |
| Mars | Rust-red and ochre dust | Iron-rich red volcanic rock | Dark maroon and charcoal basalt |
| Io | Sulfur-yellow and ochre dust | Orange-brown sulfur crust and black basalt | Carbon-black basalt with dormant traces |
| Saturn | Pale-gold powder ice and ring debris | Cream water ice and tan silicates | Dark stone and compressed blue-shadowed ice |
| Uranus | Pale-cyan frost and powder ice | Turquoise water/methane ice | Navy rock and compressed blue ice |
| Neptune | Cobalt powder ice | Royal-blue pressure ice and white-blue crystal | Deep-navy compressed crystalline bedrock |
| Khepri Prime | Deep-teal alien grains | Teal basalt with restrained emerald intrusion | Midnight-teal bedrock and pale-cyan ridges |
| Rift Belt | Cobalt shard dust and fractured ice | Cobalt/obsidian rock with cyan rift cracks | Midnight-blue obsidian shards and cold seams |

Semantic overlays are generated one asset at a time on a genuinely transparent
background. Each uses a centered silhouette with clear margins and no rock background,
text, label, frame, edge-touching particles, or watermark. Resource overlays use the
existing material-marker shapes. Hazard overlays use the corresponding Elemental enemy
sheet as the exact palette and material reference.

### Reference images

| Destination or theme | Repository reference |
|---|---|
| Moon | `assets/art/moon.png` |
| Mars | `assets/art/mars.png` |
| Io / Jupiter | `assets/art/jupiter.png` |
| Saturn | `assets/art/saturn.png` |
| Uranus | `assets/art/uranus.png` |
| Neptune | `assets/art/neptune.png` |
| Khepri Prime | `assets/art/outer-system-planet-01.png` |
| Rift Belt | `assets/art/outer-system-planet-06.png` |
| Thermal | `assets/art/enemies/enemy-elemental-lava-sheet.png` |
| Cryo | `assets/art/enemies/enemy-elemental-ice-sheet.png` |
| Radiation | `assets/art/enemies/enemy-elemental-radioactive-sheet.png` |
| Toxic | `assets/art/enemies/enemy-elemental-toxic-sheet.png` |

Reusable geology prompt template:

```text
Create a production terrain source board for OREBIT using the supplied [DESTINATION]
reference. Match its [PALETTE DIRECTION]. Crisp hand-authored 1990s arcade pixel art,
limited palette, chunky clusters, orthographic rock faces, consistent top-left light.
Exact 3x3 grid: top row three regolith variants, middle row three hard-rock variants,
bottom row three bedrock variants. Every cell must be tileable and contain geology only.
No labels, borders, gutters, perspective, ore, icons, hazard colors, transparency, text,
signature, or watermark. It must remain readable after reduction to 64x64 and at 28px.
```

Reusable semantic-overlay prompt template:

```text
Create one transparent OREBIT mining-grid overlay: [SEMANTIC DESCRIPTION]. Match the
supplied enemy or material visual language. Crisp limited-palette 1990s arcade pixel art,
centered with clear margins and readable at 28px. Transparent background; no geology,
frame, border, text, signature, watermark, or particles touching the image edge.
```

## Assembly and seam requirements

The assembler crops the nine board cells, reduces them to a shared pixel-art palette,
and blends a seven-pixel boundary band toward a common profile for each destination and
material family. Opposite outer edges are then locked to identical pixels across all
three sibling variants. Semantic overlays never touch those borders, so resource and
hazard cells inherit the same compatible geology edge.

Run from the repository root with a directory containing the selected source boards
and overlays using the filenames documented by `tools/assemble-mining-tiles.py`:

```text
.venv/Scripts/python.exe tools/assemble-mining-tiles.py <source-dir> assets/art/mining --contact-sheet <preview.png>
```
