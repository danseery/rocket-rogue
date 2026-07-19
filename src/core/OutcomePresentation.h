#pragma once

#include "core/ContentIds.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"
#include "core/GameTypes.h"
#include "core/Tuning.h"

#include <string>
#include <string_view>
#include <vector>

namespace rocket {

struct LaunchOutcomeMetricPresentation {
    std::string_view label;
    std::string value;
};

struct LaunchOutcomeMetricGroupPresentation {
    std::string_view title;
    std::string_view cssClass;
    std::vector<LaunchOutcomeMetricPresentation> metrics;
};

struct AchievementPresentation {
    std::string_view id;
    std::string_view title;
    std::string detail;
};

struct CrewFatePresentation {
    bool active = false;
    std::string_view cssClass;
    std::string_view label;
    std::string_view title;
    std::string_view detail;
};

struct LaunchOutcomePresentation {
    std::string_view label;
    std::string_view nextActionLabel;
    CrewFatePresentation crewFate;
    std::vector<LaunchOutcomeMetricGroupPresentation> metricGroups;
    std::vector<std::string> notes;
    std::vector<AchievementPresentation> achievements;
};

struct LaunchOutcomeSummaryPresentation {
    std::string title;
    std::string consequence;
    std::string progression;
};

inline LaunchOutcomeSummaryPresentation launchOutcomeSummaryPresentation(const GameState& state, const ContentCatalog& catalog)
{
    const LaunchOutcome& outcome = state.lastOutcome;
    const Destination* destination = catalog.findDestination(outcome.destinationId);
    const bool earthFlight = destination != nullptr && destination->id == content::destination::earthOrbit;
    const bool moonTransfer = destination != nullptr
        && destination->id == content::destination::moon
        && outcome.frontierTransfer;

    if (moonTransfer && outcome.type == LaunchResultType::MissionComplete) {
        return {"LUNAR ARRIVAL", "The expedition has crossed its first frontier.", "Mars route now open"};
    }
    if (moonTransfer) {
        return {
            "TRANSFER LOST",
            "The Moon escaped this burn, but the route is better understood.",
            "Flight Data 3/3  |  Next confidence 100%"
        };
    }

    if (earthFlight) {
        const int required = tuning::mission::readinessBaseRequired;
        const int readiness = std::clamp(state.run.frontierReadiness, 0, required);
        const std::string flightData = "Flight Data " + std::to_string(readiness) + "/" + std::to_string(required);
        if (outcome.type == LaunchResultType::Destroyed) {
            return {"VEHICLE LOST", "The mission ended beyond recovery.", flightData + "  |  Lunar route incomplete"};
        }

        const bool shallow = outcome.ejectMultiplier < 1.0 +
            (destination->targetMultiplier - 1.0) * tuning::rewards::shallowRecoveryTargetShare;
        const double usefulShare = outcome.recoveryMethod == RecoveryMethod::ManualEject
            ? tuning::outcomes::manualEjectUsefulDataTargetShare
            : tuning::outcomes::returnUsefulDataTargetShare;
        const bool usefulReturn = !shallow && outcome.ejectMultiplier >= destination->targetMultiplier * usefulShare;
        if (outcome.type == LaunchResultType::MissionComplete) {
            const int remaining = std::max(0, required - readiness);
            return {
                "ROUTE DATA SECURED",
                "The flight pushed far enough to chart more of the lunar corridor.",
                remaining == 0 ? "Flight Data 3/3  |  Lunar route charted"
                               : flightData + "  |  " + std::to_string(remaining) + (remaining == 1 ? " flight remains" : " flights remain")
            };
        }
        if (usefulReturn) {
            return {"SHIP RECOVERED", "The crew turned back, but brought home a usable flight profile.", flightData + "  |  Lunar route advancing"};
        }
        return {"FLIGHT RECALLED", "The ship came home before its instruments could chart a viable lunar route.", "Moon route locked  |  " + flightData};
    }

    const std::string destinationName = destination == nullptr ? std::string("the frontier") : destination->name;
    if (outcome.type == LaunchResultType::Destroyed) {
        return {"VEHICLE LOST", "The mission ended beyond recovery.", "Review the flight record before rebuilding"};
    }
    if (outcome.frontierTransfer && outcome.type == LaunchResultType::MissionComplete) {
        return {destinationName + " ARRIVAL", "The expedition has opened another frontier.", "Continue to arrival operations"};
    }
    if (outcome.type == LaunchResultType::MissionComplete) {
        return {"MISSION COMPLETE", "The crew returned with a complete flight profile.", "The next route is closer"};
    }
    return {"SHIP RECOVERED", "The crew returned before the flight profile was complete.", "Review the result and prepare the next launch"};
}

inline std::string_view launchOutcomeLabel(const LaunchOutcome& outcome)
{
    if (outcome.type == LaunchResultType::Destroyed) {
        if (outcome.recoveryMethod == RecoveryMethod::ReturnHome) {
            return text::panel::outcomes::returnFailure;
        }
        return outcome.frontierTransfer ? text::panel::outcomes::transferLost : text::panel::outcomes::vehicleLost;
    }

    if (outcome.recoveryMethod == RecoveryMethod::ManualEject) {
        return text::panel::outcomes::emergencyEject;
    }

    if (outcome.recoveryMethod == RecoveryMethod::ReturnHome) {
        return outcome.type == LaunchResultType::MissionComplete
            ? text::panel::outcomes::profileReturned
            : text::panel::outcomes::earlyReturn;
    }

    if (outcome.frontierTransfer) {
        return outcome.type == LaunchResultType::MissionComplete
            ? text::panel::outcomes::transferComplete
            : text::panel::outcomes::transferAborted;
    }

    return outcome.type == LaunchResultType::MissionComplete
        ? text::panel::outcomes::dataProfileComplete
        : text::panel::outcomes::provingReturn;
}

inline std::string_view launchOutcomeNextActionLabel(const LaunchOutcome& outcome, bool opensPostArrivalPhases = false)
{
    if (outcome.type == LaunchResultType::Destroyed) {
        return text::buttons::startReplacementRefit;
    }
    return opensPostArrivalPhases ? text::buttons::arrivalOps : text::buttons::reviewRefitOptions;
}

inline std::vector<std::string> launchOutcomeNotes(const LaunchOutcome& outcome, bool opensPostArrivalPhases = false)
{
    std::vector<std::string> notes;
    if (outcome.type == LaunchResultType::Destroyed) {
        notes.push_back("Burn " + display::multiplier(outcome.ejectMultiplier)
            + " reached the revealed failure point at " + display::multiplier(outcome.crashMultiplier) + ".");
    } else {
        notes.push_back("Burn " + display::multiplier(outcome.ejectMultiplier)
            + " stopped " + display::multiplier(std::max(0.0, outcome.crashMultiplier - outcome.ejectMultiplier))
            + " before the revealed failure point at " + display::multiplier(outcome.crashMultiplier) + ".");
    }
    if (opensPostArrivalPhases) {
        notes.emplace_back(text::panel::messages::postArrivalResearchReady);
    }
    if (!outcome.moduleDestroyedId.empty()) {
        notes.push_back(text::panel::lostModule(outcome.moduleDestroyedId));
    }
    if (outcome.crewKilled) {
        notes.emplace_back(text::panel::messages::crewLossRecorded);
    } else if (outcome.crewInjured) {
        notes.emplace_back(text::panel::messages::crewInjured);
    }
    return notes;
}

inline std::vector<AchievementPresentation> launchOutcomeAchievements(const LaunchOutcome& outcome)
{
    std::vector<AchievementPresentation> achievements;
    const double survivalMargin = outcome.crashMultiplier - outcome.ejectMultiplier;
    if (outcome.type != LaunchResultType::Destroyed && survivalMargin > 0.0 && survivalMargin <= tuning::records::closeCallSurvivalMargin) {
        achievements.push_back({
            content::achievement::skinOfYourTeeth,
            text::panel::achievements::skinOfYourTeethTitle,
            text::panel::achievements::skinOfYourTeethDetail(
                display::multiplier(survivalMargin),
                display::signedPercent(tuning::records::skinOfYourTeethCreditBonus))
        });
    }
    return achievements;
}

inline CrewFatePresentation launchOutcomeCrewFate(const LaunchOutcome& outcome)
{
    if (outcome.crewKilled) {
        return {
            true,
            "lost",
            text::panel::crewFate::label,
            text::panel::crewFate::lostTitle,
            text::panel::crewFate::lostDetail
        };
    }

    if (outcome.type == LaunchResultType::Destroyed) {
        return {
            true,
            "recovered",
            text::panel::crewFate::label,
            text::panel::crewFate::recoveredTitle,
            outcome.crewInjured ? text::panel::crewFate::recoveredInjuredDetail : text::panel::crewFate::recoveredDetail
        };
    }

    return {};
}

inline std::vector<LaunchOutcomeMetricGroupPresentation> launchOutcomeMetricGroups(const LaunchOutcome& outcome)
{
    return {
        {
            text::panel::sections::missionResult,
            "primary",
            {
                {text::labels::outcome, std::string(launchOutcomeLabel(outcome))},
                {text::labels::recovery, std::string(toString(outcome.recoveryMethod))},
                {text::labels::creditDelta, display::signedMoney(outcome.payout - outcome.recoveryCost)}
            }
        },
        {
            text::panel::sections::burnProfile,
            "",
            {
                {text::labels::burnDepth, display::multiplier(outcome.ejectMultiplier)},
                {text::labels::failurePoint, display::multiplier(outcome.crashMultiplier)}
            }
        },
        {
            text::panel::sections::peakTelemetry,
            "",
            {
                {text::labels::peakWarning, display::percent(outcome.peakWarning)},
                {text::labels::peakAbort, display::percent(outcome.peakAbortRisk)}
            }
        }
    };
}

inline LaunchOutcomePresentation launchOutcomePresentation(const LaunchOutcome& outcome, bool opensPostArrivalPhases = false)
{
    return {
        launchOutcomeLabel(outcome),
        launchOutcomeNextActionLabel(outcome, opensPostArrivalPhases),
        launchOutcomeCrewFate(outcome),
        launchOutcomeMetricGroups(outcome),
        launchOutcomeNotes(outcome, opensPostArrivalPhases),
        launchOutcomeAchievements(outcome)
    };
}

} // namespace rocket
