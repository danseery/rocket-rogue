#pragma once

#include "core/GameTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace rocket {

struct ContentCatalog;
struct GameState;

struct ScenarioActionOutcome {
    bool applied = false;
    bool beginsActivity = false;
    ScenarioEventKind activityEvent = ScenarioEventKind::None;
    std::string miningSiteDefinitionId;
    std::string message;
};

struct ScenarioRouteRequirementStatus {
    bool satisfied = true;
    std::string requiredUnlockKey;
    std::string scenarioId;
    std::string stepId;
    int current = 0;
    int required = 0;
};

// This is the common, state-derived source for objective strips, modal copy,
// map checklists, Drone Ops, and activity HUDs. UI code may choose its own
// layout, but it must not recreate campaign state switches or copy rules.
struct ScenarioObjectivePresentation {
    bool available = false;
    std::string scenarioId;
    std::string stepId;
    ScenarioStepState state = ScenarioStepState::Locked;
    std::string location;
    std::string title;
    std::string detail;
    std::string rewardPreview;
    std::string actionLabel;
    std::string failureExplanation;
    int current = 0;
    int required = 0;
    ScenarioEventKind completionEvent = ScenarioEventKind::None;
    // Event targets are typed content IDs. Presentation can use them for a
    // generic progress qualifier (for example, a delivered material) without
    // branching on an authored scenario or destination.
    std::string eventTargetId;
    bool mandatoryBriefing = false;
    bool briefingAcknowledged = false;
    bool firstFailurePending = false;
    // A protected objective can be physically secured aboard the expedition
    // ship before its return-to-Earth settlement records the scenario event.
    // Keep that transient-but-save-backed handoff visible without treating it
    // as completed gameplay progress.
    bool returnPending = false;
    bool activityStarted = false;
    ScenarioActionKind action = ScenarioActionKind::None;
    std::string miningSiteDefinitionId;
};

const ScenarioDefinition* findScenarioDefinition(
    const ContentCatalog& catalog,
    std::string_view scenarioId);
// Resolves a saved runtime instance ID to its immutable authored definition.
// A default authored instance and its definition share an ID; procedural
// instances deliberately do not.
const ScenarioDefinition* scenarioDefinitionForRuntimeId(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view scenarioId);
// A factory persists only deterministic, typed key=value overrides. This
// accessor materializes the immutable template plus those resolved values for
// one runtime instance; it never mutates the content catalog.
ScenarioDefinition resolveScenarioDefinition(
    const ScenarioDefinition& definition,
    const ScenarioInstance& instance);
bool validateScenarioInstance(
    const ContentCatalog& catalog,
    const ScenarioInstance& instance,
    std::string* error = nullptr);
const ScenarioFactoryDefinition* findScenarioFactoryDefinition(
    const ContentCatalog& catalog,
    std::string_view factoryId);
const MiningSiteDefinition* findMiningSiteDefinition(
    const ContentCatalog& catalog,
    std::string_view siteId);

bool validateScenarioCatalog(const ContentCatalog& catalog, std::string* error = nullptr);

void ensureScenarioInstances(GameState& state, const ContentCatalog& catalog);
ScenarioInstance* findScenarioInstance(MetaProgress& meta, std::string_view scenarioId);
const ScenarioInstance* findScenarioInstance(const MetaProgress& meta, std::string_view scenarioId);
ScenarioStepProgress* findScenarioStepProgress(ScenarioInstance& instance, std::string_view stepId);
const ScenarioStepProgress* findScenarioStepProgress(const ScenarioInstance& instance, std::string_view stepId);
const ScenarioStepDefinition* findScenarioStepDefinition(
    const ScenarioDefinition& definition,
    std::string_view stepId);

ScenarioStepState scenarioStepState(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view scenarioId,
    std::string_view stepId);
bool scenarioStepBriefingAcknowledged(
    const GameState& state,
    std::string_view scenarioId,
    std::string_view stepId);

ScenarioActionOutcome performScenarioAction(
    GameState& state,
    const ContentCatalog& catalog,
    std::string_view scenarioId,
    std::string_view stepId,
    ScenarioActionKind action);

bool recordScenarioEvent(
    GameState& state,
    const ContentCatalog& catalog,
    const ScenarioEvent& event);

ScenarioRouteRequirementStatus scenarioRouteRequirementStatus(
    const GameState& state,
    const ContentCatalog& catalog,
    const Destination& destination);
bool scenarioRouteUsesFlightData(
    const GameState& state,
    const ContentCatalog& catalog,
    const Destination& destination);

bool scenarioHasCompletedStep(
    const GameState& state,
    std::string_view scenarioId,
    std::string_view stepId);

ScenarioObjectivePresentation scenarioObjectivePresentation(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view scenarioId,
    std::string_view stepId);
ScenarioObjectivePresentation scenarioObjectiveForDestination(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId);
// Returns an actionable authored departure Flyby independently of ordinary
// objective ranking, so arrival screens cannot route players into a generic
// pass-through while the required departure challenge is active.
ScenarioObjectivePresentation scenarioDepartureChallengeForDestination(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId);
ScenarioObjectivePresentation scenarioObjectiveForMining(
    const GameState& state,
    const ContentCatalog& catalog);

// Creates a deterministic instance from an authored template. The resolved
// parameters are persisted by SaveData, so generators never reroll a save.
ScenarioInstance makeProceduralScenarioInstance(
    const ContentCatalog& catalog,
    std::string_view factoryId,
    std::uint64_t seed,
    const std::vector<std::string>& resolvedParameters = {});

} // namespace rocket
