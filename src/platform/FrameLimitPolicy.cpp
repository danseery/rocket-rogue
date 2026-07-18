#include "platform/FrameLimitPolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rocket {
namespace {

constexpr double minimumPlausibleRefreshRateHz = 10.0;
constexpr double maximumPlausibleRefreshRateHz = 1000.0;
constexpr std::uint32_t maximumRefreshDivisor = 120;

bool validRefreshRate(double refreshRateHz)
{
    return std::isfinite(refreshRateHz)
        && refreshRateHz >= minimumPlausibleRefreshRateHz
        && refreshRateHz <= maximumPlausibleRefreshRateHz;
}

double compatibilityTolerance(double framesPerSecond)
{
    return std::max(0.15, framesPerSecond * 0.005);
}

struct DivisorChoice {
    double framesPerSecond = 0.0;
    std::uint32_t divisor = 0;
};

DivisorChoice exactNominalChoice(double refreshRateHz, double nominalFramesPerSecond)
{
    if (!validRefreshRate(refreshRateHz) || nominalFramesPerSecond <= 0.0) return {};
    const auto divisor = static_cast<std::uint32_t>(std::max(
        1.0,
        std::round(refreshRateHz / nominalFramesPerSecond)));
    const double compatibleRate = refreshRateHz / static_cast<double>(divisor);
    if (std::abs(compatibleRate - nominalFramesPerSecond)
        > compatibilityTolerance(nominalFramesPerSecond)) {
        return {};
    }
    return {compatibleRate, divisor};
}

DivisorChoice highestCompatibleRateAtOrBelow(double refreshRateHz, double ceiling)
{
    if (!validRefreshRate(refreshRateHz) || ceiling <= 0.0) return {};
    DivisorChoice best;
    for (std::uint32_t divisor = 1; divisor <= maximumRefreshDivisor; ++divisor) {
        const double rate = refreshRateHz / static_cast<double>(divisor);
        if (rate <= ceiling + compatibilityTolerance(ceiling)
            && rate > best.framesPerSecond) {
            best = {rate, divisor};
        }
    }
    return best.divisor == 0 ? DivisorChoice {refreshRateHz, 1} : best;
}

double distanceFromBalancedBand(double framesPerSecond)
{
    if (framesPerSecond < 40.0) return 40.0 - framesPerSecond;
    if (framesPerSecond > 45.0) return framesPerSecond - 45.0;
    return 0.0;
}

DivisorChoice nearestBalancedChoice(double refreshRateHz)
{
    DivisorChoice best;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::uint32_t divisor = 1; divisor <= maximumRefreshDivisor; ++divisor) {
        const double rate = refreshRateHz / static_cast<double>(divisor);
        const double distance = distanceFromBalancedBand(rate);
        if (distance < bestDistance - 0.000001
            || (std::abs(distance - bestDistance) <= 0.000001
                && rate > best.framesPerSecond)) {
            best = {rate, divisor};
            bestDistance = distance;
        }
        if (rate < 10.0) break;
    }
    return best;
}

FrameLimitResolution unknownRefreshResolution(FrameLimitMode mode, bool runningOnSteamDeck)
{
    FrameLimitResolution result;
    result.mode = mode;
    switch (mode) {
    case FrameLimitMode::PlatformDefault:
        if (runningOnSteamDeck) {
            result.nominalTargetFramesPerSecond = 60.0;
            result.targetFramesPerSecond = 60.0;
        }
        break;
    case FrameLimitMode::Smooth60:
        result.nominalTargetFramesPerSecond = 60.0;
        result.targetFramesPerSecond = 60.0;
        break;
    case FrameLimitMode::Balanced:
        result.nominalTargetFramesPerSecond = 40.0;
        result.targetFramesPerSecond = 40.0;
        break;
    case FrameLimitMode::Battery30:
        result.nominalTargetFramesPerSecond = 30.0;
        result.targetFramesPerSecond = 30.0;
        break;
    case FrameLimitMode::Display:
        break;
    }
    return result;
}

void applyChoice(
    FrameLimitResolution& result,
    const DivisorChoice& choice,
    bool exact)
{
    result.targetFramesPerSecond = choice.framesPerSecond;
    result.refreshDivisor = choice.divisor;
    result.exactModeTarget = exact;
    result.usedRefreshCompatibleFallback = !exact;
}

} // namespace

FrameLimitResolution resolveFrameLimit(
    FrameLimitMode mode,
    double activeRefreshRateHz,
    bool runningOnSteamDeck)
{
    if (!validRefreshRate(activeRefreshRateHz)) {
        return unknownRefreshResolution(mode, runningOnSteamDeck);
    }

    FrameLimitResolution result;
    result.mode = mode;
    result.activeRefreshRateHz = activeRefreshRateHz;
    result.refreshRateKnown = true;

    if (mode == FrameLimitMode::Display
        || (mode == FrameLimitMode::PlatformDefault && !runningOnSteamDeck)) {
        result.nominalTargetFramesPerSecond = activeRefreshRateHz;
        applyChoice(result, {activeRefreshRateHz, 1}, true);
        return result;
    }

    if (mode == FrameLimitMode::Balanced) {
        // Prefer the smoother Deck OLED cadence when both rates happen to be
        // compatible, then the LCD-oriented 40 FPS cadence.
        if (const DivisorChoice choice = exactNominalChoice(activeRefreshRateHz, 45.0);
            choice.divisor != 0) {
            result.nominalTargetFramesPerSecond = 45.0;
            applyChoice(result, choice, true);
            return result;
        }
        if (const DivisorChoice choice = exactNominalChoice(activeRefreshRateHz, 40.0);
            choice.divisor != 0) {
            result.nominalTargetFramesPerSecond = 40.0;
            applyChoice(result, choice, true);
            return result;
        }
        result.nominalTargetFramesPerSecond = 42.5;
        applyChoice(result, nearestBalancedChoice(activeRefreshRateHz), false);
        return result;
    }

    const double nominalTarget = mode == FrameLimitMode::Battery30 ? 30.0 : 60.0;
    result.nominalTargetFramesPerSecond = nominalTarget;
    if (const DivisorChoice exact = exactNominalChoice(activeRefreshRateHz, nominalTarget);
        exact.divisor != 0) {
        applyChoice(result, exact, true);
    } else {
        applyChoice(
            result,
            highestCompatibleRateAtOrBelow(activeRefreshRateHz, nominalTarget),
            false);
    }
    return result;
}

