#include "game/GamePanel.h"
#include "core/GameText.h"
#include "core/Telemetry.h"
#include "core/Tuning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace rocket {

namespace {

std::string htmlEscape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

std::string money(double value)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(0) << value;
    return out.str();
}

std::string signedMoney(double value)
{
    std::ostringstream out;
    if (value > 0.0) {
        out << "+";
    }
    out << std::fixed << std::setprecision(0) << value;
    return out.str();
}

std::string multiplier(double value)
{
    std::ostringstream out;
    out << "x" << std::fixed << std::setprecision(2) << value;
    return out.str();
}

std::string percent(double value)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(0) << std::clamp(value, 0.0, 1.0) * 100.0 << "%";
    return out.str();
}

std::string signedPercent(double value)
{
    std::ostringstream out;
    if (value > 0.0) {
        out << "+";
    }
    out << std::fixed << std::setprecision(0) << value * 100.0 << "%";
    return out.str();
}

std::string metric(std::string_view label, std::string value)
{
    return "<div class=\"metric\"><strong>" + htmlEscape(value) + "</strong><span>" + htmlEscape(label) + "</span></div>";
}

std::string button(std::string_view label, std::string action, std::string cssClass = "")
{
    const std::string classAttr = cssClass.empty() ? "" : " class=\"" + cssClass + "\"";
    return "<button" + classAttr + " data-rr-action=\"" + htmlEscape(action) + "\">" + htmlEscape(label) + "</button>";
}

std::string modalButton(std::string_view label, std::string modalId, std::string cssClass = "")
{
    const std::string classAttr = cssClass.empty() ? "" : " class=\"" + cssClass + "\"";
    return "<button" + classAttr + " data-ui-modal=\"" + htmlEscape(modalId) + "\">" + htmlEscape(label) + "</button>";
}

std::string modalTemplate(std::string modalId, std::string title, std::string body)
{
    return "<template data-modal=\"" + htmlEscape(modalId) + "\" data-title=\"" + htmlEscape(title) + "\">" + body + "</template>";
}

std::string disabledButton(std::string_view label)
{
    return "<button disabled>" + htmlEscape(label) + "</button>";
}

std::string warningClass(double value)
{
    if (value >= tuning::launch::warningCriticalThreshold) {
        return "critical";
    }
    if (value >= tuning::launch::warningCautionThreshold) {
        return "caution";
    }
    return "nominal";
}

std::string warningButton(std::string_view label, double value)
{
    return "<button type=\"button\" class=\"warning-button " + warningClass(value) + "\"><strong>" +
        htmlEscape(label) + "</strong><span>" + htmlEscape(percent(value)) + "</span></button>";
}

std::string outcomeLabel(const LaunchOutcome& outcome)
{
    if (outcome.type == LaunchResultType::Destroyed) {
        if (outcome.recoveryMethod == RecoveryMethod::ReturnHome) {
            return std::string(text::panel::outcomes::returnFailure);
        }
        return std::string(outcome.frontierTransfer ? text::panel::outcomes::transferLost : text::panel::outcomes::vehicleLost);
    }
    if (outcome.recoveryMethod == RecoveryMethod::ManualEject) {
        return std::string(text::panel::outcomes::emergencyEject);
    }
    if (outcome.recoveryMethod == RecoveryMethod::ReturnHome) {
        return std::string(outcome.type == LaunchResultType::MissionComplete ? text::panel::outcomes::profileReturned : text::panel::outcomes::earlyReturn);
    }
    if (outcome.frontierTransfer) {
        return std::string(outcome.type == LaunchResultType::MissionComplete ? text::panel::outcomes::transferComplete : text::panel::outcomes::transferAborted);
    }
    return std::string(outcome.type == LaunchResultType::MissionComplete ? text::panel::outcomes::dataProfileComplete : text::panel::outcomes::provingReturn);
}

std::string slotClass(SlotType slot)
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

std::string moduleThreat(const ShipModule& module)
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

