#include "core/ResearchSystem.h"
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

void ensureDroneBayState(GameState& state, const ContentCatalog& catalog);

bool surfaceOpsTutorialSurveyComplete(const GameState& state)
{
    return ui::briefings::acknowledged(
               state.meta.acknowledgedActivityBriefingIds,
               ui::briefings::mining) ||
        ui::briefings::acknowledged(
            state.meta.acknowledgedActivityBriefingIds,
            ui::briefings::surfaceSurveyComplete);
}

bool surfaceOpsTutorialDigComplete(const GameState& state)
{
    return ui::briefings::acknowledged(
               state.meta.acknowledgedActivityBriefingIds,
               ui::briefings::mining) ||
        ui::briefings::acknowledged(
            state.meta.acknowledgedActivityBriefingIds,
            ui::briefings::surfaceDigComplete);
}

bool surfaceOpsTutorialDigUnlocked(const GameState& state)
{
    return surfaceOpsTutorialSurveyComplete(state);
}

bool surfaceOpsTutorialMiningUnlocked(const GameState& state)
{
    return surfaceOpsTutorialDigComplete(state);
}

bool surfaceOpsTutorialNeedsFirstSurveyBank(const GameState& state)
{
    return !surfaceOpsTutorialSurveyComplete(state);
}

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

int materialCargo(const MaterialInventory& materials)
{
    return std::max(0, materials.common) + std::max(0, materials.rare) * 2 + std::max(0, materials.exotic) * 4;
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
    {CampaignObjectiveId::SaturnSlingshot, content::scenario::outerTransfer, "briefing", "flyby"},
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

const MiningSiteDefinition* miningSiteForSurface(
    const GameState& state,
    const ContentCatalog& catalog)
{
    const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    if (!expedition.active) {
        return nullptr;
    }
    if (!expedition.pendingMiningSiteDefinitionId.empty()) {
        return findMiningSiteDefinition(catalog, expedition.pendingMiningSiteDefinitionId);
    }

    const MiningSiteDefinition* fallback = nullptr;
    for (const ScenarioInstance& instance : state.meta.scenarios) {
        const std::string_view definitionId = instance.definitionId.empty()
            ? std::string_view(instance.id)
            : std::string_view(instance.definitionId);
        const ScenarioDefinition* definition = findScenarioDefinition(catalog, definitionId);
        if (definition == nullptr) {
            continue;
        }
        const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, instance);
        if (!resolved.destinationId.empty() && resolved.destinationId != expedition.destinationId) {
            continue;
        }
        for (const ScenarioStepDefinition& step : resolved.steps) {
            if (step.miningSiteDefinitionId.empty()) {
                continue;
            }
            const MiningSiteDefinition* site = findMiningSiteDefinition(catalog, step.miningSiteDefinitionId);
            if (site == nullptr) {
                continue;
            }
            if (fallback == nullptr) {
                fallback = site;
            }
            const ScenarioStepState stepState = scenarioStepState(state, catalog, instance.id, step.id);
            if (stepState == ScenarioStepState::Active || stepState == ScenarioStepState::ReadyToClaim) {
                return site;
            }
        }
    }
    return fallback;
}

bool surfaceUsesThermalOnlyRegolith(const GameState& state)
{
    const MiningSiteDefinition* site = miningSiteForSurface(state, legacyCampaignCatalog());
    return site != nullptr && site->biome == MiningSiteBiome::ThermalLava;
}

