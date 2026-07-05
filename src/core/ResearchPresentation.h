#pragma once

#include "core/ContentIds.h"
#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/GameUi.h"
#include "core/MiningSystem.h"
#include "core/PanelPresentation.h"
#include "core/ResearchSystem.h"
#include "core/Tuning.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace rocket {

struct PhaseStepPresentation {
    std::string label;
    std::string stateLabel;
    std::string stateClass;
};

struct ResearchProjectCardPresentation {
    int index = 0;
    std::string rarity;
    std::string blueprintGain;
    std::string title;
    std::string detail;
    std::string reward;
    std::string materialCost;
    std::vector<PanelMetricPresentation> resourceChips;
    bool affordable = false;
    PanelButtonPresentation action;
};

struct PhaseBriefingPresentation {
    std::string title;
    std::vector<DetailPresentationRow> rows;
};

struct PhaseAdvisoryPresentation {
    std::string title;
    std::string detail;
    std::string cssClass;
};

struct ResearchPhasePresentation {
    std::vector<PhaseStepPresentation> phaseSteps;
    PhaseBriefingPresentation briefing;
    PhaseAdvisoryPresentation advisory;
    std::vector<DetailPresentationRow> details;
    std::vector<PanelMetricPresentation> metrics;
    std::vector<ResearchProjectCardPresentation> projects;
    PanelButtonPresentation skipAction;
};

struct SurfaceActionPreviewPresentation {
    std::string title;
    std::string detail;
    std::string cost;
    std::string risk;
    std::string riskLabel;
    std::string availability;
    std::vector<PanelMetricPresentation> payoffChips;
    PanelButtonPresentation action;
};

struct SurfaceUpgradeCardPresentation {
    int index = 0;
    std::string category;
    std::string rarity;
    std::string title;
    std::string detail;
    std::vector<PanelMetricPresentation> effectChips;
    PanelButtonPresentation action;
};

struct SurfaceExpeditionPresentation {
    std::vector<PhaseStepPresentation> phaseSteps;
    PhaseBriefingPresentation briefing;
    std::string postureTitle;
    std::string postureDetail;
    std::string postureClass;
    std::string siteDetail;
    std::vector<DetailPresentationRow> details;
    std::vector<PanelMetricPresentation> metrics;
    std::vector<std::string> logEntries;
    std::vector<SurfaceUpgradeCardPresentation> upgradeOffers;
    std::vector<std::string> selectedUpgradeNames;
    std::vector<SurfaceActionPreviewPresentation> actions;
    PanelButtonPresentation droneOpsAction;
};

struct MiniDroneCardPresentation {
    int index = 0;
    std::string role;
    std::string rarity;
    std::string title;
    std::string detail;
    std::string status;
    std::vector<PanelMetricPresentation> effectChips;
    PanelButtonPresentation action;
};

struct DroneOpsPresentation {
    std::vector<PanelMetricPresentation> metrics;
    std::vector<DetailPresentationRow> details;
    std::vector<MiniDroneCardPresentation> drones;
    PanelButtonPresentation upgradeSlotAction;
    PanelButtonPresentation backAction;
    std::string nextSlotCost;
};

inline std::string materialSummary(const MaterialInventory& materials)
{
    if (materials.common == 0 && materials.rare == 0 && materials.exotic == 0) {
        return std::string(text::panel::noMaterials);
    }
    return text::panel::materialSummary(materials.common, materials.rare, materials.exotic);
}

inline std::string researchMaterialSummary(const MaterialInventory& cost, const MaterialInventory& owned)
{
    return "Cost: " + materialSummary(cost) + " / Have: " + materialSummary(owned);
}

inline PhaseStepPresentation phaseStep(std::string_view label, std::string_view stateLabel, std::string_view stateClass)
{
    return {std::string(label), std::string(stateLabel), std::string(stateClass)};
}

inline std::vector<PhaseStepPresentation> postArrivalPhaseSteps(Screen screen)
{
    const bool arrivalActive = screen == Screen::Results;
    const bool researchDone = screen == Screen::SurfaceExpedition || screen == Screen::Upgrade;
    const bool surfaceDone = screen == Screen::Upgrade;
    return {
        phaseStep(text::panel::details::arrivalPhase, arrivalActive ? "Now" : "Done", arrivalActive ? "active" : "done"),
        phaseStep(text::panel::details::researchPhase, screen == Screen::Research ? "Now" : (researchDone ? "Done" : "Next"), screen == Screen::Research ? "active" : (researchDone ? "done" : "pending")),
        phaseStep(text::panel::details::surfacePhase, screen == Screen::SurfaceExpedition ? "Now" : (surfaceDone ? "Done" : "Next"), screen == Screen::SurfaceExpedition ? "active" : (surfaceDone ? "done" : "pending")),
        phaseStep(text::panel::details::refitPhase, screen == Screen::Upgrade ? "Now" : "Next", screen == Screen::Upgrade ? "active" : "pending")
    };
}