struct ModuleStatDisplay {
    double value;
    std::string_view primaryLabel;
    std::string_view chipLabel;
    std::string_view detailLabel;
    bool showInShipDetails = true;
};

std::array<ModuleStatDisplay, 10> moduleStatDisplays(const ModuleStats& stats)
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

std::string primaryImpact(const ShipModule& module)
{
    const auto displays = moduleStatDisplays(module.stats);
    const ModuleStatDisplay* best = &displays.front();
    for (const ModuleStatDisplay& display : displays) {
        if (std::abs(display.value) > std::abs(best->value)) {
            best = &display;
        }
    }

    std::ostringstream out;
    out << (best->value >= 0.0 ? "+" : "") << std::fixed << std::setprecision(1) << best->value << " " << best->primaryLabel;
    return out.str();
}

std::string statTag(std::string_view label, double value)
{
    if (std::abs(value) < 0.05) {
        return "";
    }

    std::ostringstream out;
    out << "<span class=\"stat-chip " << (value >= 0.0 ? "up" : "down") << "\">"
        << htmlEscape(label) << " " << (value >= 0.0 ? "+" : "")
        << std::fixed << std::setprecision(1) << value << "</span>";
    return out.str();
}

std::string moduleStatTags(const ShipModule& module)
{
    const ModuleStats& stats = module.stats;
    std::string tags;
    for (const ModuleStatDisplay& display : moduleStatDisplays(stats)) {
        tags += statTag(display.chipLabel, display.value);
    }
    return tags;
}

std::string crewUpgradeStatTags(const CrewUpgrade& upgrade)
{
    const CrewUpgradeStats& stats = upgrade.stats;
    std::string tags;
    tags += statTag(text::moduleStats::trainingChip, static_cast<double>(stats.trainingGain));
    tags += statTag(text::moduleStats::simStressChip, static_cast<double>(stats.trainingStressRelief));
    tags += statTag(text::moduleStats::restChip, static_cast<double>(stats.restStressBonus));
    tags += statTag(text::moduleStats::launchStressChip, static_cast<double>(stats.launchStressRelief));
    tags += statTag(text::moduleStats::traitChip, stats.traitModifier * 100.0);
    return tags;
}

std::string moduleCard(const ShipModule& module, int index, double credits)
{
    const int cost = moduleOfferCost(module);
    const bool affordable = credits >= static_cast<double>(cost);
    std::ostringstream out;
    out << "<article class=\"upgrade-card slot-" << slotClass(module.slot) << "\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(toString(module.slot)) << "</span><span>"
        << htmlEscape(toString(module.rarity)) << "</span></div>";
    out << "<div class=\"module-art\"><span>" << htmlEscape(std::string(toString(module.slot)).substr(0, 1)) << "</span></div>";
    out << "<h3>" << htmlEscape(module.name) << "</h3>";
    out << "<p class=\"module-threat\">" << htmlEscape(moduleThreat(module)) << "</p>";
    out << "<strong class=\"module-impact\">" << htmlEscape(primaryImpact(module)) << "</strong>";
    out << "<div class=\"stat-grid\">" << moduleStatTags(module) << "</div>";
    out << "<div class=\"card-footer\"><span>" << cost << " credits</span>";
    if (affordable) {
        out << button(text::buttons::install, "rr.buyOffer(" + std::to_string(index) + ")", "ok");
    } else {
        out << disabledButton(text::needCredits(cost));
    }
    out << "</div></article>";
    return out.str();
}

std::string crewUpgradeImpact(const CrewUpgrade& upgrade)
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
        return "+" + percent(stats.traitModifier) + " trait modifiers";
    }
    return std::string(text::panel::messages::crewOpsFallback);
}

