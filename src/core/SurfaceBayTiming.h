#pragma once

#include <algorithm>
#include <cstddef>

namespace rocket::surface_bay_timing {

inline constexpr double touchdownSeconds = 2.0;
inline constexpr double deploymentSeconds = 3.0;
inline constexpr double shipOnlyDepartureSeconds = 1.85;
inline constexpr double droneFlightSeconds = 0.70;

// Sound, haptic and visual choreography share the same deterministic fan.
// All equipped drones finish by 1.75s, regardless of the current bay size.
inline double droneLaunchSeconds(std::size_t index, std::size_t count)
{
    const double share = count > 1
        ? static_cast<double>(index) / static_cast<double>(count - 1) : 0.0;
    return 0.65 + std::clamp(share, 0.0, 1.0) * 0.40;
}

} // namespace rocket::surface_bay_timing
