#pragma once

#include "core/ContentIds.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"
#include "core/GameTypes.h"
#include "core/Tuning.h"

#include <cmath>
#include <string>
#include <string_view>
#include <utility>
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

inline std::string launchFundingSummary(const LaunchOutcome& outcome)
{
    if (outcome.type == LaunchResultType::Destroyed) {
        return "Funding lost";
    }
    const double netFunding = outcome.payout - outcome.recoveryCost;
    return std::abs(netFunding) < 0.5
        ? "Funding +0"
        : "Funding " + display::signedMoney(netFunding);
}

inline bool rewardedLaunchLessonReturn(const LaunchOutcome& outcome)
{
    const double netFunding = outcome.payout - outcome.recoveryCost;
    return !outcome.frontierTransfer &&
        outcome.recoveryMethod == RecoveryMethod::ReturnHome &&
        outcome.failureCause == LaunchFailureCause::None &&
        (std::abs(netFunding - tuning::launchProgression::lessonReward) < 0.5 ||
            std::abs(netFunding -
                (tuning::launchProgression::lessonReward +
                    tuning::launchProgression::fuelSurveySafetyBonus)) < 0.5);
}

inline std::string launchLessonUpgradeName(LaunchTrainingStage nextStage)
{
    switch (nextStage) {
    case LaunchTrainingStage::FlightControlsCalibration: return "Fuel Tanks I";
    case LaunchTrainingStage::MoonTransfer: return "Flight Controls I";
    case LaunchTrainingStage::MarsTransfer: return "Engine Cooling I";
    case LaunchTrainingStage::JupiterTransfer: return "Hull Plating I";
    case LaunchTrainingStage::FuelCalibration:
    case LaunchTrainingStage::ThermalManagement:
    case LaunchTrainingStage::HullIntegrity:
    case LaunchTrainingStage::Complete:
        break;
    }
    return "the next launch upgrade";
}