std::string crewUpgradeCard(const CrewUpgrade& upgrade, int index, double credits)
{
    const int cost = crewUpgradeCost(upgrade);
    const bool affordable = credits >= static_cast<double>(cost);
    std::ostringstream out;
    out << "<article class=\"upgrade-card slot-sensors\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(text::panel::details::crew) << "</span><span>" << htmlEscape(toString(upgrade.rarity)) << "</span></div>";
    out << "<div class=\"module-art\"><span>C</span></div>";
    out << "<h3>" << htmlEscape(upgrade.name) << "</h3>";
    out << "<p class=\"module-threat\">" << htmlEscape(upgrade.description) << "</p>";
    out << "<strong class=\"module-impact\">" << htmlEscape(crewUpgradeImpact(upgrade)) << "</strong>";
    out << "<div class=\"stat-grid\">" << crewUpgradeStatTags(upgrade) << "</div>";
    out << "<div class=\"card-footer\"><span>" << cost << " credits</span>";
    if (affordable) {
        out << button(text::buttons::install, "rr.buyOffer(" + std::to_string(index) + ")", "ok");
    } else {
        out << disabledButton(text::needCredits(cost));
    }
    out << "</div></article>";
    return out.str();
}

std::string operationCard(std::string title, std::string detail, std::string cost, std::string action, bool available, std::string cssClass = "")
{
    std::ostringstream out;
    out << "<article class=\"ops-card " << htmlEscape(cssClass) << "\">";
    out << "<h3>" << htmlEscape(title) << "</h3>";
    out << "<p>" << htmlEscape(detail) << "</p>";
    out << "<div class=\"card-footer\"><span>" << htmlEscape(cost) << "</span>";
    out << (available ? button(text::buttons::assign, action) : disabledButton(text::buttons::unavailable));
    out << "</div></article>";
    return out.str();
}

std::string detailRow(std::string_view label, std::string_view value)
{
    return "<div class=\"detail-row\"><span>" + htmlEscape(label) + "</span><strong>" + htmlEscape(value) + "</strong></div>";
}

std::string detailHeader(std::string_view label)
{
    return "<div class=\"detail-section\">" + htmlEscape(label) + "</div>";
}

std::string crewStressSummary(const Astronaut* astronaut)
{
    if (astronaut == nullptr) {
        return std::string(text::panel::noActiveCrew);
    }
    return std::to_string(astronaut->stress) + "%";
}

