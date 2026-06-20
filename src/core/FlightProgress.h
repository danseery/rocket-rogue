#pragma once

#include "core/GameMath.h"
#include "core/GameTypes.h"
#include "core/Tuning.h"

#include <algorithm>

namespace rocket::flight_progress {

inline double travelProgressForBurn(double burnMultiplier, const Destination& destination)
{
    return std::clamp(
        (burnMultiplier - 1.0) / std::max(tuning::session::minTravelDenominator, destination.targetMultiplier - 1.0),
        0.0,
        tuning::session::maxTravelProgress);
}

inline double returnCompletion(double elapsed, double duration)
{
    return math::smoothStep(elapsed / std::max(tuning::session::minTravelDenominator, duration));
}

inline double returnTravelProgress(double startTravelProgress, double elapsed, double duration)
{
    return std::clamp(
        startTravelProgress * (1.0 - returnCompletion(elapsed, duration)),
        0.0,
        tuning::session::maxTravelProgress);
}

inline double returnDuration(double startTravelProgress, bool driftHome)
{
    const double duration = tuning::session::returnBaseDuration +
        startTravelProgress * tuning::session::returnDurationPerProgress;
    return driftHome ? duration * tuning::session::returnDriftDurationMultiplier : duration;
}

} // namespace rocket::flight_progress
