#pragma once

#include "core/Content.h"
#include "core/GameState.h"
#include "core/LaunchSimulation.h"

#include <string>

namespace rocket {

struct PanelRenderContext {
    const GameState& state;
    const ContentCatalog& catalog;
    const PreparedLaunch& activeLaunch;
    const PreparedLaunch& flightModel;
    double currentMultiplier = 1.0;
    double returnBurnMultiplier = 1.0;
    double returnElapsed = 0.0;
    double returnDuration = 1.0;
    FlightActionState flightActions;
    bool flightArmed = true;
    bool pressureReliefUsed = false;
    bool preflightReady = true;
    bool droneTransferEnabled = true;
    int debugActOneCheckpoint = -1;
};

std::string buildGamePanelHtml(const PanelRenderContext& context);

} // namespace rocket