inline LaunchOutcomeSummaryPresentation launchOutcomeSummaryPresentation(const GameState& state, const ContentCatalog& catalog)
{
    const LaunchOutcome& outcome = state.lastOutcome;
    const Destination* destination = catalog.findDestination(outcome.destinationId);
    const bool teachingArrival = destination != nullptr &&
        destination->requiresArrivalSurveySequence && outcome.frontierTransfer;
    const std::string funding = launchFundingSummary(outcome);

    if (outcome.failureCause == LaunchFailureCause::LunarImpact) {
        return {
            "LUNAR IMPACT",
            "The uncalibrated landing solution carried the ship into the Moon. Flight Controls I is required before attempting a lunar transfer.",
            "No calibration telemetry validated | Rebuild and complete the test flight"
        };
    }
    if (outcome.failureCause == LaunchFailureCause::ThermalRunaway) {
        return {
            "ENGINES TOASTED",
            "Your engines got a little too enthusiastic and cooked themselves before Mars. Cut Engines Off to shed heat before the red line.",
            "Thermal runaway | No upgrade unlocked | Let them cool and retry"
        };
    }
    if (outcome.failureCause == LaunchFailureCause::CourseLost) {
        return {
            "OFF COURSE",
            "You missed the calibration lane and went careening off into oblivion. Keep the ship centered before the warning becomes critical.",
            "Course lost | No upgrade unlocked | Correct the drift and retry"
        };
    }
    if (outcome.fuelSurveyReturnTiming == FuelSurveyReturnTiming::Late) {
        return {
            "LATE RETURN",
            "The route data was recovered, but the ship passed the safe turnaround window before committing home.",
            "Fuel Tanks I unlocked | Safety bonus -3 | Pilot stress +5"
        };
    }
    if (outcome.failureCause == LaunchFailureCause::TrainingRescue) {
        if (state.launchConfig.missionKind == LaunchMissionKind::FlightControlsCalibration) {
            return {
                "OFF COURSE",
                "You missed the calibration lane and went careening off into oblivion. Mission Control pulled the plug before the test ship joined you.",
                "No upgrade unlocked | Correct the drift and retry"
            };
        }
        return {
            "TRAINING RESCUE",
            "Mission Control recovered the crew after the ship exhausted its fuel before safe completion. No calibration telemetry was validated.",
            "No upgrade unlocked | Review the warning and retry"
        };
    }
    if (state.meta.launchLessons.stage != LaunchTrainingStage::Complete &&
        rewardedLaunchLessonReturn(outcome)) {
        const std::string upgrade = launchLessonUpgradeName(state.meta.launchLessons.stage);
        return {
            "CALIBRATION TELEMETRY VALIDATED",
            "The crew completed the lesson and returned the test ship safely.",
            funding + " | Install " + upgrade
        };
    }
    const bool calibrationMission = !outcome.frontierTransfer &&
        outcome.type != LaunchResultType::Destroyed &&
        outcome.failureCause == LaunchFailureCause::None &&
        state.launchConfig.missionKind != LaunchMissionKind::Standard &&
        state.launchConfig.destinationId == outcome.destinationId;
    if (calibrationMission) {
        return {
            "CALIBRATION INCOMPLETE",
            "The ship returned before the marked data point. The crew is safe, but the lesson is not complete.",
            funding + " | No upgrade unlocked"
        };
    }
    if (outcome.frontierTransfer &&
        outcome.type != LaunchResultType::MissionComplete &&
        state.meta.launchLessons.stage != LaunchTrainingStage::Complete) {
        return {
            "TRANSFER INCOMPLETE",
            "The ship did not reach the destination. Review fuel and the active lesson before trying again.",
            funding + " | Retry the route"
        };
    }

    if (teachingArrival && outcome.type == LaunchResultType::MissionComplete) {
        return {
            destination->name + " ARRIVAL",
            "The expedition reached the first landing site. Continue the active scenario objective before the next route opens.",
            "Arrival operations ready  •  " + funding
        };
    }
    if (teachingArrival) {
        const std::string consequence = outcome.type == LaunchResultType::Destroyed
            ? (outcome.crewKilled
                ? "The vehicle and crew were lost. Their final telemetry remains in the route archive."
                : "The vehicle is gone, but rescue teams recovered the crew and the route archive.")
            : "The destination was not reached. The crew can review the visible limit and try again.";
        return {
            "TRANSFER INCOMPLETE",
            consequence,
            funding + " | Retry the route"
        };
    }

    const std::string destinationName = destination == nullptr ? std::string("the frontier") : destination->name;
    if (outcome.type == LaunchResultType::Destroyed) {
        return outcome.crewKilled
            ? LaunchOutcomeSummaryPresentation {
                "CREW LOST",
                "The vehicle and crew did not return. Their final telemetry remains in the archive.",
                "Funding lost  •  Rebuild the expedition"}
            : LaunchOutcomeSummaryPresentation {
                "CREW RECOVERED",
                "Rescue teams brought the crew home, but the vehicle and mission reserve are gone.",
                "Funding lost  •  Rebuild the expedition"};
    }
    if (outcome.frontierTransfer && outcome.type == LaunchResultType::MissionComplete) {
        return {
            destinationName + " ARRIVAL",
            "The expedition opened a new frontier and returned a priceless first survey.",
            funding + "  •  Arrival operations ready"
        };
    }
    if (outcome.type == LaunchResultType::MissionComplete) {
        const bool outperformedBrief = destination != nullptr && outcome.ejectMultiplier > destination->targetMultiplier + 0.001;
        return {
            outperformedBrief ? "SURVEY OUTPERFORMED" : "MISSION COMPLETE",
            outperformedBrief
                ? "The crew pushed beyond the brief. Richer findings brought stronger backing home."
                : "The crew returned with the complete profile mission control requested.",
            funding + "  •  The next route is closer"
        };
    }
    return {
        "SAFE RETURN",
        "The crew and ship are home. Any completed lesson telemetry has been validated.",
        funding + "  •  Prepare the next launch"
    };
}

