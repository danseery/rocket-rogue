#include "core/ResearchSystem.h"
#include "core/FlightSystem.h"
#include "core/LaunchSimulation.h"
#include "core/ArtifactProgression.h"
#include "core/ContentIds.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/GameUi.h"
#include "core/MiningProgression.h"
#include "core/MiningSystem.h"
#include "core/ScenarioSystem.h"
#include "core/Tuning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace rocket {

bool resumePhysicalApproach(GameState& state, const ContentCatalog& catalog)
{
    if (state.run.flight.physicalFlight) {
        state.screen = Screen::Flight;
        state.run.flight.active = true;
        return true;
    }
    const std::string destinationId = state.run.approach.active
        ? state.run.approach.destinationId : currentDestination(state, catalog).id;
    const Destination* destination = catalog.findDestination(destinationId);
    if (destination == nullptr) return false;
    const double fuel = state.run.approach.active
        ? state.run.approach.transferFuelRemaining : state.run.flight.fuelRemaining;
    const double heat = state.run.flight.heat;
    Random rng(state.seed);
    PreparedLaunch model = prepareLaunch(state, catalog, rng);
    model.config.destinationId = destinationId;
    state.run.flight = beginLaunchFlight(model, *destination);
    auto& flight = state.run.flight;
    flight.physicalFlight = true;
    flight.active = true;
    flight.fuelRemaining = std::max(0.0, fuel);
    flight.heat = heat;
    flight.positionX = -1.0;
    flight.positionY = 0.0;
    flight.velocityX = 0.0;
    flight.velocityY = std::sqrt(flightGravityAcceleration(1.0, 0.0));
    flight.heading = 1.5707963267948966;
    flight.selectedThrottle = 0.0;
    flight.mode = FlightMode::Orbit;
    flight.phase = FlightPhase::TargetApproach;
    state.screen = Screen::Flight;
    state.statusLine = "Resume flight. Establish orbit to scan, drill, and land.";
    return true;
}

void ensureDroneBayState(GameState& state, const ContentCatalog& catalog);

