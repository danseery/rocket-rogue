#pragma once

#include "core/Content.h"
#include "core/GameState.h"

#include <optional>
#include <string>
#include <string_view>

namespace rocket {

struct ProgressionArtifactOpportunity {
    std::string destinationId;
    std::string scenarioId;
    std::string stepId;
    std::string miningSiteDefinitionId;
    std::string siteIdentity;
};

struct ProgressionArtifactPlacement {
    int ordinal = 0;
    int targetDepth = 1;
    int withinDepthSlot = 0;
    int horizontalOffset = 0;
    int verticalOffset = 10;
    int manhattanDistance = 10;
};

bool destinationHasAuthoredProgressionArtifact(
    const ContentCatalog& catalog,
    std::string_view destinationId);
int recoveredProgressionArtifactDestinationCount(
    const GameState& state,
    const ContentCatalog& catalog);
std::optional<ProgressionArtifactOpportunity> unresolvedProgressionArtifactOpportunity(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId);
ProgressionArtifactPlacement resolveProgressionArtifactPlacement(
    const GameState& state,
    const ContentCatalog& catalog,
    const Destination& destination,
    int miningDifficulty,
    std::string_view siteIdentity);

} // namespace rocket
