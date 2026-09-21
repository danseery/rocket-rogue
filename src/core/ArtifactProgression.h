#pragma once

#include "core/Content.h"
#include "core/GameState.h"

#include <optional>
#include <string>
#include <string_view>

namespace rocket {

struct PreparedSurfaceLanding;
bool artifactCompletionStep(const ScenarioStepDefinition&);
bool artifactCompletionStep(ScenarioEventKind);
const MissionArtifact* missionArtifact(const GameState&, std::string_view scenarioId, std::string_view stepId);
void registerArtifactAboard(GameState&, const ContentCatalog&, const ArtifactRecord&,
    std::string_view siteId = {}, std::string_view scenarioId = {}, std::string_view stepId = {});
void reconcileArtifactCustody(GameState&, const ContentCatalog&);
bool artifactHandInAvailable(const GameState&, const MissionArtifact&);
bool completeBankedArtifact(GameState&, const ContentCatalog&, std::string_view key);
void bankMissionArtifacts(GameState&, const ContentCatalog&);
std::string artifactSectorForBody(const GameState& state, std::string_view systemId, std::string_view bodyId);
int encounterArtifactDepth(const GameState& state, std::string_view systemId, std::string_view bodyId);

struct ProgressionArtifactOpportunity {
    std::string destinationId;
    std::string bodyId;
    std::string artifactId;
    std::string scenarioId;
    std::string stepId;
    std::string miningSiteDefinitionId;
    std::string siteIdentity;
};

struct ProgressionArtifactPlacement {
    std::string artifactId;
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
    std::string_view destinationId,
    std::string_view bodyId = {},
    bool requireActiveStep = true);

struct OrbitalArtifactSignal {
    bool detected = false;
    bool localized = false;
    double bearing = 0.0;
    double depth = 0.0;
};
OrbitalArtifactSignal orbitalArtifactSignal(const GameState& state, const ContentCatalog& catalog,
    const PreparedSurfaceLanding* prepared = nullptr);
ProgressionArtifactPlacement resolveProgressionArtifactPlacement(
    const GameState& state,
    const ContentCatalog& catalog,
    const Destination& destination,
    int miningDifficulty,
    std::string_view siteIdentity);

} // namespace rocket
