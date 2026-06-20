#include "game/GamePanel.h"
#include "core/GameFormat.h"
#include "core/GameMath.h"
#include "core/GameText.h"
#include "core/OutcomePresentation.h"
#include "core/RefitPresentation.h"
#include "core/Telemetry.h"
#include "core/Tuning.h"
#include "core/GameUi.h"

#include <algorithm>
#include <sstream>
#include <vector>

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

std::string metric(std::string_view label, std::string value)
{
    return "<div class=\"metric\"><strong>" + htmlEscape(value) + "</strong><span>" + htmlEscape(label) + "</span></div>";
}

std::string button(std::string_view label, std::string_view action, std::string cssClass = "")
{
    const std::string classAttr = cssClass.empty() ? "" : " class=\"" + cssClass + "\"";
    return "<button" + classAttr + " data-rr-action=\"" + htmlEscape(action) + "\">" + htmlEscape(label) + "</button>";
}

std::string modalButton(std::string_view label, std::string_view modalId, std::string cssClass = "")
{
    const std::string classAttr = cssClass.empty() ? "" : " class=\"" + cssClass + "\"";
    return "<button" + classAttr + " data-ui-modal=\"" + htmlEscape(modalId) + "\">" + htmlEscape(label) + "</button>";
}

std::string modalTemplate(std::string_view modalId, std::string_view title, std::string body)
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
        htmlEscape(label) + "</strong><span>" + htmlEscape(display::percent(value)) + "</span></button>";
}

std::string statChip(const RefitStatChip& chip)
{
    return "<span class=\"stat-chip " + std::string(chip.positive ? "up" : "down") + "\">" +
        htmlEscape(chip.label) + " " + htmlEscape(chip.value) + "</span>";
}

std::string statChipGrid(const std::vector<RefitStatChip>& chips)
{
    std::string tags;
    for (const RefitStatChip& chip : chips) {
        tags += statChip(chip);
    }
    return tags;
}

std::string moduleCard(const ShipModule& module, int index, double credits)
{
    const int cost = moduleOfferCost(module);
    const bool affordable = credits >= static_cast<double>(cost);
    const RefitPresentation presentation = moduleRefitPresentation(module);
    std::ostringstream out;
    out << "<article class=\"upgrade-card slot-" << htmlEscape(presentation.slotClass) << "\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(presentation.category) << "</span><span>"
        << htmlEscape(presentation.rarity) << "</span></div>";
    out << "<div class=\"module-art\"><span>" << htmlEscape(presentation.glyph) << "</span></div>";
    out << "<h3>" << htmlEscape(presentation.title) << "</h3>";
    out << "<p class=\"module-threat\">" << htmlEscape(presentation.detail) << "</p>";
    out << "<strong class=\"module-impact\">" << htmlEscape(presentation.primaryImpact) << "</strong>";
    out << "<div class=\"stat-grid\">" << statChipGrid(presentation.statChips) << "</div>";
    out << "<div class=\"card-footer\"><span>" << htmlEscape(display::credits(cost)) << "</span>";
    if (affordable) {
        out << button(text::buttons::install, ui::actions::buyOffer(index), "ok");
    } else {
        out << disabledButton(text::needCredits(cost));
    }
    out << "</div></article>";
    return out.str();
}

std::string crewUpgradeCard(const CrewUpgrade& upgrade, int index, double credits)
{
    const int cost = crewUpgradeCost(upgrade);
    const bool affordable = credits >= static_cast<double>(cost);
    const RefitPresentation presentation = crewUpgradeRefitPresentation(upgrade);
    std::ostringstream out;
    out << "<article class=\"upgrade-card slot-" << htmlEscape(presentation.slotClass) << "\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(presentation.category) << "</span><span>" << htmlEscape(presentation.rarity) << "</span></div>";
    out << "<div class=\"module-art\"><span>" << htmlEscape(presentation.glyph) << "</span></div>";
    out << "<h3>" << htmlEscape(presentation.title) << "</h3>";
    out << "<p class=\"module-threat\">" << htmlEscape(presentation.detail) << "</p>";
    out << "<strong class=\"module-impact\">" << htmlEscape(presentation.primaryImpact) << "</strong>";
    out << "<div class=\"stat-grid\">" << statChipGrid(presentation.statChips) << "</div>";
    out << "<div class=\"card-footer\"><span>" << htmlEscape(display::credits(cost)) << "</span>";
    if (affordable) {
        out << button(text::buttons::install, ui::actions::buyOffer(index), "ok");
    } else {
        out << disabledButton(text::needCredits(cost));
    }
    out << "</div></article>";
    return out.str();
}

