#pragma once

#include <cstdint>

namespace rocket {

class Random {
public:
    explicit Random(std::uint64_t seed = 0xC0DEC0FFEEULL);

    std::uint64_t nextU64();
    double next01();
    double range(double minValue, double maxValue);
    int rangeInt(int minInclusive, int maxInclusive);
    bool chance(double probability);

    std::uint64_t state() const { return state_; }

private:
    std::uint64_t state_;
};

} // namespace rocket