bool surfaceHasAuthoredArtifactSignalAtDepth(
    const GameState& state,
    int depthOffset)
{
    const ContentCatalog& catalog = legacyCampaignCatalog();
    const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    const Destination* destination = catalog.findDestination(expedition.destinationId);
    const auto opportunity = unresolvedProgressionArtifactOpportunity(
        state,
        catalog,
        expedition.destinationId);
    if (destination == nullptr || !opportunity.has_value()) {
        return false;
    }
    const MiningSiteDefinition* site = opportunity->miningSiteDefinitionId.empty()
        ? nullptr
        : findMiningSiteDefinition(catalog, opportunity->miningSiteDefinitionId);
    const int completedHostileSorties = destinationHistoryValue(
        state.meta.destinationSuccesses,
        catalog,
        expedition.destinationId);
    const int landingOrdinal = destinationHistoryValue(
        state.meta.destinationLandings,
        catalog,
        expedition.destinationId);
    const MiningArenaRequest request = site != nullptr
        ? site->arena
        : campaignMiningArenaRequest(
            state.meta.chapter,
            expedition.destinationId,
            expedition.depth,
            completedHostileSorties,
            state.seed,
            landingOrdinal);
    const ProgressionArtifactPlacement placement = resolveProgressionArtifactPlacement(
        state,
        catalog,
        *destination,
        request.difficulty,
        opportunity->siteIdentity);
    return expedition.depth + std::max(0, depthOffset) == placement.targetDepth;
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

std::string artifactId(const PlanetaryExpeditionState& expedition)
{
    std::ostringstream out;
    out << expedition.destinationId << "_artifact_" << expedition.depth;
    return out.str();
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

SurfaceActionOutcome spendSupply(PlanetaryExpeditionState& expedition, int amount)
{
    SurfaceActionOutcome outcome;
    if (!expedition.active || expedition.supply < amount) {
        return outcome;
    }

    expedition.supply -= amount;
    outcome.applied = true;
    outcome.supplyDelta = -amount;
    return outcome;
}

double surfaceHazardChance(double hazard, double scale, double relief)
{
    return std::clamp(
        hazard * scale - relief,
        tuning::research::surfaceHazardChanceMinimum,
        tuning::research::surfaceHazardChanceMaximum);
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

void applySurfaceHazard(
    PlanetaryExpeditionState& expedition,
    SurfaceActionOutcome& outcome,
    Random& rng,
    double scale,
    double relief,
    std::string_view message,
    int supplyLoss,
    int cargoLoss,
    double hazardIncrease)
{
    if (!rng.chance(surfaceHazardChance(expedition.hazard, scale, relief))) {
        return;
    }

    const int actualSupplyLoss = std::min(std::max(0, supplyLoss), std::max(0, expedition.supply));
    const int actualCargoLoss = std::min(std::max(0, cargoLoss), std::max(0, expedition.cargo));
    expedition.supply -= actualSupplyLoss;
    expedition.cargo -= actualCargoLoss;
    expedition.hazard += hazardIncrease;

    outcome.hazardTriggered = true;
    outcome.hazardMessage = std::string(message);
    outcome.supplyDelta -= actualSupplyLoss;
    outcome.cargoDelta -= actualCargoLoss;
    outcome.hazardDelta += hazardIncrease;
}

bool hasSurfaceTooling(const MetaProgress& meta)
{
    return hasUnlock(meta, content::unlock::surfaceProbes)
        || hasUnlock(meta, content::unlock::surfaceDrills)
        || hasUnlock(meta, content::unlock::cargoRigs)
        || hasUnlock(meta, content::unlock::perimeterDrones);
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

void applyEnemyContact(GameState& state, SurfaceActionOutcome& outcome, double encounterChance, Random& rng)
{
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    if (!expedition.enemyEncountersEnabled || !rng.chance(encounterChance)) {
        return;
    }

    const MiningArenaRules rules = activeSurfaceArenaRules(state);
    const bool heavyContact = rules.request.act == MiningAct::ActThree || rules.request.difficulty >= 7;
    const int supplyPressure = tuning::research::surfaceEnemySupplyLoss + (heavyContact ? 1 : 0);
    const int cargoPressure = tuning::research::surfaceEnemyCargoLoss
        + (rules.request.act == MiningAct::ActThree && rules.request.difficulty >= 7 ? 1 : 0);
    const double hazardPressure = tuning::research::surfaceEnemyHazardIncrease
        * std::max(1.0, rules.enemyDamageScale);
    const int actualSupplyLoss = std::min(supplyPressure, std::max(0, expedition.supply));
    const int actualCargoLoss = std::min(cargoPressure, std::max(0, expedition.cargo));
    expedition.supply -= actualSupplyLoss;
    expedition.cargo -= actualCargoLoss;
    expedition.hazard += hazardPressure;

    outcome.eventType = SurfaceEventType::EnemyContact;
    outcome.eventMessage = std::string(text::status::surfaceEnemyContact);
    outcome.supplyDelta -= actualSupplyLoss;
    outcome.cargoDelta -= actualCargoLoss;
    outcome.hazardDelta += hazardPressure;
    outcome.enemyEncounter = true;
    state.meta.hasEncounteredEnemy = true;
}

void applySurfaceEvent(GameState& state, SurfaceActionOutcome& outcome, Random& rng)
{
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    if (!outcome.applied || outcome.hazardTriggered) {
        return;
    }

    applyEnemyContact(state, outcome, surfaceEnemyEncounterChance(state), rng);
    if (outcome.eventType == SurfaceEventType::EnemyContact) {
        return;
    }

    const double eventChance = std::clamp(
        tuning::research::surfaceEventChanceBase + expedition.hazard * tuning::research::surfaceEventChanceHazardScale,
        0.0,
        tuning::research::surfaceEventChanceMaximum);
    if (!rng.chance(eventChance)) {
        return;
    }

    const double failureShare = std::max(
        tuning::research::surfaceEquipmentFailureMinimumShare,
        tuning::research::surfaceEquipmentFailureShare - (hasSurfaceTooling(state.meta) ? tuning::research::surfaceToolFailureRelief : 0.0));
    const double roll = rng.next01();
    if (roll < failureShare) {
        const int actualSupplyLoss = std::min(tuning::research::surfaceEquipmentFailureSupplyLoss, std::max(0, expedition.supply));
        expedition.supply -= actualSupplyLoss;
        expedition.hazard += tuning::research::surfaceEquipmentFailureHazardIncrease;
        outcome.eventType = SurfaceEventType::EquipmentFailure;
        outcome.eventMessage = std::string(text::status::surfaceEquipmentFailure);
        outcome.supplyDelta -= actualSupplyLoss;
        outcome.hazardDelta += tuning::research::surfaceEquipmentFailureHazardIncrease;
        return;
    }

    if (roll < failureShare + tuning::research::surfaceUnexpectedDepositShare) {
        MaterialInventory gain {.common = tuning::research::surfaceDepositCommonGain};
        if (rng.chance(tuning::research::surfaceDepositRareChance + surfaceSiteProfileEffects(expedition.siteProfile).mineRareChanceBonus)) {
            gain.rare += 1;
        }
        expedition.temporaryMaterials.common = std::max(0, expedition.temporaryMaterials.common + gain.common);
        expedition.temporaryMaterials.rare = std::max(0, expedition.temporaryMaterials.rare + gain.rare);
        expedition.temporaryMaterials.exotic = std::max(0, expedition.temporaryMaterials.exotic + gain.exotic);
        expedition.cargo += materialCargo(gain);
        outcome.eventType = SurfaceEventType::UnexpectedDeposit;
        outcome.eventMessage = std::string(text::status::surfaceUnexpectedDeposit);
        outcome.materialDelta.common += gain.common;
        outcome.materialDelta.rare += gain.rare;
        outcome.materialDelta.exotic += gain.exotic;
        outcome.cargoDelta += materialCargo(gain);
        return;
    }

    state.meta.blueprintProgress += tuning::research::surfaceCrewDiscoveryBlueprintGain;
    outcome.eventType = SurfaceEventType::CrewDiscovery;
    outcome.eventMessage = std::string(text::status::surfaceCrewDiscovery);
    outcome.blueprintDelta = tuning::research::surfaceCrewDiscoveryBlueprintGain;
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

void finalizeSurfaceAction(GameState& state, SurfaceActionOutcome& outcome, Random& rng)
{
    applySurfaceEvent(state, outcome, rng);
    appendSurfaceLog(state.run.planetaryExpedition, surfaceActionSummary(outcome));
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

struct FlybyPathSample {
    double distance = 0.0;
    double progress = 0.0;
};

struct FlybyTravelSample {
    double progress = 0.0;
    int bestZone = 0;
    int worstZone = 2;
    bool finished = false;
};

struct FlybyFinishHit {
    bool hit = false;
    double t = 1.0;
};

double cubic(double a, double b, double c, double d, double t)
{
    const double u = 1.0 - t;
    return u * u * u * a + 3.0 * u * u * t * b + 3.0 * u * t * t * c + t * t * t * d;
}

FlybyPathSample nearestFlybyPathSample(double shipX, double shipY)
{
    constexpr int sampleCount = 240;
    double bestDistanceSq = std::numeric_limits<double>::max();
    double bestProgress = 0.0;
    double previousX = tuning::flyby::startX;
    double previousY = tuning::flyby::startY;

    for (int i = 1; i <= sampleCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sampleCount);
        const double currentX = cubic(tuning::flyby::startX, tuning::flyby::control1X, tuning::flyby::control2X, tuning::flyby::endX, t);
        const double currentY = cubic(tuning::flyby::startY, tuning::flyby::control1Y, tuning::flyby::control2Y, tuning::flyby::endY, t);
        const double segmentX = currentX - previousX;
        const double segmentY = currentY - previousY;
        const double segmentLengthSq = segmentX * segmentX + segmentY * segmentY;
        double segmentShare = 0.0;
        if (segmentLengthSq > 0.000001) {
            segmentShare = std::clamp(((shipX - previousX) * segmentX + (shipY - previousY) * segmentY) / segmentLengthSq, 0.0, 1.0);
        }
        const double projectedX = previousX + segmentX * segmentShare;
        const double projectedY = previousY + segmentY * segmentShare;
        const double dx = shipX - projectedX;
        const double dy = shipY - projectedY;
        const double distanceSq = dx * dx + dy * dy;
        if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            bestProgress = (static_cast<double>(i - 1) + segmentShare) / static_cast<double>(sampleCount);
        }
        previousX = currentX;
        previousY = currentY;
    }

    return {std::sqrt(bestDistanceSq), bestProgress};
}

std::pair<double, double> flybyEndTangent()
{
    const double dx = 3.0 * (tuning::flyby::endX - tuning::flyby::control2X);
    const double dy = 3.0 * (tuning::flyby::endY - tuning::flyby::control2Y);
    const double length = std::max(0.001, std::hypot(dx, dy));
    return {dx / length, dy / length};
}

double flybyFinishPlaneValue(double shipX, double shipY)
{
    const auto [tangentX, tangentY] = flybyEndTangent();
    return (shipX - tuning::flyby::endX) * tangentX + (shipY - tuning::flyby::endY) * tangentY;
}

int flybyZoneForDistance(double distance, double perfectBand, double goodBand)
{
    if (distance <= perfectBand) {
        return 2;
    }
    if (distance <= goodBand) {
        return 1;
    }
    return 0;
}

int flybyZoneAt(double shipX, double shipY, double perfectBand, double goodBand)
{
    return flybyZoneForDistance(nearestFlybyPathSample(shipX, shipY).distance, perfectBand, goodBand);
}

void addFlybyTravelSample(FlybyTravelSample& result, const FlybyPathSample& sample, double perfectBand, double goodBand)
{
    const int zone = flybyZoneForDistance(sample.distance, perfectBand, goodBand);
    result.progress = std::max(result.progress, sample.progress);
    result.bestZone = std::max(result.bestZone, zone);
    result.worstZone = std::min(result.worstZone, zone);
}

FlybyFinishHit flybyFinishLineHit(double startX, double startY, double endX, double endY)
{
    const double startValue = flybyFinishPlaneValue(startX, startY);
    const double endValue = flybyFinishPlaneValue(endX, endY);
    if (startValue >= 0.0) {
        return {true, 0.0};
    }
    if (endValue >= 0.0 && endValue > startValue) {
        return {true, std::clamp(startValue / (startValue - endValue), 0.0, 1.0)};
    }
    return {};
}

FlybyTravelSample sampleFlybyTravel(double startX, double startY, double endX, double endY, double perfectBand, double goodBand)
{
    const double distance = std::hypot(endX - startX, endY - startY);
    const int sampleCount = std::clamp(
        static_cast<int>(std::ceil(distance / std::max(0.001, perfectBand * 0.15))),
        2,
        96);

    FlybyTravelSample result;
    addFlybyTravelSample(result, nearestFlybyPathSample(startX, startY), perfectBand, goodBand);
    const FlybyFinishHit finishHit = flybyFinishLineHit(startX, startY, endX, endY);
    if (finishHit.hit && finishHit.t <= 0.0) {
        result.progress = std::max(result.progress, tuning::flyby::finishProgress);
        result.finished = true;
        return result;
    }

    const double sampleEndT = finishHit.hit ? std::clamp(finishHit.t, 0.0, 1.0) : 1.0;

    for (int i = 0; i <= sampleCount; ++i) {
        const double t = std::min(sampleEndT, static_cast<double>(i) / static_cast<double>(sampleCount));
        if (t <= 0.0) {
            continue;
        }
        const double x = startX + (endX - startX) * t;
        const double y = startY + (endY - startY) * t;
        const FlybyPathSample sample = nearestFlybyPathSample(x, y);

        addFlybyTravelSample(result, sample, perfectBand, goodBand);
        if (t >= sampleEndT) {
            break;
        }
    }

    if (finishHit.hit) {
        result.progress = std::max(result.progress, tuning::flyby::finishProgress);
        result.finished = true;
    }
    return result;
}

double flybyGravityForDestination(const Destination& destination)
{
    if (destination.tier <= 2) {
        return tuning::flyby::gravityEasy;
    }
    if (destination.tier == 3) {
        return tuning::flyby::gravityMedium;
    }
    if (destination.tier == 4) {
        return tuning::flyby::gravityLarge;
    }
    return tuning::flyby::gravityDeep;
}

double flybyPlanetColliderRadius(const Destination& destination)
{
    return tuning::flyby::planetColliderBaseRadius
        + static_cast<double>(std::min(4, destination.tier)) * tuning::flyby::planetColliderTierRadius
        + tuning::flyby::planetColliderPadding;
}

double flightControlScale(const ModuleStats& stats, double thrustScale, double secondaryScale, double volatilityPenalty, double secondary)
{
    return std::clamp(
        1.0 +
            std::max(0.0, stats.thrust) * thrustScale +
            std::max(0.0, secondary) * secondaryScale -
            std::max(0.0, stats.volatility) * volatilityPenalty,
        0.90,
        1.22);
}

void applyShipAssistToFlyby(FlybyRunState& flyby, const ModuleStats& stats)
{
    const double sensors = std::max(0.0, stats.sensors);
    flyby.perfectBand = tuning::flyby::perfectBand + sensors * tuning::flyby::sensorPerfectBandScale;
    flyby.goodBand = tuning::flyby::goodBand + sensors * tuning::flyby::sensorGoodBandScale;

    const double controlScale = flightControlScale(
        stats,
        tuning::flyby::thrustControlScale,
        tuning::flyby::escapeControlScale,
        tuning::flyby::volatilityControlPenalty,
        stats.escape);
    flyby.turnRateRadians = tuning::flyby::turnRateRadians * controlScale;
    flyby.thrustAcceleration = tuning::flyby::thrustAcceleration * controlScale;

    const int relief = std::clamp(
        static_cast<int>(std::round(
            std::max(0.0, stats.hull) * tuning::flyby::hullImpactReliefScale +
            std::max(0.0, stats.cooling) * tuning::flyby::coolingImpactReliefScale +
            std::max(0.0, stats.escape) * tuning::flyby::escapeImpactReliefScale)),
        0,
        tuning::flyby::impactMaximumRelief);
    flyby.impactHullDamage = std::max(6, tuning::flyby::impactHullDamage - relief);
}

void applyLaunchUpgradeAssistToOrbit(const GameState& state, OrbitRunState& orbit)
{
    const int fuelTanks = launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks);
    const int flightControls = launchUpgradeRank(state, LaunchUpgradeKind::FlightControls);
    const int cooling = launchUpgradeRank(state, LaunchUpgradeKind::Cooling);
    const int hull = launchUpgradeRank(state, LaunchUpgradeKind::Hull);

    orbit.durationSeconds += static_cast<double>(fuelTanks) * tuning::orbit::fuelDurationAssistPerRank;
    orbit.thrustAcceleration *= 1.0 +
        static_cast<double>(flightControls) * tuning::orbit::flightControlsThrustAssistPerRank +
        static_cast<double>(cooling) * tuning::orbit::coolingThrustAssistPerRank;
    orbit.collisionPadding = std::max(
        tuning::orbit::minimumCollisionPadding,
        tuning::orbit::collisionPadding -
            static_cast<double>(hull) * tuning::orbit::hullCollisionPaddingReliefPerRank);
}

bool flybyShipIntersectsPlanet(const FlybyRunState& flyby)
{
    const double speed = std::hypot(flyby.velocityX, flyby.velocityY);
    const double forwardX = speed > 0.001 ? flyby.velocityX / speed : 1.0;
    const double forwardY = speed > 0.001 ? flyby.velocityY / speed : 0.0;
    const double rightX = forwardY;
    const double rightY = -forwardX;
    const double dx = tuning::flyby::destinationX - flyby.shipX;
    const double dy = tuning::flyby::destinationY - flyby.shipY;
    const double localX = std::clamp(dx * forwardX + dy * forwardY, -tuning::flyby::shipColliderHalfLength, tuning::flyby::shipColliderHalfLength);
    const double localY = std::clamp(dx * rightX + dy * rightY, -tuning::flyby::shipColliderHalfWidth, tuning::flyby::shipColliderHalfWidth);
    const double closestX = flyby.shipX + forwardX * localX + rightX * localY;
    const double closestY = flyby.shipY + forwardY * localX + rightY * localY;
    return std::hypot(tuning::flyby::destinationX - closestX, tuning::flyby::destinationY - closestY) <= flyby.planetColliderRadius;
}

std::pair<double, double> flybyShipNosePoint(double shipX, double shipY, double velocityX, double velocityY)
{
    const double speed = std::hypot(velocityX, velocityY);
    if (speed <= 0.001) {
        return {shipX, shipY};
    }
    return {
        shipX + (velocityX / speed) * tuning::flyby::shipColliderHalfLength,
        shipY + (velocityY / speed) * tuning::flyby::shipColliderHalfLength
    };
}

double flybyBaseReward(const Destination& destination)
{
    return std::max(tuning::flyby::goodRewardFloor, destination.baseReward * tuning::flyby::goodRewardFactor);
}

double flybySpeedScale(const FlybyRunState& flyby)
{
    const double speed = std::hypot(flyby.velocityX, flyby.velocityY);
    const double baselineSpeed = std::hypot(tuning::flyby::startVelocityX, tuning::flyby::startVelocityY);
    const double range = std::max(0.001, tuning::flyby::maxSpeed - baselineSpeed);
    const double fastShare = std::clamp((speed - baselineSpeed) / range, 0.0, 1.0);
    return 1.0 + fastShare * (tuning::flyby::slingshotMaxSpeedScale - 1.0);
}

double flybySpeedBoost(const FlybyRunState& flyby, double maximumBaseBoost)
{
    const double speed = std::hypot(flyby.velocityX, flyby.velocityY);
    const double range = std::max(
        0.001,
        tuning::flyby::maxSpeed - tuning::flyby::minSpeed);
    const double speedShare = std::clamp(
        (speed - tuning::flyby::minSpeed) / range,
        0.0,
        1.0);
    return std::max(0.0, maximumBaseBoost) *
        tuning::flyby::slingshotMaxSpeedScale * speedShare;
}

double flybyExitCourseOffset(const FlybyRunState& flyby)
{
    if (flyby.pathProgress + 0.000001 < tuning::flyby::finishProgress) {
        return 0.0;
    }

    const auto [tangentX, tangentY] = flybyEndTangent();
    // Launch course offset is positive toward the route's right-hand normal.
    // Preserve that same local side at the Flyby finish rather than using
    // absolute screen X, since both authored routes curve independently.
    const double rightX = tangentY;
    const double rightY = -tangentX;
    const double signedDistance =
        (flyby.shipX - tuning::flyby::endX) * rightX +
        (flyby.shipY - tuning::flyby::endY) * rightY;
    const double distance = std::abs(signedDistance);
    const double perfectBand = std::max(0.001, flyby.perfectBand);
    const double goodBand = std::max(perfectBand + 0.001, flyby.goodBand);

    double courseMagnitude = 0.0;
    if (distance <= perfectBand) {
        courseMagnitude = tuning::launch::pilotingCourseSafe *
            distance / perfectBand;
    } else if (distance <= goodBand) {
        const double share = (distance - perfectBand) / (goodBand - perfectBand);
        courseMagnitude = tuning::launch::pilotingCourseSafe +
            (tuning::launch::pilotingCourseCaution -
                tuning::launch::pilotingCourseSafe) * share;
    } else {
        const double share = std::clamp(
            (distance - goodBand) /
                std::max(0.001, tuning::flyby::boundaryPadding),
            0.0,
            1.0);
        courseMagnitude = tuning::launch::pilotingCourseCaution +
            (tuning::launch::pilotingCourseLost -
                tuning::launch::pilotingCourseCaution) * share;
    }
    return std::copysign(
        std::min(courseMagnitude, tuning::launch::pilotingCourseLost),
        signedDistance);
}

double flybyCompletionBonusScale(const FlybyRunState& flyby)
{
    const double completionWindow = std::max(0.01, flyby.durationSeconds - tuning::flyby::minimumFinishSeconds);
    const double remainingWindow = std::clamp(flyby.durationSeconds - flyby.elapsedSeconds, 0.0, completionWindow);
    const double fastShare = std::clamp(remainingWindow / completionWindow, 0.0, 1.0);
    return 1.0 + fastShare * (tuning::flyby::completionRewardMaxScale - 1.0);
}

void populateFlybyRewardPreview(FlybyRunState& flyby, const Destination* destination)
{
    const bool existingSlingshotAwarded = flyby.slingshotAwarded;
    const double existingFuelSavings = flyby.slingshotFuelSavings;
    const double existingSpeedBoost = flyby.slingshotSpeedBoost;
    const double existingSpeedScale = flyby.slingshotSpeedScale;

    flyby.rewardCredits = 0.0;
    flyby.blueprintGain = 0;
    flyby.rewardBonusScale = 1.0;
    flyby.slingshotAwarded = false;
    flyby.slingshotFuelSavings = 0.0;
    flyby.slingshotSpeedBoost = 0.0;
    flyby.slingshotSpeedScale = 1.0;

    if (flyby.result == FlybyGrade::Perfect) {
        flyby.slingshotAwarded = true;
        if (existingSlingshotAwarded) {
            flyby.slingshotSpeedScale = existingSpeedScale;
            flyby.slingshotFuelSavings = existingFuelSavings;
            flyby.slingshotSpeedBoost = existingSpeedBoost;
        } else {
            flyby.slingshotSpeedScale = flybySpeedScale(flyby);
            flyby.slingshotFuelSavings = tuning::flyby::slingshotFuelBoost * flyby.slingshotSpeedScale;
            flyby.slingshotSpeedBoost = flybySpeedBoost(
                flyby,
                tuning::flyby::slingshotSpeedBoost);
        }
    }

    if (flyby.result == FlybyGrade::Miss || flyby.result == FlybyGrade::Active || destination == nullptr) {
        return;
    }

    const double baseReward = flybyBaseReward(*destination);
    flyby.rewardBonusScale = flybyCompletionBonusScale(flyby);
    flyby.rewardCredits = (flyby.result == FlybyGrade::Perfect
        ? baseReward * tuning::flyby::perfectRewardMultiplier
        : baseReward) * flyby.rewardBonusScale;
    flyby.blueprintGain = tuning::flyby::goodBlueprintGain;
}

double orbitPlanetRadius(const Destination& destination)
{
    return tuning::orbit::planetBaseRadius
        + static_cast<double>(std::min(5, std::max(0, destination.tier))) * tuning::orbit::planetTierRadius;
}

double orbitTargetRadius(double planetRadius)
{
    return planetRadius * tuning::orbit::targetRadiusScale;
}

double circularOrbitSpeedAtRadius(const OrbitRunState& orbit, double requestedRadius)
{
    const double radius = std::max(0.001, requestedRadius);
    const double gravityAcceleration = orbit.gravityStrength /
        (radius * radius + tuning::orbit::gravitySoftening);
    return std::clamp(
        std::sqrt(std::max(0.0, gravityAcceleration * radius)),
        tuning::orbit::minSpeed,
        tuning::orbit::maxSpeed);
}

int orbitZoneAt(const OrbitRunState& orbit, double x, double y)
{
    const double distance = std::hypot(x, y);
    if (distance <= orbit.planetRadius + tuning::orbit::collisionPadding) {
        return 0;
    }
    const double radialError = std::abs(distance - orbit.targetRadius);
    if (radialError <= orbit.perfectBand) {
        return 2;
    }
    if (radialError <= orbit.goodBand) {
        return 1;
    }
    return 0;
}

double normalizedAngleDelta(double previous, double current)
{
    double delta = current - previous;
    while (delta > 3.14159265358979323846) {
        delta -= 2.0 * 3.14159265358979323846;
    }
    while (delta < -3.14159265358979323846) {
        delta += 2.0 * 3.14159265358979323846;
    }
    return delta;
}

double orbitBaseReward(const Destination& destination)
{
    return std::max(tuning::orbit::goodRewardFloor, destination.baseReward * tuning::orbit::goodRewardFactor);
}

void populateOrbitRewardPreview(OrbitRunState& orbit, const Destination* destination)
{
    orbit.rewardCredits = 0.0;
    orbit.blueprintGain = 0;
    if (orbit.result == OrbitGrade::Active || orbit.result == OrbitGrade::Miss || destination == nullptr) {
        return;
    }

    const double baseReward = orbitBaseReward(*destination);
    orbit.rewardCredits = orbit.result == OrbitGrade::Perfect
        ? baseReward * tuning::orbit::perfectRewardMultiplier
        : baseReward;
    orbit.blueprintGain = orbit.result == OrbitGrade::Perfect
        ? tuning::orbit::perfectBlueprintGain
        : tuning::orbit::goodBlueprintGain;
    if (destinationSupportsResearch(*destination)) {
        orbit.blueprintGain += 1;
    }
}

void pushFlybyTrailPoint(FlybyRunState& flyby, double x, double y)
{
    if (!flyby.trailPoints.empty()) {
        const FlybyTrailPoint& last = flyby.trailPoints.back();
        if (std::hypot(last.x - x, last.y - y) < 0.012) {
            return;
        }
    }
    flyby.trailPoints.push_back({x, y});
}

void pushOrbitTrailPoint(OrbitRunState& orbit, double x, double y)
{
    constexpr std::size_t maxTrailPoints = 144;
    if (!orbit.trailPoints.empty()) {
        const FlybyTrailPoint& last = orbit.trailPoints.back();
        if (std::hypot(last.x - x, last.y - y) < 0.010) {
            return;
        }
    }
    orbit.trailPoints.push_back({x, y});
    if (orbit.trailPoints.size() > maxTrailPoints) {
        orbit.trailPoints.erase(orbit.trailPoints.begin());
    }
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
    if (binding == nullptr || definition == nullptr || instance == nullptr || state.run.approach.flyby.active ||
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

double flybyCreditRewardMinimum(const Destination& destination, FlybyGrade grade)
{
    if (grade != FlybyGrade::Good && grade != FlybyGrade::Perfect) {
        return 0.0;
    }
    const double multiplier = grade == FlybyGrade::Perfect ? tuning::flyby::perfectRewardMultiplier : 1.0;
    return flybyBaseReward(destination) * multiplier;
}

double flybyCreditRewardMaximum(const Destination& destination, FlybyGrade grade)
{
    return flybyCreditRewardMinimum(destination, grade) * tuning::flyby::completionRewardMaxScale;
}

int flybyResearchDataReward(FlybyGrade grade)
{
    return grade == FlybyGrade::Good || grade == FlybyGrade::Perfect
        ? tuning::flyby::goodBlueprintGain
        : 0;
}

double orbitCreditReward(const Destination& destination, OrbitGrade grade)
{
    if (grade != OrbitGrade::Good && grade != OrbitGrade::Perfect) {
        return 0.0;
    }
    return orbitBaseReward(destination) * (grade == OrbitGrade::Perfect ? tuning::orbit::perfectRewardMultiplier : 1.0);
}

int orbitResearchDataReward(const Destination& destination, OrbitGrade grade)
{
    if (grade != OrbitGrade::Good && grade != OrbitGrade::Perfect) {
        return 0;
    }
    const int base = grade == OrbitGrade::Perfect
        ? tuning::orbit::perfectBlueprintGain
        : tuning::orbit::goodBlueprintGain;
    return base + (destinationSupportsResearch(destination) ? 1 : 0);
}

bool flybyClearsGenericNextRoute(const GameState& state, const ContentCatalog& catalog)
{
    const Destination* next = nextDestination(state, catalog);
    return next != nullptr &&
        !destinationHasAuthoredProgressionArtifact(
            catalog,
            currentDestination(state, catalog).id) &&
        (next->routeRequirementKeys.empty() || scenarioRouteUsesFlightData(state, catalog, *next)) &&
        frontierReadinessCap(state, catalog) > 0;
}

bool bankFlybyRouteClearance(GameState& state, const ContentCatalog& catalog)
{
    if (!flybyClearsGenericNextRoute(state, catalog)) {
        return false;
    }
    const int before = state.run.frontierReadiness;
    state.run.frontierReadiness = frontierReadinessCap(state, catalog);
    const int gained = state.run.frontierReadiness - before;
    if (gained > 0) {
        const Destination& origin = currentDestination(state, catalog);
        const Destination* target = nextDestination(state, catalog);
        recordScenarioEvent(
            state,
            catalog,
            {ScenarioEventKind::FlightDataBanked, {}, {}, origin.id,
             target == nullptr ? std::string {} : target->id, gained, 0});
    }
    return gained > 0;
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

bool captureArrivalOrbit(GameState& state)
{
    if (!state.run.approach.active) {
        return false;
    }
    state.run.approach.rewards.orbitAwarded = true;
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

bool arrivalFlybyIntroduced(const GameState& state, const ContentCatalog& catalog)
{
    return std::any_of(
        catalog.transferAssists.begin(),
        catalog.transferAssists.end(),
        [&](const TransferAssistDefinition& definition) {
            return scenarioHasCompletedStep(
                       state,
                       definition.availabilityScenarioId,
                       definition.availabilityStepId)
                || scenarioStepBriefingAcknowledged(
                    state,
                    definition.availabilityScenarioId,
                    definition.availabilityStepId);
        });
}

} // namespace

bool canRunArrivalFlyby(const GameState& state, const ContentCatalog& catalog)
{
    return currentResearchDestination(state, catalog) != nullptr
        && arrivalFlybyIntroduced(state, catalog)
        && !state.run.approach.rewards.orbitAwarded;
}

bool canEnterArrivalOrbit(const GameState& state, const ContentCatalog& catalog)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr) {
        return false;
    }
    return !state.run.approach.rewards.orbitAwarded;
}

bool requiresArrivalOrbitBeforeLanding(const GameState& state, const ContentCatalog& catalog)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    return destination != nullptr
        && destination->requiresArrivalSurveySequence
        && destinationHistoryValue(state.meta.destinationLandings, catalog, destination->id) == 0;
}

bool canAttemptArrivalLanding(const GameState& state, const ContentCatalog& catalog)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr || !destinationSupportsSurface(*destination)) {
        return false;
    }
    if (requiresArrivalOrbitBeforeLanding(state, catalog) &&
        !state.run.approach.rewards.orbitAwarded) {
        return false;
    }
    return true;
}

bool canDepartCapturedArrivalOrbit(const GameState& state, const ContentCatalog& catalog)
{
    (void)catalog;
    return state.run.approach.rewards.orbitAwarded;
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

bool bankArrivalLandingFlightData(GameState& state, const ContentCatalog& catalog)
{
    if (!canAttemptArrivalLanding(state, catalog)) {
        return false;
    }
    const Destination& origin = currentDestination(state, catalog);
    if (destinationHasAuthoredProgressionArtifact(catalog, origin.id)) {
        // Artifact-bearing route objectives name their key-producing
        // activities explicitly. Repeated landings must not become a grindable
        // substitute for recovering the artifact and completing Orbit.
        return false;
    }
    return bankAuthoredRouteFlightData(state, catalog, origin.id);
}

std::string arrivalOperationBlockReason(const GameState& state, const ContentCatalog& catalog, std::string_view operation)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr) {
        return {};
    }
    if (state.run.approach.rewards.orbitAwarded
        && (operation == "flyby" || operation == "orbit")) {
        return "Orbit captured. Pass Through and a second capture are closed for this visit.";
    }
    if (operation == "flyby" && !arrivalFlybyIntroduced(state, catalog)) {
        return "Flyby is introduced with the Jupiter transfer window.";
    }
    if (requiresArrivalOrbitBeforeLanding(state, catalog)) {
        if (operation == "landing") {
            return "Capture Orbit before the first mapped lunar landing.";
        }
    }
    return {};
}

