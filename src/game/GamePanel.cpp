#include "game/GamePanel.h"

#include <algorithm>
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

std::string metric(std::string label, std::string value)
{
    return "<div class=\"metric\"><strong>" + htmlEscape(value) + "</strong><span>" + htmlEscape(label) + "</span></div>";
}

std::string button(std::string label, std::string action, std::string cssClass = "")
{
    const std::string classAttr = cssClass.empty() ? "" : " class=\"" + cssClass + "\"";
    return "<button" + classAttr + " data-rr-action=\"" + htmlEscape(action) + "\">" + htmlEscape(label) + "</button>";
}

std::string modalButton(std::string label, std::string modalId, std::string cssClass = "")
{
    const std::string classAttr = cssClass.empty() ? "" : " class=\"" + cssClass + "\"";
    return "<button" + classAttr + " data-ui-modal=\"" + htmlEscape(modalId) + "\">" + htmlEscape(label) + "</button>";
}

std::string modalTemplate(std::string modalId, std::string title, std::string body)
{
    return "<template data-modal=\"" + htmlEscape(modalId) + "\" data-title=\"" + htmlEscape(title) + "\">" + body + "</template>";
}

std::string disabledButton(std::string label)
{
    return "<button disabled>" + htmlEscape(label) + "</button>";
}

std::string warningClass(double value)
{
    if (value >= 0.88) {
        return "critical";
    }
    if (value >= 0.62) {
        return "caution";
    }
    return "nominal";
}

std::string warningButton(std::string label, double value)
{
    return "<button type=\"button\" class=\"warning-button " + warningClass(value) + "\"><strong>" +
        htmlEscape(label) + "</strong><span>" + htmlEscape(percent(value)) + "</span></button>";
}

std::string outcomeLabel(const LaunchOutcome& outcome)
{
    if (outcome.type == LaunchResultType::Destroyed) {
        if (outcome.recoveryMethod == RecoveryMethod::ReturnHome) {
            return "Return Failure";
        }
        return outcome.frontierTransfer ? "Transfer Lost" : "Vehicle Lost";
    }
    if (outcome.recoveryMethod == RecoveryMethod::ManualEject) {
        return "Emergency Eject";
    }
    if (outcome.recoveryMethod == RecoveryMethod::ReturnHome) {
        return outcome.type == LaunchResultType::MissionComplete ? "Profile Returned" : "Early Return";
    }
    if (outcome.frontierTransfer) {
        return outcome.type == LaunchResultType::MissionComplete ? "Transfer Complete" : "Transfer Aborted";
    }
    return outcome.type == LaunchResultType::MissionComplete ? "Data Profile Complete" : "Proving Return";
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
        return module.stats.thrust >= 0.0 ? "Shortens exposure time" : "Reduces engine load";
    case SlotType::Fuel:
        return module.stats.pressure > 0.0 ? "Stabilizes chamber pressure" : "Extends return margin";
    case SlotType::Hull:
        return "Absorbs structural damage";
    case SlotType::Cooling:
        return "Lowers TEMP buildup";
    case SlotType::Sensors:
        return module.stats.pressure > 0.0 ? "Reduces pressure uncertainty" : "Improves warning luck";
    case SlotType::Escape:
        return "Improves crew survival";
    }
    return "Improves mission odds";
}

std::string primaryImpact(const ShipModule& module)
{
    const ModuleStats& stats = module.stats;
    struct Impact {
        double magnitude;
        const char* label;
        double value;
    };
    const Impact impacts[] = {
        {std::abs(stats.thrust), "Speed", stats.thrust},
        {std::abs(stats.fuel), "Fuel", stats.fuel},
        {std::abs(stats.hull), "Hull", stats.hull},
        {std::abs(stats.cooling), "TEMP control", stats.cooling},
        {std::abs(stats.sensors), "Sensors", stats.sensors},
        {std::abs(stats.escape), "Escape", stats.escape},
        {std::abs(stats.pressure), "Pressure control", stats.pressure},
        {std::abs(stats.volatility), "Volatility", stats.volatility},
        {std::abs(stats.payout), "Data payout", stats.payout},
        {std::abs(stats.repair), "Repair cost", stats.repair}
    };

    const Impact* best = &impacts[0];
    for (const Impact& impact : impacts) {
        if (impact.magnitude > best->magnitude) {
            best = &impact;
        }
    }

    std::ostringstream out;
    out << (best->value >= 0.0 ? "+" : "") << std::fixed << std::setprecision(1) << best->value << " " << best->label;
    return out.str();
}

