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
    std::string arenaTitle;
    std::string arenaDetail;
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
    std::string buildHook;
    std::string upgradeSummary;
    std::vector<PanelMetricPresentation> effectChips;
    PanelButtonPresentation action;
    PanelButtonPresentation upgradeAction;
};

struct DroneBuildRecipePresentation {
    std::string title;
    std::string requirements;
    std::string detail;
    std::string status;
    bool active = false;
    bool signature = false;
};

struct DroneLoadoutSlotPresentation {
    int slot = 0;
    std::string title;
    std::string role;
    std::string status;
    std::string detail;
    std::string cssClass;
    std::vector<PanelMetricPresentation> chips;
    PanelButtonPresentation action;
};

struct DroneOpsPresentation {
    std::vector<PanelMetricPresentation> metrics;
    std::vector<DetailPresentationRow> details;
    std::vector<PanelMetricPresentation> buildChips;
    std::vector<PanelMetricPresentation> buildGuidanceChips;
    std::vector<PanelMetricPresentation> forecastChips;
    std::vector<DroneBuildRecipePresentation> buildRecipes;
    std::vector<DroneLoadoutSlotPresentation> loadoutSlots;
    std::vector<MiniDroneCardPresentation> drones;
    PanelButtonPresentation upgradeSlotAction;
    PanelButtonPresentation backAction;
    std::string nextSlotCost;
    std::string buildTitle;
    std::string buildDetail;
    std::string arenaTitle;
    std::string arenaDetail;
};

inline MiningArenaRules upcomingMiningArenaRules(
    const GameState& state,
    const ContentCatalog& catalog,
    int depthOffset = 0);
inline std::string miningArenaForecastTitle(const MiningArenaRules& rules);
inline std::string miningArenaForecastDetail(const MiningArenaRules& rules);

inline std::string materialSummary(const MaterialInventory& materials)
{
    if (materials.common == 0 && materials.rare == 0 && materials.exotic == 0) {
        return std::string(text::panel::noMaterials);
    }
    return text::panel::materialSummary(materials.common, materials.rare, materials.exotic);
}

inline std::string compactMaterialSummary(const MaterialInventory& materials)
{
    std::string summary;
    const auto add = [&](int amount, std::string_view suffix) {
        if (amount <= 0) {
            return;
        }
        if (!summary.empty()) {
            summary += " ";
        }
        summary += std::to_string(amount);
        summary += suffix;
    };
    add(materials.common, "C");
    add(materials.rare, "R");
    add(materials.exotic, "E");
    return summary.empty() ? "Free" : summary;
}

