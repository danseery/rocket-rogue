# Straylight parallel berth

Generated with OpenAI ImageGen on 2026-09-22. Style reference: `assets/art/straylight-ark-operational.png`. Output: `straylight-dock.png` (RGBA, 1254 square). No ship, text, targets, or moving clamps are baked into the image.

## Generation prompt

Generate a NEW transparent PNG game sprite, square 1024x1024. Reference image is ONLY the style/material reference of the Straylight Ark: weathered gray segmented armor, crisp pixel art, cyan lights, restrained yellow safety markings. Create an orthographic top-down horizontal hull-side docking recess, not a whole spaceship. Exact silhouette critical: the solid hull occupies rectangle x=10 to1014, y=480 to1014 pixels. Two short solid rectangular end bumpers occupy x=10 to300 and x=724 to1014, y=270 to480. The gap between x=300 and724, y=270 to480 is EMPTY transparent space, opening unobstructed towards the TOP of the image. Thus shallow rectangular U notch with broad horizontal berth, NOT a deep narrow U dock. All space above y270 transparent; all central notch transparent. Flat straight inner edges, no structures protruding into notch. Metallic detailing entirely inside these boundaries; cyan edge lights embedded within solid edges, rich dark gray layered armor matching reference. True transparent background and empty parking space alpha, no black fill, no backdrop, no shadows outside boundaries, no lettering, no ships, no clamps, no HUD or target guide. Ship will be drawn by game horizontally in this berth. Keep geometry crisp, orthographic, no perspective.

## Runtime calibration

The generated notch is approximately x=385..870, y=434..638. The shared profile maps this to x=−0.36..0.36, y=0.12..0.48: 1.5 ship lengths by 1.8 ship widths. Artwork dimensions are 1.862 by 2.213 local units, offset +0.139 along the opening normal. The outer wall extends to y=−0.875. Visual seams, target rectangle, and procedural clamp shoes use this same dock-local coordinate frame. The image is registered in `scene-textures.json` and packed by `tools/build-scene-atlas.py`.

Earth uses its existing profile and artwork. Straylight's opening normal is fixed at +Y, not aimed at the arriving ship. Parallel facing is selected from the saved capture heading; the save schema is unchanged.