void clearResearchAndExpeditionState(GameState& state)
{
    state.run.researchProjectIds = {};
    state.run.approach = {};
    state.run.approach.flyby = {};
    state.run.approach.orbit = {};
    state.run.planetaryExpedition = {};
}

namespace {

void preserveArrivalFuelAtDestination(GameState& state, std::string destinationId)
{
    // Fuel preservation is metadata maintenance, not a phase transition.
    // Reconstructing the aggregate here erased the live Flyby/Orbit solver
    // immediately after it was initialized while leaving Screen unchanged.
    ApproachRunState& approach = state.run.approach;
    approach.active = true;
    approach.destinationId = std::move(destinationId);
    approach.transferFuelRemaining = std::clamp(
        approach.transferFuelRemaining,
        0.0,
        std::max(0.0, approach.transferFuelCapacity));
}

} // namespace

void startArrivalOps(GameState& state, const LaunchOutcome& outcome)
{
    ApproachRunState approach;
    approach.active = true;
    approach.destinationId = outcome.destinationId;
    approach.transferFuelRemaining = std::max(0.0, outcome.transferFuelRemaining);
    approach.transferFuelCapacity = std::max(0.0, outcome.transferFuelCapacity);
    approach.incomingRoute = outcome.routeTransit;
    approach.phase = ApproachPhase::Entry;
    state.run.approach = std::move(approach);
}

namespace {

FlybyRunState createFlybyRun(
    const GameState& state,
    const ContentCatalog& catalog,
    const Destination& destination,
    FlybyPurpose purpose)
{
    FlybyRunState flyby;
    flyby.active = true;
    flyby.destinationId = destination.id;
    flyby.purpose = purpose;
    flyby.durationSeconds = tuning::flyby::durationSeconds;
    flyby.shipX = tuning::flyby::startX;
    flyby.shipY = tuning::flyby::startY;
    flyby.velocityX = tuning::flyby::startVelocityX;
    flyby.velocityY = tuning::flyby::startVelocityY;
    flyby.gravityStrength = flybyGravityForDestination(destination);
    flyby.planetColliderRadius = flybyPlanetColliderRadius(destination);
    applyShipAssistToFlyby(flyby, aggregateShipStats(state, catalog));
    const auto [scoreX, scoreY] = flybyShipNosePoint(
        flyby.shipX,
        flyby.shipY,
        flyby.velocityX,
        flyby.velocityY);
    flyby.pathProgress = nearestFlybyPathSample(scoreX, scoreY).progress;
    flyby.currentZone = flybyZoneAt(
        scoreX,
        scoreY,
        flyby.perfectBand,
        flyby.goodBand);
    flyby.worstZone = flyby.currentZone;
    pushFlybyTrailPoint(flyby, flyby.shipX, flyby.shipY);
    return flyby;
}

} // namespace

void completeArrivalFlyby(GameState& state, const ContentCatalog& catalog)
{
    applyFlybyReward(state, catalog, FlybyGrade::Good);
    if (const Destination* destination = currentResearchDestination(state, catalog)) {
        preserveArrivalFuelAtDestination(state, destination->id);
    }
}

void startArrivalFlybyRun(GameState& state, const ContentCatalog& catalog)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr || !canRunArrivalFlyby(state, catalog)) {
        return;
    }

    state.run.approach.flyby = createFlybyRun(
        state,
        catalog,
        *destination,
        FlybyPurpose::Recon);
    state.run.approach.phase = ApproachPhase::Flyby;
    preserveArrivalFuelAtDestination(state, destination->id);
    state.screen = Screen::Flyby;
}

namespace {

bool briefingPrerequisitesCanTransition(
    const ScenarioDefinition& definition,
    const ScenarioInstance& instance,
    const ScenarioStepDefinition& target,
    std::vector<std::string>& visiting)
{
    for (const std::string& prerequisiteId : target.prerequisites) {
        const ScenarioStepDefinition* prerequisite =
            findScenarioStepDefinition(definition, prerequisiteId);
        const ScenarioStepProgress* progress =
            findScenarioStepProgress(instance, prerequisiteId);
        if (prerequisite == nullptr || progress == nullptr) {
            return false;
        }
        if (progress->completed) {
            continue;
        }
        if (std::find(visiting.begin(), visiting.end(), prerequisiteId) != visiting.end() ||
            prerequisite->completionEvent != ScenarioEventKind::None ||
            !prerequisite->mandatoryBriefing ||
            prerequisite->action != ScenarioActionKind::AcknowledgeBriefing) {
            return false;
        }
        visiting.push_back(prerequisiteId);
        const bool canTransition = briefingPrerequisitesCanTransition(
            definition,
            instance,
            *prerequisite,
            visiting);
        visiting.pop_back();
        if (!canTransition) {
            return false;
        }
    }
    return true;
}

bool acknowledgeScenarioBriefingPrerequisites(
    GameState& state,
    const ContentCatalog& catalog,
    std::string_view scenarioId,
    std::string_view stepId)
{
    ensureScenarioInstances(state, catalog);
    ScenarioInstance* instance = findScenarioInstance(state.meta, scenarioId);
    const ScenarioDefinition* definition = instance == nullptr
        ? nullptr
        : scenarioDefinitionForRuntimeId(state, catalog, scenarioId);
    if (instance == nullptr || definition == nullptr) {
        return false;
    }
    const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, *instance);
    const ScenarioStepDefinition* target = findScenarioStepDefinition(resolved, stepId);
    if (target == nullptr) {
        return false;
    }

    const auto acknowledge = [&](auto&& self, const ScenarioStepDefinition& step) -> bool {
        for (const std::string& prerequisiteId : step.prerequisites) {
            const ScenarioStepDefinition* prerequisite =
                findScenarioStepDefinition(resolved, prerequisiteId);
            ScenarioStepProgress* progress =
                findScenarioStepProgress(*instance, prerequisiteId);
            if (prerequisite == nullptr || progress == nullptr) {
                return false;
            }
            if (progress->completed) {
                continue;
            }
            if (!self(self, *prerequisite)) {
                return false;
            }
        }
        ScenarioStepProgress* progress = findScenarioStepProgress(*instance, step.id);
        if (progress == nullptr || progress->completed) {
            return progress != nullptr;
        }
        if (step.completionEvent != ScenarioEventKind::None ||
            !step.mandatoryBriefing ||
            step.action != ScenarioActionKind::AcknowledgeBriefing) {
            return false;
        }
        return performScenarioAction(
            state,
            catalog,
            scenarioId,
            step.id,
            ScenarioActionKind::AcknowledgeBriefing).applied;
    };

    for (const std::string& prerequisiteId : target->prerequisites) {
        const ScenarioStepDefinition* prerequisite =
            findScenarioStepDefinition(resolved, prerequisiteId);
        if (prerequisite == nullptr || !acknowledge(acknowledge, *prerequisite)) {
            return false;
        }
    }
    return true;
}

bool canStartScenarioFlyby(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view scenarioId,
    std::string_view stepId)
{
    const ScenarioDefinition* definition = scenarioDefinitionForRuntimeId(state, catalog, scenarioId);
    const ScenarioInstance* instance = findScenarioInstance(state.meta, scenarioId);
    if (definition == nullptr || instance == nullptr) {
        return false;
    }
    const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, *instance);
    const ScenarioStepDefinition* step = findScenarioStepDefinition(resolved, stepId);
    if (step == nullptr ||
        !hasUnlock(state.meta, resolved.availabilityUnlockKey) ||
        step->completionEvent != ScenarioEventKind::FlybyFinished) {
        return false;
    }
    const Destination& destination = currentDestination(state, catalog);
    std::vector<std::string> visiting {step->id};
    const bool stepIsActive =
        scenarioStepState(state, catalog, scenarioId, stepId) == ScenarioStepState::Active;
    const bool prerequisitesCanTransition =
        !stepIsActive && briefingPrerequisitesCanTransition(
            resolved,
            *instance,
            *step,
            visiting);
    const bool matchingActivityAcknowledgesBriefing =
        step->mandatoryBriefing &&
        (step->action == ScenarioActionKind::BeginActivity ||
         step->action == ScenarioActionKind::RetryActivity);
    const bool departingFinishedSurfaceVisit =
        state.screen == Screen::ArrivalOps && state.run.approach.active;
    return (resolved.destinationId.empty() || resolved.destinationId == destination.id) &&
        (stepIsActive || prerequisitesCanTransition) &&
        (!step->mandatoryBriefing ||
         scenarioStepBriefingAcknowledged(state, scenarioId, stepId) ||
         matchingActivityAcknowledgesBriefing) &&
        !state.run.approach.flyby.active
        && (!state.run.planetaryExpedition.active || departingFinishedSurfaceVisit)
        && !state.run.mining.active;
}

} // namespace

bool canStartSaturnSlingshot(const GameState& state, const ContentCatalog& catalog)
{
    const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(CampaignObjectiveId::SaturnSlingshot);
    return binding != nullptr && canStartScenarioFlyby(
        state,
        catalog,
        binding->scenarioId,
        binding->progressStepId);
}

bool startScenarioFlybyRun(
    GameState& state,
    const ContentCatalog& catalog,
    std::string_view scenarioId,
    std::string_view stepId,
    ScenarioActionKind action)
{
    if (!acknowledgeScenarioBriefingPrerequisites(state, catalog, scenarioId, stepId)) {
        return false;
    }
    if (!canStartScenarioFlyby(state, catalog, scenarioId, stepId)) {
        return false;
    }
    const ScenarioActionOutcome actionOutcome = performScenarioAction(
        state,
        catalog,
        scenarioId,
        stepId,
        action);
    if (!actionOutcome.applied || !actionOutcome.beginsActivity) {
        return false;
    }
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr) {
        return false;
    }
    FlybyRunState flyby = createFlybyRun(state, catalog, *destination, FlybyPurpose::ScenarioChallenge);
    flyby.scenarioId = std::string(scenarioId);
    flyby.scenarioStepId = std::string(stepId);
    // Arrival Ops is the departure boundary for a completed surface visit.
    // The surface record deliberately survives ascent so its settlement can
    // be presented, but it must not keep blocking the authored departure
    // challenge once the player explicitly launches it.
    state.run.planetaryExpedition = {};
    state.run.approach = {};
    state.run.approach.active = true;
    state.run.approach.destinationId = destination->id;
    state.run.approach.phase = ApproachPhase::Flyby;
    state.run.approach.flyby = std::move(flyby);
    state.screen = Screen::Flyby;
    state.statusLine = actionOutcome.message;
    return true;
}

bool startSaturnSlingshotRun(GameState& state, const ContentCatalog& catalog)
{
    const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(CampaignObjectiveId::SaturnSlingshot);
    if (binding == nullptr || !canStartSaturnSlingshot(state, catalog)) {
        state.statusLine = "Complete the active transfer objective before beginning its challenge.";
        return false;
    }
    if (!startScenarioFlybyRun(state, catalog, binding->scenarioId, binding->progressStepId)) {
        return false;
    }
    writeLegacyCampaignSaveProjection(state, catalog);
    return true;
}

bool jupiterWindowReviewed(const GameState& state, const ContentCatalog&)
{
    return scenarioHasCompletedStep(state, content::scenario::marsBayExpansion, "funding") ||
        scenarioStepBriefingAcknowledged(
            state,
            content::scenario::marsBayExpansion,
            "funding");
}

