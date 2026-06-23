#pragma once

#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"

#include <algorithm>
#include <string>
#include <vector>

namespace rocket {

inline std::vector<DetailPresentationRow> crewDetailsPresentation(const GameState& state, const ContentCatalog& catalog)
{
    std::vector<DetailPresentationRow> rows;
    const Astronaut* astronaut = activeAstronaut(state);
    const CrewUpgradeStats crewUpgrades = aggregateCrewUpgradeStats(state, catalog);
    const HangarOperationPreview hangarOps = hangarOperationPreview(state, catalog);

    if (astronaut != nullptr) {
        const int stressSteps = crewStressStepCount(astronaut->stress);
        rows.push_back(detailPresentationRow(text::panel::details::active, astronaut->name));
        rows.push_back(detailPresentationRow(text::panel::details::crewClass, astronaut->background));
        rows.push_back(detailPresentationRow(text::panel::details::trait, astronaut->trait));
        rows.push_back(detailPresentationRow(text::panel::details::training, display::trainingWithEffective(astronaut->training, effectiveTrainingLevel(*astronaut))));
        rows.push_back(detailPresentationRow(text::panel::details::stress, display::stressWithSteps(astronaut->stress, stressSteps)));
        rows.push_back(detailPresentationRow(text::panel::details::stressEffects, display::crewStressEffects(crewNavigationPenaltyFromStress(astronaut->stress), crewAbortRiskMultiplierFromStress(astronaut->stress))));
        rows.push_back(detailPresentationRow(text::panel::details::status, std::string(toString(astronaut->status))));
    } else {
        rows.push_back(detailPresentationRow(text::panel::details::active, text::panel::noneCleared));
    }

    rows.push_back(detailPresentationHeader(text::panel::details::crewFacilities));
    if (state.run.crewUpgradeIds.empty()) {
        rows.push_back(detailPresentationRow(text::panel::details::facilities, text::panel::baselineTrainingRoom));
    } else {
        for (const std::string& upgradeId : state.run.crewUpgradeIds) {
            if (const CrewUpgrade* upgrade = catalog.findCrewUpgrade(upgradeId)) {
                rows.push_back(detailPresentationRow(std::string(toString(upgrade->rarity)), upgrade->name));
            }
        }
    }

    rows.push_back(detailPresentationHeader(text::panel::details::facilityEffects));
    rows.push_back(detailPresentationRow(text::panel::details::simulatorGain, text::panel::details::trainingDelta(hangarOps.trainingGain)));
    rows.push_back(detailPresentationRow(text::panel::details::simulatorStress, text::panel::details::stressDelta(hangarOps.trainingStressGain)));
    rows.push_back(detailPresentationRow(text::panel::details::medicalRest, text::panel::details::stressRecoveryNow(hangarOps.restStressRecovery)));
    rows.push_back(detailPresentationRow(text::panel::details::launchStress, text::panel::details::launchStressRelief(crewUpgrades.launchStressRelief)));
    rows.push_back(detailPresentationRow(text::panel::details::traitModifiers, display::signedPercent(std::max(0.0, crewUpgrades.traitModifier))));

    return rows;
}

} // namespace rocket
