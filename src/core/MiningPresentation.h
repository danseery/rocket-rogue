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

inline std::string miningToughnessValue(const MiningRunState& mining)
{
    if (mining.targetMaxToughness <= 0.0) {
        return "Clear";
    }
    return display::percent(std::clamp(mining.targetRemainingToughness / mining.targetMaxToughness, 0.0, 1.0));
}

inline MiningRunPresentation miningRunPresentation(const GameState& state, const ContentCatalog& catalog)
{
    const MiningRunState& mining = state.run.mining;
    const MiningDrillStats stats = miningDrillStats(state, catalog);
    const SurfaceExpeditionState& surface = state.run.surfaceExpedition;
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
        panelMetric(text::labels::depth, std::to_string(mining.depthZone)),
        panelMetric(text::labels::drillHeat, display::percent(mining.drillHeat)),
        panelMetric(text::labels::drillIntegrity, display::percent(mining.drillIntegrity)),
        panelMetric(text::labels::cargo, std::to_string(mining.cargo)),
        panelMetric(text::labels::extractionRisk, display::signedPercent(extractionDelta)),
        panelMetric(text::labels::commonMaterials, std::to_string(mining.temporaryMaterials.common)),
        panelMetric(text::labels::rareMaterials, std::to_string(mining.temporaryMaterials.rare)),
        panelMetric(text::labels::exoticMaterials, std::to_string(mining.temporaryMaterials.exotic)),
        panelMetric(text::labels::artifacts, std::to_string(mining.temporaryArtifacts.size())),
        panelMetric(text::labels::targetMaterial, std::string(miningMaterialName(mining.targetMaterial))),
        panelMetric(text::labels::toughness, miningToughnessValue(mining))
    };
    presentation.details = {
        detailPresentationRow("Controls", std::string("WASD/arrows move; mouse or Space drills; E scans; R stows; Esc aborts.")),
        detailPresentationRow("Site", std::string(surfaceSiteProfileName(surface.siteProfile))),
        detailPresentationRow("Drill power", display::fixed(stats.power, 1)),
        detailPresentationRow("Scanner radius", display::fixed(stats.scannerRadius, 1)),
        detailPresentationRow("Ore efficiency", display::signedPercent(stats.oreYieldChance)),
        detailPresentationRow("Heat control", display::fixed(stats.heatCoolingPerSecond, 2)),
        detailPresentationRow("Drill protection", display::signedPercent(stats.integrityRelief)),
        detailPresentationRow("Survey footprint", std::to_string(stats.terrainWidth) + "x" + std::to_string(stats.terrainHeight)),
        detailPresentationRow("Run target", std::string("Bank useful payload in 1-3 minutes, then extract from the surface phase.")),
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