namespace {

const Destination* currentResearchDestination(const GameState& state, const ContentCatalog& catalog)
{
    if (state.run.planetaryExpedition.active) {
        return catalog.findDestination(state.run.planetaryExpedition.destinationId);
    }
    if (state.run.approach.active) {
        return catalog.findDestination(state.run.approach.destinationId);
    }
    if (state.run.destinationIndex >= 0 && state.run.destinationIndex < static_cast<int>(catalog.destinations.size())) {
        return &catalog.destinations[static_cast<std::size_t>(state.run.destinationIndex)];
    }
    return nullptr;
}

bool projectUnlockedForDestination(const ResearchProject& project, const MetaProgress& meta, const Destination& destination)
{
    return project.requiredDestinationTier <= destination.tier && hasUnlock(meta, project.unlockKey);
}

bool hasTag(const std::vector<std::string>& tags, const std::string& tag)
{
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

bool containsId(const std::vector<std::string>& ids, std::string_view id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// These bindings are deliberately confined to the legacy public API used by
// existing UI and input bindings. Scenario definitions remain the authority;
// the old CampaignObjectiveId names are only a compatibility facade while the
// UI migrates to typed scenario presentation records.
struct LegacyCampaignScenarioBinding {
    CampaignObjectiveId objective = CampaignObjectiveId::LunarProspector;
    std::string_view scenarioId;
    std::string_view briefingStepId;
    std::string_view progressStepId;
};

constexpr std::array<LegacyCampaignScenarioBinding, 4> legacyCampaignScenarioBindings {{
    {CampaignObjectiveId::LunarProspector, content::scenario::lunarProspector, "briefing", "delivery"},
    {CampaignObjectiveId::MarsBayExpansion, content::scenario::marsBayExpansion, "briefing", "delivery"},
    {CampaignObjectiveId::IoVolcanicDescent, content::scenario::volcanicDescent, "commission", "recovery"},
    {CampaignObjectiveId::SaturnSlingshot, content::scenario::saturnDeparture, "briefing", "artifact"},
}};

const LegacyCampaignScenarioBinding* legacyCampaignScenarioBinding(CampaignObjectiveId objective)
{
    const auto found = std::find_if(
        legacyCampaignScenarioBindings.begin(),
        legacyCampaignScenarioBindings.end(),
        [&](const LegacyCampaignScenarioBinding& binding) { return binding.objective == objective; });
    return found == legacyCampaignScenarioBindings.end() ? nullptr : &*found;
}

const Destination* findScenarioRouteRewardDestination(
    const ContentCatalog& catalog,
    const ScenarioStepDefinition& step)
{
    for (const Destination& destination : catalog.destinations) {
        for (const ScenarioReward& reward : step.rewards) {
            const bool grantsDestination = reward.kind == ScenarioRewardKind::RouteAccess &&
                reward.id == destination.id;
            const bool grantsRequiredKey = reward.kind == ScenarioRewardKind::UnlockKey &&
                std::find(
                    destination.routeRequirementKeys.begin(),
                    destination.routeRequirementKeys.end(),
                    reward.id) != destination.routeRequirementKeys.end();
            if (grantsDestination || grantsRequiredKey) {
                return &destination;
            }
        }
    }
    return nullptr;
}

const ContentCatalog& legacyCampaignCatalog()
{
    static const ContentCatalog catalog = createDefaultContent();
    return catalog;
}

CampaignObjectiveState legacyCampaignObjectiveState(ScenarioStepState state)
{
    switch (state) {
    case ScenarioStepState::Active:
        return CampaignObjectiveState::Active;
    case ScenarioStepState::ReadyToClaim:
        return CampaignObjectiveState::ReadyToClaim;
    case ScenarioStepState::Complete:
        return CampaignObjectiveState::Complete;
    case ScenarioStepState::Locked:
        return CampaignObjectiveState::Locked;
    }
    return CampaignObjectiveState::Locked;
}

const ScenarioStepProgress* scenarioProgress(
    const GameState& state,
    std::string_view scenarioId,
    std::string_view stepId)
{
    const ScenarioInstance* instance = findScenarioInstance(state.meta, scenarioId);
    return instance == nullptr ? nullptr : findScenarioStepProgress(*instance, stepId);
}

// Compatibility cache only. ScenarioInstance remains the authoritative state;
// this writes retired named fields so old saves and callers can be migrated
// without allowing those fields to drive any route, reward, or activity.
void writeLegacyCampaignSaveProjection(GameState& state, const ContentCatalog& catalog)
{
    const auto progressFor = [&](CampaignObjectiveId objective) -> const ScenarioStepProgress* {
        const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(objective);
        return binding == nullptr ? nullptr : scenarioProgress(state, binding->scenarioId, binding->progressStepId);
    };
    const auto briefingFor = [&](CampaignObjectiveId objective) -> const ScenarioStepProgress* {
        const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(objective);
        return binding == nullptr ? nullptr : scenarioProgress(state, binding->scenarioId, binding->briefingStepId);
    };

    const ScenarioStepProgress* lunarDelivery = progressFor(CampaignObjectiveId::LunarProspector);
    const ScenarioStepProgress* lunarBriefing = briefingFor(CampaignObjectiveId::LunarProspector);
    state.meta.prospectorCommonOreRecovered = lunarDelivery == nullptr ? 0 : std::max(0, lunarDelivery->progress);
    state.meta.lunarMiningBriefingAcknowledged = lunarBriefing != nullptr && lunarBriefing->briefingAcknowledged;
    state.meta.lunarProspectorClaimed = lunarDelivery != nullptr && lunarDelivery->claimed;

    const ScenarioStepProgress* marsDelivery = progressFor(CampaignObjectiveId::MarsBayExpansion);
    const ScenarioStepProgress* marsBriefing = briefingFor(CampaignObjectiveId::MarsBayExpansion);
    state.meta.marsCommonOreRecovered = marsDelivery == nullptr ? 0 : std::max(0, marsDelivery->progress);
    state.meta.marsMiningBriefingAcknowledged = marsBriefing != nullptr && marsBriefing->briefingAcknowledged;
    state.meta.marsBayExpansionClaimed = marsDelivery != nullptr && marsDelivery->claimed;

    const ScenarioStepProgress* volcanicCommission = briefingFor(CampaignObjectiveId::IoVolcanicDescent);
    const ScenarioStepProgress* volcanicRecovery = progressFor(CampaignObjectiveId::IoVolcanicDescent);
    state.meta.ioVolcanicBriefingAcknowledged = volcanicCommission != nullptr && volcanicCommission->briefingAcknowledged;
    state.meta.ioHazardDroneCommissioned = volcanicCommission != nullptr && volcanicCommission->claimed;
    state.meta.ioArtifactRecovered = volcanicRecovery != nullptr && volcanicRecovery->claimed;

    const ScenarioStepProgress* transferBriefing = briefingFor(CampaignObjectiveId::SaturnSlingshot);
    const ScenarioStepProgress* transferFlyby = progressFor(CampaignObjectiveId::SaturnSlingshot);
    state.meta.saturnSlingshotBriefingAcknowledged = transferBriefing != nullptr && transferBriefing->briefingAcknowledged;
    state.meta.saturnSlingshotPerfect = transferFlyby != nullptr && transferFlyby->completed;
    state.meta.saturnRouteUnlocked = transferFlyby != nullptr && transferFlyby->claimed;
    state.meta.saturnSlingshotFailed = transferFlyby != nullptr && transferFlyby->failureSeen;
    state.meta.saturnSlingshotFailureAcknowledged = transferFlyby != nullptr && transferFlyby->failureAcknowledged;

    // The projection may be called after a migrated save. Let generic reward
    // state repair bay bookkeeping without re-awarding anything.
    ensureDroneBayState(state, catalog);
}

bool scenarioStepMatchesEvent(
    const ScenarioStepDefinition& step,
    const ScenarioEvent& event)
{
    return step.completionEvent == event.kind &&
        (step.eventOriginId.empty() || step.eventOriginId == event.originId) &&
        (step.eventTargetId.empty() || step.eventTargetId == event.targetId);
}

ArtifactRecord* firstUnidentifiedArtifact(GameState& state)
{
    auto artifact = std::find_if(state.meta.artifacts.begin(), state.meta.artifacts.end(), [](const ArtifactRecord& record) {
        return !record.identified;
    });
    if (artifact == state.meta.artifacts.end()) {
        return nullptr;
    }
    return &(*artifact);
}

void applyRecoveredArtifactRewards(
    GameState& state,
    const ContentCatalog& catalog,
    std::vector<ArtifactRecord>& artifacts,
    std::string_view miningSiteDefinitionId)
{
    for (ArtifactRecord& artifact : artifacts) {
        if (artifact.rewardApplied) {
            continue;
        }
        // A protected objective's reward belongs to its owning scenario, not
        // to the artifact adapter. This must run before the ordinary artifact
        // reward switch because a configured cocoon intentionally uses
        // ArtifactRewardType::None to avoid a duplicate local payout.
        if (creditRecoveredProtectedObjective(
                state,
                catalog,
                artifact,
                miningSiteDefinitionId)) {
            continue;
        }
        const double condition = std::clamp(artifact.condition, 0.0, 1.0);
        if (artifact.kind == ArtifactKind::Story) {
            state.meta.ark.repairProgress += tuning::mining::artifactStoryArkRepair;
            if (state.meta.ark.condition == ArkCondition::DamagedStranded) {
                state.meta.ark.hullDamage = std::max(0, state.meta.ark.hullDamage - static_cast<int>(std::ceil(tuning::mining::artifactStoryHullRepair * condition)));
            }
            artifact.rewardApplied = true;
            continue;
        }

        switch (artifact.rewardType) {
        case ArtifactRewardType::Credits:
            state.run.credits += std::ceil(tuning::mining::artifactCreditReward * std::max(0.25, condition));
            artifact.rewardApplied = true;
            break;
        case ArtifactRewardType::ArkFuel:
            state.meta.ark.fuelReserve += std::max(1, static_cast<int>(std::ceil(static_cast<double>(tuning::mining::artifactFuelReward) * condition)));
            artifact.rewardApplied = true;
            break;
        case ArtifactRewardType::BlueprintInsight:
            state.meta.blueprintProgress += std::max(1, static_cast<int>(std::ceil(static_cast<double>(tuning::mining::artifactBlueprintReward) * condition)));
            artifact.rewardApplied = true;
            break;
        case ArtifactRewardType::None:
            artifact.rewardApplied = true;
            break;
        }
    }
}

SurfaceSiteProfile generatedSurfaceSiteProfile(const GameState& state, const Destination& destination, Random* rng)
{
    if (rng != nullptr) {
        return static_cast<SurfaceSiteProfile>(rng->rangeInt(0, 2));
    }
    return static_cast<SurfaceSiteProfile>((state.seed + static_cast<std::uint64_t>(destination.tier)) % 3);
}

void ensureDestinationHistory(std::vector<int>& values, const ContentCatalog& catalog)
{
    if (values.size() < catalog.destinations.size()) {
        values.resize(catalog.destinations.size(), 0);
    }
}

int destinationIndexForId(const ContentCatalog& catalog, std::string_view destinationId)
{
    for (std::size_t i = 0; i < catalog.destinations.size(); ++i) {
        if (catalog.destinations[i].id == destinationId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void addDestinationHistoryValue(std::vector<int>& values, const ContentCatalog& catalog, std::string_view destinationId)
{
    const int index = destinationIndexForId(catalog, destinationId);
    if (index < 0) {
        return;
    }
    ensureDestinationHistory(values, catalog);
    values[static_cast<std::size_t>(index)] += 1;
}

double landingReconHazardPenalty(const GameState& state)
{
    // Direct descent is made physically harder by its corridor configuration.
    // It must never add an invisible percentage penalty to surface play.
    (void)state;
    return 0.0;
}

MiningArenaRules activeSurfaceArenaRules(const GameState& state)
{
    const ContentCatalog catalog = createDefaultContent();
    const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    const int completedHostileSorties = destinationHistoryValue(
        state.meta.destinationSuccesses,
        catalog,
        expedition.destinationId);
    const int landingOrdinal = destinationHistoryValue(
        state.meta.destinationLandings,
        catalog,
        expedition.destinationId);
    return resolveMiningArenaRules(campaignMiningArenaRequest(
        state.meta.chapter,
        expedition.destinationId,
        expedition.depth,
        completedHostileSorties,
        state.seed,
        landingOrdinal));
}

void appendSurfaceLog(PlanetaryExpeditionState& expedition, std::string entry)
{
    if (entry.empty()) {
        return;
    }
    expedition.logEntries.push_back(std::move(entry));
    const int overflow = static_cast<int>(expedition.logEntries.size()) - tuning::research::surfaceLogEntryLimit;
    if (overflow > 0) {
        expedition.logEntries.erase(expedition.logEntries.begin(), expedition.logEntries.begin() + overflow);
    }
}

std::string signedWhole(int value)
{
    return value > 0 ? "+" + std::to_string(value) : std::to_string(value);
}

std::string signedPercent(double value)
{
    return (value > 0.0 ? "+" : "") + display::percent(value);
}

void addDelta(std::vector<std::string>& parts, int value, std::string_view label)
{
    if (value != 0) {
        parts.push_back(signedWhole(value) + " " + std::string(label));
    }
}

void addLoss(std::vector<std::string>& parts, int value, std::string_view label)
{
    if (value > 0) {
        parts.push_back("Lost " + std::to_string(value) + " " + std::string(label));
    }
}

std::string joinParts(const std::vector<std::string>& parts)
{
    std::string summary;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            summary += "; ";
        }
        summary += parts[i];
    }
    return summary;
}

std::string surfaceDeltaSummary(const SurfaceActionOutcome& outcome)
{
    std::vector<std::string> parts;
    addDelta(parts, outcome.supplyDelta, text::labels::supply);
    addDelta(parts, outcome.fuelDelta, text::labels::rigFuel);
    addDelta(parts, outcome.materialDelta.common, text::labels::commonMaterials);
    addDelta(parts, outcome.materialDelta.rare, text::labels::rareMaterials);
    addDelta(parts, outcome.materialDelta.exotic, text::labels::exoticMaterials);
    addLoss(parts, outcome.materialLost.common, text::labels::commonMaterials);
    addLoss(parts, outcome.materialLost.rare, text::labels::rareMaterials);
    addLoss(parts, outcome.materialLost.exotic, text::labels::exoticMaterials);
    addDelta(parts, outcome.cargoDelta, text::labels::cargo);
    addDelta(parts, outcome.blueprintDelta, text::labels::blueprints);
    if (outcome.artifactFound) {
        parts.push_back("+1 " + std::string(text::labels::artifacts));
    }
    addLoss(parts, outcome.artifactsLost, text::labels::artifacts);
    if (std::abs(outcome.hazardDelta) > 0.0001) {
        parts.push_back(signedPercent(outcome.hazardDelta) + " " + std::string(text::labels::hazard));
    }

    return joinParts(parts);
}


double orbitBaseReward(const Destination& destination)
{
    return std::max(tuning::physicalFlight::goodRewardFloor, destination.baseReward * tuning::physicalFlight::goodRewardFactor);
}

} // namespace

const Destination* scenarioRouteRewardDestination(
    const ContentCatalog& catalog,
    const ScenarioStepDefinition& step)
{
    return findScenarioRouteRewardDestination(catalog, step);
}

CampaignObjectiveStatus campaignObjectiveStatus(const GameState& state, CampaignObjectiveId objective)
{
    CampaignObjectiveStatus status;
    status.id = objective;
    const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(objective);
    if (binding == nullptr) {
        return status;
    }
    const ContentCatalog& catalog = legacyCampaignCatalog();
    const ScenarioDefinition* definition = findScenarioDefinition(catalog, binding->scenarioId);
    const ScenarioStepDefinition* step = definition == nullptr
        ? nullptr
        : findScenarioStepDefinition(*definition, binding->progressStepId);
    const ScenarioStepProgress* progress = scenarioProgress(state, binding->scenarioId, binding->progressStepId);
    status.current = progress == nullptr ? 0 : std::max(0, progress->progress);
    status.required = step == nullptr ? 0 : std::max(0, step->requiredProgress);
    status.briefingAcknowledged = scenarioStepBriefingAcknowledged(
        state,
        binding->scenarioId,
        binding->briefingStepId);

    ScenarioStepState stateForPresentation = scenarioStepState(
        state,
        catalog,
        binding->scenarioId,
        binding->progressStepId);
    if (stateForPresentation == ScenarioStepState::Locked &&
        binding->briefingStepId != binding->progressStepId) {
        stateForPresentation = scenarioStepState(
            state,
            catalog,
            binding->scenarioId,
            binding->briefingStepId);
    }
    status.state = legacyCampaignObjectiveState(stateForPresentation);
    return status;
}

bool acknowledgeCampaignObjectiveBriefing(GameState& state, CampaignObjectiveId objective)
{
    const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(objective);
    if (binding == nullptr) {
        return false;
    }
    const ContentCatalog& catalog = legacyCampaignCatalog();
    const ScenarioActionOutcome outcome = performScenarioAction(
        state,
        catalog,
        binding->scenarioId,
        binding->briefingStepId,
        ScenarioActionKind::AcknowledgeBriefing);
    if (!outcome.applied) {
        return false;
    }
    writeLegacyCampaignSaveProjection(state, catalog);
    state.statusLine = outcome.message;
    return true;
}

int creditCampaignCommonOre(GameState& state, std::string_view destinationId, int deliveredCommonOre)
{
    return creditCampaignCommonOre(
        state,
        legacyCampaignCatalog(),
        destinationId,
        deliveredCommonOre);
}

int creditCampaignCommonOre(
    GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId,
    int deliveredCommonOre)
{
    const int recovered = std::max(0, deliveredCommonOre);
    if (recovered <= 0) {
        return 0;
    }
    ensureScenarioInstances(state, catalog);
    const ScenarioEvent event {
        ScenarioEventKind::SafeMaterialDelivered,
        {},
        {},
        std::string(destinationId),
        "common",
        recovered,
        0
    };
    const auto matchingProgress = [&]() {
        int total = 0;
        for (const ScenarioInstance& instance : state.meta.scenarios) {
            const std::string_view definitionId = instance.definitionId.empty()
                ? std::string_view(instance.id)
                : std::string_view(instance.definitionId);
            const ScenarioDefinition* definition = findScenarioDefinition(catalog, definitionId);
            if (definition == nullptr) {
                continue;
            }
            const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, instance);
            for (const ScenarioStepDefinition& step : resolved.steps) {
                if (!scenarioStepMatchesEvent(step, event)) {
                    continue;
                }
                const ScenarioStepProgress* progress = findScenarioStepProgress(instance, step.id);
                total += progress == nullptr ? 0 : std::max(0, progress->progress);
            }
        }
        return total;
    };
    const int before = matchingProgress();
    (void)recordScenarioEvent(state, catalog, event);
    const int allocated = std::max(0, matchingProgress() - before);
    // Contract samples are reserved when they reach home, keeping them out of
    // the general material pool until the player explicitly claims the reward.
    state.meta.materials.common = std::max(0, state.meta.materials.common - allocated);
    writeLegacyCampaignSaveProjection(state, catalog);
    return allocated;
}

bool canClaimLunarProspector(const GameState& state)
{
    const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(CampaignObjectiveId::LunarProspector);
    return binding != nullptr && scenarioStepState(
        state,
        legacyCampaignCatalog(),
        binding->scenarioId,
        binding->progressStepId) == ScenarioStepState::ReadyToClaim;
}

bool claimLunarProspector(GameState& state, const ContentCatalog& catalog)
{
    const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(CampaignObjectiveId::LunarProspector);
    if (binding == nullptr) {
        return false;
    }
    const ScenarioActionOutcome outcome = performScenarioAction(
        state,
        catalog,
        binding->scenarioId,
        binding->progressStepId,
        ScenarioActionKind::ClaimReward);
    if (!outcome.applied) {
        state.statusLine = "Complete the active route objective before claiming its reward.";
        return false;
    }
    writeLegacyCampaignSaveProjection(state, catalog);
    state.statusLine = outcome.message;
    return true;
}

bool canClaimMarsBayExpansion(const GameState& state)
{
    const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(CampaignObjectiveId::MarsBayExpansion);
    return binding != nullptr && scenarioStepState(
        state,
        legacyCampaignCatalog(),
        binding->scenarioId,
        binding->progressStepId) == ScenarioStepState::ReadyToClaim;
}

bool claimMarsBayExpansion(GameState& state, const ContentCatalog& catalog)
{
    const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(CampaignObjectiveId::MarsBayExpansion);
    if (binding == nullptr) {
        return false;
    }
    const ScenarioActionOutcome outcome = performScenarioAction(
        state,
        catalog,
        binding->scenarioId,
        binding->progressStepId,
        ScenarioActionKind::ClaimReward);
    if (!outcome.applied) {
        state.statusLine = "Complete the active route objective before claiming its reward.";
        return false;
    }
    writeLegacyCampaignSaveProjection(state, catalog);
    state.statusLine = outcome.message;
    return true;
}

bool canCommissionIoHazardDrone(const GameState& state, const ContentCatalog& catalog)
{
    const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(CampaignObjectiveId::IoVolcanicDescent);
    const ScenarioDefinition* definition = binding == nullptr
        ? nullptr
        : scenarioDefinitionForRuntimeId(state, catalog, binding->scenarioId);
    const ScenarioInstance* instance = binding == nullptr
        ? nullptr
        : findScenarioInstance(state.meta, binding->scenarioId);
    if (binding == nullptr || definition == nullptr || instance == nullptr ||
        state.run.planetaryExpedition.active || state.run.mining.active) {
        return false;
    }
    const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, *instance);
    const ScenarioStepDefinition* step = findScenarioStepDefinition(resolved, binding->briefingStepId);
    if (step == nullptr) {
        return false;
    }
    const Destination& destination = currentDestination(state, catalog);
    const bool directManualBriefingAction =
        step->mandatoryBriefing && step->completionEvent == ScenarioEventKind::ManualAction &&
        step->action == ScenarioActionKind::BeginActivity;
    return (resolved.destinationId.empty() || resolved.destinationId == destination.id) &&
        scenarioStepState(state, catalog, binding->scenarioId, binding->briefingStepId) == ScenarioStepState::Active &&
        (scenarioStepBriefingAcknowledged(state, binding->scenarioId, binding->briefingStepId) ||
         directManualBriefingAction);
}

bool commissionIoHazardDrone(GameState& state, const ContentCatalog& catalog)
{
    const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(CampaignObjectiveId::IoVolcanicDescent);
    if (binding == nullptr || !canCommissionIoHazardDrone(state, catalog)) {
        state.statusLine = "The required support drone cannot be commissioned here.";
        return false;
    }
    const ScenarioActionOutcome outcome = performScenarioAction(
        state,
        catalog,
        binding->scenarioId,
        binding->briefingStepId,
        ScenarioActionKind::BeginActivity);
    if (!outcome.applied) {
        return false;
    }
    writeLegacyCampaignSaveProjection(state, catalog);
    state.statusLine = outcome.message;
    return true;
}

bool creditRecoveredIoArtifact(GameState& state, ArtifactRecord& artifact)
{
    return creditRecoveredProtectedObjective(state, legacyCampaignCatalog(), artifact);
}

bool creditRecoveredIoArtifact(GameState& state, const ContentCatalog& catalog, ArtifactRecord& artifact)
{
    return creditRecoveredProtectedObjective(state, catalog, artifact);
}

bool creditRecoveredProtectedObjective(
    GameState& state,
    const ContentCatalog& catalog,
    ArtifactRecord& artifact,
    std::string_view miningSiteDefinitionId)
{
    if (artifact.rewardApplied) {
        return false;
    }
    ensureScenarioInstances(state, catalog);

    // A protected objective is identified by its mining-site configuration,
    // not by a destination or a narrative artifact name.
    bool matchedScenarioObjective = false;
    bool scenarioEventRecorded = false;
    for (const ScenarioInstance& instance : state.meta.scenarios) {
        const std::string_view definitionId = instance.definitionId.empty()
            ? std::string_view(instance.id)
            : std::string_view(instance.definitionId);
        const ScenarioDefinition* definition = findScenarioDefinition(catalog, definitionId);
        if (definition == nullptr) {
            continue;
        }
        const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, instance);
        for (const ScenarioStepDefinition& step : resolved.steps) {
            if (step.completionEvent != ScenarioEventKind::ProtectedObjectiveExtracted ||
                step.miningSiteDefinitionId.empty()) {
                continue;
            }
            if (!miningSiteDefinitionId.empty() && step.miningSiteDefinitionId != miningSiteDefinitionId) {
                continue;
            }
            const MiningSiteDefinition* site = findMiningSiteDefinition(catalog, step.miningSiteDefinitionId);
            if (site == nullptr || site->cocoon.protectedObjective.id != artifact.id) {
                continue;
            }
            matchedScenarioObjective = true;
            ScenarioEvent event;
            event.kind = ScenarioEventKind::ProtectedObjectiveExtracted;
            event.scenarioId = instance.id;
            event.stepId = step.id;
            event.originId = artifact.originDestinationId;
            event.targetId = step.eventTargetId;
            event.amount = 1;
            scenarioEventRecorded = recordScenarioEvent(state, catalog, event) || scenarioEventRecorded;
        }
    }

