# Rocket Rogue Art Asset Inventory

The committed PNGs are 90s arcade-style proof-of-concept sprites derived from provided GenAI source images. They are decoded through the platform texture source and drawn by the shared `OpenGlRenderer`: browser builds load them asynchronously, while native builds decode them from the packaged `assets/art` directory.

## Registered runtime textures

`src/render/OpenGlRenderer.cpp` currently registers all 33 textures in this section. A missing or corrupt registered texture is a startup error in native builds.

| Files | Dimensions | Runtime use |
|---|---:|---|
| `earth.png`, `moon.png`, `mars.png` | 512x512 each | Early solar-system bodies. |
| `mercury.png`, `venus.png`, `jupiter.png`, `saturn.png`, `uranus.png`, `neptune.png` | 512x512 each | Local solar-map bodies. |
| `straylight-ark-operational.png`, `straylight-ark-damaged.png` | 1254x1254 each | Current Straylight Ark operational/damaged states. |
| `rocket-bay-closed.png`, `rocket-bay-open.png` | 1024x1024 each | Player shuttle with the mining-drone bay sealed/open. |
| `thrust-sheet.png` | 768x128 | Six horizontal 128x128 thrust frames. |
| `explosion-sheet.png` | 1024x128 | Eight horizontal 128x128 explosion frames. |
| `local-solar-bg-sheet.png` | 4096x576 | Four horizontal 1024x576 local-solar backgrounds. |
| `mining-drone.png` | 512x512 | Player mining rig. |
| `drill-bit-sheet.png` | 672x112 | Six horizontal 112x112 drill frames. |
| `mini-drone-mining.png`, `mini-drone-resource.png`, `mini-drone-survey.png`, `mini-drone-hazard.png`, `mini-drone-attack.png`, `mini-drone-defense.png` | 512x512 each | Mining, logistics, survey, remediation, and passive-combat support drones. |
| `outer-system-planet-01.png` through `outer-system-planet-09.png` | 1254x1254 each | Post-solar destination variants selected by destination tier. |

## Not registered by the renderer

These files are not requested by `OpenGlRenderer` and have no current source-code references. They remain in the repository as authoring references, superseded alternatives, or future content. Because CMake currently copies the entire `assets` directory, they are still included in native and web packages.

| Files | Dimensions | Status |
|---|---:|---|
| `chroma-key.png` | 64x64 | Artist reference swatch for the `#ff00aa` chroma-key background. |
| `rocket.png` | 512x512 | Legacy shuttle alternative; runtime uses the two `rocket-bay-*` states. |
| `ark-operational.png`, `ark-damaged.png` | 1254x1254 each | Legacy Ark alternatives; runtime uses the named `straylight-ark-*` states. |
| `local-solar-bg.png` | 1024x576 | Single-frame background reference; runtime uses `local-solar-bg-sheet.png`. |
| `outer-system-moon-01.png` through `outer-system-moon-10.png` | 1254x1254 each | Future moon variants with no current runtime registration. |

Do not assume that an unregistered file is safe to delete solely from this list: first decide whether future-content and authoring references should be preserved. If packaging size becomes the priority, register an explicit runtime manifest or copy only the registered files instead of relying on the whole-directory copy.

## Import notes

The source images used a magenta chroma-key background. Committed runtime sprites are cleaned, cropped, and packed with alpha.

To clean residual chroma from an already packed sprite without changing its layout, run:

```text
tools/import-chroma-sprite.py source.png destination.png --preserve-layout
```

When adding or replacing a runtime texture, update this inventory and the asset table/size in `src/render/OpenGlRenderer.cpp` and `src/render/OpenGlRenderer.h` together. Verify both the web asynchronous loader and native PNG decoder paths.
