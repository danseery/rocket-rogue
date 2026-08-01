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

// A short emotional readout for the Hangar header. Stress still uses the
// existing numeric rules everywhere else; this simply makes the crew chip
// react visibly after risky assignments and difficult sorties.
inline std::string_view crewStressLevel(int stress)
{
    if (stress >= 100) return "Crisis";
    if (stress >= 90) return "Panicked";
    if (stress >= 80) return "Alarmed";
    if (stress >= 70) return "Overwhelmed";
    if (stress >= 60) return "Anxious";
    if (stress >= 50) return "Tense";
    if (stress >= 40) return "Focused";
    if (stress >= 20) return "Ready";
    if (stress >= 1) return "Calm";
    return "Sanguine";
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
    const bool salvageRebuild = state.run.shipDamage >= tuning::damage::destroyedShipDamage &&
        preview.repairAmount > 0 &&
        preview.repairCost <= state.run.credits &&
        state.run.credits < tuning::escalatedHangarOpCost(
            tuning::hangar::repairBaseCost + static_cast<double>(preview.repairAmount) * tuning::hangar::repairCostPerDamage,
            state.run.repairOpsThisExpedition);

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

    if (astronaut == nullptr) {
        cards.push_back(hangarOperationCard(
            text::panel::ops::crewIntake,
            std::string(text::panel::messages::emergencyReplacement),
            display::credits(preview.recruitCost),
            ui::actions::recruitCrew,
            preview.recruitAvailable,
            "crew"));
    } else {
        std::string simulatorDetail = text::panel::simulatorDetail(preview.trainingGain, preview.trainingStressGain);
        std::string simulatorCost = display::credits(preview.trainingCost);
        if (astronaut->training >= tuning::crew::maxTraining) {
            simulatorDetail = std::string(text::panel::messages::simulatorMastered);
            simulatorCost = std::string(text::panel::simulatorCapped);
        } else if (astronaut->stress + preview.trainingStressGain > tuning::crew::maxStress) {
            simulatorDetail = std::string(text::panel::messages::simulatorWouldOverstress);
            simulatorCost = std::string(text::panel::crewTooStressed);
        }
        cards.push_back(hangarOperationCard(
            text::panel::ops::simulatorBurn,
            simulatorDetail,
            simulatorCost,
            ui::actions::trainCrew,
            preview.trainingAvailable,
            "crew"));
        cards.push_back(hangarOperationCard(
            text::panel::ops::medicalRest,
            preview.restNeeded ? text::panel::restDetail(preview.restStressRecovery) : std::string(text::panel::noRestDetail),
            preview.restNeeded ? display::credits(preview.restCost) : std::string(text::panel::crewRested),
            ui::actions::restCrew,
            preview.restAvailable,
            "crew"));
    }

    return cards;
}

} // namespace rocket
