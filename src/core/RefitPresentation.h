#pragma once

#include "core/Content.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"
#include "core/GameUi.h"
#include "core/LaunchSimulation.h"
#include "core/PanelPresentation.h"
#include "core/Tuning.h"

#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace rocket {

struct ModuleStatDisplay {
    double value = 0.0;
    std::string_view primaryLabel;
    std::string_view chipLabel;
    std::string_view detailLabel;
    bool showInShipDetails = true;
};

struct RefitStatChip {
    std::string label;
    std::string value;
    bool positive = true;
};

struct RefitPresentation {
    std::string slotClass;
    std::string category;
    std::string rarity;
    std::string glyph;
    std::string title;
    std::string detail;
    std::string primaryImpact;
    std::vector<RefitStatChip> statChips;
};

enum class RefitOfferPresentationKind {
    ShipModule,
    CrewUpgrade,
    LaunchUpgrade
};

struct RefitOfferPresentation {
    RefitOfferPresentationKind kind = RefitOfferPresentationKind::ShipModule;
    int index = 0;
    int cost = 0;
    std::string costSummary;
    std::string footerCostSummary;
    bool affordable = false;
    RefitPresentation card;
    PanelButtonPresentation action;
};

struct RefitWindowPresentation {
    std::vector<PanelMetricPresentation> resourceChips;
    std::string recoveryDetail;
    std::vector<RefitOfferPresentation> offers;
    double rerollCost = 0.0;
    bool showReroll = true;
    PanelButtonPresentation rerollAction;
    bool showSkip = true;
    PanelButtonPresentation skipAction;
};

inline std::string slotClass(SlotType slot)
{
    switch (slot) {
    case SlotType::Engine:
        return "engine";
    case SlotType::Fuel:
        return "fuel";
    case SlotType::Hull:
        return "hull";
    case SlotType::Cooling:
        return "cooling";
    case SlotType::Sensors:
        return "sensors";
    case SlotType::Escape:
        return "escape";
    }
    return "module";
}

inline std::string refitTrackClass(RefitTrack track, SlotType fallback)
{
    switch (track) {
    case RefitTrack::Reach:
        return "reach";
    case RefitTrack::Control:
        return "control";
    case RefitTrack::Recovery:
        return "recovery";
    case RefitTrack::None:
        return slotClass(fallback);
    }
    return "module";
}

inline std::array<ModuleStatDisplay, 17> moduleStatDisplays(const ModuleStats& stats)
{
    return {{
        {stats.thrust, text::moduleStats::speed, text::moduleStats::speedChip, text::moduleStats::thrustDetail, false},
        {stats.fuel, text::moduleStats::fuel, text::moduleStats::fuelChip, text::moduleStats::fuel, false},
        {stats.hull, text::moduleStats::hull, text::moduleStats::hullChip, text::moduleStats::hull, false},
        {stats.cooling, text::moduleStats::tempControl, text::moduleStats::tempChip, text::moduleStats::tempControl, false},
        {stats.sensors, text::moduleStats::sensors, text::moduleStats::sensorsChip, text::moduleStats::sensors, false},
        {stats.escape, text::moduleStats::escape, text::moduleStats::escapeChip, text::moduleStats::escape, false},
        {stats.pressure, text::moduleStats::pressureControl, text::moduleStats::pressureChip, text::moduleStats::pressureControl, false},
        {stats.volatility, text::moduleStats::volatility, text::moduleStats::volatilityChip, text::moduleStats::volatility, false},
        {stats.payout, text::moduleStats::dataPayout, text::moduleStats::payoutChip, text::moduleStats::dataPayout, false},
        {stats.miningPower, text::moduleStats::miningPower, text::moduleStats::miningPowerChip, text::moduleStats::miningPower},
        {stats.miningYield, text::moduleStats::miningYield, text::moduleStats::miningYieldChip, text::moduleStats::miningYield},
        {stats.miningCooling, text::moduleStats::miningCooling, text::moduleStats::miningCoolingChip, text::moduleStats::miningCooling},
        {stats.miningDurability, text::moduleStats::miningDurability, text::moduleStats::miningDurabilityChip, text::moduleStats::miningDurability},
        {stats.miningWidth, text::moduleStats::miningWidth, text::moduleStats::miningWidthChip, text::moduleStats::miningWidth},
        {stats.miningDepth, text::moduleStats::miningDepth, text::moduleStats::miningDepthChip, text::moduleStats::miningDepth},
        {stats.miningStorage, text::moduleStats::miningStorage, text::moduleStats::miningStorageChip, text::moduleStats::miningStorage},
        {stats.miningEngineEfficiency, text::moduleStats::miningEngineEfficiency, text::moduleStats::miningEngineEfficiencyChip, text::moduleStats::miningEngineEfficiency}
    }};
}

