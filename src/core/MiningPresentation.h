#pragma once

#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/GameUi.h"
#include "core/MiningSystem.h"
#include "core/PanelPresentation.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace rocket {

struct MiningRunPresentation {
    std::vector<PanelMetricPresentation> metrics;
    std::vector<PanelMetricPresentation> payloadMetrics;
    std::vector<DetailPresentationRow> details;
    std::vector<PanelButtonPresentation> actions;
    bool failurePending = false;
    std::string failureTitle;
    std::string failureBody;
};

inline std::string miningOxygenValue(double seconds)
{
    return std::to_string(static_cast<int>(std::ceil(std::max(0.0, seconds)))) + "s";
}

inline std::string miningFuelCycleValue(double seconds)
{
    const double wrapped = std::fmod(std::max(0.0, seconds), tuning::mining::fuelSecondsPerUnit);
    const double remaining = tuning::mining::fuelSecondsPerUnit - wrapped;
    return std::to_string(static_cast<int>(std::ceil(std::clamp(remaining, 0.0, tuning::mining::fuelSecondsPerUnit)))) + "s";
}

inline std::string miningToughnessValue(const MiningRunState& mining)
{
    if (mining.targetMaxToughness <= 0.0) {
        return "Clear";
    }
    return display::percent(std::clamp(mining.targetRemainingToughness / mining.targetMaxToughness, 0.0, 1.0));
}

inline std::string miningDroneSummary(const MiniDroneLoadoutEffects& drones)
{
    if (drones.names.empty()) {
        return "None";
    }
    std::string summary = drones.names.front();
    for (std::size_t i = 1; i < drones.names.size(); ++i) {
        summary += ", " + drones.names[i];
    }
    return summary;
}

inline std::string hostileTunnelSummary(const MiningTerrain& terrain)
{
    int structures = 0;
    int rooms = 0;
    MiningEnemyType firstEnemy = MiningEnemyType::None;
    for (const MiningCell& cell : terrain.cells) {
        if (cell.feature != MiningCellFeature::None) {
            structures += 1;
        }
        if (cell.feature == MiningCellFeature::TreasureVault || cell.feature == MiningCellFeature::MinibossLair || cell.feature == MiningCellFeature::HiveNest) {
            rooms += 1;
        }
        if (firstEnemy == MiningEnemyType::None && cell.enemy != MiningEnemyType::None) {
            firstEnemy = cell.enemy;
        }
    }
    if (structures <= 0) {
        return "None";
    }
    std::string summary = std::to_string(structures) + " pre-dug cells";
    if (rooms > 0) {
        summary += ", " + std::to_string(rooms) + " room cells";
    }
    if (firstEnemy != MiningEnemyType::None) {
        summary += ", " + std::string(miningEnemyTypeName(firstEnemy));
    }
    return summary;
}

inline int activeMiningEnemyCount(const MiningRunState& mining)
{
    return static_cast<int>(std::count_if(mining.enemies.begin(), mining.enemies.end(), [](const MiningEnemy& enemy) {
        return enemy.active;
    }));
}

