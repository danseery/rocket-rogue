# Tunnel backdrop

`tunnel-backdrop.png` was generated with the built-in OpenAI image-generation tool on 2026-09-13, explicitly requested by the user for excavated-area atmosphere. Original generated PNG retained without image edits; the renderer dims it to keep foreground gameplay readable.

## Generation prompt

Use case: stylized-concept. Asset type: seamless tileable 2D game underground background texture, square. Create a shadowy distant rocky tunnel back wall for a pixel-art space mining game. Almost black charcoal and desaturated blue-gray rock with faint irregular strata, cracks, and broad rocky facets barely catching dim ambient light. Low contrast, atmospheric, quiet enough that brightly colored foreground terrain and mining drones remain clearly readable. Orthographic straight-on flat wall; rock covers the entire image, seamless on all edges, no perspective vanishing point, no tunnel entrance, no foreground silhouettes. Subtle pixel-art texture consistent with detailed retro sci-fi sprites. No stars or sky (game renders those separately above the horizon), no ore or gems, no lights, no objects, no text, no watermark.

## Integration

Registered as `MiningTunnelBackdrop` in the shared native/web scene atlas. Rendered behind terrain and actors, anchored to the site's surface horizon. Surface sky retains the same star frame and opacity as landing. Deeper layers are entirely underground. This is decorative only and adds no collision or discoverable resources.