const TransferAssistDefinition* availableTransferAssist(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view definitionId)
{
    const auto available = [&](const TransferAssistDefinition& definition) {
        const Destination* next = nextDestination(state, catalog);
        if (next == nullptr || currentDestination(state, catalog).id != definition.sourceDestinationId ||
            next->id != definition.targetDestinationId) {
            return false;
        }
        if (std::any_of(
                next->routeRequirementKeys.begin(), next->routeRequirementKeys.end(),
                [&](const std::string& routeKey) { return !hasUnlock(state.meta, routeKey); })) {
            return false;
        }
        if (!definition.allowedLaunchStages.empty() &&
            std::find(
                definition.allowedLaunchStages.begin(),
                definition.allowedLaunchStages.end(),
                state.meta.launchLessons.stage) == definition.allowedLaunchStages.end()) {
            return false;
        }
        return scenarioHasCompletedStep(state, definition.availabilityScenarioId, definition.availabilityStepId) ||
            scenarioStepBriefingAcknowledged(state, definition.availabilityScenarioId, definition.availabilityStepId);
    };
    if (!definitionId.empty()) {
        const TransferAssistDefinition* definition = catalog.findTransferAssist(definitionId);
        return definition != nullptr && available(*definition) ? definition : nullptr;
    }
    const auto found = std::find_if(
        catalog.transferAssists.begin(),
        catalog.transferAssists.end(),
        available);
    return found == catalog.transferAssists.end() ? nullptr : &*found;
}

bool canStartTransferAssist(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view definitionId)
{
    return availableTransferAssist(state, catalog, definitionId) != nullptr &&
        !state.run.pendingTransferAssist.active() &&
        !state.run.approach.flyby.active &&
        !state.run.planetaryExpedition.active &&
        !state.run.mining.active;
}

bool startTransferAssistRun(
    GameState& state,
    const ContentCatalog& catalog,
    std::string_view definitionId)
{
    const TransferAssistDefinition* definition = availableTransferAssist(state, catalog, definitionId);
    if (definition == nullptr || !canStartTransferAssist(state, catalog, definitionId)) {
        state.statusLine = "Complete the authored transfer objective before attempting its assist.";
        return false;
    }
    const Destination* source = catalog.findDestination(definition->sourceDestinationId);
    if (source == nullptr) {
        return false;
    }
    FlybyRunState flyby = createFlybyRun(state, catalog, *source, FlybyPurpose::TransferAssist);
    flyby.transferAssistId = definition->id;
    flyby.impactHullDamage = definition->impactHullDamage;
    const Destination* target = catalog.findDestination(definition->targetDestinationId);
    if (target == nullptr) {
        return false;
    }
    state.run.approach = {};
    state.run.approach.active = true;
    state.run.approach.destinationId = source->id;
    state.run.approach.phase = ApproachPhase::Flyby;
    state.run.approach.flyby = std::move(flyby);
    state.screen = Screen::Flyby;
    const std::string minimumGrade = definition->minimumGrade == FlybyGrade::Perfect ? "Perfect" : "Good";
    state.statusLine = source->name + " departure active. Reach " + minimumGrade +
        " for momentum; hold Perfect for a stable " + target->name + " transfer.";
    return true;
}

bool armTransferAssist(GameState& state, const ContentCatalog& catalog)
{
    FlybyRunState& flyby = state.run.approach.flyby;
    const TransferAssistDefinition* definition = catalog.findTransferAssist(flyby.transferAssistId);
    const FlybyGrade grade = flyby.result == FlybyGrade::Active ? flybyGrade(flyby) : flyby.result;
    if (!flyby.active || !flyby.completed || definition == nullptr ||
        static_cast<int>(grade) < static_cast<int>(definition->minimumGrade)) {
        return false;
    }

    populateFlybyRewardPreview(flyby, nullptr);
    flyby.slingshotAwarded = true;
    flyby.slingshotSpeedScale = flybySpeedScale(flyby);
    flyby.slingshotFuelSavings = definition->fuelSavings;
    flyby.slingshotSpeedBoost = flybySpeedBoost(
        flyby,
        definition->speedBoostBase);
    flyby.rewardCredits = 0.0;
    flyby.blueprintGain = 0;
    state.run.pendingTransferAssist = {
        definition->id,
        definition->sourceDestinationId,
        definition->targetDestinationId,
        grade,
        flyby.slingshotFuelSavings,
        flyby.slingshotSpeedBoost,
        grade == FlybyGrade::Good ? definition->goodInstabilityPenalty : 0.0,
        flybyExitCourseOffset(flyby)
    };
    return true;
}

bool transferAssistCanContinue(const GameState& state, const ContentCatalog& catalog)
{
    const PendingTransferAssist* assist = pendingTransferAssistForDestination(
        state,
        nextDestination(state, catalog) == nullptr ? std::string_view{} : nextDestination(state, catalog)->id);
    return assist != nullptr && catalog.findTransferAssist(assist->definitionId) != nullptr;
}

bool canStartJupiterSlingshot(const GameState& state, const ContentCatalog& catalog)
{
    return canStartTransferAssist(state, catalog, content::transferAssist::marsJupiter);
}

bool startJupiterSlingshotRun(GameState& state, const ContentCatalog& catalog)
{
    return startTransferAssistRun(state, catalog, content::transferAssist::marsJupiter);
}

bool armJupiterSlingshot(GameState& state)
{
    return armTransferAssist(state, legacyCampaignCatalog());
}

void setFlybyMove(GameState& state, double xAxis, double yAxis)
{
    if (!state.run.approach.flyby.active || state.run.approach.flyby.completed) {
        return;
    }
    state.run.approach.flyby.inputX = std::clamp(xAxis, -1.0, 1.0);
    state.run.approach.flyby.inputY = std::clamp(yAxis, -1.0, 1.0);
}

FlybyGrade flybyGrade(const FlybyRunState& flyby)
{
    if (!flyby.completed) {
        return FlybyGrade::Active;
    }
    if (flyby.collidedWithBody) {
        return FlybyGrade::Miss;
    }
    if (flyby.worstZone <= 0) {
        return FlybyGrade::Miss;
    }
    if (flyby.pathProgress < tuning::flyby::finishProgress) {
        return FlybyGrade::Miss;
    }
    if (flyby.worstZone >= 2) {
        return FlybyGrade::Perfect;
    }
    if (flyby.worstZone >= 1) {
        return FlybyGrade::Good;
    }
    return FlybyGrade::Miss;
}

void updateFlybyRun(GameState& state, double deltaSeconds)
{
    FlybyRunState& flyby = state.run.approach.flyby;
    if (!flyby.active || flyby.completed) {
        return;
    }

    const auto concludeFlybyImpact = [&]() {
        flyby.collidedWithBody = true;
        flyby.completed = true;
        flyby.result = FlybyGrade::Miss;
        // Freeze the final vector for the result stamp. It is both the visual
        // heading and the achieved-speed record; the completed run no longer
        // advances, so retaining it cannot continue the ship's movement.
        flyby.inputX = 0.0;
        flyby.inputY = 0.0;
        state.run.shipDamage = std::clamp(
            state.run.shipDamage + flyby.impactHullDamage,
            0,
            tuning::damage::destroyedShipDamage);
    };
    if (flybyShipIntersectsPlanet(flyby)) {
        concludeFlybyImpact();
        return;
    }

    const double dt = std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds);
    const double previousX = flyby.shipX;
    const double previousY = flyby.shipY;
    const double previousVelocityX = flyby.velocityX;
    const double previousVelocityY = flyby.velocityY;
    double speed = std::hypot(flyby.velocityX, flyby.velocityY);
    double headingX = speed > 0.001 ? flyby.velocityX / speed : 1.0;
    double headingY = speed > 0.001 ? flyby.velocityY / speed : 0.0;

    const double turnRadians = std::clamp(flyby.inputX, -1.0, 1.0) * flyby.turnRateRadians * dt;
    if (std::abs(turnRadians) > 0.000001) {
        const double c = std::cos(turnRadians);
        const double s = std::sin(turnRadians);
        const double rotatedX = headingX * c - headingY * s;
        const double rotatedY = headingX * s + headingY * c;
        headingX = rotatedX;
        headingY = rotatedY;
    }

    flyby.selectedThrottle = std::clamp(
        flyby.selectedThrottle +
            std::clamp(flyby.inputY, -1.0, 1.0) *
                tuning::flyby::throttleChangePerSecond * dt,
        0.0,
        1.0);
    speed += flyby.selectedThrottle * flyby.thrustAcceleration * dt;
    speed = std::clamp(speed, tuning::flyby::minSpeed, tuning::flyby::maxSpeed);
    flyby.velocityX = headingX * speed;
    flyby.velocityY = headingY * speed;

    const double gravityDx = tuning::flyby::destinationX - flyby.shipX;
    const double gravityDy = tuning::flyby::destinationY - flyby.shipY;
    const double gravityDistance = std::max(0.001, std::hypot(gravityDx, gravityDy));
    const double gravityAcceleration = std::min(
        tuning::flyby::maxGravityAcceleration,
        flyby.gravityStrength / (gravityDistance * gravityDistance + tuning::flyby::gravitySoftening));
    flyby.velocityX += (gravityDx / gravityDistance) * gravityAcceleration * dt;
    flyby.velocityY += (gravityDy / gravityDistance) * gravityAcceleration * dt;

    const double drag = std::max(0.0, 1.0 - tuning::flyby::driftDrag * dt);
    flyby.velocityX *= drag;
    flyby.velocityY *= drag;
    const double postGravitySpeed = std::hypot(flyby.velocityX, flyby.velocityY);
    if (postGravitySpeed > tuning::flyby::maxSpeed) {
        const double limit = tuning::flyby::maxSpeed / postGravitySpeed;
        flyby.velocityX *= limit;
        flyby.velocityY *= limit;
    }

    flyby.shipX += flyby.velocityX * dt;
    flyby.shipY += flyby.velocityY * dt;
    pushFlybyTrailPoint(flyby, flyby.shipX, flyby.shipY);

    const auto [previousScoreX, previousScoreY] = flybyShipNosePoint(previousX, previousY, previousVelocityX, previousVelocityY);
    const auto [currentScoreX, currentScoreY] = flybyShipNosePoint(flyby.shipX, flyby.shipY, flyby.velocityX, flyby.velocityY);
    const FlybyTravelSample travelSample = sampleFlybyTravel(previousScoreX, previousScoreY, currentScoreX, currentScoreY, flyby.perfectBand, flyby.goodBand);
    flyby.pathProgress = std::max(flyby.pathProgress, travelSample.progress);
    flyby.worstZone = std::min(flyby.worstZone, travelSample.worstZone);
    flyby.currentZone = travelSample.worstZone;
    if (flyby.currentZone >= 2) {
        flyby.perfectSeconds += dt;
        flyby.currentMissStreak = 0.0;
    } else if (flyby.currentZone == 1) {
        flyby.goodSeconds += dt;
        flyby.currentMissStreak = 0.0;
    } else {
        flyby.missSeconds += dt;
        flyby.currentMissStreak += dt;
        flyby.longestMissStreak = std::max(flyby.longestMissStreak, flyby.currentMissStreak);
    }

    flyby.elapsedSeconds += dt;
    if (flybyShipIntersectsPlanet(flyby)) {
        concludeFlybyImpact();
        flyby.missSeconds += dt;
        return;
    }

    if (flyby.currentZone <= 0) {
        flyby.completed = true;
        flyby.result = FlybyGrade::Miss;
        flyby.inputX = 0.0;
        flyby.inputY = 0.0;
        return;
    }

    if (travelSample.finished) {
        flyby.elapsedSeconds = std::min(flyby.elapsedSeconds, flyby.durationSeconds);
        flyby.completed = true;
        flyby.result = flybyGrade(flyby);
        populateFlybyRewardPreview(flyby, nullptr);
        flyby.inputX = 0.0;
        flyby.inputY = 0.0;
        return;
    }

    const double maxX = std::max(0.35, 1.0 + tuning::flyby::boundaryPadding);
    const double maxY = std::max(0.35, 1.0 + tuning::flyby::boundaryPadding);
    if (flyby.shipX < -maxX || flyby.shipX > maxX || flyby.shipY < -maxY || flyby.shipY > maxY) {
        flyby.shipX = std::clamp(flyby.shipX, -maxX, maxX);
        flyby.shipY = std::clamp(flyby.shipY, -maxY, maxY);
        flyby.completed = true;
        flyby.result = FlybyGrade::Miss;
        flyby.worstZone = 0;
        flyby.currentZone = 0;
        flyby.inputX = 0.0;
        flyby.inputY = 0.0;
        flyby.missSeconds += dt;
        return;
    }

    if (flyby.elapsedSeconds >= flyby.durationSeconds) {
        flyby.elapsedSeconds = flyby.durationSeconds;
        flyby.completed = true;
        flyby.result = FlybyGrade::Miss;
        flyby.inputX = 0.0;
        flyby.inputY = 0.0;
    }
}

void applyFlybyReward(GameState& state, const ContentCatalog& catalog, FlybyGrade grade)
{
    if (grade == FlybyGrade::Active || grade == FlybyGrade::Miss) {
        return;
    }

    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr && !state.run.approach.flyby.destinationId.empty()) {
        destination = catalog.findDestination(state.run.approach.flyby.destinationId);
    }
    if (destination == nullptr) {
        return;
    }

    addDestinationHistoryValue(state.meta.destinationFlybys, catalog, destination->id);
    populateFlybyRewardPreview(state.run.approach.flyby, destination);
    const int blueprintGain = state.run.approach.flyby.blueprintGain;
    const double reward = state.run.approach.flyby.rewardCredits;
    state.meta.blueprintProgress += blueprintGain;
    state.run.credits += reward;
    if (grade == FlybyGrade::Perfect) {
        state.run.nextLaunchFuelBoost = std::max(state.run.nextLaunchFuelBoost, state.run.approach.flyby.slingshotFuelSavings);
        state.run.nextLaunchSpeedBoost = std::max(state.run.nextLaunchSpeedBoost, state.run.approach.flyby.slingshotSpeedBoost);
    }
    unlockFromBlueprints(state);
}

void completeFlybyRun(GameState& state, const ContentCatalog& catalog)
{
    if (!state.run.approach.flyby.active || !state.run.approach.flyby.completed) {
        return;
    }

    const FlybyGrade grade = state.run.approach.flyby.result == FlybyGrade::Active
        ? flybyGrade(state.run.approach.flyby)
        : state.run.approach.flyby.result;
    if (!state.run.approach.flyby.transferAssistId.empty()) {
        (void)armTransferAssist(state, catalog);
    }
    FlybyRunState flyby = state.run.approach.flyby;
    switch (grade) {
    case FlybyGrade::Miss:
        state.meta.totalFlybyMisses += 1;
        break;
    case FlybyGrade::Good:
        state.meta.totalFlybyGoods += 1;
        break;
    case FlybyGrade::Perfect:
        state.meta.totalFlybyPerfects += 1;
        break;
    case FlybyGrade::Active:
        break;
    }
    const TransferAssistDefinition* transferAssist = catalog.findTransferAssist(flyby.transferAssistId);
    const bool isTransferAssist = transferAssist != nullptr;
    if (!isTransferAssist && !state.run.approach.rewards.flybyAwarded) {
        applyFlybyReward(state, catalog, grade);
        if (grade == FlybyGrade::Good || grade == FlybyGrade::Perfect) {
            state.run.approach.rewards.flybyAwarded = true;
        }
    }
    const bool scenarioChallenge = flyby.purpose == FlybyPurpose::ScenarioChallenge &&
        !flyby.scenarioId.empty() && !flyby.scenarioStepId.empty();
    ScenarioStepDefinition challengeStep;
    bool hasChallengeStep = false;
    if (isTransferAssist) {
        const Destination* source = catalog.findDestination(transferAssist->sourceDestinationId);
        const Destination* target = catalog.findDestination(transferAssist->targetDestinationId);
        const std::string sourceName = source == nullptr ? "Departure" : source->name;
        const std::string targetName = target == nullptr ? "target" : target->name;
        state.run.approach = {};
        state.screen = Screen::Hangar;
        state.statusLine = grade == FlybyGrade::Perfect
            ? sourceName + " slingshot active. Perfect execution keeps the " + targetName + " transfer stable."
            : (grade == FlybyGrade::Good
                  ? sourceName + " slingshot active. The Good pass reaches " + targetName + " with +" +
                      std::to_string(static_cast<int>(transferAssist->goodInstabilityPenalty * 100.0)) + "% flight instability."
                  : sourceName + " slingshot lost. Retry the pass or build permanent tank margin.");
    } else if (scenarioChallenge) {
        const ScenarioDefinition* definition = scenarioDefinitionForRuntimeId(state, catalog, flyby.scenarioId);
        const ScenarioInstance* instance = findScenarioInstance(state.meta, flyby.scenarioId);
        if (definition != nullptr && instance != nullptr) {
            const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, *instance);
            if (const ScenarioStepDefinition* step = findScenarioStepDefinition(resolved, flyby.scenarioStepId)) {
                challengeStep = *step;
                hasChallengeStep = true;
            }
        }
        ScenarioEvent event;
        event.kind = ScenarioEventKind::FlybyFinished;
        event.scenarioId = flyby.scenarioId;
        event.stepId = flyby.scenarioStepId;
        event.originId = flyby.scenarioId;
        event.amount = 1;
        event.grade = static_cast<int>(grade);
        (void)recordScenarioEvent(state, catalog, event);
        writeLegacyCampaignSaveProjection(state, catalog);
    }
    const Destination* destination = catalog.findDestination(flyby.destinationId);
    state.run.approach.flyby = {};
    if (isTransferAssist) {
        // The physical assist already returned to its Hangar continuation.
    } else if (scenarioChallenge) {
        state.run.approach = {};
        state.screen = Screen::Hangar;
        const ScenarioStepState challengeState = scenarioStepState(
            state,
            catalog,
            flyby.scenarioId,
            flyby.scenarioStepId);
        if (challengeState == ScenarioStepState::ReadyToClaim && hasChallengeStep) {
            const Destination* route = scenarioRouteRewardDestination(catalog, challengeStep);
            state.statusLine = route == nullptr
                ? challengeStep.rewardPreview
                : "PERFECT DEPARTURE SECURED // LOCK " + route->name + " COURSE.";
        } else if (hasChallengeStep && !challengeStep.failureExplanation.empty()) {
            state.statusLine = challengeStep.failureExplanation;
        } else {
            state.statusLine = "Scenario challenge complete.";
        }
    } else {
        preserveArrivalFuelAtDestination(
            state,
            destination == nullptr ? flyby.destinationId : destination->id);
        state.screen = Screen::ArrivalOps;
        state.run.approach.phase = ApproachPhase::Entry;
    }
}