    // Scenario rewards are granted by the scenario dispatcher and must not
    // be duplicated by the protected-objective adapter. Mark only a recorded
    // event as consumed so an invalid/out-of-order extraction cannot silently
    // discard its payload.
    if (scenarioEventRecorded) {
        artifact.rewardApplied = true;
        writeLegacyCampaignSaveProjection(state, catalog);
        return true;
    }
    if (matchedScenarioObjective) {
        return false;
    }

    return false;
}

bool destinationSupportsResearch(const Destination& destination)
{
    return destination.tier >= tuning::research::firstResearchTier;
}

bool destinationSupportsSurface(const Destination& destination)
{
    return destination.tier >= 1;
}

bool destinationAllowsEnemyEncounters(const Destination& destination)
{
    return destination.tier >= tuning::research::enemyEncounterTier;
}

double orbitCreditReward(const Destination& destination, OrbitGrade grade)
{
    if (grade != OrbitGrade::Good && grade != OrbitGrade::Perfect) {
        return 0.0;
    }
    return orbitBaseReward(destination) * (grade == OrbitGrade::Perfect ? tuning::physicalFlight::perfectRewardMultiplier : 1.0);
}

int orbitResearchDataReward(const Destination& destination, OrbitGrade grade)
{
    if (grade != OrbitGrade::Good && grade != OrbitGrade::Perfect) {
        return 0;
    }
    const int base = grade == OrbitGrade::Perfect
        ? tuning::physicalFlight::perfectBlueprintGain
        : tuning::physicalFlight::goodBlueprintGain;
    return base + (destinationSupportsResearch(destination) ? 1 : 0);
}

bool queueBlockedArrivalFlybyRecovery(GameState& state, const ContentCatalog& catalog)
{
    const ApproachRunState& arrival = state.run.approach;
    if (!arrival.active || arrival.destinationId.empty() ||
        !arrival.incomingRoute.active() ||
        arrival.incomingRoute.targetDestinationId != arrival.destinationId) {
        return false;
    }
    RouteTransitState recovery = makeRouteTransit(
        catalog,
        arrival.destinationId,
        arrival.incomingRoute.originDestinationId,
        RouteTransitIntent::Recovery);
    if (!recovery.active()) {
        return false;
    }
    state.run.routeTransit = std::move(recovery);
    state.run.approach = {};
    state.screen = Screen::Hangar;
    return true;
}

int destinationHistoryValue(const std::vector<int>& values, const ContentCatalog& catalog, std::string_view destinationId)
{
    const int index = destinationIndexForId(catalog, destinationId);
    if (index < 0 || index >= static_cast<int>(values.size())) {
        return 0;
    }
    return values[static_cast<std::size_t>(index)];
}

bool shouldOpenArrivalOps(const LaunchOutcome& outcome, const ContentCatalog& catalog)
{
    if (outcome.type != LaunchResultType::MissionComplete) {
        return false;
    }

    if (routeTransitIsRecovery(outcome.routeTransit)) {
        return false;
    }
    const Destination* destination = catalog.findDestination(outcome.destinationId);
    return destination != nullptr
        && destination->tier >= 1
        && (outcome.frontierTransfer || outcome.recoveryMethod == RecoveryMethod::TransferArrival);
}

bool shouldOpenPostArrivalPhases(const LaunchOutcome& outcome, const ContentCatalog& catalog)
{
    if (outcome.type != LaunchResultType::MissionComplete) {
        return false;
    }

    const Destination* destination = catalog.findDestination(outcome.destinationId);
    return destination != nullptr
        && destinationSupportsResearch(*destination)
        && (outcome.frontierTransfer || outcome.recoveryMethod == RecoveryMethod::TransferArrival);
}


namespace {

bool bankAuthoredRouteFlightData(
    GameState& state,
    const ContentCatalog& catalog,
    std::string_view originDestinationId)
{
    const Destination* target = nextDestination(state, catalog);
    if (target == nullptr || !scenarioRouteUsesFlightData(state, catalog, *target) ||
        currentDestination(state, catalog).id != originDestinationId) {
        return false;
    }
    const int before = state.run.frontierReadiness;
    if (!bankFrontierReadiness(state, catalog)) {
        return false;
    }
    recordScenarioEvent(
        state,
        catalog,
        {ScenarioEventKind::FlightDataBanked,
         {},
         {},
         std::string(originDestinationId),
         target->id,
         state.run.frontierReadiness - before,
         0});
    return true;
}

} // namespace

void clearResearchAndExpeditionState(GameState& state)
{
    state.run.researchProjectIds = {};
    state.run.approach = {};
    state.run.planetaryExpedition = {};
}


void startArrivalOps(GameState& state, const LaunchOutcome& outcome)
{
    ApproachRunState approach;
    approach.active = true;
    approach.destinationId = outcome.destinationId;
    approach.transferFuelRemaining = std::max(0.0, outcome.transferFuelRemaining);
    approach.transferFuelCapacity = std::max(0.0, outcome.transferFuelCapacity);
    approach.incomingRoute = outcome.routeTransit;
    state.run.approach = std::move(approach);
}


bool jupiterWindowReviewed(const GameState& state, const ContentCatalog&)
{
    return scenarioHasCompletedStep(state, content::scenario::marsBayExpansion, "funding") ||
        scenarioStepBriefingAcknowledged(
            state,
            content::scenario::marsBayExpansion,
            "funding");
}

bool commitClaimedScenarioRoute(
    GameState& state,
    const ContentCatalog& catalog,
    std::string_view scenarioId,
    std::string_view stepId)
{
    const ScenarioDefinition* definition = scenarioDefinitionForRuntimeId(
        state,
        catalog,
        scenarioId);
    const ScenarioInstance* instance = findScenarioInstance(state.meta, scenarioId);
    if (definition == nullptr || instance == nullptr) {
        return false;
    }

    const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, *instance);
    const ScenarioStepDefinition* step = findScenarioStepDefinition(resolved, stepId);
    const ScenarioStepProgress* progress = findScenarioStepProgress(*instance, stepId);
    const Destination* route = step == nullptr
        ? nullptr
        : scenarioRouteRewardDestination(catalog, *step);
    if (progress == nullptr || !progress->claimed || route == nullptr ||
        !frontierGateStatusForDestination(state, catalog, route->id).satisfied) {
        return false;
    }

    const Destination* origin = resolved.destinationId.empty()
        ? &currentDestination(state, catalog)
        : catalog.findDestination(resolved.destinationId);
    if (origin == nullptr) {
        return false;
    }
    // A route claim is a physical departure authorization, not a save repair.
    // The ship must still be at the authored origin; if it is not, leave the
    // already-claimed reward intact and let the progression audit offer the
    // explicit checkpoint path rather than relocating the campaign.
    if (currentDestination(state, catalog).id != origin->id) {
        return false;
    }
    RouteTransitState transit = makeRouteTransit(
        catalog,
        origin->id,
        route->id,
        RouteTransitIntent::Outbound);
    if (!transit.active()) {
        return false;
    }

    state.run.frontierReadiness = 0;
    state.run.routeTransit = std::move(transit);
    state.run.approach = {};
    state.launchConfig.frontierTransfer = true;
    state.launchConfig.destinationId = route->id;
    state.launchConfig.burnGoalMultiplier = route->targetMultiplier;
    state.screen = Screen::Hangar;
    syncLaunchConfig(state, catalog);
    return true;
}

void generateResearchProjects(GameState& state, const ContentCatalog& catalog, Random& rng)
{
    state.run.researchProjectIds = {};
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr || !destinationSupportsResearch(*destination)) {
        return;
    }

    std::vector<const ResearchProject*> available;
    for (const ResearchProject& project : catalog.researchProjects) {
        if (projectUnlockedForDestination(project, state.meta, *destination)) {
            available.push_back(&project);
        }
    }

    for (std::size_t slot = 0; slot < state.run.researchProjectIds.size() && !available.empty(); ++slot) {
        const int picked = rng.rangeInt(0, static_cast<int>(available.size()) - 1);
        state.run.researchProjectIds[slot] = available[static_cast<std::size_t>(picked)]->id;
        available.erase(available.begin() + picked);
    }
}

void addMaterials(MaterialInventory& owned, const MaterialInventory& delta)
{
    owned.common = std::max(0, owned.common + delta.common);
    owned.rare = std::max(0, owned.rare + delta.rare);
    owned.exotic = std::max(0, owned.exotic + delta.exotic);
}

int identifiedArtifactCount(const MetaProgress& meta)
{
    return static_cast<int>(std::count_if(meta.artifacts.begin(), meta.artifacts.end(), [](const ArtifactRecord& artifact) {
        return artifact.identified;
    }));
}

int researchFacilityBlueprintBonus(const MetaProgress& meta)
{
    return hasUnlock(meta, content::unlock::analysisLab) ? tuning::research::analysisLabBlueprintBonus : 0;
}

int artifactInsightBlueprintBonus(const MetaProgress& meta)
{
    return std::min(
        tuning::research::artifactInsightBlueprintMaximum,
        identifiedArtifactCount(meta) * tuning::research::artifactInsightBlueprintPerIdentified);
}

int researchBlueprintGain(const MetaProgress& meta, const ResearchProject& project)
{
    return project.blueprintGain + researchFacilityBlueprintBonus(meta) + artifactInsightBlueprintBonus(meta);
}

SurfaceToolEffects surfaceToolEffects(const MetaProgress& meta)
{
    SurfaceToolEffects effects;
    if (hasUnlock(meta, content::unlock::surfaceProbes)) {
        effects.supplyBonus += tuning::research::probeSupplyBonus;
        effects.surveyCommonBonus += tuning::research::probeSurveyCommonBonus;
    }
    if (hasUnlock(meta, content::unlock::surfaceDrills)) {
        effects.mineCommonBonus += tuning::research::drillMineCommonBonus;
        effects.mineRareChanceBonus += tuning::research::drillRareChanceBonus;
    }
    if (hasUnlock(meta, content::unlock::cargoRigs)) {
        effects.hazardRelief += tuning::research::cargoRigHazardRelief;
    }
    if (hasUnlock(meta, content::unlock::perimeterDrones)) {
        effects.enemyEncounterRelief += tuning::research::perimeterDroneEnemyRelief;
    }
    return effects;
}

SurfaceCrewEffects surfaceCrewEffects(const GameState& state)
{
    SurfaceCrewEffects effects;
    const Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        effects.summary = "No field specialist assigned.";
        return effects;
    }

    if (astronaut->trait == tuning::traits::beastMode) {
        effects.summary = "Capybara endurance: expanded rig and suit oxygen.";
    } else if (astronaut->trait == tuning::traits::hardReboot) {
        effects.hazardRelief = 0.04;
        effects.summary = "Beaver engineering: stronger rig integrity and field repairs.";
    } else if (astronaut->trait == tuning::traits::outtaHere) {
        effects.hazardRelief = 0.055;
        effects.summary = "Fox navigation: cleaner recovery routes and emergency control.";
    } else if (astronaut->trait == tuning::traits::deepFocus) {
        effects.surveyCommonBonus = 1;
        effects.artifactChanceBonus = 0.03;
        effects.summary = "Prairie Dog scouting: longer scans and faster excavation.";
    } else if (astronaut->trait == tuning::traits::rummageSale) {
        effects.mineRareChanceBonus = 0.12;
        effects.summary = "Squirrel prospecting: better odds of useful resource discoveries.";
    } else if (astronaut->trait == tuning::traits::phaseShift) {
        effects.summary = "Chipmunk EVA: faster suit traversal through tight shafts.";
    } else if (astronaut->trait == tuning::traits::fieldInstincts) {
        effects.hazardRelief = 0.02;
        effects.summary = "Field instincts: fewer surface hazards.";
    } else {
        effects.summary = astronaut->background.empty() ? astronaut->trait : astronaut->background;
    }

    return effects;
}

SurfaceSiteProfileEffects surfaceSiteProfileEffects(SurfaceSiteProfile profile)
{
    SurfaceSiteProfileEffects effects;
    switch (profile) {
    case SurfaceSiteProfile::SurveyBasin:
        effects.surveyCommonBonus += tuning::research::siteSurveyBasinSurveyBonus;
        effects.hazardDelta -= tuning::research::siteSurveyBasinHazardRelief;
        break;
    case SurfaceSiteProfile::OreShelf:
        effects.mineCommonBonus += tuning::research::siteOreShelfMineBonus;
        effects.mineRareChanceBonus += tuning::research::siteOreShelfRareChanceBonus;
        effects.hazardDelta += tuning::research::siteOreShelfHazardIncrease;
        break;
    case SurfaceSiteProfile::FractureField:
        effects.hazardDelta += tuning::research::siteFractureFieldHazardIncrease;
        effects.artifactChanceBonus += tuning::research::siteFractureFieldArtifactChanceBonus;
        break;
    }
    return effects;
}

