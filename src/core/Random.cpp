#include "core/Random.h"

#include <algorithm>
#include <limits>

namespace rocket {

Random::Random(std::uint64_t seed)
    : state_(seed == 0 ? 0xC0DEC0FFEEULL : seed)
{
}

std::uint64_t Random::nextU64()
{
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

double Random::next01()
{
    constexpr double scale = 1.0 / static_cast<double>(std::numeric_limits<std::uint64_t>::max());
    return static_cast<double>(nextU64()) * scale;
}

double Random::range(double minValue, double maxValue)
{
    return minValue + (maxValue - minValue) * next01();
}

int Random::rangeInt(int minInclusive, int maxInclusive)
{
    if (maxInclusive <= minInclusive) {
        return minInclusive;
    }

    const auto span = static_cast<std::uint64_t>(maxInclusive - minInclusive + 1);
    return minInclusive + static_cast<int>(nextU64() % span);
}

bool Random::chance(double probability)
{
    const double clamped = std::clamp(probability, 0.0, 1.0);
    return next01() < clamped;
}

} // namespace rocket

