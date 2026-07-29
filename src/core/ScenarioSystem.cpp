#include "core/ScenarioSystem.h"

#include "core/Content.h"
#include "core/GameState.h"
#include "core/ResearchSystem.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <sstream>
#include <system_error>

namespace rocket {
namespace {

bool containsId(const std::vector<std::string>& values, std::string_view id)
{
    return std::find(values.begin(), values.end(), id) != values.end();
}

void appendUniqueId(std::vector<std::string>& values, std::string_view id)
{
    if (!id.empty() && !containsId(values, id)) {
        values.emplace_back(id);
    }
}

bool parseNonNegativeInt(std::string_view text, int& value)
{
    int parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc {} || end != text.data() + text.size() || parsed < 0) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseBool(std::string_view text, bool& value)
{
    if (text == "1" || text == "true") {
        value = true;
        return true;
    }
    if (text == "0" || text == "false") {
        value = false;
        return true;
    }
    return false;
}

bool parseScenarioEventKind(std::string_view text, ScenarioEventKind& value)
{
    if (text == "none") value = ScenarioEventKind::None;
    else if (text == "safe_material_delivered") value = ScenarioEventKind::SafeMaterialDelivered;
    else if (text == "protected_objective_extracted") value = ScenarioEventKind::ProtectedObjectiveExtracted;
    else if (text == "flyby_finished") value = ScenarioEventKind::FlybyFinished;
    else if (text == "manual_action") value = ScenarioEventKind::ManualAction;
    else if (text == "activity_aborted") value = ScenarioEventKind::ActivityAborted;
    else if (text == "mining_site_completed") value = ScenarioEventKind::MiningSiteCompleted;
    else if (text == "equipment_assigned") value = ScenarioEventKind::EquipmentAssigned;
    else return false;
    return true;
}

bool parseScenarioActionKind(std::string_view text, ScenarioActionKind& value)
{
    if (text == "none") value = ScenarioActionKind::None;
    else if (text == "acknowledge_briefing") value = ScenarioActionKind::AcknowledgeBriefing;
    else if (text == "claim_reward") value = ScenarioActionKind::ClaimReward;
    else if (text == "begin_activity") value = ScenarioActionKind::BeginActivity;
    else if (text == "retry_activity") value = ScenarioActionKind::RetryActivity;
    else if (text == "acknowledge_failure") value = ScenarioActionKind::AcknowledgeFailure;
    else return false;
    return true;
}

bool parseScenarioRewardKind(std::string_view text, ScenarioRewardKind& value)
{
    if (text == "unlock_key") value = ScenarioRewardKind::UnlockKey;
    else if (text == "drone_bay_slots") value = ScenarioRewardKind::DroneBaySlots;
    else if (text == "support_drone") value = ScenarioRewardKind::SupportDrone;
    else if (text == "drone_upgrade_credit") value = ScenarioRewardKind::DroneUpgradeCredit;
    else if (text == "frontier_readiness") value = ScenarioRewardKind::FrontierReadiness;
    else if (text == "inventory_resources") value = ScenarioRewardKind::InventoryResources;
    else if (text == "route_access") value = ScenarioRewardKind::RouteAccess;
    else return false;
    return true;
}

bool splitResolvedParameter(
    std::string_view parameter,
    std::string_view& key,
    std::string_view& value)
{
    const std::size_t separator = parameter.find('=');
    if (separator == std::string_view::npos || separator == 0) {
        return false;
    }
    key = parameter.substr(0, separator);
    value = parameter.substr(separator + 1);
    return true;
}

bool failResolvedParameter(std::string* error, std::string_view message)
{
    if (error != nullptr) {
        *error = std::string(message);
    }
    return false;
}

bool hasPositiveMaterials(const MaterialInventory& materials)
{
    return materials.common > 0 || materials.rare > 0 || materials.exotic > 0;
}

bool validateScenarioReward(
    const ContentCatalog& catalog,
    const ScenarioReward& reward,
    std::string* error)
{
    switch (reward.kind) {
    case ScenarioRewardKind::UnlockKey:
        return !reward.id.empty() ||
            failResolvedParameter(error, "A scenario unlock-key reward requires an ID.");
    case ScenarioRewardKind::DroneBaySlots:
    case ScenarioRewardKind::DroneUpgradeCredit:
        return reward.amount > 0 ||
            failResolvedParameter(error, "A scenario quantity reward requires a positive amount.");
    case ScenarioRewardKind::SupportDrone:
        if (reward.id.empty() || catalog.findMiniDrone(reward.id) == nullptr) {
            return failResolvedParameter(error, "A scenario support-drone reward references an unknown drone.");
        }
        return true;
    case ScenarioRewardKind::InventoryResources:
        if (reward.materials.common < 0 || reward.materials.rare < 0 ||
            reward.materials.exotic < 0 || !hasPositiveMaterials(reward.materials)) {
            return failResolvedParameter(error, "A scenario inventory-resource reward requires positive materials.");
        }
        return true;
    case ScenarioRewardKind::RouteAccess: {
        const Destination* destination = catalog.findDestination(reward.id);
        if (destination == nullptr || destination->routeRequirementKeys.empty()) {
            return failResolvedParameter(error, "A scenario route-access reward references a destination without a route requirement.");
        }
        return true;
    }
    case ScenarioRewardKind::FrontierReadiness:
        return true;
    }
    return failResolvedParameter(error, "A scenario reward has an unknown kind.");
}

bool rewardGrantsRouteRequirementKey(
    const ContentCatalog& catalog,
    const ScenarioReward& reward,
    std::string_view key)
{
    if (reward.kind == ScenarioRewardKind::UnlockKey) {
        return reward.id == key;
    }
    if (reward.kind != ScenarioRewardKind::RouteAccess) {
        return false;
    }
    const Destination* destination = catalog.findDestination(reward.id);
    return destination != nullptr && containsId(destination->routeRequirementKeys, key);
}

// Procedural factories persist a compact, deterministic key=value record
// rather than serializing duplicate content definitions. Supported keys are:
// destination, availability_unlock, and step.<id>.(presentation fields,
// event_origin, event_target, completion_event, required_progress,
// required_grade, mandatory_briefing, claim_required, first_failure,
// action, mining_site, reward_count, reward.<n>.<field>). Reward fields
// include material quantities and a RouteAccess destination ID. This keeps a
// generator free to configure mechanics and presentation while the catalog
// remains the typed source of the available mechanic shapes.
bool applyResolvedParameter(
    ScenarioDefinition& definition,
    std::string_view parameter,
    std::string* error)
{
    std::string_view key;
    std::string_view value;
    if (!splitResolvedParameter(parameter, key, value)) {
        return failResolvedParameter(error, "Scenario resolved parameter must use key=value.");
    }
    if (key == "destination") {
        definition.destinationId = value;
        return true;
    }
    if (key == "availability_unlock") {
        definition.availabilityUnlockKey = value;
        return true;
    }
    constexpr std::string_view stepPrefix = "step.";
    if (!key.starts_with(stepPrefix)) {
        return failResolvedParameter(error, "Scenario resolved parameter uses an unknown key.");
    }

    const std::size_t stepSeparator = key.find('.', stepPrefix.size());
    if (stepSeparator == std::string_view::npos || stepSeparator == stepPrefix.size() ||
        stepSeparator + 1 >= key.size()) {
        return failResolvedParameter(error, "Scenario resolved step parameter is malformed.");
    }
    const std::string_view stepId = key.substr(stepPrefix.size(), stepSeparator - stepPrefix.size());
    const std::string_view field = key.substr(stepSeparator + 1);
    const auto foundStep = std::find_if(
        definition.steps.begin(),
        definition.steps.end(),
        [&](const ScenarioStepDefinition& step) { return step.id == stepId; });
    if (foundStep == definition.steps.end()) {
        return failResolvedParameter(error, "Scenario resolved parameter references an unknown step.");
    }
    ScenarioStepDefinition& step = *foundStep;

    if (field == "location") step.location = value;
    else if (field == "title") step.title = value;
    else if (field == "detail") step.detail = value;
    else if (field == "reward_preview") step.rewardPreview = value;
    else if (field == "action_label") step.actionLabel = value;
    else if (field == "failure_explanation") step.failureExplanation = value;
    else if (field == "event_origin") step.eventOriginId = value;
    else if (field == "event_target") step.eventTargetId = value;
    else if (field == "mining_site") step.miningSiteDefinitionId = value;
    else if (field == "required_progress") {
        if (!parseNonNegativeInt(value, step.requiredProgress)) {
            return failResolvedParameter(error, "Scenario required_progress must be a non-negative integer.");
        }
    } else if (field == "required_grade") {
        if (!parseNonNegativeInt(value, step.requiredGrade)) {
            return failResolvedParameter(error, "Scenario required_grade must be a non-negative integer.");
        }
    } else if (field == "mandatory_briefing") {
        if (!parseBool(value, step.mandatoryBriefing)) {
            return failResolvedParameter(error, "Scenario mandatory_briefing must be true or false.");
        }
    } else if (field == "claim_required") {
        if (!parseBool(value, step.claimRequired)) {
            return failResolvedParameter(error, "Scenario claim_required must be true or false.");
        }
    } else if (field == "first_failure") {
        if (!parseBool(value, step.firstFailureExplanation)) {
            return failResolvedParameter(error, "Scenario first_failure must be true or false.");
        }
    } else if (field == "completion_event") {
        if (!parseScenarioEventKind(value, step.completionEvent)) {
            return failResolvedParameter(error, "Scenario completion_event is unknown.");
        }
    } else if (field == "action") {
        if (!parseScenarioActionKind(value, step.action)) {
            return failResolvedParameter(error, "Scenario action is unknown.");
        }
    } else if (field == "reward_count") {
        int count = 0;
        if (!parseNonNegativeInt(value, count) || count > 32) {
            return failResolvedParameter(error, "Scenario reward_count must be between zero and 32.");
        }
        step.rewards.resize(static_cast<std::size_t>(count));
    } else if (field.starts_with("reward.")) {
        const std::string_view rewardPath = field.substr(std::string_view("reward.").size());
        const std::size_t rewardSeparator = rewardPath.find('.');
        if (rewardSeparator == std::string_view::npos || rewardSeparator == 0 ||
            rewardSeparator + 1 >= rewardPath.size()) {
            return failResolvedParameter(error, "Scenario reward parameter is malformed.");
        }
        int index = 0;
        if (!parseNonNegativeInt(rewardPath.substr(0, rewardSeparator), index) ||
            static_cast<std::size_t>(index) >= step.rewards.size()) {
            return failResolvedParameter(error, "Scenario reward parameter references an unknown reward.");
        }
        ScenarioReward& reward = step.rewards[static_cast<std::size_t>(index)];
        const std::string_view rewardField = rewardPath.substr(rewardSeparator + 1);
        if (rewardField == "kind") {
            if (!parseScenarioRewardKind(value, reward.kind)) {
                return failResolvedParameter(error, "Scenario reward kind is unknown.");
            }
        } else if (rewardField == "id") {
            reward.id = value;
        } else if (rewardField == "amount") {
            if (!parseNonNegativeInt(value, reward.amount)) {
                return failResolvedParameter(error, "Scenario reward amount must be a non-negative integer.");
            }
        } else if (rewardField == "materials.common") {
            if (!parseNonNegativeInt(value, reward.materials.common)) {
                return failResolvedParameter(error, "Scenario Common material reward must be a non-negative integer.");
            }
        } else if (rewardField == "materials.rare") {
            if (!parseNonNegativeInt(value, reward.materials.rare)) {
                return failResolvedParameter(error, "Scenario Rare material reward must be a non-negative integer.");
            }
        } else if (rewardField == "materials.exotic") {
            if (!parseNonNegativeInt(value, reward.materials.exotic)) {
                return failResolvedParameter(error, "Scenario Exotic material reward must be a non-negative integer.");
            }
        } else if (rewardField == "equip_if_slot_available") {
            if (!parseBool(value, reward.equipIfSlotAvailable)) {
                return failResolvedParameter(error, "Scenario reward equip_if_slot_available must be true or false.");
            }
        } else {
            return failResolvedParameter(error, "Scenario reward parameter uses an unknown field.");
        }
    } else {
        return failResolvedParameter(error, "Scenario resolved step parameter uses an unknown field.");
    }
    return true;
}

bool validateResolvedDefinition(
    const ContentCatalog& catalog,
    const ScenarioDefinition& definition,
    std::string* error)
{
    for (const ScenarioStepDefinition& step : definition.steps) {
        if (step.requiredProgress < 0 || step.requiredGrade < 0) {
            return failResolvedParameter(error, "Scenario resolved step has an invalid completion target.");
        }
        if (!step.miningSiteDefinitionId.empty() &&
            findMiningSiteDefinition(catalog, step.miningSiteDefinitionId) == nullptr) {
            return failResolvedParameter(error, "Scenario resolved step references an unknown mining site.");
        }
        for (const ScenarioReward& reward : step.rewards) {
            if (!validateScenarioReward(catalog, reward, error)) {
                return false;
            }
        }
    }
    return true;
}

ScenarioStepProgress& ensureStepProgress(ScenarioInstance& instance, std::string_view stepId)
{
    if (ScenarioStepProgress* existing = findScenarioStepProgress(instance, stepId)) {
        return *existing;
    }
    instance.steps.push_back({std::string(stepId)});
    return instance.steps.back();
}

bool definitionAvailable(const GameState& state, const ScenarioDefinition& definition)
{
    return definition.availabilityUnlockKey.empty() ||
        hasUnlock(state.meta, definition.availabilityUnlockKey);
}

bool stepSatisfied(
    const ScenarioDefinition& definition,
    const ScenarioInstance& instance,
    std::string_view stepId)
{
    const ScenarioStepDefinition* step = findScenarioStepDefinition(definition, stepId);
    const ScenarioStepProgress* progress = findScenarioStepProgress(instance, stepId);
    if (step == nullptr || progress == nullptr || !progress->completed) {
        return false;
    }
    return !step->claimRequired || progress->claimed;
}

bool prerequisitesSatisfied(
    const ScenarioDefinition& definition,
    const ScenarioInstance& instance,
    const ScenarioStepDefinition& step)
{
    return std::all_of(
        step.prerequisites.begin(),
        step.prerequisites.end(),
        [&](const std::string& prerequisite) {
            return stepSatisfied(definition, instance, prerequisite);
        });
}

const ScenarioDefinition* definitionForInstance(
    const ContentCatalog& catalog,
    const ScenarioInstance& instance)
{
    return findScenarioDefinition(
        catalog,
        instance.definitionId.empty() ? std::string_view(instance.id) : std::string_view(instance.definitionId));
}

bool isObsoleteDefaultTemplateInstance(
    const ScenarioDefinition& definition,
    const ScenarioInstance& instance)
{
    // Earlier v9 development snapshots instantiated every catalog definition.
    // A template-only definition must never become a live authored objective;
    // procedural instances deliberately retain the same definition ID.
    return !definition.instantiateByDefault &&
        instance.source == ScenarioSource::Authored &&
        instance.id == definition.id;
}

std::string rewardId(const ScenarioDefinition& definition, const ScenarioStepDefinition& step, std::size_t index)
{
    return definition.id + "/" + step.id + "/" + std::to_string(index);
}

void applyReward(
    GameState& state,
    const ContentCatalog& catalog,
    ScenarioInstance& instance,
    const ScenarioDefinition& definition,
    const ScenarioStepDefinition& step,
    std::size_t index,
    const ScenarioReward& reward)
{
    const std::string id = rewardId(definition, step, index);
    if (containsId(instance.awardedRewardIds, id)) {
        return;
    }

    switch (reward.kind) {
    case ScenarioRewardKind::UnlockKey:
        appendUniqueId(state.meta.unlockKeys, reward.id);
        break;
    case ScenarioRewardKind::DroneBaySlots:
        state.meta.droneBaySlots = std::max(state.meta.droneBaySlots, std::max(0, reward.amount));
        ensureDroneBayState(state, catalog);
        break;
    case ScenarioRewardKind::SupportDrone:
        appendUniqueId(state.meta.ownedDroneIds, reward.id);
        ensureDroneBayState(state, catalog);
        if (reward.equipIfSlotAvailable &&
            state.meta.equippedDroneIds.size() < static_cast<std::size_t>(state.meta.droneBaySlots)) {
            state.meta.equippedDroneIds.emplace_back(reward.id);
        }
        break;
    case ScenarioRewardKind::DroneUpgradeCredit:
        state.meta.droneUpgradeCredits += std::max(0, reward.amount);
        break;
    case ScenarioRewardKind::FrontierReadiness:
        state.run.frontierReadiness = frontierReadinessCap(state, catalog);
        break;
    case ScenarioRewardKind::InventoryResources:
        addMaterials(state.meta.materials, reward.materials);
        break;
    case ScenarioRewardKind::RouteAccess: {
        const Destination* destination = catalog.findDestination(reward.id);
        if (destination != nullptr) {
            for (const std::string& key : destination->routeRequirementKeys) {
                appendUniqueId(state.meta.unlockKeys, key);
            }
        }
        break;
    }
    }
    instance.awardedRewardIds.push_back(id);
}

void applyStepRewards(
    GameState& state,
    const ContentCatalog& catalog,
    ScenarioInstance& instance,
    const ScenarioDefinition& definition,
    const ScenarioStepDefinition& step)
{
    for (std::size_t index = 0; index < step.rewards.size(); ++index) {
        applyReward(state, catalog, instance, definition, step, index, step.rewards[index]);
    }
}

void refreshScenarioCompletion(const ScenarioDefinition& definition, ScenarioInstance& instance)
{
    instance.completed = std::all_of(
        definition.steps.begin(),
        definition.steps.end(),
        [&](const ScenarioStepDefinition& step) {
            return stepSatisfied(definition, instance, step.id);
        });
}

bool eventMatches(
    const ScenarioDefinition& definition,
    const ScenarioInstance& instance,
    const ScenarioStepDefinition& step,
    const ScenarioEvent& event)
{
    // Events emitted by a running scenario must address its concrete runtime
    // instance. Authored default instances intentionally keep the definition
    // ID, while a factory can create many instances from one template.
    // An authored default instance may also accept its definition ID for old
    // callers; a procedural template ID must never fan one event out to every
    // generated instance.
    if (!event.scenarioId.empty()) {
        const bool matchesRuntimeInstance = event.scenarioId == instance.id;
        const bool matchesAuthoredCompatibilityId =
            instance.source == ScenarioSource::Authored && event.scenarioId == definition.id;
        if (!matchesRuntimeInstance && !matchesAuthoredCompatibilityId) {
            return false;
        }
    }
    if (!event.stepId.empty() && event.stepId != step.id) {
        return false;
    }
    // Aborts are a failure signal for an active scenario activity, not the
    // completion condition of a separate story-only step.
    if (event.kind == ScenarioEventKind::ActivityAborted) {
        return step.firstFailureExplanation;
    }
    if (step.completionEvent != event.kind) {
        return false;
    }
    if (!step.eventOriginId.empty() && step.eventOriginId != event.originId) {
        return false;
    }
    if (!step.eventTargetId.empty() && step.eventTargetId != event.targetId) {
        return false;
    }
    return true;
}

} // namespace

const ScenarioDefinition* findScenarioDefinition(const ContentCatalog& catalog, std::string_view scenarioId)
{
    const auto found = std::find_if(
        catalog.scenarios.begin(),
        catalog.scenarios.end(),
        [&](const ScenarioDefinition& definition) { return definition.id == scenarioId; });
    return found == catalog.scenarios.end() ? nullptr : &*found;
}

const ScenarioDefinition* scenarioDefinitionForRuntimeId(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view scenarioId)
{
    const ScenarioInstance* instance = findScenarioInstance(state.meta, scenarioId);
    const std::string_view definitionId = instance == nullptr || instance->definitionId.empty()
        ? scenarioId
        : std::string_view(instance->definitionId);
    return findScenarioDefinition(catalog, definitionId);
}

ScenarioDefinition resolveScenarioDefinition(
    const ScenarioDefinition& definition,
    const ScenarioInstance& instance)
{
    ScenarioDefinition resolved = definition;
    for (const std::string& parameter : instance.resolvedParameters) {
        // Save compatibility intentionally favors the authored baseline if a
        // stale or malformed procedural parameter is encountered. Factories
        // are rejected up front by validateScenarioInstance().
        (void)applyResolvedParameter(resolved, parameter, nullptr);
    }
    return resolved;
}

bool validateScenarioInstance(
    const ContentCatalog& catalog,
    const ScenarioInstance& instance,
    std::string* error)
{
    const ScenarioDefinition* definition = definitionForInstance(catalog, instance);
    if (definition == nullptr) {
        return failResolvedParameter(error, "Scenario instance references an unknown definition.");
    }
    ScenarioDefinition resolved = *definition;
    for (const std::string& parameter : instance.resolvedParameters) {
        if (!applyResolvedParameter(resolved, parameter, error)) {
            return false;
        }
    }
    return validateResolvedDefinition(catalog, resolved, error);
}

const ScenarioFactoryDefinition* findScenarioFactoryDefinition(const ContentCatalog& catalog, std::string_view factoryId)
{
    const auto found = std::find_if(
        catalog.scenarioFactories.begin(),
        catalog.scenarioFactories.end(),
        [&](const ScenarioFactoryDefinition& definition) { return definition.id == factoryId; });
    return found == catalog.scenarioFactories.end() ? nullptr : &*found;
}

const MiningSiteDefinition* findMiningSiteDefinition(const ContentCatalog& catalog, std::string_view siteId)
{
    const auto found = std::find_if(
        catalog.miningSites.begin(),
        catalog.miningSites.end(),
        [&](const MiningSiteDefinition& definition) { return definition.id == siteId; });
    return found == catalog.miningSites.end() ? nullptr : &*found;
}

bool validateScenarioCatalog(const ContentCatalog& catalog, std::string* error)
{
    auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    std::vector<std::string> miningSiteIds;
    for (const MiningSiteDefinition& site : catalog.miningSites) {
        if (site.id.empty() || containsId(miningSiteIds, site.id) || site.version < 1) {
            return fail("Mining sites require unique non-empty IDs and a version.");
        }
        miningSiteIds.push_back(site.id);
        if (site.cocoon.layers.empty()) {
            continue;
        }
        if (site.gateType != MiningGateType::HazardCocoon ||
            site.cocoon.id.empty() || site.cocoon.version < 1 ||
            site.cocoon.protectedObjective.kind == ProtectedObjectiveKind::None ||
            site.cocoon.protectedObjective.id.empty()) {
            return fail("Mining cocoon '" + site.id + "' has an invalid gate or protected objective.");
        }
        std::vector<std::string> layerIds;
        for (std::size_t index = 0; index < site.cocoon.layers.size(); ++index) {
            const MiningCocoonLayerDefinition& layer = site.cocoon.layers[index];
            if (layer.id.empty() || layer.label.empty() || layer.offsets.empty() ||
                layer.requiredHazardMark < 1 || containsId(layerIds, layer.id)) {
                return fail("Mining cocoon '" + site.id + "' has an invalid layer.");
            }
            if (index == 0 && layer.revealPolicy == MiningCocoonRevealPolicy::AfterPreviousLayerCompleted) {
                return fail("The first cocoon layer cannot wait for a previous layer.");
            }
            for (std::size_t offsetIndex = 0; offsetIndex < layer.offsets.size(); ++offsetIndex) {
                const MiningCocoonOffset& offset = layer.offsets[offsetIndex];
                const bool duplicateOffset = std::any_of(
                    layer.offsets.begin(),
                    layer.offsets.begin() + static_cast<std::ptrdiff_t>(offsetIndex),
                    [&](const MiningCocoonOffset& earlier) {
                        return earlier.x == offset.x && earlier.y == offset.y;
                    });
                if (duplicateOffset) {
                    return fail("Mining cocoon '" + site.id + "' has duplicate layer offsets.");
                }
            }
            layerIds.push_back(layer.id);
        }
    }

    std::vector<std::string> scenarioIds;
    for (const ScenarioDefinition& definition : catalog.scenarios) {
        if (definition.id.empty() || containsId(scenarioIds, definition.id) ||
            definition.version < 1 || definition.steps.empty()) {
            return fail("Scenario definitions require unique non-empty IDs, a version, and at least one step.");
        }
        scenarioIds.push_back(definition.id);
        std::vector<std::string> stepIds;
        for (const ScenarioStepDefinition& step : definition.steps) {
            if (step.id.empty() || containsId(stepIds, step.id)) {
                return fail("Scenario '" + definition.id + "' has duplicate or empty step IDs.");
            }
            if (step.requiredProgress < 0 || step.requiredGrade < 0) {
                return fail("Scenario '" + definition.id + "' has an invalid completion target.");
            }
            for (const ScenarioReward& reward : step.rewards) {
                std::string rewardError;
                if (!validateScenarioReward(catalog, reward, &rewardError)) {
                    return fail("Scenario '" + definition.id + "' has an invalid reward: " + rewardError);
                }
            }
            stepIds.push_back(step.id);
        }
        for (const ScenarioStepDefinition& step : definition.steps) {
            for (const std::string& prerequisite : step.prerequisites) {
                if (!containsId(stepIds, prerequisite) || prerequisite == step.id) {
                    return fail("Scenario '" + definition.id + "' has an invalid prerequisite.");
                }
            }
            if (!step.miningSiteDefinitionId.empty() &&
                findMiningSiteDefinition(catalog, step.miningSiteDefinitionId) == nullptr) {
                return fail("Scenario '" + definition.id + "' references an unknown mining site.");
            }
        }

        // Prerequisites are deliberately local to a definition. A scenario
        // can model branches and staged encounters without quietly coupling
        // two authored definitions; cross-scenario availability is expressed
        // through an unlock reward/key instead.
        std::vector<int> visitState(stepIds.size(), 0);
        const auto indexForStep = [&](std::string_view id) {
            return static_cast<int>(std::distance(
                stepIds.begin(), std::find(stepIds.begin(), stepIds.end(), id)));
        };
        const auto visit = [&](auto&& self, int index) -> bool {
            if (visitState[static_cast<std::size_t>(index)] == 1) {
                return false;
            }
            if (visitState[static_cast<std::size_t>(index)] == 2) {
                return true;
            }
            visitState[static_cast<std::size_t>(index)] = 1;
            const ScenarioStepDefinition& step = definition.steps[static_cast<std::size_t>(index)];
            for (const std::string& prerequisite : step.prerequisites) {
                if (!self(self, indexForStep(prerequisite))) {
                    return false;
                }
            }
            visitState[static_cast<std::size_t>(index)] = 2;
            return true;
        };
        for (std::size_t index = 0; index < stepIds.size(); ++index) {
            if (!visit(visit, static_cast<int>(index))) {
                return fail("Scenario '" + definition.id + "' has cyclic step dependencies.");
            }
        }
    }
    std::vector<std::string> factoryIds;
    for (const ScenarioFactoryDefinition& factory : catalog.scenarioFactories) {
        const ScenarioDefinition* templateDefinition =
            findScenarioDefinition(catalog, factory.templateScenarioId);
        if (factory.id.empty() || containsId(factoryIds, factory.id) ||
            factory.version < 1 || templateDefinition == nullptr) {
            return fail("Scenario factory references an unknown template.");
        }
        if (templateDefinition->instantiateByDefault) {
            return fail("Scenario factory '" + factory.id + "' must reference a non-default template.");
        }
        factoryIds.push_back(factory.id);
    }
    return true;
}

ScenarioInstance* findScenarioInstance(MetaProgress& meta, std::string_view scenarioId)
{
    const auto found = std::find_if(
        meta.scenarios.begin(),
        meta.scenarios.end(),
        [&](const ScenarioInstance& instance) { return instance.id == scenarioId; });
    return found == meta.scenarios.end() ? nullptr : &*found;
}

const ScenarioInstance* findScenarioInstance(const MetaProgress& meta, std::string_view scenarioId)
{
    const auto found = std::find_if(
        meta.scenarios.begin(),
        meta.scenarios.end(),
        [&](const ScenarioInstance& instance) { return instance.id == scenarioId; });
    return found == meta.scenarios.end() ? nullptr : &*found;
}

ScenarioStepProgress* findScenarioStepProgress(ScenarioInstance& instance, std::string_view stepId)
{
    const auto found = std::find_if(
        instance.steps.begin(),
        instance.steps.end(),
        [&](const ScenarioStepProgress& step) { return step.id == stepId; });
    return found == instance.steps.end() ? nullptr : &*found;
}

const ScenarioStepProgress* findScenarioStepProgress(const ScenarioInstance& instance, std::string_view stepId)
{
    const auto found = std::find_if(
        instance.steps.begin(),
        instance.steps.end(),
        [&](const ScenarioStepProgress& step) { return step.id == stepId; });
    return found == instance.steps.end() ? nullptr : &*found;
}

const ScenarioStepDefinition* findScenarioStepDefinition(
    const ScenarioDefinition& definition,
    std::string_view stepId)
{
    const auto found = std::find_if(
        definition.steps.begin(),
        definition.steps.end(),
        [&](const ScenarioStepDefinition& step) { return step.id == stepId; });
    return found == definition.steps.end() ? nullptr : &*found;
}

void ensureScenarioInstances(GameState& state, const ContentCatalog& catalog)
{
    state.meta.scenarios.erase(
        std::remove_if(
            state.meta.scenarios.begin(),
            state.meta.scenarios.end(),
            [&](const ScenarioInstance& instance) {
                const ScenarioDefinition* definition = definitionForInstance(catalog, instance);
                return definition != nullptr && isObsoleteDefaultTemplateInstance(*definition, instance);
            }),
        state.meta.scenarios.end());

    for (const ScenarioDefinition& definition : catalog.scenarios) {
        if (!definition.instantiateByDefault) {
            continue;
        }
        ScenarioInstance* instance = findScenarioInstance(state.meta, definition.id);
        if (instance == nullptr) {
            ScenarioInstance created;
            created.id = definition.id;
            created.definitionId = definition.id;
            created.definitionVersion = definition.version;
            state.meta.scenarios.push_back(std::move(created));
            instance = &state.meta.scenarios.back();
        }
        if (instance->definitionId.empty()) {
            instance->definitionId = definition.id;
        }
        if (instance->definitionVersion < 1) {
            instance->definitionVersion = definition.version;
        }
        for (const ScenarioStepDefinition& step : definition.steps) {
            (void)ensureStepProgress(*instance, step.id);
        }
    }
}

ScenarioStepState scenarioStepState(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view scenarioId,
    std::string_view stepId)
{
    const ScenarioInstance* instance = findScenarioInstance(state.meta, scenarioId);
    const ScenarioDefinition* definition = instance == nullptr
        ? findScenarioDefinition(catalog, scenarioId)
        : definitionForInstance(catalog, *instance);
    if (definition == nullptr || instance == nullptr) {
        return ScenarioStepState::Locked;
    }
    const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, *instance);
    if (!definitionAvailable(state, resolved)) {
        return ScenarioStepState::Locked;
    }
    const ScenarioStepDefinition* step = findScenarioStepDefinition(resolved, stepId);
    const ScenarioStepProgress* progress = findScenarioStepProgress(*instance, stepId);
    if (step == nullptr || progress == nullptr || !prerequisitesSatisfied(resolved, *instance, *step)) {
        return ScenarioStepState::Locked;
    }
    if (progress->claimed || (progress->completed && !step->claimRequired)) {
        return ScenarioStepState::Complete;
    }
    if (progress->completed) {
        return ScenarioStepState::ReadyToClaim;
    }
    return ScenarioStepState::Active;
}

