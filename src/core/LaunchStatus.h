#pragma once

#include "core/GameText.h"
#include "core/LaunchSimulation.h"
#include "core/Telemetry.h"
#include "core/Tuning.h"

#include <string>

namespace rocket {

struct LaunchStatusContext {
    TelemetryEvent event;
    FlightActionState actions;
    bool frontierTransfer = false;
    bool arkKnown = false;
    bool returnDriftHome = false;
    bool pastDataGoal = false;
    double returnProgress = 0.0;
};

inline std::string launchStatusLine(const LaunchStatusContext& context)
{
    if (context.actions.returningHome) {
        if (context.event.warning > tuning::session::returnWarningThreshold ||
            context.event.heat > tuning::launch::warningCriticalThreshold) {
            return context.returnDriftHome
                ? text::returnDriftWarning(context.event.message)
                : text::returnBurnWarning(context.event.message);
        }

        if (context.returnProgress < tuning::session::returnEarlyProgressThreshold) {
            return context.returnDriftHome
                ? text::status::fuelReserveGoneForHome(context.arkKnown)
                : std::string(text::status::returnBurnRotating);
        }

        return context.returnDriftHome
            ? text::status::coastingHomeForHome(context.arkKnown)
            : std::string(text::status::returnBurnUnderway);
    }

    if (context.event.warning > tuning::launch::warningCriticalThreshold) {
        return text::telemetryDecision(context.event.message);
    }

    if (context.event.warning > tuning::launch::warningCautionThreshold ||
        context.event.heat > tuning::session::heatCautionThreshold) {
        return text::telemetryStatement(context.event.message);
    }

    if (context.actions.cutEnginesActive) {
        return context.pastDataGoal
            ? std::string(text::status::enginesCutAfterGoal)
            : std::string(text::status::enginesCut);
    }

    if (context.frontierTransfer) {
        return std::string(text::status::transferBurnStable);
    }

    return context.pastDataGoal
        ? text::status::dataGoalReachedForHome(context.arkKnown)
        : text::status::provingBurnStableForHome(context.arkKnown);
}

} // namespace rocket