inline std::string launchUpgradeDetail(const ShipModule& module)
{
    switch (module.launchUpgradeKind) {
    case LaunchUpgradeKind::FuelTanks:
        return "Adds 5 fuel. More fuel extends range and protects the return margin.";
    case LaunchUpgradeKind::FlightControls:
        return "Flight instability falls to " +
            display::percent(launchControlChaosForRank(module.launchUpgradeRank)) +
            ". Steering drift, oversteer, and throttle kicks all settle down.";
    case LaunchUpgradeKind::Cooling:
        return "Engines-off cooling reaches " +
            display::fixed(launchEngineOffCoolingForRank(module.launchUpgradeRank) * 100.0, 0) +
            "%/s. Powered heat falls to " +
            display::percent(launchPoweredHeatMultiplierForRank(module.launchUpgradeRank)) + ".";
    case LaunchUpgradeKind::Hull:
        return "Hull reaches " + display::fixed(
            tuning::launch::hullBaseIntegrity +
                static_cast<double>(module.launchUpgradeRank) * tuning::launch::hullIntegrityPerRank,
            0) + " HP. A standard asteroid hit falls to " +
            display::fixed(launchAsteroidImpactDamage(module.launchUpgradeRank, 1.0), 0) + " damage.";
    case LaunchUpgradeKind::None:
        break;
    }
    return {};
}

inline std::string moduleThreat(const ShipModule& module)
{
    if (module.launchUpgradeKind != LaunchUpgradeKind::None) {
        return launchUpgradeDetail(module);
    }
    if (module.stats.miningPower > 0.0) {
        return std::string(text::moduleThreats::cutsTougherRock);
    }
    if (module.stats.miningYield > 0.0) {
        return std::string(text::moduleThreats::recoversMoreOre);
    }
    if (module.stats.miningCooling > 0.0) {
        return std::string(text::moduleThreats::keepsDrillCool);
    }
    if (module.stats.miningDurability > 0.0) {
        return std::string(text::moduleThreats::protectsDrillHead);
    }
    if (module.stats.miningWidth > 0.0) {
        return std::string(text::moduleThreats::expandsSurveyGrid);
    }
    if (module.stats.miningDepth > 0.0) {
        return std::string(text::moduleThreats::opensDeeperShaft);
    }
    if (module.stats.miningStorage > 0.0) {
        return "Gives the Mining Rig more free carry before load drag";
    }
    if (module.stats.miningEngineEfficiency > 0.0) {
        return "Keeps the Mining Rig faster and cheaper under load";
    }

    switch (module.slot) {
    case SlotType::Engine:
        return "Legacy engine package retained for existing saves.";
    case SlotType::Fuel:
        return "Legacy fuel package retained for existing saves.";
    case SlotType::Hull:
        return "Legacy hull package retained for existing saves.";
    case SlotType::Cooling:
        return "Legacy cooling package retained for existing saves.";
    case SlotType::Sensors:
        return "Legacy sensor package retained for existing saves.";
    case SlotType::Escape:
        return "Legacy recovery package retained for existing saves.";
    }
    return std::string(text::moduleThreats::improvesMissionOdds);
}

inline std::string modulePrimaryImpact(const ShipModule& module)
{
    switch (module.launchUpgradeKind) {
    case LaunchUpgradeKind::FuelTanks:
        return "+5 fuel";
    case LaunchUpgradeKind::FlightControls:
        return "Instability \xE2\x86\x92 " + display::percent(launchControlChaosForRank(module.launchUpgradeRank));
    case LaunchUpgradeKind::Cooling:
        return "Cooling \xE2\x86\x92 " +
            display::fixed(launchEngineOffCoolingForRank(module.launchUpgradeRank) * 100.0, 0) + "%/s";
    case LaunchUpgradeKind::Hull:
        return "+" + display::fixed(tuning::launch::hullIntegrityPerRank, 0) + " HP";
    case LaunchUpgradeKind::None:
        break;
    }
    const auto displays = moduleStatDisplays(module.stats);
    const ModuleStatDisplay* best = &displays.front();
    for (const ModuleStatDisplay& display : displays) {
        if (std::abs(display.value) > std::abs(best->value)) {
            best = &display;
        }
    }

    return display::signedFixed(best->value, 1) + " " + std::string(best->primaryLabel);
}

