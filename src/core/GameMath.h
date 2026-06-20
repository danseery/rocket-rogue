#pragma once

#include <algorithm>

namespace rocket::math {

inline double clampUnit(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

inline double smoothStep(double value)
{
    const double t = clampUnit(value);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace rocket::math
