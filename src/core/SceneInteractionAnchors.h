#pragma once

namespace rocket {

// Logical viewport pixels from the same camera frame that drew the target.
struct SceneInteractionAnchor {
    bool visible = false;
    float x = 0.0F;
    float y = 0.0F;
};

struct SceneInteractionAnchors {
    SceneInteractionAnchor target;
    SceneInteractionAnchor ship;
    SceneInteractionAnchor dock;
};

} // namespace rocket