namespace {

constexpr int kMaximumRunUpgradeRank = 3;
constexpr double kBaseExpeditionExperienceThreshold = 10.0;
constexpr double kExpeditionExperienceGrowth = 1.55;

void clearRunUpgradeOffers(ExpeditionProgressionState& expedition)
{
    expedition.runUpgradeOffers = {};
    expedition.runUpgradeOfferCount = 0;
    expedition.runUpgradeOfferPending = false;
}

int rarityOrdinal(Rarity rarity)
{
    return std::clamp(static_cast<int>(rarity), 0, static_cast<int>(Rarity::Prototype));
}

Rarity rarityAtLeast(Rarity rarity, Rarity minimum)
{
    return static_cast<Rarity>(std::max(rarityOrdinal(rarity), rarityOrdinal(minimum)));
}

int runUpgradeOfferWeight(Rarity rarity)
{
    switch (rarity) {
    case Rarity::Common: return 60;
    case Rarity::Uncommon: return 30;
    case Rarity::Rare: return 10;
    case Rarity::Prototype: return 3;
    }
    return 1;
}

bool roleUsesCombat(MiniDroneRole role)
{
    return role == MiniDroneRole::Attack || role == MiniDroneRole::Defense;
}

bool synergyUsesCombat(const DroneSynergyDefinition& synergy)
{
    return std::any_of(
               synergy.requiredRoles.begin(),
               synergy.requiredRoles.end(),
               roleUsesCombat) ||
        synergy.stats.enemyDamageRelief > 0.0 ||
        synergy.stats.areaControlDamagePerSecond > 0.0 ||
        synergy.stats.enemySlow > 0.0 ||
        synergy.stats.reactiveArmorDamagePerSecond > 0.0 ||
        synergy.stats.alliedCritChanceBonus > 0.0 ||
        synergy.stats.alliedFireRateBonus > 0.0 ||
        synergy.stats.sentryVolleyBonus > 0;
}

bool runUpgradeRequiresEnemyEncounter(
    const ContentCatalog& catalog,
    const RunUpgradeOffer& offer)
{
    switch (offer.kind) {
    case RunUpgradeKind::Rig:
        if (const SurfaceUpgrade* upgrade = catalog.findSurfaceUpgrade(offer.definitionId)) {
            return hasTag(upgrade->tags, "combat") || upgrade->stats.scannerPulseDamage > 0;
        }
        break;
    case RunUpgradeKind::DroneRank:
        if (const MiniDrone* drone = catalog.findMiniDrone(offer.definitionId)) {
            return roleUsesCombat(drone->role) || hasTag(drone->tags, "combat");
        }
        break;
    case RunUpgradeKind::DroneGraft:
        if (const DroneModuleDefinition* module = catalog.findDroneModule(offer.definitionId)) {
            return roleUsesCombat(module->hostRole) || roleUsesCombat(module->secondaryRole);
        }
        break;
    case RunUpgradeKind::Synergy:
        if (const DroneSynergyDefinition* synergy = catalog.findDroneSynergy(offer.definitionId)) {
            return synergyUsesCombat(*synergy);
        }
        break;
    }
    return false;
}

bool equippedRoleAvailable(const GameState& state, const ContentCatalog& catalog, MiniDroneRole role)
{
    return std::any_of(
        state.meta.equippedDroneIds.begin(),
        state.meta.equippedDroneIds.end(),
        [&](const std::string& droneId) {
            const MiniDrone* drone = catalog.findMiniDrone(droneId);
            return drone != nullptr && isMiniDroneUnlocked(state.meta, *drone) && drone->role == role;
        });
}

bool synergyRequirementsMet(
    const GameState& state,
    const ContentCatalog& catalog,
    const DroneSynergyDefinition& synergy)
{
    if (!hasUnlock(state.meta, synergy.requiredUnlock)) {
        return false;
    }
    return std::all_of(
        synergy.requiredRoles.begin(),
        synergy.requiredRoles.end(),
        [&](MiniDroneRole role) { return equippedRoleAvailable(state, catalog, role); });
}

struct WeightedRunUpgradeCandidate {
    RunUpgradeOffer offer;
    Rarity rarity = Rarity::Common;
};

} // namespace

int runRigUpgradeRank(const GameState& state, std::string_view upgradeId)
{
    const auto found = std::find_if(
        state.run.expedition.progression.runRigUpgradeRanks.begin(),
        state.run.expedition.progression.runRigUpgradeRanks.end(),
        [&](const RunRigUpgradeRank& record) { return record.upgradeId == upgradeId; });
    return found == state.run.expedition.progression.runRigUpgradeRanks.end()
        ? 0
        : std::clamp(found->rank, 0, kMaximumRunUpgradeRank);
}

int expeditionDroneRank(const GameState& state, std::string_view droneId)
{
    const auto found = std::find_if(
        state.run.expedition.progression.runDroneRanks.begin(),
        state.run.expedition.progression.runDroneRanks.end(),
        [&](const RunDroneRank& record) { return record.droneId == droneId; });
    return found == state.run.expedition.progression.runDroneRanks.end()
        ? 1
        : std::clamp(found->rank, 1, kMaximumRunUpgradeRank);
}

double expeditionExperienceThreshold(int level)
{
    const int safeLevel = std::clamp(level, 1, 80);
    return std::ceil(kBaseExpeditionExperienceThreshold * std::pow(
        kExpeditionExperienceGrowth,
        static_cast<double>(safeLevel - 1)));
}

void resetExpeditionProgression(GameState& state)
{
    state.run.expedition.progression = {};
    state.run.expedition.progression.droneModuleAssignments.clear();
    state.run.expedition.progression.droneModuleRuntime.clear();
}

ExpeditionExperienceAward awardExpeditionExperience(
    GameState& state,
    double amount,
    Screen returnScreen)
{
    ExpeditionExperienceAward award;
    award.resultingLevel = std::max(1, state.run.expedition.progression.expeditionLevel);
    award.resultingExperience = std::max(0.0, state.run.expedition.progression.expeditionExperience);
    award.pendingChoices = std::max(0, state.run.expedition.progression.pendingRunUpgradeChoices);
    if (!state.run.active || !std::isfinite(amount) || amount <= 0.0) {
        return award;
    }

    const bool hadPendingChoice = state.run.expedition.progression.pendingRunUpgradeChoices > 0 || state.run.expedition.progression.runUpgradeOfferPending;
    const double applied = std::min(amount, 1.0e12);
    state.run.expedition.progression.expeditionLevel = std::max(1, state.run.expedition.progression.expeditionLevel);
    state.run.expedition.progression.expeditionExperience = std::max(0.0, state.run.expedition.progression.expeditionExperience) + applied;
    award.appliedExperience = applied;
    for (int guard = 0; guard < 256; ++guard) {
        const double threshold = expeditionExperienceThreshold(state.run.expedition.progression.expeditionLevel);
        if (state.run.expedition.progression.expeditionExperience + 0.000001 < threshold) {
            break;
        }
        state.run.expedition.progression.expeditionExperience = std::max(0.0, state.run.expedition.progression.expeditionExperience - threshold);
        state.run.expedition.progression.expeditionLevel += 1;
        state.run.expedition.progression.pendingRunUpgradeChoices += 1;
        award.levelsGained += 1;
    }
    if (award.levelsGained > 0 && !hadPendingChoice) {
        state.run.expedition.progression.runUpgradeReturnScreen = returnScreen;
    }
    award.resultingLevel = state.run.expedition.progression.expeditionLevel;
    award.resultingExperience = state.run.expedition.progression.expeditionExperience;
    award.pendingChoices = state.run.expedition.progression.pendingRunUpgradeChoices;
    return award;
}

int miningMaterialExperience(const MaterialInventory& materials)
{
    const long long total =
        static_cast<long long>(std::max(0, materials.common)) +
        static_cast<long long>(std::max(0, materials.rare)) * 3LL +
        static_cast<long long>(std::max(0, materials.exotic)) * 9LL;
    return static_cast<int>(std::min<long long>(total, std::numeric_limits<int>::max()));
}

Rarity runUpgradeOfferRarity(
    const GameState& state,
    const ContentCatalog& catalog,
    const RunUpgradeOffer& offer)
{
    switch (offer.kind) {
    case RunUpgradeKind::Rig:
        if (const SurfaceUpgrade* upgrade = catalog.findSurfaceUpgrade(offer.definitionId)) {
            const Rarity rankFloor = offer.targetRank >= 3
                ? Rarity::Rare
                : (offer.targetRank == 2 ? Rarity::Uncommon : Rarity::Common);
            return rarityAtLeast(upgrade->rarity, rankFloor);
        }
        break;
    case RunUpgradeKind::DroneRank:
        if (catalog.findMiniDrone(offer.definitionId) != nullptr) {
            return offer.targetRank >= 3 ? Rarity::Rare : Rarity::Uncommon;
        }
        break;
    case RunUpgradeKind::DroneGraft:
        if (const DroneModuleDefinition* module = catalog.findDroneModule(offer.definitionId)) {
            return module->rarity;
        }
        break;
    case RunUpgradeKind::Synergy:
        if (const DroneSynergyDefinition* synergy = catalog.findDroneSynergy(offer.definitionId)) {
            return synergy->rarity;
        }
        break;
    }
    (void)state;
    return Rarity::Common;
}

std::string_view runUpgradeKindLabel(RunUpgradeKind kind)
{
    switch (kind) {
    case RunUpgradeKind::Rig: return "RIG";
    case RunUpgradeKind::DroneRank:
    case RunUpgradeKind::DroneGraft:
        return "DRONE";
    case RunUpgradeKind::Synergy: return "SYNERGY";
    }
    return "UPGRADE";
}

std::string runUpgradeRankLabel(int rank)
{
    switch (std::clamp(rank, 1, kMaximumRunUpgradeRank)) {
    case 1: return "I";
    case 2: return "II";
    case 3: return "III";
    }
    return "I";
}

