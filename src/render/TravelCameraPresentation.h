#pragma once

#include <algorithm>
#include <cmath>

namespace rocket::travel_camera {
inline constexpr double goldenInset = 0.3819660112501051;
inline constexpr double acquireSpeed = 5.0;
inline constexpr double releaseSpeed = 3.0;
inline constexpr double acquireSeconds = 1.5;
inline constexpr double smoothingSeconds = 1.5;
inline constexpr double directionCosine = 0.9396926207859084; // 20 degrees
inline constexpr double safePaddingPixels = 24.0;
inline constexpr float shipRenderSize = .17F * 1.35F;

struct Offset { double x = 0, y = 0; }; // viewport fractions, positive Y up

inline Offset targetOffset(double vx, double vy, double width, double height,
    double shipHalfSizePixels)
{
    width = std::max(1.0, width); height = std::max(1.0, height);
    const double nx = vx / width, ny = vy / height;
    const double extent = std::max(std::abs(nx), std::abs(ny));
    if (extent < 1e-12) return {};
    const double margin = shipHalfSizePixels + safePaddingPixels;
    const double boundX = std::min(.5-goldenInset, std::max(0.0, .5-margin/width));
    const double boundY = std::min(.5-goldenInset, std::max(0.0, .5-margin/height));
    const double ray = std::min(std::abs(nx) > 1e-12 ? boundX/std::abs(nx) : 1e12,
        std::abs(ny) > 1e-12 ? boundY/std::abs(ny) : 1e12);
    return {-nx*ray, -ny*ray};
}

struct State {
    Offset offset, direction;
    double stableSeconds = 0;
    bool hasDirection = false, active = false;

    void update(double dt, double vx, double vy, double speedMetersPerSecond,
        bool eligible, double width, double height, double shipHalfSizePixels)
    {
        dt = std::clamp(dt, 0.0, .1);
        const double length = std::hypot(vx, vy);
        double activeDt = dt;
        if (!eligible || speedMetersPerSecond < releaseSpeed || length < 1e-12) {
            active = hasDirection = false; stableSeconds = 0;
        } else {
            const Offset unit{vx/length, vy/length};
            if (!hasDirection || unit.x*direction.x + unit.y*direction.y < directionCosine) {
                direction = unit; hasDirection = true;
                active = false; stableSeconds = 0;
            }
            if (!active) {
                const double previous = stableSeconds;
                stableSeconds = speedMetersPerSecond >= acquireSpeed ? stableSeconds+dt : 0;
                if (stableSeconds + 1e-9 >= acquireSeconds) {
                    active = true;
                    activeDt = std::clamp(previous+dt-acquireSeconds, 0.0, dt);
                }
            }
        }
        // Split the acquisition frame so settling is identical at 30/60/120 Hz.
        const double relax = std::exp(-(dt-(active ? activeDt : 0.0))/smoothingSeconds);
        offset.x *= relax; offset.y *= relax;
        if (active) {
            const auto target = targetOffset(vx,vy,width,height,shipHalfSizePixels);
            const double blend = -std::expm1(-activeDt/smoothingSeconds);
            offset.x = std::lerp(offset.x,target.x,blend);
            offset.y = std::lerp(offset.y,target.y,blend);
        }
        const double margin = shipHalfSizePixels+safePaddingPixels;
        const double bx = std::max(0.0,.5-margin/std::max(1.0,width));
        const double by = std::max(0.0,.5-margin/std::max(1.0,height));
        offset.x = std::clamp(offset.x,-bx,bx);
        offset.y = std::clamp(offset.y,-by,by);
    }
};
}