inline std::string researchMaterialSummary(const MaterialInventory& cost)
{
    return "Cost: " + materialSummary(cost);
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
    const int depthReach = std::clamp(static_cast<int>(std::floor(std::max(
        std::max(0.0, stats.scannerRadius) / 2.5,
        (std::max(0.0, stats.drillDurability) + std::max(0.0, stats.drillCooling)) / 4.0))),
        0,
        2);
    addDoubleChip(chips, "Drill", stats.drillPower);
    addDoubleChip(chips, "Cooling", stats.drillCooling);
    addDoubleChip(chips, "Durability", stats.drillDurability);
    addPositiveChip(chips, "Depth reach", depthReach);
    addPercentChip(chips, "Recoil", stats.hardRockBounceRelief);
    addPercentChip(chips, "Ore yield", stats.oreYieldChance);
    addDoubleChip(chips, "Scanner", stats.scannerRadius);
    addPercentChip(chips, text::labels::hazard, stats.hazardRelief);
    addDoubleChip(chips, "Drone speed", stats.droneSpeed);
    if (stats.oxygenSeconds > 0.0) {
        chips.push_back(panelMetric("Oxygen", "+" + std::to_string(static_cast<int>(std::round(stats.oxygenSeconds))) + "s"));
    }
    addPercentChip(chips, text::labels::extractionRisk, stats.extractionRiskRelief);
    addDoubleChip(chips, "Storage", stats.droneStorage);
    addPercentChip(chips, "Haul engines", stats.droneEngineEfficiency);
    addPercentChip(chips, "Towline", stats.artifactTowEfficiency);
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

inline MiniDroneStats scaledMiniDroneStats(MiniDroneStats stats, int upgradeLevel)
{
    const double multiplier = 1.0 + 0.30 * static_cast<double>(std::clamp(upgradeLevel, 1, 3) - 1);
    stats.passiveMiningRate *= multiplier;
    stats.oxygenSeconds *= multiplier;
    stats.scannerRadius *= multiplier;
    stats.drillIntegrityRelief *= multiplier;
    stats.hardRockBounceRelief *= multiplier;
    stats.extractionRiskRelief *= multiplier;
    stats.enemyEncounterRelief *= multiplier;
    stats.sentryDamagePerSecond *= multiplier;
    stats.enemyDamageRelief *= multiplier;
    stats.areaControlDamagePerSecond *= multiplier;
    stats.enemySlow *= multiplier;
    stats.reactiveArmorDamagePerSecond *= multiplier;
    stats.environmentalShieldRelief *= multiplier;
    return stats;
}

inline std::vector<PanelMetricPresentation> miniDroneChips(
    const MiniDroneStats& stats,
    int upgradeLevel,
    MiniDroneRole role)
{
    std::vector<PanelMetricPresentation> chips;
    if (upgradeLevel > 1) {
        chips.push_back(panelMetric("Upgrade", "Mk " + std::to_string(upgradeLevel)));
    }
    if (role == MiniDroneRole::Defense) {
        chips.push_back(panelMetric(
            "Arc HP",
            display::percent(tuning::mining::defenseDroneShieldHitPoints(upgradeLevel))));
        chips.push_back(panelMetric(
            "Recharge",
            display::fixed(tuning::mining::defenseDroneRechargeSeconds(upgradeLevel), 1) + "s"));
        chips.push_back(panelMetric(
            "Tracking",
            display::fixed(tuning::mining::defenseDroneTrackingSlerpPerSecond(upgradeLevel), 2)));
        return chips;
    }
    if (role == MiniDroneRole::Hazard) {
        const std::string conditions = upgradeLevel <= 1
            ? "Thermal/Cryo"
            : (upgradeLevel == 2 ? "Thermal/Cryo/Toxic" : "All 4 (Radiation)");
        chips.push_back(panelMetric("Hazard set", conditions));
        chips.push_back(panelMetric(
            "Treatment",
            display::fixed(tuning::mining::hazardDroneTreatmentSeconds(upgradeLevel), 2) + "s"));
        chips.push_back(panelMetric(
            "Batch",
            std::to_string(tuning::mining::hazardDroneBatchSize(upgradeLevel)) +
                (tuning::mining::hazardDroneBatchSize(upgradeLevel) == 1 ? " tile" : " tiles")));
        chips.push_back(panelMetric(
            "Refine",
            display::percent(tuning::mining::hazardDroneRefinementChance(upgradeLevel))));
        return chips;
    }
    if (stats.passiveMiningRate > 0.0) {
        chips.push_back(panelMetric(
            "Mine cycle",
            display::fixed(
                tuning::mining::miningDroneWorkSeconds(upgradeLevel, MiningCellMaterial::CommonOre),
                1) + "s"));
        chips.push_back(panelMetric(
            "Haul",
            std::to_string(tuning::mining::miningDroneCapacityChunks(upgradeLevel)) + " chunks"));
    }
    if (stats.oxygenSeconds > 0.0) {
        chips.push_back(panelMetric("Oxygen", "+" + std::to_string(static_cast<int>(std::round(stats.oxygenSeconds))) + "s"));
    }
    addDoubleChip(chips, "Scanner", stats.scannerRadius);
    addPercentChip(chips, "Durability", stats.drillIntegrityRelief);
    addPercentChip(chips, "Bounce relief", stats.hardRockBounceRelief);
    addPercentChip(chips, text::labels::extractionRisk, stats.extractionRiskRelief);
    addPercentChip(chips, text::labels::contactRisk, stats.enemyEncounterRelief);
    if (stats.sentryDamagePerSecond > 0.0) {
        chips.push_back(panelMetric("Auto-fire", display::fixed(tuning::mining::alliedShotIntervalSeconds, 2) + "s"));
        chips.push_back(panelMetric("Shot power", "+" + display::fixed(stats.sentryDamagePerSecond, 1) + "/s"));
        chips.push_back(panelMetric("Crits", display::percent(tuning::mining::alliedCritChance)));
    }
    if (stats.areaControlDamagePerSecond > 0.0) {
        chips.push_back(panelMetric("Field pulse", "+" + display::fixed(stats.areaControlDamagePerSecond, 1) + "/s"));
    }
    addPercentChip(chips, "Slow", stats.enemySlow);
    if (stats.reactiveArmorDamagePerSecond > 0.0) {
        chips.push_back(panelMetric("Counter-hit", "+" + display::fixed(stats.reactiveArmorDamagePerSecond, 1) + "/s"));
    }
    addPercentChip(chips, "Shield", stats.environmentalShieldRelief);
    return chips;
}

inline std::string miniDroneBestUpgradePayoff(const MiniDroneStats& current, const MiniDroneStats& next)
{
    struct Candidate {
        std::string label;
        std::string value;
        double weight = 0.0;
    };
    std::vector<Candidate> candidates;
    auto addRate = [&](std::string label, double before, double after, double scale, std::string suffix, double weightScale) {
        const double delta = after - before;
        if (delta > 0.0001) {
            candidates.push_back({std::move(label), "+" + display::fixed(delta * scale, 1) + suffix, delta * weightScale});
        }
    };
    auto addPercent = [&](std::string label, double before, double after, double weightScale) {
        const double delta = after - before;
        if (delta > 0.0001) {
            candidates.push_back({std::move(label), display::signedPercent(delta), delta * weightScale});
        }
    };
    addRate("auto-mine", current.passiveMiningRate, next.passiveMiningRate, 60.0, "/min", 80.0);
    addRate("oxygen", current.oxygenSeconds, next.oxygenSeconds, 1.0, "s", 1.0);
    addRate("scanner", current.scannerRadius, next.scannerRadius, 1.0, " radius", 12.0);
    addPercent("durability", current.drillIntegrityRelief, next.drillIntegrityRelief, 100.0);
    addPercent("bounce relief", current.hardRockBounceRelief, next.hardRockBounceRelief, 80.0);
    addPercent("extraction risk", current.extractionRiskRelief, next.extractionRiskRelief, 130.0);
    addPercent("contact risk", current.enemyEncounterRelief, next.enemyEncounterRelief, 90.0);
    addRate("shot power", current.sentryDamagePerSecond, next.sentryDamagePerSecond, 1.0, "/s", 18.0);
    addPercent("shield", current.enemyDamageRelief + current.environmentalShieldRelief, next.enemyDamageRelief + next.environmentalShieldRelief, 120.0);
    addRate("field pulse", current.areaControlDamagePerSecond, next.areaControlDamagePerSecond, 1.0, "/s", 14.0);
    addPercent("slow field", current.enemySlow, next.enemySlow, 90.0);
    addRate("counter-hit", current.reactiveArmorDamagePerSecond, next.reactiveArmorDamagePerSecond, 1.0, "/s", 16.0);
    if (candidates.empty()) {
        return "No stat gain";
    }
    const auto best = std::max_element(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.weight < rhs.weight;
    });
    return best->label + " " + best->value;
}

inline std::string miniDroneUpgradeSummary(const MiniDrone& drone, bool owned, int upgradeLevel)
{
    if (!owned) {
        return "Acquire drone to upgrade";
    }
    if (upgradeLevel >= 3) {
        return "Mk 3 max upgrade";
    }
    const int nextLevel = upgradeLevel + 1;
    if (drone.role == MiniDroneRole::Defense) {
        return "Mk " + std::to_string(upgradeLevel) + " -> Mk " + std::to_string(nextLevel) +
            ": arc " + display::percent(tuning::mining::defenseDroneShieldHitPoints(upgradeLevel)) +
            " -> " + display::percent(tuning::mining::defenseDroneShieldHitPoints(nextLevel)) +
            ", recharge " + display::fixed(tuning::mining::defenseDroneRechargeSeconds(nextLevel), 1) +
            "s / " + materialSummary(miniDroneUpgradeCost(nextLevel));
    }
    if (drone.role == MiniDroneRole::Hazard) {
        const std::string unlock = nextLevel == 2 ? "adds Toxic" : "adds Radiation";
        return "Mk " + std::to_string(upgradeLevel) + " -> Mk " + std::to_string(nextLevel) +
            ": " + unlock + ", " +
            display::fixed(tuning::mining::hazardDroneTreatmentSeconds(nextLevel), 2) + "s, " +
            std::to_string(tuning::mining::hazardDroneBatchSize(nextLevel)) + " tiles, " +
            display::percent(tuning::mining::hazardDroneRefinementChance(nextLevel)) + " refine / " +
            materialSummary(miniDroneUpgradeCost(nextLevel));
    }
    const MiniDroneStats currentStats = scaledMiniDroneStats(drone.stats, upgradeLevel);
    const MiniDroneStats nextStats = scaledMiniDroneStats(drone.stats, nextLevel);
    return "Mk " + std::to_string(upgradeLevel) + " -> Mk " + std::to_string(nextLevel) + ": " +
        miniDroneBestUpgradePayoff(currentStats, nextStats) + " / " + materialSummary(miniDroneUpgradeCost(nextLevel));
}

