# Post-Solar Procedural Bodies

## Scope

Sol remains an authored destination sequence with its existing Moon-through-Neptune
mining sheets. Aaru Vale and Khepri Prime are star systems, not palettes. Rift Belt is
a later procedural region. Each system generates a stable roster of planets, moons,
giants, or minor bodies; each mineable body owns its portrait archetype, surface
geology, deep geology, hazard bias, and deterministic seed.

This layer is presentation and world identity. Existing mining terrain generation,
reward caps, collisions, progression bands, and scenario rules remain authoritative.

## Deterministic roster rules

| Region | Generated roster | Mining rule |
|---|---|---|
| Aaru Vale | 4-6 primary bodies and 3-7 moons | First three primaries are landable. Later primaries may be non-mineable giants. Intended for late Act 1 discovery without hostile encounters. |
| Khepri Prime | 3-5 primary bodies and 2-8 moons | First three primaries are landable. Later primaries may be giants. Intended for Act 2 hostile-system mining. |
| Rift Belt | 4-8 fragments | Every fragment is a mineable minor body. Intended for Act 3. |

Rosters are generated from the campaign seed, system ID, and generator version, then
stored in save data. Once discovered, a body's name, parent, portrait, geology, and seed
do not reroll when the player reloads or revisits it.

## Portrait-to-geology compatibility

The nine existing `outer-system-planet-01.png` through `-09.png` portraits are visual
archetypes. Each archetype selects only from its three compatible geology families:

| Portrait | Compatible families |
|---:|---|
| 01 | Island Basalt, Coral Limestone, Jade Sediment |
| 02 | Cyan Fracture Basalt, Nickel-Iron Crater, Voidglass Breccia |
| 03 | Violet Crystal, Ammonia Ice, Pale Silicate |
| 04 | Sulfur Salt, Radiation Glass, Toxic Tarstone |
| 05 | Molten Obsidian, Black Scoria, Basalt Columns |
| 06 | Water Ice, Clathrate Ice, Cryovolcanic Slush |
| 07 | Gold Regolith, Salt Evaporite, Desert Sandstone |
| 08 | Teal Xenobasalt, Opaline Crystal, Methane Ice |
| 09 | Ferric Breccia, Copper Oxide, Carbonaceous Chondrite |

Rift fragments draw from Rift Obsidian, Phase Glass, Cobalt Shard Ice, Voidstone, and
Resonant Gold. A body usually keeps one family at all depths; a deterministic minority
switches to another compatible family below its entry layer.

## Runtime contract

- The 32 geology families are rows in
  `assets/art/mining/mining-tiles-post-solar.png` (`1216x2048`).
- Every row preserves the existing 19-frame material/affinity mapping.
- Cell variants hash body seed, coordinates, material, and destination tier, so repeated
  runs remain visually stable without storing a tile choice per cell.
- Revealed terrain still submits one textured base batch; fog remains a separate solid
  batch. Per-cell draw calls are not introduced.
- If the post-solar atlas texture is not ready, revealed cells use the existing flat
  material colors. Fog never reveals hidden material through the fallback.
- Local light, damage integrity, reveal animation, and alpha tint the generated art;
  destination/body palettes are otherwise preserved.

## Authoring and debug workflow

`assets/art/mining/post-solar-geology-profiles.json` is the canonical row and prompt
manifest. `tools/build-post-solar-mining-library.py` converts its ImageGen boards into
edge-compatible individual sheets, the runtime library, and an optional 28px contact
sheet. It rejects any sibling edge pairing that would create a seam.

The web Mining Arena Lab exposes a Post-solar system and Mineable body selector. The
preview lists the deterministic body name, portrait, surface family, and deep family;
launching remains a sandbox and does not write campaign save data.
