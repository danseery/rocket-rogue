#pragma once
#include "core/UiViewportLayout.h"

namespace rocket {
// One resolved transform for the terrain, ship, actors and their scene controls.
struct SurfaceCameraPresentation {
    bool active = false;
    UiRect viewport;
    float progress = 0.0F;
    float left = 0.0F;
    float top = 0.0F;
    float cellWidth = 0.0F;
    float cellHeight = 0.0F;
    int frameTopRow = 0;
    double lastTime = -1.0;
    double followX = 0.0;
    double followY = 0.0;
};
}