void abortFlybyRun(GameState& state)
{
    abortFlybyRun(state, legacyCampaignCatalog());
}

void abortFlybyRun(GameState& state, const ContentCatalog& catalog)
{
    if (!state.run.approach.flyby.active || state.run.approach.flyby.completed) {
        return;
    }

    const FlybyRunState flyby = state.run.approach.flyby;
    state.run.approach.flyby = {};
    const TransferAssistDefinition* transferAssist = catalog.findTransferAssist(flyby.transferAssistId);
    const bool isTransferAssist = transferAssist != nullptr;
    const bool scenarioChallenge = flyby.purpose == FlybyPurpose::ScenarioChallenge &&
        !flyby.scenarioId.empty() && !flyby.scenarioStepId.empty();
    if (isTransferAssist) {
        const Destination* source = catalog.findDestination(transferAssist->sourceDestinationId);
        const Destination* target = catalog.findDestination(transferAssist->targetDestinationId);
        state.run.approach = {};
        state.screen = Screen::Hangar;
        state.statusLine = (source == nullptr ? std::string("Departure") : source->name) +
            " slingshot aborted. " + (target == nullptr ? std::string("Transfer") : target->name) +
            " options remain open.";
    } else if (scenarioChallenge) {
        ScenarioEvent event;
        event.kind = ScenarioEventKind::ActivityAborted;
        event.scenarioId = flyby.scenarioId;
        event.stepId = flyby.scenarioStepId;
        event.originId = flyby.scenarioId;
        (void)recordScenarioEvent(state, catalog, event);
        writeLegacyCampaignSaveProjection(state, catalog);
        state.run.approach = {};
        state.screen = Screen::Hangar;
        const ScenarioDefinition* definition = scenarioDefinitionForRuntimeId(state, catalog, flyby.scenarioId);
        const ScenarioInstance* instance = findScenarioInstance(state.meta, flyby.scenarioId);
        const ScenarioDefinition resolved = definition != nullptr && instance != nullptr
            ? resolveScenarioDefinition(*definition, *instance)
            : ScenarioDefinition {};
        const ScenarioStepDefinition* step = definition == nullptr || instance == nullptr
            ? nullptr
            : findScenarioStepDefinition(resolved, flyby.scenarioStepId);
        state.statusLine = step != nullptr && !step->failureExplanation.empty()
            ? step->failureExplanation
            : "Scenario challenge aborted.";
    } else {
        preserveArrivalFuelAtDestination(state, flyby.destinationId);
        state.screen = Screen::ArrivalOps;
    }
}

void acknowledgeFlybyResult(GameState& state)
{
    if (!state.run.approach.flyby.active || !state.run.approach.flyby.completed) {
        return;
    }
    preserveArrivalFuelAtDestination(state, state.run.approach.flyby.destinationId);
}

bool canClaimSaturnCourse(const GameState& state)
{
    const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(CampaignObjectiveId::SaturnSlingshot);
    if (binding == nullptr) {
        return false;
    }
    const ContentCatalog& catalog = legacyCampaignCatalog();
    if (scenarioStepState(
            state,
            catalog,
            binding->scenarioId,
            binding->progressStepId) == ScenarioStepState::ReadyToClaim) {
        return true;
    }
    const ScenarioInstance* instance = findScenarioInstance(
        state.meta,
        binding->scenarioId);
    const ScenarioStepProgress* progress = instance == nullptr
        ? nullptr
        : findScenarioStepProgress(*instance, binding->progressStepId);
    const Destination* saturn = catalog.findDestination(content::destination::saturn);
    if (progress == nullptr || !progress->claimed || saturn == nullptr ||
        !hasUnlock(state.meta, content::unlock::routeSaturn)) {
        return false;
    }
    const auto destination = std::find_if(
        catalog.destinations.begin(),
        catalog.destinations.end(),
        [&](const Destination& candidate) { return candidate.id == saturn->id; });
    const int saturnIndex = destination == catalog.destinations.end()
        ? -1
        : static_cast<int>(std::distance(catalog.destinations.begin(), destination));
    const bool alreadyQueued = state.run.routeTransit.active() &&
        state.run.routeTransit.intent == RouteTransitIntent::Outbound &&
        state.run.routeTransit.targetDestinationId == saturn->id;
    return !alreadyQueued && saturnIndex > state.run.destinationIndex;
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

bool claimSaturnCourse(GameState& state, const ContentCatalog& catalog)
{
    const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(CampaignObjectiveId::SaturnSlingshot);
    if (binding == nullptr) {
        return false;
    }
    const ScenarioInstance* instance = findScenarioInstance(
        state.meta,
        binding->scenarioId);
    const ScenarioStepProgress* progress = instance == nullptr
        ? nullptr
        : findScenarioStepProgress(*instance, binding->progressStepId);
    if (progress == nullptr || !progress->claimed) {
        const ScenarioActionOutcome outcome = performScenarioAction(
            state,
            catalog,
            binding->scenarioId,
            binding->progressStepId,
            ScenarioActionKind::ClaimReward);
        if (!outcome.applied) {
            state.statusLine = "Complete the active transfer objective before claiming its route.";
            return false;
        }
    }
    if (!commitClaimedScenarioRoute(
            state,
            catalog,
            binding->scenarioId,
            binding->progressStepId)) {
        state.statusLine = "Saturn route is secured, but the frontier could not advance.";
        return false;
    }
    writeLegacyCampaignSaveProjection(state, catalog);
    state.statusLine = "Saturn course locked. Launch when ready.";
    return true;
}

bool acknowledgeSaturnSlingshotFailure(GameState& state)
{
    const LegacyCampaignScenarioBinding* binding = legacyCampaignScenarioBinding(CampaignObjectiveId::SaturnSlingshot);
    if (binding == nullptr) {
        return false;
    }
    const ContentCatalog& catalog = legacyCampaignCatalog();
    const ScenarioActionOutcome outcome = performScenarioAction(
        state,
        catalog,
        binding->scenarioId,
        binding->progressStepId,
        ScenarioActionKind::AcknowledgeFailure);
    if (!outcome.applied) {
        return false;
    }
    writeLegacyCampaignSaveProjection(state, catalog);
    state.statusLine = outcome.message;
    return true;
}

void completeArrivalOrbit(GameState& state, const ContentCatalog& catalog)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr) {
        return;
    }
    addDestinationHistoryValue(state.meta.destinationOrbits, catalog, destination->id);
    state.meta.blueprintProgress += destinationSupportsResearch(*destination) ? 2 : 1;
    state.run.credits += std::max(18.0, destination->baseReward * 0.55);
    unlockFromBlueprints(state);
    preserveArrivalFuelAtDestination(state, destination->id);
}

void startArrivalOrbitRun(GameState& state, const ContentCatalog& catalog)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr || !canEnterArrivalOrbit(state, catalog)) {
        return;
    }

    OrbitRunState orbit;
    orbit.active = true;
    orbit.destinationId = destination->id;
    orbit.durationSeconds = tuning::orbit::durationSeconds;
    orbit.planetRadius = orbitPlanetRadius(*destination);
    orbit.targetRadius = orbitTargetRadius(orbit.planetRadius);
    orbit.goodBand = orbit.planetRadius * tuning::orbit::goodBandScale;
    orbit.perfectBand = orbit.planetRadius * tuning::orbit::perfectBandScale;
    orbit.gravityStrength = orbit.targetRadius * orbit.targetRadius * tuning::orbit::gravityScale;
    orbit.thrustAcceleration = tuning::orbit::thrustAcceleration;
    applyLaunchUpgradeAssistToOrbit(state, orbit);

    const double angle = tuning::orbit::flybyExitAngleRadians();
    // Begin on the authored solution. Perfect now asks the player to preserve
    // the gold orbit instead of correcting an insertion that starts in green.
    const double insertionRadius = orbit.targetRadius;
    orbit.shipX = std::cos(angle) * insertionRadius;
    orbit.shipY = std::sin(angle) * insertionRadius;
    const double insertionSpeed = circularOrbitSpeedAtRadius(orbit, insertionRadius);
    orbit.velocityX = tuning::orbit::direction * -std::sin(angle) * insertionSpeed;
    orbit.velocityY = tuning::orbit::direction * std::cos(angle) * insertionSpeed;
    orbit.angleRadians = std::atan2(orbit.shipY, orbit.shipX);
    orbit.currentZone = orbitZoneAt(orbit, orbit.shipX, orbit.shipY);
    orbit.worstZone = orbit.currentZone;
    pushOrbitTrailPoint(orbit, orbit.shipX, orbit.shipY);

    state.run.approach.orbit = orbit;
    state.run.approach.phase = ApproachPhase::Orbit;
    preserveArrivalFuelAtDestination(state, destination->id);
    state.screen = Screen::Orbit;
}

void setOrbitMove(GameState& state, double xAxis, double yAxis)
{
    if (!state.run.approach.orbit.active || state.run.approach.orbit.completed) {
        return;
    }
    state.run.approach.orbit.inputX = std::clamp(xAxis, -1.0, 1.0);
    state.run.approach.orbit.inputY = std::clamp(yAxis, -1.0, 1.0);
}

OrbitGrade orbitGrade(const OrbitRunState& orbit)
{
    if (!orbit.completed) {
        return OrbitGrade::Active;
    }
    if (orbit.orbitProgress < 1.0) {
        return OrbitGrade::Miss;
    }
    // Perfect is a clean finish: complete the loop in the inner band without
    // ever losing the capture solution. This lets a pilot correct from the
    // authored Good-band insertion late in the loop instead of demanding an
    // arbitrary multi-second hold after the correction.
    if (orbit.currentZone >= 2 && orbit.missSeconds <= 0.001) {
        return OrbitGrade::Perfect;
    }
    const double trackedSeconds = orbit.goodSeconds + orbit.perfectSeconds + orbit.missSeconds;
    const double stableSeconds = orbit.goodSeconds + orbit.perfectSeconds;
    if (trackedSeconds <= 0.001
        || stableSeconds / trackedSeconds >= tuning::orbit::goodBandMinimumTimeShare) {
        return OrbitGrade::Good;
    }
    return OrbitGrade::Miss;
}

void updateOrbitRun(GameState& state, double deltaSeconds)
{
    OrbitRunState& orbit = state.run.approach.orbit;
    if (!orbit.active || orbit.completed) {
        return;
    }

    const double dt = std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds);
    const double previousAngle = orbit.angleRadians;
    const double distance = std::max(0.001, std::hypot(orbit.shipX, orbit.shipY));
    const double radialX = orbit.shipX / distance;
    const double radialY = orbit.shipY / distance;
    const double tangentX = -radialY;
    const double tangentY = radialX;
    // Positive tangential input must remain prograde even though the visible
    // approach now travels clockwise from the Flyby endpoint.
    const double directedTangentX = tangentX * tuning::orbit::direction;
    const double directedTangentY = tangentY * tuning::orbit::direction;
    const double gravityAcceleration = orbit.gravityStrength / (distance * distance + tuning::orbit::gravitySoftening);
    orbit.velocityX += -radialX * gravityAcceleration * dt;
    orbit.velocityY += -radialY * gravityAcceleration * dt;

    const double radialInput = std::clamp(orbit.inputX, -1.0, 1.0);
    orbit.trimApplied = orbit.trimApplied
        || std::abs(orbit.inputX) > 0.05
        || std::abs(orbit.inputY) > 0.05;
    orbit.selectedThrottle = std::clamp(
        orbit.selectedThrottle +
            std::clamp(orbit.inputY, -1.0, 1.0) *
                tuning::orbit::throttleChangePerSecond * dt,
        0.0,
        1.0);
    const double tangentialInput = orbit.selectedThrottle;
    orbit.velocityX += (radialX * radialInput + directedTangentX * tangentialInput) * orbit.thrustAcceleration * dt;
    orbit.velocityY += (radialY * radialInput + directedTangentY * tangentialInput) * orbit.thrustAcceleration * dt;

    const double drag = std::max(0.0, 1.0 - tuning::orbit::driftDrag * dt);
    orbit.velocityX *= drag;
    orbit.velocityY *= drag;

    const double speed = std::hypot(orbit.velocityX, orbit.velocityY);
    if (speed > tuning::orbit::maxSpeed) {
        const double scale = tuning::orbit::maxSpeed / speed;
        orbit.velocityX *= scale;
        orbit.velocityY *= scale;
    } else if (speed < tuning::orbit::minSpeed) {
        const double scale = tuning::orbit::minSpeed / std::max(0.001, speed);
        orbit.velocityX *= scale;
        orbit.velocityY *= scale;
    }

    orbit.shipX += orbit.velocityX * dt;
    orbit.shipY += orbit.velocityY * dt;
    orbit.angleRadians = std::atan2(orbit.shipY, orbit.shipX);
    orbit.orbitProgress += std::abs(normalizedAngleDelta(previousAngle, orbit.angleRadians)) / (2.0 * 3.14159265358979323846);
    pushOrbitTrailPoint(orbit, orbit.shipX, orbit.shipY);

    orbit.currentZone = orbitZoneAt(orbit, orbit.shipX, orbit.shipY);
    orbit.worstZone = std::min(orbit.worstZone, orbit.currentZone);
    if (orbit.currentZone >= 2) {
        orbit.perfectSeconds += dt;
    } else if (orbit.currentZone == 1) {
        orbit.goodSeconds += dt;
    } else {
        orbit.missSeconds += dt;
    }

    orbit.elapsedSeconds += dt;
    const double currentDistance = std::hypot(orbit.shipX, orbit.shipY);
    const double escapeRadius = orbit.targetRadius + orbit.goodBand * tuning::orbit::escapeRadiusScale;
    if (currentDistance <= orbit.planetRadius + orbit.collisionPadding || currentDistance >= escapeRadius) {
        orbit.completed = true;
        orbit.result = OrbitGrade::Miss;
    } else if (orbit.orbitProgress >= 1.0) {
        orbit.completed = true;
        orbit.result = orbitGrade(orbit);
    } else if (orbit.elapsedSeconds >= orbit.durationSeconds) {
        orbit.elapsedSeconds = orbit.durationSeconds;
        orbit.completed = true;
        orbit.result = OrbitGrade::Miss;
    }

    if (orbit.completed) {
        populateOrbitRewardPreview(orbit, nullptr);
        orbit.velocityX = 0.0;
        orbit.velocityY = 0.0;
        orbit.inputX = 0.0;
        orbit.inputY = 0.0;
    }
}