inline PhaseBriefingPresentation postArrivalPhaseBriefing(Screen screen)
{
    if (screen == Screen::Results) {
        return {
            std::string(text::panel::modals::arrivalBriefing),
            {
                detailPresentationRow(text::panel::details::phaseIntent, std::string("Confirm the agency reached a research frontier and prepare the post-arrival sequence.")),
                detailPresentationRow(text::panel::details::phaseInputs, std::string("Transfer telemetry, surviving crew, vehicle condition, and any resources already in the archive.")),
                detailPresentationRow(text::panel::details::phaseOutputs, std::string("Opens one research decision, then a landed surface expedition before the next refit.")),
                detailPresentationRow(text::panel::details::phaseRisk, std::string("The ship still needs to survive surface operations and the next launch cycle; arrival is not the end of the run.")),
                detailPresentationRow(text::panel::details::phaseNext, std::string("Continue to Research to spend or preserve materials before the landing team deploys."))
            }
        };
    }

    if (screen == Screen::Research) {
        return {
            std::string(text::panel::modals::researchBriefing),
            {
                detailPresentationRow(text::panel::details::phaseIntent, std::string("Turn a successful Mars arrival into long-term agency capability.")),
                detailPresentationRow(text::panel::details::phaseInputs, std::string("Blueprints, recovered materials, decoded artifacts, and lab bonuses.")),
                detailPresentationRow(text::panel::details::phaseOutputs, std::string("Unlock module families, crew facilities, field tools, and artifact analysis threads.")),
                detailPresentationRow(text::panel::details::phaseRisk, std::string("Research spends scarce materials before you know what the surface team will recover.")),
                detailPresentationRow(text::panel::details::phaseNext, std::string("Pick one project or skip, then deploy the surface expedition."))
            }
        };
    }

    return {
        std::string(text::panel::modals::surfaceBriefing),
        {
            detailPresentationRow(text::panel::details::phaseIntent, std::string("Convert a landing into recoverable samples, artifacts, and future refit options.")),
            detailPresentationRow(text::panel::details::phaseInputs, std::string("Action kits, site profile, field-kit unlocks, and the payload already in the canisters.")),
            detailPresentationRow(text::panel::details::phaseOutputs, std::string("Common, rare, and exotic materials plus occasional artifacts or blueprint leads.")),
            detailPresentationRow(text::panel::details::phaseRisk, std::string("Cargo, hazard, low action kits, and depth raise extraction risk. Failed extraction can lose payload.")),
            detailPresentationRow(text::panel::details::phaseNext, std::string("Extract to bank the payload and return to the refit window."))
        }
    };
}

inline void addResearchResourceChip(std::vector<PanelMetricPresentation>& chips, std::string_view label, int value)
{
    if (value != 0) {
        chips.push_back(panelMetric(label, "-" + std::to_string(value)));
    }
}

inline std::vector<PanelMetricPresentation> researchResourceChips(const ResearchProject& project, const MetaProgress& meta)
{
    std::vector<PanelMetricPresentation> chips;
    chips.push_back(panelMetric(text::labels::blueprints, text::panel::blueprintGain(researchBlueprintGain(meta, project))));
    addResearchResourceChip(chips, text::labels::commonMaterials, project.materialCost.common);
    addResearchResourceChip(chips, text::labels::rareMaterials, project.materialCost.rare);
    addResearchResourceChip(chips, text::labels::exoticMaterials, project.materialCost.exotic);
    return chips;
}

inline void addPositiveChip(std::vector<PanelMetricPresentation>& chips, std::string_view label, int value)
{
    if (value > 0) {
        chips.push_back(panelMetric(label, "+" + std::to_string(value)));
    }
}

inline void addPercentChip(std::vector<PanelMetricPresentation>& chips, std::string_view label, double value)
{
    if (value > 0.0) {
        chips.push_back(panelMetric(label, display::signedPercent(value)));
    }
}

inline void addDoubleChip(std::vector<PanelMetricPresentation>& chips, std::string_view label, double value)
{
    if (value > 0.0) {
        chips.push_back(panelMetric(label, "+" + display::fixed(value, 1)));
    }
}

inline void addSignedPercentChip(std::vector<PanelMetricPresentation>& chips, std::string_view label, double value)
{
    if (std::abs(value) >= 0.005) {
        chips.push_back(panelMetric(label, display::signedPercent(value)));
    }
}

inline std::vector<PanelMetricPresentation> surfaceUpgradeChips(const SurfaceUpgradeStats& stats)
{
    std::vector<PanelMetricPresentation> chips;
    addDoubleChip(chips, "Drill", stats.drillPower);
    addDoubleChip(chips, "Cooling", stats.drillCooling);
    addDoubleChip(chips, "Durability", stats.drillDurability);
    addPercentChip(chips, "Recoil", stats.hardRockBounceRelief);
    addPercentChip(chips, "Ore yield", stats.oreYieldChance);
    addDoubleChip(chips, "Scanner", stats.scannerRadius);
    addPercentChip(chips, text::labels::hazard, stats.hazardRelief);
    addDoubleChip(chips, "Drone speed", stats.droneSpeed);
    if (stats.oxygenSeconds > 0.0) {
        chips.push_back(panelMetric("Oxygen", "+" + std::to_string(static_cast<int>(std::round(stats.oxygenSeconds))) + "s"));
    }
    addPercentChip(chips, text::labels::extractionRisk, stats.extractionRiskRelief);
    return chips;
}

inline SurfaceUpgradeCardPresentation surfaceUpgradeCardPresentation(const SurfaceUpgrade& upgrade, int index)
{
    return {
        index,
        std::string(toString(upgrade.category)),
        std::string(toString(upgrade.rarity)),
        upgrade.name,
        upgrade.description,
        surfaceUpgradeChips(upgrade.stats),
        panelActionButton("Choose upgrade", ui::actions::surfaceUpgrade(index), "ok")
    };
}

