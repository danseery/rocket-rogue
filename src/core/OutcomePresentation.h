#pragma once

#include "core/ContentIds.h"
#include "core/GameFormat.h"
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

struct LaunchOutcomePresentation {
    std::string_view label;
    std::string_view nextActionLabel;
    std::vector<LaunchOutcomeMetricGroupPresentation> metricGroups;
    std::vector<std::string> notes;
    std::vector<AchievementPresentation> achievements;
};

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
            text::panel::achievements::skinOfYourTeethDetail(display::multiplier(survivalMargin))
        });
    }
    return achievements;
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
        launchOutcomeMetricGroups(outcome),
        launchOutcomeNotes(outcome, opensPostArrivalPhases),
        launchOutcomeAchievements(outcome)
    };
}

} // namespace rocket