bool generateRunUpgradeOffers(GameState& state, const ContentCatalog& catalog, Random& rng)
{
    if (state.run.expedition.progression.runUpgradeOfferPending) {
        const bool containsLockedCombatOffer = !state.meta.hasEncounteredEnemy && std::any_of(
            state.run.expedition.progression.runUpgradeOffers.begin(),
            state.run.expedition.progression.runUpgradeOffers.begin() + std::clamp(
                state.run.expedition.progression.runUpgradeOfferCount,
                0,
                static_cast<int>(state.run.expedition.progression.runUpgradeOffers.size())),
            [&](const RunUpgradeOffer& offer) {
                return runUpgradeRequiresEnemyEncounter(catalog, offer);
            });
        if (!containsLockedCombatOffer) {
            return state.run.expedition.progression.runUpgradeOfferCount > 0;
        }
        // Existing saves can hold a card rolled before the discovery gate was
        // introduced. Replace only that draft; the earned pick remains intact.
        clearRunUpgradeOffers(state.run.expedition.progression);
    }
    clearRunUpgradeOffers(state.run.expedition.progression);
    if (state.run.expedition.progression.pendingRunUpgradeChoices <= 0) {
        return false;
    }

    std::vector<WeightedRunUpgradeCandidate> candidates;
    candidates.reserve(catalog.surfaceUpgrades.size() + catalog.miniDrones.size() +
        catalog.droneModules.size() + catalog.droneSynergies.size());

    for (const SurfaceUpgrade& upgrade : catalog.surfaceUpgrades) {
        const int targetRank = runRigUpgradeRank(state, upgrade.id) + 1;
        if (targetRank <= std::clamp(upgrade.maxRank, 1, kMaximumRunUpgradeRank) &&
            (state.meta.hasEncounteredEnemy ||
                !runUpgradeRequiresEnemyEncounter(catalog, {RunUpgradeKind::Rig, upgrade.id, targetRank, -1}))) {
            RunUpgradeOffer offer {RunUpgradeKind::Rig, upgrade.id, targetRank, -1};
            candidates.push_back({offer, runUpgradeOfferRarity(state, catalog, offer)});
        }
    }

    std::vector<std::string> seenDroneIds;
    for (const std::string& droneId : state.meta.equippedDroneIds) {
        if (containsId(seenDroneIds, droneId)) {
            continue;
        }
        seenDroneIds.push_back(droneId);
        const MiniDrone* drone = catalog.findMiniDrone(droneId);
        const int targetRank = expeditionDroneRank(state, droneId) + 1;
        if (drone != nullptr && isMiniDroneUnlocked(state.meta, *drone) && targetRank <= kMaximumRunUpgradeRank &&
            (state.meta.hasEncounteredEnemy ||
                !runUpgradeRequiresEnemyEncounter(catalog, {RunUpgradeKind::DroneRank, droneId, targetRank, -1}))) {
            RunUpgradeOffer offer {RunUpgradeKind::DroneRank, droneId, targetRank, -1};
            candidates.push_back({offer, runUpgradeOfferRarity(state, catalog, offer)});
        }
    }

    for (const DroneModuleDefinition& module : catalog.droneModules) {
        if (!hasUnlock(state.meta, module.unlockKey)) {
            continue;
        }
        for (std::size_t slot = 0; slot < state.meta.equippedDroneIds.size(); ++slot) {
            const bool occupied = std::any_of(
                state.run.expedition.progression.droneModuleAssignments.begin(),
                state.run.expedition.progression.droneModuleAssignments.end(),
                [&](const DroneFrameModuleAssignment& assignment) {
                    return assignment.equippedFrame == static_cast<int>(slot);
                });
            const MiniDrone* drone = catalog.findMiniDrone(state.meta.equippedDroneIds[slot]);
            if (!occupied && drone != nullptr && drone->role == module.hostRole &&
                (state.meta.hasEncounteredEnemy ||
                    !runUpgradeRequiresEnemyEncounter(catalog, {RunUpgradeKind::DroneGraft, module.id, 0, static_cast<int>(slot)}))) {
                RunUpgradeOffer offer {
                    RunUpgradeKind::DroneGraft,
                    module.id,
                    0,
                    static_cast<int>(slot)};
                candidates.push_back({offer, runUpgradeOfferRarity(state, catalog, offer)});
            }
        }
    }

    for (const DroneSynergyDefinition& synergy : catalog.droneSynergies) {
        if (!containsId(state.run.expedition.progression.selectedSynergyIds, synergy.id) &&
            synergyRequirementsMet(state, catalog, synergy) &&
            (state.meta.hasEncounteredEnemy ||
                !runUpgradeRequiresEnemyEncounter(catalog, {RunUpgradeKind::Synergy, synergy.id, 0, -1}))) {
            RunUpgradeOffer offer {RunUpgradeKind::Synergy, synergy.id, 0, -1};
            candidates.push_back({offer, runUpgradeOfferRarity(state, catalog, offer)});
        }
    }

    if (candidates.empty()) {
        // A level is never rolled back. If every finite upgrade is exhausted,
        // consume exactly one pending choice so the App can loop deterministically.
        state.run.expedition.progression.pendingRunUpgradeChoices = std::max(0, state.run.expedition.progression.pendingRunUpgradeChoices - 1);
        return false;
    }

    const int offerCount = std::min(3, static_cast<int>(candidates.size()));
    const int draftNumber = state.run.expedition.progression.runUpgradeDraftCount + 1;
    std::vector<std::string> forcedRigIds;
    if (draftNumber == 1) {
        forcedRigIds = {content::surfaceUpgrade::highTorqueMotor, content::surfaceUpgrade::wideDrillHead};
    } else if (draftNumber <= 3 && !state.run.expedition.progression.sideCuttersOffered &&
               runRigUpgradeRank(state, content::surfaceUpgrade::sideCutters) == 0) {
        forcedRigIds.push_back(content::surfaceUpgrade::sideCutters);
    }
    const auto isDrillCandidate = [&](const WeightedRunUpgradeCandidate& candidate) {
        if (candidate.offer.kind != RunUpgradeKind::Rig) return false;
        const SurfaceUpgrade* upgrade = catalog.findSurfaceUpgrade(candidate.offer.definitionId);
        return upgrade != nullptr && upgrade->category == SurfaceUpgradeCategory::Drill;
    };
    forcedRigIds.erase(std::remove_if(forcedRigIds.begin(), forcedRigIds.end(), [&](const std::string& id) {
        return std::none_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
            return candidate.offer.kind == RunUpgradeKind::Rig && candidate.offer.definitionId == id;
        });
    }), forcedRigIds.end());
    if (forcedRigIds.empty() && std::any_of(candidates.begin(), candidates.end(), isDrillCandidate)) {
        const auto drill = std::find_if(candidates.begin(), candidates.end(), isDrillCandidate);
        forcedRigIds.push_back(drill->offer.definitionId);
    }
    for (int slot = 0; slot < offerCount; ++slot) {
        if (slot < static_cast<int>(forcedRigIds.size())) {
            const auto forced = std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
                return candidate.offer.kind == RunUpgradeKind::Rig &&
                    candidate.offer.definitionId == forcedRigIds[static_cast<std::size_t>(slot)];
            });
            if (forced != candidates.end()) {
                state.run.expedition.progression.runUpgradeOffers[static_cast<std::size_t>(slot)] = forced->offer;
                candidates.erase(forced);
                continue;
            }
        }
        int totalWeight = 0;
        for (const WeightedRunUpgradeCandidate& candidate : candidates) {
            totalWeight += runUpgradeOfferWeight(candidate.rarity);
        }
        int roll = rng.rangeInt(1, std::max(1, totalWeight));
        std::size_t picked = 0;
        for (; picked + 1 < candidates.size(); ++picked) {
            roll -= runUpgradeOfferWeight(candidates[picked].rarity);
            if (roll <= 0) {
                break;
            }
        }
        state.run.expedition.progression.runUpgradeOffers[static_cast<std::size_t>(slot)] = candidates[picked].offer;
        candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(picked));
    }
    state.run.expedition.progression.runUpgradeOfferCount = offerCount;
    state.run.expedition.progression.runUpgradeOfferPending = offerCount > 0;
    if (offerCount > 0) {
        ++state.run.expedition.progression.runUpgradeDraftCount;
        for (int slot = 0; slot < offerCount; ++slot) {
            const std::string& id = state.run.expedition.progression.runUpgradeOffers[static_cast<std::size_t>(slot)].definitionId;
            state.run.expedition.progression.wideDrillHeadOffered =
                state.run.expedition.progression.wideDrillHeadOffered || id == content::surfaceUpgrade::wideDrillHead;
            state.run.expedition.progression.sideCuttersOffered =
                state.run.expedition.progression.sideCuttersOffered || id == content::surfaceUpgrade::sideCutters;
        }
    }
    return state.run.expedition.progression.runUpgradeOfferPending;
}

bool chooseRunUpgrade(GameState& state, const ContentCatalog& catalog, int index)
{
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    if (!state.run.expedition.progression.runUpgradeOfferPending || state.run.expedition.progression.pendingRunUpgradeChoices <= 0 ||
        index < 0 || index >= state.run.expedition.progression.runUpgradeOfferCount ||
        index >= static_cast<int>(state.run.expedition.progression.runUpgradeOffers.size())) {
        return false;
    }
    const RunUpgradeOffer offer = state.run.expedition.progression.runUpgradeOffers[static_cast<std::size_t>(index)];
    std::string installedName;
    switch (offer.kind) {
    case RunUpgradeKind::Rig: {
        const SurfaceUpgrade* upgrade = catalog.findSurfaceUpgrade(offer.definitionId);
        const int expectedRank = runRigUpgradeRank(state, offer.definitionId) + 1;
        if (upgrade == nullptr || offer.targetRank != expectedRank || expectedRank > upgrade->maxRank) {
            return false;
        }
        auto found = std::find_if(
            state.run.expedition.progression.runRigUpgradeRanks.begin(),
            state.run.expedition.progression.runRigUpgradeRanks.end(),
            [&](const RunRigUpgradeRank& record) { return record.upgradeId == offer.definitionId; });
        if (found == state.run.expedition.progression.runRigUpgradeRanks.end()) {
            state.run.expedition.progression.runRigUpgradeRanks.push_back({offer.definitionId, expectedRank});
        } else {
            found->rank = expectedRank;
        }
        installedName = upgrade->name + " " + runUpgradeRankLabel(expectedRank);
        state.run.mining.rigGeometryValidated = false;
        break;
    }
    case RunUpgradeKind::DroneRank: {
        const MiniDrone* drone = catalog.findMiniDrone(offer.definitionId);
        const int expectedRank = expeditionDroneRank(state, offer.definitionId) + 1;
        if (drone == nullptr || !isMiniDroneUnlocked(state.meta, *drone) ||
            !containsId(state.meta.equippedDroneIds, offer.definitionId) ||
            offer.targetRank != expectedRank || expectedRank > kMaximumRunUpgradeRank) {
            return false;
        }
        auto found = std::find_if(
            state.run.expedition.progression.runDroneRanks.begin(),
            state.run.expedition.progression.runDroneRanks.end(),
            [&](const RunDroneRank& record) { return record.droneId == offer.definitionId; });
        if (found == state.run.expedition.progression.runDroneRanks.end()) {
            state.run.expedition.progression.runDroneRanks.push_back({offer.definitionId, expectedRank});
        } else {
            found->rank = expectedRank;
        }
        installedName = drone->name + " " + runUpgradeRankLabel(expectedRank);
        break;
    }
    case RunUpgradeKind::DroneGraft: {
        const DroneModuleDefinition* module = catalog.findDroneModule(offer.definitionId);
        if (module == nullptr || !hasUnlock(state.meta, module->unlockKey) ||
            offer.slotIndex < 0 || offer.slotIndex >= static_cast<int>(state.meta.equippedDroneIds.size())) {
            return false;
        }
        const bool occupied = std::any_of(
            state.run.expedition.progression.droneModuleAssignments.begin(),
            state.run.expedition.progression.droneModuleAssignments.end(),
            [&](const DroneFrameModuleAssignment& assignment) {
                return assignment.equippedFrame == offer.slotIndex;
            });
        const std::string& droneId = state.meta.equippedDroneIds[static_cast<std::size_t>(offer.slotIndex)];
        const MiniDrone* drone = catalog.findMiniDrone(droneId);
        if (occupied || drone == nullptr || drone->role != module->hostRole) {
            return false;
        }
        state.run.expedition.progression.droneModuleAssignments.push_back({offer.slotIndex, droneId, module->kind});
        installedName = module->name + " on slot " + std::to_string(offer.slotIndex + 1);
        break;
    }
    case RunUpgradeKind::Synergy: {
        const DroneSynergyDefinition* synergy = catalog.findDroneSynergy(offer.definitionId);
        if (synergy == nullptr || containsId(state.run.expedition.progression.selectedSynergyIds, synergy->id) ||
            !synergyRequirementsMet(state, catalog, *synergy)) {
            return false;
        }
        state.run.expedition.progression.selectedSynergyIds.push_back(synergy->id);
        installedName = synergy->name;
        break;
    }
    }

    state.run.expedition.progression.pendingRunUpgradeChoices = std::max(0, state.run.expedition.progression.pendingRunUpgradeChoices - 1);
    clearRunUpgradeOffers(state.run.expedition.progression);
    appendSurfaceLog(expedition, "Run upgrade installed: " + installedName + ".");
    state.statusLine = "Run upgrade installed: " + installedName + ".";
    return true;
}

SurfaceUpgradeEffects surfaceUpgradeEffects(const GameState& state, const ContentCatalog& catalog)
{
    SurfaceUpgradeEffects effects;
    for (const SurfaceUpgrade& upgrade : catalog.surfaceUpgrades) {
        const int rank = runRigUpgradeRank(state, upgrade.id);
        if (rank <= 0) {
            continue;
        }
        const double scale = static_cast<double>(rank);
        effects.oreAttractionRadius += upgrade.stats.oreAttractionRadius * scale;
        effects.drillPower += upgrade.stats.drillPower * scale;
        effects.drillCooling += upgrade.stats.drillCooling * scale;
        effects.drillHeatReduction += upgrade.stats.drillHeatReduction * scale;
        effects.drillDurability += upgrade.stats.drillDurability * scale;
        effects.drillHeadWidth += upgrade.stats.drillHeadWidth * scale;
        effects.sideCutterReach += upgrade.stats.sideCutterReach * scale;
        effects.hardRockPower += upgrade.stats.hardRockPower * scale;
        effects.hardRockBounceRelief += upgrade.stats.hardRockBounceRelief * scale;
        effects.oreYieldChance += upgrade.stats.oreYieldChance * scale;
        effects.scannerRadius += upgrade.stats.scannerRadius * scale;
        effects.hazardRelief += upgrade.stats.hazardRelief * scale;
        effects.droneSpeed += upgrade.stats.droneSpeed * scale;
        effects.oxygenSeconds += upgrade.stats.oxygenSeconds * scale;
        effects.droneStorage += upgrade.stats.droneStorage * scale;
        effects.droneEngineEfficiency += upgrade.stats.droneEngineEfficiency * scale;
        effects.artifactTowEfficiency += upgrade.stats.artifactTowEfficiency * scale;
        effects.scannerPulseDamage += upgrade.stats.scannerPulseDamage * rank;
        effects.names.push_back(upgrade.name + " " + runUpgradeRankLabel(rank));
    }
    effects.hardRockBounceRelief = std::clamp(effects.hardRockBounceRelief, 0.0, 0.35);
    effects.droneEngineEfficiency = std::clamp(effects.droneEngineEfficiency, 0.0, 0.75);
    effects.artifactTowEfficiency = std::clamp(effects.artifactTowEfficiency, 0.0, 0.80);
    return effects;
}

double nominalSurfaceRigFuelCapacity(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId)
{
    const Destination* destination = catalog.findDestination(destinationId);
    const int tier = destination == nullptr ? 1 : std::max(1, destination->tier);
    const double transferCapacity =
        tuning::launchProgression::baseFuelCapacity +
        static_cast<double>(std::clamp(
            state.meta.launchUpgrades.fuelTanks,
            0,
            tuning::launchProgression::maximumUpgradeRank)) *
            tuning::launchProgression::fuelPerTankRank;
    const double calibratedRouteCost = std::min(
        tuning::launch::routeFuelMaximum,
        tuning::launch::routeFuelBase +
            static_cast<double>(tier) * tuning::launch::routeFuelPerTier);
    return tuning::research::expeditionRigPackFuel +
        std::max(0.0, transferCapacity - calibratedRouteCost);
}