inline std::vector<PanelMetricPresentation> miniDroneChips(const MiniDroneStats& stats)
{
    std::vector<PanelMetricPresentation> chips;
    if (stats.passiveMiningRate > 0.0) {
        chips.push_back(panelMetric("Auto-mine", "+" + display::fixed(stats.passiveMiningRate * 60.0, 1) + "/min"));
    }
    if (stats.oxygenSeconds > 0.0) {
        chips.push_back(panelMetric("Oxygen", "+" + std::to_string(static_cast<int>(std::round(stats.oxygenSeconds))) + "s"));
    }
    addDoubleChip(chips, "Scanner", stats.scannerRadius);
    addPercentChip(chips, "Durability", stats.drillIntegrityRelief);
    addPercentChip(chips, "Bounce relief", stats.hardRockBounceRelief);
    addPercentChip(chips, text::labels::extractionRisk, stats.extractionRiskRelief);
    addPercentChip(chips, text::labels::contactRisk, stats.enemyEncounterRelief);
    return chips;
}

inline MiniDroneCardPresentation miniDroneCardPresentation(const MiniDrone& drone, const GameState& state, int index)
{
    const bool unlocked = isMiniDroneUnlocked(state.meta, drone);
    const bool owned = std::find(state.meta.ownedDroneIds.begin(), state.meta.ownedDroneIds.end(), drone.id) != state.meta.ownedDroneIds.end();
    const bool equipped = std::find(state.meta.equippedDroneIds.begin(), state.meta.equippedDroneIds.end(), drone.id) != state.meta.equippedDroneIds.end();
    const bool hasFreeSlot = state.meta.equippedDroneIds.size() < static_cast<std::size_t>(std::max(0, state.meta.droneBaySlots));
    PanelButtonPresentation action = disabledPanelButton(unlocked ? "Slot full" : "Locked");
    std::string status = unlocked ? (equipped ? "Equipped" : (owned ? "Ready" : "Not owned")) : ("Locked: " + std::string(unlockDisplayName(drone.unlockKey)));
    if (owned && unlocked) {
        action = equipped
            ? panelActionButton("Unequip", ui::actions::equipDrone(index), "warn")
            : (hasFreeSlot ? panelActionButton("Equip", ui::actions::equipDrone(index), "ok") : disabledPanelButton("Slot full"));
    }
    return {
        index,
        std::string(toString(drone.role)),
        std::string(toString(drone.rarity)),
        drone.name,
        drone.description,
        std::move(status),
        miniDroneChips(drone.stats),
        std::move(action)
    };
}

inline std::string miniDroneNameSummary(const GameState& state, const ContentCatalog& catalog)
{
    if (!droneBayUnlocked(state)) {
        return "Not unlocked";
    }
    if (state.meta.equippedDroneIds.empty()) {
        return "No drones assigned";
    }
    std::string summary;
    for (const std::string& droneId : state.meta.equippedDroneIds) {
        const MiniDrone* drone = catalog.findMiniDrone(droneId);
        if (drone == nullptr) {
            continue;
        }
        if (!summary.empty()) {
            summary += ", ";
        }
        summary += drone->name;
    }
    return summary.empty() ? "No drones assigned" : summary;
}

inline DroneOpsPresentation droneOpsPresentation(GameState state, const ContentCatalog& catalog)
{
    ensureDroneBayState(state, catalog);
    const MiniDroneLoadoutEffects effects = miniDroneLoadoutEffects(state, catalog);
    const int nextSlot = state.meta.droneBaySlots + 1;
    const MaterialInventory nextCost = droneSlotUpgradeCost(nextSlot);
    const bool maxed = state.meta.droneBaySlots >= 6;
    const bool affordable = !maxed && canAffordMaterials(state.meta.materials, nextCost);

    DroneOpsPresentation presentation;
    presentation.metrics = {
        panelMetric("Slots", std::to_string(static_cast<int>(state.meta.equippedDroneIds.size())) + "/" + std::to_string(std::max(0, state.meta.droneBaySlots))),
        panelMetric("Owned drones", std::to_string(static_cast<int>(state.meta.ownedDroneIds.size()))),
        panelMetric(text::labels::commonMaterials, std::to_string(state.meta.materials.common)),
        panelMetric(text::labels::rareMaterials, std::to_string(state.meta.materials.rare)),
        panelMetric(text::labels::exoticMaterials, std::to_string(state.meta.materials.exotic))
    };
    presentation.details = {
        detailPresentationRow("Drone Bay", std::to_string(std::max(0, state.meta.droneBaySlots)) + " slot capacity"),
        detailPresentationRow("Loadout", miniDroneNameSummary(state, catalog)),
        detailPresentationRow("Mining support", effects.passiveMiningRate > 0.0 ? ("+" + display::fixed(effects.passiveMiningRate * 60.0, 1) + " common/min") : "None"),
        detailPresentationRow("Oxygen support", effects.oxygenSeconds > 0.0 ? ("+" + std::to_string(static_cast<int>(std::round(effects.oxygenSeconds))) + "s") : "None"),
        detailPresentationRow("Scanner support", effects.scannerRadius > 0.0 ? ("+" + display::fixed(effects.scannerRadius, 1) + " radius") : "None"),
        detailPresentationRow("Stability support", effects.hardRockBounceRelief > 0.0 ? display::percent(effects.hardRockBounceRelief) + " less hard-rock bounce" : "None"),
        detailPresentationRow("Future combat", std::string("Attack and Defense drones unlock after hostile surface encounters exist beyond the solar system."))
    };

    for (int index = 0; index < static_cast<int>(catalog.miniDrones.size()); ++index) {
        presentation.drones.push_back(miniDroneCardPresentation(catalog.miniDrones[static_cast<std::size_t>(index)], state, index));
    }
    presentation.nextSlotCost = maxed ? "Max capacity" : materialSummary(nextCost);
    const std::string blockedSlotLabel = "Need " + presentation.nextSlotCost;
    presentation.upgradeSlotAction = maxed
        ? disabledPanelButton("Bay maxed")
        : (affordable ? panelActionButton("Add drone slot", ui::actions::upgradeDroneSlot, "ok") : disabledPanelButton(blockedSlotLabel));
    presentation.backAction = panelActionButton("Back to Surface Ops", ui::actions::backToSurfaceOps);
    return presentation;
}