bool scenarioStepBriefingAcknowledged(
    const GameState& state,
    std::string_view scenarioId,
    std::string_view stepId)
{
    const ScenarioInstance* instance = findScenarioInstance(state.meta, scenarioId);
    const ScenarioStepProgress* progress = instance == nullptr ? nullptr : findScenarioStepProgress(*instance, stepId);
    return progress != nullptr && progress->briefingAcknowledged;
}

ScenarioActionOutcome performScenarioAction(
    GameState& state,
    const ContentCatalog& catalog,
    std::string_view scenarioId,
    std::string_view stepId,
    ScenarioActionKind action)
{
    ScenarioActionOutcome outcome;
    ensureScenarioInstances(state, catalog);
    ScenarioInstance* instance = findScenarioInstance(state.meta, scenarioId);
    const ScenarioDefinition* definition = instance == nullptr
        ? findScenarioDefinition(catalog, scenarioId)
        : definitionForInstance(catalog, *instance);
    if (definition == nullptr || instance == nullptr) {
        return outcome;
    }
    const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, *instance);
    const ScenarioStepDefinition* step = findScenarioStepDefinition(resolved, stepId);
    ScenarioStepProgress* progress = findScenarioStepProgress(*instance, stepId);
    if (step == nullptr || progress == nullptr ||
        scenarioStepState(state, catalog, scenarioId, stepId) == ScenarioStepState::Locked) {
        return outcome;
    }

    if (action == ScenarioActionKind::AcknowledgeBriefing) {
        if (!step->mandatoryBriefing || progress->briefingAcknowledged) {
            return outcome;
        }
        progress->briefingAcknowledged = true;
        if (step->completionEvent == ScenarioEventKind::None) {
            progress->progress = std::max(1, step->requiredProgress);
            progress->completed = true;
            progress->claimed = true;
            applyStepRewards(state, catalog, *instance, resolved, *step);
            refreshScenarioCompletion(resolved, *instance);
        }
        outcome.applied = true;
        outcome.message = step->detail;
        return outcome;
    }
    if (action == ScenarioActionKind::AcknowledgeFailure) {
        if (!progress->failureSeen || progress->failureAcknowledged) {
            return outcome;
        }
        progress->failureAcknowledged = true;
        outcome.applied = true;
        outcome.message = step->failureExplanation;
        return outcome;
    }
    if (action == ScenarioActionKind::ClaimReward) {
        if (!progress->completed || !step->claimRequired || progress->claimed) {
            return outcome;
        }
        progress->claimed = true;
        applyStepRewards(state, catalog, *instance, resolved, *step);
        refreshScenarioCompletion(resolved, *instance);
        outcome.applied = true;
        outcome.message = step->rewardPreview;
        return outcome;
    }
    if (action == ScenarioActionKind::BeginActivity || action == ScenarioActionKind::RetryActivity) {
        const bool activityActionAuthored =
            step->action == ScenarioActionKind::BeginActivity ||
            step->action == ScenarioActionKind::RetryActivity;
        if (!activityActionAuthored || step->action != action) {
            return outcome;
        }
        // An authored activity action can be the only meaningful
        // acknowledgement. This supports both commissioning equipment and a
        // briefing whose named button starts a challenge, without a dead-end
        // acknowledge-then-repeat interaction.
        const bool actionAcknowledgesBriefing =
            step->mandatoryBriefing && !progress->briefingAcknowledged &&
            activityActionAuthored;
        if (actionAcknowledgesBriefing) {
            progress->briefingAcknowledged = true;
        }
        if (step->mandatoryBriefing && !progress->briefingAcknowledged) {
            return outcome;
        }
        outcome.applied = true;
        if (step->completionEvent == ScenarioEventKind::ManualAction) {
            progress->progress = std::max(1, step->requiredProgress);
            progress->completed = true;
            progress->claimed = true;
            applyStepRewards(state, catalog, *instance, resolved, *step);
            refreshScenarioCompletion(resolved, *instance);
        } else {
            progress->activityStarted = true;
            outcome.beginsActivity = true;
            outcome.activityEvent = step->completionEvent;
        }
        outcome.miningSiteDefinitionId = step->miningSiteDefinitionId;
        outcome.message = step->detail;
        return outcome;
    }

    return outcome;
}