bool droneBayUnlocked(const GameState& state)
{
    return hasUnlock(state.meta, content::unlock::droneBay);
}

MaterialInventory droneSlotUpgradeCost(int nextSlot)
{
    switch (nextSlot) {
    case 2:
        return {.common = 4};
    case 3:
        return {.common = 6, .rare = 2};
    case 4:
        return {.rare = 5};
    case 5:
        return {.rare = 7, .exotic = 2};
    case 6:
        return {.rare = 10, .exotic = 4};
    default:
        return {};
    }
}

MaterialInventory miniDroneAdditionalUnitCost(const MiniDrone& drone)
{
    switch (drone.rarity) {
    case Rarity::Common:
        return {.common = 20};
    case Rarity::Uncommon:
        return {.common = 30};
    case Rarity::Rare:
        return {.common = 40, .rare = 1};
    case Rarity::Prototype:
        return {.common = 60, .rare = 2};
    }
    return {.common = 30};
}

int ownedMiniDroneCount(const GameState& state, std::string_view droneId)
{
    return static_cast<int>(std::count(
        state.meta.ownedDroneIds.begin(),
        state.meta.ownedDroneIds.end(),
        droneId));
}

int equippedMiniDroneCount(const GameState& state, std::string_view droneId)
{
    return static_cast<int>(std::count(
        state.meta.equippedDroneIds.begin(),
        state.meta.equippedDroneIds.end(),
        droneId));
}

double expeditionDroneRankMultiplier(int level)
{
    return 1.0 + 0.30 * static_cast<double>(std::clamp(level, 1, 3) - 1);
}

void ensureDroneBayState(GameState& state, const ContentCatalog& catalog)
{
    if (!droneBayUnlocked(state)) {
        state.meta.droneBaySlots = 0;
        state.meta.ownedDroneIds.clear();
        state.meta.equippedDroneIds.clear();
        return;
    }

    state.meta.droneBaySlots = std::clamp(state.meta.droneBaySlots <= 0 ? 1 : state.meta.droneBaySlots, 1, 6);

    // Unlocking a drone type makes its frame purchasable in Drone Ops; it
    // does not silently add a frame to the player's bay. Scenario rewards
    // that intentionally grant a specific drone still add it explicitly.

    state.meta.ownedDroneIds.erase(
        std::remove_if(
            state.meta.ownedDroneIds.begin(),
            state.meta.ownedDroneIds.end(),
            [&](const std::string& id) {
                const MiniDrone* drone = catalog.findMiniDrone(id);
                return drone == nullptr || !isMiniDroneUnlocked(state.meta, *drone);
            }),
        state.meta.ownedDroneIds.end());

    std::vector<std::string> validEquipped;
    validEquipped.reserve(state.meta.equippedDroneIds.size());
    for (const std::string& id : state.meta.equippedDroneIds) {
        if (static_cast<int>(std::count(validEquipped.begin(), validEquipped.end(), id)) <
            ownedMiniDroneCount(state, id)) {
            validEquipped.push_back(id);
        }
    }
    state.meta.equippedDroneIds = std::move(validEquipped);
    if (state.meta.equippedDroneIds.size() > static_cast<std::size_t>(state.meta.droneBaySlots)) {
        state.meta.equippedDroneIds.resize(static_cast<std::size_t>(state.meta.droneBaySlots));
    }

}

bool canUpgradeDroneSlot(const GameState& state)
{
    if (!droneBayUnlocked(state) || state.meta.droneBaySlots >= 6) {
        return false;
    }
    // Paid expansion begins once the authored campaign has awarded a second
    // bay slot. This is a capacity rule, not a campaign-name dependency.
    if (state.meta.droneBaySlots < 2) {
        return false;
    }
    return canAffordMaterials(state.meta.materials, droneSlotUpgradeCost(state.meta.droneBaySlots + 1));
}

bool upgradeDroneSlot(GameState& state, const ContentCatalog& catalog)
{
    ensureDroneBayState(state, catalog);
    if (!droneBayUnlocked(state) || state.meta.droneBaySlots >= 6) {
        state.statusLine = "Drone Bay is already at maximum capacity.";
        return false;
    }
    if (state.meta.droneBaySlots < 2) {
        state.statusLine = "Unlock a second bay slot before adding paid capacity.";
        return false;
    }

    const MaterialInventory cost = droneSlotUpgradeCost(state.meta.droneBaySlots + 1);
    if (!spendMaterials(state.meta.materials, cost)) {
        state.statusLine = "Recovered materials are short for the next Drone Bay slot.";
        return false;
    }

    state.meta.droneBaySlots += 1;
    state.statusLine = "Drone Bay expanded to " + std::to_string(state.meta.droneBaySlots) + " slots.";
    return true;
}

bool equipMiniDrone(GameState& state, const ContentCatalog& catalog, int index)
{
    ensureDroneBayState(state, catalog);
    if (!droneBayUnlocked(state) || index < 0 || index >= static_cast<int>(catalog.miniDrones.size())) {
        return false;
    }

    const MiniDrone& drone = catalog.miniDrones[static_cast<std::size_t>(index)];
    if (!isMiniDroneUnlocked(state.meta, drone)) {
        state.statusLine = drone.name + " is still locked.";
        return false;
    }

    if (state.meta.equippedDroneIds.size() >= static_cast<std::size_t>(state.meta.droneBaySlots)) {
        state.statusLine = "Drone Loadout full. Unequip a slot or expand the bay.";
        return false;
    }

    const int ownedCount = ownedMiniDroneCount(state, drone.id);
    const int equippedCount = equippedMiniDroneCount(state, drone.id);
    if (equippedCount >= ownedCount) {
        const MaterialInventory cost = miniDroneAdditionalUnitCost(drone);
        if (!spendMaterials(state.meta.materials, cost)) {
            state.statusLine = "Need additional materials to build another " + drone.name + ".";
            return false;
        }
        state.meta.ownedDroneIds.push_back(drone.id);
    }
    state.meta.equippedDroneIds.push_back(drone.id);
    // Equipment assignment is a first-class scenario event. It deliberately
    // routes by the equipped unit ID instead of by a campaign objective so
    // authored and procedural scenarios can require any configured support
    // frame without a new dispatcher branch.
    (void)recordScenarioEvent(
        state,
        catalog,
        {ScenarioEventKind::EquipmentAssigned, {}, {}, drone.id, {}, 1, 0});
    state.statusLine = equippedCount >= ownedCount
        ? "Built and assigned " + (ownedCount == 0 ? drone.name : ("another " + drone.name)) + " to Drone Loadout slot " + std::to_string(static_cast<int>(state.meta.equippedDroneIds.size())) + "."
        : drone.name + " added to Drone Loadout slot " + std::to_string(static_cast<int>(state.meta.equippedDroneIds.size())) + ".";
    return true;
}

bool unequipMiniDroneSlot(GameState& state, const ContentCatalog& catalog, int slotIndex)
{
    ensureDroneBayState(state, catalog);
    if (!droneBayUnlocked(state) || slotIndex < 0 || slotIndex >= static_cast<int>(state.meta.equippedDroneIds.size())) {
        return false;
    }

    const std::string droneId = state.meta.equippedDroneIds[static_cast<std::size_t>(slotIndex)];
    const MiniDrone* drone = catalog.findMiniDrone(droneId);
    state.meta.equippedDroneIds.erase(state.meta.equippedDroneIds.begin() + slotIndex);
    state.statusLine = (drone != nullptr ? drone->name : std::string("Drone")) + " removed from Drone Loadout.";
    return true;
}

MiniDroneLoadoutEffects miniDroneLoadoutEffects(const GameState& state, const ContentCatalog& catalog)
{
    MiniDroneLoadoutEffects effects;
    if (!droneBayUnlocked(state)) {
        return effects;
    }

    int miningDrones = 0;
    int resourceDrones = 0;
    int surveyDrones = 0;
    int hazardDrones = 0;
    int attackDrones = 0;
    int defenseDrones = 0;
    for (const std::string& droneId : state.meta.equippedDroneIds) {
        const MiniDrone* drone = catalog.findMiniDrone(droneId);
        if (drone == nullptr || !isMiniDroneUnlocked(state.meta, *drone)) {
            continue;
        }
        const int upgradeLevel = expeditionDroneRank(state, drone->id);
        const double upgradeMultiplier = expeditionDroneRankMultiplier(upgradeLevel);
        effects.passiveMiningRate += drone->stats.passiveMiningRate * upgradeMultiplier;
        effects.oxygenSeconds += drone->stats.oxygenSeconds * upgradeMultiplier;
        effects.scannerRadius += drone->stats.scannerRadius * upgradeMultiplier;
        effects.drillIntegrityRelief += drone->stats.drillIntegrityRelief * upgradeMultiplier;
        effects.hardRockBounceRelief += drone->stats.hardRockBounceRelief * upgradeMultiplier;
        effects.enemyEncounterRelief += drone->stats.enemyEncounterRelief * upgradeMultiplier;
        effects.sentryDamagePerSecond += drone->stats.sentryDamagePerSecond * upgradeMultiplier;
        effects.enemyDamageRelief += drone->stats.enemyDamageRelief * upgradeMultiplier;
        effects.areaControlDamagePerSecond += drone->stats.areaControlDamagePerSecond * upgradeMultiplier;
        effects.enemySlow += drone->stats.enemySlow * upgradeMultiplier;
        effects.reactiveArmorDamagePerSecond += drone->stats.reactiveArmorDamagePerSecond * upgradeMultiplier;
        effects.environmentalShieldRelief += drone->stats.environmentalShieldRelief * upgradeMultiplier;
        effects.names.push_back(drone->name + " Mk " + runUpgradeRankLabel(upgradeLevel));
        switch (drone->role) {
        case MiniDroneRole::Mining:
            miningDrones += 1;
            break;
        case MiniDroneRole::Resource:
            resourceDrones += 1;
            break;
        case MiniDroneRole::Survey:
            surveyDrones += 1;
            break;
        case MiniDroneRole::Hazard:
            hazardDrones += 1;
            break;
        case MiniDroneRole::Attack:
            attackDrones += 1;
            break;
        case MiniDroneRole::Defense:
            defenseDrones += 1;
            break;
        }
    }

    (void)miningDrones;
    (void)resourceDrones;
    (void)surveyDrones;
    (void)hazardDrones;
    (void)attackDrones;
    (void)defenseDrones;
    for (const std::string& synergyId : state.run.expedition.progression.selectedSynergyIds) {
        const DroneSynergyDefinition* synergy = catalog.findDroneSynergy(synergyId);
        if (synergy == nullptr || !synergyRequirementsMet(state, catalog, *synergy)) {
            continue;
        }
        const DroneSynergyStats& bonus = synergy->stats;
        effects.passiveMiningRate += bonus.passiveMiningRate;
        effects.oxygenSeconds += bonus.oxygenSeconds;
        effects.scannerRadius += bonus.scannerRadius;
        effects.enemyDamageRelief += bonus.enemyDamageRelief;
        effects.areaControlDamagePerSecond += bonus.areaControlDamagePerSecond;
        effects.enemySlow += bonus.enemySlow;
        effects.reactiveArmorDamagePerSecond += bonus.reactiveArmorDamagePerSecond;
        effects.environmentalShieldRelief += bonus.environmentalShieldRelief;
        effects.hazardTreatmentRateBonus += bonus.hazardTreatmentRateBonus;
        effects.alliedCritChanceBonus += bonus.alliedCritChanceBonus;
        effects.alliedFireRateBonus += bonus.alliedFireRateBonus;
        effects.sentryVolleyBonus += bonus.sentryVolleyBonus;
        effects.synergyNames.push_back(synergy->name);
        if (synergy->signatureKind != MiniDroneSignatureKind::None &&
            synergy->signatureTier > effects.signatureTier) {
            effects.signatureKind = synergy->signatureKind;
            effects.signatureName = synergy->name;
            effects.signatureDetail = synergy->description;
            effects.signatureTier = synergy->signatureTier;
        }
    }

    effects.passiveMiningRate = std::clamp(effects.passiveMiningRate, 0.0, 0.40);
    effects.scannerRadius = std::clamp(effects.scannerRadius, 0.0, 5.0);
    effects.drillIntegrityRelief = std::clamp(effects.drillIntegrityRelief, 0.0, 0.35);
    effects.hardRockBounceRelief = std::clamp(effects.hardRockBounceRelief, 0.0, 0.55);
    effects.enemyEncounterRelief = std::clamp(effects.enemyEncounterRelief, 0.0, 0.18);
    effects.sentryDamagePerSecond = std::clamp(effects.sentryDamagePerSecond, 0.0, 8.0);
    effects.enemyDamageRelief = std::clamp(effects.enemyDamageRelief, 0.0, 0.55);
    effects.areaControlDamagePerSecond = std::clamp(effects.areaControlDamagePerSecond, 0.0, 3.0);
    effects.enemySlow = std::clamp(effects.enemySlow, 0.0, 0.45);
    effects.reactiveArmorDamagePerSecond = std::clamp(effects.reactiveArmorDamagePerSecond, 0.0, 4.0);
    effects.environmentalShieldRelief = std::clamp(effects.environmentalShieldRelief, 0.0, 0.35);
    effects.hazardTreatmentRateBonus = std::clamp(effects.hazardTreatmentRateBonus, 0.0, 0.25);
    effects.alliedCritChanceBonus = std::clamp(effects.alliedCritChanceBonus, 0.0, tuning::mining::alliedCritChanceMaximum - tuning::mining::alliedCritChance);
    effects.alliedFireRateBonus = std::clamp(effects.alliedFireRateBonus, 0.0, tuning::mining::alliedFireRateBonusMaximum);
    effects.sentryVolleyBonus = std::clamp(effects.sentryVolleyBonus, 0, tuning::mining::alliedSentryVolleyMaximum);
    return effects;
}

