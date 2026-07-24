#pragma once

#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameUi.h"
#include "core/PanelPresentation.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace rocket {

struct SurfaceScanRailPresentation {
    std::string kicker = "OREBIT";
    std::string title = "Survey Scan";
    std::string objective = "Read the layer, then bank";
    std::array<PanelMetricPresentation, 3> metrics;
    std::string signal;
    int signalPercent = 0;
    std::string layerReadout;
    std::string layerCssClass;
    std::vector<PanelButtonPresentation> actions;
};

struct SurfaceScanProspectReadout {
    std::string label;
    int amount = 0;
};

inline SurfaceScanProspectReadout dominantSurfaceScanProspect(const SurfaceDepthProspect& prospect)
{
    // Prefer the rarer result when equal quantities are mapped. This keeps the
    // single-line readout useful without changing or collapsing the stored
    // forecast data.
    const std::array<SurfaceScanProspectReadout, 4> candidates {{
        {"COMMON", std::max(0, prospect.possibleMaterials.common)},
        {"RARE", std::max(0, prospect.possibleMaterials.rare)},
        {"EXOTIC", std::max(0, prospect.possibleMaterials.exotic)},
        {"ARTIFACT", std::max(0, prospect.possibleArtifacts)}
    }};
    SurfaceScanProspectReadout result;
    for (const SurfaceScanProspectReadout& candidate : candidates) {
        if (candidate.amount >= result.amount && candidate.amount > 0) {
            result = candidate;
        }
    }
    return result;
}

inline std::string surfaceScanProspectCssClass(std::string_view label)
{
    if (label == "RARE") {
        return "rare";
    }
    if (label == "EXOTIC") {
        return "exotic";
    }
    if (label == "ARTIFACT") {
        return "artifact";
    }
    return "common";
}

inline SurfaceScanRailPresentation surfaceScanRailPresentation(const GameState& state)
{
    const SurfaceScanRunState& scan = state.run.surfaceScan;
    SurfaceScanRailPresentation presentation;
    presentation.metrics = {{
        panelMetric("PULSES", std::to_string(scan.pulses) + "/" + std::to_string(std::max(1, scan.maxPulses))),
        panelMetric("FORECAST", scan.cargo > 0 ? "+" + std::to_string(scan.cargo) : "0"),
        panelMetric("RISK", display::percent(scan.bustRisk))
    }};
    presentation.signalPercent = std::clamp(
        static_cast<int>(scan.signal * 100.0 + 0.5),
        0,
        100);
    presentation.signal = std::to_string(presentation.signalPercent) + "%";

    if (scan.depthProspects.empty()) {
        presentation.layerReadout = "NO LAYER READ";
        presentation.layerCssClass = "empty";
    } else {
        const SurfaceDepthProspect& latest = scan.depthProspects.back();
        const SurfaceScanProspectReadout dominant = dominantSurfaceScanProspect(latest);
        presentation.layerReadout = "LAYER +" + std::to_string(std::max(0, latest.depthOffset)) + ": ";
        if (dominant.amount > 0) {
            presentation.layerReadout += dominant.label + " +" + std::to_string(dominant.amount);
            presentation.layerCssClass = surfaceScanProspectCssClass(dominant.label);
        } else {
            presentation.layerReadout += "CLEAR";
            presentation.layerCssClass = "empty";
        }
    }

    if (scan.busted) {
        presentation.actions.push_back(panelActionButton(
            "RETURN TO SURFACE OPS",
            ui::actions::surfaceScanBank,
            "danger scan-return-action"));
        return presentation;
    }

    presentation.actions.push_back(scan.completed
        ? disabledPanelButton("PULSE COMPLETE")
        : panelActionButton("PULSE SCANNER", ui::actions::surfaceScanPulse, "warn scan-pulse-action"));
    presentation.actions.push_back(panelActionButton(
        "BANK FORECAST",
        ui::actions::surfaceScanBank,
        "ok scan-bank-action"));
    presentation.actions.push_back(panelActionButton(
        "ABORT",
        ui::actions::surfaceScanAbort,
        "danger scan-abort-action"));
    return presentation;
}

} // namespace rocket
