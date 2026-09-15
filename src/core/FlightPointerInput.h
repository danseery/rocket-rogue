#pragma once

#include "core/UiViewportLayout.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace rocket {
// Final rendered ship pose in logical pixels. Input uses exactly the same
// camera, viewport, surface blend and shake as the visible ship.
struct FlightPointerPresentation {
    bool active = false;
    UiRect viewport;
    double shipX = 0.0, shipY = 0.0;
    double forwardX = 0.0, forwardY = 1.0; // Y up.

    std::optional<double> angleTo(double x, double y) const {
        if (!active || !std::isfinite(x) || !std::isfinite(y) ||
            x < viewport.x || y < viewport.y ||
            x >= uiRectRight(viewport) || y >= uiRectBottom(viewport)) return {};
        const double dx = x - shipX, dy = shipY - y;
        if (std::hypot(dx, dy) < 12.0) return {};
        return std::atan2(forwardX*dy-forwardY*dx, forwardX*dx+forwardY*dy);
    }
};

inline double flightPointerSteer(double angleError, double angularVelocity) {
    // Feedback commands the existing angular acceleration/turn-rate model.
    // Velocity feedback brakes before reaching the heading; no pose snapping.
    return -std::clamp(angleError*3.0-angularVelocity*0.6, -1.0, 1.0);
}
}
