#pragma once

#include "core/GameTypes.h"

namespace rocket {

struct RigFuelDemand {
    bool thrustPowered = false;
    bool drillPowered = false;
    double loadMultiplier = 1.0;
    int efficiencyRank = 0;
};

struct RigFuelEvent {
    double consumed = 0.0;
    double transferred = 0.0;
    bool becameUnpowered = false;
    bool restarted = false;
};

double rigFuelEfficiency(int rank);
double rigFuelDemandPerSecond(const RigFuelDemand& demand);
RigFuelEvent consumeRigFuel(
    ResourceTankState& tank,
    const RigFuelDemand& demand,
    double deltaSeconds);
RigFuelEvent transferFuelCell(ResourceTankState& tank, double fuelValue);

} // namespace rocket
