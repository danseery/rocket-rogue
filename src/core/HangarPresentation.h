#pragma once

#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"
#include "core/GameUi.h"
#include "core/Tuning.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rocket {

struct HangarOperationCardPresentation {
    std::string title;
    std::string detail;
    std::string cost;
    std::string_view actionId;
    bool available = false;
    std::string cssClass;
};

inline HangarOperationCardPresentation hangarOperationCard(
    std::string_view title,
    std::string detail,
    std::string cost,
    std::string_view actionId,
    bool available,
    std::string cssClass)
{
    return {
        std::string(title),
        std::move(detail),
        std::move(cost),
        actionId,
        available,
        std::move(cssClass)
    };
}

inline std::vector<HangarOperationCardPresentation> hangarOperationCards(const GameState& state, const ContentCatalog& catalog)
{
    std::vector<HangarOperationCardPresentation> cards;
    const Astronaut* astronaut = activeAstronaut(state);
    const HangarOperationPreview preview = hangarOperationPreview(state, catalog);

    cards.push_back(hangarOperationCard(
        text::panel::ops::repairBay,
        preview.repairAmount > 0 ? text::panel::repairDetail(preview.repairAmount) : std::string(text::panel::messages::noStructuralWork),
        preview.repairAmount > 0 ? display::credits(preview.repairCost) : std::string(text::panel::shipStable),
        ui::actions::repairShip,
        preview.repairAvailable,
        "repair"));

    if (astronaut == nullptr) {
        cards.push_back(hangarOperationCard(
            text::panel::ops::crewIntake,
            std::string(text::panel::messages::emergencyReplacement),
            display::credits(preview.recruitCost),
            ui::actions::recruitCrew,
            preview.recruitAvailable,
            "crew"));
        cards.push_back(hangarOperationCard(
            text::panel::ops::reserveRoster,
            std::string(text::panel::messages::reserveRoster),
            display::credits(tuning::hangar::recruitCost),
            ui::actions::recruitCrew,
            state.run.credits >= tuning::hangar::recruitCost,
            "crew"));
    } else {
        cards.push_back(hangarOperationCard(
            text::panel::ops::simulatorBurn,
            text::panel::simulatorDetail(preview.trainingGain, preview.trainingStressGain),
            display::credits(preview.trainingCost),
            ui::actions::trainCrew,
            preview.trainingAvailable,
            "crew"));
        cards.push_back(hangarOperationCard(
            text::panel::ops::medicalRest,
            text::panel::restDetail(preview.restStressRecovery),
            display::credits(preview.restCost),
            ui::actions::restCrew,
            preview.restAvailable,
            "crew"));
    }

    return cards;
}

} // namespace rocket