std::string_view surfaceSiteProfileName(SurfaceSiteProfile profile)
{
    switch (profile) {
    case SurfaceSiteProfile::SurveyBasin:
        return text::panel::surfaceSites::surveyBasin;
    case SurfaceSiteProfile::OreShelf:
        return text::panel::surfaceSites::oreShelf;
    case SurfaceSiteProfile::FractureField:
        return text::panel::surfaceSites::fractureField;
    }
    return text::panel::surfaceSites::surveyBasin;
}

std::string_view surfaceSiteProfileDetail(SurfaceSiteProfile profile)
{
    switch (profile) {
    case SurfaceSiteProfile::SurveyBasin:
        return text::panel::surfaceSites::surveyBasinDetail;
    case SurfaceSiteProfile::OreShelf:
        return text::panel::surfaceSites::oreShelfDetail;
    case SurfaceSiteProfile::FractureField:
        return text::panel::surfaceSites::fractureFieldDetail;
    }
    return text::panel::surfaceSites::surveyBasinDetail;
}

std::string researchOutcomeSummary(const ResearchOutcome& outcome)
{
    if (!outcome.completed) {
        return std::string(text::status::researchCompleted);
    }

    std::vector<std::string> parts;
    parts.push_back(std::string(text::status::researchCompleted));
    if (outcome.blueprintGain > 0) {
        parts.push_back(text::panel::blueprintGain(outcome.blueprintGain));
    }
    if (outcome.materialCost.common > 0 || outcome.materialCost.rare > 0 || outcome.materialCost.exotic > 0) {
        parts.push_back("Spent " + text::panel::materialSummary(
            outcome.materialCost.common,
            outcome.materialCost.rare,
            outcome.materialCost.exotic));
    }
    if (outcome.unlockedReward && !outcome.rewardUnlockKey.empty()) {
        parts.push_back(text::panel::unlocksFamily(unlockDisplayName(outcome.rewardUnlockKey)));
    }
    if (outcome.identifiedArtifact) {
        parts.push_back("Decoded " + outcome.artifactId);
    }
    return joinParts(parts);
}

std::string surfaceActionSummary(const SurfaceActionOutcome& outcome)
{
    if (!outcome.applied) {
        return outcome.message.empty() ? std::string(text::status::surfaceSupplyBlocked) : outcome.message;
    }
    std::string status = outcome.message;
    if (outcome.hazardTriggered && !outcome.hazardMessage.empty()) {
        status += " " + outcome.hazardMessage;
    }
    if (outcome.eventType != SurfaceEventType::None && !outcome.eventMessage.empty()) {
        status += " " + outcome.eventMessage;
    }
    const std::string deltaSummary = surfaceDeltaSummary(outcome);
    if (!deltaSummary.empty()) {
        status += " (" + deltaSummary + ")";
    }
    return status;
}

ResearchOutcome completeResearchProject(GameState& state, const ContentCatalog& catalog, int index)
{
    ResearchOutcome outcome;
    if (index < 0 || index >= static_cast<int>(state.run.researchProjectIds.size())) {
        return outcome;
    }

    const std::string& projectId = state.run.researchProjectIds[static_cast<std::size_t>(index)];
    const ResearchProject* project = catalog.findResearchProject(projectId);
    const Destination* destination = currentResearchDestination(state, catalog);
    if (project == nullptr || destination == nullptr || !projectUnlockedForDestination(*project, state.meta, *destination)) {
        return outcome;
    }
    if (!spendMaterials(state.meta.materials, project->materialCost)) {
        return outcome;
    }

    const int blueprintGain = researchBlueprintGain(state.meta, *project);
    const bool rewardUnlockAvailable = !project->rewardUnlockKey.empty() && !hasUnlock(state.meta, project->rewardUnlockKey);
    if (rewardUnlockAvailable) {
        state.meta.unlockKeys.push_back(project->rewardUnlockKey);
    }
    state.meta.blueprintProgress += blueprintGain;
    unlockFromBlueprints(state);
    if (hasTag(project->tags, "artifact")) {
        if (ArtifactRecord* artifact = firstUnidentifiedArtifact(state)) {
            artifact->identified = true;
            outcome.identifiedArtifact = true;
            outcome.artifactId = artifact->id;
        }
    }
    state.run.researchProjectIds[static_cast<std::size_t>(index)].clear();

    outcome.completed = true;
    outcome.projectId = project->id;
    outcome.blueprintGain = blueprintGain;
    outcome.materialCost = project->materialCost;
    outcome.rewardUnlockKey = project->rewardUnlockKey;
    outcome.unlockedReward = rewardUnlockAvailable;
    ensureDroneBayState(state, catalog);
    return outcome;
}

void startSurfaceExpedition(GameState& state, const ContentCatalog& catalog, Random* rng)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr || !destinationSupportsSurface(*destination)) {
        PlanetaryExpeditionState preserved;
        state.run.planetaryExpedition = std::move(preserved);
        return;
    }

    PlanetaryExpeditionState expedition;
    expedition.active = true;
    expedition.destinationId = destination->id;
    expedition.siteProfile = generatedSurfaceSiteProfile(state, *destination, rng);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const double baseHazard = tuning::research::baseHazard + destination->tier * tuning::research::hazardPerTier;
    const double reconPenalty = landingReconHazardPenalty(state);
    expedition.supply = tuning::research::baseSupply + destination->tier * tuning::research::supplyPerTier + surfaceToolEffects(state.meta).supplyBonus + crew.supplyBonus + site.supplyBonus;
    expedition.transferFuelRecovered = std::max(0.0, state.run.approach.transferFuelRemaining);
    expedition.expeditionPackFuel = tuning::research::expeditionRigPackFuel;
    expedition.rigFuelCapacity = expedition.expeditionPackFuel + expedition.transferFuelRecovered;
    expedition.rigFuel = expedition.rigFuelCapacity;
    expedition.hazard = std::max(baseHazard + reconPenalty, baseHazard + site.hazardDelta + reconPenalty - crew.hazardRelief);
    expedition.enemyEncountersEnabled = destinationAllowsEnemyEncounters(*destination);
    addDestinationHistoryValue(state.meta.destinationLandings, catalog, destination->id);
    state.run.approach = {};
    appendSurfaceLog(expedition, std::string(surfaceSiteProfileName(expedition.siteProfile)) + ": " + std::string(surfaceSiteProfileDetail(expedition.siteProfile)));
    appendSurfaceLog(
        expedition,
        "Rig fuel loaded: " + display::fixed(expedition.rigFuel, 1) +
            " (" + display::fixed(expedition.transferFuelRecovered, 1) +
            " transfer + " + display::fixed(expedition.expeditionPackFuel, 1) +
            " expedition allotment). No surface reserve or automatic refill.");
    state.run.planetaryExpedition = expedition;
    // Landing never grants a free draft. XP thresholds are the only source of
    // run-upgrade choices; the App opens a persisted offer after an award.
    (void)rng;
}

double surfaceEnemyEncounterChance(const GameState& state)
{
    const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    if (!expedition.active || !expedition.enemyEncountersEnabled) {
        return 0.0;
    }

    const SurfaceToolEffects tools = surfaceToolEffects(state.meta);
    const MiniDroneLoadoutEffects drones = miniDroneLoadoutEffects(state, createDefaultContent());
    const MiningArenaRules rules = activeSurfaceArenaRules(state);
    const double progressionPressure = rules.request.act == MiningAct::ActThree
        ? 0.14 + static_cast<double>(rules.request.difficulty) * 0.02
        : 0.05 + static_cast<double>(rules.request.difficulty) * 0.02;
    return std::clamp(
        progressionPressure
            + expedition.hazard * tuning::research::surfaceEnemyChanceHazardScale
            - tools.enemyEncounterRelief
            - drones.enemyEncounterRelief,
        0.0,
        tuning::research::surfaceEnemyChanceMaximum);
}

bool hasPendingSurfacePayload(const MaterialInventory& materials, const std::vector<ArtifactRecord>& artifacts, int cargo)
{
    return cargo > 0 || materials.common > 0 || materials.rare > 0 || materials.exotic > 0 || !artifacts.empty();
}


const SurfaceDepthProspect* findSurfaceDepthProspect(const PlanetaryExpeditionState& expedition, int absoluteDepth)
{
    const auto found = std::find_if(expedition.depthProspects.begin(), expedition.depthProspects.end(), [&](const SurfaceDepthProspect& prospect) {
        return prospect.absoluteDepth == absoluteDepth;
    });
    return found == expedition.depthProspects.end() ? nullptr : &(*found);
}

SurfaceReturnSafetyAssessment surfaceReturnSafetyAssessment(
    const GameState& state,
    const ContentCatalog& catalog,
    int absoluteDepth)
{
    SurfaceReturnSafetyAssessment assessment;
    assessment.depth = std::max(0, absoluteDepth);
    const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    const int completedHostileSorties = destinationHistoryValue(
        state.meta.destinationSuccesses,
        catalog,
        expedition.destinationId);
    const int landingOrdinal = destinationHistoryValue(
        state.meta.destinationLandings,
        catalog,
        expedition.destinationId);
    MiningArenaRequest request = campaignMiningArenaRequest(
        state.meta.chapter,
        expedition.destinationId,
        assessment.depth,
        completedHostileSorties,
        state.seed,
        landingOrdinal);
    MiningArenaRules rules;
    if (!expedition.pendingMiningSiteDefinitionId.empty()) {
        if (const MiningSiteDefinition* site = catalog.findMiningSite(
                expedition.pendingMiningSiteDefinitionId)) {
            MiningArenaRequest siteRequest = site->arena;
            if (siteRequest.seed == 0) {
                siteRequest.seed = request.seed;
            }
            rules = resolveMiningSiteArenaRules(siteRequest, *site);
        } else {
            rules = resolveMiningArenaRules(request);
        }
    } else if (const MiningSiteProgress* site = pendingCompatibilityMiningSite(
                   state.meta,
                   expedition.destinationId)) {
        request.act = site->act;
        request.difficulty = site->difficulty;
        request.seed = site->seed;
        request.gateOverrideEnabled = true;
        request.gateOverride = site->gateType;
        rules = resolveMiningArenaRules(request);
    } else {
        rules = resolveMiningArenaRules(request);
    }

    if (!rules.mechanics.oxygenAndFuel || assessment.depth <= 0) {
        return assessment;
    }

    const MiningDrillStats stats = miningDrillStats(state, catalog);
    assessment.oxygenSeconds = static_cast<int>(std::floor(stats.oxygenSeconds));
    const double verticalCells = std::max(1, stats.terrainHeight - 7);
    const double idealLayerSeconds = verticalCells / std::max(0.1, stats.speed);
    const double safeLayerSeconds =
        (idealLayerSeconds + 0.65) *
        tuning::mining::returnEnduranceTraversalScale;
    const double estimatedSeconds =
        tuning::mining::returnEnduranceDockingSeconds +
        static_cast<double>(assessment.depth) * safeLayerSeconds;
    assessment.estimatedReturnSeconds =
        static_cast<int>(std::ceil(estimatedSeconds));
    assessment.fuelCycleSeconds = miningRigFuelCycleSeconds(state);
    assessment.fuelNeededAfterDeployment = static_cast<int>(std::ceil(
        estimatedSeconds * miningRigFuelConsumptionPerSecond(state)));
    assessment.fuelAvailableAfterDeployment = static_cast<int>(std::floor(std::max(
        0.0,
        expedition.rigFuel - 1.0)));

    const int oxygenMargin =
        assessment.oxygenSeconds - assessment.estimatedReturnSeconds;
    const int fuelMargin =
        assessment.fuelAvailableAfterDeployment -
        assessment.fuelNeededAfterDeployment;
    assessment.oxygenCritical = oxygenMargin < 0;
    assessment.fuelCritical = fuelMargin < 0;
    if (assessment.oxygenCritical || assessment.fuelCritical) {
        assessment.severity = SurfaceReturnSafetySeverity::Critical;
    } else if (
        oxygenMargin <= tuning::mining::returnEnduranceCautionSeconds ||
        fuelMargin == 0) {
        assessment.severity = SurfaceReturnSafetySeverity::Caution;
    }
    return assessment;
}

int deepestContiguousSurveyedDepth(const GameState& state)
{
    const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    const int surveyRating = surfaceDepthRating(
        state,
        SurfaceDepthUpgradeKind::SurveyArray);
    int depth = std::max(0, expedition.depth);
    while (depth < surveyRating &&
           findSurfaceDepthProspect(expedition, depth + 1) != nullptr) {
        ++depth;
    }
    return depth;
}

