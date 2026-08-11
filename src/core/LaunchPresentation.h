#pragma once

#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameUi.h"
#include "core/LaunchSimulation.h"
#include "core/PanelPresentation.h"
#include "core/Tuning.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rocket {

using FlightActionButtonPresentation = PanelButtonPresentation;

struct LaunchPanelPresentation {
    std::string sectionTitle;
    std::string destinationName;
    std::string objectiveTitle;
    std::string objectiveCopy;
    std::vector<PanelMetricPresentation> metrics;
    std::string telemetryMessage;
    std::vector<FlightActionButtonPresentation> primaryActions;
    std::vector<FlightActionButtonPresentation> systemActions;
    double displayedMultiplier = 1.0;
    double returnProgress = 0.0;
};

inline FlightActionButtonPresentation flightActionButton(
    std::string_view label,
    std::string_view actionId,
    std::string cssClass = {})
{
    return panelActionButton(label, actionId, std::move(cssClass));
}

inline FlightActionButtonPresentation disabledFlightActionButton(std::string_view label)
{
    return disabledPanelButton(label);
}

inline std::vector<FlightActionButtonPresentation> primaryFlightActions(const FlightActionState& actions)
{
    return {actions.returningHome
        ? disabledFlightActionButton("Returning")
        : flightActionButton("Turn Around", ui::actions::returnHome, "ok")};
}

inline std::vector<FlightActionButtonPresentation> systemFlightActions(
    const PreparedLaunch& launch,
    const FlightActionState& actions)
{
    if (!launch.heatEnabled) {
        return {};
    }
    return {flightActionButton(
        actions.cutEnginesActive ? "Engines On" : "Engines Off",
        ui::actions::cutEngines,
        actions.cutEnginesActive ? "ok" : "warn")};
}

inline const Destination& launchDisplayDestination(
    const GameState& state,
    const ContentCatalog& catalog,
    const PreparedLaunch& launch)
{
    if (const Destination* destination = catalog.findDestination(launch.config.destinationId)) {
        return *destination;
    }
    return currentDestination(state, catalog);
}

inline void addLaunchLessonCopy(
    LaunchPanelPresentation& presentation,
    const PreparedLaunch& launch,
    const Destination& destination)
{
    switch (launch.config.missionKind) {
    case LaunchMissionKind::FuelCalibration:
        presentation.objectiveTitle = "TURN AROUND ON LOW FUEL";
        presentation.objectiveCopy =
            "The Moon is out of range. Fly until FUEL warns, then Turn Around and bring the route data home.";
        break;
    case LaunchMissionKind::FlightControlsCalibration:
        presentation.objectiveTitle = "CALIBRATE FLIGHT CONTROLS";
        presentation.objectiveCopy =
            "Navigation is not cleared for a lunar landing. Reach the yellow test line, correct the drift, then Turn Around; continuing to the Moon will cause an impact.";
        break;
    case LaunchMissionKind::ThermalManagement:
        presentation.objectiveTitle = "REACH " + destination.name;
        presentation.objectiveCopy =
            "Manage heat with Engines Off. Reaching " + destination.name + " completes the flight and lands the ship.";
        break;
    case LaunchMissionKind::AsteroidBelt:
        presentation.objectiveTitle = "REACH " + destination.name;
        presentation.objectiveCopy =
            "Steer through the asteroid gaps. Reaching " + destination.name + " completes the flight and lands the ship.";
        break;
    case LaunchMissionKind::Standard:
        presentation.objectiveTitle = "REACH " + destination.name;
        if (launch.asteroidsEnabled) {
            presentation.objectiveCopy = "Manage fuel, temperature, course, and hull damage through the asteroid belt.";
        } else if (launch.heatEnabled) {
            presentation.objectiveCopy = "Manage fuel and temperature. Turn Engines Off before heat reaches critical.";
        } else if (launch.manualControlsEnabled) {
            if (launch.config.frontierTransfer && launch.arrivalReserveFuel > 0.0) {
                presentation.objectiveCopy =
                    "Reach " + destination.name + ". Higher throttle uses more fuel.";
            } else {
                presentation.objectiveCopy = "Manage fuel, throttle, and course through the full route.";
            }
        } else {
            presentation.objectiveCopy = "Watch FUEL and preserve enough for the return leg.";
        }
        break;
    }
}