std::string statTag(std::string label, double value)
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
    tags += statTag("SPD", stats.thrust);
    tags += statTag("FUEL", stats.fuel);
    tags += statTag("HULL", stats.hull);
    tags += statTag("TEMP", stats.cooling);
    tags += statTag("WARN", stats.sensors);
    tags += statTag("ESC", stats.escape);
    tags += statTag("PCTRL", stats.pressure);
    tags += statTag("VOL", stats.volatility);
    tags += statTag("PAY", stats.payout);
    tags += statTag("FIX", stats.repair);
    return tags;
}

std::string crewUpgradeStatTags(const CrewUpgrade& upgrade)
{
    const CrewUpgradeStats& stats = upgrade.stats;
    std::string tags;
    tags += statTag("TRAIN", static_cast<double>(stats.trainingGain));
    tags += statTag("SIM STRESS", static_cast<double>(stats.trainingStressRelief));
    tags += statTag("REST", static_cast<double>(stats.restStressBonus));
    tags += statTag("LAUNCH STRESS", static_cast<double>(stats.launchStressRelief));
    tags += statTag("TRAIT", stats.traitModifier * 100.0);
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
        out << button("Install", "rr.buyOffer(" + std::to_string(index) + ")", "ok");
    } else {
        out << disabledButton("Need " + std::to_string(cost) + " credits");
    }
    out << "</div></article>";
    return out.str();
}

std::string crewUpgradeImpact(const CrewUpgrade& upgrade)
{
    const CrewUpgradeStats& stats = upgrade.stats;
    if (stats.trainingGain > 0) {
        return "+" + std::to_string(stats.trainingGain) + " training per simulator burn";
    }
    if (stats.restStressBonus > 0) {
        return "+" + std::to_string(stats.restStressBonus) + " rest recovery";
    }
    if (stats.launchStressRelief > 0) {
        return "-" + std::to_string(stats.launchStressRelief) + " stress after launches";
    }
    if (stats.trainingStressRelief > 0) {
        return "-" + std::to_string(stats.trainingStressRelief) + " simulator stress";
    }
    if (stats.traitModifier > 0.0) {
        return "+" + percent(stats.traitModifier) + " trait modifiers";
    }
    return "Improves crew operations";
}

