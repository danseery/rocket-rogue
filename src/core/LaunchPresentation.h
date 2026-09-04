#pragma once

#include "core/GameFormat.h"
#include "core/FlightImpactPresentation.h"
#include "core/FlightSystem.h"
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
            "The Moon is out of range. When FUEL warns, Turn Around any time before the tank reaches 0. The return burn is protected.";
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
    case LaunchMissionKind::StraylightApproach:
        presentation.objectiveTitle = "REACH THE CONTACT";
        presentation.objectiveCopy =
            "Autoguidance has the corridor. Cross the quiet beyond Neptune and see what has been waiting in the dark.";
        break;
    case LaunchMissionKind::Standard:
        presentation.objectiveTitle = "REACH " + destination.name;
        if (launch.asteroidsEnabled) {
            presentation.objectiveCopy = "Manage fuel, temperature, course, and hull damage through the asteroid belt.";
        } else if (launch.heatEnabled) {
            presentation.objectiveCopy = "Manage fuel and temperature. Turn Engines Off before heat reaches critical.";
        } else if (launch.manualControlsEnabled) {
            presentation.objectiveCopy = "Reach " + destination.name + ". Higher throttle uses more fuel.";
        } else {
            presentation.objectiveCopy = "Watch FUEL and preserve enough for the return leg.";
        }
        break;
    }
}