inline std::string launchStatusMessage(
    const PreparedLaunch& launch,
    const LaunchFlightState& flight,
    const FlightActionState& actions)
{
    if (flight.fuelFailureSeconds > 0.0) {
        return "OUT OF FUEL \xE2\x80\x94 rescue in " + display::fixed(
            std::max(0.0, tuning::launch::pilotingFuelFailureSeconds - flight.fuelFailureSeconds),
            1) + "s";
    }
    if (launch.heatEnabled && flight.heatFailureSeconds > 0.0) {
        return "TEMPERATURE CRITICAL \xE2\x80\x94 Engines Off \xE2\x80\x94 " + display::fixed(
            std::max(0.0, tuning::launch::pilotingHeatFailureSeconds - flight.heatFailureSeconds),
            1) + "s";
    }
    if (launch.manualControlsEnabled && flight.courseFailureSeconds > 0.0) {
        return "COURSE LOST \xE2\x80\x94 correct now \xE2\x80\x94 " + display::fixed(
            std::max(0.0, tuning::launch::pilotingCourseFailureSeconds - flight.courseFailureSeconds),
            1) + "s";
    }
    if (launch.asteroidsEnabled && flight.hullRemaining <= flight.hullMaximum * 0.25) {
        return "HULL CRITICAL \xE2\x80\x94 avoid the next asteroid band";
    }
    if (!actions.returningHome && !launch.config.frontierTransfer &&
        flight.projectedFuelReserve <= 0.0) {
        return "FUEL LOW \xE2\x80\x94 Turn Around now";
    }
    if (!actions.returningHome && launch.config.frontierTransfer &&
        flight.projectedFuelReserve < -tuning::launch::arrivalReserveGraceFuel - 0.05) {
        return "FUEL LOW \xE2\x80\x94 reduce throttle";
    }
    if (launch.heatEnabled && flight.heat >= tuning::launch::pilotingCriticalThreshold) {
        return "TEMPERATURE CRITICAL \xE2\x80\x94 turn Engines Off";
    }
    if (launch.heatEnabled && flight.heat >= tuning::launch::pilotingWarningThreshold) {
        return "Temperature rising \xE2\x80\x94 prepare to turn Engines Off";
    }
    if (launch.manualControlsEnabled &&
        std::abs(flight.courseOffset) >= tuning::launch::pilotingCourseCaution) {
        return "COURSE CRITICAL \xE2\x80\x94 steer toward center";
    }
    if (launch.manualControlsEnabled &&
        std::abs(flight.courseOffset) >= tuning::launch::pilotingCourseSafe) {
        return "Course drifting \xE2\x80\x94 correct toward center";
    }
    if (actions.returningHome) {
        return "RETURN LEG \xE2\x80\x94 bring the ship home";
    }
    switch (launch.config.missionKind) {
    case LaunchMissionKind::FuelCalibration:
        return "Watch FUEL. Turn Around when the warning appears.";
    case LaunchMissionKind::FlightControlsCalibration:
        return "UNCALIBRATED LANDING — turn at the yellow test line. The Moon will cause an impact.";
    case LaunchMissionKind::ThermalManagement:
        return "Watch TEMPERATURE. Engines Off always cools the ship.";
    case LaunchMissionKind::AsteroidBelt:
        return "Steer through the gaps. Protect the HULL.";
    case LaunchMissionKind::Standard:
        return "Keep the ship inside its visible limits.";
    }
    return {};
}

inline LaunchPanelPresentation launchPanelPresentation(
    const GameState& state,
    const ContentCatalog& catalog,
    const PreparedLaunch& flightModel,
    double currentMultiplier,
    double,
    double,
    double,
    const FlightActionState& actions,
    const LaunchFlightState* launchFlight = nullptr)
{
    LaunchPanelPresentation presentation;
    const Destination& destination = launchDisplayDestination(state, catalog, flightModel);
    presentation.destinationName = destination.name;
    presentation.sectionTitle = launchFlight != nullptr
        ? (actions.returningHome ? "Return \xE2\x80\xA2 " : "Outbound \xE2\x80\xA2 ") + destination.name
        : "Launch \xE2\x80\xA2 " + destination.name;
    addLaunchLessonCopy(presentation, flightModel, destination);

    if (launchFlight == nullptr) {
        presentation.displayedMultiplier = currentMultiplier;
        std::string fuelValue = display::fixed(flightModel.fuelCapacity, 0) + " fuel";
        presentation.metrics.push_back(panelMetric("Fuel", std::move(fuelValue)));
        if (flightModel.manualControlsEnabled) {
            presentation.metrics.push_back(panelMetric("Throttle", display::percent(tuning::launch::pilotingInitialThrottle)));
        }
        if (flightModel.heatEnabled) {
            presentation.metrics.push_back(panelMetric("Temperature", "READY"));
        }
        if (flightModel.asteroidsEnabled) {
            presentation.metrics.push_back(panelMetric("Hull", "READY"));
        }
        presentation.telemetryMessage = presentation.objectiveCopy;
        return presentation;
    }

    const LaunchFlightState& flight = *launchFlight;
    presentation.displayedMultiplier = flight.currentMultiplier;
    presentation.returnProgress = flight.returningHome
        ? std::clamp(1.0 - flight.travelProgress, 0.0, 1.0)
        : std::clamp(flight.travelProgress, 0.0, 1.0);
    std::string fuelValue = !actions.returningHome && !flightModel.config.frontierTransfer &&
            flight.projectedFuelReserve <= 0.0
        ? std::string("TURN AROUND")
        : display::fixed(std::max(0.0, flight.fuelRemaining), 0) + " / " +
            display::fixed(flight.fuelCapacity, 0);
    presentation.metrics.push_back(panelMetric("Fuel", std::move(fuelValue)));
    if (flightModel.manualControlsEnabled) {
        presentation.metrics.push_back(panelMetric(
            "Throttle",
            actions.cutEnginesActive
                ? "OFF \xE2\x80\xA2 " + display::percent(flight.selectedThrottle) + " set"
                : display::percent(flight.selectedThrottle)));
    }
    if (flightModel.heatEnabled) {
        presentation.metrics.push_back(panelMetric("Temperature", display::percent(flight.heat)));
    }
    if (flightModel.asteroidsEnabled) {
        presentation.metrics.push_back(panelMetric(
            "Hull",
            display::fixed(std::max(0.0, flight.hullRemaining), 0) + " / " +
                display::fixed(flight.hullMaximum, 0) + " HP"));
    }

    presentation.telemetryMessage = launchStatusMessage(flightModel, flight, actions);
    presentation.primaryActions = primaryFlightActions(actions);
    presentation.systemActions = systemFlightActions(flightModel, actions);
    return presentation;
}

} // namespace rocket
