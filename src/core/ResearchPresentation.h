#pragma once

#include "core/ContentIds.h"
#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/GameUi.h"
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
    std::vector<SurfaceActionPreviewPresentation> actions;
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
            detailPresentationRow(text::panel::details::phaseInputs, std::string("Supply, site profile, field-kit unlocks, and the payload already in the canisters.")),
            detailPresentationRow(text::panel::details::phaseOutputs, std::string("Common, rare, and exotic materials plus occasional artifacts or blueprint leads.")),
            detailPresentationRow(text::panel::details::phaseRisk, std::string("Cargo, hazard, low supply, and depth raise extraction risk. Failed extraction can lose payload.")),
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

inline void addSignedPercentChip(std::vector<PanelMetricPresentation>& chips, std::string_view label, double value)
{
    if (std::abs(value) >= 0.005) {
        chips.push_back(panelMetric(label, display::signedPercent(value)));
    }
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
        : disabledPanelButton(text::panel::messages::needSupply(cost));
}

inline std::string surfaceActionAvailability(int supply, int cost)
{
    return supply >= cost ? std::string(text::panel::ready) : text::panel::messages::needSupply(cost);
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
    addPositiveChip(chips, text::labels::commonMaterials, tuning::research::surveyCommonGain + tools.surveyCommonBonus + crew.surveyCommonBonus + site.surveyCommonBonus);
    addSignedPercentChip(chips, text::labels::extractionRisk, projectedSurfaceExtractionRiskDelta(state, projectedSurveyExpedition(state.run.surfaceExpedition, tools, crew, site)));
    return chips;
}

inline std::vector<PanelMetricPresentation> minePayoffChips(const GameState& state, const SurfaceToolEffects& tools, const SurfaceCrewEffects& crew, const SurfaceSiteProfileEffects& site)
{
    std::vector<PanelMetricPresentation> chips;
    addPositiveChip(chips, text::labels::commonMaterials, tuning::research::mineCommonGain + tools.mineCommonBonus + crew.mineCommonBonus + site.mineCommonBonus);
    addPercentChip(chips, text::labels::rareMaterials, std::min(1.0, tools.mineRareChanceBonus + crew.mineRareChanceBonus + site.mineRareChanceBonus));
    addSignedPercentChip(chips, text::labels::extractionRisk, projectedSurfaceExtractionRiskDelta(state, projectedMineExpedition(state.run.surfaceExpedition, tools, crew, site)));
    return chips;
}