inline ResearchProjectCardPresentation researchProjectCardPresentation(const ResearchProject& project, const GameState& state, int index)
{
    const bool affordable = canAffordMaterials(state.meta.materials, project.materialCost);
    return {
        index,
        std::string(toString(project.rarity)),
        text::panel::blueprintGain(researchBlueprintGain(state.meta, project)),
        project.name,
        project.description,
        project.rewardUnlockKey.empty() || hasUnlock(state.meta, project.rewardUnlockKey)
            ? std::string()
            : text::panel::unlocksFamily(unlockDisplayName(project.rewardUnlockKey)),
        researchMaterialSummary(project.materialCost, state.meta.materials),
        researchResourceChips(project, state.meta),
        affordable,
        affordable
            ? panelActionButton(text::buttons::conductResearch, ui::actions::researchProject(index), "ok")
            : disabledPanelButton(text::panel::needMaterials)
    };
}

inline PhaseAdvisoryPresentation researchAdvisoryPresentation(const std::vector<ResearchProjectCardPresentation>& projects)
{
    if (projects.empty()) {
        return {
            std::string(text::panel::messages::researchAdvisoryEmpty),
            std::string(text::panel::messages::researchAdvisoryEmptyDetail),
            "caution"
        };
    }

    const bool hasAffordableProject = std::any_of(projects.begin(), projects.end(), [](const ResearchProjectCardPresentation& project) {
        return project.affordable;
    });
    if (!hasAffordableProject) {
        return {
            std::string(text::panel::messages::researchAdvisoryMaterials),
            std::string(text::panel::messages::researchAdvisoryMaterialsDetail),
            "caution"
        };
    }

    return {
        std::string(text::panel::messages::researchAdvisoryReady),
        std::string(text::panel::messages::researchAdvisoryReadyDetail),
        "ok"
    };
}

inline ResearchPhasePresentation researchPhasePresentation(const GameState& state, const ContentCatalog& catalog)
{
    ResearchPhasePresentation presentation;
    presentation.phaseSteps = postArrivalPhaseSteps(Screen::Research);
    presentation.briefing = postArrivalPhaseBriefing(Screen::Research);
    presentation.details = {
        detailPresentationRow(text::labels::blueprints, std::to_string(state.meta.blueprintProgress)),
        detailPresentationRow(text::labels::artifactInsight, text::panel::blueprintGain(artifactInsightBlueprintBonus(state.meta))),
        detailPresentationRow(text::labels::labBonus, text::panel::blueprintGain(researchFacilityBlueprintBonus(state.meta))),
        detailPresentationRow(text::panel::details::commonMaterials, std::to_string(state.meta.materials.common)),
        detailPresentationRow(text::panel::details::rareMaterials, std::to_string(state.meta.materials.rare)),
        detailPresentationRow(text::panel::details::exoticMaterials, std::to_string(state.meta.materials.exotic)),
        detailPresentationHeader(text::panel::details::researchRules),
        detailPresentationRow(text::panel::details::blueprintUse, std::string("Research adds blueprint progress and can unlock new module families, facilities, or field tools.")),
        detailPresentationRow(text::panel::details::materialsUse, std::string("Recovered samples pay material costs. Credit costs still happen later in refit.")),
        detailPresentationRow(text::panel::details::artifactInsightUse, std::string("Decoded artifacts add a capped blueprint bonus to future research projects.")),
        detailPresentationRow(text::panel::details::labBonusUse, std::string("Mission Analysis Lab adds a permanent blueprint bonus to future research choices.")),
        detailPresentationRow(text::panel::details::skippedResearch, std::string("Skipping preserves materials and moves directly to the surface expedition."))
    };
    presentation.metrics = {
        panelMetric(text::labels::blueprints, std::to_string(state.meta.blueprintProgress)),
        panelMetric(text::labels::artifactInsight, text::panel::blueprintGain(artifactInsightBlueprintBonus(state.meta))),
        panelMetric(text::labels::labBonus, text::panel::blueprintGain(researchFacilityBlueprintBonus(state.meta))),
        panelMetric(text::labels::commonMaterials, std::to_string(state.meta.materials.common)),
        panelMetric(text::labels::rareMaterials, std::to_string(state.meta.materials.rare)),
        panelMetric(text::labels::exoticMaterials, std::to_string(state.meta.materials.exotic))
    };

    for (std::size_t i = 0; i < state.run.researchProjectIds.size(); ++i) {
        if (const ResearchProject* project = catalog.findResearchProject(state.run.researchProjectIds[i])) {
            presentation.projects.push_back(researchProjectCardPresentation(*project, state, static_cast<int>(i)));
        }
    }

    presentation.advisory = researchAdvisoryPresentation(presentation.projects);
    presentation.skipAction = panelActionButton(text::buttons::skipResearch, ui::actions::skipResearch);
    return presentation;
}

