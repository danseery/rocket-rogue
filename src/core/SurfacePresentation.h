#pragma once
#include "core/FlightSystem.h"
#include <algorithm>

namespace rocket {
// Presentation only; never changes the landing or departure simulation.
inline double surfacePresentationProgress(const FlightRunState& flight, bool arrivalActive)
{
    if (flight.mode != FlightMode::Landing) {
        return flight.handoff.from == FlightMode::Landing
            ? 1.0-departureCameraProgress(flight.handoff.elapsed/flight_landing::handoffSeconds)
            : 0.0;
    }
    if (arrivalActive) return 1.0;
    if (flight.landing.departureActive) return 1.0;
    return std::clamp(flight.handoff.elapsed / 0.6, 0.0, 1.0);
}
}