inline void addStatChip(std::vector<RefitStatChip>& chips, std::string_view label, double value)
{
    if (std::abs(value) < tuning::presentation::statChipMinimumMagnitude) {
        return;
    }

    chips.push_back({
        std::string(label),
        display::signedFixed(value, 1),
        value >= 0.0
    });
}

inline void addBeneficialReductionChip(std::vector<RefitStatChip>& chips, std::string_view label, double reduction)
{
    if (reduction < tuning::presentation::statChipMinimumMagnitude) {
        return;
    }
    chips.push_back({std::string(label), display::signedFixed(-reduction, 1), true});
}

inline std::vector<RefitStatChip> moduleStatChips(const ShipModule& module)
{
    switch (module.launchUpgradeKind) {
    case LaunchUpgradeKind::FuelTanks:
        return {{"FUEL", "+5", true}};
    case LaunchUpgradeKind::FlightControls:
        return {{"INSTABILITY", display::percent(launchControlChaosForRank(module.launchUpgradeRank)), true}};
    case LaunchUpgradeKind::Cooling:
        return {{"COOL", display::fixed(launchEngineOffCoolingForRank(module.launchUpgradeRank) * 100.0, 0) + "%/s", true}};
    case LaunchUpgradeKind::Hull:
        return {
            {"HULL", "+" + display::fixed(tuning::launch::hullIntegrityPerRank, 0) + " HP", true},
            {"IMPACT", display::percent(launchHullImpactMultiplierForRank(module.launchUpgradeRank)), true}
        };
    case LaunchUpgradeKind::None:
        break;
    }
    std::vector<RefitStatChip> chips;
    for (const ModuleStatDisplay& display : moduleStatDisplays(module.stats)) {
        addStatChip(chips, display.chipLabel, display.value);
    }
    return chips;
}

inline std::vector<RefitStatChip> crewUpgradeStatChips(const CrewUpgrade& upgrade)
{
    const CrewUpgradeStats& stats = upgrade.stats;
    std::vector<RefitStatChip> chips;
    addStatChip(chips, text::moduleStats::trainingChip, static_cast<double>(stats.trainingGain));
    addBeneficialReductionChip(chips, text::moduleStats::simStressChip, static_cast<double>(stats.trainingStressRelief));
    addStatChip(chips, text::moduleStats::restChip, static_cast<double>(stats.restStressBonus));
    addStatChip(chips, text::moduleStats::launchStressChip, static_cast<double>(stats.launchStressRelief));
    addStatChip(chips, text::moduleStats::traitChip, stats.traitModifier * 100.0);
    return chips;
}

inline std::string crewUpgradePrimaryImpact(const CrewUpgrade& upgrade)
{
    const CrewUpgradeStats& stats = upgrade.stats;
    if (stats.trainingGain > 0) {
        return text::panel::trainingImpact(stats.trainingGain);
    }
    if (stats.restStressBonus > 0) {
        return text::panel::restImpact(stats.restStressBonus);
    }
    if (stats.launchStressRelief > 0) {
        return text::panel::launchStressImpact(stats.launchStressRelief);
    }
    if (stats.trainingStressRelief > 0) {
        return text::panel::simulatorStressImpact(stats.trainingStressRelief);
    }
    if (stats.traitModifier > 0.0) {
        return text::panel::traitModifierImpact(display::signedPercent(stats.traitModifier));
    }
    return std::string(text::panel::messages::crewOpsFallback);
}

inline RefitPresentation moduleRefitPresentation(const ShipModule& module)
{
    std::string category = module.refitTrack == RefitTrack::None
        ? std::string(toString(module.slot))
        : std::string(toString(module.refitTrack));
    if (module.refitRank > 0) {
        static constexpr std::array<std::string_view, 3> ranks {"I", "II", "III"};
        category += " ";
        if (module.refitRank <= static_cast<int>(ranks.size())) {
            category += ranks[static_cast<std::size_t>(module.refitRank - 1)];
        } else {
            category += std::to_string(module.refitRank);
        }
    }
    return {
        refitTrackClass(module.refitTrack, module.slot),
        category,
        std::string(toString(module.rarity)),
        std::string(toString(module.slot)).substr(0, 1),
        module.name,
        moduleThreat(module),
        modulePrimaryImpact(module),
        moduleStatChips(module)
    };
}

