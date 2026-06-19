#include "game/RocketGameApp.h"

#include "core/SaveData.h"
#include "platform/WebSaveStore.h"

#include <algorithm>
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

std::string metric(std::string label, std::string value)
{
    return "<div class=\"metric\"><strong>" + htmlEscape(value) + "</strong><span>" + htmlEscape(label) + "</span></div>";
}

std::string button(std::string label, std::string action, std::string cssClass = "")
{
    const std::string classAttr = cssClass.empty() ? "" : " class=\"" + cssClass + "\"";
    return "<button" + classAttr + " data-rr-action=\"" + htmlEscape(action) + "\">" + htmlEscape(label) + "</button>";
}

std::string percent(double value)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(0) << std::clamp(value, 0.0, 1.0) * 100.0 << "%";
    return out.str();
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

double returnTelemetryMultiplier(double commitMultiplier, double crashMultiplier, double returnElapsed, double returnDuration)
{
    const double progress = std::clamp(returnElapsed / std::max(0.1, returnDuration), 0.0, 1.0);
    const double shaped = progress * progress * (3.0 - 2.0 * progress);
    const double headroom = std::max(0.04, crashMultiplier - commitMultiplier);
    const double overshoot = std::min(headroom * 0.22, 0.18 + headroom * 0.10);
    const double bump = std::sin(shaped * 3.1415926535) * overshoot;
    const double settle = shaped * std::min(headroom * 0.08, 0.06);
    return std::min(crashMultiplier - 0.02, commitMultiplier + bump - settle);
}

int offerCost(const ShipModule& module)
{
    switch (module.rarity) {
    case Rarity::Common:
        return 28;
    case Rarity::Uncommon:
        return 48;
    case Rarity::Rare:
        return 74;
    case Rarity::Prototype:
        return 105;
    }
    return 35;
}