inline std::string miniDroneBuildHook(MiniDroneRole role)
{
    switch (role) {
    case MiniDroneRole::Mining:
        return "Pairs with Attack for Excavation Barrage, Resource for Long Haul Rig, and Survey for Relic Pathfinder.";
    case MiniDroneRole::Resource:
        return "Pairs with Mining for Long Haul Rig, Survey for Pathfinder Loop, and Defense/Hazard for Containment Rig.";
    case MiniDroneRole::Survey:
        return "Pairs with Attack for Targeting Grid and helps unlock Sentry Killbox or Relic Pathfinder signatures.";
    case MiniDroneRole::Hazard:
        return "Pairs with Defense for Containment Screen and anchors the Containment Rig endurance signature.";
    case MiniDroneRole::Attack:
        return "Pairs with Survey for crits, Defense for volleys, and Mining for area-control excavation.";
    case MiniDroneRole::Defense:
        return "Pairs with Attack for Killbox Screen and Hazard/Resource for containment-heavy endurance builds.";
    }
    return "Equip complementary roles to unlock named drone synergies.";
}

inline MiniDroneCardPresentation miniDroneCardPresentation(const MiniDrone& drone, const GameState& state, int index)
{
    const bool unlocked = isMiniDroneUnlocked(state.meta, drone);
    const bool owned = std::find(state.meta.ownedDroneIds.begin(), state.meta.ownedDroneIds.end(), drone.id) != state.meta.ownedDroneIds.end();
    const int equippedCount = static_cast<int>(std::count(state.meta.equippedDroneIds.begin(), state.meta.equippedDroneIds.end(), drone.id));
    const bool hasFreeSlot = state.meta.equippedDroneIds.size() < static_cast<std::size_t>(std::max(0, state.meta.droneBaySlots));
    const int upgradeLevel = owned ? miniDroneUpgradeLevel(state, drone.id) : 1;
    const MaterialInventory nextUpgradeCost = miniDroneUpgradeCost(upgradeLevel + 1);
    const bool combatDrone = drone.role == MiniDroneRole::Attack || drone.role == MiniDroneRole::Defense;
    const bool coordinationRequired = combatDrone && !hasUnlock(state.meta, content::unlock::perimeterCoordination);
    PanelButtonPresentation action = disabledPanelButton(unlocked ? "Slot full" : "Locked");
    std::string status = unlocked
        ? (equippedCount > 0 ? ("Equipped x" + std::to_string(equippedCount)) : (owned ? "Ready" : "Not owned"))
        : ("Locked: " + std::string(unlockDisplayName(drone.unlockKey)));
    PanelButtonPresentation upgradeAction = disabledPanelButton(unlocked ? "Locked" : "Locked");
    std::string upgradeSummary = miniDroneUpgradeSummary(drone, owned, upgradeLevel);
    if (owned && unlocked && upgradeLevel < 3 && coordinationRequired) {
        upgradeSummary = "Mk " + std::to_string(upgradeLevel) + " tuning locked: complete Perimeter Drone Network research";
    }
    if (owned && unlocked) {
        action = hasFreeSlot ? panelActionButton("Add copy", ui::actions::equipDrone(index), "ok") : disabledPanelButton("Slot full");
        upgradeAction = upgradeLevel >= 3
            ? disabledPanelButton("Mk III")
            : (coordinationRequired
                ? disabledPanelButton("Need research")
                : (canAffordMaterials(state.meta.materials, nextUpgradeCost)
                ? panelActionButton("Upgrade", ui::actions::upgradeDrone(index), "ok")
                : disabledPanelButton("Need mats")));
    }
    return {
        index,
        std::string(toString(drone.role)),
        std::string(toString(drone.rarity)),
        drone.name,
        drone.description,
        std::move(status),
        miniDroneBuildHook(drone.role),
        std::move(upgradeSummary),
        miniDroneChips(scaledMiniDroneStats(drone.stats, upgradeLevel), upgradeLevel, drone.role),
        std::move(action),
        std::move(upgradeAction)
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
    struct DroneNameCount {
        std::string name;
        int count = 0;
    };
    std::vector<DroneNameCount> counts;
    std::string summary;
    for (const std::string& droneId : state.meta.equippedDroneIds) {
        const MiniDrone* drone = catalog.findMiniDrone(droneId);
        if (drone == nullptr) {
            continue;
        }
        auto existing = std::find_if(counts.begin(), counts.end(), [&](const DroneNameCount& count) {
            return count.name == drone->name;
        });
        if (existing != counts.end()) {
            existing->count += 1;
        } else {
            counts.push_back({drone->name, 1});
        }
    }
    for (const DroneNameCount& count : counts) {
        if (!summary.empty()) {
            summary += ", ";
        }
        summary += count.name;
        if (count.count > 1) {
            summary += " x" + std::to_string(count.count);
        }
    }
    return summary.empty() ? "No drones assigned" : summary;
}

inline std::string miniDroneSynergySummary(const MiniDroneLoadoutEffects& effects)
{
    if (effects.synergyNames.empty()) {
        return "None";
    }
    std::string summary = effects.synergyNames.front();
    for (std::size_t i = 1; i < effects.synergyNames.size(); ++i) {
        summary += ", " + effects.synergyNames[i];
    }
    return summary;
}

inline std::string droneBuildTitle(const MiniDroneLoadoutEffects& effects)
{
    if (effects.names.empty()) {
        return "No build assigned";
    }
    if (!effects.signatureName.empty()) {
        return effects.signatureName;
    }
    if (!effects.synergyNames.empty()) {
        return effects.synergyNames.front() + " build";
    }
    if (effects.sentryDamagePerSecond > 0.0 || effects.enemyDamageRelief > 0.0) {
        return "Combat support build";
    }
    if (effects.passiveMiningRate > 0.0) {
        return "Excavation support build";
    }
    if (effects.oxygenSeconds > 0.0 || effects.extractionRiskRelief > 0.0) {
        return "Endurance support build";
    }
    return "Field support build";
}

inline std::string droneBuildDetail(const MiniDroneLoadoutEffects& effects)
{
    if (effects.names.empty()) {
        return "Assign drones to create passive mining, combat, shield, scanner, and endurance synergies before the run starts.";
    }
    if (!effects.signatureName.empty()) {
        return effects.signatureDetail;
    }
    if (!effects.synergyNames.empty()) {
        return "Active synergies change how the rig survives while you mine: " + miniDroneSynergySummary(effects) + ".";
    }
    return "This loadout has useful solo drone effects. Add complementary roles to unlock named synergies.";
}

inline std::vector<PanelMetricPresentation> droneCombatForecastChips(const MiniDroneLoadoutEffects& effects)
{
    const double critChance = std::clamp(tuning::mining::alliedCritChance + effects.alliedCritChanceBonus, 0.0, tuning::mining::alliedCritChanceMaximum);
    const double shotInterval = tuning::mining::alliedShotIntervalSeconds /
        (1.0 + std::clamp(effects.alliedFireRateBonus, 0.0, tuning::mining::alliedFireRateBonusMaximum));
    const int volley = 1 + std::clamp(effects.sentryVolleyBonus, 0, tuning::mining::alliedSentryVolleyMaximum);
    const double sentryOutput = tuning::mining::baseDefenseDamagePerSecond + effects.sentryDamagePerSecond;
    const double shieldRelief = std::clamp(effects.enemyDamageRelief + effects.enemyEncounterRelief * 0.75 + effects.environmentalShieldRelief, 0.0, 0.82);
    std::vector<PanelMetricPresentation> chips {
        panelMetric("Volley", std::to_string(volley) + " target" + (volley == 1 ? "" : "s")),
        panelMetric("Cadence", display::fixed(shotInterval, 2) + "s"),
        panelMetric("Crit chance", display::percent(critChance)),
        panelMetric("Sentry output", display::fixed(sentryOutput, 1) + "/s"),
        panelMetric("Field pulse", effects.areaControlDamagePerSecond > 0.0 ? (display::fixed(effects.areaControlDamagePerSecond, 1) + "/s") : "None"),
        panelMetric("Shield relief", shieldRelief > 0.0 ? display::percent(shieldRelief) : "None"),
        panelMetric("Counter-hit", effects.reactiveArmorDamagePerSecond > 0.0 ? (display::fixed(effects.reactiveArmorDamagePerSecond, 1) + "/s") : "None"),
        panelMetric("Enemy slow", effects.enemySlow > 0.0 ? display::percent(effects.enemySlow) : "None"),
        panelMetric("Auto-mine", effects.passiveMiningRate > 0.0 ? ("+" + display::fixed(effects.passiveMiningRate * 60.0, 1) + "/min") : "None")
    };
    if (effects.names.empty()) {
        chips.push_back(panelMetric("Build state", "No drones"));
    } else if (!effects.signatureName.empty()) {
        chips.push_back(panelMetric("Build state", "Signature"));
    } else if (!effects.synergyNames.empty()) {
        chips.push_back(panelMetric("Build state", "Synergy"));
    } else {
        chips.push_back(panelMetric("Build state", "Solo effects"));
    }
    return chips;
}

inline int tunedDroneCount(const GameState& state)
{
    return static_cast<int>(std::count_if(state.meta.droneUpgrades.begin(), state.meta.droneUpgrades.end(), [](const DroneUpgradeRecord& record) {
        return record.level > 1;
    }));
}

inline int equippedMiniDroneRoleCount(const GameState& state, const ContentCatalog& catalog, MiniDroneRole role)
{
    int count = 0;
    for (const std::string& droneId : state.meta.equippedDroneIds) {
        const MiniDrone* drone = catalog.findMiniDrone(droneId);
        if (drone != nullptr && drone->role == role) {
            count += 1;
        }
    }
    return count;
}

inline std::string miniDroneRoleLabel(MiniDroneRole role)
{
    return std::string(toString(role));
}

inline std::string miniDroneRoleClass(MiniDroneRole role)
{
    switch (role) {
    case MiniDroneRole::Mining:
        return "role-mining";
    case MiniDroneRole::Resource:
        return "role-resource";
    case MiniDroneRole::Survey:
        return "role-survey";
    case MiniDroneRole::Hazard:
        return "role-hazard";
    case MiniDroneRole::Attack:
        return "role-attack";
    case MiniDroneRole::Defense:
        return "role-defense";
    }
    return "role-support";
}

inline std::vector<DroneLoadoutSlotPresentation> droneLoadoutSlots(const GameState& state, const ContentCatalog& catalog)
{
    std::vector<DroneLoadoutSlotPresentation> slots;
    constexpr int maxSlots = 6;
    const int unlockedSlots = std::clamp(state.meta.droneBaySlots, 0, maxSlots);
    auto compactMaterialSummary = [](const MaterialInventory& materials) {
        std::string summary;
        auto add = [&](int amount, std::string_view suffix) {
            if (amount <= 0) {
                return;
            }
            if (!summary.empty()) {
                summary += " ";
            }
            summary += std::to_string(amount);
            summary += suffix;
        };
        add(materials.common, "C");
        add(materials.rare, "R");
        add(materials.exotic, "E");
        return summary.empty() ? "Free" : summary;
    };
    for (int index = 0; index < maxSlots; ++index) {
        const int slotNumber = index + 1;
        if (index < unlockedSlots) {
            const bool equipped = index < static_cast<int>(state.meta.equippedDroneIds.size());
            const MiniDrone* drone = equipped ? catalog.findMiniDrone(state.meta.equippedDroneIds[static_cast<std::size_t>(index)]) : nullptr;
            if (drone != nullptr) {
                const int upgradeLevel = miniDroneUpgradeLevel(state, drone->id);
                slots.push_back({
                    slotNumber,
                    drone->name,
                    miniDroneRoleLabel(drone->role),
                    "Equipped",
                    "Mk " + std::to_string(upgradeLevel) + " " + miniDroneRoleLabel(drone->role) + " support is active in the next mining run.",
                    "filled " + miniDroneRoleClass(drone->role),
                    {
                        panelMetric("Slot", std::to_string(slotNumber)),
                        panelMetric("Mk", std::to_string(upgradeLevel))
                    },
                    panelActionButton("Unequip", ui::actions::unequipDroneSlot(index), "warn")
                });
            } else {
                slots.push_back({
                    slotNumber,
                    "Open slot",
                    "Empty",
                    "Ready",
                    "Equip a drone from the roster to add another passive ability to the build.",
                    "open",
                    {
                        panelMetric("Slot", std::to_string(slotNumber)),
                        panelMetric("State", "Open")
                    },
                    {}
                });
            }
            continue;
        }

        const MaterialInventory cost = droneSlotUpgradeCost(slotNumber);
        slots.push_back({
            slotNumber,
            "Locked slot",
            "Locked",
            index == unlockedSlots ? "Next bay upgrade" : "Locked",
            index == unlockedSlots
                ? ("Upgrade Drone Bay to unlock this build slot: " + materialSummary(cost) + ".")
                : "Unlock earlier bay slots before this position becomes available.",
            "locked",
            {
                panelMetric("Slot", std::to_string(slotNumber)),
                panelMetric(index == unlockedSlots ? "Cost" : "Prior", index == unlockedSlots ? compactMaterialSummary(cost) : "")
            },
            {}
        });
    }
    return slots;
}

inline std::string droneRecipeRequirements(const std::vector<MiniDroneRole>& roles)
{
    if (roles.empty()) {
        return "Any loadout";
    }
    std::string result = miniDroneRoleLabel(roles.front());
    for (std::size_t i = 1; i < roles.size(); ++i) {
        result += " + " + miniDroneRoleLabel(roles[i]);
    }
    return result;
}

inline DroneBuildRecipePresentation droneBuildRecipe(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string title,
    std::vector<MiniDroneRole> roles,
    std::string detail,
    bool signature)
{
    std::vector<std::string> missing;
    for (MiniDroneRole role : roles) {
        if (equippedMiniDroneRoleCount(state, catalog, role) <= 0) {
            missing.push_back(miniDroneRoleLabel(role));
        }
    }
    std::string status = "Active";
    if (!missing.empty()) {
        status = "Need " + missing.front();
        for (std::size_t i = 1; i < missing.size(); ++i) {
            status += ", " + missing[i];
        }
    }
    return {
        std::move(title),
        droneRecipeRequirements(roles),
        std::move(detail),
        std::move(status),
        missing.empty(),
        signature
    };
}

inline std::vector<DroneBuildRecipePresentation> droneBuildRecipes(const GameState& state, const ContentCatalog& catalog)
{
    return {
        droneBuildRecipe(state, catalog, "Targeting Grid", {MiniDroneRole::Attack, MiniDroneRole::Survey}, "Crit chance, fire rate, and scanner paint for priority targets.", false),
        droneBuildRecipe(state, catalog, "Killbox Screen", {MiniDroneRole::Attack, MiniDroneRole::Defense}, "Extra sentry target plus shield and retaliatory damage.", false),
        droneBuildRecipe(state, catalog, "Excavation Barrage", {MiniDroneRole::Attack, MiniDroneRole::Mining}, "Mining output and area-control pressure in the work zone.", false),
        droneBuildRecipe(state, catalog, "Containment Screen", {MiniDroneRole::Defense, MiniDroneRole::Hazard}, "Faster hazard treatment backed by stronger environmental shielding.", false),
        droneBuildRecipe(state, catalog, "Long Haul Rig", {MiniDroneRole::Mining, MiniDroneRole::Resource}, "More passive excavation, oxygen, and safer extraction.", false),
        droneBuildRecipe(state, catalog, "Pathfinder Loop", {MiniDroneRole::Resource, MiniDroneRole::Survey}, "Scanner reach and extraction safety for artifact routes.", false),
        droneBuildRecipe(state, catalog, "Sentry Killbox", {MiniDroneRole::Attack, MiniDroneRole::Defense, MiniDroneRole::Survey}, "Signature: faster volleys, better crits, and tougher shields.", true),
        droneBuildRecipe(state, catalog, "Excavation Storm", {MiniDroneRole::Attack, MiniDroneRole::Mining, MiniDroneRole::Resource}, "Signature: ore flow stays high while combat pulses slow enemies.", true),
        droneBuildRecipe(state, catalog, "Containment Rig", {MiniDroneRole::Defense, MiniDroneRole::Hazard, MiniDroneRole::Resource}, "Signature: fast remediation with shields, reserve time, and counter-hits.", true),
        droneBuildRecipe(state, catalog, "Relic Pathfinder", {MiniDroneRole::Mining, MiniDroneRole::Resource, MiniDroneRole::Survey}, "Signature: artifact routing with wider scans and safer extraction.", true),
        droneBuildRecipe(state, catalog, "Full Spectrum Swarm", {MiniDroneRole::Attack, MiniDroneRole::Defense, MiniDroneRole::Survey, MiniDroneRole::Mining, MiniDroneRole::Resource, MiniDroneRole::Hazard}, "Capstone: every role online for volleys, scans, remediation, shields, logistics, and mining.", true)
    };
}

struct DroneBuildGuidancePresentation {
    std::string nextRecipe;
    std::string missingRoles;
    std::string tuneNext;
    std::string runPosture;
    std::string detail;
};

inline std::string droneRoleListSummary(const std::vector<MiniDroneRole>& roles)
{
    if (roles.empty()) {
        return "None";
    }
    std::string result = miniDroneRoleLabel(roles.front());
    for (std::size_t i = 1; i < roles.size(); ++i) {
        result += ", " + miniDroneRoleLabel(roles[i]);
    }
    return result;
}

inline std::string droneRunPosture(const MiniDroneLoadoutEffects& effects)
{
    switch (effects.signatureKind) {
    case MiniDroneSignatureKind::SentryKillbox:
        return "Killbox";
    case MiniDroneSignatureKind::ExcavationStorm:
        return "Greedy mine";
    case MiniDroneSignatureKind::ContainmentRig:
        return "Hold ground";
    case MiniDroneSignatureKind::RelicPathfinder:
        return "Artifact route";
    case MiniDroneSignatureKind::FullSpectrumSwarm:
        return "Capstone";
    case MiniDroneSignatureKind::None:
        break;
    }
    if (effects.sentryDamagePerSecond > 0.0 || effects.areaControlDamagePerSecond > 0.0) {
        return "Cover fire";
    }
    if (effects.enemyDamageRelief > 0.0 || effects.environmentalShieldRelief > 0.0) {
        return "Shield line";
    }
    if (effects.passiveMiningRate > 0.0) {
        return "Ore tempo";
    }
    if (effects.scannerRadius > 0.0 || effects.extractionRiskRelief > 0.0) {
        return "Route scout";
    }
    return effects.names.empty() ? "Open bay" : "Field support";
}

inline std::string droneTunePriority(const GameState& state, const ContentCatalog& catalog, const MiniDroneLoadoutEffects& effects)
{
    auto shortDroneName = [](const MiniDrone& drone) {
        std::string name = drone.name;
        const std::string suffix = " Drone";
        if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            name.erase(name.size() - suffix.size());
        }
        return name;
    };
    auto equippedDroneWithRole = [&](MiniDroneRole role) -> const MiniDrone* {
        for (const std::string& droneId : state.meta.equippedDroneIds) {
            const MiniDrone* drone = catalog.findMiniDrone(droneId);
            if (drone != nullptr && drone->role == role && miniDroneUpgradeLevel(state, drone->id) < 3) {
                return drone;
            }
        }
        return nullptr;
    };

    std::vector<MiniDroneRole> priorities;
    if (effects.sentryDamagePerSecond > 0.0 || effects.areaControlDamagePerSecond > 0.0 || effects.alliedCritChanceBonus > 0.0) {
        priorities.push_back(MiniDroneRole::Attack);
    }
    if (effects.enemyDamageRelief > 0.0 || effects.reactiveArmorDamagePerSecond > 0.0 || effects.environmentalShieldRelief > 0.0) {
        priorities.push_back(MiniDroneRole::Defense);
    }
    if (effects.passiveMiningRate > 0.0) {
        priorities.push_back(MiniDroneRole::Mining);
    }
    if (effects.oxygenSeconds > 0.0 || effects.extractionRiskRelief > 0.0) {
        priorities.push_back(MiniDroneRole::Resource);
    }
    if (effects.scannerRadius > 0.0) {
        priorities.push_back(MiniDroneRole::Survey);
    }
    if (effects.hazardTreatmentRateBonus > 0.0) {
        priorities.push_back(MiniDroneRole::Hazard);
    }

    for (MiniDroneRole role : priorities) {
        if (const MiniDrone* drone = equippedDroneWithRole(role)) {
            return shortDroneName(*drone) + " Mk " + std::to_string(miniDroneUpgradeLevel(state, drone->id) + 1);
        }
    }
    for (const std::string& droneId : state.meta.equippedDroneIds) {
        const MiniDrone* drone = catalog.findMiniDrone(droneId);
        if (drone != nullptr && miniDroneUpgradeLevel(state, drone->id) < 3) {
            return shortDroneName(*drone) + " Mk " + std::to_string(miniDroneUpgradeLevel(state, drone->id) + 1);
        }
    }
    return state.meta.equippedDroneIds.empty() ? "Equip first" : "All Mk III";
}

