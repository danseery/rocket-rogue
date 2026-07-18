#include "platform/FrameLimitPolicy.h"

#include <cassert>
#include <cmath>
#include <cstdint>

namespace {

bool approximately(double lhs, double rhs, double tolerance = 0.001)
{
    return std::abs(lhs - rhs) <= tolerance;
}

struct FakeSteamUtils {
    bool deck = false;
    bool IsSteamRunningOnSteamDeck() { return deck; }
};

} // namespace

int main()
{
    using namespace rocket;

    FrameLimitResolution limit = resolveFrameLimit(
        FrameLimitMode::PlatformDefault, 144.0, false);
    assert(limit.refreshRateKnown);
    assert(approximately(limit.targetFramesPerSecond, 144.0));
    assert(limit.refreshDivisor == 1 && limit.exactModeTarget);

    limit = resolveFrameLimit(FrameLimitMode::PlatformDefault, 60.0, true);
    assert(approximately(limit.nominalTargetFramesPerSecond, 60.0));
    assert(approximately(limit.targetFramesPerSecond, 60.0));
    assert(limit.refreshDivisor == 1 && limit.exactModeTarget);

    // A Deck running its 90 Hz mode cannot present 60 FPS evenly, so the Deck
    // default resolves to the nearest stable rate at or below 60 instead.
    limit = resolveFrameLimit(FrameLimitMode::PlatformDefault, 90.0, true);
    assert(approximately(limit.targetFramesPerSecond, 45.0));
    assert(limit.refreshDivisor == 2 && limit.usedRefreshCompatibleFallback);

    limit = resolveFrameLimit(FrameLimitMode::Smooth60, 120.0, false);
    assert(approximately(limit.targetFramesPerSecond, 60.0));
    assert(limit.refreshDivisor == 2 && limit.exactModeTarget);
    limit = resolveFrameLimit(FrameLimitMode::Smooth60, 144.0, false);
    assert(approximately(limit.targetFramesPerSecond, 48.0));
    assert(limit.refreshDivisor == 3 && limit.usedRefreshCompatibleFallback);

    // Balanced exposes 45/40 only when they divide the active refresh. Other
    // modes still receive the closest stable refresh divisor.
    limit = resolveFrameLimit(FrameLimitMode::Balanced, 90.0, true);
    assert(approximately(limit.targetFramesPerSecond, 45.0));
    assert(limit.refreshDivisor == 2 && limit.exactModeTarget);
    limit = resolveFrameLimit(FrameLimitMode::Balanced, 120.0, true);
    assert(approximately(limit.targetFramesPerSecond, 40.0));
    assert(limit.refreshDivisor == 3 && limit.exactModeTarget);
    limit = resolveFrameLimit(FrameLimitMode::Balanced, 60.0, true);
    assert(approximately(limit.targetFramesPerSecond, 30.0));
    assert(limit.refreshDivisor == 2 && limit.usedRefreshCompatibleFallback);
    limit = resolveFrameLimit(FrameLimitMode::Balanced, 144.0, false);
    assert(approximately(limit.targetFramesPerSecond, 48.0));
    assert(limit.refreshDivisor == 3 && limit.usedRefreshCompatibleFallback);

    limit = resolveFrameLimit(FrameLimitMode::Battery30, 60.0, true);
    assert(approximately(limit.targetFramesPerSecond, 30.0));
    assert(limit.refreshDivisor == 2 && limit.exactModeTarget);
    limit = resolveFrameLimit(FrameLimitMode::Battery30, 144.0, false);
    assert(approximately(limit.targetFramesPerSecond, 28.8));
    assert(limit.refreshDivisor == 5 && limit.usedRefreshCompatibleFallback);

    limit = resolveFrameLimit(FrameLimitMode::Balanced, 89.91, true);
    assert(approximately(limit.targetFramesPerSecond, 44.955));
    assert(limit.refreshDivisor == 2 && limit.exactModeTarget);

    limit = resolveFrameLimit(FrameLimitMode::Display, 240.0, false);
    assert(approximately(limit.targetFramesPerSecond, 240.0));
    assert(limit.refreshDivisor == 1);

    limit = resolveFrameLimit(FrameLimitMode::PlatformDefault, 0.0, false);
    assert(!limit.refreshRateKnown && limit.targetFramesPerSecond == 0.0);
    limit = resolveFrameLimit(FrameLimitMode::PlatformDefault, 0.0, true);
    assert(!limit.refreshRateKnown && limit.targetFramesPerSecond == 60.0);
    limit = resolveFrameLimit(FrameLimitMode::Balanced, 0.0, true);
    assert(!limit.refreshRateKnown && limit.targetFramesPerSecond == 40.0);

    constexpr std::uint64_t millisecond = 1'000'000;
    PresentIntervalDeadline deadline;
    deadline.configure(60.0);
    assert(deadline.active());
    const std::uint64_t interval = deadline.targetIntervalNanoseconds();
    assert(interval > 16 * millisecond && interval < 17 * millisecond);

    // The first present establishes an absolute next-frame-start cadence and
    // immediately keeps the CPU from filling a triple-buffered FIFO queue.
    FrameDeadlineDecision decision = deadline.presented(100 * millisecond);
    assert(decision.deadlineRebased);
    assert(decision.delayNanoseconds == interval);
    const std::uint64_t firstDelay = decision.delayNanoseconds;

    // Two milliseconds of CPU work after the scheduled start receives only
    // the remainder of the next absolute interval.
    decision = deadline.presented(100 * millisecond + firstDelay + 2 * millisecond);
    assert(decision.delayNanoseconds > 14 * millisecond);
    const std::uint64_t secondDelay = decision.delayNanoseconds;
    decision = deadline.presented(
        100 * millisecond + firstDelay + 2 * millisecond + secondDelay + 2 * millisecond);
    assert(!decision.platformLimiterSatisfied);
    assert(decision.delayNanoseconds > 14 * millisecond);

    // If equal-rate platform pacing blocks for another full interval after the
    // scheduled sleep, its completion has already reached the next deadline.
    deadline.reset();
    const std::uint64_t equalInitialDelay = deadline.presented(200 * millisecond).delayNanoseconds;
    decision = deadline.presented(200 * millisecond + equalInitialDelay + interval);
    assert(decision.platformLimiterSatisfied);
    assert(!decision.platformLimiterStricter);
    assert(decision.delayNanoseconds == 0);

    // A stricter Gamescope/platform cap misses the absolute app deadline and
    // is never followed by an extra game-side delay.
    deadline.reset();
    const std::uint64_t strictInitialDelay = deadline.presented(300 * millisecond).delayNanoseconds;
    decision = deadline.presented(300 * millisecond + strictInitialDelay + interval * 2);
    assert(decision.platformLimiterSatisfied);
    assert(decision.platformLimiterStricter);
    assert(decision.delayNanoseconds == 0);
    decision = deadline.presented(300 * millisecond + strictInitialDelay + interval * 4);
    assert(decision.platformLimiterStricter);
    assert(decision.delayNanoseconds == 0);

    // Fast FIFO returns remain on the absolute frame-start schedule instead of
    // alternating between an immediate submission and a two-frame stall.
    deadline.reset();
    const std::uint64_t queuedInitialDelay = deadline.presented(350 * millisecond).delayNanoseconds;
    decision = deadline.presented(350 * millisecond + queuedInitialDelay + 2 * millisecond);
    assert(!decision.platformLimiterSatisfied);
    assert(decision.delayNanoseconds > 14 * millisecond);
    const std::uint64_t queuedSecondDelay = decision.delayNanoseconds;
    decision = deadline.presented(
        350 * millisecond + queuedInitialDelay + 2 * millisecond
            + queuedSecondDelay + 2 * millisecond);
    assert(!decision.platformLimiterSatisfied);
    assert(decision.delayNanoseconds > 14 * millisecond);

    // Debugger/suspend stalls rebase rather than scheduling catch-up frames.
    deadline.reset();
    const std::uint64_t stalledInitialDelay = deadline.presented(400 * millisecond).delayNanoseconds;
    assert(stalledInitialDelay == interval);
    decision = deadline.presented(400 * millisecond + interval * 5);
    assert(decision.deadlineRebased);
    assert(!decision.platformLimiterStricter);
    assert(decision.delayNanoseconds == 0);

    deadline.configure(0.0);
    assert(!deadline.active());
    assert(deadline.presented(1).delayNanoseconds == 0);
    deadline.configure(0.001);
    assert(!deadline.active());

    SteamDeckRuntimeDetector detector;
    SteamDeckRuntimeDetection detection = detector.detect();
    assert(!detection.queryAvailable && !detection.runningOnSteamDeck);
    FakeSteamUtils steamUtils;
    detector = SteamDeckRuntimeDetector::bindSteamUtils(&steamUtils);
    detection = detector.detect();
    assert(detection.queryAvailable && !detection.runningOnSteamDeck);
    steamUtils.deck = true;
    detection = detector.detect();
    assert(detection.queryAvailable && detection.runningOnSteamDeck);
    detector.clear();
    assert(!detector.detect().queryAvailable);

    return 0;
}
