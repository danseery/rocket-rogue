#pragma once

#include "core/FlightProgress.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"
#include "core/GameUi.h"
#include "core/LaunchSimulation.h"
#include "core/PanelPresentation.h"
#include "core/Telemetry.h"
#include "core/Tuning.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rocket {

using FlightActionButtonPresentation = PanelButtonPresentation;

struct LaunchPanelPresentation {
    std::string sectionTitle;
    std::string destinationName;
    std::vector<PanelMetricPresentation> metrics;
    std::vector<TelemetryChannelSample> telemetry;
    std::string telemetryMessage;
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

inline std::vector<FlightActionButtonPresentation> primaryFlightActions(
    const FlightActionState& actions,
    bool arkKnown,
    bool outerExpedition = false)
{
    std::vector<FlightActionButtonPresentation> buttons;
    if (actions.returningHome) {
        buttons.push_back(disabledFlightActionButton(
            text::buttons::returningHomeLabel(arkKnown, outerExpedition)));
    } else {
        buttons.push_back(flightActionButton(
            text::buttons::returnHomeLabel(arkKnown, outerExpedition),
            ui::actions::returnHome,
            "ok"));
    }
    buttons.push_back(flightActionButton(text::buttons::eject, ui::actions::ejectNow, "danger"));
    return buttons;
}

inline std::vector<FlightActionButtonPresentation> systemFlightActions(const FlightActionState& actions, bool)
{
    std::vector<FlightActionButtonPresentation> buttons;
    buttons.push_back(flightActionButton(
        actions.cutEnginesActive ? text::buttons::restoreThrust : text::buttons::cutEngines,
        ui::actions::cutEngines,
        "warn"));

    if (actions.pressureReliefOpen) {
        buttons.push_back(flightActionButton(text::buttons::closeValve, ui::actions::closeReliefValve, "warn"));
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

inline bool launchUsesOuterExpeditionRecovery(
    const ContentCatalog&,
    const Destination& destination)
{
    return destination.oneWayExpedition;
}

inline bool advancedFlightControlsUnlocked(const GameState& state, const ContentCatalog& catalog, const PreparedLaunch& flightModel)
{
    static_cast<void>(state);
    static_cast<void>(catalog);
    static_cast<void>(flightModel);
    return true;
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
    bool pressureReliefUsed,
    const LaunchFlightState* launchFlight = nullptr)
{
    LaunchPanelPresentation presentation;
    const Destination& destination = launchDisplayDestination(state, catalog, flightModel);
    presentation.destinationName = destination.name;
    presentation.sectionTitle = launchFlight != nullptr
        ? (actions.returningHome ? "Return leg \xE2\x80\xA2 " : "Outbound leg \xE2\x80\xA2 ") + destination.name
        : launchSectionTitle(actions, flightModel.config.frontierTransfer);

    if (launchFlight != nullptr) {
        const LaunchFlightState& flight = *launchFlight;
        presentation.displayedMultiplier = flight.currentMultiplier;
        presentation.returnProgress = flight.returningHome
            ? std::clamp(1.0 - flight.travelProgress, 0.0, 1.0)
            : std::clamp(flight.travelProgress, 0.0, 1.0);
        presentation.recoveryRisk = 0.0;
        presentation.metrics.push_back(panelMetric(
            "Route",
            std::string(flight.returningHome ? "RETURN " : "OUTBOUND ") + display::percent(presentation.returnProgress)));
        presentation.metrics.push_back(panelMetric(
            "Throttle",
            actions.cutEnginesActive
                ? "CUT \xE2\x80\xA2 " + display::percent(flight.selectedThrottle) + " set"
                : display::percent(flight.selectedThrottle)));
        presentation.metrics.push_back(panelMetric(
            "Fuel",
            display::percent(flight.fuelRemaining / std::max(0.01, flight.fuelCapacity))));
        presentation.metrics.push_back(panelMetric(
            "Course",
            std::abs(flight.courseOffset) < tuning::launch::pilotingCourseSafe
                ? "CENTERED"
                : display::signedFixed(flight.courseOffset, 2)));
        presentation.metrics.push_back(panelMetric("Temperature", display::percent(flight.heat)));
        presentation.metrics.push_back(panelMetric("Pressure", display::percent(flight.pressure)));

        const TelemetryEvent event = launchTelemetryAt(flightModel, flight);
        const auto samples = telemetrySamples(event);
        presentation.telemetry.assign(samples.begin(), samples.end());
        if (flight.heatFailureSeconds > 0.0) {
            presentation.telemetryMessage = "THERMAL FAILURE IN " +
                display::fixed(std::max(0.0, tuning::launch::pilotingHeatFailureSeconds - flight.heatFailureSeconds), 1) +
                "s \xE2\x80\x94 cut engines now";
        } else if (flight.pressureFailureSeconds > 0.0) {
            presentation.telemetryMessage = "PRESSURE RUPTURE IN " +
                display::fixed(std::max(0.0, launchPressureGraceSeconds(flightModel) - flight.pressureFailureSeconds), 1) +
                "s \xE2\x80\x94 open the relief valve";
        } else if (flight.courseFailureSeconds > 0.0) {
            presentation.telemetryMessage = "COURSE LOST IN " +
                display::fixed(std::max(0.0, tuning::launch::pilotingCourseFailureSeconds - flight.courseFailureSeconds), 1) +
                "s \xE2\x80\x94 steer toward center";
        } else if (flight.heat >= tuning::launch::pilotingCriticalThreshold) {
            presentation.telemetryMessage = "TEMPERATURE CRITICAL \xE2\x80\x94 reduce throttle or cut engines";
        } else if (flight.pressure >= tuning::launch::pilotingCriticalThreshold) {
            presentation.telemetryMessage = "PRESSURE CRITICAL \xE2\x80\x94 open the relief valve";
        } else if (flight.heat >= tuning::launch::pilotingWarningThreshold) {
            presentation.telemetryMessage = "Temperature caution \xE2\x80\x94 reduce throttle to cool";
        } else if (flight.pressure >= tuning::launch::pilotingWarningThreshold) {
            presentation.telemetryMessage = "Pressure caution \xE2\x80\x94 prepare the relief valve";
        } else if (flight.forecastIncidentIndex >= 0) {
            const TelemetryIncident& incident = flightModel.incidents[static_cast<std::size_t>(flight.forecastIncidentIndex)];
            const std::string incidentType = incident.heat >= incident.pressure && incident.heat >= incident.guidance
                ? "thermal pulse"
                : (incident.pressure >= incident.guidance ? "pressure pulse" : "course disturbance");
            presentation.telemetryMessage = "INCOMING " + incidentType + " IN " +
                display::fixed(flight.incidentWarningSeconds, 1) + "s";
        } else if (std::abs(flight.courseOffset) >= tuning::launch::pilotingCourseCaution) {
            presentation.telemetryMessage = "COURSE CRITICAL \xE2\x80\x94 steer toward the corridor center";
        } else if (std::abs(flight.courseOffset) >= tuning::launch::pilotingCourseSafe) {
            presentation.telemetryMessage = "Course caution \xE2\x80\x94 correct toward center";
        } else {
            presentation.telemetryMessage = event.message;
        }
        presentation.primaryActions = primaryFlightActions(
            actions,
            arkDiscovered(state),
            launchUsesOuterExpeditionRecovery(catalog, destination));
        if (advancedFlightControlsUnlocked(state, catalog, flightModel)) {
            presentation.systemActions = systemFlightActions(actions, pressureReliefUsed);
        }
        return presentation;
    }

    presentation.displayedMultiplier = actions.returningHome
        ? returnTelemetryMultiplier(returnBurnMultiplier, flightModel.crashMultiplier, returnElapsed, returnDuration)
        : currentMultiplier;
    presentation.returnProgress = flight_progress::returnCompletion(returnElapsed, returnDuration);
    presentation.recoveryRisk = returnHomeRisk(flightModel, catalog, state, presentation.displayedMultiplier);

    presentation.metrics.push_back(panelMetric(text::labels::burnDepth, display::multiplier(presentation.displayedMultiplier)));
    presentation.metrics.push_back(panelMetric(
        actions.returningHome ? text::labels::returnProgress :
            (flightModel.config.frontierTransfer ? text::labels::requiredBurn : text::labels::dataGoal),
        actions.returningHome ? display::percent(presentation.returnProgress) :
            display::multiplier(flightModel.config.frontierTransfer ? destination.targetMultiplier : flightModel.config.burnGoalMultiplier)));
    presentation.metrics.push_back(panelMetric(text::labels::returnRisk, display::percent(presentation.recoveryRisk)));
    if (flightModel.objectiveConfidence > 0.0) {
        presentation.metrics.push_back(panelMetric("Confidence", display::percent(flightModel.objectiveConfidence)));
    }

    const TelemetryEvent event = telemetryAt(flightModel, presentation.displayedMultiplier);
    const auto samples = telemetrySamples(event);
    presentation.telemetry.assign(samples.begin(), samples.end());
    presentation.telemetryMessage = event.message;
    presentation.primaryActions = primaryFlightActions(
        actions,
        arkDiscovered(state),
        launchUsesOuterExpeditionRecovery(catalog, destination));
    if (advancedFlightControlsUnlocked(state, catalog, flightModel)) {
        presentation.systemActions = systemFlightActions(actions, pressureReliefUsed);
    }
    return presentation;
}

} // namespace rocket