inline PanelButtonPresentation surfaceActionButton(std::string_view label, std::string_view actionId, int supply, int cost, std::string cssClass = "")
{
    return supply >= cost
        ? panelActionButton(label, actionId, std::move(cssClass))
        : disabledPanelButton(text::buttons::unavailable);
}

inline PanelButtonPresentation miningSurfaceActionButton(const GameState& state)
{
    if (state.run.surfaceExpedition.miningRunUsed) {
        return disabledPanelButton(text::buttons::unavailable);
    }
    if (state.run.surfaceExpedition.sharedFuel <= 0) {
        return disabledPanelButton(text::buttons::unavailable);
    }
    return panelActionButton(text::buttons::mineDeposit, ui::actions::mineSurface);
}

inline std::string surfaceActionAvailability(int supply, int cost)
{
    return supply >= cost ? std::string(text::panel::ready) : text::panel::messages::needSupply(cost);
}

inline std::string miningSurfaceActionAvailability(const GameState& state)
{
    if (state.run.surfaceExpedition.miningRunUsed) {
        return std::string(text::fuel::offline);
    }
    if (state.run.surfaceExpedition.sharedFuel <= 0) {
        return std::string(text::fuel::offline);
    }
    return text::fuel::availability(arkDiscovered(state));
}

inline PanelButtonPresentation pushSurfaceActionButton(const GameState& state)
{
    if (state.run.surfaceExpedition.miningRunUsed) {
        return disabledPanelButton(text::buttons::unavailable);
    }
    return surfaceActionButton(text::buttons::pushDeeper, ui::actions::pushSurface, state.run.surfaceExpedition.supply, tuning::research::pushSupplyCost, "danger");
}

inline std::string pushSurfaceActionAvailability(const GameState& state)
{
    if (state.run.surfaceExpedition.miningRunUsed) {
        return std::string(text::fuel::offline);
    }
    return surfaceActionAvailability(state.run.surfaceExpedition.supply, tuning::research::pushSupplyCost);
}

inline std::string surfaceHazardRisk(double hazard, double scale, double relief)
{
    return display::percent(std::clamp(
        hazard * scale - relief,
        tuning::research::surfaceHazardChanceMinimum,
        tuning::research::surfaceHazardChanceMaximum));
}

inline SurfaceActionPreviewPresentation surfaceActionPreview(
    std::string_view title,
    std::string detail,
    int supply,
    int cost,
    std::string risk,
    std::string riskLabel,
    std::vector<PanelMetricPresentation> payoffChips,
    PanelButtonPresentation action)
{
    return {
        std::string(title),
        std::move(detail),
        text::panel::messages::supplyCost(cost),
        std::move(risk),
        std::move(riskLabel),
        surfaceActionAvailability(supply, cost),
        std::move(payoffChips),
        std::move(action)
    };
}

inline double projectedSurfaceExtractionRiskDelta(const GameState& state, const SurfaceExpeditionState& projectedExpedition)
{
    GameState projected = state;
    projected.run.surfaceExpedition = projectedExpedition;
    return surfaceExtractionRisk(projected) - surfaceExtractionRisk(state);
}

inline SurfaceExpeditionState projectedSurveyExpedition(const SurfaceExpeditionState& expedition, const SurfaceToolEffects& tools, const SurfaceCrewEffects& crew, const SurfaceSiteProfileEffects& site)
{
    SurfaceExpeditionState projected = expedition;
    projected.supply = std::max(0, projected.supply - tuning::research::surveySupplyCost);
    const MaterialInventory gain {.common = tuning::research::surveyCommonGain + tools.surveyCommonBonus + crew.surveyCommonBonus + site.surveyCommonBonus};
    projected.temporaryMaterials.common += gain.common;
    projected.cargo += std::max(0, gain.common);
    return projected;
}

inline SurfaceExpeditionState projectedMineExpedition(const SurfaceExpeditionState& expedition, const SurfaceToolEffects& tools, const SurfaceCrewEffects& crew, const SurfaceSiteProfileEffects& site)
{
    SurfaceExpeditionState projected = expedition;
    projected.supply = std::max(0, projected.supply - tuning::research::mineSupplyCost);
    const MaterialInventory gain {.common = tuning::research::mineCommonGain + tools.mineCommonBonus + crew.mineCommonBonus + site.mineCommonBonus};
    projected.temporaryMaterials.common += gain.common;
    projected.cargo += std::max(0, gain.common);
    return projected;
}

inline SurfaceExpeditionState projectedPushExpedition(const SurfaceExpeditionState& expedition)
{
    SurfaceExpeditionState projected = expedition;
    projected.supply = std::max(0, projected.supply - tuning::research::pushSupplyCost);
    projected.depth += 1;
    projected.hazard += tuning::research::hazardPerDepth;
    return projected;
}

inline std::vector<PanelMetricPresentation> surveyPayoffChips(const GameState& state, const SurfaceToolEffects& tools, const SurfaceCrewEffects& crew, const SurfaceSiteProfileEffects& site)
{
    std::vector<PanelMetricPresentation> chips;
    chips.push_back(panelMetric("Layer read", "+0 first"));
    chips.push_back(panelMetric("More pulses", "+1, +2..."));
    addPositiveChip(chips, text::labels::commonMaterials, tuning::research::surveyCommonGain + tools.surveyCommonBonus + crew.surveyCommonBonus + site.surveyCommonBonus);
    addSignedPercentChip(chips, text::labels::extractionRisk, projectedSurfaceExtractionRiskDelta(state, projectedSurveyExpedition(state.run.surfaceExpedition, tools, crew, site)));
    return chips;
}

