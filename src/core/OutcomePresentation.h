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

inline LaunchOutcomeSummaryPresentation launchOutcomeSummaryPresentation(const GameState& state, const ContentCatalog& catalog)
{
    const LaunchOutcome& outcome = state.lastOutcome;
    const Destination* destination = catalog.findDestination(outcome.destinationId);
    const bool earthFlight = destination != nullptr && destination->id == content::destination::earthOrbit;
    const bool moonTransfer = destination != nullptr
        && destination->id == content::destination::moon
        && outcome.frontierTransfer;
    const std::string funding = launchFundingSummary(outcome);

    if (moonTransfer && outcome.type == LaunchResultType::MissionComplete) {
        return {
            "LUNAR ARRIVAL",
            "The Moon is ours. Fresh telemetry and renewed backing open the road to Mars.",
            "Mars route open  •  " + funding
        };
    }
    if (moonTransfer) {
        const std::string consequence = outcome.type == LaunchResultType::Destroyed
            ? (outcome.crewKilled
                ? "The vehicle and crew were lost. Their final telemetry completed the lunar route archive."
                : "The vehicle is gone, but rescue teams recovered the crew and the lunar route archive.")
            : "The Moon escaped this burn, but the crew brought home a sharper route.";
        return {
            "TRANSFER LOST",
            consequence,
            "Flight Data 3/3  •  " + funding + "  •  Retry 100%"
        };
    }

    if (earthFlight) {
        const int required = tuning::mission::readinessBaseRequired;
        const int readiness = std::clamp(state.run.frontierReadiness, 0, required);
        const std::string flightData = "Flight Data " + std::to_string(readiness) + "/" + std::to_string(required);
        if (outcome.type == LaunchResultType::Destroyed) {
            return outcome.crewKilled
                ? LaunchOutcomeSummaryPresentation {
                    "CREW LOST",
                    "The vehicle and crew did not return. No new route data survived the loss.",
                    "Flight Data held at " + std::to_string(readiness) + "/" + std::to_string(required) + "  •  Funding lost"}
                : LaunchOutcomeSummaryPresentation {
                    "CREW RECOVERED",
                    "Rescue teams brought the crew home. No new route data survived the loss.",
                    "Flight Data held at " + std::to_string(readiness) + "/" + std::to_string(required) + "  •  Funding lost"};
        }

        if (readiness >= required) {
            return {
                "LUNAR ROUTE CHARTED",
                "The route is complete. Mission Control has cleared the next launch for the Moon.",
                flightData + "  •  " + funding
            };
        }

        const bool shallow = outcome.ejectMultiplier < 1.0 +
            (destination->targetMultiplier - 1.0) * tuning::rewards::shallowRecoveryTargetShare;
        const double usefulShare = outcome.recoveryMethod == RecoveryMethod::ManualEject
            ? tuning::outcomes::manualEjectUsefulDataTargetShare
            : tuning::outcomes::returnUsefulDataTargetShare;
        const bool usefulReturn = !shallow && outcome.ejectMultiplier >= destination->targetMultiplier * usefulShare;
        if (outcome.type == LaunchResultType::MissionComplete) {
            const bool outperformedBrief = outcome.ejectMultiplier > destination->targetMultiplier + 0.001;
            return {
                outperformedBrief ? "ROUTE OUTPERFORMED" : "ROUTE DATA SECURED",
                outperformedBrief
                    ? "The flight delivered the route we needed—and extra distance brought back richer findings."
                    : "The flight delivered the route we needed and renewed the program's backing.",
                flightData + "  •  " + funding
            };
        }
        if (usefulReturn) {
            if (outcome.recoveryMethod == RecoveryMethod::ManualEject) {
                return {
                    "CAPSULE RECOVERED",
                    "The crew and usable telemetry made it home; recovery costs took their share.",
                    flightData + "  •  " + funding
                };
            }
            return {
                "USEFUL DATA HOME",
                "The crew turned back with usable readings and enough backing to keep the program moving.",
                flightData + "  •  " + funding
            };
        }
        return outcome.recoveryMethod == RecoveryMethod::ManualEject
            ? LaunchOutcomeSummaryPresentation {
                "CAPSULE RECOVERED",
                "The crew is safe, but the short flight and rescue bill left no useful return.",
                flightData + "  •  " + funding}
            : LaunchOutcomeSummaryPresentation {
                "FLIGHT RECALLED",
                "The crew is home. The brief fell short, so the lunar route drew no new backing.",
                flightData + "  •  " + funding};
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
    if (outcome.recoveryMethod == RecoveryMethod::ManualEject) {
        return {
            "CAPSULE RECOVERED",
            "The crew and surviving telemetry are home; the recovery bill consumed the mission's return.",
            funding + "  •  Prepare the next launch"
        };
    }
    return {
        "USEFUL DATA HOME",
        "The crew returned early with readings the program can still build on.",
        funding + "  •  Prepare the next launch"
    };
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
