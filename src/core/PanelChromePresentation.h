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
    if (state.screen == Screen::ArrivalFanfare || state.screen == Screen::ArrivalOps || state.screen == Screen::Flyby || state.screen == Screen::Orbit || state.screen == Screen::SurfaceScan || state.screen == Screen::SurfacePush) {
        if (const Destination* arrivalDestination = catalog.findDestination(state.lastOutcome.destinationId)) {
            return *arrivalDestination;
        }
        if (state.screen == Screen::Flyby && !state.run.flyby.destinationId.empty()) {
            if (const Destination* flybyDestination = catalog.findDestination(state.run.flyby.destinationId)) {
                return *flybyDestination;
            }
        }
        if (state.screen == Screen::Orbit && !state.run.orbit.destinationId.empty()) {
            if (const Destination* orbitDestination = catalog.findDestination(state.run.orbit.destinationId)) {
                return *orbitDestination;
            }
        }
        if (state.screen == Screen::SurfaceScan && !state.run.surfaceScan.destinationId.empty()) {
            if (const Destination* scanDestination = catalog.findDestination(state.run.surfaceScan.destinationId)) {
                return *scanDestination;
            }
        }
        if (state.screen == Screen::SurfacePush && !state.run.surfacePush.destinationId.empty()) {
            if (const Destination* pushDestination = catalog.findDestination(state.run.surfacePush.destinationId)) {
                return *pushDestination;
            }
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

inline std::vector<PanelMetricPresentation> panelHeaderMetrics(
    const GameState& state,
    const ContentCatalog& catalog,
    const PreparedLaunch& activeLaunch,
    const PreparedLaunch& flightModel)
{
    std::vector<PanelMetricPresentation> metrics;
    const Destination& displayDestination = panelDisplayDestination(state, catalog, activeLaunch);
    const bool transferLaunch = state.screen == Screen::Launch && activeLaunch.config.frontierTransfer;
    const Astronaut* astronaut = activeAstronaut(state);

    metrics.push_back(panelMetric(text::labels::missionCredits, display::money(state.run.credits)));
    metrics.push_back(panelMetric(text::labels::chapter, chapterLabel(state.meta.chapter)));
    metrics.push_back(panelMetric(text::labels::hullDamage, display::wholePercent(state.run.shipDamage)));
    metrics.push_back(panelMetric(transferLaunch ? text::labels::transferTarget : text::labels::currentFrontier, displayDestination.name));
    if (state.screen == Screen::Launch) {
        metrics.push_back(panelMetric("Launch lesson", std::string(toString(flightModel.config.missionKind))));
    }
    metrics.push_back(panelMetric(text::labels::crewStress, crewStressSummary(astronaut)));
    const double pendingFuelSavings = state.screen == Screen::Launch ? activeLaunch.slingshotFuelSavings : state.run.nextLaunchFuelBoost;
    const double pendingSpeedBoost = state.screen == Screen::Launch ? activeLaunch.slingshotSpeedBoost : state.run.nextLaunchSpeedBoost;
    const double pendingInstability = state.screen == Screen::Launch
        ? activeLaunch.slingshotInstabilityPenalty
        : state.run.nextLaunchInstabilityPenalty;
    if (pendingFuelSavings > 0.0 || pendingSpeedBoost > 0.0 || pendingInstability > 0.0) {
        metrics.push_back(panelMetric(
            "Slingshot momentum",
            display::fixed(pendingFuelSavings, 1) + " fuel saved / +" +
                display::percent(pendingSpeedBoost) + " velocity" +
                (pendingInstability > 0.0
                    ? " / +" + display::percent(pendingInstability) + " flight instability"
                    : " / stable")));
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