inline std::vector<PanelMetricPresentation> minePayoffChips(
    const GameState& state,
    const SurfaceToolEffects& tools,
    const SurfaceCrewEffects& crew,
    const SurfaceSiteProfileEffects& site,
    const SurfaceUpgradeEffects& upgrades)
{
    std::vector<PanelMetricPresentation> chips;
    addPositiveChip(chips, text::labels::commonMaterials, tuning::research::mineCommonGain + tools.mineCommonBonus + crew.mineCommonBonus + site.mineCommonBonus);
    addPercentChip(chips, text::labels::rareMaterials, std::min(1.0, tools.mineRareChanceBonus + crew.mineRareChanceBonus + site.mineRareChanceBonus + upgrades.oreYieldChance));
    addSignedPercentChip(chips, text::labels::extractionRisk, projectedSurfaceExtractionRiskDelta(state, projectedMineExpedition(state.run.surfaceExpedition, tools, crew, site)));
    return chips;
}

inline std::vector<PanelMetricPresentation> pushPayoffChips(const GameState& state, const SurfaceCrewEffects& crew, const SurfaceSiteProfileEffects& site)
{
    std::vector<PanelMetricPresentation> chips;
    chips.push_back(panelMetric(text::labels::depth, "+1"));
    const bool nextLayerScanned = std::any_of(
        state.run.surfaceExpedition.depthProspects.begin(),
        state.run.surfaceExpedition.depthProspects.end(),
        [&](const SurfaceDepthProspect& prospect) {
            return prospect.absoluteDepth == state.run.surfaceExpedition.depth + 1;
        });
    chips.push_back(panelMetric("Layer +1", nextLayerScanned ? "Scanned" : "Unknown"));
    addPercentChip(chips, text::labels::artifacts, std::min(1.0, tuning::research::artifactChanceBase + crew.artifactChanceBonus + site.artifactChanceBonus));
    chips.push_back(panelMetric(text::labels::hazard, display::signedPercent(tuning::research::hazardPerDepth)));
    addSignedPercentChip(chips, text::labels::extractionRisk, projectedSurfaceExtractionRiskDelta(state, projectedPushExpedition(state.run.surfaceExpedition)));
    return chips;
}

inline std::vector<PanelMetricPresentation> extractPayoffChips(const SurfaceExpeditionState& expedition)
{
    std::vector<PanelMetricPresentation> chips;
    addPositiveChip(chips, text::labels::commonMaterials, expedition.temporaryMaterials.common);
    addPositiveChip(chips, text::labels::rareMaterials, expedition.temporaryMaterials.rare);
    addPositiveChip(chips, text::labels::exoticMaterials, expedition.temporaryMaterials.exotic);
    addPositiveChip(chips, text::labels::artifacts, static_cast<int>(expedition.temporaryArtifacts.size()));
    return chips;
}

inline bool hasSurfacePayload(const SurfaceExpeditionState& expedition)
{
    return expedition.cargo > 0
        || expedition.temporaryMaterials.common > 0
        || expedition.temporaryMaterials.rare > 0
        || expedition.temporaryMaterials.exotic > 0
        || !expedition.temporaryArtifacts.empty();
}

inline SurfaceExpeditionPresentation surfacePosturePresentation(const SurfaceExpeditionState& expedition, double extractionRisk)
{
    SurfaceExpeditionPresentation presentation;
    const bool payloadLoaded = hasSurfacePayload(expedition);
    const bool miningWindowOpen = expedition.sharedFuel > 0 && !expedition.miningRunUsed;
    if (!payloadLoaded && miningWindowOpen) {
        presentation.postureTitle = "Recommended: mine deposit";
        presentation.postureDetail = "The site is opened. Mining uses fuel instead of action kits, and only runs once this loop.";
        presentation.postureClass = "neutral";
        return presentation;
    }
    if (expedition.supply <= 0 && !miningWindowOpen) {
        presentation.postureTitle = std::string(text::panel::messages::surfacePostureExtract);
        presentation.postureDetail = std::string(text::panel::messages::surfacePostureExtractDetail);
        presentation.postureClass = "danger";
        return presentation;
    }
    if (payloadLoaded && extractionRisk >= 0.45) {
        presentation.postureTitle = std::string(text::panel::messages::surfacePostureGreedy);
        presentation.postureDetail = std::string(text::panel::messages::surfacePostureGreedyDetail);
        presentation.postureClass = "danger";
        return presentation;
    }
    if (payloadLoaded && (extractionRisk >= 0.25 || expedition.supply <= tuning::research::pushSupplyCost)) {
        presentation.postureTitle = std::string(text::panel::messages::surfacePostureNarrowing);
        presentation.postureDetail = std::string(text::panel::messages::surfacePostureNarrowingDetail);
        presentation.postureClass = "caution";
        return presentation;
    }
    if (payloadLoaded) {
        presentation.postureTitle = std::string(text::panel::messages::surfacePostureStable);
        presentation.postureDetail = std::string(text::panel::messages::surfacePostureStableDetail);
        presentation.postureClass = "ok";
        return presentation;
    }

    presentation.postureTitle = std::string(text::panel::messages::surfacePostureScout);
    presentation.postureDetail = std::string(text::panel::messages::surfacePostureScoutDetail);
    presentation.postureClass = "neutral";
    return presentation;
}

