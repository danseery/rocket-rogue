# Rocket Rogue Art Assets

90s arcade-style proof-of-concept sprites derived from the provided GenAI source images.

- `earth.png` - Earth sprite with alpha.
- `moon.png` - Moon sprite with alpha.
- `mars.png` - Mars sprite with alpha.
- `mercury.png` - Mercury sprite with alpha, packed on a 512x512 square.
- `venus.png` - Venus sprite with alpha, packed on a 512x512 square.
- `jupiter.png` - Jupiter sprite with alpha, packed on a 512x512 square.
- `saturn.png` - Saturn sprite with alpha, packed on a 512x512 square.
- `uranus.png` - Uranus sprite with alpha, packed on a 512x512 square.
- `neptune.png` - Neptune sprite with alpha, packed on a 512x512 square.
- `ark-operational.png` - Operational Ark sprite with alpha, packed on a 1254x1254 square.
- `ark-damaged.png` - Damaged Ark sprite with alpha, packed on a 1254x1254 square.
- `rocket.png` - Player rocket sprite with alpha and no active thrust.
- `thrust-sheet.png` - 6-frame horizontal thrust animation sheet with alpha, extracted from the original rocket flame.
- `explosion-sheet.png` - 8-frame horizontal explosion sprite sheet with alpha.
- `mining-drone.png` - Mining drone sprite with alpha, packed on a 512x512 square.
- `drill-bit-sheet.png` - 6-frame horizontal drill bit animation sheet with alpha, packed as fixed 256x256 frames.
- `chroma-key.png` - 64x64 artist reference swatch for the `#ff00aa` chroma-key background.
- `outer-system-planet-01.png` through `outer-system-planet-09.png` - Future post-solar-system planet/moon sprites with alpha, packed on fixed 1254x1254 squares.
- `outer-system-moon-01.png` through `outer-system-moon-10.png` - Future post-solar-system moon sprites with alpha, packed on fixed 1254x1254 squares.

The source images used a magenta chroma-key background. The committed PNGs are cleaned, cropped, and packed for the WebGL renderer.