bool recordScenarioEvent(GameState& state, const ContentCatalog& catalog, const ScenarioEvent& event)
{
    ensureScenarioInstances(state, catalog);
    bool changed = false;
    for (ScenarioInstance& instance : state.meta.scenarios) {
        const ScenarioDefinition* definition = definitionForInstance(catalog, instance);
        if (definition == nullptr) {
            continue;
        }
        const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, instance);
        if (!definitionAvailable(state, resolved)) {
            continue;
        }
        for (const ScenarioStepDefinition& step : resolved.steps) {
            ScenarioStepProgress* progress = findScenarioStepProgress(instance, step.id);
            if (progress == nullptr || progress->completed ||
                !prerequisitesSatisfied(resolved, instance, step) ||
                (step.mandatoryBriefing && !progress->briefingAcknowledged) ||
                !eventMatches(resolved, instance, step, event)) {
                continue;
            }

            if (event.kind == ScenarioEventKind::ActivityAborted) {
                if (step.firstFailureExplanation && !progress->failureSeen) {
                    progress->failureSeen = true;
                    changed = true;
                }
                continue;
            }
            if (event.kind == ScenarioEventKind::FlybyFinished && event.grade < step.requiredGrade) {
                if (step.firstFailureExplanation && !progress->failureSeen) {
                    progress->failureSeen = true;
                    changed = true;
                }
                continue;
            }

            const int increment = event.kind == ScenarioEventKind::SafeMaterialDelivered
                ? std::max(0, event.amount)
                : std::max(1, event.amount);
            progress->progress = std::min(
                std::max(1, step.requiredProgress),
                progress->progress + increment);
            if (progress->progress >= std::max(1, step.requiredProgress)) {
                progress->completed = true;
                if (!step.claimRequired) {
                    progress->claimed = true;
                    applyStepRewards(state, catalog, instance, resolved, step);
                }
                refreshScenarioCompletion(resolved, instance);
            }
            changed = true;
        }
    }
    return changed;
}