double smoothStep(double value)
{
    const double t = std::clamp(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

std::string buildGamePanelHtml(const PanelRenderContext& context)
{
    const GameState& state = context.state;
    const ContentCatalog& catalog = context.catalog;
    const Destination& currentFrontier = currentDestination(state, catalog);
    const Destination* displayDestination = &currentFrontier;
    if (state.screen == Screen::Launch) {
        if (const Destination* activeDestination = catalog.findDestination(context.activeLaunch.config.destinationId)) {
            displayDestination = activeDestination;
        }
    }

    const Astronaut* astronaut = activeAstronaut(state);
    const ModuleStats stats = aggregateShipStats(state, catalog);
    const bool transferLaunch = state.screen == Screen::Launch && context.activeLaunch.config.frontierTransfer;
    const int requiredReadiness = frontierReadinessRequired(state, catalog);
    const Destination* next = nextDestination(state, catalog);
    const bool hullLaunchBlocked = state.run.shipDamage >= tuning::damage::destroyedShipDamage;
    const bool crewLaunchBlocked = astronaut == nullptr;

    std::ostringstream out;
    out << "<div class=\"panel-head\"><div><h1>" << htmlEscape(text::panel::title) << "</h1></div>"
        << modalButton(text::buttons::settings, "settings", "ghost") << "</div>";
    out << "<p class=\"status\">" << htmlEscape(state.statusLine) << "</p>";

    std::ostringstream settingsBody;
    settingsBody << "<div class=\"detail-stack\">";
    settingsBody << detailRow(text::panel::details::keyboard, text::panel::details::keyboardValue);
    settingsBody << detailRow(text::panel::details::save, text::panel::details::saveValue);
    settingsBody << detailRow(text::panel::details::build, text::panel::details::buildValue);
    settingsBody << "</div><div class=\"modal-actions\">";
    settingsBody << button(text::buttons::resetSave, "rr.resetSave()", "danger");
    settingsBody << "</div>";

    out << "<div class=\"metric-grid\">";
    out << metric(text::labels::missionCredits, money(state.run.credits));
    out << metric(text::labels::hullDamage, std::to_string(state.run.shipDamage) + "%");
    out << metric(transferLaunch ? text::labels::transferTarget : text::labels::currentFrontier, displayDestination->name);
    out << metric(
        transferLaunch ? std::string_view(text::labels::requiredBurn) : std::string_view(text::labels::flightData),
        transferLaunch ? multiplier(displayDestination->targetMultiplier) :
                         (requiredReadiness == 0 ? std::string(text::panel::complete) : std::to_string(state.run.frontierReadiness) + "/" + std::to_string(requiredReadiness)));
    out << metric(text::labels::missionDifficulty, signedPercent(state.screen == Screen::Launch ? context.flightModel.pressureModifier : missionPressureModifier(state, catalog, *displayDestination)));
    out << metric(text::labels::crewStress, crewStressSummary(astronaut));
    out << "</div>";

    if (state.screen == Screen::Launch) {
        const double displayedMultiplier = context.returningHome
            ? returnTelemetryMultiplier(context.returnBurnMultiplier, context.flightModel.crashMultiplier, context.returnElapsed, context.returnDuration)
            : context.currentMultiplier;
        const TelemetryEvent event = telemetryAt(context.flightModel, displayedMultiplier);
        const double recoveryRisk = returnHomeRisk(context.flightModel, catalog, state, displayedMultiplier);
        const double returnProgress = smoothStep(context.returnElapsed / std::max(0.1, context.returnDuration));

        out << "<h2>" << htmlEscape(context.returningHome ? text::panel::sections::returnBurn : (transferLaunch ? text::panel::sections::transferAttempt : text::panel::sections::provingFlight)) << "</h2>";
        out << "<div class=\"metric-grid\">";
        out << metric(text::labels::burnDepth, multiplier(displayedMultiplier));
        out << metric(context.returningHome ? text::labels::returnProgress : (transferLaunch ? text::labels::requiredBurn : text::labels::dataGoal),
            context.returningHome ? percent(returnProgress) : multiplier(displayDestination->targetMultiplier));
        out << metric(text::labels::returnRisk, percent(recoveryRisk));
        out << "</div>";

        out << "<h2>" << htmlEscape(text::panel::sections::telemetry) << "</h2>";
        out << "<div class=\"warning-grid\">";
        for (const TelemetryChannelSample& sample : telemetrySamples(event)) {
            out << warningButton(sample.label, sample.value);
        }
        out << "</div>";
        out << "<div class=\"utility-row\">" << modalButton(text::panel::modals::telemetryDetails, "telemetry", "ghost") << "</div>";
        out << "<p class=\"status\">" << htmlEscape(event.message) << "</p>";

        out << "<h2>" << htmlEscape(text::panel::sections::flightControls) << "</h2>";
        out << "<div class=\"actions primary-actions\">";
        if (context.returningHome) {
            out << disabledButton(text::buttons::returningHome);
        } else {
            out << button(text::buttons::returnHome, "rr.returnHome()", "ok");
        }
        out << button(text::buttons::eject, "rr.ejectNow()", "danger");
        out << "</div>";

        out << "<div class=\"actions system-actions\">";
        if (context.returningHome) {
            out << disabledButton(text::buttons::cutEngines);
            out << disabledButton(text::buttons::reliefValve);
            out << disabledButton(text::buttons::jettisonCargo);
        } else {
            out << button(context.cutEnginesActive ? text::buttons::restoreThrust : text::buttons::cutEngines, "rr.cutEngines()", "warn");
            out << (context.pressureReliefUsed
                ? (context.pressureReliefFailed
                    ? disabledButton(text::buttons::reliefValveFailed)
                    : (context.pressureReliefOpen
                        ? button(text::buttons::closeValve, "rr.closeReliefValve()", "warn")
                        : disabledButton(text::buttons::valveClosed)))
                : button(text::buttons::reliefValve, "rr.pressureRelief()", "warn"));
            out << (context.cargoJettisoned
                ? disabledButton(text::buttons::cargoGone)
                : button(text::buttons::jettisonCargo, "rr.jettisonCargo()", "warn"));
        }
        out << "</div>";
        std::ostringstream telemetryBody;
        telemetryBody << "<div class=\"detail-stack\">";
        for (const TelemetryChannelSample& sample : telemetrySamples(event)) {
            telemetryBody << detailRow(std::string(sample.label), percent(sample.value));
        }
        telemetryBody << detailRow(std::string(text::labels::returnRisk), percent(recoveryRisk));
        telemetryBody << detailRow(std::string(text::labels::missionDifficulty), signedPercent(context.flightModel.pressureModifier));
        telemetryBody << "</div>";
        out << modalTemplate("telemetry", std::string(text::panel::modals::telemetryDetails), telemetryBody.str());
        out << modalTemplate("settings", std::string(text::panel::modals::settings), settingsBody.str());
        return out.str();
    }

    if (state.screen == Screen::Results) {
        out << "<h2>" << htmlEscape(text::panel::sections::result) << "</h2>";
        out << "<div class=\"metric-grid\">";
        out << metric(text::labels::outcome, outcomeLabel(state.lastOutcome));
        out << metric(text::labels::recovery, std::string(toString(state.lastOutcome.recoveryMethod)));
        out << metric(text::labels::burnDepth, multiplier(state.lastOutcome.ejectMultiplier));
        out << metric(text::labels::failurePoint, multiplier(state.lastOutcome.crashMultiplier));
        out << metric(text::labels::peakWarning, percent(state.lastOutcome.peakWarning));
        out << metric(text::labels::peakAbort, percent(state.lastOutcome.peakAbortRisk));
        out << metric(text::labels::creditDelta, signedMoney(state.lastOutcome.payout - state.lastOutcome.recoveryCost));
        out << "</div>";
        if (!state.lastOutcome.moduleDestroyedId.empty()) {
            out << "<p class=\"status\">" << htmlEscape(text::panel::lostModule(state.lastOutcome.moduleDestroyedId)) << "</p>";
        }
        if (state.lastOutcome.crewKilled) {
            out << "<p class=\"status\">" << htmlEscape(text::panel::messages::crewLossRecorded) << "</p>";
        } else if (state.lastOutcome.crewInjured) {
            out << "<p class=\"status\">" << htmlEscape(text::panel::messages::crewInjured) << "</p>";
        }
        out << "<div class=\"actions\">";
        out << button(state.lastOutcome.type == LaunchResultType::Destroyed ? text::buttons::startReplacementRefit : text::buttons::reviewRefitOptions, "rr.next()", "ok");
        out << "</div>";
        out << modalTemplate("settings", std::string(text::panel::modals::settings), settingsBody.str());
        return out.str();
    }

    if (state.screen == Screen::Upgrade) {
        out << "<h2>" << htmlEscape(text::panel::sections::refitWindow) << "</h2>";
        out << "<p>" << htmlEscape(text::panel::messages::chooseOneRefit) << "</p>";
        out << "<div class=\"upgrade-grid\">";
        for (std::size_t i = 0; i < state.run.offerModuleIds.size(); ++i) {
            const ShipModule* module = catalog.findModule(state.run.offerModuleIds[i]);
            if (module != nullptr) {
                out << moduleCard(*module, static_cast<int>(i), state.run.credits);
                continue;
            }

            const CrewUpgrade* upgrade = catalog.findCrewUpgrade(state.run.offerCrewUpgradeIds[i]);
            if (upgrade != nullptr) {
                out << crewUpgradeCard(*upgrade, static_cast<int>(i), state.run.credits);
            }
        }
        const double rerollCost = offerRerollCost(state);
        out << "</div><div class=\"actions\">";
        out << (state.run.credits >= rerollCost
            ? button(text::panel::rerollOffers(money(rerollCost)), "rr.rerollOffers()", "warn")
            : disabledButton(text::panel::needCredits(money(rerollCost))));
        out << button(text::buttons::skipRefit, "rr.next()");
        out << "</div>";
        out << modalTemplate("settings", std::string(text::panel::modals::settings), settingsBody.str());
        return out.str();
    }

    std::ostringstream shipBody;
    shipBody << "<div class=\"detail-stack\">";
    for (const ModuleStatDisplay& display : moduleStatDisplays(stats)) {
        if (display.showInShipDetails) {
            shipBody << detailRow(std::string(display.detailLabel), money(display.value));
        }
    }
    shipBody << detailRow(std::string(text::moduleStats::damage), std::to_string(state.run.shipDamage) + "%");
    shipBody << detailHeader(text::panel::details::equippedShipUpgrades);
    for (const std::string& moduleId : state.run.equippedModuleIds) {
        if (const ShipModule* module = catalog.findModule(moduleId)) {
            shipBody << detailRow(std::string(toString(module->slot)), module->name + " (" + std::string(toString(module->rarity)) + ")");
        }
    }
    shipBody << detailHeader(text::panel::details::storedShipUpgrades);
    bool hasStoredModule = false;
    for (const std::string& moduleId : state.run.inventoryModuleIds) {
        const bool equipped = std::find(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), moduleId) != state.run.equippedModuleIds.end();
        if (!equipped) {
            if (const ShipModule* module = catalog.findModule(moduleId)) {
                hasStoredModule = true;
                shipBody << detailRow(std::string(toString(module->slot)), module->name + " (" + std::string(toString(module->rarity)) + ")");
            }
        }
    }
    if (!hasStoredModule) {
        shipBody << detailRow(text::panel::details::inventory, text::panel::noSpareModules);
    }
    shipBody << "</div>";

    std::ostringstream crewBody;
    crewBody << "<div class=\"detail-stack\">";
    const CrewUpgradeStats crewUpgrades = aggregateCrewUpgradeStats(state, catalog);
    if (astronaut != nullptr) {
        const int stressSteps = crewStressStepCount(astronaut->stress);
        crewBody << detailRow(text::panel::details::active, astronaut->name);
        crewBody << detailRow(text::panel::details::trait, astronaut->trait);
        crewBody << detailRow(text::panel::details::training, std::to_string(astronaut->training) + " (" + std::to_string(effectiveTrainingLevel(*astronaut)) + " effective)");
        crewBody << detailRow(text::panel::details::stress, std::to_string(astronaut->stress) + "% / " + std::to_string(stressSteps) + " steps");
        crewBody << detailRow(text::panel::details::stressEffects, "NAV +" + percent(crewNavigationPenaltyFromStress(astronaut->stress)) + ", ABORT " + multiplier(crewAbortRiskMultiplierFromStress(astronaut->stress)));
        crewBody << detailRow(text::panel::details::status, std::string(toString(astronaut->status)));
    } else {
        crewBody << detailRow(text::panel::details::active, text::panel::noneCleared);
    }
    crewBody << detailHeader(text::panel::details::crewFacilities);
    if (state.run.crewUpgradeIds.empty()) {
        crewBody << detailRow(text::panel::details::facilities, text::panel::baselineTrainingRoom);
    } else {
        for (const std::string& upgradeId : state.run.crewUpgradeIds) {
            if (const CrewUpgrade* upgrade = catalog.findCrewUpgrade(upgradeId)) {
                crewBody << detailRow(std::string(toString(upgrade->rarity)), upgrade->name);
            }
        }
    }
    crewBody << detailHeader(text::panel::details::facilityEffects);
    crewBody << detailRow(text::panel::details::simulatorGain, "+" + std::to_string(std::max(1, 1 + crewUpgrades.trainingGain)) + " training");
    crewBody << detailRow(text::panel::details::simulatorStress, "+" + std::to_string(crewTrainingStressGain(state, catalog)) + " stress");
    crewBody << detailRow(text::panel::details::medicalRest, "-" + std::to_string(crewRestStressRecovery(state, catalog)) + " stress now");
    crewBody << detailRow(text::panel::details::launchStress, "-" + std::to_string(crewUpgrades.launchStressRelief) + " stress");
    crewBody << detailRow(text::panel::details::traitModifiers, "+" + percent(std::max(0.0, crewUpgrades.traitModifier)));
    crewBody << "</div>";

    std::ostringstream frontierBody;
    frontierBody << "<div class=\"detail-stack\">";
    frontierBody << detailRow(text::panel::details::current, currentFrontier.name);
    frontierBody << detailRow(text::labels::flightData, requiredReadiness == 0 ? std::string(text::panel::complete) : std::to_string(state.run.frontierReadiness) + "/" + std::to_string(requiredReadiness));
    frontierBody << detailRow(text::labels::missionDifficulty, signedPercent(missionPressureModifier(state, catalog, currentFrontier)));
    if (next != nullptr) {
        frontierBody << detailRow(text::panel::details::next, next->name);
        frontierBody << detailRow(text::panel::details::transferBurn, multiplier(next->targetMultiplier));
    } else {
        frontierBody << detailRow(text::panel::details::next, text::panel::noneCharted);
    }
    frontierBody << "</div>";

    std::ostringstream launchBlockedBody;
    launchBlockedBody << "<div class=\"detail-stack\">";
    if (hullLaunchBlocked) {
        launchBlockedBody << "<p class=\"status\">" << htmlEscape(text::panel::messages::totalHullBlocked) << "</p>";
    }
    if (crewLaunchBlocked) {
        launchBlockedBody << "<p class=\"status\">" << htmlEscape(text::panel::messages::noLivingCrewBlocked) << "</p>";
    }
    launchBlockedBody << detailRow(text::labels::hullDamage, std::to_string(state.run.shipDamage) + "%");
    launchBlockedBody << detailRow(text::panel::details::crew, astronaut == nullptr ? std::string(text::panel::noneCleared) : astronaut->name);
    launchBlockedBody << detailRow(text::panel::details::requiredAction, hullLaunchBlocked ? text::panel::details::repairVehicle : text::panel::details::recruitCrew);
    launchBlockedBody << "</div><div class=\"modal-actions actions\">";
    if (hullLaunchBlocked) {
        const double blockedRepairCost = repairShipCost(state);
        launchBlockedBody << (state.run.credits >= blockedRepairCost
            ? button(text::buttons::assignRepairBay, "rr.repairShip()", "ok")
            : disabledButton(text::panel::needCredits(money(blockedRepairCost))));
    }
    if (crewLaunchBlocked) {
        launchBlockedBody << button(text::buttons::recruitCrew, "rr.recruitCrew()", "ok");
    }
    launchBlockedBody << "</div>";

    out << "<h2>" << htmlEscape(text::panel::sections::hangarBay) << "</h2>";
    out << "<div class=\"summary-grid\">";
    out << "<article class=\"summary-card\"><span>" << htmlEscape(text::panel::details::ship) << "</span><strong>" << state.run.shipDamage << "% damage</strong>"
        << modalButton(text::buttons::details, "ship", "ghost") << "</article>";
    out << "<article class=\"summary-card\"><span>" << htmlEscape(text::panel::details::crew) << "</span><strong>"
        << htmlEscape(astronaut == nullptr ? std::string(text::panel::noPilot) : astronaut->name) << "</strong>"
        << modalButton(text::buttons::details, "crew", "ghost") << "</article>";
    out << "<article class=\"summary-card\"><span>" << htmlEscape(text::panel::details::frontier) << "</span><strong>" << htmlEscape(currentFrontier.name) << "</strong>"
        << modalButton(text::buttons::details, "frontier", "ghost") << "</article>";
    out << "</div>";

    out << "<h2>" << htmlEscape(text::panel::sections::hangarOps) << "</h2>";
    out << "<div class=\"ops-grid\">";
    const int repairAmount = repairShipAmount(state);
    const double repairCost = repairShipCost(state);
    out << operationCard(
        std::string(text::panel::ops::repairBay),
        repairAmount > 0 ? text::panel::repairDetail(repairAmount) : std::string(text::panel::messages::noStructuralWork),
        repairAmount > 0 ? text::panel::credits(money(repairCost)) : std::string(text::panel::shipStable),
        "rr.repairShip()",
        repairAmount > 0 && state.run.credits >= repairCost,
        "repair");
    if (astronaut == nullptr) {
        out << operationCard(std::string(text::panel::ops::crewIntake), std::string(text::panel::messages::emergencyReplacement), std::string(text::panel::zeroCredits), "rr.recruitCrew()", true, "crew");
        out << operationCard(std::string(text::panel::ops::reserveRoster), std::string(text::panel::messages::reserveRoster), text::panel::credits(money(tuning::hangar::recruitCost)), "rr.recruitCrew()", state.run.credits >= tuning::hangar::recruitCost, "crew");
    } else {
        out << operationCard(
            std::string(text::panel::ops::simulatorBurn),
            text::panel::simulatorDetail(std::max(1, 1 + crewUpgrades.trainingGain), crewTrainingStressGain(state, catalog)),
            text::panel::credits(money(crewTrainingCost(state, catalog))),
            "rr.trainCrew()",
            state.run.credits >= crewTrainingCost(state, catalog) && astronaut->stress + crewTrainingStressGain(state, catalog) <= tuning::crew::maxStress,
            "crew");
        out << operationCard(
            std::string(text::panel::ops::medicalRest),
            text::panel::restDetail(crewRestStressRecovery(state, catalog)),
            text::panel::credits(money(crewRestCost(state, catalog))),
            "rr.restCrew()",
            state.run.credits >= crewRestCost(state, catalog),
            "crew");
    }
    out << "</div>";

    out << "<div class=\"actions\">";
    out << (hullLaunchBlocked || crewLaunchBlocked
        ? modalButton(text::buttons::launchProvingFlight, "launch_blocked", "ok")
        : button(text::buttons::launchProvingFlight, "rr.startLaunch()", "ok"));
    if (next != nullptr) {
        if (canCommitToNextFrontier(state, catalog)) {
            out << (hullLaunchBlocked || crewLaunchBlocked
                ? modalButton(text::panel::attemptFrontier(next->name), "launch_blocked", "danger")
                : button(text::panel::attemptFrontier(next->name), "rr.attemptFrontier()", "danger"));
        } else {
            out << disabledButton(text::buttons::needFlightData);
        }
    }
    out << "</div>";

    std::ostringstream legacyBody;
    legacyBody << "<div class=\"detail-stack\">";
    legacyBody << detailRow(text::panel::details::blueprints, std::to_string(state.meta.blueprintProgress));
    legacyBody << detailRow(text::panel::details::shipsLost, std::to_string(state.meta.shipsLost));
    legacyBody << detailRow(text::panel::details::astronautsLost, std::to_string(state.meta.astronautsLost));
    legacyBody << detailRow(text::panel::details::furthestTier, std::to_string(state.meta.furthestTier));
    legacyBody << "</div>";

    out << "<div class=\"utility-row bottom-tools\">";
    out << modalButton(text::buttons::legacy, "legacy", "ghost");
    out << "</div>";
    out << modalTemplate("ship", std::string(text::panel::modals::shipDetails), shipBody.str());
    out << modalTemplate("crew", std::string(text::panel::modals::crewDetails), crewBody.str());
    out << modalTemplate("frontier", std::string(text::panel::modals::frontierDetails), frontierBody.str());
    out << modalTemplate("launch_blocked", std::string(text::panel::modals::launchHold), launchBlockedBody.str());
    out << modalTemplate("legacy", std::string(text::panel::modals::legacy), legacyBody.str());
    out << modalTemplate("settings", std::string(text::panel::modals::settings), settingsBody.str());

    return out.str();
}

} // namespace rocket
