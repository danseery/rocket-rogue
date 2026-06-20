#pragma once

#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"
#include "core/RefitPresentation.h"

#include <algorithm>
#include <string>
#include <vector>

namespace rocket {

inline std::string shipModuleSummary(const ShipModule& module)
{
    return module.name + " (" + std::string(toString(module.rarity)) + ")";
}

inline std::vector<DetailPresentationRow> shipDetailsPresentation(const GameState& state, const ContentCatalog& catalog)
{
    std::vector<DetailPresentationRow> rows;
    const ModuleStats stats = aggregateShipStats(state, catalog);

    for (const ModuleStatDisplay& stat : moduleStatDisplays(stats)) {
        if (stat.showInShipDetails) {
            rows.push_back(detailPresentationRow(stat.detailLabel, display::money(stat.value)));
        }
    }

    rows.push_back(detailPresentationRow(text::moduleStats::damage, display::wholePercent(state.run.shipDamage)));
    rows.push_back(detailPresentationHeader(text::panel::details::equippedShipUpgrades));
    for (const std::string& moduleId : state.run.equippedModuleIds) {
        if (const ShipModule* module = catalog.findModule(moduleId)) {
            rows.push_back(detailPresentationRow(toString(module->slot), shipModuleSummary(*module)));
        }
    }

    rows.push_back(detailPresentationHeader(text::panel::details::storedShipUpgrades));
    bool hasStoredModule = false;
    for (const std::string& moduleId : state.run.inventoryModuleIds) {
        const bool equipped = std::find(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), moduleId) != state.run.equippedModuleIds.end();
        if (!equipped) {
            if (const ShipModule* module = catalog.findModule(moduleId)) {
                hasStoredModule = true;
                rows.push_back(detailPresentationRow(toString(module->slot), shipModuleSummary(*module)));
            }
        }
    }

    if (!hasStoredModule) {
        rows.push_back(detailPresentationRow(text::panel::details::inventory, text::panel::noSpareModules));
    }

    return rows;
}

} // namespace rocket
