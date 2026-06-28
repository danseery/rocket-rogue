#pragma once

#include "core/Content.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/PanelPresentation.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rocket {

struct InventoryItemPresentation {
    std::string glyph;
    std::string title;
    std::string detail;
    std::string count;
    std::string cssClass;
};

struct InventorySectionPresentation {
    std::string title;
    std::string detail;
    std::vector<InventoryItemPresentation> items;
};

struct InventoryPresentation {
    std::vector<PanelMetricPresentation> summary;
    std::vector<InventorySectionPresentation> sections;
};

inline void addInventoryItem(
    InventorySectionPresentation& section,
    std::string glyph,
    std::string title,
    std::string detail,
    std::string count,
    std::string cssClass)
{
    section.items.push_back({
        std::move(glyph),
        std::move(title),
        std::move(detail),
        std::move(count),
        std::move(cssClass)
    });
}

inline std::string artifactDisplayName(const ArtifactRecord& artifact)
{
    std::string name = artifact.id;
    std::replace(name.begin(), name.end(), '_', ' ');
    return name;
}

inline InventoryPresentation inventoryPresentation(const GameState& state, const ContentCatalog& catalog)
{
    InventoryPresentation presentation;
    const MaterialInventory& owned = state.meta.materials;
    const int artifactCount = static_cast<int>(state.meta.artifacts.size());
    const int identifiedArtifacts = static_cast<int>(std::count_if(
        state.meta.artifacts.begin(),
        state.meta.artifacts.end(),
        [](const ArtifactRecord& artifact) {
            return artifact.identified;
        }));

    presentation.summary = {
        panelMetric(text::labels::missionCredits, display::credits(state.run.credits)),
        panelMetric(text::labels::blueprints, std::to_string(state.meta.blueprintProgress)),
        panelMetric(text::labels::commonMaterials, std::to_string(owned.common)),
        panelMetric(text::labels::rareMaterials, std::to_string(owned.rare)),
        panelMetric(text::labels::exoticMaterials, std::to_string(owned.exotic)),
        panelMetric(text::labels::artifacts, std::to_string(artifactCount))
    };

    if (state.run.mining.active || state.run.surfaceExpedition.active) {
        const MaterialInventory& surfaceMaterials = state.run.mining.active
            ? state.run.mining.temporaryMaterials
            : state.run.surfaceExpedition.temporaryMaterials;
        const std::vector<ArtifactRecord>& surfaceArtifacts = state.run.mining.active
            ? state.run.mining.temporaryArtifacts
            : state.run.surfaceExpedition.temporaryArtifacts;
        InventorySectionPresentation payload {
            "Current payload",
            "Cargo that must still be extracted before it joins the permanent inventory.",
            {}
        };
        addInventoryItem(payload, "CM", text::labels::commonMaterials.data(), "Recovered this sortie", std::to_string(surfaceMaterials.common), "common");
        addInventoryItem(payload, "RM", text::labels::rareMaterials.data(), "Recovered this sortie", std::to_string(surfaceMaterials.rare), "rare");
        addInventoryItem(payload, "EX", text::labels::exoticMaterials.data(), "Recovered this sortie", std::to_string(surfaceMaterials.exotic), "exotic");
        addInventoryItem(payload, "AR", text::labels::artifacts.data(), "Recovered this sortie", std::to_string(surfaceArtifacts.size()), "artifact");
        presentation.sections.push_back(std::move(payload));
    }

    InventorySectionPresentation resources {
        "Recovered resources",
        "Banked materials available for research, Drone Bay work, and special components.",
        {}
    };
    addInventoryItem(resources, "BP", text::labels::blueprints.data(), "Research and unlock progress", std::to_string(state.meta.blueprintProgress), "blueprint");
    addInventoryItem(resources, "CM", text::labels::commonMaterials.data(), "Bulk build stock", std::to_string(owned.common), "common");
    addInventoryItem(resources, "RM", text::labels::rareMaterials.data(), "Refined specialty stock", std::to_string(owned.rare), "rare");
    addInventoryItem(resources, "EX", text::labels::exoticMaterials.data(), "Deep-field material", std::to_string(owned.exotic), "exotic");
    presentation.sections.push_back(std::move(resources));

    InventorySectionPresentation artifacts {
        "Artifacts",
        "Recovered anomaly objects for later decoding and Ark progression.",
        {}
    };
    if (state.meta.artifacts.empty()) {
        addInventoryItem(artifacts, "AR", "No artifacts", "None recovered yet", "0", "artifact");
    } else {
        for (const ArtifactRecord& artifact : state.meta.artifacts) {
            const Destination* origin = catalog.findDestination(artifact.originDestinationId);
            addInventoryItem(
                artifacts,
                "AR",
                artifact.identified ? artifactDisplayName(artifact) : "Unidentified artifact",
                origin != nullptr ? origin->name : artifact.originDestinationId,
                artifact.identified ? "Decoded" : "Sealed",
                "artifact");
        }
    }
    artifacts.detail += " " + std::to_string(identifiedArtifacts) + "/" + std::to_string(artifactCount) + " decoded.";
    presentation.sections.push_back(std::move(artifacts));

    InventorySectionPresentation modules {
        "Ship tech",
        "Permanent shipyard improvements and currently stored modules.",
        {}
    };
    if (state.meta.ownedModuleIds.empty()) {
        addInventoryItem(modules, "ST", "Starter hardware", "Baseline shuttle parts", "Built in", "module");
    } else {
        for (const std::string& moduleId : state.meta.ownedModuleIds) {
            const ShipModule* module = catalog.findModule(moduleId);
            if (module == nullptr) {
                continue;
            }
            addInventoryItem(
                modules,
                std::string(toString(module->slot)).substr(0, 1),
                module->name,
                std::string(toString(module->slot)) + " - " + std::string(toString(module->rarity)),
                std::find(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), moduleId) != state.run.equippedModuleIds.end() ? "Equipped" : "Stored",
                "module");
        }
    }
    presentation.sections.push_back(std::move(modules));

    InventorySectionPresentation drones {
        "Drone bay",
        "Persistent mining support hardware.",
        {}
    };
    addInventoryItem(drones, "DB", "Drone slots", "Installed bay capacity", std::to_string(state.meta.droneBaySlots), "drone");
    for (const std::string& droneId : state.meta.ownedDroneIds) {
        const MiniDrone* drone = catalog.findMiniDrone(droneId);
        if (drone == nullptr) {
            continue;
        }
        addInventoryItem(
            drones,
            drone->name.empty() ? "DR" : std::string(1, drone->name.front()),
            drone->name,
            std::string(toString(drone->role)) + " - " + std::string(toString(drone->rarity)),
            std::find(state.meta.equippedDroneIds.begin(), state.meta.equippedDroneIds.end(), droneId) != state.meta.equippedDroneIds.end() ? "Equipped" : "Owned",
            "drone");
    }
    presentation.sections.push_back(std::move(drones));

    return presentation;
}

} // namespace rocket
