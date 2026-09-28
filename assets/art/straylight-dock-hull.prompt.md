# Straylight dock hull extension

Generated with the built-in OpenAI ImageGen tool on 2026-09-28. Final asset: `straylight-dock-hull.png` (1536x1024 RGBA). Source alpha is preserved; an opaque runtime underlay ensures the solid metal never reveals the starfield.

## Generation prompt

Use case: stylized-concept
Asset type: transparent game environment sprite, Straylight hull extension.
Create one large, broad rectangular section of a massive starship's grey hull, seen straight down in strict orthographic view. It will connect directly BELOW an existing small docking recess, making that berth feel attached to the much larger Straylight Ark. A solid uninterrupted slab: broad flat straight top edge, straight side edges, flat bottom edge, no cutouts or windows through the hull. Entire rectangular interior opaque. The hull should fill almost the entire landscape canvas, aspect ratio about 3:2, with only a thin transparent outer margin. Weathered medium cool grey metal armour, large segmented panels, dark seams and inset ventilation, sparse tiny cyan status lights. Crisp richly detailed 1990s arcade pixel-art sprite with hard edges, same material language as an industrial grey docking station. Large armour plates and restrained details readable when scaled down. Top edge is a broad clean connection surface that can tuck just behind a smaller dock, without a new docking notch. No perspective, no isometric view, no rounded spaceship silhouette, no backdrop, no shadows outside hull, no stars, no text, no markings, no ship, no HUD, no holes. True transparent background outside the single solid rectangular hull. This is a replaceable asset named straylight-dock-hull.png.

## Runtime calibration

The opaque hull is approximately x=15..1522, y=63..961. This rectangle maps to dock-local x=-2.35..2.35, y=-2.85..-0.35. It overlaps the lower berth base, which ends at y=-0.875; the berth is drawn over the hull. The complete hull and shuttle collision envelope remain inside the existing approach radius. A single exterior collision contour avoids internal seams between the hull and berth. The parking recess, alignment requirements, capture timing, and save fields are unchanged.

Replace this named PNG with human art using the same canvas/opaque bounds, then regenerate the shared scene atlas. No ship, controls, or mission text are baked into the asset.