inline std::vector<PanelMetricPresentation> pushPayoffChips(const GameState& state, const SurfaceCrewEffects& crew, const SurfaceSiteProfileEffects& site)
{
    std::vector<PanelMetricPresentation> chips;
    chips.push_back(panelMetric(text::labels::depth, "+1"));
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
    if (expedition.supply <= 0) {
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
    if (tools.empty()) {
        return "Baseline kit";
    }

    std::string summary = tools.front();
    for (std::size_t i = 1; i < tools.size(); ++i) {
        summary += ", " + tools[i];
    }
    return summary;
}

inline std::vector<DetailPresentationRow> surfaceDetailsPresentation(
    const SurfaceExpeditionState& expedition,
    const MetaProgress& meta,
    const SurfaceCrewEffects& crew,
    double extractionRisk)
{
    const SurfaceToolEffects tools = surfaceToolEffects(meta);
    std::vector<DetailPresentationRow> rows {
        detailPresentationRow(text::labels::site, std::string(surfaceSiteProfileName(expedition.siteProfile))),
        detailPresentationRow(text::labels::fieldKit, surfaceFieldKitSummary(meta)),
        detailPresentationRow(text::panel::details::fieldSpecialist, crew.summary),
        detailPresentationRow(text::labels::hazard, display::percent(expedition.hazard)),
        detailPresentationRow(text::labels::extractionRisk, display::percent(extractionRisk)),
        detailPresentationHeader(text::panel::details::fieldRules),
        detailPresentationRow(text::panel::details::surveyRisk, std::string("Dust can burn extra supply; field probes improve yield and reduce survey trouble.")),
        detailPresentationRow(text::panel::details::miningRisk, std::string("Drill chatter can damage cargo; drill rigs improve yield and reduce mining trouble.")),
        detailPresentationRow(text::panel::details::depthRisk, std::string("Pushing deeper raises hazard, artifact odds, and extraction pressure.")),
        detailPresentationRow(text::panel::details::extraction, std::string("Cargo and hazard raise recovery risk; cargo rigs reduce the penalty from heavier payloads.")),
        detailPresentationRow(
            text::panel::details::toolMitigation,
            tools.supplyBonus > 0 || tools.mineCommonBonus > 0 || tools.extractionRiskRelief > 0.0
                ? std::string("Installed surface tools are already changing these odds.")
                : std::string("Research field probes, drill rigs, and cargo rigs to improve future expeditions."))
    };
    if (expedition.enemyEncountersEnabled) {
        rows.push_back(detailPresentationRow(text::panel::details::hostileContact, std::string("Enemy contact can consume supply and cargo beyond the solar system; perimeter drones reduce contact risk.")));
    }
    return rows;
}

inline SurfaceExpeditionPresentation surfaceExpeditionPresentation(const GameState& state)
{
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    const SurfaceToolEffects tools = surfaceToolEffects(state.meta);
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    const double extractionRisk = surfaceExtractionRisk(state);
    SurfaceExpeditionPresentation presentation = surfacePosturePresentation(expedition, extractionRisk);
    presentation.phaseSteps = postArrivalPhaseSteps(Screen::SurfaceExpedition);
    presentation.briefing = postArrivalPhaseBriefing(Screen::SurfaceExpedition);
    presentation.siteDetail = std::string(surfaceSiteProfileDetail(expedition.siteProfile));
    presentation.details = surfaceDetailsPresentation(expedition, state.meta, crew, extractionRisk);
    presentation.logEntries = expedition.logEntries;
    presentation.metrics = {
        panelMetric(text::labels::site, std::string(surfaceSiteProfileName(expedition.siteProfile))),
        panelMetric(text::labels::fieldKit, surfaceFieldKitSummary(state.meta)),
        panelMetric(text::labels::hazard, display::percent(expedition.hazard)),
        panelMetric(text::labels::supply, std::to_string(expedition.supply)),
        panelMetric(text::labels::cargo, std::to_string(expedition.cargo)),
        panelMetric(text::labels::depth, std::to_string(expedition.depth)),
        panelMetric(text::labels::extractionRisk, display::percent(extractionRisk)),
        panelMetric(text::labels::commonMaterials, std::to_string(expedition.temporaryMaterials.common)),
        panelMetric(text::labels::rareMaterials, std::to_string(expedition.temporaryMaterials.rare)),
        panelMetric(text::labels::exoticMaterials, std::to_string(expedition.temporaryMaterials.exotic)),
        panelMetric(text::labels::artifacts, std::to_string(expedition.temporaryArtifacts.size()))
    };
    if (expedition.enemyEncountersEnabled) {
        presentation.metrics.push_back(panelMetric(text::labels::contactRisk, display::percent(surfaceEnemyEncounterChance(state))));
    }
    presentation.actions = {
        surfaceActionPreview(
            text::buttons::surveySite,
            std::string(text::panel::messages::surfaceSurveyDetail),
            expedition.supply,
            tuning::research::surveySupplyCost,
            surfaceHazardRisk(expedition.hazard, tuning::research::surveyHazardChanceScale, (tools.surveyCommonBonus > 0 ? tuning::research::probeHazardRelief : 0.0) + crew.hazardRelief),
            std::string(text::labels::hazard),
            surveyPayoffChips(state, tools, crew, site),
            surfaceActionButton(text::buttons::surveySite, ui::actions::surveySurface, expedition.supply, tuning::research::surveySupplyCost)),
        surfaceActionPreview(
            text::buttons::mineDeposit,
            std::string(text::panel::messages::surfaceMineDetail),
            expedition.supply,
            tuning::research::mineSupplyCost,
            surfaceHazardRisk(expedition.hazard, tuning::research::mineHazardChanceScale, (tools.mineCommonBonus > 0 ? tuning::research::drillHazardRelief : 0.0) + crew.hazardRelief),
            std::string(text::labels::hazard),
            minePayoffChips(state, tools, crew, site),
            surfaceActionButton(text::buttons::mineDeposit, ui::actions::mineSurface, expedition.supply, tuning::research::mineSupplyCost)),
        surfaceActionPreview(
            text::buttons::pushDeeper,
            std::string(text::panel::messages::surfacePushDetail),
            expedition.supply,
            tuning::research::pushSupplyCost,
            surfaceHazardRisk(expedition.hazard, tuning::research::pushHazardChanceScale, (tools.extractionRiskRelief > 0.0 ? tuning::research::cargoRigHazardRelief : 0.0) + crew.hazardRelief),
            std::string(text::labels::hazard),
            pushPayoffChips(state, crew, site),
            surfaceActionButton(text::buttons::pushDeeper, ui::actions::pushSurface, expedition.supply, tuning::research::pushSupplyCost, "danger")),
        surfaceActionPreview(
            text::buttons::extractPayload,
            std::string(text::panel::messages::surfaceExtractDetail),
            expedition.supply,
            0,
            display::percent(extractionRisk),
            std::string(text::labels::extractionRisk),
            extractPayoffChips(expedition),
            panelActionButton(text::buttons::extractPayload, ui::actions::extractSurface, "ok"))
    };
    return presentation;
}

} // namespace rocket