void applyOrbitReward(GameState& state, const ContentCatalog& catalog, OrbitGrade grade)
{
    if (grade == OrbitGrade::Active || grade == OrbitGrade::Miss) {
        return;
    }

    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr && !state.run.approach.orbit.destinationId.empty()) {
        destination = catalog.findDestination(state.run.approach.orbit.destinationId);
    }
    if (destination == nullptr) {
        return;
    }

    addDestinationHistoryValue(state.meta.destinationOrbits, catalog, destination->id);
    populateOrbitRewardPreview(state.run.approach.orbit, destination);
    state.meta.blueprintProgress += state.run.approach.orbit.blueprintGain;
    state.run.credits += state.run.approach.orbit.rewardCredits;
    unlockFromBlueprints(state);
}

void completeOrbitRun(GameState& state, const ContentCatalog& catalog)
{
    if (!state.run.approach.orbit.active || !state.run.approach.orbit.completed) {
        return;
    }

    const OrbitRunState orbit = state.run.approach.orbit;
    const OrbitGrade grade = orbit.result == OrbitGrade::Active ? orbitGrade(orbit) : orbit.result;
    if (!state.run.approach.rewards.orbitAwarded) {
        applyOrbitReward(state, catalog, grade);
        if (grade == OrbitGrade::Good || grade == OrbitGrade::Perfect) {
            state.run.approach.rewards.orbitAwarded = true;
        }
    }
    const Destination* destination = catalog.findDestination(orbit.destinationId);
    if (destination != nullptr &&
        (grade == OrbitGrade::Good || grade == OrbitGrade::Perfect)) {
        (void)bankAuthoredRouteFlightData(state, catalog, destination->id);
    }
    preserveArrivalFuelAtDestination(
        state,
        destination == nullptr ? orbit.destinationId : destination->id);
    state.run.approach.orbit = {};
    state.run.approach.phase = ApproachPhase::Entry;
    state.screen = Screen::ArrivalOps;
}

void abortOrbitRun(GameState& state)
{
    if (!state.run.approach.orbit.active || state.run.approach.orbit.completed) {
        return;
    }

    const std::string destinationId = state.run.approach.orbit.destinationId;
    preserveArrivalFuelAtDestination(state, destinationId);
    state.run.approach.orbit = {};
    state.run.approach.phase = ApproachPhase::Entry;
    state.screen = Screen::ArrivalOps;
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

void clearRunUpgradeOffers(PlanetaryExpeditionState& expedition)
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

void copyRunProgression(
    const PlanetaryExpeditionState& source,
    PlanetaryExpeditionState& destination)
{
    destination.expeditionLevel = std::max(1, source.expeditionLevel);
    destination.expeditionExperience = std::max(0.0, source.expeditionExperience);
    destination.pendingRunUpgradeChoices = std::max(0, source.pendingRunUpgradeChoices);
    destination.runUpgradeOffers = source.runUpgradeOffers;
    destination.runUpgradeOfferCount = std::clamp(
        source.runUpgradeOfferCount,
        0,
        static_cast<int>(destination.runUpgradeOffers.size()));
    destination.runUpgradeOfferPending = source.runUpgradeOfferPending && destination.runUpgradeOfferCount > 0;
    destination.runUpgradeReturnScreen = source.runUpgradeReturnScreen;
    destination.runRigUpgradeRanks = source.runRigUpgradeRanks;
    destination.runDroneRanks = source.runDroneRanks;
    destination.selectedSynergyIds = source.selectedSynergyIds;
    destination.droneModuleAssignments = source.droneModuleAssignments;
}

struct WeightedRunUpgradeCandidate {
    RunUpgradeOffer offer;
    Rarity rarity = Rarity::Common;
};

} // namespace

int runRigUpgradeRank(const GameState& state, std::string_view upgradeId)
{
    const auto found = std::find_if(
        state.run.planetaryExpedition.runRigUpgradeRanks.begin(),
        state.run.planetaryExpedition.runRigUpgradeRanks.end(),
        [&](const RunRigUpgradeRank& record) { return record.upgradeId == upgradeId; });
    return found == state.run.planetaryExpedition.runRigUpgradeRanks.end()
        ? 0
        : std::clamp(found->rank, 0, kMaximumRunUpgradeRank);
}

