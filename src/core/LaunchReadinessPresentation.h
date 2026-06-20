#pragma once

#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"
#include "core/GameUi.h"
#include "core/PanelPresentation.h"
#include "core/Tuning.h"

#include <string>
#include <vector>

namespace rocket {

struct LaunchReadinessPresentation {
    bool hullBlocked = false;
    bool crewBlocked = false;
    bool blocked = false;
    std::vector<std::string> messages;
    std::vector<DetailPresentationRow> details;
    std::vector<PanelButtonPresentation> actions;
};

inline std::string launchRequiredActionLabel(bool hullBlocked, bool crewBlocked)
{
    if (hullBlocked && crewBlocked) {
        return std::string(text::panel::details::repairAndRecruitCrew);
    }
    return std::string(hullBlocked ? text::panel::details::repairVehicle : text::panel::details::recruitCrew);
}

inline LaunchReadinessPresentation launchReadinessPresentation(const GameState& state, const ContentCatalog& catalog)
{
    LaunchReadinessPresentation presentation;
    const Astronaut* astronaut = activeAstronaut(state);
    const HangarOperationPreview hangarOps = hangarOperationPreview(state, catalog);
    presentation.hullBlocked = state.run.shipDamage >= tuning::damage::destroyedShipDamage;
    presentation.crewBlocked = astronaut == nullptr;
    presentation.blocked = presentation.hullBlocked || presentation.crewBlocked;

    if (presentation.hullBlocked) {
        presentation.messages.emplace_back(text::panel::messages::totalHullBlocked);
    }
    if (presentation.crewBlocked) {
        presentation.messages.emplace_back(text::panel::messages::noLivingCrewBlocked);
    }

    presentation.details.push_back(detailPresentationRow(text::labels::hullDamage, display::wholePercent(state.run.shipDamage)));
    presentation.details.push_back(detailPresentationRow(
        text::panel::details::crew,
        astronaut == nullptr ? std::string(text::panel::noneCleared) : astronaut->name));
    presentation.details.push_back(detailPresentationRow(
        text::panel::details::requiredAction,
        presentation.blocked ? launchRequiredActionLabel(presentation.hullBlocked, presentation.crewBlocked) : std::string(text::panel::details::clearForLaunch)));

    if (presentation.hullBlocked) {
        presentation.actions.push_back(hangarOps.repairAvailable
            ? panelActionButton(text::buttons::assignRepairBay, ui::actions::repairShip, "ok")
            : disabledPanelButton(display::needCredits(hangarOps.repairCost)));
    }
    if (presentation.crewBlocked) {
        presentation.actions.push_back(panelActionButton(text::buttons::recruitCrew, ui::actions::recruitCrew, "ok"));
    }

    return presentation;
}

} // namespace rocket