inline RefitPresentation crewUpgradeRefitPresentation(const CrewUpgrade& upgrade)
{
    return {
        slotClass(SlotType::Sensors),
        std::string(toString(upgrade.refitTrack)),
        std::string(toString(upgrade.rarity)),
        "C",
        upgrade.name,
        upgrade.description,
        crewUpgradePrimaryImpact(upgrade),
        crewUpgradeStatChips(upgrade)
    };
}

inline std::string refitCostSummary(int credits, const MaterialInventory& materials)
{
    const std::string creditCost = display::credits(credits);
    if (materials.common == 0 && materials.rare == 0 && materials.exotic == 0) {
        return creditCost;
    }
    return text::panel::creditsAndMaterials(creditCost, text::panel::materialSummary(materials.common, materials.rare, materials.exotic));
}

inline std::string refitFooterCostSummary(int credits, const MaterialInventory& materials)
{
    if (materials.common == 0 && materials.rare == 0 && materials.exotic == 0) {
        return display::credits(credits);
    }
    std::string summary = std::to_string(credits) + " cr";
    if (materials.common > 0) {
        summary += " + " + std::to_string(materials.common) + "C";
    }
    if (materials.rare > 0) {
        summary += " " + std::to_string(materials.rare) + "R";
    }
    if (materials.exotic > 0) {
        summary += " " + std::to_string(materials.exotic) + "X";
    }
    return summary;
}

inline std::string missingModuleCostLabel(const GameState& state, const ShipModule& module)
{
    if (state.run.credits < static_cast<double>(moduleOfferCost(module))) {
        return text::needCredits(moduleOfferCost(module));
    }
    if (!canAffordMaterials(state.meta.materials, module.materialCost)) {
        return std::string(text::panel::needMaterials);
    }
    return std::string(text::buttons::install);
}

inline void addResourceChip(std::vector<PanelMetricPresentation>& chips, std::string_view label, int value)
{
    if (value > 0) {
        chips.push_back(panelMetric(label, std::to_string(value)));
    }
}

inline std::vector<PanelMetricPresentation> refitResourceChips(const GameState& state)
{
    std::vector<PanelMetricPresentation> chips;
    addResourceChip(chips, text::labels::blueprints, state.meta.blueprintProgress);
    addResourceChip(chips, text::labels::commonMaterials, state.meta.materials.common);
    addResourceChip(chips, text::labels::rareMaterials, state.meta.materials.rare);
    addResourceChip(chips, text::labels::exoticMaterials, state.meta.materials.exotic);
    addResourceChip(chips, text::labels::artifacts, static_cast<int>(state.meta.artifacts.size()));
    return chips;
}

inline bool startsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

inline std::string refitRecoveryDetail(const GameState& state, bool hasResources)
{
    if (startsWith(state.statusLine, text::status::surfaceExtracted) ||
        startsWith(state.statusLine, text::status::surfaceExtractionRough)) {
        return state.statusLine;
    }
    return hasResources ? std::string(text::panel::messages::recoveredResourcesDetail) : std::string();
}

inline RefitOfferPresentation moduleOfferPresentation(const ShipModule& module, int index, const GameState& state)
{
    const int cost = moduleOfferCost(module);
    const bool affordable = canAffordModuleOffer(state, module);
    return {
        RefitOfferPresentationKind::ShipModule,
        index,
        cost,
        refitCostSummary(cost, module.materialCost),
        refitFooterCostSummary(cost, module.materialCost),
        affordable,
        moduleRefitPresentation(module),
        affordable
            ? panelActionButton(text::buttons::installPermanently, ui::actions::buyOffer(index), "ok")
            : disabledPanelButton(missingModuleCostLabel(state, module))
    };
}

inline RefitOfferPresentation launchUpgradeOfferPresentation(
    const ShipModule& module,
    int index,
    const GameState& state,
    const ContentCatalog& catalog)
{
    const int cost = static_cast<int>(tuning::launchProgression::upgradeCost);
    const bool affordable = canInstallLaunchUpgrade(state, catalog, module.launchUpgradeKind);
    return {
        RefitOfferPresentationKind::LaunchUpgrade,
        index,
        cost,
        display::credits(cost),
        display::credits(cost),
        affordable,
        moduleRefitPresentation(module),
        affordable
            ? panelActionButton(
                text::buttons::install,
                ui::actions::installLaunchUpgrade(static_cast<int>(module.launchUpgradeKind)),
                "ok")
            : disabledPanelButton(text::needCredits(cost))
    };
}

