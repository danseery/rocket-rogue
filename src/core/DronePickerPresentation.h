#pragma once

#include "core/ResearchPresentation.h"

namespace rocket {

struct DronePickerEntry {
    int index = -1;
    bool available = false;
    int owned = 0;
    int equipped = 0;
    int rank = 1;
    std::string name, purpose, art, unlockRequirement;
    std::vector<PanelMetricPresentation> capabilities;
};

struct DronePickerPresentation {
    ui::DroneOpsSelection selection;
    std::vector<DronePickerEntry> entries;
    int availableCount = 0;
    int lockedCount = 0;
    bool alreadyEquipped = false;
    bool paid = false;
    MaterialInventory cost;
    std::vector<PanelMetricPresentation> balances;
    std::vector<DetailPresentationRow> effects;
    std::string expansionRequirement;
    std::string shortfall;
    PanelButtonPresentation action;
};

inline std::string dronePurpose(MiniDroneRole role)
{
    switch (role) {
    case MiniDroneRole::Mining: return "Drills revealed ore nearby.";
    case MiniDroneRole::Resource: return "Hauls loose ore to the ship.";
    case MiniDroneRole::Survey: return "Scouts ahead and scans ore.";
    case MiniDroneRole::Hazard: return "Treats environmental hazards.";
    case MiniDroneRole::Attack: return "Fires at nearby threats.";
    case MiniDroneRole::Defense: return "Shields against incoming damage.";
    }
    return {};
}

inline std::string droneRewardRequirement(const ContentCatalog& catalog, const MiniDrone* drone)
{
    if (drone) {
        for (const auto& research : catalog.researchProjects)
            if (research.rewardUnlockKey == drone->unlockKey)
                return "Research " + research.name + ".";
    }
    for (const auto& scenario : catalog.scenarios) {
        for (const auto& step : scenario.steps) {
            for (const auto& reward : step.rewards) {
                const bool matches = drone
                    ? (reward.kind == ScenarioRewardKind::SupportDrone && reward.id == drone->id) ||
                      (reward.kind == ScenarioRewardKind::UnlockKey && reward.id == drone->unlockKey)
                    : reward.kind == ScenarioRewardKind::DroneBaySlots && reward.amount == 2;
                if (matches) return "Claim " + step.title + (step.location.empty() ? "." : " / " + step.location + ".");
            }
        }
    }
    return drone ? "Requires " + unlockDisplayName(drone->unlockKey) + "." : "Claim the next mission's bay expansion.";
}

inline DronePickerPresentation dronePickerPresentation(
    const GameState& state, const ContentCatalog& catalog, ui::DroneOpsSelection selection = {})
{
    DronePickerPresentation result;
    const int count = static_cast<int>(state.meta.equippedDroneIds.size());
    const int slots = std::max(0, state.meta.droneBaySlots);
    if (selection.slot < 0) selection.slot = count < slots ? count : 0;
    selection.slot = std::clamp(selection.slot, 0, std::max(0, std::min(count, slots - 1)));
    for (int i = 0; i < static_cast<int>(catalog.miniDrones.size()); ++i) {
        const auto& drone = catalog.miniDrones[i];
        DronePickerEntry entry;
        entry.index = i;
        entry.available = isMiniDroneUnlocked(state.meta, drone);
        entry.owned = ownedMiniDroneCount(state, drone.id);
        entry.equipped = equippedMiniDroneCount(state, drone.id);
        entry.rank = expeditionDroneRank(state, drone.id);
        entry.name = drone.name;
        entry.purpose = dronePurpose(drone.role);
        const auto roleClass = miniDroneRoleClass(drone.role);
        entry.art = "../art/mini-drone-" + roleClass.substr(5) + ".png";
        entry.unlockRequirement = droneRewardRequirement(catalog, &drone);
        entry.capabilities = miniDroneChips(scaledMiniDroneStats(drone.stats, entry.rank), entry.rank, drone.role, false);
        if (drone.role == MiniDroneRole::Resource) {
            const double transferSeconds = tuning::mining::resourceDroneTransferSeconds /
                (1.0 + static_cast<double>(entry.rank - 1) * tuning::mining::resourceDroneUpgradeRateBonus);
            entry.capabilities = {
                panelMetric("Haul", std::to_string(tuning::mining::resourceDroneCapacityChunks) + " chunks"),
                panelMetric("Collection", display::fixed(transferSeconds, 2) + "s / chunk"),
                panelMetric("Reach", display::fixed(tuning::mining::resourceDroneCollectionRadiusCells, 1) + " cells")
            };
        }
        if (entry.capabilities.size() > 3) entry.capabilities.resize(3);
        if (entry.available) ++result.availableCount; else ++result.lockedCount;
        result.entries.push_back(std::move(entry));
    }
    const auto matchesView = [&](int index) {
        return index >= 0 && index < static_cast<int>(result.entries.size()) &&
            result.entries[index].available != selection.locked;
    };
    if (!matchesView(selection.drone)) {
        selection.drone = -1;
        if (!selection.locked && selection.slot < count) {
            for (const auto& entry : result.entries)
                if (catalog.miniDrones[entry.index].id == state.meta.equippedDroneIds[selection.slot] && entry.available)
                    selection.drone = entry.index;
        }
        if (selection.drone < 0)
            for (const auto& entry : result.entries) if (entry.available != selection.locked) {
                selection.drone = entry.index;
                break;
            }
    }
    selection.expansion = selection.expansion && slots >= 2 && slots < 6;
    result.selection = selection;
    if (slots < 2) result.expansionRequirement = "Slot 2: " + droneRewardRequirement(catalog, nullptr);
    result.action = disabledPanelButton("Choose a drone");
    if (selection.expansion) {
        result.paid = true;
        result.cost = droneSlotUpgradeCost(slots + 1);
        result.action = panelActionButton("Unlock slot " + std::to_string(slots + 1), ui::actions::upgradeDroneSlot, "ok");
        result.action.enabled = canUpgradeDroneSlot(state);
    } else if (selection.drone >= 0) {
        const auto& entry = result.entries[selection.drone];
        const auto& drone = catalog.miniDrones[selection.drone];
        result.alreadyEquipped = selection.slot < count && state.meta.equippedDroneIds[selection.slot] == drone.id;
        result.paid = entry.available && !result.alreadyEquipped && entry.owned <= entry.equipped;
        if (result.paid) result.cost = miniDroneAdditionalUnitCost(drone);
        if (!entry.available) result.action = disabledPanelButton("Locked");
        else if (result.alreadyEquipped) result.action = disabledPanelButton("Equipped");
        else {
            const auto* outgoing = selection.slot < count ? catalog.findMiniDrone(state.meta.equippedDroneIds[selection.slot]) : nullptr;
            const std::string label = outgoing
                ? (result.paid ? "Build & replace " : "Replace ") + std::string(toString(outgoing->role))
                : (result.paid ? "Build & equip to slot " : "Equip to slot ") + std::to_string(selection.slot + 1);
            result.action = panelActionButton(label,
                std::string(ui::actions::assignDroneSlotPrefix) + std::to_string(selection.slot) + ":" + std::to_string(selection.drone), "ok");
            result.action.enabled = slots > 0 && droneBayUnlocked(state) && (!result.paid || canAffordMaterials(state.meta.materials, result.cost));
        }
        // Simulate only the equipment list for the preview; never run commands,
        // spend materials or grant recipes while browsing.
        GameState proposed = state;
        if (entry.available && !result.alreadyEquipped && slots > 0) {
            if (selection.slot < count) proposed.meta.equippedDroneIds[selection.slot] = drone.id;
            else proposed.meta.equippedDroneIds.push_back(drone.id);
            std::erase_if(proposed.run.expedition.progression.droneModuleAssignments,
                [&](const auto& graft) { return graft.equippedFrame == selection.slot; });
        }
        for (const auto& graft : state.run.expedition.progression.droneModuleAssignments) {
            if (graft.equippedFrame != selection.slot) continue;
            const auto module = std::find_if(catalog.droneModules.begin(), catalog.droneModules.end(),
                [&](const auto& candidate) { return candidate.kind == graft.module; });
            if (module != catalog.droneModules.end()) result.effects.push_back(detailPresentationRow(module->name,
                result.alreadyEquipped ? droneModuleEffectSummary(module->kind, entry.rank) : "Removed when this drone is replaced."));
        }
        for (const auto& synergyId : state.run.expedition.progression.selectedSynergyIds) {
            const auto* synergy = catalog.findDroneSynergy(synergyId);
            if (!synergy) continue;
            const auto recipeFor = [&](const GameState& candidate) {
                auto recipe = droneBuildRecipe(candidate, catalog, synergy->name, synergy->requiredRoles,
                    synergy->description, synergy->signatureKind != MiniDroneSignatureKind::None);
                if (!hasUnlock(candidate.meta, synergy->requiredUnlock)) {
                    recipe.active = false;
                    recipe.status = "Dormant / research locked";
                } else if (!recipe.active) recipe.status = "Dormant / " + recipe.status;
                return recipe;
            };
            const auto oldRecipe = recipeFor(state);
            const auto nextRecipe = recipeFor(proposed);
            const bool changed = oldRecipe.active != nextRecipe.active;
            const bool relevant = std::find(synergy->requiredRoles.begin(), synergy->requiredRoles.end(), drone.role) != synergy->requiredRoles.end();
            if (entry.available && (changed || relevant)) result.effects.push_back(detailPresentationRow(synergy->name,
                (changed ? (nextRecipe.active ? "Activates. " : "Becomes dormant. ") : nextRecipe.status + ". ") + synergy->description));
        }
    }
    if (result.paid) {
        const auto addCost = [&](const char* label, int have, int required) {
            if (required <= 0) return;
            result.balances.push_back(panelMetric(label, std::to_string(have) + " / " + std::to_string(required)));
            if (have < required) {
                if (!result.shortfall.empty()) result.shortfall += ", ";
                result.shortfall += std::to_string(required - have) + " " + label;
            }
        };
        addCost("Common", state.meta.materials.common, result.cost.common);
        addCost("Rare", state.meta.materials.rare, result.cost.rare);
        addCost("Exotic", state.meta.materials.exotic, result.cost.exotic);
        if (!result.shortfall.empty()) result.shortfall = "Need " + result.shortfall + " more.";
    }
    return result;
}
} // namespace rocket
