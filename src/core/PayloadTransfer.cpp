#include "core/PayloadTransfer.h"

#include "core/Content.h"
#include "core/GameState.h"
#include "core/ScenarioSystem.h"
#include "core/Tuning.h"

#include <algorithm>
#include <string>

namespace rocket {

namespace {

void add(MaterialInventory& target, const MaterialInventory& value)
{
    target.common += std::max(0, value.common);
    target.rare += std::max(0, value.rare);
    target.exotic += std::max(0, value.exotic);
}

int allocateUnits(int available, int unitMass, int& availableMass)
{
    if (available <= 0 || unitMass <= 0 || availableMass < unitMass) {
        return 0;
    }
    const int units = std::min(available, availableMass / unitMass);
    availableMass -= units * unitMass;
    return units;
}

} // namespace

int materialCargoMass(const MaterialInventory& materials)
{
    return std::max(0, materials.common)
        + std::max(0, materials.rare) * 2
        + std::max(0, materials.exotic) * 4;
}

int shipHoldCapacity(const GameState& state, const ContentCatalog&)
{
    constexpr int capacities[] {12, 16, 20, 24};
    return capacities[std::clamp(state.meta.shipHoldRank, 0, 3)];
}

int shipHoldUsed(const GameState& state)
{
    return materialCargoMass(state.meta.materials);
}

int shipHoldAvailable(const GameState& state, const ContentCatalog& catalog)
{
    return std::max(0, shipHoldCapacity(state, catalog) - shipHoldUsed(state));
}

MaterialInventory activeContractMaterialNeed(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId)
{
    MaterialInventory need;
    for (const ScenarioInstance& instance : state.meta.scenarios) {
        const ScenarioDefinition* authored = scenarioDefinitionForRuntimeId(
            state,
            catalog,
            instance.id);
        if (authored == nullptr) {
            continue;
        }
        const ScenarioDefinition resolved = resolveScenarioDefinition(*authored, instance);
        for (const ScenarioStepDefinition& step : resolved.steps) {
            if (step.completionEvent != ScenarioEventKind::SafeMaterialDelivered ||
                (!step.eventOriginId.empty() && step.eventOriginId != destinationId) ||
                scenarioStepState(state, catalog, instance.id, step.id) != ScenarioStepState::Active) {
                continue;
            }
            const ScenarioStepProgress* progress = findScenarioStepProgress(instance, step.id);
            const int remaining = std::max(
                0,
                step.requiredProgress - (progress == nullptr ? 0 : progress->progress));
            if (step.eventTargetId == "rare") {
                need.rare += remaining;
            } else if (step.eventTargetId == "exotic") {
                need.exotic += remaining;
            } else {
                need.common += remaining;
            }
        }
    }
    return need;
}

PayloadTransferPlan planPayloadTransfer(
    const MaterialInventory& source,
    const MaterialInventory& contractNeed,
    const MaterialInventory& existingShipHold,
    int holdCapacity)
{
    PayloadTransferPlan plan;
    plan.remainingAtSource = {
        std::max(0, source.common),
        std::max(0, source.rare),
        std::max(0, source.exotic)};
    plan.holdMassBefore = materialCargoMass(existingShipHold);

    plan.toContract.common = std::min(plan.remainingAtSource.common, std::max(0, contractNeed.common));
    plan.toContract.rare = std::min(plan.remainingAtSource.rare, std::max(0, contractNeed.rare));
    plan.toContract.exotic = std::min(plan.remainingAtSource.exotic, std::max(0, contractNeed.exotic));
    plan.remainingAtSource.common -= plan.toContract.common;
    plan.remainingAtSource.rare -= plan.toContract.rare;
    plan.remainingAtSource.exotic -= plan.toContract.exotic;

    int massAvailable = std::max(0, holdCapacity - plan.holdMassBefore);
    plan.toShipHold.common = allocateUnits(plan.remainingAtSource.common, 1, massAvailable);
    plan.remainingAtSource.common -= plan.toShipHold.common;
    plan.toShipHold.rare = allocateUnits(plan.remainingAtSource.rare, 2, massAvailable);
    plan.remainingAtSource.rare -= plan.toShipHold.rare;
    plan.toShipHold.exotic = allocateUnits(plan.remainingAtSource.exotic, 4, massAvailable);
    plan.remainingAtSource.exotic -= plan.toShipHold.exotic;
    plan.holdMassAfter = plan.holdMassBefore + materialCargoMass(plan.toShipHold);
    plan.capacityBlocked = materialCargoMass(plan.remainingAtSource) > 0;
    return plan;
}

void applyPayloadTransferPlan(
    GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId,
    const PayloadTransferPlan& plan)
{
    const auto record = [&](std::string materialId, int amount) {
        if (amount <= 0) {
            return;
        }
        (void)recordScenarioEvent(
            state,
            catalog,
            {ScenarioEventKind::SafeMaterialDelivered,
             {},
             {},
             std::string(destinationId),
             std::move(materialId),
             amount,
             0});
    };
    record("common", plan.toContract.common);
    record("rare", plan.toContract.rare);
    record("exotic", plan.toContract.exotic);
    add(state.meta.materials, plan.toShipHold);
}

bool validateCargoRequirements(const ContentCatalog& catalog, std::string* error)
{
    const auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    for (const ScenarioDefinition& scenario : catalog.scenarios) {
        for (const ScenarioStepDefinition& step : scenario.steps) {
            if (step.requiredProgress < 0) {
                return fail("negative material requirement in " + scenario.id + ":" + step.id);
            }
            if (step.completionEvent != ScenarioEventKind::SafeMaterialDelivered) {
                continue;
            }
            const int unitMass = step.eventTargetId == "exotic" ? 4 : (step.eventTargetId == "rare" ? 2 : 1);
            if (step.requiredProgress * unitMass > tuning::mining::rigCargoCapacityMass) {
                return fail("single-haul contract exceeds guaranteed rig capacity in " + scenario.id + ":" + step.id);
            }
        }
    }
    return true;
}

} // namespace rocket