inline std::string surfaceFieldKitSummary(const MetaProgress& meta)
{
    std::vector<std::string> tools;
    if (hasUnlock(meta, content::unlock::surfaceProbes)) {
        tools.push_back(unlockDisplayName(content::unlock::surfaceProbes));
    }
    if (hasUnlock(meta, content::unlock::surfaceDrills)) {
        tools.push_back(unlockDisplayName(content::unlock::surfaceDrills));
    }
    if (hasUnlock(meta, content::unlock::cargoRigs)) {
        tools.push_back(unlockDisplayName(content::unlock::cargoRigs));
    }
    if (hasUnlock(meta, content::unlock::perimeterDrones)) {
        tools.push_back(unlockDisplayName(content::unlock::perimeterDrones));
    }
    if (hasUnlock(meta, content::unlock::droneBay)) {
        tools.push_back(unlockDisplayName(content::unlock::droneBay));
    }
    if (tools.empty()) {
        return "Baseline kit";
    }

    std::string summary = tools.front();
    for (std::size_t i = 1; i < tools.size(); ++i) {
        summary += ", " + tools[i];
    }
    return summary;
}

inline std::string surfaceUpgradeNameSummary(const std::vector<std::string>& names)
{
    if (names.empty()) {
        return "None yet";
    }
    std::string summary = names.front();
    for (std::size_t i = 1; i < names.size(); ++i) {
        summary += ", " + names[i];
    }
    return summary;
}

inline std::vector<DetailPresentationRow> surfaceDetailsPresentation(
    const SurfaceExpeditionState& expedition,
    const MetaProgress& meta,
    const SurfaceCrewEffects& crew,
    const SurfaceUpgradeEffects& upgrades,
    double extractionRisk,
    bool arkKnown)
{
    const SurfaceToolEffects tools = surfaceToolEffects(meta);
    std::vector<DetailPresentationRow> rows {
        detailPresentationRow(text::labels::site, std::string(surfaceSiteProfileName(expedition.siteProfile))),
        detailPresentationRow(text::labels::fieldKit, surfaceFieldKitSummary(meta)),
        detailPresentationRow("Drone loadout", hasUnlock(meta, content::unlock::droneBay)
            ? std::string("Configure persistent helper drones from Drone Ops before mining.")
            : std::string("Research Drone Bay to assign persistent mining helpers.")),
        detailPresentationRow(text::panel::details::fieldSpecialist, crew.summary),
        detailPresentationRow("Field upgrades", surfaceUpgradeNameSummary(upgrades.names)),
        detailPresentationRow(text::fuel::reserveLabel(arkKnown), std::to_string(expedition.sharedFuel) + "/" + std::to_string(std::max(1, expedition.sharedFuelCapacity)) + " available for shuttle and drone operations"),
        detailPresentationRow(text::labels::hazard, display::percent(expedition.hazard)),
        detailPresentationRow(text::labels::extractionRisk, display::percent(extractionRisk)),
        detailPresentationHeader(text::panel::details::fieldRules),
        detailPresentationRow(text::panel::details::surveyRisk, std::string("Scans forecast one layer per pulse: +0 first, then the Push Deeper layers. Dust can still burn extra action kits.")),
        detailPresentationRow(text::panel::details::miningRisk, std::string("Mining is one fuel-only drone run for this surface loop; pushing deeper closes once the run is used.")),
        detailPresentationRow(text::panel::details::depthRisk, std::string("Pushing deeper commits the actual layer depth and marks what the mining drone should find there.")),
        detailPresentationRow(text::panel::details::extraction, std::string("Cargo and hazard raise recovery risk; cargo rigs reduce the penalty from heavier payloads.")),
        detailPresentationRow(
            text::panel::details::toolMitigation,
            tools.supplyBonus > 0 || tools.mineCommonBonus > 0 || tools.extractionRiskRelief > 0.0
                ? std::string("Installed surface tools are already changing these odds.")
                : std::string("Research field probes, drill rigs, and cargo rigs to improve future expeditions."))
    };
    if (expedition.enemyEncountersEnabled) {
        rows.push_back(detailPresentationRow(text::panel::details::hostileContact, std::string("Enemy contact can consume action kits and cargo beyond the solar system; perimeter drones reduce contact risk.")));
    }
    return rows;
}

