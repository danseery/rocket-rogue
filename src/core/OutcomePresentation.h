#pragma once

#include "core/GameText.h"
#include "core/GameTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace rocket {

struct LaunchOutcomePresentation {
    std::string_view label;
    std::string_view nextActionLabel;
    std::vector<std::string> notes;
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

inline std::string_view launchOutcomeNextActionLabel(const LaunchOutcome& outcome)
{
    return outcome.type == LaunchResultType::Destroyed
        ? text::buttons::startReplacementRefit
        : text::buttons::reviewRefitOptions;
}

inline std::vector<std::string> launchOutcomeNotes(const LaunchOutcome& outcome)
{
    std::vector<std::string> notes;
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

inline LaunchOutcomePresentation launchOutcomePresentation(const LaunchOutcome& outcome)
{
    return {
        launchOutcomeLabel(outcome),
        launchOutcomeNextActionLabel(outcome),
        launchOutcomeNotes(outcome)
    };
}

} // namespace rocket