std::string operationCard(std::string title, std::string detail, std::string cost, std::string_view action, bool available, std::string cssClass = "")
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
    return display::wholePercent(astronaut->stress);
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
        << modalButton(text::buttons::settings, ui::modals::settings, "ghost") << "</div>";
    out << "<p class=\"status\">" << htmlEscape(state.statusLine) << "</p>";

    std::ostringstream settingsBody;
    settingsBody << "<div class=\"detail-stack\">";
    settingsBody << detailRow(text::panel::details::keyboard, text::panel::details::keyboardValue);
    settingsBody << detailRow(text::panel::details::save, text::panel::details::saveValue);
    settingsBody << detailRow(text::panel::details::build, text::panel::details::buildValue);
    settingsBody << "</div><div class=\"modal-actions\">";
    settingsBody << button(text::buttons::resetSave, ui::actions::resetSave, "danger");
    settingsBody << "</div>";

    out << "<div class=\"metric-grid\">";
    out << metric(text::labels::missionCredits, display::money(state.run.credits));
    out << metric(text::labels::hullDamage, display::wholePercent(state.run.shipDamage));
    out << metric(transferLaunch ? text::labels::transferTarget : text::labels::currentFrontier, displayDestination->name);
    out << metric(
        transferLaunch ? std::string_view(text::labels::requiredBurn) : std::string_view(text::labels::flightData),
        transferLaunch ? display::multiplier(displayDestination->targetMultiplier) :
                         (requiredReadiness == 0 ? std::string(text::panel::complete) : display::fraction(state.run.frontierReadiness, requiredReadiness)));
    out << metric(text::labels::missionDifficulty, display::signedPercent(state.screen == Screen::Launch ? context.flightModel.pressureModifier : missionPressureModifier(state, catalog, *displayDestination)));
    out << metric(text::labels::crewStress, crewStressSummary(astronaut));
    out << "</div>";

    if (state.screen == Screen::Launch) {
        const double displayedMultiplier = context.flightActions.returningHome
            ? returnTelemetryMultiplier(context.returnBurnMultiplier, context.flightModel.crashMultiplier, context.returnElapsed, context.returnDuration)
            : context.currentMultiplier;
        const TelemetryEvent event = telemetryAt(context.flightModel, displayedMultiplier);
        const double recoveryRisk = returnHomeRisk(context.flightModel, catalog, state, displayedMultiplier);
        const double returnProgress = math::smoothStep(context.returnElapsed / std::max(0.1, context.returnDuration));

        out << "<h2>" << htmlEscape(context.flightActions.returningHome ? text::panel::sections::returnBurn : (transferLaunch ? text::panel::sections::transferAttempt : text::panel::sections::provingFlight)) << "</h2>";
        out << "<div class=\"metric-grid\">";
        out << metric(text::labels::burnDepth, display::multiplier(displayedMultiplier));
        out << metric(context.flightActions.returningHome ? text::labels::returnProgress : (transferLaunch ? text::labels::requiredBurn : text::labels::dataGoal),
            context.flightActions.returningHome ? display::percent(returnProgress) : display::multiplier(displayDestination->targetMultiplier));
        out << metric(text::labels::returnRisk, display::percent(recoveryRisk));
        out << "</div>";

        out << "<h2>" << htmlEscape(text::panel::sections::telemetry) << "</h2>";
        out << "<div class=\"warning-grid\">";
        for (const TelemetryChannelSample& sample : telemetrySamples(event)) {
            out << warningButton(sample.label, sample.value);
        }
        out << "</div>";
        out << "<div class=\"utility-row\">" << modalButton(text::panel::modals::telemetryDetails, ui::modals::telemetry, "ghost") << "</div>";
        out << "<p class=\"status\">" << htmlEscape(event.message) << "</p>";

        out << "<h2>" << htmlEscape(text::panel::sections::flightControls) << "</h2>";
        out << "<div class=\"actions primary-actions\">";
        if (context.flightActions.returningHome) {
            out << disabledButton(text::buttons::returningHome);
        } else {
            out << button(text::buttons::returnHome, ui::actions::returnHome, "ok");
        }
        out << button(text::buttons::eject, ui::actions::ejectNow, "danger");
        out << "</div>";

        out << "<div class=\"actions system-actions\">";
        if (context.flightActions.returningHome) {
            out << disabledButton(text::buttons::cutEngines);
            out << disabledButton(text::buttons::reliefValve);
            out << disabledButton(text::buttons::jettisonCargo);
        } else {
            out << button(context.flightActions.cutEnginesActive ? text::buttons::restoreThrust : text::buttons::cutEngines, ui::actions::cutEngines, "warn");
            out << (context.pressureReliefUsed
                ? (context.flightActions.pressureReliefFailed
                    ? disabledButton(text::buttons::reliefValveFailed)
                    : (context.flightActions.pressureReliefOpen
                        ? button(text::buttons::closeValve, ui::actions::closeReliefValve, "warn")
                        : disabledButton(text::buttons::valveClosed)))
                : button(text::buttons::reliefValve, ui::actions::pressureRelief, "warn"));
            out << (context.flightActions.cargoJettisoned
                ? disabledButton(text::buttons::cargoGone)
                : button(text::buttons::jettisonCargo, ui::actions::jettisonCargo, "warn"));
        }
        out << "</div>";
        std::ostringstream telemetryBody;
        telemetryBody << "<div class=\"detail-stack\">";
        for (const TelemetryChannelSample& sample : telemetrySamples(event)) {
            telemetryBody << detailRow(std::string(sample.label), display::percent(sample.value));
        }
        telemetryBody << detailRow(std::string(text::labels::returnRisk), display::percent(recoveryRisk));
        telemetryBody << detailRow(std::string(text::labels::missionDifficulty), display::signedPercent(context.flightModel.pressureModifier));
        telemetryBody << "</div>";
        out << modalTemplate(ui::modals::telemetry, text::panel::modals::telemetryDetails, telemetryBody.str());
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        return out.str();
    }

    if (state.screen == Screen::Results) {
        const LaunchOutcomePresentation presentation = launchOutcomePresentation(state.lastOutcome);
        out << "<h2>" << htmlEscape(text::panel::sections::result) << "</h2>";
        out << "<div class=\"metric-grid\">";
        out << metric(text::labels::outcome, std::string(presentation.label));
        out << metric(text::labels::recovery, std::string(toString(state.lastOutcome.recoveryMethod)));
        out << metric(text::labels::burnDepth, display::multiplier(state.lastOutcome.ejectMultiplier));
        out << metric(text::labels::failurePoint, display::multiplier(state.lastOutcome.crashMultiplier));
        out << metric(text::labels::peakWarning, display::percent(state.lastOutcome.peakWarning));
        out << metric(text::labels::peakAbort, display::percent(state.lastOutcome.peakAbortRisk));
        out << metric(text::labels::creditDelta, display::signedMoney(state.lastOutcome.payout - state.lastOutcome.recoveryCost));
        out << "</div>";
        for (const std::string& note : presentation.notes) {
            out << "<p class=\"status\">" << htmlEscape(note) << "</p>";
        }
        out << "<div class=\"actions\">";
        out << button(presentation.nextActionLabel, ui::actions::next, "ok");
        out << "</div>";
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
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
            ? button(text::panel::rerollOffers(display::money(rerollCost)), ui::actions::rerollOffers, "warn")
            : disabledButton(display::needCredits(rerollCost)));
        out << button(text::buttons::skipRefit, ui::actions::next);
        out << "</div>";
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        return out.str();
    }

    std::ostringstream shipBody;
    shipBody << "<div class=\"detail-stack\">";
    for (const ModuleStatDisplay& display : moduleStatDisplays(stats)) {
        if (display.showInShipDetails) {
            shipBody << detailRow(std::string(display.detailLabel), display::money(display.value));
        }
    }
    shipBody << detailRow(std::string(text::moduleStats::damage), display::wholePercent(state.run.shipDamage));
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
    const HangarOperationPreview hangarOps = hangarOperationPreview(state, catalog);
    if (astronaut != nullptr) {
        const int stressSteps = crewStressStepCount(astronaut->stress);
        crewBody << detailRow(text::panel::details::active, astronaut->name);
        crewBody << detailRow(text::panel::details::trait, astronaut->trait);
        crewBody << detailRow(text::panel::details::training, display::trainingWithEffective(astronaut->training, effectiveTrainingLevel(*astronaut)));
        crewBody << detailRow(text::panel::details::stress, display::stressWithSteps(astronaut->stress, stressSteps));
        crewBody << detailRow(text::panel::details::stressEffects, display::crewStressEffects(crewNavigationPenaltyFromStress(astronaut->stress), crewAbortRiskMultiplierFromStress(astronaut->stress)));
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
    crewBody << detailRow(text::panel::details::simulatorGain, "+" + std::to_string(hangarOps.trainingGain) + " training");
    crewBody << detailRow(text::panel::details::simulatorStress, "+" + std::to_string(hangarOps.trainingStressGain) + " stress");
    crewBody << detailRow(text::panel::details::medicalRest, "-" + std::to_string(hangarOps.restStressRecovery) + " stress now");
    crewBody << detailRow(text::panel::details::launchStress, "-" + std::to_string(crewUpgrades.launchStressRelief) + " stress");
    crewBody << detailRow(text::panel::details::traitModifiers, display::signedPercent(std::max(0.0, crewUpgrades.traitModifier)));
    crewBody << "</div>";

    std::ostringstream frontierBody;
    frontierBody << "<div class=\"detail-stack\">";
    frontierBody << detailRow(text::panel::details::current, currentFrontier.name);
    frontierBody << detailRow(text::labels::flightData, requiredReadiness == 0 ? std::string(text::panel::complete) : display::fraction(state.run.frontierReadiness, requiredReadiness));
    frontierBody << detailRow(text::labels::missionDifficulty, display::signedPercent(missionPressureModifier(state, catalog, currentFrontier)));
    if (next != nullptr) {
        frontierBody << detailRow(text::panel::details::next, next->name);
        frontierBody << detailRow(text::panel::details::transferBurn, display::multiplier(next->targetMultiplier));
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
    launchBlockedBody << detailRow(text::labels::hullDamage, display::wholePercent(state.run.shipDamage));
    launchBlockedBody << detailRow(text::panel::details::crew, astronaut == nullptr ? std::string(text::panel::noneCleared) : astronaut->name);
    launchBlockedBody << detailRow(text::panel::details::requiredAction, hullLaunchBlocked ? text::panel::details::repairVehicle : text::panel::details::recruitCrew);
    launchBlockedBody << "</div><div class=\"modal-actions actions\">";
    if (hullLaunchBlocked) {
        launchBlockedBody << (hangarOps.repairAvailable
            ? button(text::buttons::assignRepairBay, ui::actions::repairShip, "ok")
            : disabledButton(display::needCredits(hangarOps.repairCost)));
    }
    if (crewLaunchBlocked) {
        launchBlockedBody << button(text::buttons::recruitCrew, ui::actions::recruitCrew, "ok");
    }
    launchBlockedBody << "</div>";

    out << "<h2>" << htmlEscape(text::panel::sections::hangarBay) << "</h2>";
    out << "<div class=\"summary-grid\">";
    out << "<article class=\"summary-card\"><span>" << htmlEscape(text::panel::details::ship) << "</span><strong>" << htmlEscape(display::damage(state.run.shipDamage)) << "</strong>"
        << modalButton(text::buttons::details, ui::modals::ship, "ghost") << "</article>";
    out << "<article class=\"summary-card\"><span>" << htmlEscape(text::panel::details::crew) << "</span><strong>"
        << htmlEscape(astronaut == nullptr ? std::string(text::panel::noPilot) : astronaut->name) << "</strong>"
        << modalButton(text::buttons::details, ui::modals::crew, "ghost") << "</article>";
    out << "<article class=\"summary-card\"><span>" << htmlEscape(text::panel::details::frontier) << "</span><strong>" << htmlEscape(currentFrontier.name) << "</strong>"
        << modalButton(text::buttons::details, ui::modals::frontier, "ghost") << "</article>";
    out << "</div>";

    out << "<h2>" << htmlEscape(text::panel::sections::hangarOps) << "</h2>";
    out << "<div class=\"ops-grid\">";
    out << operationCard(
        std::string(text::panel::ops::repairBay),
        hangarOps.repairAmount > 0 ? text::panel::repairDetail(hangarOps.repairAmount) : std::string(text::panel::messages::noStructuralWork),
        hangarOps.repairAmount > 0 ? display::credits(hangarOps.repairCost) : std::string(text::panel::shipStable),
        ui::actions::repairShip,
        hangarOps.repairAvailable,
        "repair");
    if (astronaut == nullptr) {
        out << operationCard(std::string(text::panel::ops::crewIntake), std::string(text::panel::messages::emergencyReplacement), display::credits(hangarOps.recruitCost), ui::actions::recruitCrew, hangarOps.recruitAvailable, "crew");
        out << operationCard(std::string(text::panel::ops::reserveRoster), std::string(text::panel::messages::reserveRoster), display::credits(tuning::hangar::recruitCost), ui::actions::recruitCrew, state.run.credits >= tuning::hangar::recruitCost, "crew");
    } else {
        out << operationCard(
            std::string(text::panel::ops::simulatorBurn),
            text::panel::simulatorDetail(hangarOps.trainingGain, hangarOps.trainingStressGain),
            display::credits(hangarOps.trainingCost),
            ui::actions::trainCrew,
            hangarOps.trainingAvailable,
            "crew");
        out << operationCard(
            std::string(text::panel::ops::medicalRest),
            text::panel::restDetail(hangarOps.restStressRecovery),
            display::credits(hangarOps.restCost),
            ui::actions::restCrew,
            hangarOps.restAvailable,
            "crew");
    }
    out << "</div>";

    out << "<div class=\"actions\">";
    out << (hullLaunchBlocked || crewLaunchBlocked
        ? modalButton(text::buttons::launchProvingFlight, ui::modals::launchBlocked, "ok")
        : button(text::buttons::launchProvingFlight, ui::actions::startLaunch, "ok"));
    if (next != nullptr) {
        if (canCommitToNextFrontier(state, catalog)) {
            out << (hullLaunchBlocked || crewLaunchBlocked
                ? modalButton(text::panel::attemptFrontier(next->name), ui::modals::launchBlocked, "danger")
                : button(text::panel::attemptFrontier(next->name), ui::actions::attemptFrontier, "danger"));
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
    out << modalButton(text::buttons::legacy, ui::modals::legacy, "ghost");
    out << "</div>";
    out << modalTemplate(ui::modals::ship, text::panel::modals::shipDetails, shipBody.str());
    out << modalTemplate(ui::modals::crew, text::panel::modals::crewDetails, crewBody.str());
    out << modalTemplate(ui::modals::frontier, text::panel::modals::frontierDetails, frontierBody.str());
    out << modalTemplate(ui::modals::launchBlocked, text::panel::modals::launchHold, launchBlockedBody.str());
    out << modalTemplate(ui::modals::legacy, text::panel::modals::legacy, legacyBody.str());
    out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());

    return out.str();
}

} // namespace rocket