bool surfaceSurveyLimitReached(const GameState& state)
{
    return deepestContiguousSurveyedDepth(state) >= surfaceSurveyDepthLimit(state);
}

int surfaceSurveyDepthLimit(const GameState& state)
{
    return std::min(
        surfaceDepthRating(state, SurfaceDepthUpgradeKind::SurveyArray),
        surfaceDepthRating(state, SurfaceDepthUpgradeKind::BoreSystem));
}

SurfaceDepthCapability surfaceDepthCapability(
    const GameState& state,
    const ContentCatalog& catalog,
    int targetDepth)
{
    SurfaceDepthCapability capability;
    const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    capability.targetDepth = std::max(0, targetDepth);
    capability.surveyRating = surfaceDepthRating(
        state,
        SurfaceDepthUpgradeKind::SurveyArray);
    capability.boreRating = surfaceDepthRating(
        state,
        SurfaceDepthUpgradeKind::BoreSystem);
    capability.surveyedThroughDepth = deepestContiguousSurveyedDepth(state);
    capability.usableDepth = std::max(0, expedition.depth);

    const int physicalSurveyLimit = std::min(
        capability.surveyedThroughDepth,
        capability.boreRating);
    for (int depth = expedition.depth + 1; depth <= physicalSurveyLimit; ++depth) {
        const SurfaceReturnSafetyAssessment safety =
            surfaceReturnSafetyAssessment(state, catalog, depth);
        if (safety.severity == SurfaceReturnSafetySeverity::Critical) {
            break;
        }
        capability.usableDepth = depth;
    }

    if (capability.targetDepth <= expedition.depth) {
        return capability;
    }
    if (capability.targetDepth > capability.surveyRating) {
        capability.blocker = SurfaceDepthBlocker::SurveyRating;
        return capability;
    }
    if (capability.targetDepth > capability.boreRating) {
        capability.blocker = SurfaceDepthBlocker::BoreRating;
        return capability;
    }
    if (findSurfaceDepthProspect(expedition, capability.targetDepth) == nullptr) {
        capability.blocker = SurfaceDepthBlocker::Unsurveyed;
        return capability;
    }
    capability.returnSafety = surfaceReturnSafetyAssessment(
        state,
        catalog,
        capability.targetDepth);
    if (capability.returnSafety.severity == SurfaceReturnSafetySeverity::Critical) {
        capability.blocker = SurfaceDepthBlocker::ReturnCritical;
        return capability;
    }
    capability.canDig = true;
    return capability;
}

std::string surfaceDepthBlockerMessage(const SurfaceDepthCapability& capability)
{
    switch (capability.blocker) {
    case SurfaceDepthBlocker::SurveyRating:
        return "Survey Array limit +" + std::to_string(capability.surveyRating) +
            ". Install the next permanent rank in Refit.";
    case SurfaceDepthBlocker::Unsurveyed:
        return "Survey level +" + std::to_string(capability.targetDepth) +
            " before digging there.";
    case SurfaceDepthBlocker::BoreRating:
        return "Bore System limit +" + std::to_string(capability.boreRating) +
            ". Install the next permanent rank in Refit.";
    case SurfaceDepthBlocker::ReturnCritical: {
        std::string reason;
        if (capability.returnSafety.oxygenCritical) {
            reason = "oxygen";
        }
        if (capability.returnSafety.fuelCritical) {
            reason += reason.empty() ? "fuel" : " and fuel";
        }
        return "Return range critical at depth +" +
            std::to_string(capability.targetDepth) + ": " + reason +
            " endurance is insufficient.";
    }
    case SurfaceDepthBlocker::None:
        break;
    }
    return {};
}

MaterialInventory maxMaterials(const MaterialInventory& left, const MaterialInventory& right)
{
    return {
        std::max(left.common, right.common),
        std::max(left.rare, right.rare),
        std::max(left.exotic, right.exotic)
    };
}

MaterialInventory materialDeltaAbove(const MaterialInventory& next, const MaterialInventory& previous)
{
    return {
        std::max(0, next.common - previous.common),
        std::max(0, next.rare - previous.rare),
        std::max(0, next.exotic - previous.exotic)
    };
}

void mergeSurfaceDepthProspect(PlanetaryExpeditionState& expedition, const SurfaceDepthProspect& prospect)
{
    auto found = std::find_if(expedition.depthProspects.begin(), expedition.depthProspects.end(), [&](const SurfaceDepthProspect& existing) {
        return existing.absoluteDepth == prospect.absoluteDepth;
    });
    if (found == expedition.depthProspects.end()) {
        expedition.depthProspects.push_back(prospect);
        return;
    }

    found->depthOffset = std::max(0, found->absoluteDepth - expedition.depth);
    found->possibleMaterials = maxMaterials(found->possibleMaterials, prospect.possibleMaterials);
    found->possibleArtifacts = std::max(found->possibleArtifacts, prospect.possibleArtifacts);
    found->informationPercent = std::max(found->informationPercent, prospect.informationPercent);
}

SurfaceReturnLedger surfaceReturnLedger(const GameState& state, const ContentCatalog& catalog)
{
    SurfaceReturnLedger ledger;
    const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    if (!expedition.active) {
        return ledger;
    }

    ledger.onShip = expedition.temporaryMaterials;
    ledger.toMaterials = ledger.onShip;
    ledger.artifacts = static_cast<int>(expedition.temporaryArtifacts.size());
    if (!expedition.bankedMiningArenaValid || !expedition.bankedMiningProgressionEligible) {
        return ledger;
    }

    for (const ScenarioInstance& instance : state.meta.scenarios) {
        const ScenarioDefinition* definition = findScenarioDefinition(
            catalog,
            instance.definitionId.empty() ? std::string_view(instance.id) : std::string_view(instance.definitionId));
        if (definition == nullptr) {
            continue;
        }
        const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, instance);
        for (const ScenarioStepDefinition& step : resolved.steps) {
            const std::string materialId = step.eventTargetId.empty() ? "common" : step.eventTargetId;
            const ScenarioEvent deliveryEvent {
                ScenarioEventKind::SafeMaterialDelivered,
                {}, {}, expedition.destinationId, materialId, 1, 0
            };
            if (!scenarioStepMatchesEvent(step, deliveryEvent) ||
                scenarioStepState(state, catalog, instance.id, step.id) != ScenarioStepState::Active) {
                continue;
            }
            const ScenarioStepProgress* progress = findScenarioStepProgress(instance, step.id);
            if (progress == nullptr) {
                continue;
            }
            const auto eligibleMaterial = [&]() {
                if (materialId == "rare") {
                    return std::max(0, std::min(expedition.bankedMiningMaterials.rare, ledger.toMaterials.rare));
                }
                if (materialId == "exotic") {
                    return std::max(0, std::min(expedition.bankedMiningMaterials.exotic, ledger.toMaterials.exotic));
                }
                return std::max(0, std::min(expedition.bankedMiningMaterials.common, ledger.toMaterials.common));
            };
            const int allocation = std::min(
                eligibleMaterial(),
                std::max(0, step.requiredProgress - progress->progress));
            if (allocation <= 0) {
                continue;
            }
            ledger.allocations.push_back({
                instance.id,
                step.id,
                step.title.empty() ? step.location : step.title,
                materialId,
                allocation,
                progress->progress + allocation,
                std::max(0, step.requiredProgress)
            });
            if (materialId == "rare") {
                ledger.toMaterials.rare = std::max(0, ledger.toMaterials.rare - allocation);
            } else if (materialId == "exotic") {
                ledger.toMaterials.exotic = std::max(0, ledger.toMaterials.exotic - allocation);
            } else {
                ledger.toMaterials.common = std::max(0, ledger.toMaterials.common - allocation);
            }
        }
    }
    return ledger;
}

SurfaceReturnLedger surfaceReturnLedger(const GameState& state)
{
    return surfaceReturnLedger(state, legacyCampaignCatalog());
}

SurfaceActionOutcome extractSurfacePayload(GameState& state)
{
    return extractSurfacePayload(state, legacyCampaignCatalog());
}

SurfaceActionOutcome extractSurfacePayload(GameState& state, const ContentCatalog& catalog)
{
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    SurfaceActionOutcome outcome;
    if (!expedition.active) {
        return outcome;
    }

    ensureScenarioInstances(state, catalog);
    const SurfaceReturnLedger ledger = surfaceReturnLedger(state, catalog);
    outcome.applied = true;
    outcome.cargoRecovered = true;
    outcome.materialReturned = ledger.onShip;
    outcome.materialDelta = ledger.toMaterials;
    for (const SurfaceReturnAllocation& allocation : ledger.allocations) {
        if (allocation.materialId == "rare") {
            outcome.materialCommitted.rare += allocation.amount;
        } else if (allocation.materialId == "exotic") {
            outcome.materialCommitted.exotic += allocation.amount;
        } else {
            outcome.materialCommitted.common += allocation.amount;
        }
        (void)recordScenarioEvent(
            state,
            catalog,
            {ScenarioEventKind::SafeMaterialDelivered,
             allocation.scenarioId,
             allocation.stepId,
             expedition.destinationId,
             allocation.materialId,
             allocation.amount,
             0});
    }
    writeLegacyCampaignSaveProjection(state, catalog);
    if (state.run.expedition.travelInitialized) addMaterials(state.run.expedition.cargo.materials, outcome.materialDelta);
    else addMaterials(state.meta.materials, outcome.materialDelta);
    const bool recoveredNewAuthoredArtifact = std::any_of(
        expedition.temporaryArtifacts.begin(),
        expedition.temporaryArtifacts.end(),
        [&](const ArtifactRecord& recovered) {
            if (!destinationHasAuthoredProgressionArtifact(
                    catalog,
                    recovered.originDestinationId)) {
                return false;
            }
            return std::none_of(
                state.meta.artifacts.begin(),
                state.meta.artifacts.end(),
                [&](const ArtifactRecord& permanent) {
                    return permanent.originDestinationId ==
                        recovered.originDestinationId;
                });
        });
    applyRecoveredArtifactRewards(
        state,
        catalog,
        expedition.temporaryArtifacts,
        expedition.pendingMiningSiteDefinitionId);
    for (const ArtifactRecord& artifact : expedition.temporaryArtifacts) {
        (void)recordScenarioEvent(
            state,
            catalog,
            {ScenarioEventKind::ArtifactRecovered,
             {},
             {},
             artifact.originDestinationId,
             artifact.id,
             1,
             0});
    }
    if (recoveredNewAuthoredArtifact) {
        (void)bankAuthoredRouteFlightData(
            state,
            catalog,
            expedition.destinationId);
    }
    creditExtractedCompatibilityMiningSiteArtifacts(
        state.meta,
        expedition.temporaryArtifacts);
    if (!expedition.temporaryArtifacts.empty()) {
        awardExpeditionExperience(
            state,
            75.0 * static_cast<double>(expedition.temporaryArtifacts.size()),
            state.screen);
    }
    state.meta.artifacts.insert(
        state.meta.artifacts.end(),
        expedition.temporaryArtifacts.begin(),
        expedition.temporaryArtifacts.end());
    outcome.artifactFound = !expedition.temporaryArtifacts.empty();

    if (expedition.bankedMiningArenaValid && expedition.bankedMiningProgressionEligible) {
        const MiningArenaRules rules = resolveMiningArenaRules({
            expedition.bankedMiningArenaMetadata.act,
            expedition.bankedMiningArenaMetadata.difficulty,
            expedition.bankedMiningArenaMetadata.seed
        });
        creditBankedMiningFirstClearRewards(
            state.meta,
            rules,
            std::max(0, expedition.bankedMiningMaterials.rare),
            std::max(0, expedition.bankedMiningMaterials.exotic));
    }

    outcome.message = "Returned " + std::to_string(ledger.onShip.common) + " Common";
    if (outcome.materialCommitted.common > 0) {
        outcome.message += ". " + std::to_string(outcome.materialCommitted.common) + " committed to " +
            ledger.allocations.front().label + ".";
    }
    if (outcome.materialDelta.common > 0) {
        outcome.message += " " + std::to_string(outcome.materialDelta.common) +
            (state.run.expedition.travelInitialized ? " carried aboard. Dock home to bank." : " added to Materials.");
    }

    if (!expedition.pendingMiningSiteDefinitionId.empty()) {
        (void)recordScenarioEvent(
            state,
            catalog,
            {ScenarioEventKind::MiningSiteCompleted,
             expedition.pendingScenarioId,
             expedition.pendingScenarioStepId,
             expedition.pendingMiningSiteDefinitionId,
             {},
             1,
             0});
    }

    if (state.run.flight.landing.siteCommitted) {
        expedition.active=false;
        expedition.cargo=0;
        expedition.temporaryMaterials={};
        expedition.temporaryArtifacts.clear();
        expedition.bankedMiningMaterials={};
        expedition.bankedMiningArenaValid=false;
        expedition.pendingMiningSiteDefinitionId.clear();
        expedition.pendingScenarioId.clear();
        expedition.pendingScenarioStepId.clear();
    } else {
        PlanetaryExpeditionState preservedProgression;
        expedition = std::move(preservedProgression);
    }
    return outcome;
}

} // namespace rocket
