#pragma once

#include "game/RocketGameApp.h"
#include "platform/AppServices.h"

#include <array>
#include <cstddef>

namespace rocket {

class GameRunner {
public:
    explicit GameRunner(AppServices& services);

    bool initialize();
    void frame();
    void resetFrameClock();
    void shutdown();

    RocketGameApp& app();
    const RocketGameApp& app() const;

private:
    static constexpr std::size_t performanceSampleCapacity_ = 120;

    struct SampleSummary {
        double average = 0.0;
        double median = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
    };

    void dispatchPendingHaptic();
    void refreshPreferences(bool force);
    void resetPerformanceSamples();
    void recordPerformanceSample(double deltaSeconds, double cpuMilliseconds);
    SampleSummary summarizeSamples(
        const std::array<double, performanceSampleCapacity_>& samples) const;

    AppServices& services_;
    RocketGameApp app_;
    AppPreferences cachedPreferences_;
    std::uint64_t preferenceRevision_ = 0;
    std::array<double, performanceSampleCapacity_> performanceFrameSamples_ {};
    std::array<double, performanceSampleCapacity_> performanceCpuSamples_ {};
    std::size_t performanceSampleCount_ = 0;
    std::size_t performanceSampleCursor_ = 0;
    double lastFrameSeconds_ = 0.0;
    double lastPerformancePublishSeconds_ = 0.0;
    double startupMilliseconds_ = 0.0;
    bool preferenceCacheInitialized_ = false;
    bool performanceStatsEnabled_ = false;
    bool initialized_ = false;
};

} // namespace rocket