inline DroneBuildGuidancePresentation droneBuildGuidance(const GameState& state, const ContentCatalog& catalog, const MiniDroneLoadoutEffects& effects)
{
    struct Candidate {
        std::string title;
        std::vector<MiniDroneRole> roles;
        std::string detail;
        bool signature = false;
    };
    const std::vector<Candidate> candidates {
        {"Targeting Grid", {MiniDroneRole::Attack, MiniDroneRole::Survey}, "Add scanner paint to raise crit chance and drone fire rate.", false},
        {"Killbox Screen", {MiniDroneRole::Attack, MiniDroneRole::Defense}, "Pair cover fire with shields so close threats trigger counter-hits.", false},
        {"Excavation Barrage", {MiniDroneRole::Attack, MiniDroneRole::Mining}, "Turn ore tempo into area-control pressure around the work zone.", false},
        {"Containment Screen", {MiniDroneRole::Defense, MiniDroneRole::Hazard}, "Pair remediation with environmental shielding for safer long digs.", false},
        {"Long Haul Rig", {MiniDroneRole::Mining, MiniDroneRole::Resource}, "Keep ore and oxygen flowing for deeper mining routes.", false},
        {"Pathfinder Loop", {MiniDroneRole::Resource, MiniDroneRole::Survey}, "Scout artifact paths and reduce extraction pressure.", false},
        {"Sentry Killbox", {MiniDroneRole::Attack, MiniDroneRole::Defense, MiniDroneRole::Survey}, "Next logical combat signature: volleys, crits, and shield relief.", true},
        {"Excavation Storm", {MiniDroneRole::Attack, MiniDroneRole::Mining, MiniDroneRole::Resource}, "A greedier mining signature that keeps damage pulsing while ore flows.", true},
        {"Containment Rig", {MiniDroneRole::Defense, MiniDroneRole::Hazard, MiniDroneRole::Resource}, "The remediation and endurance signature for hazardous long digs.", true},
        {"Relic Pathfinder", {MiniDroneRole::Mining, MiniDroneRole::Resource, MiniDroneRole::Survey}, "The artifact-routing signature for safer, wider recovery lines.", true},
        {"Full Spectrum Swarm", {MiniDroneRole::Attack, MiniDroneRole::Defense, MiniDroneRole::Survey, MiniDroneRole::Mining, MiniDroneRole::Resource, MiniDroneRole::Hazard}, "Capstone build: every role online for combat, logistics, scans, remediation, and mining.", true}
    };

    const Candidate* best = nullptr;
    std::vector<MiniDroneRole> bestMissing;
    int bestMissingCount = 99;
    for (const Candidate& candidate : candidates) {
        std::vector<MiniDroneRole> missing;
        for (MiniDroneRole role : candidate.roles) {
            if (equippedMiniDroneRoleCount(state, catalog, role) <= 0) {
                missing.push_back(role);
            }
        }
        if (missing.empty()) {
            continue;
        }
        const int missingCount = static_cast<int>(missing.size());
        const bool preferCandidate =
            best == nullptr ||
            missingCount < bestMissingCount ||
            (missingCount == bestMissingCount && candidate.signature && !best->signature);
        if (preferCandidate) {
            best = &candidate;
            bestMissing = std::move(missing);
            bestMissingCount = missingCount;
        }
    }

    if (best == nullptr) {
        return {
            "Full Spectrum Swarm",
            "None",
            droneTunePriority(state, catalog, effects),
            droneRunPosture(effects),
            "Every build recipe is active. Spend materials on favorite drones and push hostile mining depth."
        };
    }
    if (state.meta.equippedDroneIds.empty()) {
        return {
            "First role",
            "Mining or Attack",
            droneTunePriority(state, catalog, effects),
            droneRunPosture(effects),
            "Start with Mining for ore tempo or Attack for hostile-system cover, then pair a second role to unlock the first named recipe."
        };
    }
    return {
        best->title,
        droneRoleListSummary(bestMissing),
        droneTunePriority(state, catalog, effects),
        droneRunPosture(effects),
        best->detail
    };
}

