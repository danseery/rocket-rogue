#pragma once

#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"

#include <string>
#include <vector>

namespace rocket {

inline std::vector<DetailPresentationRow> frontierDetailsPresentation(const GameState& state, const ContentCatalog& catalog)
{
    std::vector<DetailPresentationRow> rows;
    const Destination& currentFrontier = currentDestination(state, catalog);
    const Destination* next = nextDestination(state, catalog);
    const int requiredReadiness = frontierReadinessRequired(state, catalog);

    rows.push_back(detailPresentationRow(text::panel::details::current, currentFrontier.name));
    rows.push_back(detailPresentationRow(
        text::labels::flightData,
        requiredReadiness == 0 ? std::string(text::panel::complete) : display::fraction(state.run.frontierReadiness, requiredReadiness)));
    rows.push_back(detailPresentationRow(text::labels::missionDifficulty, display::signedPercent(missionPressureModifier(state, catalog, currentFrontier))));
    if (next != nullptr) {
        rows.push_back(detailPresentationRow(text::panel::details::next, next->name));
        rows.push_back(detailPresentationRow(text::panel::details::transferBurn, display::multiplier(next->targetMultiplier)));
    } else {
        rows.push_back(detailPresentationRow(text::panel::details::next, text::panel::noneCharted));
    }

    return rows;
}

inline std::vector<DetailPresentationRow> legacyDetailsPresentation(const GameState& state)
{
    return {
        detailPresentationRow(text::panel::details::blueprints, std::to_string(state.meta.blueprintProgress)),
        detailPresentationRow(text::panel::details::shipsLost, std::to_string(state.meta.shipsLost)),
        detailPresentationRow(text::panel::details::astronautsLost, std::to_string(state.meta.astronautsLost)),
        detailPresentationRow(text::panel::details::furthestTier, std::to_string(state.meta.furthestTier))
    };
}

} // namespace rocket
