#pragma once

#include "core/Content.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"
#include "core/GameUi.h"
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
    CrewUpgrade
};

struct RefitOfferPresentation {
    RefitOfferPresentationKind kind = RefitOfferPresentationKind::ShipModule;
    int index = 0;
    int cost = 0;
    bool affordable = false;
    RefitPresentation card;
    PanelButtonPresentation action;
};

struct RefitWindowPresentation {
    std::vector<RefitOfferPresentation> offers;
    double rerollCost = 0.0;
    PanelButtonPresentation rerollAction;
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

inline std::array<ModuleStatDisplay, 10> moduleStatDisplays(const ModuleStats& stats)
{
    return {{
        {stats.thrust, text::moduleStats::speed, text::moduleStats::speedChip, text::moduleStats::thrustDetail},
        {stats.fuel, text::moduleStats::fuel, text::moduleStats::fuelChip, text::moduleStats::fuel},
        {stats.hull, text::moduleStats::hull, text::moduleStats::hullChip, text::moduleStats::hull},
        {stats.cooling, text::moduleStats::tempControl, text::moduleStats::tempChip, text::moduleStats::tempControl},
        {stats.sensors, text::moduleStats::sensors, text::moduleStats::sensorsChip, text::moduleStats::sensors},
        {stats.escape, text::moduleStats::escape, text::moduleStats::escapeChip, text::moduleStats::escape},
        {stats.pressure, text::moduleStats::pressureControl, text::moduleStats::pressureChip, text::moduleStats::pressureControl},
        {stats.volatility, text::moduleStats::volatility, text::moduleStats::volatilityChip, text::moduleStats::volatility, false},
        {stats.payout, text::moduleStats::dataPayout, text::moduleStats::payoutChip, text::moduleStats::dataPayout, false},
        {stats.repair, text::moduleStats::repairCost, text::moduleStats::repairChip, text::moduleStats::repairCost, false}
    }};
}

inline std::string moduleThreat(const ShipModule& module)
{
    switch (module.slot) {
    case SlotType::Engine:
        return std::string(module.stats.thrust >= 0.0 ? text::moduleThreats::shortensExposure : text::moduleThreats::reducesEngineLoad);
    case SlotType::Fuel:
        return std::string(module.stats.pressure > 0.0 ? text::moduleThreats::stabilizesPressure : text::moduleThreats::extendsReturnMargin);
    case SlotType::Hull:
        return std::string(text::moduleThreats::absorbsDamage);
    case SlotType::Cooling:
        return std::string(text::moduleThreats::lowersTemperature);
    case SlotType::Sensors:
        return std::string(module.stats.pressure > 0.0 ? text::moduleThreats::reducesPressureUncertainty : text::moduleThreats::improvesWarningLuck);
    case SlotType::Escape:
        return std::string(text::moduleThreats::improvesCrewSurvival);
    }
    return std::string(text::moduleThreats::improvesMissionOdds);
}

inline std::string modulePrimaryImpact(const ShipModule& module)
{
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

inline std::vector<RefitStatChip> moduleStatChips(const ShipModule& module)
{
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
    addStatChip(chips, text::moduleStats::simStressChip, static_cast<double>(stats.trainingStressRelief));
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
    return {
        slotClass(module.slot),
        std::string(toString(module.slot)),
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
        std::string(text::panel::details::crew),
        std::string(toString(upgrade.rarity)),
        "C",
        upgrade.name,
        upgrade.description,
        crewUpgradePrimaryImpact(upgrade),
        crewUpgradeStatChips(upgrade)
    };
}

inline RefitOfferPresentation moduleOfferPresentation(const ShipModule& module, int index, double credits)
{
    const int cost = moduleOfferCost(module);
    const bool affordable = credits >= static_cast<double>(cost);
    return {
        RefitOfferPresentationKind::ShipModule,
        index,
        cost,
        affordable,
        moduleRefitPresentation(module),
        affordable
            ? panelActionButton(text::buttons::install, ui::actions::buyOffer(index), "ok")
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
        affordable,
        crewUpgradeRefitPresentation(upgrade),
        affordable
            ? panelActionButton(text::buttons::install, ui::actions::buyOffer(index), "ok")
            : disabledPanelButton(text::needCredits(cost))
    };
}

inline RefitWindowPresentation refitWindowPresentation(const GameState& state, const ContentCatalog& catalog)
{
    RefitWindowPresentation presentation;
    presentation.offers.reserve(state.run.offerModuleIds.size());

    for (std::size_t i = 0; i < state.run.offerModuleIds.size(); ++i) {
        const int index = static_cast<int>(i);
        if (const ShipModule* module = catalog.findModule(state.run.offerModuleIds[i])) {
            presentation.offers.push_back(moduleOfferPresentation(*module, index, state.run.credits));
            continue;
        }

        if (const CrewUpgrade* upgrade = catalog.findCrewUpgrade(state.run.offerCrewUpgradeIds[i])) {
            presentation.offers.push_back(crewUpgradeOfferPresentation(*upgrade, index, state.run.credits));
        }
    }

    presentation.rerollCost = offerRerollCost(state);
    presentation.rerollAction = state.run.credits >= presentation.rerollCost
        ? panelActionButton(text::panel::rerollOffers(display::money(presentation.rerollCost)), ui::actions::rerollOffers, "warn")
        : disabledPanelButton(display::needCredits(presentation.rerollCost));
    presentation.skipAction = panelActionButton(text::buttons::skipRefit, ui::actions::next);
    return presentation;
}

} // namespace rocket
