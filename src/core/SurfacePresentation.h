#pragma once
#include "core/FlightSystem.h"
#include <algorithm>

namespace rocket {
// Presentation only; never changes the landing or departure simulation.
inline double surfacePresentationProgress(const FlightRunState& flight, bool arrivalActive)
{
    if (flight.mode != FlightMode::Landing) return 0.0;
    if (arrivalActive) return 1.0;
    if (flight.landing.departureActive || flight.landing.verticalVelocity > 0.0)
        return 1.0 - std::clamp((flight.landing.altitude - 24.0) /
            (flight_landing::departureAltitude - 24.0), 0.0, 1.0);
    return std::clamp(flight.handoff.elapsed / 0.6, 0.0, 1.0);
}
}
