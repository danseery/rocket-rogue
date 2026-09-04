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

// Hull damage remains numeric in the systems that calculate
// it. The Hangar turns that percentage into a quick condition readout so the
// player can feel the ship's state change after a rough mission or repair.
inline std::string_view hullDamageLevel(int damage)
{
    if (damage >= 100) return "Totaled";
    if (damage >= 90) return "Junk";
    if (damage >= 80) return "Mangled";
    if (damage >= 70) return "Damaged";
    if (damage >= 60) return "Beat-up";
    if (damage >= 50) return "Worn";
    if (damage >= 40) return "Scuffed";
    if (damage >= 20) return "Ready";
    if (damage >= 1) return "Like-new";
    return "Pristine";
}

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
    const bool starterLaunchLesson =
        state.meta.launchLessons.stage == LaunchTrainingStage::FuelCalibration ||
        state.meta.launchLessons.stage == LaunchTrainingStage::FlightControlsCalibration;
    const bool salvageRebuild = state.run.shipDamage >= tuning::damage::destroyedShipDamage &&
        preview.repairAmount > 0 &&
        preview.repairCost <= state.run.credits &&
        state.run.credits < tuning::escalatedHangarOpCost(
            tuning::hangar::repairBaseCost + static_cast<double>(preview.repairAmount) * tuning::hangar::repairCostPerDamage,
            state.run.repairOpsThisExpedition);

    if (!starterLaunchLesson) {
        cards.push_back(hangarOperationCard(
            text::panel::ops::repairBay,
            preview.repairAmount > 0
                ? (salvageRebuild ? text::panel::salvageRebuildDetail(preview.repairAmount) : text::panel::repairDetail(preview.repairAmount))
                : std::string(text::panel::messages::noStructuralWork),
            preview.repairAmount > 0
                ? (salvageRebuild ? std::string(text::panel::messages::salvageRebuildCost) : display::credits(preview.repairCost))
                : std::string(text::panel::shipStable),
            ui::actions::repairShip,
            preview.repairAvailable,
            "repair"));
    }

    if (astronaut == nullptr) {
        cards.push_back(hangarOperationCard(
            "Crew Replacement",
            "Accept the next authored specialist with the same race/class perk.",
            "FREE",
            ui::actions::acceptCrewReplacement,
            preview.recruitAvailable,
            "crew"));
    }

    return cards;
}

} // namespace rocket
