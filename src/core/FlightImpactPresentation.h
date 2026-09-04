#pragma once

#include "core/GameFormat.h"
#include "core/GameTypes.h"

namespace rocket {

inline std::string flightImpactReadout(const FlightImpactEvent& impact)
{
    if (!impact.valid) return {};
    return "IMPACT " + display::fixed(impact.impactSpeed,1) + " m/s · DAMAGE " +
        display::fixed(impact.damage,1) + " · HULL " + display::fixed(impact.hullBefore,1) +
        " → " + display::fixed(impact.hullAfter,1);
}

inline std::string flightHullReadout(const FlightRunState& flight)
{
    return display::fixed(flight.hullRemaining,1)+" / "+display::fixed(flight.hullMaximum,0)+" HP";
}

} // namespace rocket
