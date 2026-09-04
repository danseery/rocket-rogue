#include "core/RigFuelSystem.h"

#include <algorithm>

namespace rocket {

double rigFuelEfficiency(int rank)
{
    return 0.10 * static_cast<double>(std::clamp(rank, 0, 3));
}

double rigFuelDemandPerSecond(const RigFuelDemand& demand)
{
    const double thrust = demand.thrustPowered
        ? std::max(0.0, demand.loadMultiplier) / 30.0
        : 0.0;
    const double drilling = demand.drillPowered ? 1.0 / 30.0 : 0.0;
    return (thrust + drilling) * (1.0 - rigFuelEfficiency(demand.efficiencyRank));
}

RigFuelEvent consumeRigFuel(
    ResourceTankState& tank,
    const RigFuelDemand& demand,
    double deltaSeconds)
{
    RigFuelEvent event;
    tank.capacity = std::max(0.0, tank.capacity);
    tank.current = std::clamp(tank.current, 0.0, tank.capacity);
    const double requested = rigFuelDemandPerSecond(demand) * std::max(0.0, deltaSeconds);
    event.consumed = std::min(tank.current, requested);
    const bool wasPowered = tank.current > 0.0;
    tank.current = std::max(0.0, tank.current - event.consumed);
    event.becameUnpowered = wasPowered && requested > 0.0 && tank.current <= 0.0;
    return event;
}

RigFuelEvent transferFuelCell(ResourceTankState& tank, double fuelValue)
{
    RigFuelEvent event;
    tank.capacity = std::max(0.0, tank.capacity);
    tank.current = std::clamp(tank.current, 0.0, tank.capacity);
    const bool wasUnpowered = tank.current <= 0.0;
    event.transferred = std::min(
        std::max(0.0, fuelValue),
        std::max(0.0, tank.capacity - tank.current));
    tank.current += event.transferred;
    event.restarted = wasUnpowered && tank.current > 0.0;
    return event;
}

} // namespace rocket
