#pragma once

#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"
#include "core/GameUi.h"
#include "core/LaunchSimulation.h"
#include "core/PanelPresentation.h"

#include <string>
#include <vector>

namespace rocket {

inline std::string crewStatusSummary(const Astronaut* astronaut)
{
    if (astronaut == nullptr) {
        return std::string(text::panel::noActiveCrew);
    }
    return std::string(toString(astronaut->status));
}

inline const Destination& panelDisplayDestination(const GameState& state, const ContentCatalog& catalog, const PreparedLaunch& activeLaunch)
{
    if (state.screen == Screen::Flight) {
        if (const Destination* activeDestination = catalog.findDestination(activeLaunch.config.destinationId)) {
            return *activeDestination;
        }
    }
    if (state.screen == Screen::ArrivalFanfare || state.screen == Screen::ArrivalOps) {
        if (const Destination* arrivalDestination = catalog.findDestination(state.lastOutcome.destinationId)) {
            return *arrivalDestination;
        }

    }
    const Destination& current = currentDestination(state, catalog);
    if (current.hiddenFromProgression) {
        if (const Destination* launchTarget = catalog.findDestination(state.launchConfig.destinationId)) {
            if (!launchTarget->hiddenFromProgression) {
                return *launchTarget;
            }
        }
        if (const Destination* next = nextDestination(state, catalog)) {
            return *next;
        }
    }
    return current;
}

inline std::vector<PanelButtonPresentation> settingsActionPresentation()
{
    return {
        panelActionButton(text::buttons::resetSave, ui::actions::resetSave, "danger")
    };
}

} // namespace rocket
