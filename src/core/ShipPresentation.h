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
    rows.push_back(detailPresentationHeader("Installed ship systems"));
    for (const std::string& moduleId : state.meta.ownedModuleIds) {
        if (const ShipModule* module = catalog.findModule(moduleId)) {
            const bool operational = std::find(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), moduleId) != state.run.equippedModuleIds.end();
            const bool builtIn = module->refitTrack == RefitTrack::None && module->unlockKey == content::unlock::starter;
            const std::string status = builtIn ? "Built in" : (operational ? "Installed" : "Offline this expedition");
            rows.push_back(detailPresentationRow(status, shipModuleSummary(*module)));
        }
    }

    return rows;
}

} // namespace rocket