double smoothStep(double value)
{
    const double t = std::clamp(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

PreparedLaunch RocketGameApp::currentFlightModel() const
{
    PreparedLaunch launch = activeLaunch_;
    if (!cutEnginesActive_ || returningHome_) {
        return launch;
    }

    launch.throttleFactor = 0.58;
    launch.cutHeatRelief = 0.18;
    launch.cutVibrationRelief = 0.14;
    launch.cutGuidancePenalty = 0.22;
    return launch;
}

bool RocketGameApp::initialize()
{
    catalog_ = createDefaultContent();
    state_ = createNewGame(catalog_, 0x524F434B45544ULL);
    rng_ = Random(state_.seed);

    if (const auto saveData = deserializeSaveData(loadBrowserSave())) {
        restoreSaveData(state_, catalog_, *saveData);
        rng_ = Random(saveData->seed + 0xA51CE5ULL + static_cast<std::uint64_t>(saveData->blueprintProgress));
        state_.statusLine = "Save data restored from local mission control.";
    }

    generateModuleOffers(state_, catalog_, rng_);
    syncLaunchConfig(state_, catalog_);

    if (!renderer_.initialize()) {
        state_.statusLine = "WebGL2 failed to initialize.";
    }

    refreshPanel();
    return true;
}

void RocketGameApp::tick(double deltaSeconds)
{
    if (state_.screen == Screen::Launch) {
        const PreparedLaunch flightModel = currentFlightModel();
        const Destination* activeDestination = catalog_.findDestination(activeLaunch_.config.destinationId);
        const Destination& destination = activeDestination == nullptr ? currentDestination(state_, catalog_) : *activeDestination;

        if (returningHome_) {
            returnElapsed_ += std::clamp(deltaSeconds, 0.0, 0.08);
            const double returnProgress = smoothStep(returnElapsed_ / std::max(0.1, returnDuration_));
            const double returnTelemetry = returnTelemetryMultiplier(
                returnBurnMultiplier_,
                flightModel.crashMultiplier,
                returnElapsed_,
                returnDuration_);
            if (returnProgress >= 1.0) {
                completeLaunch(returnTelemetry, RecoveryMethod::ReturnHome);
            } else {
                const TelemetryEvent event = telemetryAt(flightModel, returnTelemetry);
                if (event.warning > 0.78 || event.heat > 0.88) {
                    state_.statusLine = event.message + ". The return burn is still biting.";
                } else if (returnProgress < 0.28) {
                    state_.statusLine = "Return burn committed. Rotating ship for retrograde flight.";
                } else {
                    state_.statusLine = "Return burn underway. Systems are easing, but this is not free.";
                }
            }
        } else {
            launchElapsed_ += std::clamp(deltaSeconds, 0.0, 0.08);
            const double thrust = std::max(0.4, flightModel.stats.thrust);
            const double cruiseRate = 0.016 + thrust * 0.0017 + static_cast<double>(destination.tier) * 0.0008;
            const double acceleration = (0.00026 + destination.hazard * 0.00008) * flightModel.throttleFactor;
            currentMultiplier_ += std::clamp(deltaSeconds, 0.0, 0.08) * cruiseRate * flightModel.throttleFactor + acceleration;

            if (currentMultiplier_ >= flightModel.crashMultiplier) {
                completeLaunch(flightModel.crashMultiplier, RecoveryMethod::None);
            } else if (activeLaunch_.config.frontierTransfer && currentMultiplier_ >= destination.targetMultiplier) {
                completeLaunch(destination.targetMultiplier, RecoveryMethod::TransferArrival);
            } else {
                const TelemetryEvent event = telemetryAt(flightModel, currentMultiplier_);
                if (event.warning > 0.88) {
                    state_.statusLine = event.message + ". Decide now: return or eject.";
                } else if (event.warning > 0.62 || event.heat > 0.82) {
                    state_.statusLine = event.message + ".";
                } else {
                    const bool pastDataGoal = !activeLaunch_.config.frontierTransfer && currentMultiplier_ >= destination.targetMultiplier;
                    if (cutEnginesActive_) {
                        state_.statusLine = pastDataGoal
                            ? "Engines cut. Thermal load is dropping, but nav drift is growing."
                            : "Engines cut. Cooler burn, less vibration, slower climb, shakier tracking.";
                    } else {
                        state_.statusLine = activeLaunch_.config.frontierTransfer
                            ? "Transfer burn stable. Survive to the required burn or abort."
                            : (pastDataGoal ? "Data goal reached. Return home now, or overburn for extra telemetry." : "Proving burn stable. Push for more data or return home.");
                    }
                }
            }
        }

        panelDirty_ = true;
    } else if (state_.screen == Screen::Results) {
        resultElapsed_ += std::clamp(deltaSeconds, 0.0, 0.08);
    }

    if (panelDirty_) {
        refreshPanel();
    }
}

void RocketGameApp::render()
{
    renderer_.render(snapshot());
}

void RocketGameApp::startLaunch()
{
    if (state_.screen != Screen::Hangar || !state_.run.active) {
        return;
    }

    if (state_.run.shipDamage >= 100) {
        state_.statusLine = "That vehicle is less rocket than cautionary sculpture.";
        panelDirty_ = true;
        return;
    }

    if (activeAstronaut(state_) == nullptr) {
        state_.statusLine = "No living astronaut is cleared for launch.";
        panelDirty_ = true;
        return;
    }

    syncLaunchConfig(state_, catalog_);
    state_.launchConfig.frontierTransfer = false;
    state_.launchConfig.destinationId = currentDestination(state_, catalog_).id;
    activeLaunch_ = prepareLaunch(state_, catalog_, rng_);
    currentMultiplier_ = 1.0;
    launchElapsed_ = 0.0;
    returningHome_ = false;
    cutEnginesActive_ = false;
    returnElapsed_ = 0.0;
    resultUsesTravelProgress_ = false;
    resultElapsed_ = 0.0;
    state_.screen = Screen::Launch;
    state_.statusLine = "Proving burn underway. Return home to bank data; eject only when the vehicle leaves you no choice.";
    panelDirty_ = true;
}

void RocketGameApp::ejectNow()
{
    if (state_.screen != Screen::Launch) {
        return;
    }
    const double liveMultiplier = returningHome_
        ? returnTelemetryMultiplier(returnBurnMultiplier_, currentFlightModel().crashMultiplier, returnElapsed_, returnDuration_)
        : currentMultiplier_;
    completeLaunch(liveMultiplier, RecoveryMethod::ManualEject);
}

void RocketGameApp::returnHome()
{
    if (state_.screen != Screen::Launch || returningHome_) {
        return;
    }

    const Destination* activeDestination = catalog_.findDestination(activeLaunch_.config.destinationId);
    const Destination& destination = activeDestination == nullptr ? currentDestination(state_, catalog_) : *activeDestination;
    returnBurnMultiplier_ = currentMultiplier_;
    returnStartTravelProgress_ = std::clamp(
        (currentMultiplier_ - 1.0) / std::max(0.1, destination.targetMultiplier - 1.0),
        0.0,
        1.42);
    returnElapsed_ = 0.0;
    returnDuration_ = 2.1 + returnStartTravelProgress_ * 1.4;
    returningHome_ = true;
    cutEnginesActive_ = false;
    state_.statusLine = "Return burn committed. Rotating ship for retrograde flight.";
    panelDirty_ = true;
}

void RocketGameApp::cutEngines()
{
    if (state_.screen != Screen::Launch || returningHome_) {
        return;
    }

    cutEnginesActive_ = !cutEnginesActive_;
    state_.statusLine = cutEnginesActive_
        ? "Engine cut confirmed. Ship is running cooler, but guidance drift is widening."
        : "Thrust restored. Burn is climbing again, and so are the hot systems.";
    panelDirty_ = true;
}

void RocketGameApp::next()
{
    if (state_.screen == Screen::Results) {
        if (!state_.run.active || state_.lastOutcome.type == LaunchResultType::Destroyed) {
            startNewExpedition(state_, catalog_);
        }
        state_.screen = Screen::Hangar;
        generateModuleOffers(state_, catalog_, rng_);
        syncLaunchConfig(state_, catalog_);
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::adjustTarget(int deltaSteps)
{
    (void)deltaSteps;
    if (state_.screen != Screen::Hangar) {
        return;
    }

    state_.statusLine = "No planned return setting. Fly the burn, then choose return home or eject.";
    panelDirty_ = true;
}

void RocketGameApp::attemptFrontierTransfer()
{
    if (state_.screen != Screen::Hangar) {
        return;
    }

    if (!canCommitToNextFrontier(state_, catalog_)) {
        const Destination* next = nextDestination(state_, catalog_);
        state_.statusLine = next == nullptr ? "No farther frontier is charted in this proof of concept." : "More proving data is needed before the transfer attempt.";
        panelDirty_ = true;
        return;
    }

    const Destination* next = nextDestination(state_, catalog_);
    if (next == nullptr) {
        state_.statusLine = "No farther frontier is charted in this proof of concept.";
        panelDirty_ = true;
        return;
    }

    state_.launchConfig.frontierTransfer = true;
    state_.launchConfig.destinationId = next->id;
    state_.launchConfig.targetEjectMultiplier = next->targetMultiplier;
    activeLaunch_ = prepareLaunch(state_, catalog_, rng_);
    currentMultiplier_ = 1.0;
    launchElapsed_ = 0.0;
    returningHome_ = false;
    cutEnginesActive_ = false;
    returnElapsed_ = 0.0;
    resultUsesTravelProgress_ = false;
    resultElapsed_ = 0.0;
    state_.screen = Screen::Launch;
    state_.statusLine = "Transfer attempt committed. Survive to the required burn, or abort before the ship decides for you.";
    panelDirty_ = true;
}

void RocketGameApp::buyOffer(int index)
{
    if (state_.screen != Screen::Hangar) {
        return;
    }
    if (rocket::buyOffer(state_, catalog_, index)) {
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::repairShip()
{
    if (rocket::repairShip(state_)) {
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::recruitCrew()
{
    if (rocket::recruitCrew(state_, catalog_)) {
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::trainCrew()
{
    if (rocket::trainCrew(state_)) {
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::restCrew()
{
    if (rocket::restCrew(state_)) {
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::resetSave()
{
    clearBrowserSave();
    state_ = createNewGame(catalog_, 0x524F434B45544ULL);
    rng_ = Random(state_.seed);
    generateModuleOffers(state_, catalog_, rng_);
    returningHome_ = false;
    cutEnginesActive_ = false;
    returnElapsed_ = 0.0;
    resultUsesTravelProgress_ = false;
    resultElapsed_ = 0.0;
    panelDirty_ = true;
}

void RocketGameApp::completeLaunch(double burnMultiplier, RecoveryMethod method)
{
    const PreparedLaunch flightModel = currentFlightModel();
    const bool wasReturningHome = returningHome_;
    double frozenTravelProgress = std::clamp(
        (burnMultiplier - 1.0) / std::max(0.1, currentDestination(state_, catalog_).targetMultiplier - 1.0),
        0.0,
        1.42);
    if (wasReturningHome) {
        const double returnProgress = smoothStep(returnElapsed_ / std::max(0.1, returnDuration_));
        frozenTravelProgress = std::clamp(returnStartTravelProgress_ * (1.0 - returnProgress), 0.0, 1.42);
    } else if (const Destination* activeDestination = catalog_.findDestination(activeLaunch_.config.destinationId)) {
        frozenTravelProgress = std::clamp(
            (burnMultiplier - 1.0) / std::max(0.1, activeDestination->targetMultiplier - 1.0),
            0.0,
            1.42);
    }

    LaunchOutcome outcome = resolveLaunch(flightModel, catalog_, state_, burnMultiplier, method, rng_);
    applyLaunchOutcome(state_, catalog_, outcome);
    state_.screen = Screen::Results;
    currentMultiplier_ = outcome.ejectMultiplier;
    resultUsesTravelProgress_ = wasReturningHome;
    resultTravelProgress_ = frozenTravelProgress;
    returningHome_ = false;
    cutEnginesActive_ = false;
    returnElapsed_ = 0.0;
    resultElapsed_ = 0.0;
    save();
    panelDirty_ = true;
}

void RocketGameApp::save()
{
    storeBrowserSave(serializeSaveData(captureSaveData(state_)));
}

void RocketGameApp::refreshPanel()
{
    setBrowserPanelHtml(buildPanelHtml());
    panelDirty_ = false;
}

std::string RocketGameApp::buildPanelHtml() const
{
    const Destination& currentFrontier = currentDestination(state_, catalog_);
    const Destination* displayDestination = &currentFrontier;
    if (state_.screen == Screen::Launch) {
        if (const Destination* activeDestination = catalog_.findDestination(activeLaunch_.config.destinationId)) {
            displayDestination = activeDestination;
        }
    }
    const Astronaut* astronaut = activeAstronaut(state_);
    const ModuleStats stats = aggregateShipStats(state_, catalog_);
    const bool transferLaunch = state_.screen == Screen::Launch && activeLaunch_.config.frontierTransfer;
    const bool returnLaunch = state_.screen == Screen::Launch && returningHome_;
    const int requiredReadiness = frontierReadinessRequired(state_, catalog_);
    const Destination* next = nextDestination(state_, catalog_);

    std::ostringstream out;
    out << "<h1>Rocket Rogue</h1>";
    out << "<p class=\"status\">" << htmlEscape(state_.statusLine) << "</p>";

    out << "<div class=\"metric-grid\">";
    out << metric("Mission credits", money(state_.run.credits));
    out << metric("Hull damage", std::to_string(state_.run.shipDamage) + "%");
    out << metric(transferLaunch ? "Transfer target" : "Current frontier", displayDestination->name);
    out << metric(transferLaunch ? "Required burn" : "Flight data", transferLaunch ? multiplier(displayDestination->targetMultiplier) : (requiredReadiness == 0 ? "Complete" : std::to_string(state_.run.frontierReadiness) + "/" + std::to_string(requiredReadiness)));
    out << "</div>";

    if (state_.screen == Screen::Launch) {
        const PreparedLaunch flightModel = currentFlightModel();
        const double displayedMultiplier = returnLaunch
            ? returnTelemetryMultiplier(returnBurnMultiplier_, flightModel.crashMultiplier, returnElapsed_, returnDuration_)
            : currentMultiplier_;
        const TelemetryEvent event = telemetryAt(flightModel, displayedMultiplier);
        const double recoveryRisk = returnHomeRisk(flightModel, catalog_, state_, displayedMultiplier);
        const double returnProgress = smoothStep(returnElapsed_ / std::max(0.1, returnDuration_));
        out << "<h2>" << (returnLaunch ? "Return burn" : (transferLaunch ? "Transfer attempt" : "Proving flight")) << "</h2>";
        out << "<div class=\"metric-grid\">";
        out << metric("Burn depth", multiplier(displayedMultiplier));
        out << metric(returnLaunch ? "Return progress" : (transferLaunch ? "Required burn" : "Data goal"), returnLaunch ? percent(returnProgress) : multiplier(displayDestination->targetMultiplier));
        out << metric("Return risk", percent(recoveryRisk));
        out << metric("Heat", std::to_string(static_cast<int>(event.heat * 100.0)) + "%");
        out << metric("Warning", std::to_string(static_cast<int>(event.warning * 100.0)) + "%");
        out << "</div>";
        out << "<div class=\"warning-grid\">";
        out << warningButton("TEMP", event.heat);
        out << warningButton("PRESS", event.pressure);
        out << warningButton("VIB", event.vibration);
        out << warningButton("NAV", event.guidance);
        out << warningButton("MIX", event.fuelMix);
        out << warningButton("ABORT", event.abortRisk);
        out << "</div>";
        out << "<p class=\"status\">" << htmlEscape(event.message) << "</p>";
        out << "<div class=\"actions launch-actions\">";
        if (returnLaunch) {
            out << "<button disabled>Returning home</button>";
        } else {
            out << button("Return home", "rr.returnHome()", "ok");
        }
        if (returnLaunch) {
            out << "<button disabled>Cut engines</button>";
        } else {
            out << button(cutEnginesActive_ ? "Restore thrust" : "Cut engines", "rr.cutEngines()", "warn");
        }
        out << button("Eject", "rr.ejectNow()", "danger");
        out << "</div>";
        return out.str();
    }

    if (state_.screen == Screen::Results) {
        out << "<h2>Result</h2>";
        out << "<div class=\"metric-grid\">";
        out << metric("Outcome", outcomeLabel(state_.lastOutcome));
        out << metric("Recovery", std::string(toString(state_.lastOutcome.recoveryMethod)));
        out << metric("Burn depth", multiplier(state_.lastOutcome.ejectMultiplier));
        out << metric("Failure point", multiplier(state_.lastOutcome.crashMultiplier));
        out << metric("Credit delta", signedMoney(state_.lastOutcome.payout - state_.lastOutcome.recoveryCost));
        out << "</div>";
        if (!state_.lastOutcome.moduleDestroyedId.empty()) {
            out << "<p class=\"status\">Lost module: " << htmlEscape(state_.lastOutcome.moduleDestroyedId) << "</p>";
        }
        if (state_.lastOutcome.crewKilled) {
            out << "<p class=\"status\">Crew loss recorded in the memorial ledger.</p>";
        } else if (state_.lastOutcome.crewInjured) {
            out << "<p class=\"status\">Crew injured. Rest before you ask for another miracle.</p>";
        }
        out << "<div class=\"actions\">";
        out << button(state_.lastOutcome.type == LaunchResultType::Destroyed ? "New expedition" : "Return to hangar", "rr.next()", "ok");
        out << "</div>";
        return out.str();
    }

    out << "<h2>Hangar</h2>";
    out << "<p>";
    if (astronaut != nullptr) {
        out << htmlEscape(astronaut->name) << " - " << htmlEscape(astronaut->trait)
            << " - training " << astronaut->training
            << ", stress " << astronaut->stress
            << ", " << htmlEscape(toString(astronaut->status));
    } else {
        out << "No active astronaut available.";
    }
    out << "</p>";

    out << "<div class=\"tag-row\">";
    out << "<span class=\"tag\">Thrust " << std::fixed << std::setprecision(1) << stats.thrust << "</span>";
    out << "<span class=\"tag\">Fuel " << stats.fuel << "</span>";
    out << "<span class=\"tag\">Hull " << stats.hull << "</span>";
    out << "<span class=\"tag\">Cooling " << stats.cooling << "</span>";
    out << "<span class=\"tag\">Sensors " << stats.sensors << "</span>";
    out << "<span class=\"tag\">Escape " << stats.escape << "</span>";
    out << "</div>";

    out << "<h2>Frontier program</h2>";
    out << "<div class=\"metric-grid\">";
    out << metric("Flight data", requiredReadiness == 0 ? "Complete" : std::to_string(state_.run.frontierReadiness) + "/" + std::to_string(requiredReadiness));
    out << metric("Next frontier", next == nullptr ? "None charted" : next->name);
    if (next != nullptr) {
        out << metric("Transfer burn", multiplier(next->targetMultiplier));
    }
    out << "</div>";
    if (next != nullptr) {
        out << "<p>Repeat " << htmlEscape(currentFrontier.name)
            << " proving flights: burn as long as you dare, return home to bank data, then upgrade before committing to "
            << htmlEscape(next->name) << ".</p>";
    }

    out << "<div class=\"actions\">";
    out << button("Repair", "rr.repairShip()");
    out << button(activeAstronaut(state_) == nullptr ? "Recruit crew" : "Train crew", activeAstronaut(state_) == nullptr ? "rr.recruitCrew()" : "rr.trainCrew()");
    out << button(activeAstronaut(state_) == nullptr ? "Recruit reserve" : "Rest crew", activeAstronaut(state_) == nullptr ? "rr.recruitCrew()" : "rr.restCrew()");
    out << button("Launch proving flight", "rr.startLaunch()", "ok");
    if (next != nullptr) {
        if (canCommitToNextFrontier(state_, catalog_)) {
            out << button("Attempt: " + next->name, "rr.attemptFrontier()", "danger");
        } else {
            out << "<button disabled>Need flight data</button>";
        }
    }
    out << "</div>";

    out << "<h2>Module offers</h2><ul>";
    for (std::size_t i = 0; i < state_.run.offerModuleIds.size(); ++i) {
        const ShipModule* module = catalog_.findModule(state_.run.offerModuleIds[i]);
        if (module == nullptr) {
            continue;
        }
        out << "<li>" << htmlEscape(module->name) << " - " << htmlEscape(toString(module->slot))
            << " - " << htmlEscape(toString(module->rarity))
            << " - " << offerCost(*module) << " credits</li>";
    }
    out << "</ul><div class=\"actions\">";
    out << button("Buy offer 1", "rr.buyOffer(0)");
    out << button("Buy offer 2", "rr.buyOffer(1)");
    out << button("Buy offer 3", "rr.buyOffer(2)");
    out << button("Reset save", "rr.resetSave()", "danger");
    out << "</div>";

    out << "<h2>Legacy</h2>";
    out << "<p>Blueprints " << state_.meta.blueprintProgress
        << " - ships lost " << state_.meta.shipsLost
        << " - astronauts lost " << state_.meta.astronautsLost
        << " - furthest tier " << state_.meta.furthestTier << "</p>";

    return out.str();
}

RenderSnapshot RocketGameApp::snapshot() const
{
    RenderSnapshot result;
    const PreparedLaunch flightModel = currentFlightModel();
    result.screen = state_.screen;
    result.lastResult = state_.screen == Screen::Results ? state_.lastOutcome.type : LaunchResultType::None;
    result.currentMultiplier = currentMultiplier_;
    result.animationTime = state_.screen == Screen::Launch ? launchElapsed_ : resultElapsed_;
    const Destination& currentFrontier = currentDestination(state_, catalog_);
    const Destination* visualDestination = &currentFrontier;
    if (state_.screen == Screen::Launch) {
        if (const Destination* activeDestination = catalog_.findDestination(activeLaunch_.config.destinationId)) {
            visualDestination = activeDestination;
        }
        result.frontierTransfer = activeLaunch_.config.frontierTransfer;
    } else if (state_.screen == Screen::Results) {
        if (const Destination* resultDestination = catalog_.findDestination(state_.lastOutcome.destinationId)) {
            visualDestination = resultDestination;
        }
        result.frontierTransfer = state_.lastOutcome.frontierTransfer;
    }
    result.targetMultiplier = visualDestination->targetMultiplier;
    if (returningHome_) {
        const double returnProgress = smoothStep(returnElapsed_ / std::max(0.1, returnDuration_));
        result.travelProgress = std::clamp(returnStartTravelProgress_ * (1.0 - returnProgress), 0.0, 1.42);
        result.returningHome = true;
        result.returnTurnProgress = std::clamp(returnElapsed_ / 1.15, 0.0, 1.0);
    } else if (state_.screen == Screen::Results && resultUsesTravelProgress_) {
        result.travelProgress = resultTravelProgress_;
    } else {
        result.travelProgress = std::clamp(
            (currentMultiplier_ - 1.0) / std::max(0.1, result.targetMultiplier - 1.0),
            0.0,
            1.42);
    }
    result.shipDamage = static_cast<double>(state_.run.shipDamage);
    result.destinationTier = visualDestination->tier;
    result.currentFrontierTier = currentFrontier.tier;

    if (state_.screen == Screen::Launch) {
        const double displayedMultiplier = returningHome_
            ? returnTelemetryMultiplier(returnBurnMultiplier_, flightModel.crashMultiplier, returnElapsed_, returnDuration_)
            : currentMultiplier_;
        const TelemetryEvent event = telemetryAt(flightModel, displayedMultiplier);
        result.heat = event.heat;
        result.warning = event.warning;
        for (int i = 0; i < static_cast<int>(result.telemetry.size()); ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(result.telemetry.size() - 1);
            const double sampleCeiling = returningHome_
                ? displayedMultiplier
                : std::max(currentMultiplier_, result.targetMultiplier);
            const double sampleMultiplier = 1.0 + (sampleCeiling - 1.0) * t;
            const TelemetryEvent sample = telemetryAt(flightModel, sampleMultiplier);
            result.telemetry[static_cast<std::size_t>(i)] = sample.warning;
            result.heatTelemetry[static_cast<std::size_t>(i)] = std::clamp(sample.heat, 0.0, 1.0);
        }
        result.telemetryCount = static_cast<int>(result.telemetry.size());
        result.poweredFlight = !returningHome_ && !cutEnginesActive_;
    } else if (!state_.lastOutcome.telemetry.empty()) {
        const int count = std::min(static_cast<int>(result.telemetry.size()), static_cast<int>(state_.lastOutcome.telemetry.size()));
        for (int i = 0; i < count; ++i) {
            const TelemetryEvent& sample = state_.lastOutcome.telemetry[static_cast<std::size_t>(i)];
            result.telemetry[static_cast<std::size_t>(i)] = sample.warning;
            result.heatTelemetry[static_cast<std::size_t>(i)] = std::clamp(sample.heat, 0.0, 1.0);
        }
        result.telemetryCount = count;
    }

    return result;
}

} // namespace rocket
