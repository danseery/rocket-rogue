#pragma once

#include <cstdint>

namespace rocket {

enum class FrameLimitMode {
    PlatformDefault,
    Smooth60,
    Balanced,
    Battery30,
    Display,
};

struct FrameLimitResolution {
    FrameLimitMode mode = FrameLimitMode::PlatformDefault;
    double activeRefreshRateHz = 0.0;
    double nominalTargetFramesPerSecond = 0.0;
    double targetFramesPerSecond = 0.0;
    std::uint32_t refreshDivisor = 0;
    bool refreshRateKnown = false;
    bool exactModeTarget = false;
    bool usedRefreshCompatibleFallback = false;
};

// Every target returned for a known refresh is refresh/divisor. This prevents
// uneven presentation cadence even when the requested 60, 45, 40, or 30 FPS
// rate is not compatible with the active display mode.
FrameLimitResolution resolveFrameLimit(
    FrameLimitMode mode,
    double activeRefreshRateHz,
    bool runningOnSteamDeck);

struct FrameDeadlineDecision {
    std::uint64_t delayNanoseconds = 0;
    std::uint64_t targetIntervalNanoseconds = 0;
    std::uint64_t observedPresentIntervalNanoseconds = 0;
    bool platformLimiterSatisfied = false;
    bool platformLimiterStricter = false;
    bool deadlineRebased = false;
};

// Feed this helper the timestamp immediately after each completed present. It
// schedules the next CPU frame start on an absolute monotonic cadence. FIFO is
// then given the elapsed sleep interval to retire an image before acquisition,
// avoiding the two-frame submission bursts possible with a triple-buffered
// swapchain. A stricter platform/Gamescope limiter naturally misses the app's
// deadline and therefore receives no additional delay.
class PresentIntervalDeadline {
public:
    void configure(double targetFramesPerSecond);
    void reset();
    FrameDeadlineDecision presented(std::uint64_t nowNanoseconds);

    bool active() const;
    std::uint64_t targetIntervalNanoseconds() const;

private:
    std::uint64_t targetIntervalNanoseconds_ = 0;
    std::uint64_t nextDeadlineNanoseconds_ = 0;
    std::uint64_t lastPresentNanoseconds_ = 0;
    std::uint64_t lastScheduledDelayNanoseconds_ = 0;
    bool hasPresented_ = false;
};

struct SteamDeckRuntimeDetection {
    bool queryAvailable = false;
    bool runningOnSteamDeck = false;
};

// No Steamworks type or library is referenced until bindSteamUtils() is
// instantiated by a target that already has access to ISteamUtils.
class SteamDeckRuntimeDetector {
public:
    using QueryFunction = bool (*)(void*) noexcept;

    SteamDeckRuntimeDetector() = default;
    SteamDeckRuntimeDetector(void* context, QueryFunction query) noexcept;

    template <typename SteamUtils>
    static SteamDeckRuntimeDetector bindSteamUtils(SteamUtils* steamUtils) noexcept
    {
        if (steamUtils == nullptr) return {};
        return SteamDeckRuntimeDetector(
            steamUtils,
            [](void* context) noexcept {
                return static_cast<SteamUtils*>(context)->IsSteamRunningOnSteamDeck();
            });
    }

    SteamDeckRuntimeDetection detect() const noexcept;
    void clear() noexcept;

private:
    void* context_ = nullptr;
    QueryFunction query_ = nullptr;
};

} // namespace rocket