ScenarioRouteRequirementStatus scenarioRouteRequirementStatus(
    const GameState& state,
    const ContentCatalog& catalog,
    const Destination& destination)
{
    ScenarioRouteRequirementStatus status;
    for (const std::string& key : destination.routeRequirementKeys) {
        if (hasUnlock(state.meta, key)) {
            continue;
        }
        status.satisfied = false;
        status.requiredUnlockKey = key;
        const auto findRewardingStep = [&](const ScenarioDefinition& definition,
                                           const ScenarioInstance* instance) {
            for (const ScenarioStepDefinition& step : definition.steps) {
                const bool grantsKey = std::any_of(
                    step.rewards.begin(),
                    step.rewards.end(),
                    [&](const ScenarioReward& reward) {
                        return rewardGrantsRouteRequirementKey(catalog, reward, key);
                    });
                if (grantsKey) {
                    status.scenarioId = instance == nullptr ? definition.id : instance->id;
                    status.stepId = step.id;
                    const ScenarioStepProgress* progress = instance == nullptr ? nullptr : findScenarioStepProgress(*instance, step.id);
                    status.current = progress == nullptr ? 0 : progress->progress;
                    status.required = std::max(1, step.requiredProgress);
                    return true;
                }
            }
            return false;
        };
        for (const ScenarioInstance& instance : state.meta.scenarios) {
            const ScenarioDefinition* definition = definitionForInstance(catalog, instance);
            if (definition == nullptr) {
                continue;
            }
            const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, instance);
            if (findRewardingStep(resolved, &instance)) {
                return status;
            }
        }
        // This fallback gives a newly constructed (but not yet initialized)
        // game state actionable route-copy. Template-only definitions are
        // intentionally excluded: a factory must create their instance first.
        for (const ScenarioDefinition& definition : catalog.scenarios) {
            if (definition.instantiateByDefault && findRewardingStep(definition, nullptr)) {
                return status;
            }
        }
        return status;
    }
    return status;
}

