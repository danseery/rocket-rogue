#pragma once

#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"
#include "core/RefitPresentation.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace rocket {

inline std::string launchUpgradeTrackName(LaunchUpgradeKind kind)
{
    switch (kind) {
    case LaunchUpgradeKind::FuelTanks: return "Fuel Tanks";
    case LaunchUpgradeKind::FlightControls: return "Flight Controls";
    case LaunchUpgradeKind::Cooling: return "Engine Cooling";
    case LaunchUpgradeKind::Hull: return "Hull Plating";
    case LaunchUpgradeKind::None: break;
    }
    return "Launch Upgrade";
}

inline std::string launchUpgradeEffect(LaunchUpgradeKind kind, int rank)
{
    switch (kind) {
    case LaunchUpgradeKind::FuelTanks:
        return display::fixed(launchFuelCapacityForRank(rank), 0) + " transfer fuel";
    case LaunchUpgradeKind::FlightControls:
        return display::percent(launchControlChaosForRank(rank)) + " flight instability";
    case LaunchUpgradeKind::Cooling:
        return display::percent(launchPoweredHeatMultiplierForRank(rank)) +
            " powered heat / " +
            display::fixed(launchEngineOffCoolingForRank(rank) * 100.0, 0) +
            "%/s off cooling";
    case LaunchUpgradeKind::Hull:
        return display::fixed(
            tuning::launch::hullBaseIntegrity +
                static_cast<double>(rank) * tuning::launch::hullIntegrityPerRank,
            0) + " HP / " + display::percent(launchHullImpactMultiplierForRank(rank)) + " impact";
    case LaunchUpgradeKind::None:
        break;
    }
    return {};
}

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
    rows.push_back(detailPresentationHeader("Launch systems"));
    rows.push_back(detailPresentationRow(
        text::labels::transferFuel,
        display::fixed(launchFuelCapacity(state), 0) + " capacity"));
    rows.push_back(detailPresentationRow(
        "Expedition rig pack",
        display::fixed(tuning::research::expeditionRigPackFuel, 0) + " rig fuel"));
    rows.push_back(detailPresentationRow(text::labels::returnStage, std::string("RESERVED")));
    for (const LaunchUpgradeKind kind : {
             LaunchUpgradeKind::FuelTanks,
             LaunchUpgradeKind::FlightControls,
             LaunchUpgradeKind::Cooling,
             LaunchUpgradeKind::Hull}) {
        const int rank = launchUpgradeRank(state, kind);
        rows.push_back(detailPresentationRow(
            launchUpgradeTrackName(kind),
            (rank > 0 ? "RANK " + std::to_string(rank) : std::string("BASE")) +
                " / " + launchUpgradeEffect(kind, rank)));
    }
    rows.push_back(detailPresentationHeader("Installed ship systems"));
    for (const std::string& moduleId : state.meta.ownedModuleIds) {
        if (const ShipModule* module = catalog.findModule(moduleId)) {
            if (module->launchUpgradeKind != LaunchUpgradeKind::None) {
                continue;
            }
            const bool operational = std::find(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), moduleId) != state.run.equippedModuleIds.end();
            const bool builtIn = module->refitTrack == RefitTrack::None && module->unlockKey == content::unlock::starter;
            const std::string status = builtIn ? "Built in" : (operational ? "Installed" : "Offline this expedition");
            rows.push_back(detailPresentationRow(status, shipModuleSummary(*module)));
        }
    }

    return rows;
}

} // namespace rocket
