#pragma once

#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"

#include <algorithm>
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
    const int identifiedArtifacts = static_cast<int>(std::count_if(state.meta.artifacts.begin(), state.meta.artifacts.end(), [](const ArtifactRecord& artifact) {
        return artifact.identified;
    }));
    const int artifactCount = static_cast<int>(state.meta.artifacts.size());

    return {
        detailPresentationRow(text::panel::details::blueprints, std::to_string(state.meta.blueprintProgress)),
        detailPresentationRow(text::panel::details::commonMaterials, std::to_string(state.meta.materials.common)),
        detailPresentationRow(text::panel::details::rareMaterials, std::to_string(state.meta.materials.rare)),
        detailPresentationRow(text::panel::details::exoticMaterials, std::to_string(state.meta.materials.exotic)),
        detailPresentationRow(text::panel::details::artifacts, text::panel::artifactSummary(identifiedArtifacts, artifactCount)),
        detailPresentationRow(text::panel::details::shipsLost, std::to_string(state.meta.shipsLost)),
        detailPresentationRow(text::panel::details::astronautsLost, std::to_string(state.meta.astronautsLost)),
        detailPresentationRow(text::panel::details::furthestTier, std::to_string(state.meta.furthestTier)),
        detailPresentationRow(text::panel::details::closestSurvival, state.meta.closestSurvivalMargin > 0.0 ? display::multiplier(state.meta.closestSurvivalMargin) : std::string(text::enums::launchResult::none)),
        detailPresentationRow(text::panel::details::maxBurnDepth, display::multiplier(state.meta.maxBurnDepth)),
        detailPresentationRow(text::panel::details::maxPeakWarning, display::percent(state.meta.maxPeakWarning)),
        detailPresentationRow(text::panel::details::maxPeakAbort, display::percent(state.meta.maxPeakAbortRisk)),
        detailPresentationRow(text::panel::details::bestCreditDelta, display::signedMoney(state.meta.bestCreditDelta)),
        detailPresentationRow(text::panel::details::worstCreditDelta, display::signedMoney(state.meta.worstCreditDelta))
    };
}

inline std::string artifactArchiveOrigin(const ContentCatalog& catalog, const ArtifactRecord& artifact)
{
    if (const Destination* destination = catalog.findDestination(artifact.originDestinationId)) {
        return destination->name;
    }
    return artifact.originDestinationId.empty() ? std::string(text::enums::unknown) : artifact.originDestinationId;
}

inline std::string artifactArchiveLabel(const ContentCatalog& catalog, const ArtifactRecord& artifact, int index)
{
    return artifactArchiveOrigin(catalog, artifact) + " artifact " + std::to_string(index + 1);
}

inline std::vector<DetailPresentationRow> artifactArchivePresentation(const GameState& state, const ContentCatalog& catalog)
{
    std::vector<DetailPresentationRow> rows;
    if (state.meta.artifacts.empty()) {
        return rows;
    }

    rows.push_back(detailPresentationHeader(text::panel::details::artifactArchive));
    for (std::size_t i = 0; i < state.meta.artifacts.size(); ++i) {
        const ArtifactRecord& artifact = state.meta.artifacts[i];
        rows.push_back(detailPresentationRow(
            artifactArchiveLabel(catalog, artifact, static_cast<int>(i)),
            artifact.identified ? text::panel::details::decoded : text::panel::details::awaitingResearch));
    }
    return rows;
}

inline std::vector<DetailPresentationRow> legacyDetailsPresentation(const GameState& state, const ContentCatalog& catalog)
{
    std::vector<DetailPresentationRow> rows = legacyDetailsPresentation(state);
    std::vector<DetailPresentationRow> artifactRows = artifactArchivePresentation(state, catalog);
    rows.insert(rows.end(), artifactRows.begin(), artifactRows.end());
    return rows;
}

} // namespace rocket