inline RefitOfferPresentation crewUpgradeOfferPresentation(const CrewUpgrade& upgrade, int index, double credits)
{
    const int cost = crewUpgradeCost(upgrade);
    const bool affordable = credits >= static_cast<double>(cost);
    return {
        RefitOfferPresentationKind::CrewUpgrade,
        index,
        cost,
        display::credits(cost),
        display::credits(cost),
        affordable,
        crewUpgradeRefitPresentation(upgrade),
        affordable
            ? panelActionButton(text::buttons::installPermanently, ui::actions::buyOffer(index), "ok")
            : disabledPanelButton(text::needCredits(cost))
    };
}

inline RefitWindowPresentation refitWindowPresentation(const GameState& state, const ContentCatalog& catalog)
{
    RefitWindowPresentation presentation;
    presentation.resourceChips = refitResourceChips(state);
    presentation.recoveryDetail = refitRecoveryDetail(state, !presentation.resourceChips.empty());
    presentation.offers.reserve(state.run.offerModuleIds.size() + 4);

    LaunchUpgradeKind lessonUpgrade = LaunchUpgradeKind::None;
    switch (state.meta.launchLessons.stage) {
    case LaunchTrainingStage::FlightControlsCalibration:
        lessonUpgrade = LaunchUpgradeKind::FuelTanks;
        break;
    case LaunchTrainingStage::MoonTransfer:
        lessonUpgrade = LaunchUpgradeKind::FlightControls;
        break;
    case LaunchTrainingStage::MarsTransfer:
        lessonUpgrade = LaunchUpgradeKind::Cooling;
        break;
    case LaunchTrainingStage::JupiterTransfer:
        lessonUpgrade = LaunchUpgradeKind::Hull;
        break;
    case LaunchTrainingStage::FuelCalibration:
    case LaunchTrainingStage::ThermalManagement:
    case LaunchTrainingStage::HullIntegrity:
    case LaunchTrainingStage::Complete:
        break;
    }
    if (lessonUpgrade != LaunchUpgradeKind::None && launchUpgradeRank(state, lessonUpgrade) == 0) {
        const ShipModule* module = nextLaunchUpgrade(state, catalog, lessonUpgrade);
        if (module != nullptr && launchUpgradeUnlocked(state, lessonUpgrade, module->launchUpgradeRank)) {
            presentation.offers.push_back(launchUpgradeOfferPresentation(
                *module,
                static_cast<int>(presentation.offers.size()),
                state,
                catalog));
        }
    }

    // Lesson rewards are deliberately a single, taught Launch upgrade. Normal
    // refit entitlements earned between lessons still expose the existing
    // non-Launch module and crew choices instead of opening an empty board.
    if (!curatedProvingRefitsActive(state)) {
        for (std::size_t i = 0; i < state.run.offerModuleIds.size(); ++i) {
            const int index = static_cast<int>(i);
            if (const ShipModule* module = catalog.findModule(state.run.offerModuleIds[i])) {
                if (module->launchUpgradeKind != LaunchUpgradeKind::None ||
                    module->compatibilityOnly) {
                    continue;
                }
                presentation.offers.push_back(moduleOfferPresentation(*module, index, state));
                continue;
            }

            if (const CrewUpgrade* upgrade = catalog.findCrewUpgrade(state.run.offerCrewUpgradeIds[i])) {
                presentation.offers.push_back(crewUpgradeOfferPresentation(*upgrade, index, state.run.credits));
            }
        }
    }

    presentation.rerollCost = offerRerollCost(state);
    presentation.showReroll = state.meta.launchLessons.stage == LaunchTrainingStage::Complete &&
        !curatedProvingRefitsActive(state);
    presentation.rerollAction = state.run.credits >= presentation.rerollCost
        ? panelActionButton(text::panel::rerollOffers(display::money(presentation.rerollCost)), ui::actions::rerollOffers, "warn")
        : disabledPanelButton(display::needCredits(presentation.rerollCost));
    // Training refits teach a required capability.  They are not a choice
    // window: leaving without the upgrade only produces a blocked launch.
    presentation.showSkip = !curatedProvingRefitsActive(state);
    presentation.skipAction = panelActionButton(text::buttons::keepCredits, ui::actions::next);
    return presentation;
}

} // namespace rocket
