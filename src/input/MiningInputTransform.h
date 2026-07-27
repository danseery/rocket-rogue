#pragma once

#include "core/UiViewportLayout.h"

#include <algorithm>

namespace rocket {

struct MiningPointerAim {
    double x = 0.0;
    double y = 0.0;
    bool valid = false;
};

// Convert a logical-pixel pointer position into the same aspect-corrected
// direction space used by the controller's right stick. Both native SDL and
// the browser pass their logical viewport coordinates through this one
// transform, keeping the protected mining HUD lanes out of the calculation.
inline MiningPointerAim miningPointerAimFromViewport(
    double pointerX,
    double pointerY,
    int viewportWidth,
    int viewportHeight) noexcept
{
    const UiRect scene = resolveUiViewportLayout(
        viewportWidth,
        viewportHeight,
        UiSurfaceKind::Mining).sceneRect;
    if (scene.width <= 0 || scene.height <= 0) {
        return {};
    }

    const double halfWidth = static_cast<double>(scene.width) * 0.5;
    const double halfHeight = static_cast<double>(scene.height) * 0.5;
    const double centerX = static_cast<double>(scene.x) + halfWidth;
    const double centerY = static_cast<double>(scene.y) + halfHeight;
    return {
        std::clamp((pointerX - centerX) / halfWidth, -1.0, 1.0),
        std::clamp((pointerY - centerY) / halfHeight, -1.0, 1.0),
        true
    };
}

} // namespace rocket
