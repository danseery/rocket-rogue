#pragma once

#include "game/RocketGameApp.h"
#include "performance/PerformanceMetrics.h"
#include "platform/AppServices.h"

#include <optional>

namespace rocket {

class GameRunner {
public:
    explicit GameRunner(AppServices& services);

    bool initialize();
    void frame();
    // Benchmark-only deterministic frame advancement. Shipping gameplay keeps
    // using the monotonic frame delta through frame().
    void frameForBenchmark(double fixedDeltaSeconds);
    void resetFrameClock();
    void shutdown();

    RocketGameApp& app();
    const RocketGameApp& app() const;

private:
    void dispatchPendingHaptic();
    void frameWithDelta(std::optional<double> fixedDeltaSeconds);
    void refreshPreferences(bool force);
    void resetPerformanceSamples();

    AppServices& services_;
    RocketGameApp app_;
    AppPreferences cachedPreferences_;
    std::uint64_t preferenceRevision_ = 0;
    performance::RollingFrameTimingSamples performanceSamples_;
    double lastFrameSeconds_ = 0.0;
    double lastPerformancePublishSeconds_ = 0.0;
    double startupMilliseconds_ = 0.0;
    bool preferenceCacheInitialized_ = false;
    bool performanceStatsEnabled_ = false;
    bool initialized_ = false;
};

} // namespace rocket
