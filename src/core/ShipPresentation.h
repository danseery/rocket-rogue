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

struct LaunchUpgradeInstallPresentation {
    LaunchUpgradeKind kind = LaunchUpgradeKind::None;
    std::string title;
    int currentRank = 0;
    int nextRank = 0;
    std::string currentEffect;
    std::string nextEffect;
    int cost = 0;
    PanelButtonPresentation action;
};

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
        return display::fixed(launchFuelCapacityForRank(rank), 0) + " fuel";
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

inline std::string launchUpgradeLockLabel(LaunchUpgradeKind kind, int nextRank)
{
    if (nextRank <= 1) {
        switch (kind) {
        case LaunchUpgradeKind::FuelTanks: return "Complete fuel test";
        case LaunchUpgradeKind::FlightControls: return "Complete controls test";
        case LaunchUpgradeKind::Cooling: return "Complete heat test";
        case LaunchUpgradeKind::Hull: return "Complete asteroid test";
        case LaunchUpgradeKind::None: break;
        }
    }
    if (kind == LaunchUpgradeKind::FuelTanks) {
        return nextRank == 2 ? "Reveal Mars route" : "Reveal Jupiter route";
    }
    const int requiredTier = kind == LaunchUpgradeKind::Hull
        ? nextRank + 1
        : (kind == LaunchUpgradeKind::Cooling ? nextRank : nextRank - 1);
    switch (requiredTier) {
    case 1: return "Reach Moon";
    case 2: return "Reach Mars";
    case 3: return "Reach Jupiter";
    case 4: return "Reach Saturn";
    default: return "Locked";
    }
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

inline std::vector<LaunchUpgradeInstallPresentation> launchUpgradeInstallPresentation(
    const GameState& state,
    const ContentCatalog& catalog)
{
    std::vector<LaunchUpgradeInstallPresentation> upgrades;
    for (const LaunchUpgradeKind kind : {
             LaunchUpgradeKind::FuelTanks,
             LaunchUpgradeKind::FlightControls,
             LaunchUpgradeKind::Cooling,
             LaunchUpgradeKind::Hull}) {
        const int currentRank = launchUpgradeRank(state, kind);
        const ShipModule* module = nextLaunchUpgrade(state, catalog, kind);
        const int nextRank = module == nullptr ? currentRank : module->launchUpgradeRank;
        const int cost = static_cast<int>(tuning::launchProgression::upgradeCost);
        PanelButtonPresentation action;
        if (module == nullptr) {
            action = disabledPanelButton("MAX");
        } else if (!launchUpgradeUnlocked(state, kind, nextRank)) {
            action = disabledPanelButton(launchUpgradeLockLabel(kind, nextRank));
        } else if (!canInstallLaunchUpgrade(state, catalog, kind)) {
            action = disabledPanelButton(text::needCredits(cost));
        } else {
            action = panelActionButton(
                text::buttons::install,
                ui::actions::installLaunchUpgrade(static_cast<int>(kind)),
                "ok");
        }
        upgrades.push_back({
            kind,
            launchUpgradeTrackName(kind),
            currentRank,
            nextRank,
            launchUpgradeEffect(kind, currentRank),
            module == nullptr ? std::string("Maximum rank") : launchUpgradeEffect(kind, nextRank),
            cost,
            std::move(action)
        });
    }
    return upgrades;
}

} // namespace rocket