inline std::string_view launchOutcomeLabel(const LaunchOutcome& outcome)
{
    if (outcome.failureCause == LaunchFailureCause::LunarImpact) {
        return "Lunar Impact";
    }
    if (outcome.failureCause == LaunchFailureCause::TrainingRescue) {
        return "Training Rescue";
    }
    if (outcome.type == LaunchResultType::Destroyed) {
        if (outcome.recoveryMethod == RecoveryMethod::ReturnHome) {
            return text::panel::outcomes::returnFailure;
        }
        return outcome.frontierTransfer ? text::panel::outcomes::transferLost : text::panel::outcomes::vehicleLost;
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

inline std::vector<std::string> launchOutcomeNotes(
    const LaunchOutcome& outcome,
    bool opensPostArrivalPhases = false,
    LaunchMissionKind missionKind = LaunchMissionKind::Standard)
{
    std::vector<std::string> notes;
    if (outcome.pilotedFlight) {
        if (outcome.failureCause == LaunchFailureCause::LunarImpact) {
            notes.emplace_back("The ship collided with the Moon because lunar landing guidance has not been calibrated.");
        } else if (outcome.failureCause == LaunchFailureCause::TrainingRescue) {
            notes.emplace_back(missionKind == LaunchMissionKind::FlightControlsCalibration
                ? "The ship left the calibration lane, so Mission Control ended the test before it careened off into oblivion. No calibration telemetry was validated."
                : "Mission Control recovered the crew after the ship exhausted its fuel before safe completion. No calibration telemetry was validated.");
        } else if (outcome.failureCause != LaunchFailureCause::None) {
            notes.push_back("Flight ended by " + std::string(toString(outcome.failureCause)) +
                " after its visible safety countdown expired.");
        } else {
            notes.push_back("Minimum visible safety margin: " + display::percent(outcome.minimumSafetyMargin) + ".");
            if (outcome.fuelSurveyReturnTiming == FuelSurveyReturnTiming::Timely) {
                notes.emplace_back("Fuel Survey safety bonus: +3 credits.");
            } else if (outcome.fuelSurveyReturnTiming == FuelSurveyReturnTiming::Late) {
                notes.emplace_back("Late Fuel Survey return: safety bonus -3 credits; pilot stress +5.");
            }
        }
    } else {
        notes.emplace_back("Legacy launch record preserved for save compatibility.");
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
    const double survivalMargin = outcome.pilotedFlight
        ? outcome.minimumSafetyMargin
        : outcome.crashMultiplier - outcome.ejectMultiplier;
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

inline std::string launchOutcomeRecoveryLabel(
    const GameState& state,
    const ContentCatalog& catalog,
    const LaunchOutcome& outcome)
{
    if (outcome.recoveryMethod == RecoveryMethod::TransferArrival ||
        outcome.recoveryMethod == RecoveryMethod::None) {
        return std::string(toString(outcome.recoveryMethod));
    }

    if (outcome.recoveryMethod != RecoveryMethod::ReturnHome) {
        return "Crew recovered";
    }

    const Destination* destination = catalog.findDestination(outcome.destinationId);
    if (destination == nullptr) {
        destination = &currentDestination(state, catalog);
    }
    const bool outerExpedition = destination != nullptr && destination->oneWayExpedition;
    return text::enums::recovery::returnLabel(
        arkDiscovered(state),
        outerExpedition);
}

inline std::vector<LaunchOutcomeMetricGroupPresentation> launchOutcomeMetricGroups(
    const LaunchOutcome& outcome,
    std::string recoveryLabel = {})
{
    if (recoveryLabel.empty()) {
        recoveryLabel = std::string(toString(outcome.recoveryMethod));
    }
    std::vector<LaunchOutcomeMetricGroupPresentation> groups {
        {
            text::panel::sections::missionResult,
            "primary",
            {
                {text::labels::outcome, std::string(launchOutcomeLabel(outcome))},
                {text::labels::recovery, std::move(recoveryLabel)},
                {text::labels::creditDelta, display::signedMoney(outcome.payout - outcome.recoveryCost)}
            }
        },
        outcome.pilotedFlight
            ? LaunchOutcomeMetricGroupPresentation {
                "Flight systems",
                "",
                {
                    {"Safety margin", display::percent(outcome.minimumSafetyMargin)},
                    {"Hull damage", display::wholePercent(outcome.shipDamage)},
                    {"Terminal cause", outcome.failureCause == LaunchFailureCause::None
                        ? std::string("None")
                        : std::string(toString(outcome.failureCause))}
                }
            }
            : LaunchOutcomeMetricGroupPresentation {
                "Archived record",
                "",
                {
                    {"Record", "Legacy launch"},
                    {"Runtime", "Retired rules"}
                }
            }
    };
    return groups;
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

inline LaunchOutcomePresentation launchOutcomePresentation(
    const GameState& state,
    const ContentCatalog& catalog,
    bool opensPostArrivalPhases = false)
{
    const LaunchOutcome& outcome = state.lastOutcome;
    return {
        launchOutcomeLabel(outcome),
        launchOutcomeNextActionLabel(outcome, opensPostArrivalPhases),
        launchOutcomeCrewFate(outcome),
        launchOutcomeMetricGroups(
            outcome,
            launchOutcomeRecoveryLabel(state, catalog, outcome)),
        launchOutcomeNotes(outcome, opensPostArrivalPhases, state.launchConfig.missionKind),
        launchOutcomeAchievements(outcome)
    };
}

} // namespace rocket