inline SurfaceExpeditionPresentation surfaceExpeditionPresentation(const GameState& state, const ContentCatalog& catalog)
{
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    const SurfaceToolEffects tools = surfaceToolEffects(state.meta);
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    const SurfaceUpgradeEffects upgrades = surfaceUpgradeEffects(state, catalog);
    const double extractionRisk = surfaceExtractionRisk(state);
    SurfaceExpeditionPresentation presentation = surfacePosturePresentation(expedition, extractionRisk);
    presentation.phaseSteps = postArrivalPhaseSteps(Screen::SurfaceExpedition);
    presentation.briefing = postArrivalPhaseBriefing(Screen::SurfaceExpedition);
    presentation.siteDetail = std::string(surfaceSiteProfileDetail(expedition.siteProfile));
    presentation.details = surfaceDetailsPresentation(expedition, state.meta, crew, upgrades, extractionRisk, arkDiscovered(state));
    presentation.logEntries = expedition.logEntries;
    presentation.selectedUpgradeNames = upgrades.names;
    if (expedition.surfaceUpgradeOfferAvailable) {
        for (std::size_t i = 0; i < expedition.surfaceUpgradeOfferIds.size(); ++i) {
            if (const SurfaceUpgrade* upgrade = catalog.findSurfaceUpgrade(expedition.surfaceUpgradeOfferIds[i])) {
                presentation.upgradeOffers.push_back(surfaceUpgradeCardPresentation(*upgrade, static_cast<int>(i)));
            }
        }
    }
    presentation.metrics = {
        panelMetric(text::labels::site, std::string(surfaceSiteProfileName(expedition.siteProfile))),
        panelMetric(text::labels::fieldKit, surfaceFieldKitSummary(state.meta)),
        panelMetric(text::labels::hazard, display::percent(expedition.hazard)),
        panelMetric(text::labels::supply, std::to_string(expedition.supply)),
        panelMetric(text::fuel::reserveLabel(arkDiscovered(state)), std::to_string(expedition.sharedFuel) + "/" + std::to_string(std::max(1, expedition.sharedFuelCapacity))),
        panelMetric(text::labels::cargo, std::to_string(expedition.cargo)),
        panelMetric(text::labels::depth, std::to_string(expedition.depth)),
        panelMetric(text::labels::extractionRisk, display::percent(extractionRisk)),
        panelMetric(text::labels::commonMaterials, std::to_string(expedition.temporaryMaterials.common)),
        panelMetric(text::labels::rareMaterials, std::to_string(expedition.temporaryMaterials.rare)),
        panelMetric(text::labels::exoticMaterials, std::to_string(expedition.temporaryMaterials.exotic)),
        panelMetric(text::labels::artifacts, std::to_string(expedition.temporaryArtifacts.size())),
        panelMetric("Prospects",
            std::to_string(
                std::max(0, expedition.prospectMaterials.common) +
                std::max(0, expedition.prospectMaterials.rare) +
                std::max(0, expedition.prospectMaterials.exotic) +
                std::max(0, expedition.prospectArtifacts)))
    };
    if (expedition.enemyEncountersEnabled) {
        presentation.metrics.push_back(panelMetric(text::labels::contactRisk, display::percent(surfaceEnemyEncounterChance(state))));
    }
    presentation.droneOpsAction = droneBayUnlocked(state)
        ? panelActionButton("Drone Ops", ui::actions::droneOps, "warn")
        : disabledPanelButton("Research Drone Bay");
    presentation.actions.push_back(surfaceActionPreview(
        text::buttons::surveySite,
        std::string(text::panel::messages::surfaceSurveyDetail),
        expedition.supply,
        tuning::research::surveySupplyCost,
        surfaceHazardRisk(expedition.hazard, tuning::research::surveyHazardChanceScale, (tools.surveyCommonBonus > 0 ? tuning::research::probeHazardRelief : 0.0) + crew.hazardRelief + upgrades.hazardRelief),
        std::string(text::labels::hazard),
        surveyPayoffChips(state, tools, crew, site),
        surfaceActionButton(text::buttons::surveySite, ui::actions::surveySurface, expedition.supply, tuning::research::surveySupplyCost)));

    SurfaceActionPreviewPresentation miningPreview = surfaceActionPreview(
        text::buttons::mineDeposit,
        std::string(text::panel::messages::surfaceMineDetail) + text::fuel::deployDetail(arkDiscovered(state)),
        expedition.supply,
        0,
        std::to_string(static_cast<int>(std::round(miningDrillStats(state, catalog).oxygenSeconds))) + "s",
        std::string(text::labels::oxygen),
        {},
        miningSurfaceActionButton(state));
    miningPreview.cost = "1 " + std::string(text::fuel::reserveLabel(arkDiscovered(state)));
    miningPreview.availability = miningSurfaceActionAvailability(state);
    miningPreview.payoffChips.push_back(panelMetric(text::labels::oxygen, std::to_string(static_cast<int>(std::round(miningDrillStats(state, catalog).oxygenSeconds))) + "s"));
    miningPreview.payoffChips.push_back(panelMetric(text::fuel::reserveLabel(arkDiscovered(state)), "-1 deploy"));
    addPositiveChip(miningPreview.payoffChips, "Tagged CM", expedition.prospectMaterials.common);
    addPositiveChip(miningPreview.payoffChips, "Tagged RM", expedition.prospectMaterials.rare);
    addPositiveChip(miningPreview.payoffChips, "Tagged EX", expedition.prospectMaterials.exotic);
    addPositiveChip(miningPreview.payoffChips, "Tagged AR", expedition.prospectArtifacts);
    presentation.actions.push_back(std::move(miningPreview));

    SurfaceActionPreviewPresentation pushPreview = surfaceActionPreview(
        text::buttons::pushDeeper,
        std::string(text::panel::messages::surfacePushDetail),
        expedition.supply,
        tuning::research::pushSupplyCost,
        surfaceHazardRisk(expedition.hazard, tuning::research::pushHazardChanceScale, (tools.extractionRiskRelief > 0.0 ? tuning::research::cargoRigHazardRelief : 0.0) + crew.hazardRelief + upgrades.hazardRelief),
        std::string(text::labels::hazard),
        pushPayoffChips(state, crew, site),
        pushSurfaceActionButton(state));
    pushPreview.availability = pushSurfaceActionAvailability(state);
    presentation.actions.push_back(std::move(pushPreview));

    presentation.actions.push_back(surfaceActionPreview(
        text::buttons::returnHome,
        std::string(text::panel::messages::surfaceExtractDetail),
        expedition.supply,
        0,
        display::percent(extractionRisk),
        std::string(text::labels::extractionRisk),
        extractPayoffChips(expedition),
        panelActionButton(text::buttons::returnHome, ui::actions::extractSurface, "ok")));
    return presentation;
}

inline SurfaceExpeditionPresentation surfaceExpeditionPresentation(const GameState& state)
{
    return surfaceExpeditionPresentation(state, createDefaultContent());
}

} // namespace rocket