bool scenarioHasCompletedStep(
    const GameState& state,
    std::string_view scenarioId,
    std::string_view stepId)
{
    const ScenarioInstance* instance = findScenarioInstance(state.meta, scenarioId);
    const ScenarioStepProgress* progress = instance == nullptr ? nullptr : findScenarioStepProgress(*instance, stepId);
    return progress != nullptr && progress->completed && progress->claimed;
}

ScenarioObjectivePresentation scenarioObjectivePresentation(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view scenarioId,
    std::string_view stepId)
{
    ScenarioObjectivePresentation presentation;
    const ScenarioInstance* instance = findScenarioInstance(state.meta, scenarioId);
    const ScenarioDefinition* definition = instance == nullptr
        ? findScenarioDefinition(catalog, scenarioId)
        : definitionForInstance(catalog, *instance);
    if (definition == nullptr) {
        return presentation;
    }
    if (instance == nullptr) {
        return presentation;
    }
    const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, *instance);
    const ScenarioStepDefinition* step = findScenarioStepDefinition(resolved, stepId);
    const ScenarioStepProgress* progress = instance == nullptr
        ? nullptr
        : findScenarioStepProgress(*instance, stepId);
    if (step == nullptr || progress == nullptr) {
        return presentation;
    }

    presentation.available = true;
    presentation.scenarioId = instance->id;
    presentation.stepId = step->id;
    presentation.state = scenarioStepState(state, catalog, instance->id, step->id);
    presentation.location = step->location;
    presentation.title = step->title;
    presentation.detail = step->detail;
    presentation.rewardPreview = step->rewardPreview;
    presentation.actionLabel = step->actionLabel;
    presentation.failureExplanation = step->failureExplanation;
    presentation.current = std::clamp(progress->progress, 0, std::max(1, step->requiredProgress));
    presentation.required = std::max(1, step->requiredProgress);
    presentation.completionEvent = step->completionEvent;
    presentation.eventTargetId = step->eventTargetId;
    presentation.mandatoryBriefing = step->mandatoryBriefing;
    presentation.briefingAcknowledged = progress->briefingAcknowledged;
    presentation.firstFailurePending = progress->failureSeen && !progress->failureAcknowledged;
    presentation.activityStarted = progress->activityStarted;
    presentation.miningSiteDefinitionId = step->miningSiteDefinitionId;
    if (presentation.firstFailurePending) {
        presentation.action = ScenarioActionKind::AcknowledgeFailure;
    } else if (step->mandatoryBriefing && !progress->briefingAcknowledged) {
        presentation.action =
                (step->action == ScenarioActionKind::BeginActivity ||
                 step->action == ScenarioActionKind::RetryActivity)
            ? step->action
            : ScenarioActionKind::AcknowledgeBriefing;
    } else if (presentation.state == ScenarioStepState::ReadyToClaim) {
        presentation.action = ScenarioActionKind::ClaimReward;
    } else if (presentation.state == ScenarioStepState::Active &&
               (step->action == ScenarioActionKind::BeginActivity ||
                step->action == ScenarioActionKind::RetryActivity)) {
        presentation.action = step->action;
    } else {
        // Passive objectives such as safe delivery may use ClaimReward as
        // their eventual action in content, but must not present a premature
        // claim button while their counter is still active.
        presentation.action = ScenarioActionKind::None;
    }
    if (presentation.activityStarted &&
        (presentation.action == ScenarioActionKind::BeginActivity ||
         presentation.action == ScenarioActionKind::RetryActivity)) {
        constexpr std::string_view beginPrefix = "Begin ";
        if (presentation.actionLabel.rfind(beginPrefix, 0) == 0) {
            presentation.actionLabel = "Retry " + presentation.actionLabel.substr(beginPrefix.size());
        } else {
            presentation.actionLabel = "Retry " + presentation.title;
        }
    }
    return presentation;
}