inline DroneOpsPresentation droneOpsPresentation(GameState state, const ContentCatalog& catalog)
{
    ensureDroneBayState(state, catalog);
    const MiniDroneLoadoutEffects effects = miniDroneLoadoutEffects(state, catalog);
    const DroneBuildGuidancePresentation guidance = droneBuildGuidance(state, catalog, effects);
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
    presentation.buildTitle = droneBuildTitle(effects);
    presentation.buildDetail = droneBuildDetail(effects);
    presentation.buildChips = {
        panelMetric("Signature", effects.signatureName.empty() ? "None" : effects.signatureName),
        panelMetric("Active synergies", std::to_string(static_cast<int>(effects.synergyNames.size()))),
        panelMetric("Upgraded drones", std::to_string(tunedDroneCount(state))),
        panelMetric("Crit chance", display::percent(std::clamp(tuning::mining::alliedCritChance + effects.alliedCritChanceBonus, 0.0, tuning::mining::alliedCritChanceMaximum))),
        panelMetric("Volley", std::to_string(1 + effects.sentryVolleyBonus)),
        panelMetric("Fire rate", effects.alliedFireRateBonus > 0.0 ? ("+" + display::percent(effects.alliedFireRateBonus)) : "Base")
    };
    presentation.buildGuidanceChips = {
        panelMetric("Recipe", guidance.nextRecipe),
        panelMetric("Missing", guidance.missingRoles),
        panelMetric("Upgrade", guidance.tuneNext),
        panelMetric("Posture", guidance.runPosture)
    };
    presentation.forecastChips = droneCombatForecastChips(effects);
    presentation.loadoutSlots = droneLoadoutSlots(state, catalog);
    presentation.buildRecipes = droneBuildRecipes(state, catalog);
    presentation.details = {
        detailPresentationRow("Drone Bay", std::to_string(std::max(0, state.meta.droneBaySlots)) + " slot capacity"),
        detailPresentationRow("Loadout", miniDroneNameSummary(state, catalog)),
        detailPresentationRow("Build signature", effects.signatureName.empty() ? "None" : effects.signatureName),
        detailPresentationRow("Signature payoff", effects.signatureDetail.empty() ? "Equip three complementary roles to activate a signature build." : effects.signatureDetail),
        detailPresentationRow("Build guidance", guidance.detail),
        detailPresentationRow("Next recipe", guidance.nextRecipe + " / Missing: " + guidance.missingRoles + " / Upgrade: " + guidance.tuneNext),
        detailPresentationRow("Drone copies", std::string("Drone controls add another copy of an owned type. Drone Loadout slots unequip one copy at a time.")),
        detailPresentationRow("Drone upgrades", std::to_string(tunedDroneCount(state)) + " drone types above Mk I. Tuning scales every equipped copy of that type."),
        detailPresentationRow("Active synergies", miniDroneSynergySummary(effects)),
        detailPresentationRow("Mining support", effects.passiveMiningRate > 0.0 ? ("+" + display::fixed(effects.passiveMiningRate * 60.0, 1) + " common/min") : "None"),
        detailPresentationRow("Oxygen support", effects.oxygenSeconds > 0.0 ? ("+" + std::to_string(static_cast<int>(std::round(effects.oxygenSeconds))) + "s") : "None"),
        detailPresentationRow("Scanner support", effects.scannerRadius > 0.0 ? ("+" + display::fixed(effects.scannerRadius, 1) + " radius") : "None"),
        detailPresentationRow("Stability support", effects.hardRockBounceRelief > 0.0 ? display::percent(effects.hardRockBounceRelief) + " less hard-rock bounce" : "None"),
        detailPresentationRow("Passive combat plan", std::string("During hostile mining, the rig mines while equipped mini-drones auto-fire, shield, slow, and counter-hit enemies.")),
        detailPresentationRow("Combat forecast", std::string("The forecast row shows the passive combat profile that will carry into the next hostile mining run.")),
        detailPresentationRow("Upgrade path", std::string("Material-paid bay slots are the build lever: more slots mean more passive abilities active while you focus on ore and artifacts.")),
        detailPresentationRow("Combat support", effects.sentryDamagePerSecond > 0.0 || effects.enemyDamageRelief > 0.0
            ? display::fixed(effects.sentryDamagePerSecond + effects.areaControlDamagePerSecond, 1) + "/s sentry output, " + display::percent(effects.enemyDamageRelief + effects.environmentalShieldRelief) + " shield relief"
            : "Attack and Defense drones unlock after hostile surface encounters beyond the solar system.")
    };
    if (state.run.surfaceExpedition.active && !state.run.surfaceExpedition.destinationId.empty()) {
        const MiningArenaRules arenaRules = upcomingMiningArenaRules(state, catalog);
        const MiningGateType gateType = selectMiningGateType(arenaRules);
        const MiningGateDefinition gate = resolveMiningGateDefinition(
            arenaRules,
            gateType,
            arenaRules.fixedStoryGate == gateType || arenaRules.request.gateOverrideEnabled);
        const MiningCapabilityProfile capability = miningCapabilityProfile(state, catalog);
        presentation.arenaTitle = miningArenaForecastTitle(arenaRules);
        if (gateType != MiningGateType::None) {
            presentation.arenaTitle += " | Site: " + std::string(gate.name);
        }
        presentation.arenaDetail = miningArenaForecastDetail(arenaRules);
        presentation.details.insert(presentation.details.begin(), {
            detailPresentationRow("Upcoming arena", presentation.arenaTitle),
            detailPresentationRow("Artifact site", std::string(gate.name)),
            detailPresentationRow("Required capability", std::string(gate.requiredCapability)),
            detailPresentationRow("Current loadout", miningGateCapabilityStatus(capability, gate)),
            detailPresentationRow("New complication", std::string(arenaRules.complication)),
            detailPresentationRow("Mineral forecast", std::string(arenaRules.mineralAvailability)),
            detailPresentationRow("Known enemy roles", std::string(arenaRules.knownEnemyRoles)),
            detailPresentationRow("Recommended counters", std::string(arenaRules.recommendedCounters))
        });
    }

    for (int index = 0; index < static_cast<int>(catalog.miniDrones.size()); ++index) {
        presentation.drones.push_back(miniDroneCardPresentation(catalog.miniDrones[static_cast<std::size_t>(index)], state, index));
    }
    presentation.nextSlotCost = maxed ? "Max capacity" : compactMaterialSummary(nextCost);
    const std::string blockedSlotLabel = "Need mats";
    presentation.upgradeSlotAction = maxed
        ? disabledPanelButton("Bay maxed")
        : (affordable ? panelActionButton("Add slot", ui::actions::upgradeDroneSlot, "ok") : disabledPanelButton(blockedSlotLabel));
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
        researchMaterialSummary(project.materialCost),
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

inline PanelButtonPresentation fieldSurfaceActionButton(const GameState& state, std::string_view label, std::string_view actionId, int cost, std::string cssClass = "")
{
    if (state.run.surfaceExpedition.miningRunUsed) {
        return disabledPanelButton(text::buttons::unavailable);
    }
    return surfaceActionButton(label, actionId, state.run.surfaceExpedition.supply, cost, std::move(cssClass));
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

inline std::string fieldSurfaceActionAvailability(const GameState& state, int cost)
{
    if (state.run.surfaceExpedition.miningRunUsed) {
        return std::string(text::panel::messages::surfaceFieldworkClosed);
    }
    return surfaceActionAvailability(state.run.surfaceExpedition.supply, cost);
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
    return fieldSurfaceActionButton(state, text::buttons::pushDeeper, ui::actions::pushSurface, tuning::research::pushSupplyCost, "danger");
}

inline std::string pushSurfaceActionAvailability(const GameState& state)
{
    return fieldSurfaceActionAvailability(state, tuning::research::pushSupplyCost);
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

inline SurfaceExpeditionPresentation surfacePosturePresentation(const SurfaceExpeditionState& expedition, double extractionRisk, bool arkKnown)
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
        presentation.postureDetail = text::panel::messages::surfacePostureExtractDetailForHome(arkKnown);
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

inline MiningArenaRules upcomingMiningArenaRules(
    const GameState& state,
    const ContentCatalog& catalog,
    int depthOffset)
{
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    const int completedHostileSorties = destinationHistoryValue(
        state.meta.destinationSuccesses,
        catalog,
        expedition.destinationId);
    const int landingOrdinal = destinationHistoryValue(
        state.meta.destinationLandings,
        catalog,
        expedition.destinationId);
    MiningArenaRequest request = campaignMiningArenaRequest(
        state.meta.chapter,
        expedition.destinationId,
        std::max(0, expedition.depth + depthOffset),
        completedHostileSorties,
        state.seed,
        landingOrdinal);
    if (const MiningStorySiteProgress* site = pendingMiningStorySite(state.meta, expedition.destinationId)) {
        request.act = site->act;
        request.difficulty = site->difficulty;
        request.seed = site->seed;
        request.gateOverrideEnabled = true;
        request.gateOverride = site->gateType;
    }
    return resolveMiningArenaRules(request);
}

inline std::string miningArenaForecastTitle(const MiningArenaRules& rules)
{
    return std::string(miningActName(rules.request.act))
        + " • Level " + std::to_string(rules.request.difficulty)
        + " • " + std::string(miningProgressionBandName(rules.band));
}

inline std::string miningArenaForecastDetail(const MiningArenaRules& rules)
{
    // This row shares the fixed-height Surface Ops board with the four action
    // cards. Keep the forecast scannable in one line so those cards retain
    // their protected footer and button lanes.
    return "New: " + std::string(rules.complication)
        + " • " + std::string(rules.mineralAvailability)
        + " • " + std::string(rules.knownEnemyRoles)
        + " • " + std::string(rules.recommendedCounters);
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
        detailPresentationRow(text::panel::details::surveyRisk, std::string("Scans forecast one layer per pulse: +0 first, then layers available through Push Deeper. Dust can still burn extra action kits.")),
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
    const bool arkKnown = arkDiscovered(state);
    const MiningArenaRules arenaRules = upcomingMiningArenaRules(state, catalog);
    const MiningGateType gateType = selectMiningGateType(arenaRules);
    const MiningGateDefinition gate = resolveMiningGateDefinition(
        arenaRules,
        gateType,
        arenaRules.fixedStoryGate == gateType || arenaRules.request.gateOverrideEnabled);
    const MiningCapabilityProfile capability = miningCapabilityProfile(state, catalog);
    SurfaceExpeditionPresentation presentation = surfacePosturePresentation(expedition, extractionRisk, arkKnown);
    presentation.phaseSteps = postArrivalPhaseSteps(Screen::SurfaceExpedition);
    presentation.briefing = postArrivalPhaseBriefing(Screen::SurfaceExpedition);
    presentation.siteDetail = std::string(surfaceSiteProfileDetail(expedition.siteProfile));
    presentation.arenaTitle = miningArenaForecastTitle(arenaRules);
    if (gateType != MiningGateType::None) {
        presentation.arenaTitle += " | Site: " + std::string(gate.name);
    }
    presentation.arenaDetail = miningArenaForecastDetail(arenaRules);
    presentation.details = surfaceDetailsPresentation(expedition, state.meta, crew, upgrades, extractionRisk, arkKnown);
    presentation.details.insert(presentation.details.begin(), {
        detailPresentationRow("Upcoming arena", presentation.arenaTitle),
        detailPresentationRow("Artifact site", std::string(gate.name)),
        detailPresentationRow("Required capability", std::string(gate.requiredCapability)),
        detailPresentationRow("Current loadout", miningGateCapabilityStatus(capability, gate)),
        detailPresentationRow("New complication", std::string(arenaRules.complication)),
        detailPresentationRow("Mineral forecast", std::string(arenaRules.mineralAvailability)),
        detailPresentationRow("Known enemy roles", std::string(arenaRules.knownEnemyRoles)),
        detailPresentationRow("Recommended counters", std::string(arenaRules.recommendedCounters))
    });
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
        panelMetric(text::fuel::reserveLabel(arkKnown), std::to_string(expedition.sharedFuel) + "/" + std::to_string(std::max(1, expedition.sharedFuelCapacity))),
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
    SurfaceActionPreviewPresentation surveyPreview = surfaceActionPreview(
        text::buttons::surveySite,
        std::string(text::panel::messages::surfaceSurveyDetail),
        expedition.supply,
        tuning::research::surveySupplyCost,
        surfaceHazardRisk(expedition.hazard, tuning::research::surveyHazardChanceScale, (tools.surveyCommonBonus > 0 ? tuning::research::probeHazardRelief : 0.0) + crew.hazardRelief + upgrades.hazardRelief),
        std::string(text::labels::hazard),
        surveyPayoffChips(state, tools, crew, site),
        fieldSurfaceActionButton(state, text::buttons::surveySite, ui::actions::surveySurface, tuning::research::surveySupplyCost));
    surveyPreview.payoffChips.push_back(panelMetric("Arena", std::string(miningActName(arenaRules.request.act)) + " L" + std::to_string(arenaRules.request.difficulty)));
    presentation.actions.push_back(std::move(surveyPreview));
    presentation.actions.back().availability = fieldSurfaceActionAvailability(state, tuning::research::surveySupplyCost);

    SurfaceActionPreviewPresentation miningPreview = surfaceActionPreview(
        text::buttons::mineDeposit,
        std::string(text::panel::messages::surfaceMineDetail) + text::fuel::deployDetail(arkKnown),
        expedition.supply,
        0,
        std::to_string(static_cast<int>(std::round(miningDrillStats(state, catalog).oxygenSeconds))) + "s",
        std::string(text::labels::oxygen),
        {},
        miningSurfaceActionButton(state));
    miningPreview.cost = "1 " + std::string(text::fuel::reserveLabel(arkKnown));
    miningPreview.availability = miningSurfaceActionAvailability(state);
    miningPreview.payoffChips.push_back(panelMetric(text::labels::oxygen, std::to_string(static_cast<int>(std::round(miningDrillStats(state, catalog).oxygenSeconds))) + "s"));
    miningPreview.payoffChips.push_back(panelMetric(text::fuel::reserveLabel(arkKnown), "-1 deploy"));
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
    const MiningArenaRules deeperArenaRules = upcomingMiningArenaRules(state, catalog, 1);
    pushPreview.payoffChips.push_back(panelMetric("Next arena", std::string(miningActName(deeperArenaRules.request.act)) + " L" + std::to_string(deeperArenaRules.request.difficulty)));
    presentation.actions.push_back(std::move(pushPreview));

    presentation.actions.push_back(surfaceActionPreview(
        text::buttons::returnHomeLabel(arkKnown),
        text::panel::messages::surfaceExtractDetailForHome(arkKnown),
        expedition.supply,
        0,
        display::percent(extractionRisk),
        std::string(text::labels::extractionRisk),
        extractPayoffChips(expedition),
        panelActionButton(text::buttons::returnHomeLabel(arkKnown), ui::actions::extractSurface, "ok")));
    return presentation;
}

inline SurfaceExpeditionPresentation surfaceExpeditionPresentation(const GameState& state)
{
    return surfaceExpeditionPresentation(state, createDefaultContent());
}

} // namespace rocket
