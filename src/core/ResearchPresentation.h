#pragma once

#include "core/ContentIds.h"
#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/GameUi.h"
#include "core/MiningSystem.h"
#include "core/PanelPresentation.h"
#include "core/ResearchSystem.h"
#include "core/ScenarioSystem.h"
#include "core/Tuning.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
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

struct PhaseAdvisoryPresentation {
    std::string title;
    std::string detail;
    std::string cssClass;
};

struct SurfaceReturnSafetyPresentation {
    SurfaceReturnSafetySeverity severity = SurfaceReturnSafetySeverity::Safe;
    int depth = 0;
    int estimatedReturnSeconds = 0;
    int oxygenSeconds = 0;
    int fuelNeededAfterDeployment = 0;
    int fuelAvailableAfterDeployment = 0;
    double fuelCycleSeconds = tuning::rigFuelLoopProgression::baseCycleSeconds;
    std::string title;
    std::string detail;
    std::string cssClass;
};

struct ResearchPhasePresentation {
    std::vector<PhaseStepPresentation> phaseSteps;
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
    std::string summary;
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
    bool droneModule = false;
    std::string hostRole;
    std::string secondaryRole;
};

inline std::string miniDroneRoleLabel(MiniDroneRole role);

struct SurfaceExpeditionPresentation {
    std::vector<PhaseStepPresentation> phaseSteps;
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

// A delivery contract is a scenario event, not a campaign destination. Keep
// the Surface Ops presentation bound to the configured material target and
// the safely banked Mining Rig payload so authored and generated scenarios
// use the same extraction UI.
struct ScenarioDeliveryPresentation {
    ScenarioObjectivePresentation objective;
    int safelyAboard = 0;
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

inline int scenarioObjectiveRank(ScenarioStepState state)
{
    switch (state) {
    case ScenarioStepState::ReadyToClaim:
        return 0;
    case ScenarioStepState::Active:
        return 1;
    case ScenarioStepState::Locked:
        return 2;
    case ScenarioStepState::Complete:
        return 3;
    }
    return 4;
}

inline int scenarioTargetMaterialAmount(
    const MaterialInventory& materials,
    std::string_view targetId)
{
    if (targetId == "common") {
        return materials.common;
    }
    if (targetId == "rare") {
        return materials.rare;
    }
    if (targetId == "exotic") {
        return materials.exotic;
    }
    return 0;
}

inline std::string scenarioTargetMaterialLabel(std::string_view targetId)
{
    if (targetId == "common") {
        return "Common Ore";
    }
    if (targetId == "rare") {
        return "Rare Ore";
    }
    if (targetId == "exotic") {
        return "Exotic Ore";
    }
    return targetId.empty() ? "materials" : std::string(targetId);
}

inline ScenarioObjectivePresentation scenarioSafeDeliveryObjectiveForDestination(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId)
{
    ScenarioObjectivePresentation best;
    int bestRank = 100;
    for (const ScenarioInstance& instance : state.meta.scenarios) {
        const ScenarioDefinition* definition =
            scenarioDefinitionForRuntimeId(state, catalog, instance.id);
        if (definition == nullptr) {
            continue;
        }
        const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, instance);
        if (resolved.destinationId != destinationId) {
            continue;
        }
        for (const ScenarioStepDefinition& step : resolved.steps) {
            if (step.completionEvent != ScenarioEventKind::SafeMaterialDelivered) {
                continue;
            }
            ScenarioObjectivePresentation candidate =
                scenarioObjectivePresentation(state, catalog, instance.id, step.id);
            if (!candidate.available) {
                continue;
            }
            const int rank = scenarioObjectiveRank(candidate.state);
            if (rank < bestRank) {
                best = std::move(candidate);
                bestRank = rank;
            }
        }
    }
    return best;
}

inline ScenarioDeliveryPresentation scenarioSafeDeliveryPresentation(
    const GameState& state,
    const ContentCatalog& catalog,
    const SurfaceExpeditionState& expedition)
{
    ScenarioDeliveryPresentation presentation;
    if (!expedition.active) {
        return presentation;
    }
    presentation.objective = scenarioSafeDeliveryObjectiveForDestination(
        state,
        catalog,
        expedition.destinationId);
    if (!presentation.objective.available ||
        !expedition.bankedMiningArenaValid ||
        !expedition.bankedMiningProgressionEligible) {
        return presentation;
    }
    presentation.safelyAboard = std::max(
        0,
        std::min(
            scenarioTargetMaterialAmount(
                expedition.bankedMiningMaterials,
                presentation.objective.eventTargetId),
            scenarioTargetMaterialAmount(
                expedition.temporaryMaterials,
                presentation.objective.eventTargetId)));
    return presentation;
}

inline ScenarioObjectivePresentation scenarioCapacityRewardObjective(
    const GameState& state,
    const ContentCatalog& catalog,
    int requiredSlots)
{
    ScenarioObjectivePresentation best;
    int bestRank = 100;
    for (const ScenarioInstance& instance : state.meta.scenarios) {
        const ScenarioDefinition* definition =
            scenarioDefinitionForRuntimeId(state, catalog, instance.id);
        if (definition == nullptr) {
            continue;
        }
        const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, instance);
        for (const ScenarioStepDefinition& step : resolved.steps) {
            const bool grantsRequiredCapacity = std::any_of(
                step.rewards.begin(),
                step.rewards.end(),
                [&](const ScenarioReward& reward) {
                    return reward.kind == ScenarioRewardKind::DroneBaySlots &&
                        reward.amount >= requiredSlots;
                });
            if (!grantsRequiredCapacity) {
                continue;
            }
            ScenarioObjectivePresentation candidate =
                scenarioObjectivePresentation(state, catalog, instance.id, step.id);
            if (!candidate.available) {
                continue;
            }
            const int rank = scenarioObjectiveRank(candidate.state);
            if (rank < bestRank) {
                best = std::move(candidate);
                bestRank = rank;
            }
        }
    }
    return best;
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
    const bool debugResearch = screen == Screen::Research;
    const bool approachDone = debugResearch || screen == Screen::SurfaceExpedition || screen == Screen::Upgrade;
    const bool surfaceDone = screen == Screen::Upgrade;
    return {
        phaseStep(text::panel::details::arrivalPhase, arrivalActive ? "Now" : "Done", arrivalActive ? "active" : "done"),
        phaseStep(debugResearch ? "Research (Debug)" : "Approach", debugResearch ? "Now" : (approachDone ? "Done" : "Next"), debugResearch ? "active" : (approachDone ? "done" : "pending")),
        phaseStep(text::panel::details::surfacePhase, screen == Screen::SurfaceExpedition ? "Now" : (surfaceDone ? "Done" : "Next"), screen == Screen::SurfaceExpedition ? "active" : (surfaceDone ? "done" : "pending")),
        phaseStep(text::panel::details::refitPhase, screen == Screen::Upgrade ? "Now" : "Next", screen == Screen::Upgrade ? "active" : "pending")
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
    addDoubleChip(chips, "Rig speed", stats.droneSpeed);
    if (stats.oxygenSeconds > 0.0) {
        chips.push_back(panelMetric("Oxygen", "+" + std::to_string(static_cast<int>(std::round(stats.oxygenSeconds))) + "s"));
    }
    addDoubleChip(chips, "Storage", stats.droneStorage);
    addPercentChip(chips, "Haul engines", stats.droneEngineEfficiency);
    addPercentChip(chips, "Towline", stats.artifactTowEfficiency);
    if (stats.scannerPulseDamage > 0) {
        chips.push_back(panelMetric("Scan pulse", "+" + std::to_string(stats.scannerPulseDamage) + " damage / rank"));
    }
    return chips;
}

inline MiniDroneStats scaledMiniDroneStats(MiniDroneStats stats, int upgradeLevel)
{
    const double multiplier = 1.0 + 0.30 * static_cast<double>(std::clamp(upgradeLevel, 1, 3) - 1);
    stats.passiveMiningRate *= multiplier;
    stats.oxygenSeconds *= multiplier;
    stats.scannerRadius *= multiplier;
    stats.drillIntegrityRelief *= multiplier;
    stats.hardRockBounceRelief *= multiplier;
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
        chips.push_back(panelMetric("Upgrade", "Mk " + runUpgradeRankLabel(upgradeLevel)));
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

inline std::vector<PanelMetricPresentation> droneSynergyChips(const DroneSynergyStats& stats)
{
    std::vector<PanelMetricPresentation> chips;
    addPercentChip(chips, "Passive mining", stats.passiveMiningRate);
    if (stats.oxygenSeconds > 0.0) {
        chips.push_back(panelMetric("Oxygen", "+" + std::to_string(static_cast<int>(std::round(stats.oxygenSeconds))) + "s"));
    }
    addDoubleChip(chips, "Scanner", stats.scannerRadius);
    addPercentChip(chips, "Damage relief", stats.enemyDamageRelief);
    if (stats.areaControlDamagePerSecond > 0.0) {
        chips.push_back(panelMetric("Field pulse", "+" + display::fixed(stats.areaControlDamagePerSecond, 2) + "/s"));
    }
    addPercentChip(chips, "Enemy slow", stats.enemySlow);
    if (stats.reactiveArmorDamagePerSecond > 0.0) {
        chips.push_back(panelMetric("Counter-hit", "+" + display::fixed(stats.reactiveArmorDamagePerSecond, 2) + "/s"));
    }
    addPercentChip(chips, "Hazard shield", stats.environmentalShieldRelief);
    addPercentChip(chips, "Treatment rate", stats.hazardTreatmentRateBonus);
    addPercentChip(chips, "Crit chance", stats.alliedCritChanceBonus);
    addPercentChip(chips, "Fire rate", stats.alliedFireRateBonus);
    if (stats.sentryVolleyBonus > 0) {
        chips.push_back(panelMetric("Volley", "+" + std::to_string(stats.sentryVolleyBonus) + " target" + (stats.sentryVolleyBonus == 1 ? "" : "s")));
    }
    return chips;
}

inline std::vector<PanelMetricPresentation> droneModuleEffectChips(DroneModuleKind kind, int rank)
{
    const int safeRank = std::clamp(rank, 1, 3);
    const double value = secondaryModuleValue(kind, safeRank);
    switch (kind) {
    case DroneModuleKind::CombatDrill:
        return {panelMetric("Drill contact", display::fixed(value, 1) + " damage"), panelMetric("Per target", "0.8s")};
    case DroneModuleKind::DrillGuard:
        return {panelMetric("While drilling", display::percent(value) + " damage relief")};
    case DroneModuleKind::PulseStrike:
        return {panelMetric("Scan pulse", std::to_string(safeRank) + " damage"), panelMetric("Drone radius", "145%")};
    case DroneModuleKind::SpectrumFilter:
        return {panelMetric("After scan", display::percent(value) + " hazard relief")};
    case DroneModuleKind::OreRelay:
        return {panelMetric("Haul", "+" + std::to_string(safeRank) + " chunks"), panelMetric("Pickup radius", "+" + display::fixed(0.5 * value, 1))};
    case DroneModuleKind::TreasurePing:
        return {panelMetric("Marked ore tiles", std::to_string(safeRank)), panelMetric("Marked deposit", "2x yield"), panelMetric("Scan radius", "+" + display::fixed(1.5 * safeRank, 1))};
    case DroneModuleKind::ContainmentShell:
        return {panelMetric("Within 3.5 cells", display::percent(value) + " hazard relief")};
    case DroneModuleKind::ReclamationLoop:
        return {panelMetric("Per treatment", "+" + display::fixed(value, 1) + "s O2"), panelMetric("Fuel", "+" + display::fixed(0.05 * value / 0.5, 2))};
    case DroneModuleKind::TargetedAssault:
        return {panelMetric("Scanned target", "+" + display::fixed(value, 0) + "% crit")};
    case DroneModuleKind::PenetratingImpact:
        return {panelMetric("Armor bypass", display::percent(value)), panelMetric("Half-damage pierce", std::to_string(secondaryModuleSecondaryHits(kind, safeRank)))};
    case DroneModuleKind::RetributionArc:
        return {panelMetric("Shield counter", display::fixed(value, 1) + " damage"), panelMetric("Cooldown", "1.2s")};
    case DroneModuleKind::HazardScreen:
        return {panelMetric("Charged shield", display::percent(value) + " elemental relief")};
    case DroneModuleKind::None:
        break;
    }
    return {};
}

inline std::string effectChipSummary(const std::vector<PanelMetricPresentation>& chips)
{
    if (chips.empty()) {
        return {};
    }
    std::string summary;
    for (const PanelMetricPresentation& chip : chips) {
        if (!summary.empty()) {
            summary += "; ";
        }
        summary += chip.label + " " + chip.value;
    }
    return summary;
}

inline std::string droneModuleEffectSummary(DroneModuleKind kind, int rank)
{
    const std::string summary = effectChipSummary(droneModuleEffectChips(kind, rank));
    return summary.empty() ? "No field effect." : summary + ".";
}

inline SurfaceUpgradeCardPresentation runUpgradeOfferCardPresentation(
    const GameState& state,
    const ContentCatalog& catalog,
    const RunUpgradeOffer& offer,
    int index)
{
    SurfaceUpgradeCardPresentation card;
    card.index = index;
    card.category = std::string(runUpgradeKindLabel(offer.kind));
    card.rarity = std::string(toString(runUpgradeOfferRarity(state, catalog, offer)));

    switch (offer.kind) {
    case RunUpgradeKind::Rig:
        if (const SurfaceUpgrade* upgrade = catalog.findSurfaceUpgrade(offer.definitionId)) {
            card.title = upgrade->name + " " + runUpgradeRankLabel(offer.targetRank);
            card.detail = "Rank " + runUpgradeRankLabel(offer.targetRank) + " adds another full stack this expedition. " + upgrade->description;
            card.effectChips = surfaceUpgradeChips(upgrade->stats);
            if (upgrade->stats.scannerPulseDamage > 0) {
                for (PanelMetricPresentation& chip : card.effectChips) {
                    if (chip.label == "Scan pulse") {
                        chip.value = "+" + std::to_string(upgrade->stats.scannerPulseDamage * offer.targetRank) + " damage";
                    }
                }
            }
            const std::string currentRank = offer.targetRank <= 1
                ? "None"
                : ("Rank " + runUpgradeRankLabel(offer.targetRank - 1));
            card.effectChips.insert(card.effectChips.begin(), panelMetric({}, currentRank + " -> Rank " + runUpgradeRankLabel(offer.targetRank)));
            card.action = panelActionButton("UPGRADE RIG TO " + runUpgradeRankLabel(offer.targetRank), ui::actions::surfaceUpgrade(index), "ok");
        }
        break;
    case RunUpgradeKind::DroneRank:
        if (const MiniDrone* drone = catalog.findMiniDrone(offer.definitionId)) {
            card.title = drone->name + " MK " + runUpgradeRankLabel(offer.targetRank);
            card.detail = "All current and future equipped copies gain +30% damage and support stats for this expedition.";
            card.effectChips = miniDroneChips(scaledMiniDroneStats(drone->stats, offer.targetRank), offer.targetRank, drone->role);
            card.effectChips.insert(card.effectChips.begin(), panelMetric("Scope", "All copies"));
            card.effectChips.insert(card.effectChips.begin(), panelMetric(
                {},
                "Mk " + runUpgradeRankLabel(offer.targetRank - 1) + " -> Mk " + runUpgradeRankLabel(offer.targetRank)));
            card.action = panelActionButton("UPGRADE TO MK " + runUpgradeRankLabel(offer.targetRank), ui::actions::surfaceUpgrade(index), "ok");
        }
        break;
    case RunUpgradeKind::DroneGraft:
        if (const DroneModuleDefinition* module = catalog.findDroneModule(offer.definitionId)) {
            const bool slotValid = offer.slotIndex >= 0 && offer.slotIndex < static_cast<int>(state.meta.equippedDroneIds.size());
            const MiniDrone* drone = slotValid
                ? catalog.findMiniDrone(state.meta.equippedDroneIds[static_cast<std::size_t>(offer.slotIndex)])
                : nullptr;
            const int rank = drone == nullptr ? 1 : expeditionDroneRank(state, drone->id);
            card.title = "SLOT " + std::to_string(offer.slotIndex + 1) + " / " +
                (drone == nullptr ? std::string("EMPTY") : drone->name) + " / " + module->name;
            card.detail = miniDroneRoleLabel(module->hostRole) + " + " + miniDroneRoleLabel(module->secondaryRole) +
                ". One graft occupies this slot for the expedition.";
            card.effectChips = droneModuleEffectChips(module->kind, rank);
            card.effectChips.insert(card.effectChips.begin(), panelMetric("Frame", miniDroneRoleLabel(module->hostRole) + " -> " + miniDroneRoleLabel(module->secondaryRole)));
            card.action = panelActionButton("INSTALL ON SLOT " + std::to_string(offer.slotIndex + 1), ui::actions::surfaceUpgrade(index), "ok");
            card.droneModule = true;
            card.hostRole = miniDroneRoleLabel(module->hostRole);
            card.secondaryRole = miniDroneRoleLabel(module->secondaryRole);
        }
        break;
    case RunUpgradeKind::Synergy:
        if (const DroneSynergyDefinition* synergy = catalog.findDroneSynergy(offer.definitionId)) {
            card.title = synergy->name;
            card.detail = synergy->description;
            card.effectChips = droneSynergyChips(synergy->stats);
            std::string roles;
            for (MiniDroneRole role : synergy->requiredRoles) {
                if (!roles.empty()) {
                    roles += " + ";
                }
                roles += miniDroneRoleLabel(role);
            }
            card.effectChips.insert(card.effectChips.begin(), panelMetric("Required roles", roles));
            card.action = panelActionButton("ACTIVATE SYNERGY", ui::actions::surfaceUpgrade(index), "ok");
        }
        break;
    }
    const std::string effectSummary = effectChipSummary(card.effectChips);
    if (!effectSummary.empty()) {
        card.detail += " EFFECTS // " + effectSummary + ".";
    }
    return card;
}

inline std::string miniDroneBuildHook(MiniDroneRole role)
{
    switch (role) {
    case MiniDroneRole::Mining:
        return "Can qualify future Level Up offers for Excavation Barrage, Long Haul Rig, and Relic Pathfinder.";
    case MiniDroneRole::Resource:
        return "Can qualify future Level Up offers for Long Haul Rig, Pathfinder Loop, and Containment Rig.";
    case MiniDroneRole::Survey:
        return "Can qualify future Level Up offers for Targeting Grid, Sentry Killbox, or Relic Pathfinder.";
    case MiniDroneRole::Hazard:
        return "Can qualify future Level Up offers for Containment Screen and Containment Rig.";
    case MiniDroneRole::Attack:
        return "Can qualify future Level Up offers for targeting, volley, and excavation synergies.";
    case MiniDroneRole::Defense:
        return "Can qualify future Level Up offers for Killbox Screen and containment synergies.";
    }
    return "Equip complementary roles, then choose a named synergy when it appears at Level Up.";
}

inline MiniDroneCardPresentation miniDroneCardPresentation(const MiniDrone& drone, const GameState& state, int index)
{
    const bool unlocked = isMiniDroneUnlocked(state.meta, drone);
    const int ownedCount = ownedMiniDroneCount(state, drone.id);
    const bool owned = ownedCount > 0;
    const int equippedCount = equippedMiniDroneCount(state, drone.id);
    const bool hasFreeSlot = state.meta.equippedDroneIds.size() < static_cast<std::size_t>(std::max(0, state.meta.droneBaySlots));
    const MaterialInventory additionalUnitCost = miniDroneAdditionalUnitCost(drone);
    const int upgradeLevel = owned ? expeditionDroneRank(state, drone.id) : 1;
    PanelButtonPresentation action = disabledPanelButton(unlocked ? "Slot full" : "Locked");
    std::string status = unlocked
        ? (equippedCount > 0
            ? "Assigned " + std::to_string(equippedCount) + "/" + std::to_string(ownedCount)
            : (owned ? "Ready " + std::to_string(ownedCount) : "Not owned"))
        : "Locked";
    std::string upgradeSummary = !owned
        ? "Fabricate this Support Drone to make its run-rank cards eligible."
        : ("Expedition Mk " + runUpgradeRankLabel(upgradeLevel) +
            (upgradeLevel >= 3 ? " / maximum" : " / higher ranks appear at Level Up"));
    if (unlocked) {
        if (!hasFreeSlot) {
            action = disabledPanelButton("Slot full");
        } else if (equippedCount < ownedCount) {
            action = panelActionButton("Assign", ui::actions::equipDrone(index), "ok");
        } else if (canAffordMaterials(state.meta.materials, additionalUnitCost)) {
            action = panelActionButton("Fabricate", ui::actions::equipDrone(index), "ok");
            status += " / Build " + materialSummary(additionalUnitCost);
        } else {
            action = disabledPanelButton("Need " + materialSummary(additionalUnitCost));
            status += " / Build " + materialSummary(additionalUnitCost);
        }
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
        std::move(action)
    };
}

inline std::string miniDroneNameSummary(const GameState& state, const ContentCatalog& catalog)
{
    if (!droneBayUnlocked(state)) {
        return "Not unlocked";
    }
    if (state.meta.equippedDroneIds.empty()) {
        return "No Support Drones assigned";
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
    return summary.empty() ? "No Support Drones assigned" : summary;
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
    if (effects.oxygenSeconds > 0.0) {
        return "Endurance support build";
    }
    return "Field support build";
}

inline std::string droneBuildDetail(const MiniDroneLoadoutEffects& effects)
{
    if (effects.names.empty()) {
        return "Assign Support Drones to create passive mining, combat, shield, scanner, and endurance synergies before the run starts.";
    }
    if (!effects.signatureName.empty()) {
        return effects.signatureDetail;
    }
    if (!effects.synergyNames.empty()) {
        return "Active synergies change how the Mining Rig survives while you mine: " + miniDroneSynergySummary(effects) + ".";
    }
    return "This loadout has useful solo Support Drone effects. Add complementary roles to make named synergy cards eligible at Level Up.";
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
        chips.push_back(panelMetric("Build state", "No Support Drones"));
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
    return static_cast<int>(std::count_if(state.run.surfaceExpedition.runDroneRanks.begin(), state.run.surfaceExpedition.runDroneRanks.end(), [](const RunDroneRank& record) {
        return record.rank > 1;
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
    auto graftForSlot = [&](int slot) -> const DroneFrameModuleAssignment* {
        const auto found = std::find_if(
            state.run.surfaceExpedition.droneModuleAssignments.begin(),
            state.run.surfaceExpedition.droneModuleAssignments.end(),
            [&](const DroneFrameModuleAssignment& assignment) { return assignment.equippedFrame == slot; });
        return found == state.run.surfaceExpedition.droneModuleAssignments.end() ? nullptr : &*found;
    };
    auto graftDefinition = [&](const DroneFrameModuleAssignment* assignment) -> const DroneModuleDefinition* {
        if (assignment == nullptr) {
            return nullptr;
        }
        const auto found = std::find_if(
            catalog.droneModules.begin(),
            catalog.droneModules.end(),
            [&](const DroneModuleDefinition& module) { return module.kind == assignment->module; });
        return found == catalog.droneModules.end() ? nullptr : &*found;
    };
    for (int index = 0; index < maxSlots; ++index) {
        const int slotNumber = index + 1;
        if (index < unlockedSlots) {
            const bool equipped = index < static_cast<int>(state.meta.equippedDroneIds.size());
            const MiniDrone* drone = equipped ? catalog.findMiniDrone(state.meta.equippedDroneIds[static_cast<std::size_t>(index)]) : nullptr;
            const DroneFrameModuleAssignment* graft = graftForSlot(index);
            const DroneModuleDefinition* module = graftDefinition(graft);
            if (drone != nullptr) {
                const int upgradeLevel = expeditionDroneRank(state, drone->id);
                const bool graftActive = module != nullptr && drone->role == module->hostRole;
                std::string detail = "Expedition Mk " + runUpgradeRankLabel(upgradeLevel) + " " +
                    miniDroneRoleLabel(drone->role) + " support is active in the next mining run.";
                std::vector<PanelMetricPresentation> chips {
                    panelMetric("Slot", std::to_string(slotNumber)),
                    panelMetric("Run Mk", runUpgradeRankLabel(upgradeLevel))
                };
                if (module != nullptr) {
                    detail += " Graft: " + module->name + " — " + (graftActive ? "Active. " : "Dormant. ") +
                        droneModuleEffectSummary(module->kind, upgradeLevel);
                    chips.push_back(panelMetric("Graft", module->name));
                    chips.push_back(panelMetric("Graft state", graftActive ? "Active" : "Dormant"));
                }
                slots.push_back({
                    slotNumber,
                    drone->name,
                    miniDroneRoleLabel(drone->role),
                    module == nullptr ? "Equipped" : (graftActive ? "Equipped / Graft active" : "Equipped / Graft dormant"),
                    std::move(detail),
                    "filled " + miniDroneRoleClass(drone->role) + (module == nullptr ? "" : (graftActive ? " graft-active" : " graft-dormant")),
                    std::move(chips),
                    panelActionButton("Unequip", ui::actions::unequipDroneSlot(index), "warn")
                });
            } else {
                std::string detail = "Equip a Support Drone from the roster to add another passive ability to the build.";
                std::vector<PanelMetricPresentation> chips {
                    panelMetric("Slot", std::to_string(slotNumber)),
                    panelMetric("State", "Open")
                };
                if (module != nullptr) {
                    detail += " Installed graft: " + module->name + " — Dormant until a compatible " +
                        miniDroneRoleLabel(module->hostRole) + " Drone occupies this slot.";
                    chips.push_back(panelMetric("Graft", module->name));
                    chips.push_back(panelMetric("Graft state", "Dormant"));
                }
                slots.push_back({
                    slotNumber,
                    "Open slot",
                    "Empty",
                    module == nullptr ? "Ready" : "Graft dormant",
                    std::move(detail),
                    module == nullptr ? "open" : "open graft-dormant",
                    std::move(chips),
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
    std::vector<DroneBuildRecipePresentation> recipes;
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;

    for (const DroneFrameModuleAssignment& assignment : expedition.droneModuleAssignments) {
        const auto module = std::find_if(
            catalog.droneModules.begin(),
            catalog.droneModules.end(),
            [&](const DroneModuleDefinition& candidate) { return candidate.kind == assignment.module; });
        if (module == catalog.droneModules.end()) {
            continue;
        }
        const bool slotValid = assignment.equippedFrame >= 0 &&
            assignment.equippedFrame < static_cast<int>(state.meta.equippedDroneIds.size());
        const MiniDrone* drone = slotValid
            ? catalog.findMiniDrone(state.meta.equippedDroneIds[static_cast<std::size_t>(assignment.equippedFrame)])
            : nullptr;
        const bool active = drone != nullptr && drone->role == module->hostRole;
        const int rank = drone == nullptr ? 1 : expeditionDroneRank(state, drone->id);
        recipes.push_back({
            "Slot " + std::to_string(assignment.equippedFrame + 1) + " / " + module->name,
            miniDroneRoleLabel(module->hostRole) + " -> " + miniDroneRoleLabel(module->secondaryRole),
            droneModuleEffectSummary(module->kind, rank),
            active
                ? "Active"
                : ("Dormant / needs " + miniDroneRoleLabel(module->hostRole) + " in slot " + std::to_string(assignment.equippedFrame + 1)),
            active,
            false
        });
    }

    for (const std::string& synergyId : expedition.selectedSynergyIds) {
        const DroneSynergyDefinition* synergy = catalog.findDroneSynergy(synergyId);
        if (synergy == nullptr) {
            continue;
        }
        DroneBuildRecipePresentation recipe = droneBuildRecipe(
            state,
            catalog,
            synergy->name,
            synergy->requiredRoles,
            synergy->description,
            synergy->signatureKind != MiniDroneSignatureKind::None);
        if (!hasUnlock(state.meta, synergy->requiredUnlock)) {
            recipe.active = false;
            recipe.status = "Dormant / research locked";
        } else if (!recipe.active) {
            recipe.status = "Dormant / " + recipe.status;
        }
        recipes.push_back(std::move(recipe));
    }
    return recipes;
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
    if (effects.scannerRadius > 0.0) {
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
            if (drone != nullptr && drone->role == role && expeditionDroneRank(state, drone->id) < 3) {
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
    if (effects.oxygenSeconds > 0.0) {
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
            return shortDroneName(*drone) + " Mk " + runUpgradeRankLabel(expeditionDroneRank(state, drone->id) + 1);
        }
    }
    for (const std::string& droneId : state.meta.equippedDroneIds) {
        const MiniDrone* drone = catalog.findMiniDrone(droneId);
        if (drone != nullptr && expeditionDroneRank(state, drone->id) < 3) {
            return shortDroneName(*drone) + " Mk " + runUpgradeRankLabel(expeditionDroneRank(state, drone->id) + 1);
        }
    }
    return state.meta.equippedDroneIds.empty() ? "Equip first" : "All Mk III";
}

inline DroneBuildGuidancePresentation droneBuildGuidance(const GameState& state, const ContentCatalog& catalog, const MiniDroneLoadoutEffects& effects)
{
    for (const std::string& synergyId : state.run.surfaceExpedition.selectedSynergyIds) {
        const DroneSynergyDefinition* synergy = catalog.findDroneSynergy(synergyId);
        if (synergy == nullptr) {
            continue;
        }
        std::vector<MiniDroneRole> missing;
        for (MiniDroneRole role : synergy->requiredRoles) {
            if (equippedMiniDroneRoleCount(state, catalog, role) <= 0) {
                missing.push_back(role);
            }
        }
        if (!missing.empty() || !hasUnlock(state.meta, synergy->requiredUnlock)) {
            return {
                synergy->name,
                missing.empty() ? "Research unlock" : droneRoleListSummary(missing),
                droneTunePriority(state, catalog, effects),
                droneRunPosture(effects),
                "This selected synergy is dormant until its required roles and research are active again."
            };
        }
    }
    if (state.run.surfaceExpedition.selectedSynergyIds.empty()) {
        return {
            "Await Level Up",
            "Chosen synergy",
            droneTunePriority(state, catalog, effects),
            droneRunPosture(effects),
            "Named synergies are expedition upgrades. Matching roles make a card eligible, but no synergy activates until you choose it at Level Up."
        };
    }
    return {
        "Selected upgrades active",
        "None",
        droneTunePriority(state, catalog, effects),
        droneRunPosture(effects),
        "Every listed synergy was selected this expedition and its required roles are currently equipped."
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
    // Paid capacity begins after any explicitly claimed scenario reward has
    // granted a second bay slot. The slot count is the reusable capability;
    // do not consult a named campaign projection here.
    const bool paidCapacityUnlocked = state.meta.droneBaySlots >= 2;
    const ScenarioObjectivePresentation capacityObjective =
        scenarioCapacityRewardObjective(state, catalog, 2);
    const bool canAddSlot = canUpgradeDroneSlot(state);

    DroneOpsPresentation presentation;
    presentation.metrics = {
        panelMetric("Slots", std::to_string(static_cast<int>(state.meta.equippedDroneIds.size())) + "/" + std::to_string(std::max(0, state.meta.droneBaySlots))),
        panelMetric("Owned Support Drones", std::to_string(static_cast<int>(state.meta.ownedDroneIds.size()))),
        panelMetric(text::labels::commonMaterials, std::to_string(state.meta.materials.common)),
        panelMetric(text::labels::rareMaterials, std::to_string(state.meta.materials.rare)),
        panelMetric(text::labels::exoticMaterials, std::to_string(state.meta.materials.exotic))
    };
    presentation.buildTitle = droneBuildTitle(effects);
    presentation.buildDetail = droneBuildDetail(effects);
    presentation.buildChips = {
        panelMetric("Signature", effects.signatureName.empty() ? "None" : effects.signatureName),
        panelMetric("Active synergies", std::to_string(static_cast<int>(effects.synergyNames.size()))),
        panelMetric("Ranked Support Drones", std::to_string(tunedDroneCount(state))),
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
        detailPresentationRow(
            "First frame",
            state.meta.ownedDroneIds.empty()
                ? std::string("Fabricate a Support Drone frame and assign it to an open bay slot. Run ranks, grafts, and synergies are chosen at Level Up.")
                : std::string("Fabricate extra frames for open slots; expedition upgrades apply to every equipped copy of a chosen type.")),
        detailPresentationRow("Drone Bay", std::to_string(std::max(0, state.meta.droneBaySlots)) + " slot capacity"),
        detailPresentationRow("Loadout", miniDroneNameSummary(state, catalog)),
        detailPresentationRow("Build signature", effects.signatureName.empty() ? "None" : effects.signatureName),
        detailPresentationRow("Signature payoff", effects.signatureDetail.empty() ? "Equip three complementary roles to activate a signature build." : effects.signatureDetail),
        detailPresentationRow("Build guidance", guidance.detail),
        detailPresentationRow("Next recipe", guidance.nextRecipe + " / Missing: " + guidance.missingRoles + " / Upgrade: " + guidance.tuneNext),
        detailPresentationRow("Support Drone copies", std::string("Each unlocked type starts with one frame. Build extra copies into open slots; duplicate equipped frames share that type's expedition rank.")),
        detailPresentationRow("Run ranks", std::to_string(tunedDroneCount(state)) + " Support Drone types above Mk I this expedition. Ranks reset with the run."),
        detailPresentationRow(
            "Capacity objective",
            paidCapacityUnlocked
                ? "Second bay slot installed. Material-paid expansion is available."
                : (capacityObjective.available
                    ? capacityObjective.title + " // " + capacityObjective.rewardPreview
                    : "Claim a scenario reward that installs a second bay slot.")),
        detailPresentationRow("Selected synergies", state.run.surfaceExpedition.selectedSynergyIds.empty() ? "None" : miniDroneSynergySummary(effects)),
        detailPresentationRow("Mining support", effects.passiveMiningRate > 0.0 ? ("+" + display::fixed(effects.passiveMiningRate * 60.0, 1) + " common/min") : "None"),
        detailPresentationRow("Oxygen support", effects.oxygenSeconds > 0.0 ? ("+" + std::to_string(static_cast<int>(std::round(effects.oxygenSeconds))) + "s") : "None"),
        detailPresentationRow("Scanner support", effects.scannerRadius > 0.0 ? ("+" + display::fixed(effects.scannerRadius, 1) + " radius") : "None"),
        detailPresentationRow("Stability support", effects.hardRockBounceRelief > 0.0 ? display::percent(effects.hardRockBounceRelief) + " less hard-rock bounce" : "None"),
        detailPresentationRow("Passive combat plan", std::string("During hostile mining, the Mining Rig drills while equipped Support Drones auto-fire, shield, slow, and counter-hit enemies.")),
        detailPresentationRow("Combat forecast", std::string("The forecast row shows the passive combat profile that will carry into the next hostile mining run.")),
        detailPresentationRow("Upgrade path", std::string("Material-paid bay slots are the build lever: more slots mean more passive abilities active while you focus on ore and artifacts.")),
        detailPresentationRow("Combat support", effects.sentryDamagePerSecond > 0.0 || effects.enemyDamageRelief > 0.0
            ? display::fixed(effects.sentryDamagePerSecond + effects.areaControlDamagePerSecond, 1) + "/s sentry output, " + display::percent(effects.enemyDamageRelief + effects.environmentalShieldRelief) + " shield relief"
            : "Attack and Defense Support Drones unlock after hostile surface encounters beyond the solar system.")
    };
    if (state.run.surfaceExpedition.active && !state.run.surfaceExpedition.destinationId.empty()) {
        const MiningArenaRules arenaRules = upcomingMiningArenaRules(state, catalog);
        const MiningGateType gateType = selectMiningGateType(arenaRules);
        const MiningGateDefinition gate = resolveMiningGateDefinition(
            arenaRules,
            gateType,
            false);
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
    presentation.nextSlotCost = maxed
        ? "MAX"
        : (paidCapacityUnlocked ? compactMaterialSummary(nextCost) : "OBJECTIVE");
    const std::string blockedSlotLabel = "Need mats";
    presentation.upgradeSlotAction = maxed
        ? disabledPanelButton("Bay maxed")
        : (!paidCapacityUnlocked
              ? disabledPanelButton("Unlock slot 2")
              : (canAddSlot
                    ? panelActionButton("Add slot", ui::actions::upgradeDroneSlot, "ok")
                    : disabledPanelButton(blockedSlotLabel)));
    presentation.backAction = panelActionButton(
        state.run.surfaceExpedition.active ? "Return to Surface Ops" : "Return to Hangar",
        ui::actions::backToSurfaceOps,
        "drone-done-action");
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
    presentation.details = {
        detailPresentationRow(text::labels::blueprints, std::to_string(state.meta.blueprintProgress)),
        detailPresentationRow(text::labels::artifactInsight, text::panel::blueprintGain(artifactInsightBlueprintBonus(state.meta))),
        detailPresentationRow(text::labels::labBonus, text::panel::blueprintGain(researchFacilityBlueprintBonus(state.meta))),
        detailPresentationRow(text::panel::details::commonMaterials, std::to_string(state.meta.materials.common)),
        detailPresentationRow(text::panel::details::rareMaterials, std::to_string(state.meta.materials.rare)),
        detailPresentationRow(text::panel::details::exoticMaterials, std::to_string(state.meta.materials.exotic)),
        detailPresentationHeader(text::panel::details::researchRules),
        detailPresentationRow(text::panel::details::blueprintUse, std::string("Research adds Research Data and can add new module families, facilities, or field tools to future offers.")),
        detailPresentationRow(text::panel::details::materialsUse, std::string("Recovered samples pay material costs. Credit costs still happen later in refit.")),
        detailPresentationRow(text::panel::details::artifactInsightUse, std::string("Decoded artifacts add a capped Research Data bonus to future debug research projects.")),
        detailPresentationRow(text::panel::details::labBonusUse, std::string("Mission Analysis Lab adds a permanent Research Data bonus to future debug research choices.")),
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
    if (!surfaceOpsTutorialMiningUnlocked(state)) {
        return {"Dig First", {}, "risk", false};
    }
    if (state.run.surfaceExpedition.miningRunUsed) {
        return disabledPanelButton(text::buttons::unavailable);
    }
    if (state.run.surfaceExpedition.rigFuel < 1.0) {
        return disabledPanelButton(text::buttons::unavailable);
    }
    return panelActionButton(text::buttons::mineDeposit, ui::actions::mineSurface, "risk");
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
    if (!surfaceOpsTutorialMiningUnlocked(state)) {
        return "Set a start depth to unlock";
    }
    if (state.run.surfaceExpedition.miningRunUsed) {
        return std::string(text::fuel::offline);
    }
    if (state.run.surfaceExpedition.rigFuel < 1.0) {
        return std::string(text::fuel::offline);
    }
    return text::fuel::availability(arkDiscovered(state));
}

inline std::string surfaceDepthBlockerLabel(const SurfaceDepthCapability& capability)
{
    switch (capability.blocker) {
    case SurfaceDepthBlocker::SurveyRating:
        return "Survey limit +" + std::to_string(capability.surveyRating) + " reached";
    case SurfaceDepthBlocker::Unsurveyed:
        return "Survey +" + std::to_string(capability.targetDepth) + " first";
    case SurfaceDepthBlocker::BoreRating:
        return "Bore limit +" + std::to_string(capability.boreRating) + " reached";
    case SurfaceDepthBlocker::ReturnCritical:
        return "Return range critical";
    case SurfaceDepthBlocker::None:
        break;
    }
    return std::string(text::buttons::unavailable);
}

inline PanelButtonPresentation pushSurfaceActionButton(
    const GameState& state,
    const ContentCatalog& catalog)
{
    if (!surfaceOpsTutorialDigUnlocked(state)) {
        return {"Survey First", {}, "warn", false};
    }
    if (state.run.surfaceExpedition.miningRunUsed) {
        return disabledPanelButton(text::buttons::unavailable);
    }
    const SurfaceDepthCapability capability = surfaceDepthCapability(
        state,
        catalog,
        state.run.surfaceExpedition.depth + 1);
    if (!capability.canDig) {
        return disabledPanelButton(surfaceDepthBlockerLabel(capability));
    }
    return fieldSurfaceActionButton(
        state,
        text::buttons::pushDeeper,
        ui::actions::pushSurface,
        tuning::research::pushSupplyCost,
        "warn");
}

inline std::string pushSurfaceActionAvailability(
    const GameState& state,
    const ContentCatalog& catalog)
{
    if (!surfaceOpsTutorialDigUnlocked(state)) {
        return "Log a Survey to unlock";
    }
    if (state.run.surfaceExpedition.miningRunUsed) {
        return std::string(text::panel::messages::surfaceFieldworkClosed);
    }
    const SurfaceDepthCapability capability = surfaceDepthCapability(
        state,
        catalog,
        state.run.surfaceExpedition.depth + 1);
    if (!capability.canDig) {
        return surfaceDepthBlockerMessage(capability);
    }
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
    PanelButtonPresentation action,
    std::string summary = {})
{
    return {
        std::string(title),
        std::move(detail),
        text::panel::messages::supplyCost(cost),
        std::move(risk),
        std::move(riskLabel),
        std::move(summary),
        surfaceActionAvailability(supply, cost),
        std::move(payoffChips),
        std::move(action)
    };
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
    chips.push_back(panelMetric("Mining layer", "Current"));
    chips.push_back(panelMetric(
        "Survey rating",
        "+" + std::to_string(surfaceDepthRating(
            state,
            SurfaceDepthUpgradeKind::SurveyArray))));
    addPositiveChip(chips, text::labels::commonMaterials, tuning::research::surveyCommonGain + tools.surveyCommonBonus + crew.surveyCommonBonus + site.surveyCommonBonus);
    return chips;
}

inline std::vector<PanelMetricPresentation> pushPayoffChips(const GameState& state, const SurfaceCrewEffects& crew, const SurfaceSiteProfileEffects& site)
{
    std::vector<PanelMetricPresentation> chips;
    chips.push_back(panelMetric("Mining start", "Next layer"));
    const bool nextLayerScanned = std::any_of(
        state.run.surfaceExpedition.depthProspects.begin(),
        state.run.surfaceExpedition.depthProspects.end(),
        [&](const SurfaceDepthProspect& prospect) {
            return prospect.absoluteDepth == state.run.surfaceExpedition.depth + 1;
        });
    chips.push_back(panelMetric("Next layer", nextLayerScanned ? "Surveyed" : "Unsurveyed"));
    addPercentChip(chips, text::labels::artifacts, std::min(1.0, tuning::research::artifactChanceBase + crew.artifactChanceBonus + site.artifactChanceBonus));
    chips.push_back(panelMetric(text::labels::hazard, display::signedPercent(tuning::research::hazardPerDepth)));
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

inline bool surfaceUsesOuterExpeditionRecovery(
    const SurfaceExpeditionState& expedition,
    const ContentCatalog& catalog)
{
    const Destination* destination = catalog.findDestination(expedition.destinationId);
    return destination != nullptr && destination->oneWayExpedition;
}

inline SurfaceExpeditionPresentation surfacePosturePresentation(
    const SurfaceExpeditionState& expedition,
    bool arkKnown,
    bool outerExpedition = false)
{
    SurfaceExpeditionPresentation presentation;
    const bool payloadLoaded = hasSurfacePayload(expedition);
    const bool miningWindowOpen = expedition.rigFuel >= 1.0 && !expedition.miningRunUsed;
    if (!payloadLoaded && miningWindowOpen) {
        presentation.postureTitle = "Recommended: survey, dig, then mine";
        presentation.postureDetail = "Survey reveals each level, Dig chooses the Mining Rig's start depth, and Mine begins the hands-on extraction run.";
        presentation.postureClass = "neutral";
        return presentation;
    }
    if (expedition.supply <= 0 && !miningWindowOpen) {
        presentation.postureTitle = std::string(text::panel::messages::surfacePostureExtract);
        presentation.postureDetail =
            text::panel::messages::surfacePostureExtractDetailForHome(
                arkKnown,
                outerExpedition);
        presentation.postureClass = "danger";
        return presentation;
    }
    if (payloadLoaded) {
        presentation.postureTitle = "Ready: return recovered ore";
        presentation.postureDetail = "Every material and artifact on the Ship returns intact. Continue only for more field finds.";
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
    if (!expedition.pendingMiningSiteDefinitionId.empty()) {
        if (const MiningSiteDefinition* site = catalog.findMiningSite(
                expedition.pendingMiningSiteDefinitionId)) {
            MiningArenaRequest siteRequest = site->arena;
            if (siteRequest.seed == 0) {
                siteRequest.seed = request.seed;
            }
            return resolveMiningSiteArenaRules(siteRequest, *site);
        }
    }
    if (const MiningSiteProgress* site = pendingCompatibilityMiningSite(
            state.meta,
            expedition.destinationId)) {
        request.act = site->act;
        request.difficulty = site->difficulty;
        request.seed = site->seed;
        request.gateOverrideEnabled = true;
        request.gateOverride = site->gateType;
    }
    return resolveMiningArenaRules(request);
}

inline SurfaceReturnSafetyPresentation surfaceReturnSafetyPresentation(
    const GameState& state,
    const ContentCatalog& catalog,
    int absoluteDepth)
{
    SurfaceReturnSafetyPresentation presentation;
    const SurfaceReturnSafetyAssessment assessment =
        surfaceReturnSafetyAssessment(state, catalog, absoluteDepth);
    presentation.severity = assessment.severity;
    presentation.depth = assessment.depth;
    presentation.estimatedReturnSeconds = assessment.estimatedReturnSeconds;
    presentation.oxygenSeconds = assessment.oxygenSeconds;
    presentation.fuelNeededAfterDeployment = assessment.fuelNeededAfterDeployment;
    presentation.fuelAvailableAfterDeployment = assessment.fuelAvailableAfterDeployment;
    presentation.fuelCycleSeconds = assessment.fuelCycleSeconds;
    if (presentation.severity == SurfaceReturnSafetySeverity::Safe) {
        return presentation;
    }
    presentation.title = presentation.severity == SurfaceReturnSafetySeverity::Critical
        ? "RETURN RANGE CRITICAL"
        : "RETURN MARGIN LOW";
    presentation.cssClass = presentation.severity == SurfaceReturnSafetySeverity::Critical
        ? "danger dig-endurance-warning"
        : "caution dig-endurance-warning";

    presentation.detail =
        "DEPTH +" + std::to_string(presentation.depth) +
        " RETURN: ~" + std::to_string(presentation.estimatedReturnSeconds) + "s\n" +
        "OXYGEN: " + std::to_string(presentation.oxygenSeconds) + "s\n" +
        "FUEL: " + std::to_string(presentation.fuelAvailableAfterDeployment) +
        " available / " + std::to_string(presentation.fuelNeededAfterDeployment) + " needed\n" +
        "FUEL LOOP: 1 / " + display::fixed(presentation.fuelCycleSeconds, 0) + "s\n\n";
    presentation.detail += presentation.severity == SurfaceReturnSafetySeverity::Critical
        ? "DO NOT MINE HERE.\nUpgrade endurance first."
        : "MINE BRIEFLY.\nReturn as soon as the first payload is secured.";
    return presentation;
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
    const GameState& state,
    const ContentCatalog& catalog,
    const SurfaceExpeditionState& expedition,
    const SurfaceCrewEffects& crew,
    const SurfaceUpgradeEffects& upgrades,
    bool arkKnown)
{
    const MetaProgress& meta = state.meta;
    const SurfaceToolEffects tools = surfaceToolEffects(meta);
    const ScenarioDeliveryPresentation delivery =
        scenarioSafeDeliveryPresentation(state, catalog, expedition);
    const auto supportDroneLoadoutDetail = [&]() {
        if (droneBayUnlocked(state)) {
            return std::string("Configure persistent Support Drones from Drone Ops before mining.");
        }
        if (!delivery.objective.available) {
            return std::string("Claim a scenario reward that installs the Drone Bay.");
        }
        if (delivery.objective.state == ScenarioStepState::ReadyToClaim) {
            return delivery.objective.title + ": READY TO CLAIM.";
        }
        if (delivery.objective.state == ScenarioStepState::Complete) {
            return delivery.objective.title + ": reward claimed.";
        }
        if (delivery.objective.state == ScenarioStepState::Locked) {
            return delivery.objective.title + ": acknowledge the active directive to begin safe delivery.";
        }
        return delivery.objective.title + ": " +
            std::to_string(std::clamp(delivery.objective.current, 0, delivery.objective.required)) + "/" +
            std::to_string(delivery.objective.required) + " " +
            scenarioTargetMaterialLabel(delivery.objective.eventTargetId) + " delivered from the Mining Rig.";
    };
    std::vector<DetailPresentationRow> rows {
        detailPresentationRow(text::labels::site, std::string(surfaceSiteProfileName(expedition.siteProfile))),
        detailPresentationRow(text::labels::fieldKit, surfaceFieldKitSummary(meta)),
        detailPresentationRow("Support Drone loadout", supportDroneLoadoutDetail()),
        detailPresentationRow(text::panel::details::fieldSpecialist, crew.summary),
        detailPresentationRow("Rig upgrades", surfaceUpgradeNameSummary(upgrades.names)),
        detailPresentationRow(text::fuel::reserveLabel(arkKnown), display::fixed(expedition.rigFuel, 1) + "/" + display::fixed(std::max(0.0, expedition.rigFuelCapacity), 1) + " available for Mining Rig operations"),
        detailPresentationRow(text::labels::transferFuel, display::fixed(expedition.transferFuelRecovered, 1) + " recovered at touchdown"),
        detailPresentationRow("Expedition rig pack", display::fixed(expedition.expeditionPackFuel, 1)),
        detailPresentationRow(text::labels::returnStage, std::string("RESERVED")),
        detailPresentationRow(text::labels::hazard, display::percent(expedition.hazard)),
        detailPresentationHeader(text::panel::details::fieldRules),
        detailPresentationRow(text::panel::details::surveyRisk, std::string("Survey maps the current level first, then deeper levels up to the permanent Survey Array rating. A missed level must be scanned again.")),
        detailPresentationRow(text::panel::details::miningRisk, std::string("Mine puts you in direct control of the Mining Rig drone at the selected start depth to recover ore and artifacts.")),
        detailPresentationRow(text::panel::details::depthRisk, std::string("Dig only enters surveyed levels within the permanent Bore System rating and a non-critical return range. The first step is stable; later steps can collapse.")),
        detailPresentationRow(text::panel::details::extraction, std::string("Normal return recovers every material and artifact loaded onto the Ship.")),
        detailPresentationRow(
            text::panel::details::toolMitigation,
            tools.supplyBonus > 0 || tools.mineCommonBonus > 0 || tools.hazardRelief > 0.0
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
    const bool arkKnown = arkDiscovered(state);
    const bool outerExpedition =
        surfaceUsesOuterExpeditionRecovery(expedition, catalog);
    const MiningArenaRules arenaRules = upcomingMiningArenaRules(state, catalog);
    const bool authoredMiningSite = !expedition.pendingMiningSiteDefinitionId.empty();
    const MiningSiteDefinition* authoredSite = authoredMiningSite
        ? catalog.findMiningSite(expedition.pendingMiningSiteDefinitionId)
        : nullptr;
    const MiningSiteProgress* compatibilitySite = authoredMiningSite
        ? nullptr
        : pendingCompatibilityMiningSite(state.meta, expedition.destinationId);
    const MiningEnemyTheme enemyTheme = resolveMiningEnemyTheme(
        arenaRules,
        authoredSite,
        compatibilitySite);
    const MiningElementalAffinity themeAffinity = miningEnemyThemeAffinity(enemyTheme);
    const std::string ecologyForecast = enemyTheme == MiningEnemyTheme::Neutral
        ? "Neutral ecology // No site affinity"
        : std::string(miningEnemyThemeName(enemyTheme)) + " ecology // " +
            std::string(miningElementalAffinityName(themeAffinity)) +
            " pressure on Elementals and true elites only";
    const MiningSwarmPreview swarmPreview = miningSwarmPreview(
        state,
        catalog,
        arenaRules,
        0,
        authoredMiningSite);
    const MiningGateType gateType = selectMiningGateType(arenaRules);
    const MiningGateDefinition gate = resolveMiningGateDefinition(
        arenaRules,
        gateType,
        false);
    const MiningCapabilityProfile capability = miningCapabilityProfile(state, catalog);
    const SurfaceDepthCapability depthCapability = surfaceDepthCapability(
        state,
        catalog,
        expedition.depth + 1);
    SurfaceExpeditionPresentation presentation = surfacePosturePresentation(
        expedition,
        arkKnown,
        outerExpedition);
    presentation.phaseSteps = postArrivalPhaseSteps(Screen::SurfaceExpedition);
    presentation.siteDetail = std::string(surfaceSiteProfileDetail(expedition.siteProfile));
    presentation.arenaTitle = miningArenaForecastTitle(arenaRules);
    if (gateType != MiningGateType::None) {
        presentation.arenaTitle += " | Site: " + std::string(gate.name);
    }
    presentation.arenaDetail = miningArenaForecastDetail(arenaRules);
    presentation.details = surfaceDetailsPresentation(state, catalog, expedition, crew, upgrades, arkKnown);
    presentation.details.insert(presentation.details.begin(), {
        detailPresentationRow("Upcoming arena", presentation.arenaTitle),
        detailPresentationRow("Artifact site", std::string(gate.name)),
        detailPresentationRow("Required capability", std::string(gate.requiredCapability)),
        detailPresentationRow("Current loadout", miningGateCapabilityStatus(capability, gate)),
        detailPresentationRow("New complication", std::string(arenaRules.complication)),
        detailPresentationRow("Mineral forecast", std::string(arenaRules.mineralAvailability)),
        detailPresentationRow("Enemy ecology", ecologyForecast),
        detailPresentationRow("Known enemy roles", std::string(arenaRules.knownEnemyRoles)),
        detailPresentationRow("Recommended counters", std::string(arenaRules.recommendedCounters))
    });
    presentation.logEntries = expedition.logEntries;
    presentation.selectedUpgradeNames = upgrades.names;
    for (const RunDroneRank& rank : expedition.runDroneRanks) {
        if (const MiniDrone* drone = catalog.findMiniDrone(rank.droneId)) {
            presentation.selectedUpgradeNames.push_back(drone->name + " Mk " + runUpgradeRankLabel(rank.rank));
        }
    }
    for (const DroneFrameModuleAssignment& assignment : expedition.droneModuleAssignments) {
        const auto module = std::find_if(catalog.droneModules.begin(), catalog.droneModules.end(), [&](const DroneModuleDefinition& candidate) {
            return candidate.kind == assignment.module;
        });
        if (module != catalog.droneModules.end()) {
            presentation.selectedUpgradeNames.push_back(module->name + " / Slot " + std::to_string(assignment.equippedFrame + 1));
        }
    }
    for (const std::string& synergyId : expedition.selectedSynergyIds) {
        if (const DroneSynergyDefinition* synergy = catalog.findDroneSynergy(synergyId)) {
            presentation.selectedUpgradeNames.push_back(synergy->name);
        }
    }
    if (expedition.runUpgradeOfferPending) {
        for (int i = 0; i < expedition.runUpgradeOfferCount && i < static_cast<int>(expedition.runUpgradeOffers.size()); ++i) {
            presentation.upgradeOffers.push_back(runUpgradeOfferCardPresentation(
                state,
                catalog,
                expedition.runUpgradeOffers[static_cast<std::size_t>(i)],
                i));
        }
    }
    presentation.metrics = {
        panelMetric("Expedition level", std::to_string(std::max(1, expedition.expeditionLevel))),
        panelMetric("Experience", std::to_string(static_cast<int>(std::floor(std::max(0.0, expedition.expeditionExperience)))) + "/" + std::to_string(static_cast<int>(expeditionExperienceThreshold(expedition.expeditionLevel)))),
        panelMetric("Level-up choices", std::to_string(std::max(0, expedition.pendingRunUpgradeChoices))),
        panelMetric(text::labels::site, std::string(surfaceSiteProfileName(expedition.siteProfile))),
        panelMetric(text::labels::fieldKit, surfaceFieldKitSummary(state.meta)),
        panelMetric(text::labels::hazard, display::percent(expedition.hazard)),
        panelMetric(text::labels::supply, std::to_string(expedition.supply)),
        panelMetric(text::fuel::reserveLabel(arkKnown), display::fixed(expedition.rigFuel, 1) + "/" + display::fixed(std::max(0.0, expedition.rigFuelCapacity), 1)),
        panelMetric(text::labels::returnStage, "RESERVED"),
        panelMetric(text::labels::cargo, std::to_string(expedition.cargo)),
        panelMetric("Start depth", "+" + std::to_string(expedition.depth)),
        panelMetric("Survey rating", "+" + std::to_string(depthCapability.surveyRating)),
        panelMetric("Bore rating", "+" + std::to_string(depthCapability.boreRating)),
        panelMetric("Surveyed through", "+" + std::to_string(depthCapability.surveyedThroughDepth)),
        panelMetric("Usable depth", "+" + std::to_string(depthCapability.usableDepth)),
        panelMetric(
            "Fuel loop",
            "1 / " + display::fixed(miningRigFuelCycleSeconds(state), 0) + "s"),
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
        : disabledPanelButton("Build Prospector");
    const std::string surveyHazardRisk = surfaceHazardRisk(
        expedition.hazard,
        tuning::research::surveyHazardChanceScale,
        (tools.surveyCommonBonus > 0 ? tuning::research::probeHazardRelief : 0.0) +
            crew.hazardRelief + upgrades.hazardRelief);
    const int surveyRating = surfaceDepthRating(
        state,
        SurfaceDepthUpgradeKind::SurveyArray);
    const int boreRating = surfaceDepthRating(
        state,
        SurfaceDepthUpgradeKind::BoreSystem);
    const bool surveyLimitReached = surfaceSurveyLimitReached(state);
    const bool boreLimitsSurvey = boreRating < surveyRating;
    const std::string surveyLimitLabel = boreLimitsSurvey
        ? "Bore limit +" + std::to_string(boreRating) + " reached"
        : "Survey limit +" + std::to_string(surveyRating) + " reached";
    SurfaceActionPreviewPresentation surveyPreview = surfaceActionPreview(
        text::buttons::surveySite,
        std::string(text::panel::messages::surfaceSurveyDetail),
        expedition.supply,
        tuning::research::surveySupplyCost,
        surveyHazardRisk,
        std::string(text::labels::hazard),
        surveyPayoffChips(state, tools, crew, site),
        surveyLimitReached
            ? disabledPanelButton(surveyLimitLabel)
            : fieldSurfaceActionButton(
                  state,
                  text::buttons::surveySite,
                  ui::actions::surveySurface,
                  tuning::research::surveySupplyCost,
                  "ok"),
        surveyLimitReached
            ? "All diggable layers mapped"
            : "Maps current mining layer");
    surveyPreview.payoffChips.push_back(panelMetric("Arena", std::string(miningActName(arenaRules.request.act)) + " L" + std::to_string(arenaRules.request.difficulty)));
    if (swarmPreview.available) {
        surveyPreview.risk = "SWARM NEST +" + std::to_string(swarmPreview.depthZone) + " • Artifact " + display::percent(swarmPreview.artifactChance);
        surveyPreview.riskLabel = "DANGER";
        surveyPreview.summary = surveyPreview.risk + " " + surveyPreview.riskLabel;
        surveyPreview.payoffChips.push_back(panelMetric("Danger", "SWARM NEST +" + std::to_string(swarmPreview.depthZone)));
        surveyPreview.payoffChips.push_back(panelMetric("Artifact", display::percent(swarmPreview.artifactChance)));
    }
    presentation.actions.push_back(std::move(surveyPreview));
    presentation.actions.back().availability = surveyLimitReached
        ? (boreLimitsSurvey
              ? "Upgrade Bore System to survey and dig deeper"
              : "Upgrade Survey Array to survey and dig deeper")
        : fieldSurfaceActionAvailability(state, tuning::research::surveySupplyCost);

    const std::string pushCollapseRisk = surfaceHazardRisk(
        expedition.hazard,
        tuning::research::pushHazardChanceScale,
        tools.hazardRelief + crew.hazardRelief + upgrades.hazardRelief);
    SurfaceActionPreviewPresentation pushPreview = surfaceActionPreview(
        text::buttons::pushDeeper,
        std::string(text::panel::messages::surfacePushDetail),
        expedition.supply,
        tuning::research::pushSupplyCost,
        pushCollapseRisk,
        std::string(text::labels::hazard),
        pushPayoffChips(state, crew, site),
        pushSurfaceActionButton(state, catalog),
        "Surveyed layers only • " + pushCollapseRisk + " collapse chance");
    pushPreview.availability = pushSurfaceActionAvailability(state, catalog);
    pushPreview.payoffChips.push_back(panelMetric("Surveyed through", "+" + std::to_string(depthCapability.surveyedThroughDepth)));
    pushPreview.payoffChips.push_back(panelMetric("Bore rating", "+" + std::to_string(depthCapability.boreRating)));
    const MiningArenaRules deeperArenaRules = upcomingMiningArenaRules(state, catalog, 1);
    pushPreview.payoffChips.push_back(panelMetric("Next arena", std::string(miningActName(deeperArenaRules.request.act)) + " L" + std::to_string(deeperArenaRules.request.difficulty)));
    if (swarmPreview.available) {
        pushPreview.risk = "SWARM NEST BELOW";
        pushPreview.riskLabel = "DANGER";
        pushPreview.summary = pushPreview.risk + " " + pushPreview.riskLabel;
        pushPreview.payoffChips.push_back(panelMetric("Nest", "Depth +" + std::to_string(swarmPreview.depthZone)));
        pushPreview.payoffChips.push_back(panelMetric("Artifact", display::percent(swarmPreview.artifactChance)));
    }
    presentation.actions.push_back(std::move(pushPreview));

    const std::string miningOxygen = std::to_string(
        static_cast<int>(std::round(miningDrillStats(state, catalog).oxygenSeconds))) + "s";
    SurfaceActionPreviewPresentation miningPreview = surfaceActionPreview(
        text::buttons::mineDeposit,
        std::string(text::panel::messages::surfaceMineDetail) + text::fuel::deployDetail(arkKnown),
        expedition.supply,
        0,
        miningOxygen,
        std::string(text::labels::oxygen),
        {},
        miningSurfaceActionButton(state),
        miningOxygen + " oxygen • One run; return ore to ship");
    miningPreview.cost = "1 " + std::string(text::fuel::reserveLabel(arkKnown));
    miningPreview.availability = miningSurfaceActionAvailability(state);
    miningPreview.payoffChips.push_back(panelMetric("Run", "One deployment"));
    miningPreview.payoffChips.push_back(panelMetric("Mining start", "Layer +" + std::to_string(expedition.depth)));
    miningPreview.payoffChips.push_back(panelMetric("Ecology", std::string(miningEnemyThemeName(enemyTheme))));
    miningPreview.payoffChips.push_back(panelMetric("Ship", "SURFACE"));
    addPositiveChip(miningPreview.payoffChips, "Tagged CM", expedition.prospectMaterials.common);
    addPositiveChip(miningPreview.payoffChips, "Tagged RM", expedition.prospectMaterials.rare);
    addPositiveChip(miningPreview.payoffChips, "Tagged EX", expedition.prospectMaterials.exotic);
    addPositiveChip(miningPreview.payoffChips, "Tagged AR", expedition.prospectArtifacts);
    if (swarmPreview.available) {
        miningPreview.risk = "SWARM DETECTED AT START DEPTH";
        miningPreview.riskLabel = "DANGER";
        miningPreview.summary = miningPreview.risk + " " + miningPreview.riskLabel;
        miningPreview.payoffChips.push_back(panelMetric("Swarm nest", "Depth +" + std::to_string(swarmPreview.depthZone)));
        miningPreview.payoffChips.push_back(panelMetric("Artifact chance", display::percent(swarmPreview.artifactChance)));
    }
    presentation.actions.push_back(std::move(miningPreview));

    const SurfaceReturnLedger returnLedger = surfaceReturnLedger(state, catalog);
    const std::string extractionTitle = text::buttons::returnHomeLabel(arkKnown, outerExpedition);
    std::string extractionDetail = "On Ship: " + std::to_string(returnLedger.onShip.common) +
        " Common. Normal return is guaranteed.";
    if (!returnLedger.allocations.empty()) {
        const SurfaceReturnAllocation& allocation = returnLedger.allocations.front();
        extractionDetail += " Return allocation: " + std::to_string(allocation.amount) +
            " -> " + allocation.label + "; " + std::to_string(returnLedger.toMaterials.common) + " -> Materials.";
    }
    SurfaceActionPreviewPresentation extractionPreview = surfaceActionPreview(
        extractionTitle,
        extractionDetail,
        expedition.supply,
        0,
        "",
        "",
        extractPayoffChips(expedition),
        panelActionButton(
            extractionTitle,
            ui::actions::extractSurface,
            "ghost"));
    extractionPreview.payoffChips.insert(
        extractionPreview.payoffChips.begin(),
        panelMetric("On Ship", std::to_string(returnLedger.onShip.common) + " Common"));
    for (const SurfaceReturnAllocation& allocation : returnLedger.allocations) {
        extractionPreview.payoffChips.push_back(panelMetric(
            "Return allocation",
            std::to_string(allocation.amount) + " " + allocation.materialId + " -> " + allocation.label));
    }
    presentation.actions.push_back(std::move(extractionPreview));
    return presentation;
}

inline SurfaceExpeditionPresentation surfaceExpeditionPresentation(const GameState& state)
{
    return surfaceExpeditionPresentation(state, createDefaultContent());
}

} // namespace rocket