std::string crewUpgradeCard(const CrewUpgrade& upgrade, int index, double credits)
{
    const int cost = crewUpgradeCost(upgrade);
    const bool affordable = credits >= static_cast<double>(cost);
    std::ostringstream out;
    out << "<article class=\"upgrade-card slot-sensors\">";
    out << "<div class=\"card-topline\"><span>Crew</span><span>" << htmlEscape(toString(upgrade.rarity)) << "</span></div>";
    out << "<div class=\"module-art\"><span>C</span></div>";
    out << "<h3>" << htmlEscape(upgrade.name) << "</h3>";
    out << "<p class=\"module-threat\">" << htmlEscape(upgrade.description) << "</p>";
    out << "<strong class=\"module-impact\">" << htmlEscape(crewUpgradeImpact(upgrade)) << "</strong>";
    out << "<div class=\"stat-grid\">" << crewUpgradeStatTags(upgrade) << "</div>";
    out << "<div class=\"card-footer\"><span>" << cost << " credits</span>";
    if (affordable) {
        out << button("Install", "rr.buyOffer(" + std::to_string(index) + ")", "ok");
    } else {
        out << disabledButton("Need " + std::to_string(cost) + " credits");
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
    out << (available ? button("Assign", action) : disabledButton("Unavailable"));
    out << "</div></article>";
    return out.str();
}

std::string detailRow(std::string label, std::string value)
{
    return "<div class=\"detail-row\"><span>" + htmlEscape(label) + "</span><strong>" + htmlEscape(value) + "</strong></div>";
}

std::string detailHeader(std::string label)
{
    return "<div class=\"detail-section\">" + htmlEscape(label) + "</div>";
}

std::string crewStressSummary(const Astronaut* astronaut)
{
    if (astronaut == nullptr) {
        return "No active crew";
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
    const bool hullLaunchBlocked = state.run.shipDamage >= 100;
    const bool crewLaunchBlocked = astronaut == nullptr;

    std::ostringstream out;
    out << "<div class=\"panel-head\"><div><h1>Rocket Rogue</h1></div>"
        << modalButton("Settings", "settings", "ghost") << "</div>";
    out << "<p class=\"status\">" << htmlEscape(state.statusLine) << "</p>";

    std::ostringstream settingsBody;
    settingsBody << "<div class=\"detail-stack\">";
    settingsBody << detailRow("Keyboard", "R return home, E eject");
    settingsBody << detailRow("Save", "Browser localStorage");
    settingsBody << detailRow("Build", "WebGL2 / Emscripten POC");
    settingsBody << "</div><div class=\"modal-actions\">";
    settingsBody << button("Reset save", "rr.resetSave()", "danger");
    settingsBody << "</div>";

    out << "<div class=\"metric-grid\">";
    out << metric("Mission credits", money(state.run.credits));
    out << metric("Hull damage", std::to_string(state.run.shipDamage) + "%");
    out << metric(transferLaunch ? "Transfer target" : "Current frontier", displayDestination->name);
    out << metric(
        transferLaunch ? "Required burn" : "Flight data",
        transferLaunch ? multiplier(displayDestination->targetMultiplier) :
                         (requiredReadiness == 0 ? "Complete" : std::to_string(state.run.frontierReadiness) + "/" + std::to_string(requiredReadiness)));
    out << metric("Mission difficulty", signedPercent(state.screen == Screen::Launch ? context.flightModel.pressureModifier : missionPressureModifier(state, catalog, *displayDestination)));
    out << metric("Crew stress", crewStressSummary(astronaut));
    out << "</div>";

    if (state.screen == Screen::Launch) {
        const double displayedMultiplier = context.returningHome
            ? returnTelemetryMultiplier(context.returnBurnMultiplier, context.flightModel.crashMultiplier, context.returnElapsed, context.returnDuration)
            : context.currentMultiplier;
        const TelemetryEvent event = telemetryAt(context.flightModel, displayedMultiplier);
        const double recoveryRisk = returnHomeRisk(context.flightModel, catalog, state, displayedMultiplier);
        const double returnProgress = smoothStep(context.returnElapsed / std::max(0.1, context.returnDuration));

        out << "<h2>" << (context.returningHome ? "Return burn" : (transferLaunch ? "Transfer attempt" : "Proving flight")) << "</h2>";
        out << "<div class=\"metric-grid\">";
        out << metric("Burn depth", multiplier(displayedMultiplier));
        out << metric(context.returningHome ? "Return progress" : (transferLaunch ? "Required burn" : "Data goal"),
            context.returningHome ? percent(returnProgress) : multiplier(displayDestination->targetMultiplier));
        out << metric("Return risk", percent(recoveryRisk));
        out << "</div>";

        out << "<h2>Telemetry</h2>";
        out << "<div class=\"warning-grid\">";
        out << warningButton("TEMP", event.heat);
        out << warningButton("PRESS", event.pressure);
        out << warningButton("VIB", event.vibration);
        out << warningButton("NAV", event.guidance);
        out << warningButton("MIX", event.fuelMix);
        out << warningButton("ABORT", event.abortRisk);
        out << "</div>";
        out << "<div class=\"utility-row\">" << modalButton("Telemetry details", "telemetry", "ghost") << "</div>";
        out << "<p class=\"status\">" << htmlEscape(event.message) << "</p>";

        out << "<h2>Flight controls</h2>";
        out << "<div class=\"actions primary-actions\">";
        if (context.returningHome) {
            out << "<button disabled>Returning home</button>";
        } else {
            out << button("Return home", "rr.returnHome()", "ok");
        }
        out << button("Eject", "rr.ejectNow()", "danger");
        out << "</div>";

        out << "<div class=\"actions system-actions\">";
        if (context.returningHome) {
            out << "<button disabled>Cut engines</button>";
            out << "<button disabled>Relief valve</button>";
            out << "<button disabled>Jettison cargo</button>";
        } else {
            out << button(context.cutEnginesActive ? "Restore thrust" : "Cut engines", "rr.cutEngines()", "warn");
            out << (context.pressureReliefUsed
                ? (context.pressureReliefFailed
                    ? std::string("<button disabled>Valve failed</button>")
                    : (context.pressureReliefOpen
                        ? button("Close valve", "rr.closeReliefValve()", "warn")
                        : std::string("<button disabled>Valve closed</button>")))
                : button("Relief valve", "rr.pressureRelief()", "warn"));
            out << (context.cargoJettisoned
                ? std::string("<button disabled>Cargo gone</button>")
                : button("Jettison cargo", "rr.jettisonCargo()", "warn"));
        }
        out << "</div>";
        std::ostringstream telemetryBody;
        telemetryBody << "<div class=\"detail-stack\">";
        telemetryBody << detailRow("TEMP", percent(event.heat));
        telemetryBody << detailRow("PRESS", percent(event.pressure));
        telemetryBody << detailRow("VIB", percent(event.vibration));
        telemetryBody << detailRow("NAV", percent(event.guidance));
        telemetryBody << detailRow("MIX", percent(event.fuelMix));
        telemetryBody << detailRow("ABORT", percent(event.abortRisk));
        telemetryBody << detailRow("Return risk", percent(recoveryRisk));
        telemetryBody << detailRow("Mission difficulty", signedPercent(context.flightModel.pressureModifier));
        telemetryBody << "</div>";
        out << modalTemplate("telemetry", "Telemetry Details", telemetryBody.str());
        out << modalTemplate("settings", "Settings", settingsBody.str());
        return out.str();
    }

    if (state.screen == Screen::Results) {
        out << "<h2>Result</h2>";
        out << "<div class=\"metric-grid\">";
        out << metric("Outcome", outcomeLabel(state.lastOutcome));
        out << metric("Recovery", std::string(toString(state.lastOutcome.recoveryMethod)));
        out << metric("Burn depth", multiplier(state.lastOutcome.ejectMultiplier));
        out << metric("Failure point", multiplier(state.lastOutcome.crashMultiplier));
        out << metric("Peak warning", percent(state.lastOutcome.peakWarning));
        out << metric("Peak abort", percent(state.lastOutcome.peakAbortRisk));
        out << metric("Credit delta", signedMoney(state.lastOutcome.payout - state.lastOutcome.recoveryCost));
        out << "</div>";
        if (!state.lastOutcome.moduleDestroyedId.empty()) {
            out << "<p class=\"status\">Lost module: " << htmlEscape(state.lastOutcome.moduleDestroyedId) << "</p>";
        }
        if (state.lastOutcome.crewKilled) {
            out << "<p class=\"status\">Crew loss recorded in the memorial ledger.</p>";
        } else if (state.lastOutcome.crewInjured) {
            out << "<p class=\"status\">Crew injured. Rest before you ask for another miracle.</p>";
        }
        out << "<div class=\"actions\">";
        out << button(state.lastOutcome.type == LaunchResultType::Destroyed ? "Start replacement refit" : "Review refit options", "rr.next()", "ok");
        out << "</div>";
        out << modalTemplate("settings", "Settings", settingsBody.str());
        return out.str();
    }

    if (state.screen == Screen::Upgrade) {
        out << "<h2>Refit window</h2>";
        out << "<p>Choose one ship or crew upgrade. The install crew can only complete one refit before the next launch cycle.</p>";
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
            ? button("Reroll offers (" + money(rerollCost) + " credits)", "rr.rerollOffers()", "warn")
            : disabledButton("Need " + money(rerollCost) + " credits"));
        out << button("Skip refit", "rr.next()");
        out << "</div>";
        out << modalTemplate("settings", "Settings", settingsBody.str());
        return out.str();
    }

    std::ostringstream shipBody;
    shipBody << "<div class=\"detail-stack\">";
    shipBody << detailRow("Thrust", money(stats.thrust));
    shipBody << detailRow("Fuel", money(stats.fuel));
    shipBody << detailRow("Hull", money(stats.hull));
    shipBody << detailRow("TEMP control", money(stats.cooling));
    shipBody << detailRow("Sensors", money(stats.sensors));
    shipBody << detailRow("Escape", money(stats.escape));
    shipBody << detailRow("Pressure control", money(stats.pressure));
    shipBody << detailRow("Damage", std::to_string(state.run.shipDamage) + "%");
    shipBody << detailHeader("Equipped Ship Upgrades");
    for (const std::string& moduleId : state.run.equippedModuleIds) {
        if (const ShipModule* module = catalog.findModule(moduleId)) {
            shipBody << detailRow(std::string(toString(module->slot)), module->name + " (" + std::string(toString(module->rarity)) + ")");
        }
    }
    shipBody << detailHeader("Stored Ship Upgrades");
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
        shipBody << detailRow("Inventory", "No spare modules");
    }
    shipBody << "</div>";

    std::ostringstream crewBody;
    crewBody << "<div class=\"detail-stack\">";
    const CrewUpgradeStats crewUpgrades = aggregateCrewUpgradeStats(state, catalog);
    if (astronaut != nullptr) {
        const int stressSteps = crewStressStepCount(astronaut->stress);
        crewBody << detailRow("Active", astronaut->name);
        crewBody << detailRow("Trait", astronaut->trait);
        crewBody << detailRow("Training", std::to_string(astronaut->training) + " (" + std::to_string(effectiveTrainingLevel(*astronaut)) + " effective)");
        crewBody << detailRow("Stress", std::to_string(astronaut->stress) + "% / " + std::to_string(stressSteps) + " steps");
        crewBody << detailRow("Stress effects", "NAV +" + percent(crewNavigationPenaltyFromStress(astronaut->stress)) + ", ABORT " + multiplier(crewAbortRiskMultiplierFromStress(astronaut->stress)));
        crewBody << detailRow("Status", std::string(toString(astronaut->status)));
    } else {
        crewBody << detailRow("Active", "None cleared");
    }
    crewBody << detailHeader("Crew Facilities");
    if (state.run.crewUpgradeIds.empty()) {
        crewBody << detailRow("Facilities", "Baseline training room");
    } else {
        for (const std::string& upgradeId : state.run.crewUpgradeIds) {
            if (const CrewUpgrade* upgrade = catalog.findCrewUpgrade(upgradeId)) {
                crewBody << detailRow(std::string(toString(upgrade->rarity)), upgrade->name);
            }
        }
    }
    crewBody << detailHeader("Facility Effects");
    crewBody << detailRow("Simulator gain", "+" + std::to_string(std::max(1, 1 + crewUpgrades.trainingGain)) + " training");
    crewBody << detailRow("Simulator stress", "+" + std::to_string(std::max(0, 6 - crewUpgrades.trainingStressRelief)) + " stress");
    crewBody << detailRow("Medical rest", "-" + std::to_string(crewRestStressRecovery(state, catalog)) + " stress now");
    crewBody << detailRow("Launch stress", "-" + std::to_string(crewUpgrades.launchStressRelief) + " stress");
    crewBody << detailRow("Trait modifiers", "+" + percent(std::max(0.0, crewUpgrades.traitModifier)));
    crewBody << "</div>";

    std::ostringstream frontierBody;
    frontierBody << "<div class=\"detail-stack\">";
    frontierBody << detailRow("Current", currentFrontier.name);
    frontierBody << detailRow("Flight data", requiredReadiness == 0 ? "Complete" : std::to_string(state.run.frontierReadiness) + "/" + std::to_string(requiredReadiness));
    frontierBody << detailRow("Mission difficulty", signedPercent(missionPressureModifier(state, catalog, currentFrontier)));
    if (next != nullptr) {
        frontierBody << detailRow("Next", next->name);
        frontierBody << detailRow("Transfer burn", multiplier(next->targetMultiplier));
    } else {
        frontierBody << detailRow("Next", "None charted");
    }
    frontierBody << "</div>";

    std::ostringstream launchBlockedBody;
    launchBlockedBody << "<div class=\"detail-stack\">";
    if (hullLaunchBlocked) {
        launchBlockedBody << "<p class=\"status\">Mission control will not clear a vehicle at total hull damage.</p>";
    }
    if (crewLaunchBlocked) {
        launchBlockedBody << "<p class=\"status\">No living astronaut is currently cleared for launch.</p>";
    }
    launchBlockedBody << detailRow("Hull damage", std::to_string(state.run.shipDamage) + "%");
    launchBlockedBody << detailRow("Crew", astronaut == nullptr ? "None cleared" : astronaut->name);
    launchBlockedBody << detailRow("Required action", hullLaunchBlocked ? "Repair vehicle" : "Recruit crew");
    launchBlockedBody << "</div><div class=\"modal-actions actions\">";
    if (hullLaunchBlocked) {
        const double blockedRepairCost = repairShipCost(state);
        launchBlockedBody << (state.run.credits >= blockedRepairCost
            ? button("Assign repair bay", "rr.repairShip()", "ok")
            : disabledButton("Need " + money(blockedRepairCost) + " credits"));
    }
    if (crewLaunchBlocked) {
        launchBlockedBody << button("Recruit crew", "rr.recruitCrew()", "ok");
    }
    launchBlockedBody << "</div>";

    out << "<h2>Hangar Bay</h2>";
    out << "<div class=\"summary-grid\">";
    out << "<article class=\"summary-card\"><span>Ship</span><strong>" << state.run.shipDamage << "% damage</strong>"
        << modalButton("Details", "ship", "ghost") << "</article>";
    out << "<article class=\"summary-card\"><span>Crew</span><strong>"
        << htmlEscape(astronaut == nullptr ? std::string("No pilot") : astronaut->name) << "</strong>"
        << modalButton("Details", "crew", "ghost") << "</article>";
    out << "<article class=\"summary-card\"><span>Frontier</span><strong>" << htmlEscape(currentFrontier.name) << "</strong>"
        << modalButton("Details", "frontier", "ghost") << "</article>";
    out << "</div>";

    out << "<h2>Hangar ops</h2>";
    out << "<div class=\"ops-grid\">";
    const int repairAmount = repairShipAmount(state);
    const double repairCost = repairShipCost(state);
    out << operationCard(
        "Repair bay",
        repairAmount > 0 ? "Restore up to " + std::to_string(repairAmount) + " hull damage. Repeated assignments cost more this expedition." : "No structural work is needed right now.",
        repairAmount > 0 ? money(repairCost) + " credits" : "Ship stable",
        "rr.repairShip()",
        repairAmount > 0 && state.run.credits >= repairCost,
        "repair");
    if (astronaut == nullptr) {
        out << operationCard("Crew intake", "Emergency replacement clears the launch soft lock.", "0 credits", "rr.recruitCrew()", true, "crew");
        out << operationCard("Reserve roster", "Add another qualified astronaut before the next proving run.", "24 credits", "rr.recruitCrew()", state.run.credits >= 24.0, "crew");
    } else {
        out << operationCard(
            "Simulator burn",
            "+" + std::to_string(std::max(1, 1 + crewUpgrades.trainingGain)) + " training, +" + std::to_string(crewTrainingStressGain(state, catalog)) + " stress. Repeated assignments cost more this expedition.",
            money(crewTrainingCost(state, catalog)) + " credits",
            "rr.trainCrew()",
            state.run.credits >= crewTrainingCost(state, catalog) && astronaut->stress + crewTrainingStressGain(state, catalog) <= 100,
            "crew");
        out << operationCard(
            "Medical rest",
            "-" + std::to_string(crewRestStressRecovery(state, catalog)) + " stress at current difficulty. Repeated assignments cost more this expedition.",
            money(crewRestCost(state, catalog)) + " credits",
            "rr.restCrew()",
            state.run.credits >= crewRestCost(state, catalog),
            "crew");
    }
    out << "</div>";

    out << "<div class=\"actions\">";
    out << (hullLaunchBlocked || crewLaunchBlocked
        ? modalButton("Launch proving flight", "launch_blocked", "ok")
        : button("Launch proving flight", "rr.startLaunch()", "ok"));
    if (next != nullptr) {
        if (canCommitToNextFrontier(state, catalog)) {
            out << (hullLaunchBlocked || crewLaunchBlocked
                ? modalButton("Attempt: " + next->name, "launch_blocked", "danger")
                : button("Attempt: " + next->name, "rr.attemptFrontier()", "danger"));
        } else {
            out << "<button disabled>Need flight data</button>";
        }
    }
    out << "</div>";

    std::ostringstream legacyBody;
    legacyBody << "<div class=\"detail-stack\">";
    legacyBody << detailRow("Blueprints", std::to_string(state.meta.blueprintProgress));
    legacyBody << detailRow("Ships lost", std::to_string(state.meta.shipsLost));
    legacyBody << detailRow("Astronauts lost", std::to_string(state.meta.astronautsLost));
    legacyBody << detailRow("Furthest tier", std::to_string(state.meta.furthestTier));
    legacyBody << "</div>";

    out << "<div class=\"utility-row bottom-tools\">";
    out << modalButton("Legacy", "legacy", "ghost");
    out << "</div>";
    out << modalTemplate("ship", "Ship Details", shipBody.str());
    out << modalTemplate("crew", "Crew Details", crewBody.str());
    out << modalTemplate("frontier", "Frontier Details", frontierBody.str());
    out << modalTemplate("launch_blocked", "Launch Hold", launchBlockedBody.str());
    out << modalTemplate("legacy", "Legacy", legacyBody.str());
    out << modalTemplate("settings", "Settings", settingsBody.str());

    return out.str();
}

} // namespace rocket
