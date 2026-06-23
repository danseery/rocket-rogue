#pragma once

#include "core/DetailPresentation.h"
#include "core/FlightProgress.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"
#include "core/GameUi.h"
#include "core/LaunchSimulation.h"
#include "core/PanelPresentation.h"
#include "core/Telemetry.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rocket {

using FlightActionButtonPresentation = PanelButtonPresentation;

struct LaunchPanelPresentation {
    std::string sectionTitle;
    std::vector<PanelMetricPresentation> metrics;
    std::vector<TelemetryChannelSample> telemetry;
    std::string telemetryMessage;
    std::vector<DetailPresentationRow> telemetryDetails;
    std::vector<FlightActionButtonPresentation> primaryActions;
    std::vector<FlightActionButtonPresentation> systemActions;
    double displayedMultiplier = 1.0;
    double returnProgress = 0.0;
    double recoveryRisk = 0.0;
};

inline FlightActionButtonPresentation flightActionButton(std::string_view label, std::string_view actionId, std::string cssClass = "")
{
    return panelActionButton(label, actionId, std::move(cssClass));
}

inline FlightActionButtonPresentation disabledFlightActionButton(std::string_view label)
{
    return disabledPanelButton(label);
}

inline std::string launchSectionTitle(const FlightActionState& actions, bool frontierTransfer)
{
    if (actions.returningHome) {
        return std::string(text::panel::sections::returnBurn);
    }
    return std::string(frontierTransfer ? text::panel::sections::transferAttempt : text::panel::sections::provingFlight);
}

inline bool canCommitArrivalOps(const GameState& state, const ContentCatalog& catalog, const PreparedLaunch& flightModel, double currentMultiplier)
{
    const Destination* configuredDestination = catalog.findDestination(flightModel.config.destinationId);
    const Destination& destination = configuredDestination == nullptr ? currentDestination(state, catalog) : *configuredDestination;
    return !flightModel.config.frontierTransfer
        && destination.tier >= 1
        && currentMultiplier >= destination.targetMultiplier;
}

inline std::vector<FlightActionButtonPresentation> primaryFlightActions(
    const FlightActionState& actions,
    bool arrivalOpsAvailable)
{
    std::vector<FlightActionButtonPresentation> buttons;
    if (actions.returningHome) {
        buttons.push_back(disabledFlightActionButton(text::buttons::returningHome));
        buttons.push_back(disabledFlightActionButton(text::buttons::arrivalOps));
    } else {
        buttons.push_back(flightActionButton(text::buttons::returnHome, ui::actions::returnHome, "ok"));
        buttons.push_back(arrivalOpsAvailable
            ? flightActionButton(text::buttons::arrivalOps, ui::actions::arrivalOps, "warn")
            : disabledFlightActionButton(text::buttons::arrivalOps));
    }
    buttons.push_back(flightActionButton(text::buttons::eject, ui::actions::ejectNow, "danger"));
    return buttons;
}

inline std::vector<FlightActionButtonPresentation> systemFlightActions(const FlightActionState& actions, bool pressureReliefUsed)
{
    if (actions.returningHome) {
        return {
            disabledFlightActionButton(text::buttons::cutEngines),
            disabledFlightActionButton(text::buttons::reliefValve),
            disabledFlightActionButton(text::buttons::jettisonCargo)
        };
    }

    std::vector<FlightActionButtonPresentation> buttons;
    buttons.push_back(flightActionButton(
        actions.cutEnginesActive ? text::buttons::restoreThrust : text::buttons::cutEngines,
        ui::actions::cutEngines,
        "warn"));

    if (pressureReliefUsed) {
        if (actions.pressureReliefFailed) {
            buttons.push_back(disabledFlightActionButton(text::buttons::reliefValveFailed));
        } else if (actions.pressureReliefOpen) {
            buttons.push_back(flightActionButton(text::buttons::closeValve, ui::actions::closeReliefValve, "warn"));
        } else {
            buttons.push_back(disabledFlightActionButton(text::buttons::valveClosed));
        }
    } else {
        buttons.push_back(flightActionButton(text::buttons::reliefValve, ui::actions::pressureRelief, "warn"));
    }

    buttons.push_back(actions.cargoJettisoned
        ? disabledFlightActionButton(text::buttons::cargoGone)
        : flightActionButton(text::buttons::jettisonCargo, ui::actions::jettisonCargo, "warn"));
    return buttons;
}

inline const Destination& launchDisplayDestination(const GameState& state, const ContentCatalog& catalog, const PreparedLaunch& launch)
{
    if (const Destination* destination = catalog.findDestination(launch.config.destinationId)) {
        return *destination;
    }
    return currentDestination(state, catalog);
}

inline LaunchPanelPresentation launchPanelPresentation(
    const GameState& state,
    const ContentCatalog& catalog,
    const PreparedLaunch& flightModel,
    double currentMultiplier,
    double returnBurnMultiplier,
    double returnElapsed,
    double returnDuration,
    const FlightActionState& actions,
    bool pressureReliefUsed)
{
    LaunchPanelPresentation presentation;
    const Destination& destination = launchDisplayDestination(state, catalog, flightModel);
    presentation.sectionTitle = launchSectionTitle(actions, flightModel.config.frontierTransfer);
    presentation.displayedMultiplier = actions.returningHome
        ? returnTelemetryMultiplier(returnBurnMultiplier, flightModel.crashMultiplier, returnElapsed, returnDuration)
        : currentMultiplier;
    presentation.returnProgress = flight_progress::returnCompletion(returnElapsed, returnDuration);
    presentation.recoveryRisk = returnHomeRisk(flightModel, catalog, state, presentation.displayedMultiplier);

    presentation.metrics.push_back(panelMetric(text::labels::burnDepth, display::multiplier(presentation.displayedMultiplier)));
    presentation.metrics.push_back(panelMetric(
        actions.returningHome ? text::labels::returnProgress :
            (flightModel.config.frontierTransfer ? text::labels::requiredBurn : text::labels::dataGoal),
        actions.returningHome ? display::percent(presentation.returnProgress) : display::multiplier(destination.targetMultiplier)));
    presentation.metrics.push_back(panelMetric(text::labels::returnRisk, display::percent(presentation.recoveryRisk)));

    const TelemetryEvent event = telemetryAt(flightModel, presentation.displayedMultiplier);
    const auto samples = telemetrySamples(event);
    presentation.telemetry.assign(samples.begin(), samples.end());
    presentation.telemetryMessage = event.message;
    for (const TelemetryChannelSample& sample : presentation.telemetry) {
        presentation.telemetryDetails.push_back(detailPresentationRow(sample.label, display::percent(sample.value)));
    }
    presentation.telemetryDetails.push_back(detailPresentationRow(text::labels::returnRisk, display::percent(presentation.recoveryRisk)));
    presentation.telemetryDetails.push_back(detailPresentationRow(text::labels::missionDifficulty, display::signedPercent(flightModel.pressureModifier)));
    presentation.primaryActions = primaryFlightActions(actions, canCommitArrivalOps(state, catalog, flightModel, currentMultiplier));
    presentation.systemActions = systemFlightActions(actions, pressureReliefUsed);
    return presentation;
}

} // namespace rocket