int expeditionDroneRank(const GameState& state, std::string_view droneId)
{
    const auto found = std::find_if(
        state.run.planetaryExpedition.runDroneRanks.begin(),
        state.run.planetaryExpedition.runDroneRanks.end(),
        [&](const RunDroneRank& record) { return record.droneId == droneId; });
    return found == state.run.planetaryExpedition.runDroneRanks.end()
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

void resetExpeditionProgression(PlanetaryExpeditionState& expedition)
{
    expedition.expeditionLevel = 1;
    expedition.expeditionExperience = 0.0;
    expedition.pendingRunUpgradeChoices = 0;
    clearRunUpgradeOffers(expedition);
    expedition.runUpgradeReturnScreen = Screen::Mining;
    expedition.runRigUpgradeRanks.clear();
    expedition.runDroneRanks.clear();
    expedition.selectedSynergyIds.clear();
    expedition.droneModuleAssignments.clear();
    expedition.droneModuleRuntime.clear();

}

void resetExpeditionProgression(GameState& state)
{
    resetExpeditionProgression(state.run.planetaryExpedition);
}

ExpeditionExperienceAward awardExpeditionExperience(
    GameState& state,
    double amount,
    Screen returnScreen)
{
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    ExpeditionExperienceAward award;
    award.resultingLevel = std::max(1, expedition.expeditionLevel);
    award.resultingExperience = std::max(0.0, expedition.expeditionExperience);
    award.pendingChoices = std::max(0, expedition.pendingRunUpgradeChoices);
    if (!state.run.active || !std::isfinite(amount) || amount <= 0.0) {
        return award;
    }

    const bool hadPendingChoice = expedition.pendingRunUpgradeChoices > 0 || expedition.runUpgradeOfferPending;
    const double applied = std::min(amount, 1.0e12);
    expedition.expeditionLevel = std::max(1, expedition.expeditionLevel);
    expedition.expeditionExperience = std::max(0.0, expedition.expeditionExperience) + applied;
    award.appliedExperience = applied;
    for (int guard = 0; guard < 256; ++guard) {
        const double threshold = expeditionExperienceThreshold(expedition.expeditionLevel);
        if (expedition.expeditionExperience + 0.000001 < threshold) {
            break;
        }
        expedition.expeditionExperience = std::max(0.0, expedition.expeditionExperience - threshold);
        expedition.expeditionLevel += 1;
        expedition.pendingRunUpgradeChoices += 1;
        award.levelsGained += 1;
    }
    if (award.levelsGained > 0 && !hadPendingChoice) {
        expedition.runUpgradeReturnScreen = returnScreen;
    }
    award.resultingLevel = expedition.expeditionLevel;
    award.resultingExperience = expedition.expeditionExperience;
    award.pendingChoices = expedition.pendingRunUpgradeChoices;
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
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    if (expedition.runUpgradeOfferPending) {
        const bool containsLockedCombatOffer = !state.meta.hasEncounteredEnemy && std::any_of(
            expedition.runUpgradeOffers.begin(),
            expedition.runUpgradeOffers.begin() + std::clamp(
                expedition.runUpgradeOfferCount,
                0,
                static_cast<int>(expedition.runUpgradeOffers.size())),
            [&](const RunUpgradeOffer& offer) {
                return runUpgradeRequiresEnemyEncounter(catalog, offer);
            });
        if (!containsLockedCombatOffer) {
            return expedition.runUpgradeOfferCount > 0;
        }
        // Existing saves can hold a card rolled before the discovery gate was
        // introduced. Replace only that draft; the earned pick remains intact.
        clearRunUpgradeOffers(expedition);
    }
    clearRunUpgradeOffers(expedition);
    if (expedition.pendingRunUpgradeChoices <= 0) {
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
                expedition.droneModuleAssignments.begin(),
                expedition.droneModuleAssignments.end(),
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
        if (!containsId(expedition.selectedSynergyIds, synergy.id) &&
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
        expedition.pendingRunUpgradeChoices = std::max(0, expedition.pendingRunUpgradeChoices - 1);
        return false;
    }

    const int offerCount = std::min(3, static_cast<int>(candidates.size()));
    for (int slot = 0; slot < offerCount; ++slot) {
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
        expedition.runUpgradeOffers[static_cast<std::size_t>(slot)] = candidates[picked].offer;
        candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(picked));
    }
    expedition.runUpgradeOfferCount = offerCount;
    expedition.runUpgradeOfferPending = offerCount > 0;
    return expedition.runUpgradeOfferPending;
}

bool chooseRunUpgrade(GameState& state, const ContentCatalog& catalog, int index)
{
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    if (!expedition.runUpgradeOfferPending || expedition.pendingRunUpgradeChoices <= 0 ||
        index < 0 || index >= expedition.runUpgradeOfferCount ||
        index >= static_cast<int>(expedition.runUpgradeOffers.size())) {
        return false;
    }
    const RunUpgradeOffer offer = expedition.runUpgradeOffers[static_cast<std::size_t>(index)];
    std::string installedName;
    switch (offer.kind) {
    case RunUpgradeKind::Rig: {
        const SurfaceUpgrade* upgrade = catalog.findSurfaceUpgrade(offer.definitionId);
        const int expectedRank = runRigUpgradeRank(state, offer.definitionId) + 1;
        if (upgrade == nullptr || offer.targetRank != expectedRank || expectedRank > upgrade->maxRank) {
            return false;
        }
        auto found = std::find_if(
            expedition.runRigUpgradeRanks.begin(),
            expedition.runRigUpgradeRanks.end(),
            [&](const RunRigUpgradeRank& record) { return record.upgradeId == offer.definitionId; });
        if (found == expedition.runRigUpgradeRanks.end()) {
            expedition.runRigUpgradeRanks.push_back({offer.definitionId, expectedRank});
        } else {
            found->rank = expectedRank;
        }
        installedName = upgrade->name + " " + runUpgradeRankLabel(expectedRank);
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
            expedition.runDroneRanks.begin(),
            expedition.runDroneRanks.end(),
            [&](const RunDroneRank& record) { return record.droneId == offer.definitionId; });
        if (found == expedition.runDroneRanks.end()) {
            expedition.runDroneRanks.push_back({offer.definitionId, expectedRank});
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
            expedition.droneModuleAssignments.begin(),
            expedition.droneModuleAssignments.end(),
            [&](const DroneFrameModuleAssignment& assignment) {
                return assignment.equippedFrame == offer.slotIndex;
            });
        const std::string& droneId = state.meta.equippedDroneIds[static_cast<std::size_t>(offer.slotIndex)];
        const MiniDrone* drone = catalog.findMiniDrone(droneId);
        if (occupied || drone == nullptr || drone->role != module->hostRole) {
            return false;
        }
        expedition.droneModuleAssignments.push_back({offer.slotIndex, droneId, module->kind});
        installedName = module->name + " on slot " + std::to_string(offer.slotIndex + 1);
        break;
    }
    case RunUpgradeKind::Synergy: {
        const DroneSynergyDefinition* synergy = catalog.findDroneSynergy(offer.definitionId);
        if (synergy == nullptr || containsId(expedition.selectedSynergyIds, synergy->id) ||
            !synergyRequirementsMet(state, catalog, *synergy)) {
            return false;
        }
        expedition.selectedSynergyIds.push_back(synergy->id);
        installedName = synergy->name;
        break;
    }
    }

    expedition.pendingRunUpgradeChoices = std::max(0, expedition.pendingRunUpgradeChoices - 1);
    clearRunUpgradeOffers(expedition);
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
        effects.drillPower += upgrade.stats.drillPower * scale;
        effects.drillCooling += upgrade.stats.drillCooling * scale;
        effects.drillDurability += upgrade.stats.drillDurability * scale;
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
    for (const std::string& synergyId : state.run.planetaryExpedition.selectedSynergyIds) {
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
        copyRunProgression(state.run.planetaryExpedition, preserved);
        state.run.planetaryExpedition = std::move(preserved);
        return;
    }

    const PlanetaryExpeditionState previousExpedition = state.run.planetaryExpedition;
    PlanetaryExpeditionState expedition;
    copyRunProgression(previousExpedition, expedition);
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

SurfaceActionOutcome surveySurfaceSite(GameState& state, Random& rng)
{
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    SurfaceActionOutcome outcome;
    if (expedition.miningRunUsed) {
        outcome.message = "Mining run is complete. Extract before surveying again.";
        return outcome;
    }
    outcome = spendSupply(expedition, tuning::research::surveySupplyCost);
    if (!outcome.applied) {
        return outcome;
    }

    const SurfaceToolEffects tools = surfaceToolEffects(state.meta);
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    const SurfaceUpgradeEffects upgrades = surfaceUpgradeEffects(state, createDefaultContent());
    const bool thermalSurface = surfaceUsesThermalOnlyRegolith(state);
    const MaterialInventory gain {
        .common = thermalSurface
            ? 0
            : tuning::research::surveyCommonGain + tools.surveyCommonBonus + crew.surveyCommonBonus + site.surveyCommonBonus
    };
    addMaterials(expedition.temporaryMaterials, gain);
    awardExpeditionExperience(state, miningMaterialExperience(gain), Screen::Mining);
    expedition.miningSitePrepared = true;
    expedition.cargo += materialCargo(gain);
    outcome.materialDelta = gain;
    outcome.cargoDelta = materialCargo(gain);
    applySurfaceHazard(
        expedition,
        outcome,
        rng,
        tuning::research::surveyHazardChanceScale,
        (tools.surveyCommonBonus > 0 ? tuning::research::probeHazardRelief : 0.0) + crew.hazardRelief + upgrades.hazardRelief,
        text::status::surfaceDustHazard,
        tuning::research::dustHazardSupplyLoss,
        0,
        tuning::research::dustHazardIncrease);
    outcome.message = thermalSurface
        ? "Survey complete. The regolith is inert; ore signatures remain sealed inside thermal seams."
        : std::string(text::status::surfaceSurveyed);
    finalizeSurfaceAction(state, outcome, rng);
    return outcome;
}

SurfaceActionOutcome mineSurfaceDeposit(GameState& state, Random& rng)
{
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    SurfaceActionOutcome outcome = spendSupply(expedition, tuning::research::mineSupplyCost);
    if (!outcome.applied) {
        return outcome;
    }

    const SurfaceToolEffects tools = surfaceToolEffects(state.meta);
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    const SurfaceUpgradeEffects upgrades = surfaceUpgradeEffects(state, createDefaultContent());
    const bool thermalSurface = surfaceUsesThermalOnlyRegolith(state);
    MaterialInventory gain {
        .common = thermalSurface
            ? 0
            : tuning::research::mineCommonGain + tools.mineCommonBonus + crew.mineCommonBonus + site.mineCommonBonus
    };
    if (!thermalSurface
        && (expedition.depth >= tuning::research::mineRareDepthThreshold
            || rng.chance(std::min(1.0, expedition.hazard + tools.mineRareChanceBonus + crew.mineRareChanceBonus + site.mineRareChanceBonus + upgrades.oreYieldChance)))) {
        gain.rare += 1;
    }

    addMaterials(expedition.temporaryMaterials, gain);
    awardExpeditionExperience(state, miningMaterialExperience(gain), Screen::Mining);
    expedition.cargo += materialCargo(gain);
    outcome.materialDelta = gain;
    outcome.cargoDelta = materialCargo(gain);
    applySurfaceHazard(
        expedition,
        outcome,
        rng,
        tuning::research::mineHazardChanceScale,
        (tools.mineCommonBonus > 0 ? tuning::research::drillHazardRelief : 0.0) + crew.hazardRelief + upgrades.hazardRelief,
        text::status::surfaceDrillHazard,
        0,
        tuning::research::drillHazardCargoLoss,
        tuning::research::drillHazardIncrease);
    outcome.message = thermalSurface
        ? "Regolith broken. No resource recovered; treat a thermal seam with Hazard support."
        : std::string(text::status::surfaceMined);
    finalizeSurfaceAction(state, outcome, rng);
    return outcome;
}

SurfaceActionOutcome pushSurfaceDeeper(GameState& state, Random& rng)
{
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    SurfaceActionOutcome outcome;
    if (expedition.miningRunUsed) {
        outcome.message = "Mining run is complete. Extract before pushing deeper.";
        return outcome;
    }
    outcome = spendSupply(expedition, tuning::research::pushSupplyCost);
    if (!outcome.applied) {
        return outcome;
    }

    expedition.miningSitePrepared = true;
    expedition.depth += 1;
    awardExpeditionExperience(state, 2.0, Screen::Mining);
    expedition.hazard += tuning::research::hazardPerDepth;
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    const SurfaceUpgradeEffects upgrades = surfaceUpgradeEffects(state, createDefaultContent());
    if (!surfaceUsesThermalOnlyRegolith(state)
        && expedition.depth >= tuning::research::artifactDepthThreshold
        && expedition.temporaryArtifacts.empty()
        && rng.chance(std::min(1.0, tuning::research::artifactChanceBase + crew.artifactChanceBonus + site.artifactChanceBonus))) {
        expedition.temporaryArtifacts.push_back({artifactId(expedition), expedition.destinationId, false});
        outcome.artifactFound = true;
        outcome.cargoDelta += 3;
        expedition.cargo += 3;
    }
    applySurfaceHazard(
        expedition,
        outcome,
        rng,
        tuning::research::pushHazardChanceScale,
        surfaceToolEffects(state.meta).hazardRelief + crew.hazardRelief + upgrades.hazardRelief,
        text::status::surfaceTerrainHazard,
        tuning::research::pushHazardSupplyLoss,
        0,
        tuning::research::unstableTerrainHazardIncrease);
    outcome.message = std::string(text::status::surfacePushed);
    finalizeSurfaceAction(state, outcome, rng);
    return outcome;
}

bool hasPendingSurfacePayload(const MaterialInventory& materials, const std::vector<ArtifactRecord>& artifacts, int cargo)
{
    return cargo > 0 || materials.common > 0 || materials.rare > 0 || materials.exotic > 0 || !artifacts.empty();
}

struct SurfaceScanSupport {
    double signalBonus = 0.0;
    double riskRelief = 0.0;
    double rareChanceBonus = 0.0;
    double exoticChanceBonus = 0.0;
    double artifactChanceBonus = 0.0;
    double hazardRelief = 0.0;
};

SurfaceScanSupport surfaceScanSupport(const GameState& state)
{
    const ContentCatalog catalog = createDefaultContent();
    const SurfaceUpgradeEffects upgrades = surfaceUpgradeEffects(state, catalog);
    const MiniDroneLoadoutEffects drones = miniDroneLoadoutEffects(state, catalog);
    const double scannerReach = std::max(0.0, upgrades.scannerRadius + drones.scannerRadius);

    SurfaceScanSupport support;
    support.signalBonus = std::clamp(scannerReach * 0.025 + upgrades.oreYieldChance * 0.25, 0.0, 0.16);
    support.riskRelief = std::clamp(upgrades.hazardRelief + scannerReach * 0.015, 0.0, 0.18);
    support.rareChanceBonus = std::clamp(upgrades.oreYieldChance + scannerReach * 0.012, 0.0, 0.24);
    support.exoticChanceBonus = std::clamp(upgrades.oreYieldChance * 0.35 + scannerReach * 0.006, 0.0, 0.10);
    support.artifactChanceBonus = std::clamp(scannerReach * 0.010, 0.0, 0.09);
    support.hazardRelief = std::clamp(upgrades.hazardRelief, 0.0, 0.06);
    return support;
}

struct SurfacePushSupport {
    double pressureRelief = 0.0;
    double collapseRelief = 0.0;
    double richChanceBonus = 0.0;
    double artifactChanceBonus = 0.0;
    double hazardRelief = 0.0;
};

SurfacePushSupport surfacePushSupport(const GameState& state)
{
    const ContentCatalog catalog = createDefaultContent();
    const SurfaceUpgradeEffects upgrades = surfaceUpgradeEffects(state, catalog);
    const MiniDroneLoadoutEffects drones = miniDroneLoadoutEffects(state, catalog);
    const double structureSupport =
        upgrades.drillDurability * 0.025 +
        upgrades.drillCooling * 0.010 +
        upgrades.hardRockBounceRelief * 0.30 +
        upgrades.droneSpeed * 0.045 +
        drones.drillIntegrityRelief * 0.22 +
        drones.hardRockBounceRelief * 0.24;

    SurfacePushSupport support;
    support.pressureRelief = std::clamp(structureSupport + upgrades.hazardRelief * 0.50, 0.0, 0.20);
    support.collapseRelief = std::clamp(structureSupport + upgrades.hazardRelief, 0.0, 0.24);
    support.richChanceBonus = std::clamp(upgrades.oreYieldChance + upgrades.hardRockBounceRelief * 0.15 + drones.hardRockBounceRelief * 0.10, 0.0, 0.22);
    support.artifactChanceBonus = std::clamp(upgrades.hardRockBounceRelief * 0.10 + drones.drillIntegrityRelief * 0.12, 0.0, 0.08);
    support.hazardRelief = std::clamp(upgrades.hazardRelief + structureSupport * 0.20, 0.0, 0.08);
    return support;
}

MiningCellMaterial rollSurfacePushRichMarker(
    const PlanetaryExpeditionState& expedition,
    int step,
    const SurfacePushSupport& support,
    Random& rng)
{
    const ContentCatalog catalog = createDefaultContent();
    const Destination* destination = catalog.findDestination(expedition.destinationId);
    const double bodyRichness = destination == nullptr
        ? 0.0
        : std::clamp(static_cast<double>(destination->tier) * 0.045 + destination->hazard * 0.025, 0.0, 0.22);
    const double depthRichness = std::clamp(static_cast<double>(std::max(0, expedition.depth + step)) * 0.055, 0.0, 0.30);
    const double exoticChance = std::clamp(0.03 + bodyRichness * 0.34 + depthRichness * 0.22 + support.richChanceBonus * 0.20, 0.0, 0.26);

    if (step >= 3 && rng.chance(exoticChance)) {
        return MiningCellMaterial::ExoticVein;
    }
    return MiningCellMaterial::RareOre;
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

SurfaceDepthProspect rollSurfaceDepthProspect(
    const GameState& state,
    int depthOffset,
    double signal,
    const SurfaceScanSupport& support,
    Random& rng)
{
    const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    SurfaceDepthProspect prospect;
    prospect.depthOffset = std::max(0, depthOffset);
    prospect.absoluteDepth = std::max(0, expedition.depth + prospect.depthOffset);
    const bool thermalSurface = surfaceUsesThermalOnlyRegolith(state);
    const bool authoredArtifactSignal =
        surfaceHasAuthoredArtifactSignalAtDepth(state, prospect.depthOffset);

    prospect.possibleMaterials.common = prospect.depthOffset == 0 || rng.chance(0.62 + signal * 0.18) ? 1 : 0;
    if (prospect.depthOffset > 0 || rng.chance(0.12 + signal * 0.30 + site.mineRareChanceBonus + support.rareChanceBonus)) {
        prospect.possibleMaterials.rare += 1;
    }
    if (prospect.depthOffset >= 2 && rng.chance(0.12 + signal * 0.16 + support.exoticChanceBonus + static_cast<double>(prospect.depthOffset) * 0.035)) {
        prospect.possibleMaterials.exotic += 1;
    }
    if (prospect.depthOffset >= 3 && rng.chance(0.08 + signal * 0.10 + support.exoticChanceBonus)) {
        prospect.possibleMaterials.exotic += 1;
    }
    if (authoredArtifactSignal) {
        prospect.possibleArtifacts = 1;
    } else if (!thermalSurface
        && !unresolvedProgressionArtifactOpportunity(
            state,
            legacyCampaignCatalog(),
            expedition.destinationId).has_value()
        && prospect.depthOffset >= 2
        && rng.chance(std::min(0.70, 0.05 + signal * 0.18 + crew.artifactChanceBonus + site.artifactChanceBonus + support.artifactChanceBonus))) {
        prospect.possibleArtifacts = 1;
    }
    if (materialCargo(prospect.possibleMaterials) == 0 && prospect.possibleArtifacts == 0) {
        prospect.possibleMaterials.common = 1;
    }
    return prospect;
}

MaterialInventory actualizePushMaterials(
    const PlanetaryExpeditionState& expedition,
    int step,
    const SurfaceDepthProspect* forecast,
    const SurfacePushSupport& support,
    Random& rng)
{
    MaterialInventory gain;
    gain.common = step == 1 ? 1 : 0;
    gain.rare = 1;
    if (forecast != nullptr) {
        gain.common = std::max(gain.common, forecast->possibleMaterials.common > 0 && rng.chance(0.78) ? 1 : 0);
        gain.rare = std::max(gain.rare, forecast->possibleMaterials.rare);
        if (forecast->possibleMaterials.exotic > 0) {
            if (rng.chance(0.58 + support.richChanceBonus)) {
                gain.exotic += 1;
            } else {
                gain.rare += 1;
            }
        }
        return gain;
    }

    if (step >= 3 && rng.chance(0.35 + support.richChanceBonus)) {
        const MiningCellMaterial richMarker = rollSurfacePushRichMarker(expedition, step, support, rng);
        if (richMarker == MiningCellMaterial::ExoticVein) {
            gain.exotic += 1;
        } else {
            gain.rare += 1;
        }
    }
    return gain;
}

std::vector<MiningCellMaterial> depthProspectMarkers(const SurfaceDepthProspect& prospect)
{
    std::vector<MiningCellMaterial> markers;
    for (int i = 0; i < std::max(0, prospect.possibleMaterials.common); ++i) {
        markers.push_back(MiningCellMaterial::CommonOre);
    }
    for (int i = 0; i < std::max(0, prospect.possibleMaterials.rare); ++i) {
        markers.push_back(MiningCellMaterial::RareOre);
    }
    for (int i = 0; i < std::max(0, prospect.possibleMaterials.exotic); ++i) {
        markers.push_back(MiningCellMaterial::ExoticVein);
    }
    for (int i = 0; i < std::max(0, prospect.possibleArtifacts); ++i) {
        markers.push_back(MiningCellMaterial::ArtifactCache);
    }
    return markers;
}

void addSurfaceScanMarker(SurfaceDepthProspect& prospect, MiningCellMaterial marker)
{
    switch (marker) {
    case MiningCellMaterial::CommonOre:
        prospect.possibleMaterials.common += 1;
        break;
    case MiningCellMaterial::RareOre:
        prospect.possibleMaterials.rare += 1;
        break;
    case MiningCellMaterial::ExoticVein:
        prospect.possibleMaterials.exotic += 1;
        break;
    case MiningCellMaterial::ArtifactCache:
        prospect.possibleArtifacts += 1;
        break;
    default:
        break;
    }
}

SurfaceScanPulseGrade surfaceScanPulseGrade(const SurfaceScanRunState& scan)
{
    constexpr double twoPi = 6.28318530717958647692;
    const int depthOffset = static_cast<int>(scan.depthProspects.size());
    const double sweep = tuning::research::surfaceScanSweepAngleRadians(scan.elapsedSeconds);
    const double difference = std::abs(std::remainder(
        sweep - tuning::research::scanWindowCenterRadians,
        twoPi));
    if (difference <= tuning::research::surfaceScanPerfectWindowHalfAngleForDepth(depthOffset)) {
        return SurfaceScanPulseGrade::Perfect;
    }
    if (difference <= tuning::research::surfaceScanGoodWindowHalfAngleForDepth(depthOffset)) {
        return SurfaceScanPulseGrade::Good;
    }
    return SurfaceScanPulseGrade::Miss;
}

SurfaceDepthProspect partialSurfaceDepthProspect(
    const SurfaceDepthProspect& complete,
    Random& rng,
    bool preserveAuthoredArtifactSignal)
{
    SurfaceDepthProspect partial = complete;
    partial.possibleMaterials = {};
    partial.possibleArtifacts = 0;
    partial.informationPercent = tuning::research::scanGoodInformationPercent;

    const std::vector<MiningCellMaterial> markers = depthProspectMarkers(complete);
    bool revealedMarker = false;
    for (const MiningCellMaterial marker : markers) {
        const bool revealGuaranteedCurrentLayerMarker =
            partial.depthOffset == 0 && marker == MiningCellMaterial::CommonOre;
        const bool revealGuaranteedObjectiveMarker =
            preserveAuthoredArtifactSignal &&
            marker == MiningCellMaterial::ArtifactCache;
        if (revealGuaranteedCurrentLayerMarker ||
            revealGuaranteedObjectiveMarker ||
            rng.chance(static_cast<double>(tuning::research::scanGoodInformationPercent) / 100.0)) {
            addSurfaceScanMarker(partial, marker);
            revealedMarker = true;
        }
    }
    if (!revealedMarker && !markers.empty()) {
        addSurfaceScanMarker(partial, markers.front());
    }
    return partial;
}

void appendSurfacePushMarkers(SurfacePushRunState& push, const MaterialInventory& gain, bool artifactFound, int depthOffset)
{
    for (int i = 0; i < std::max(0, gain.common); ++i) {
        push.rewardMarkers.push_back(MiningCellMaterial::CommonOre);
        push.rewardMarkerDepthOffsets.push_back(depthOffset);
    }
    for (int i = 0; i < std::max(0, gain.rare); ++i) {
        push.rewardMarkers.push_back(MiningCellMaterial::RareOre);
        push.rewardMarkerDepthOffsets.push_back(depthOffset);
    }
    for (int i = 0; i < std::max(0, gain.exotic); ++i) {
        push.rewardMarkers.push_back(MiningCellMaterial::ExoticVein);
        push.rewardMarkerDepthOffsets.push_back(depthOffset);
    }
    if (artifactFound) {
        push.rewardMarkers.push_back(MiningCellMaterial::ArtifactCache);
        push.rewardMarkerDepthOffsets.push_back(depthOffset);
    }
}

void resetSurfaceScan(GameState& state)
{
    state.run.surfaceScan = {};
}

void resetSurfacePush(GameState& state)
{
    state.run.surfacePush = {};
}

SurfaceActionOutcome startSurfaceScanRun(GameState& state, Random&)
{
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    SurfaceActionOutcome outcome;
    if (expedition.miningRunUsed) {
        outcome.message = "Mining run is complete. Extract before surveying again.";
        return outcome;
    }
    if (surfaceSurveyLimitReached(state)) {
        const int surveyRating = surfaceDepthRating(
            state,
            SurfaceDepthUpgradeKind::SurveyArray);
        const int boreRating = surfaceDepthRating(
            state,
            SurfaceDepthUpgradeKind::BoreSystem);
        outcome.message = boreRating < surveyRating
            ? "Bore System limit +" + std::to_string(boreRating) +
                " reached. Install the next Bore System upgrade to survey and dig deeper layers."
            : "Survey Array limit +" + std::to_string(surveyRating) +
                " reached. Install the next Survey Array upgrade to map deeper layers.";
        return outcome;
    }
    outcome = spendSupply(expedition, tuning::research::surveySupplyCost);
    if (!outcome.applied) {
        outcome.message = "Need an action kit to run a surface scan.";
        return outcome;
    }

    SurfaceScanRunState scan;
    const SurfaceScanSupport support = surfaceScanSupport(state);
    scan.active = true;
    scan.destinationId = expedition.destinationId;
    scan.maxPulses = std::max(
        1,
        surfaceSurveyDepthLimit(state) - state.run.planetaryExpedition.depth + 1);
    scan.elapsedSeconds = tuning::research::scanWindowCenterRadians /
        tuning::research::scanSweepRadiansPerSecond;
    scan.signal = std::clamp(0.12 + support.signalBonus, 0.0, 0.42);
    scan.interference = std::clamp(expedition.hazard * 0.25 - support.riskRelief * 0.35, 0.0, 0.30);
    scan.bustRisk = std::clamp(
        tuning::research::scanBaseBustRisk + expedition.hazard * tuning::research::scanBustRiskHazardScale - support.riskRelief,
        0.02,
        0.38);
    scan.message = "Time the sweep: gold maps all data; green maps 80%.";
    state.run.surfaceScan = scan;
    state.screen = Screen::SurfaceScan;
    outcome.message = "Scanner lattice armed. Survey levels before deciding where to Dig.";
    return outcome;
}

SurfaceActionOutcome pulseSurfaceScan(GameState& state, Random& rng)
{
    SurfaceScanRunState& scan = state.run.surfaceScan;
    SurfaceActionOutcome outcome;
    if (scan.completed) {
        outcome.message = "Survey scan limit reached. Log the survey to continue.";
        scan.message = outcome.message;
        return outcome;
    }
    if (!scan.active) {
        outcome.message = "Surface scan is not active.";
        return outcome;
    }

    outcome.applied = true;
    scan.pulses += 1;
    scan.lastPulseGrade = surfaceScanPulseGrade(scan);
    scan.lastPulseDepthOffset = static_cast<int>(scan.depthProspects.size());
    const SurfaceScanSupport support = surfaceScanSupport(state);
    scan.signal = std::clamp(scan.signal + tuning::research::scanSignalPerPulse + support.signalBonus * 0.35 + rng.range(0.02, 0.09), 0.0, 1.0);
    scan.interference = std::clamp(scan.interference + std::max(0.06, 0.12 - support.riskRelief * 0.30) + rng.range(0.00, 0.05), 0.0, 1.0);
    const double scanHazardDelta = std::max(0.0, tuning::research::scanHazardPerPulse - support.hazardRelief * 0.10);
    scan.hazardDelta += scanHazardDelta;
    scan.bustRisk = std::clamp(
        tuning::research::scanBaseBustRisk +
            state.run.planetaryExpedition.hazard * tuning::research::scanBustRiskHazardScale +
            scan.pulses * tuning::research::scanBustRiskPerPulse +
            scan.interference * 0.045 -
            support.riskRelief,
        0.02,
        0.72);

    const int depthOffset = scan.lastPulseDepthOffset;
    if (scan.lastPulseGrade == SurfaceScanPulseGrade::Miss) {
        scan.successFanfareSeconds = 0.0;
        scan.missFanfareSeconds = tuning::research::scanMissFanfareSeconds;
        outcome.message = scan.pulses >= scan.maxPulses
            ? "Sweep missed. Survey scan limit reached; log the survey you have."
            : "Sweep missed. Pulse spent; no data recorded. Try level +" + std::to_string(depthOffset) + " again.";
        scan.message = surfaceActionSummary(outcome);
        if (scan.pulses >= scan.maxPulses) {
            scan.completed = true;
        }
        return outcome;
    }

    scan.missFanfareSeconds = 0.0;
    SurfaceDepthProspect prospect = rollSurfaceDepthProspect(state, depthOffset, scan.signal, support, rng);
    if (scan.lastPulseGrade == SurfaceScanPulseGrade::Good) {
        prospect = partialSurfaceDepthProspect(
            prospect,
            rng,
            surfaceHasAuthoredArtifactSignalAtDepth(state, depthOffset));
    } else {
        prospect.informationPercent = tuning::research::scanPerfectInformationPercent;
    }
    scan.successFanfareSeconds = scan.lastPulseGrade == SurfaceScanPulseGrade::Perfect
        ? tuning::research::scanPerfectSuccessFanfareSeconds
        : tuning::research::scanGoodSuccessFanfareSeconds;
    scan.depthProspects.push_back(prospect);
    if (prospect.possibleArtifacts > 0 && scan.temporaryArtifacts.empty()) {
        scan.temporaryArtifacts.push_back({artifactId(state.run.planetaryExpedition), state.run.planetaryExpedition.destinationId, false});
        outcome.artifactFound = true;
        outcome.cargoDelta += 3;
        scan.cargo += 3;
    }

    addMaterials(scan.temporaryMaterials, prospect.possibleMaterials);
    const int cargoGain = materialCargo(prospect.possibleMaterials);
    scan.cargo += cargoGain;
    outcome.materialDelta = prospect.possibleMaterials;
    outcome.cargoDelta += cargoGain;
    outcome.hazardDelta = scanHazardDelta;
    const std::string quality = scan.lastPulseGrade == SurfaceScanPulseGrade::Perfect
        ? "Perfect pulse: all data mapped"
        : "Good pulse: 80% data mapped";
    outcome.message = scan.pulses >= scan.maxPulses
        ? quality + ". Survey scan limit reached; log the survey."
        : quality + " for level +" + std::to_string(depthOffset) + ".";
    scan.message = surfaceActionSummary(outcome);
    if (scan.pulses >= scan.maxPulses) {
        scan.completed = true;
    }
    return outcome;
}

SurfaceActionOutcome bankSurfaceScan(GameState& state)
{
    SurfaceScanRunState& scan = state.run.surfaceScan;
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    SurfaceActionOutcome outcome;
    if (!scan.completed && !scan.active) {
        outcome.message = "No surface survey is ready to log.";
        return outcome;
    }

    outcome.applied = true;
    if (scan.busted) {
        outcome.message = "Scan window closed. No survey logged.";
    } else {
        for (const SurfaceDepthProspect& prospect : scan.depthProspects) {
            const SurfaceDepthProspect* existing = findSurfaceDepthProspect(expedition, prospect.absoluteDepth);
            const MaterialInventory existingMaterials = existing == nullptr ? MaterialInventory{} : existing->possibleMaterials;
            const int existingArtifacts = existing == nullptr ? 0 : existing->possibleArtifacts;
            if (prospect.absoluteDepth == expedition.depth) {
                addMaterials(expedition.prospectMaterials, materialDeltaAbove(maxMaterials(existingMaterials, prospect.possibleMaterials), existingMaterials));
                expedition.prospectArtifacts += std::max(0, prospect.possibleArtifacts - existingArtifacts);
            }
            mergeSurfaceDepthProspect(expedition, prospect);
        }
        const bool tutorialSurveyCompleted =
            findSurfaceDepthProspect(expedition, expedition.depth + 1) != nullptr;
        expedition.hazard += scan.hazardDelta;
        expedition.miningSitePrepared = true;
        if (tutorialSurveyCompleted) {
            ui::briefings::acknowledge(
                state.meta.acknowledgedActivityBriefingIds,
                ui::briefings::surfaceSurveyComplete);
        }
        outcome.hazardDelta = scan.hazardDelta;
        outcome.message = !scan.depthProspects.empty()
            ? "Survey logged. Level forecasts now show what each Dig can reach."
            : "Survey logged. The crew found a clean mining line, but no strong payload.";
    }
    appendSurfaceLog(expedition, surfaceActionSummary(outcome));
    resetSurfaceScan(state);
    state.screen = Screen::SurfaceExpedition;
    return outcome;
}

SurfaceActionOutcome abortSurfaceScan(GameState& state)
{
    SurfaceActionOutcome outcome;
    if (!state.run.surfaceScan.active && !state.run.surfaceScan.completed) {
        outcome.message = "No surface scan is active.";
        return outcome;
    }
    outcome.applied = true;
    outcome.message = "Surface scan recalled. No survey logged.";
    appendSurfaceLog(state.run.planetaryExpedition, surfaceActionSummary(outcome));
    resetSurfaceScan(state);
    state.screen = Screen::SurfaceExpedition;
    return outcome;
}

SurfaceActionOutcome startSurfacePushRun(GameState& state, Random&)
{
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    SurfaceActionOutcome outcome;
    if (expedition.miningRunUsed) {
        outcome.message = "Mining run is complete. Extract before pushing deeper.";
        return outcome;
    }

    // Repair forecasts stored by builds that suppressed authored objectives
    // under the Thermal random-artifact rule. Only an already-surveyed layer
    // is upgraded, so Dig still cannot reveal an unscanned route for free.
    for (SurfaceDepthProspect& prospect : expedition.depthProspects) {
        if (surfaceHasAuthoredArtifactSignalAtDepth(
                state,
                prospect.depthOffset)) {
            prospect.possibleArtifacts = std::max(
                1,
                prospect.possibleArtifacts);
        }
    }

    const ContentCatalog catalog = createDefaultContent();
    const SurfaceDepthCapability capability = surfaceDepthCapability(
        state,
        catalog,
        expedition.depth + 1);
    if (!capability.canDig) {
        outcome.message = surfaceDepthBlockerMessage(capability);
        return outcome;
    }

    outcome = spendSupply(expedition, tuning::research::pushSupplyCost);
    if (!outcome.applied) {
        outcome.message = "The retired Dig board is unavailable; drill in the physical Mining world.";
        return outcome;
    }

    SurfacePushRunState push;
    const SurfacePushSupport support = surfacePushSupport(state);
    push.active = true;
    push.destinationId = expedition.destinationId;
    push.maxSteps = std::max(1, capability.usableDepth - expedition.depth);
    push.pressure = std::clamp(expedition.hazard * 0.35 - support.pressureRelief, 0.0, 0.45);
    push.collapseRisk = std::clamp(
        tuning::research::pushBaseCollapseRisk + expedition.hazard * tuning::research::pushRiskHazardScale - support.collapseRelief,
        0.04,
        0.42);
    push.message = "Descent lane armed. Every reachable layer is surveyed; collapse risk begins on the second push.";
    state.run.surfacePush = push;
    state.screen = Screen::SurfacePush;
    outcome.message = "Deep route armed. Survey, Bore rating, and return range now bound this tunnel.";
    return outcome;
}

SurfaceActionOutcome pushSurfaceDepthStep(GameState& state, Random& rng)
{
    SurfacePushRunState& push = state.run.surfacePush;
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    SurfaceActionOutcome outcome;
    if (!push.active || push.completed) {
        outcome.message = "Dig is not active.";
        return outcome;
    }


    const ContentCatalog catalog = createDefaultContent();
    const int targetDepth = expedition.depth + push.steps + 1;
    const SurfaceDepthCapability capability = surfaceDepthCapability(
        state,
        catalog,
        targetDepth);
    if (!capability.canDig) {
        push.completed = true;
        outcome.message = surfaceDepthBlockerMessage(capability);
        push.message = outcome.message;
        return outcome;
    }

    outcome.applied = true;
    if (push.steps > 0 && rng.chance(push.collapseRisk)) {
        push.completed = true;
        push.busted = true;
        push.active = false;
        expedition.hazard += tuning::research::pushCollapseHazardIncrease;
        outcome.hazardTriggered = true;
        outcome.hazardMessage = "A shelf collapsed across the descent lane.";
        outcome.hazardDelta = tuning::research::pushCollapseHazardIncrease;
        outcome.message = "Dig failed. The tunnel collapsed before the crew could secure the new start depth.";
        push.message = surfaceActionSummary(outcome);
        appendSurfaceLog(expedition, push.message);
        return outcome;
    }

    push.steps += 1;
    awardExpeditionExperience(state, 2.0, Screen::SurfacePush);
    const SurfacePushSupport support = surfacePushSupport(state);
    push.depthGain = std::max(push.depthGain, push.steps);
    push.pressure = std::clamp(push.pressure + std::max(0.08, 0.16 - support.pressureRelief * 0.35) + rng.range(0.00, 0.06), 0.0, 1.0);
    const double pushHazardDelta = std::max(0.0, tuning::research::pushHazardPerStep - support.hazardRelief * 0.12);
    push.hazardDelta += pushHazardDelta;
    push.collapseRisk = std::clamp(
        tuning::research::pushBaseCollapseRisk +
            expedition.hazard * tuning::research::pushRiskHazardScale +
            push.steps * tuning::research::pushRiskPerStep +
            push.pressure * 0.045 -
            support.collapseRelief,
        0.04,
        0.84);

    const SurfaceDepthProspect* forecast = findSurfaceDepthProspect(expedition, targetDepth);
    MaterialInventory gain = actualizePushMaterials(expedition, push.steps, forecast, support, rng);
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    bool artifactFound = false;
    const bool thermalSurface = surfaceUsesThermalOnlyRegolith(state);
    const bool authoredArtifactPending = unresolvedProgressionArtifactOpportunity(
        state,
        catalog,
        expedition.destinationId).has_value();
    const bool forecastArtifact = forecast != nullptr && forecast->possibleArtifacts > 0;
    const double blindArtifactChance = thermalSurface
        ? 0.0
        : tuning::research::artifactChanceBase * 0.40 + push.steps * 0.10 +
            crew.artifactChanceBonus + site.artifactChanceBonus + support.artifactChanceBonus;
    if (!thermalSurface
        && !authoredArtifactPending
        && (forecastArtifact || push.steps >= 2)
        && push.temporaryArtifacts.empty()
        && (forecastArtifact || rng.chance(std::min(0.82, blindArtifactChance)))) {
        push.temporaryArtifacts.push_back({artifactId(expedition), expedition.destinationId, false});
        outcome.artifactFound = true;
        artifactFound = true;
        outcome.cargoDelta += 3;
        push.cargo += 3;
    }

    addMaterials(push.temporaryMaterials, gain);
    const int cargoGain = materialCargo(gain);
    push.cargo += cargoGain;
    appendSurfacePushMarkers(push, gain, artifactFound, push.steps);
    outcome.materialDelta = gain;
    outcome.cargoDelta += cargoGain;
    outcome.hazardDelta = pushHazardDelta;
    if (thermalSurface) {
        outcome.message = "Thermal seam confirmed. Its mapped resources will require Hazard treatment in the mining layer.";
    } else if (push.steps >= push.maxSteps) {
        const SurfaceDepthCapability nextCapability = surfaceDepthCapability(
            state,
            catalog,
            targetDepth + 1);
        outcome.message = "Surveyed depth +" + std::to_string(targetDepth) +
            " confirmed. " + surfaceDepthBlockerMessage(nextCapability);
    } else {
        outcome.message = "Surveyed layer +" + std::to_string(push.steps) +
            " confirmed. Set the start depth now or risk it on the next push.";
    }
    push.message = surfaceActionSummary(outcome);
    if (push.steps >= push.maxSteps) {
        push.completed = true;
    }
    return outcome;
}

SurfaceActionOutcome bankSurfacePush(GameState& state)
{
    SurfacePushRunState& push = state.run.surfacePush;
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    SurfaceActionOutcome outcome;
    if (!push.completed && !push.active) {
        outcome.message = "No start depth is ready to set.";
        return outcome;
    }
    if (!push.busted && push.depthGain <= 0) {
        outcome.message = "Dig at least one surveyed layer before setting a new start depth.";
        return outcome;
    }

    outcome.applied = true;
    if (push.busted) {
        outcome.message = "Deep route lost. No new start depth set.";
    } else {
        const bool tutorialDigCompleted = push.steps > 0 && push.depthGain > 0;
        expedition.prospectMaterials = {};
        expedition.prospectArtifacts = 0;
        addMaterials(expedition.prospectMaterials, push.temporaryMaterials);
        expedition.prospectArtifacts += static_cast<int>(push.temporaryArtifacts.size());
        expedition.depth += push.depthGain;
        expedition.hazard += push.hazardDelta;
        expedition.miningSitePrepared = true;
        if (tutorialDigCompleted) {
            ui::briefings::acknowledge(
                state.meta.acknowledgedActivityBriefingIds,
                ui::briefings::surfaceDigComplete);
        }
        outcome.hazardDelta = push.hazardDelta;
        outcome.message = hasPendingSurfacePayload(push.temporaryMaterials, push.temporaryArtifacts, push.cargo)
            ? "Start depth set. Richer deposits are marked in the mining lane."
            : "Start depth set. The next mining lane is open.";
    }
    appendSurfaceLog(expedition, surfaceActionSummary(outcome));
    resetSurfacePush(state);
    state.screen = Screen::SurfaceExpedition;
    return outcome;
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
    addMaterials(state.meta.materials, outcome.materialDelta);
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
        outcome.message += " " + std::to_string(outcome.materialDelta.common) + " added to Materials.";
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

    PlanetaryExpeditionState preservedProgression;
    copyRunProgression(expedition, preservedProgression);
    expedition = std::move(preservedProgression);
    return outcome;
}

} // namespace rocket