ScenarioObjectivePresentation scenarioObjectiveForDestination(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId)
{
    ScenarioObjectivePresentation best;
    int bestRank = 100;
    for (const ScenarioInstance& instance : state.meta.scenarios) {
        const ScenarioDefinition* definition = definitionForInstance(catalog, instance);
        if (definition == nullptr) {
            continue;
        }
        const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, instance);
        if (resolved.destinationId != destinationId) {
            continue;
        }
        for (const ScenarioStepDefinition& step : resolved.steps) {
            ScenarioObjectivePresentation candidate =
                scenarioObjectivePresentation(state, catalog, instance.id, step.id);
            if (!candidate.available) {
                continue;
            }
            const int rank = candidate.state == ScenarioStepState::ReadyToClaim ? 0
                : candidate.state == ScenarioStepState::Active ? 1
                : candidate.state == ScenarioStepState::Locked ? 2
                : 3;
            if (rank < bestRank) {
                best = std::move(candidate);
                bestRank = rank;
            }
            // Definition order is meaningful for an equal state rank.
            if (bestRank == 0) {
                break;
            }
        }
    }
    return best;
}

ScenarioObjectivePresentation scenarioObjectiveForMining(
    const GameState& state,
    const ContentCatalog& catalog)
{
    const MiningRunState& mining = state.run.mining;
    if (!mining.scenarioId.empty() && !mining.scenarioStepId.empty()) {
        return scenarioObjectivePresentation(state, catalog, mining.scenarioId, mining.scenarioStepId);
    }
    return scenarioObjectiveForDestination(state, catalog, mining.destinationId);
}

ScenarioInstance makeProceduralScenarioInstance(
    const ContentCatalog& catalog,
    std::string_view factoryId,
    std::uint64_t seed,
    const std::vector<std::string>& resolvedParameters)
{
    ScenarioInstance instance;
    const ScenarioFactoryDefinition* factory = findScenarioFactoryDefinition(catalog, factoryId);
    if (factory == nullptr) {
        return instance;
    }
    const ScenarioDefinition* definition = findScenarioDefinition(catalog, factory->templateScenarioId);
    if (definition == nullptr) {
        return instance;
    }
    instance.id = definition->id + "@" + factory->id + "@" + std::to_string(seed);
    instance.definitionId = definition->id;
    instance.definitionVersion = definition->version;
    instance.source = ScenarioSource::Procedural;
    instance.factoryId = factory->id;
    instance.factoryVersion = factory->version;
    instance.seed = seed;
    instance.resolvedParameters = resolvedParameters;
    for (const ScenarioStepDefinition& step : definition->steps) {
        instance.steps.push_back({step.id});
    }
    std::string validationError;
    if (!validateScenarioInstance(catalog, instance, &validationError)) {
        return {};
    }
    return instance;
}

} // namespace rocket
