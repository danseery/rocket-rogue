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

inline std::string crewStressSummary(const Astronaut* astronaut)
{
    if (astronaut == nullptr) {
        return std::string(text::panel::noActiveCrew);
    }
    return display::wholePercent(astronaut->stress);
}

inline const Destination& panelDisplayDestination(const GameState& state, const ContentCatalog& catalog, const PreparedLaunch& activeLaunch)
{
    if (state.screen == Screen::Launch) {
        if (const Destination* activeDestination = catalog.findDestination(activeLaunch.config.destinationId)) {
            return *activeDestination;
        }
    }
    if (state.screen == Screen::ArrivalFanfare || state.screen == Screen::ArrivalOps || state.screen == Screen::Flyby) {
        if (const Destination* arrivalDestination = catalog.findDestination(state.lastOutcome.destinationId)) {
            return *arrivalDestination;
        }
        if (state.screen == Screen::Flyby && !state.run.flyby.destinationId.empty()) {
            if (const Destination* flybyDestination = catalog.findDestination(state.run.flyby.destinationId)) {
                return *flybyDestination;
            }
        }
    }
    return currentDestination(state, catalog);
}

inline std::vector<PanelMetricPresentation> panelHeaderMetrics(
    const GameState& state,
    const ContentCatalog& catalog,
    const PreparedLaunch& activeLaunch,
    const PreparedLaunch& flightModel)
{
    std::vector<PanelMetricPresentation> metrics;
    const Destination& displayDestination = panelDisplayDestination(state, catalog, activeLaunch);
    const bool transferLaunch = state.screen == Screen::Launch && activeLaunch.config.frontierTransfer;
    const int requiredReadiness = frontierReadinessRequired(state, catalog);
    const Astronaut* astronaut = activeAstronaut(state);

    metrics.push_back(panelMetric(text::labels::missionCredits, display::money(state.run.credits)));
    metrics.push_back(panelMetric(text::labels::hullDamage, display::wholePercent(state.run.shipDamage)));
    metrics.push_back(panelMetric(transferLaunch ? text::labels::transferTarget : text::labels::currentFrontier, displayDestination.name));
    metrics.push_back(panelMetric(
        transferLaunch ? text::labels::requiredBurn : text::labels::flightData,
        transferLaunch ? display::multiplier(displayDestination.targetMultiplier) :
                         (requiredReadiness == 0 ? std::string(text::panel::complete) : display::fraction(state.run.frontierReadiness, requiredReadiness))));
    metrics.push_back(panelMetric(
        text::labels::missionDifficulty,
        display::signedPercent(state.screen == Screen::Launch
            ? flightModel.pressureModifier
            : missionPressureModifier(state, catalog, displayDestination))));
    metrics.push_back(panelMetric(text::labels::crewStress, crewStressSummary(astronaut)));
    const double pendingFuelBoost = state.screen == Screen::Launch ? activeLaunch.slingshotFuelBoost : state.run.nextLaunchFuelBoost;
    const double pendingSpeedBoost = state.screen == Screen::Launch ? activeLaunch.slingshotSpeedBoost : state.run.nextLaunchSpeedBoost;
    if (pendingFuelBoost > 0.0 || pendingSpeedBoost > 0.0) {
        metrics.push_back(panelMetric("Slingshot window", "+" + display::fixed(pendingFuelBoost, 1) + " fuel / +" + display::fixed(pendingSpeedBoost, 2) + " speed"));
    }
    return metrics;
}

inline std::vector<DetailPresentationRow> settingsDetailsPresentation()
{
    return {
        detailPresentationRow(text::panel::details::keyboard, text::panel::details::keyboardValue),
        detailPresentationRow(text::panel::details::save, text::panel::details::saveValue),
        detailPresentationRow(text::panel::details::build, text::panel::details::buildValue)
    };
}

inline std::vector<PanelButtonPresentation> settingsActionPresentation()
{
    return {
        panelActionButton(text::buttons::resetSave, ui::actions::resetSave, "danger")
    };
}

} // namespace rocket