inline std::string launchStatusMessage(
    const PreparedLaunch& launch,
    const FlightRunState& flight,
    const FlightActionState& actions)
{
    if (flight.physicalFlight) {
        switch (flight.phase) {
        case FlightPhase::Departure:
        case FlightPhase::Transfer:
            return "FLY THE SHIP — rotate, thrust, and release to coast";
        case FlightPhase::TargetApproach:
            return "Shape a loop around the planet. Release thrust to confirm.";
        case FlightPhase::Orbiting:
            return "ORBIT CAPTURED — fly inward through the green descent gate";
        case FlightPhase::Descent:
            return "DEORBITING — reduce sideways speed before the surface";
        case FlightPhase::Landing:
            return "LANDING — V " + display::fixed(flight.landing.verticalVelocity, 1) +
                " m/s • L " + display::fixed(flight.landing.lateralVelocity, 1) + " m/s";
        case FlightPhase::Landed:
            return flight.landing.hardLanding ? "HARD LANDING" : "TOUCHDOWN";
        case FlightPhase::Flyby:
            return "FLYBY — destination influence exited";
        case FlightPhase::Impact:
            return "IMPACT";
        case FlightPhase::Complete:
            return "FLIGHT COMPLETE";
        }
    }
    if (launch.config.missionKind == LaunchMissionKind::FuelCalibration &&
        actions.returningHome) {
        switch (flight.fuelSurveyReturnTiming) {
        case FuelSurveyReturnTiming::Timely:
            return "RETURN COMMITTED \xE2\x80\x94 Safety bonus secured";
        case FuelSurveyReturnTiming::Late:
            return "RETURN COMMITTED \xE2\x80\x94 Late-return penalty recorded";
        case FuelSurveyReturnTiming::Unqualified:
            return "RETURN COMMITTED \xE2\x80\x94 Route data incomplete";
        }
    }
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
    const CalibrationFuelWarning calibrationWarning = calibrationFuelWarning(launch, flight);
    if (calibrationWarning != CalibrationFuelWarning::None) {
        if (launch.config.missionKind == LaunchMissionKind::FuelCalibration) {
            switch (calibrationWarning) {
            case CalibrationFuelWarning::Critical:
                return "LATE RETURN \xE2\x80\x94 Turn Around Now \xE2\x80\xA2 -3 credits";
            case CalibrationFuelWarning::TurnAround:
                return "TURN AROUND NOW \xE2\x80\x94 +3 credit safety bonus";
            case CalibrationFuelWarning::Approaching:
                return "TURNAROUND APPROACHING \xE2\x80\x94 Prepare to Turn Around";
            case CalibrationFuelWarning::None:
                break;
            }
        }
        switch (calibrationWarning) {
        case CalibrationFuelWarning::Critical:
            return "FUEL CRITICAL \xE2\x80\x94 Turn Around Now";
        case CalibrationFuelWarning::TurnAround:
            return "TURN AROUND NOW \xE2\x80\x94 Return burn is protected";
        case CalibrationFuelWarning::Approaching:
            return "TURNAROUND APPROACHING \xE2\x80\x94 Prepare to Turn Around";
        case CalibrationFuelWarning::None:
            break;
        }
    }
    if (!actions.returningHome && !launch.config.frontierTransfer &&
        flight.projectedFuelReserve <= 0.0) {
        return "FUEL LOW \xE2\x80\x94 Turn Around now";
    }
    if (!actions.returningHome && launch.config.frontierTransfer &&
        flight.projectedFuelReserve < -0.05) {
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
        return "COLLECTING ROUTE DATA";
    case LaunchMissionKind::FlightControlsCalibration:
        return "UNCALIBRATED LANDING — turn at the yellow test line. The Moon will cause an impact.";
    case LaunchMissionKind::ThermalManagement:
        return "Watch TEMPERATURE. Engines Off always cools the ship.";
    case LaunchMissionKind::AsteroidBelt:
        return "Steer through the gaps. Protect the HULL.";
    case LaunchMissionKind::StraylightApproach:
        return "CONTACT LOCKED — automatic approach in progress";
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
    const FlightRunState* launchFlight = nullptr)
{
    LaunchPanelPresentation presentation;
    const Destination& destination = launchDisplayDestination(state, catalog, flightModel);
    const bool straylightApproach =
        flightModel.config.missionKind == LaunchMissionKind::StraylightApproach;
    presentation.destinationName = straylightApproach ? "Unknown Contact" : destination.name;
    const RouteTransitState& transit = flightModel.config.routeTransit;
    const Destination* origin = transit.active()
        ? catalog.findDestination(transit.originDestinationId)
        : nullptr;
    const std::string routeLabel = straylightApproach
        ? "Neptune \xE2\x86\x92 Unknown Contact"
        : origin == nullptr
        ? destination.name
        : origin->name + " \xE2\x86\x92 " + destination.name;
    if (transit.active() && transit.intent == RouteTransitIntent::Recovery) {
        presentation.sectionTitle = launchFlight != nullptr
            ? (actions.returningHome ? "Return \xE2\x80\xA2 " : "Recovery \xE2\x80\xA2 ") + routeLabel
            : "Recovery \xE2\x80\xA2 " + routeLabel;
        presentation.objectiveTitle = "RECOVER TO " + destination.name;
        presentation.objectiveCopy = "Fly the return route to " + destination.name + ". Turning around returns to the passed destination.";
    } else if (transit.active() && transit.intent == RouteTransitIntent::Reapproach) {
        presentation.sectionTitle = launchFlight != nullptr
            ? (actions.returningHome ? "Return \xE2\x80\xA2 " : "Reapproach \xE2\x80\xA2 ") + routeLabel
            : "Reapproach \xE2\x80\xA2 " + routeLabel;
        presentation.objectiveTitle = "REAPPROACH " + destination.name;
        presentation.objectiveCopy = "Re-enter " + destination.name + " from the recovered staging route.";
    } else if (straylightApproach) {
        presentation.sectionTitle = launchFlight != nullptr
            ? "Outbound \xE2\x80\xA2 Neptune \xE2\x86\x92 Unknown Contact"
            : "Launch \xE2\x80\xA2 Neptune \xE2\x86\x92 Unknown Contact";
    } else {
        presentation.sectionTitle = launchFlight != nullptr
            ? (actions.returningHome ? "Return \xE2\x80\xA2 " : "Outbound \xE2\x80\xA2 ") + routeLabel
            : "Launch \xE2\x80\xA2 " + routeLabel;
    }
    if (presentation.objectiveTitle.empty()) {
        addLaunchLessonCopy(presentation, flightModel, destination);
    }

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

    const FlightRunState& flight = *launchFlight;
    if (flight.physicalFlight) {
        presentation.objectiveTitle = flight.orbit.captured
            ? "DEORBIT AND LAND"
            : (flightModel.orbitRequired ? "CAPTURE ONE ORBIT" : "ORBIT OR LAND");
        presentation.objectiveCopy = flight.orbit.captured
            ? "Fly inward through the green descent gate to enter Landing."
            : "Your trajectory is guidance, not a rail. Rotate, use forward or reverse thrust, and release to coast.";
    }
    presentation.displayedMultiplier = flight.currentMultiplier;
    presentation.returnProgress = flight.returningHome
        ? std::clamp(1.0 - flight.travelProgress, 0.0, 1.0)
        : std::clamp(flight.travelProgress, 0.0, 1.0);
    std::string fuelValue = !actions.returningHome && !flightModel.config.frontierTransfer &&
            flight.projectedFuelReserve <= 0.0
        ? std::string("TURN AROUND")
        : display::fixed(std::max(0.0, flight.fuelRemaining), 0) + " / " +
            display::fixed(flight.fuelCapacity, 0);
    presentation.metrics.push_back(panelMetric(text::labels::transferFuel, std::move(fuelValue)));
    if (flight.physicalFlight) {
        presentation.metrics.push_back(panelMetric(
            "Thrust",
            flight.selectedThrottle > 0.01
                ? "MAIN " + display::percent(flight.selectedThrottle)
                : (flight.selectedThrottle < -0.01
                      ? "REVERSE " + display::percent(std::abs(flight.selectedThrottle))
                      : "COAST")));
        presentation.metrics.push_back(panelMetric(
            "Orbit",
            flight.orbit.captured
                ? "CAPTURED"
                : (!flight.orbit.loopQualifies ? "FIND A LOOP"
                    : (std::abs(flight.selectedThrottle) > 0.001 ? "RELEASE THRUST"
                        : "CONFIRMING " + display::percent(orbitConfirmationProgress(flight))))));
    } else if (flightModel.manualControlsEnabled) {
        presentation.metrics.push_back(panelMetric(
            "Throttle",
            actions.cutEnginesActive
                ? "OFF \xE2\x80\xA2 " + display::percent(flight.selectedThrottle) + " set"
                : display::percent(flight.selectedThrottle)));
    }
    if (flightModel.heatEnabled) {
        presentation.metrics.push_back(panelMetric("Temperature", display::percent(flight.heat)));
    }
    if (flightModel.asteroidsEnabled || flight.physicalFlight) {
        presentation.metrics.push_back(panelMetric(
            "Hull",
            flightHullReadout(flight)));
    }

    presentation.telemetryMessage = launchStatusMessage(flightModel, flight, actions);
    if (!straylightApproach && !flight.physicalFlight) {
        presentation.primaryActions = primaryFlightActions(actions);
    }
    if (!flight.physicalFlight) {
        presentation.systemActions = systemFlightActions(flightModel, actions);
    }
    return presentation;
}

} // namespace rocket