inline MiningRunPresentation miningRunPresentation(const GameState& state, const ContentCatalog& catalog)
{
    const MiningRunState& mining = state.run.mining;
    const MiningDrillStats stats = miningDrillStats(state, catalog);
    const MiniDroneLoadoutEffects drones = miniDroneLoadoutEffects(state, catalog);
    const SurfaceExpeditionState& surface = state.run.surfaceExpedition;
    const bool arkKnown = arkDiscovered(state);
    const double extractionDelta = std::clamp(
        mining.hazardDelta + static_cast<double>(std::max(0, mining.cargo)) * tuning::mining::cargoExtractionRiskScale - stats.extractionRiskRelief,
        0.0,
        tuning::mining::maxMiningHazardDelta);
    MiningRunPresentation presentation;
    presentation.failurePending = mining.failurePending;
    presentation.failureTitle = mining.drillIntegrity <= 0.0 ? "Drill failure" : "Emergency recall";
    presentation.failureBody = mining.failureMessage.empty()
        ? std::string("Mining drone recall is in progress.")
        : mining.failureMessage;
    presentation.metrics = {
        panelMetric(text::labels::oxygen, miningOxygenValue(mining.oxygenSeconds)),
        panelMetric(text::fuel::reserveLabel(arkKnown), std::to_string(surface.sharedFuel) + "/" + std::to_string(std::max(1, surface.sharedFuelCapacity))),
        panelMetric("Next fuel", miningFuelCycleValue(mining.fuelBurnSeconds)),
        panelMetric(text::labels::depth, std::to_string(mining.depthZone)),
        panelMetric(text::labels::drillHeat, display::percent(mining.drillHeat)),
        panelMetric(text::labels::drillIntegrity, display::percent(mining.drillIntegrity)),
        panelMetric(text::labels::extractionRisk, display::signedPercent(extractionDelta)),
        panelMetric("Enemies", std::to_string(activeMiningEnemyCount(mining))),
        panelMetric("Drones", drones.names.empty() ? "0" : std::to_string(static_cast<int>(drones.names.size()))),
        panelMetric(text::labels::targetMaterial, std::string(miningMaterialName(mining.targetMaterial))),
        panelMetric(text::labels::toughness, miningToughnessValue(mining))
    };
    presentation.payloadMetrics = {
        panelMetric(text::labels::cargo, std::to_string(mining.cargo)),
        panelMetric(text::labels::commonMaterials, std::to_string(mining.temporaryMaterials.common)),
        panelMetric(text::labels::rareMaterials, std::to_string(mining.temporaryMaterials.rare)),
        panelMetric(text::labels::exoticMaterials, std::to_string(mining.temporaryMaterials.exotic)),
        panelMetric(text::labels::artifacts, std::to_string(mining.temporaryArtifacts.size()))
    };
    presentation.details = {
        detailPresentationRow("Controls", std::string("WASD/arrows move; mouse or Space drills; E scans; R stows; Esc aborts.")),
        detailPresentationRow("Site", std::string(surfaceSiteProfileName(surface.siteProfile))),
        detailPresentationRow("Drill power", display::fixed(stats.power, 1)),
        detailPresentationRow("Scanner radius", display::fixed(stats.scannerRadius, 1)),
        detailPresentationRow(text::fuel::reserveLabel(arkKnown), std::to_string(surface.sharedFuel) + "/" + std::to_string(std::max(1, surface.sharedFuelCapacity)) + " available for this dig"),
        detailPresentationRow("Fuel spent this dig", std::to_string(mining.fuelSpent)),
        detailPresentationRow("Fuel draw", text::fuel::drawDetail(arkKnown, static_cast<int>(std::round(tuning::mining::fuelSecondsPerUnit)))),
        detailPresentationRow("Drone loadout", miningDroneSummary(drones)),
        detailPresentationRow("Drone auto-mining", drones.passiveMiningRate > 0.0 ? ("+" + display::fixed(drones.passiveMiningRate * 60.0, 1) + " common/min") : "None"),
        detailPresentationRow("Passive defense", display::fixed(tuning::mining::baseDefenseDamagePerSecond + drones.sentryDamagePerSecond, 1) + " DPS; " + display::percent(drones.enemyDamageRelief) + " shield relief"),
        detailPresentationRow("Drone oxygen reserve", drones.oxygenSeconds > 0.0 ? ("+" + std::to_string(static_cast<int>(std::round(drones.oxygenSeconds))) + "s") : "None"),
        detailPresentationRow("Drone stability", drones.hardRockBounceRelief > 0.0 ? display::percent(drones.hardRockBounceRelief) + " less hard-rock bounce" : "None"),
        detailPresentationRow("Hostile tunnels", hostileTunnelSummary(mining.terrain)),
        detailPresentationRow("Enemies defeated", std::to_string(mining.enemiesDefeated)),
        detailPresentationRow("Ore efficiency", display::signedPercent(stats.oreYieldChance)),
        detailPresentationRow("Heat control", display::fixed(stats.heatCoolingPerSecond, 2)),
        detailPresentationRow("Drill protection", display::signedPercent(stats.integrityRelief)),
        detailPresentationRow("Survey footprint", std::to_string(stats.terrainWidth) + "x" + std::to_string(stats.terrainHeight)),
        detailPresentationRow("Run target", text::fuel::miningRunTarget(arkKnown)),
        detailPresentationRow("Depth pressure", std::string("Deeper zones have tougher terrain, richer pockets, and more extraction risk."))
    };
    if (mining.failurePending) {
        presentation.actions = {
            disabledPanelButton("Drill disabled")
        };
    } else {
        presentation.actions = {
            panelActionButton(text::buttons::pulseScanner, ui::actions::miningScanner, "warn"),
            panelActionButton(text::buttons::stowPayload, ui::actions::miningStow, "ok"),
            panelActionButton(text::buttons::abortMining, ui::actions::miningAbort, "danger")
        };
    }
    return presentation;
}

} // namespace rocket
