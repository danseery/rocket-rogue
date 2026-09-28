#pragma once

namespace rocket {

// Logical viewport pixels from the same camera frame that drew the target.
struct SceneInteractionAnchor {
    bool visible = false;
    float x = 0.0F;
    float y = 0.0F;
    // Conservative screen-space artwork bounds, including rotation.
    float halfWidth = 0.0F;
    float halfHeight = 0.0F;
};

struct SceneInteractionAnchors {
    SceneInteractionAnchor target;
    SceneInteractionAnchor ship;
    SceneInteractionAnchor player;
    float shipRadiusX = 0.0F;
    float shipRadiusY = 0.0F;
};

} // namespace rocket