void PresentIntervalDeadline::configure(double targetFramesPerSecond)
{
    if (!std::isfinite(targetFramesPerSecond)
        || targetFramesPerSecond < 1.0
        || targetFramesPerSecond > maximumPlausibleRefreshRateHz) {
        targetIntervalNanoseconds_ = 0;
    } else {
        targetIntervalNanoseconds_ = static_cast<std::uint64_t>(std::llround(
            1'000'000'000.0 / targetFramesPerSecond));
    }
    reset();
}

void PresentIntervalDeadline::reset()
{
    nextDeadlineNanoseconds_ = 0;
    lastPresentNanoseconds_ = 0;
    lastScheduledDelayNanoseconds_ = 0;
    hasPresented_ = false;
}

FrameDeadlineDecision PresentIntervalDeadline::presented(std::uint64_t nowNanoseconds)
{
    FrameDeadlineDecision result;
    result.targetIntervalNanoseconds = targetIntervalNanoseconds_;
    if (targetIntervalNanoseconds_ == 0) return result;

    if (!hasPresented_) {
        hasPresented_ = true;
        lastPresentNanoseconds_ = nowNanoseconds;
        nextDeadlineNanoseconds_ = nowNanoseconds + targetIntervalNanoseconds_;
        result.deadlineRebased = true;
        result.delayNanoseconds = targetIntervalNanoseconds_;
        lastScheduledDelayNanoseconds_ = result.delayNanoseconds;
        nextDeadlineNanoseconds_ += targetIntervalNanoseconds_;
        return result;
    }

    if (nowNanoseconds < lastPresentNanoseconds_) {
        lastPresentNanoseconds_ = nowNanoseconds;
        nextDeadlineNanoseconds_ = nowNanoseconds + targetIntervalNanoseconds_;
        lastScheduledDelayNanoseconds_ = 0;
        result.deadlineRebased = true;
        return result;
    }

    const std::uint64_t observedInterval = nowNanoseconds - lastPresentNanoseconds_;
    const std::uint64_t priorSoftwareDelay = lastScheduledDelayNanoseconds_;
    result.observedPresentIntervalNanoseconds = observedInterval;
    lastPresentNanoseconds_ = nowNanoseconds;

    if (observedInterval > targetIntervalNanoseconds_ * 4) {
        nextDeadlineNanoseconds_ = nowNanoseconds + targetIntervalNanoseconds_;
        lastScheduledDelayNanoseconds_ = 0;
        result.deadlineRebased = true;
        return result;
    }

    const std::uint64_t tolerance = std::max<std::uint64_t>(
        250'000,
        targetIntervalNanoseconds_ * 3 / 100);
    const std::uint64_t unassistedInterval = observedInterval > priorSoftwareDelay
        ? observedInterval - priorSoftwareDelay
        : 0;
    result.platformLimiterSatisfied =
        unassistedInterval + tolerance >= targetIntervalNanoseconds_;
    result.platformLimiterStricter =
        unassistedInterval > targetIntervalNanoseconds_ + tolerance;

    if (nowNanoseconds < nextDeadlineNanoseconds_) {
        result.delayNanoseconds = nextDeadlineNanoseconds_ - nowNanoseconds;
        nextDeadlineNanoseconds_ += targetIntervalNanoseconds_;
    } else {
        const std::uint64_t missedIntervals =
            (nowNanoseconds - nextDeadlineNanoseconds_) / targetIntervalNanoseconds_ + 1U;
        nextDeadlineNanoseconds_ += missedIntervals * targetIntervalNanoseconds_;
    }
    lastScheduledDelayNanoseconds_ = result.delayNanoseconds;
    return result;
}

bool PresentIntervalDeadline::active() const
{
    return targetIntervalNanoseconds_ != 0;
}

std::uint64_t PresentIntervalDeadline::targetIntervalNanoseconds() const
{
    return targetIntervalNanoseconds_;
}

SteamDeckRuntimeDetector::SteamDeckRuntimeDetector(
    void* context,
    QueryFunction query) noexcept
    : context_(context), query_(query)
{
}

SteamDeckRuntimeDetection SteamDeckRuntimeDetector::detect() const noexcept
{
    if (context_ == nullptr || query_ == nullptr) return {};
    return {true, query_(context_)};
}

void SteamDeckRuntimeDetector::clear() noexcept
{
    context_ = nullptr;
    query_ = nullptr;
}

} // namespace rocket
