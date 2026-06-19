#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rocket {

class Random {
public:
    explicit Random(std::uint64_t seed = 0xC0DEC0FFEEULL);

    std::uint64_t nextU64();
    double next01();
    double range(double minValue, double maxValue);
    int rangeInt(int minInclusive, int maxInclusive);
    bool chance(double probability);

    template <typename T>
    const T& pick(const std::vector<T>& values)
    {
        return values[static_cast<std::size_t>(rangeInt(0, static_cast<int>(values.size()) - 1))];
    }

    std::uint64_t state() const { return state_; }

private:
    std::uint64_t state_;
};

} // namespace rocket
