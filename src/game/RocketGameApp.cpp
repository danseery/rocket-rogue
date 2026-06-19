#include "game/RocketGameApp.h"

#include "core/SaveData.h"
#include "game/GamePanel.h"
#include "platform/WebSaveStore.h"

#include <algorithm>

namespace rocket {

namespace {

double smoothStep(double value)
{
    const double t = std::clamp(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

PreparedLaunch RocketGameApp::currentFlightModel() const
{
    PreparedLaunch launch = activeLaunch_;
    if (pressureReliefOpen_ || pressureReliefFailed_) {
        launch = withPressureRelief(launch, pressureReliefFailed_);
    }
    if (cargoJettisoned_) {
        launch = withJettisonedCargo(launch);
    }
    if (cutEnginesActive_ && !returningHome_) {
        launch = withCutEngines(launch);
    }
    return launch;
}

void RocketGameApp::recordTelemetryPeak(const TelemetryEvent& event)
{
    peakWarning_ = std::max(peakWarning_, event.warning);
    peakAbortRisk_ = std::max(peakAbortRisk_, event.abortRisk);
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
                recordTelemetryPeak(telemetryAt(flightModel, returnTelemetry));
                completeLaunch(returnTelemetry, RecoveryMethod::ReturnHome);
            } else {
                const TelemetryEvent event = telemetryAt(flightModel, returnTelemetry);
                recordTelemetryPeak(event);
                if (event.warning > 0.78 || event.heat > 0.88) {
                    state_.statusLine = returnDriftHome_
                        ? event.message + ". Coasting gives mission control fewer ways to help."
                        : event.message + ". The return burn is still biting.";
                } else if (returnProgress < 0.28) {
                    state_.statusLine = returnDriftHome_
                        ? "Fuel reserve is gone. Coasting home on gravity and uncomfortable math."
                        : "Return burn committed. Rotating ship for retrograde flight.";
                } else {
                    state_.statusLine = returnDriftHome_
                        ? "Coasting home. No thrust, less control, plenty of silence."
                        : "Return burn underway. Systems are easing, but this is not free.";
                }
            }
        } else {
            const double clampedDelta = std::clamp(deltaSeconds, 0.0, 0.08);
            currentMultiplier_ += burnMultiplierDelta(flightModel, destination, launchElapsed_, clampedDelta);
            launchElapsed_ += clampedDelta;

            if (currentMultiplier_ >= flightModel.crashMultiplier) {
                recordTelemetryPeak(telemetryAt(flightModel, flightModel.crashMultiplier));
                completeLaunch(flightModel.crashMultiplier, RecoveryMethod::None);
            } else if (activeLaunch_.config.frontierTransfer && currentMultiplier_ >= destination.targetMultiplier) {
                recordTelemetryPeak(telemetryAt(flightModel, destination.targetMultiplier));
                completeLaunch(destination.targetMultiplier, RecoveryMethod::TransferArrival);
            } else {
                const TelemetryEvent event = telemetryAt(flightModel, currentMultiplier_);
                recordTelemetryPeak(event);
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
    peakWarning_ = 0.0;
    peakAbortRisk_ = 0.0;
    launchElapsed_ = 0.0;
    returningHome_ = false;
    returnDriftHome_ = false;
    cutEnginesActive_ = false;
    pressureReliefUsed_ = false;
    pressureReliefOpen_ = false;
    pressureReliefFailed_ = false;
    cargoJettisoned_ = false;
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
    recordTelemetryPeak(telemetryAt(currentFlightModel(), liveMultiplier));
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
    const PreparedLaunch flightModel = currentFlightModel();
    const TelemetryEvent event = telemetryAt(flightModel, currentMultiplier_);
    recordTelemetryPeak(event);
    const double fuelReserve = std::max(0.0, flightModel.stats.fuel);
    returnDriftHome_ = event.fuelMix > 0.86 || (fuelReserve < 1.0 && currentMultiplier_ > destination.targetMultiplier * 0.85);
    if (returnDriftHome_) {
        returnDuration_ *= 1.25;
    }
    returningHome_ = true;
    cutEnginesActive_ = false;
    state_.statusLine = returnDriftHome_
        ? "Fuel reserve is gone. Coasting home on gravity and uncomfortable math."
        : "Return burn committed. Rotating ship for retrograde flight.";
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

void RocketGameApp::pressureReliefValve()
{
    if (state_.screen != Screen::Launch || returningHome_ || pressureReliefUsed_) {
        return;
    }

    const PreparedLaunch flightModel = currentFlightModel();
    const TelemetryEvent event = telemetryAt(flightModel, currentMultiplier_);
    recordTelemetryPeak(event);
    const double failureChance = std::clamp(0.16 + event.pressure * 0.16 - flightModel.stats.pressure * 0.035 - flightModel.stats.hull * 0.010, 0.04, 0.34);
    const double decompressionChance = std::clamp(0.012 + event.pressure * 0.045 + static_cast<double>(state_.run.shipDamage) * 0.00025 - flightModel.stats.hull * 0.0035, 0.004, 0.08);

    pressureReliefUsed_ = true;
    pressureReliefOpen_ = true;
    if (rng_.chance(decompressionChance)) {
        completeLaunch(currentMultiplier_, RecoveryMethod::None);
        state_.statusLine = "Rapid decompression after relief-valve actuation. Vehicle lost.";
        save();
        return;
    }

    pressureReliefFailed_ = rng_.chance(failureChance);
    state_.statusLine = pressureReliefFailed_
        ? "Pressure relief valve stuck. PRESS is worse and nav authority is degraded."
        : "Pressure relief valve opened. PRESS dropped, but the vent shoved the ship off-track.";
    panelDirty_ = true;
}

void RocketGameApp::closePressureReliefValve()
{
    if (state_.screen != Screen::Launch || returningHome_ || !pressureReliefOpen_ || pressureReliefFailed_) {
        return;
    }

    pressureReliefOpen_ = false;
    state_.statusLine = "Pressure relief valve closed. PRESS is building normally again, and the vent drift is fading.";
    panelDirty_ = true;
}

void RocketGameApp::jettisonCargo()
{
    if (state_.screen != Screen::Launch || returningHome_ || cargoJettisoned_) {
        return;
    }

    cargoJettisoned_ = true;
    state_.statusLine = "Cargo jettisoned. Fuel mix stabilized, but debris and mass shift hurt NAV, VIB, and return margin.";
    panelDirty_ = true;
}

void RocketGameApp::next()
{
    if (state_.screen == Screen::Results) {
        if (!state_.run.active || state_.lastOutcome.type == LaunchResultType::Destroyed) {
            startNewExpedition(state_, catalog_);
        }
        generateModuleOffers(state_, catalog_, rng_);
        state_.screen = Screen::Upgrade;
        state_.statusLine = "Choose one refit. Installation takes the whole hangar window.";
        syncLaunchConfig(state_, catalog_);
        save();
    } else if (state_.screen == Screen::Upgrade) {
        state_.run.offerModuleIds = {};
        state_.run.offerCrewUpgradeIds = {};
        state_.screen = Screen::Hangar;
        state_.statusLine = "Refit window closed. Handle repairs, crew, and the next flight plan.";
        syncLaunchConfig(state_, catalog_);
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::attemptFrontierTransfer()
{
    if (state_.screen != Screen::Hangar) {
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
    state_.launchConfig.burnGoalMultiplier = next->targetMultiplier;
    activeLaunch_ = prepareLaunch(state_, catalog_, rng_);
    currentMultiplier_ = 1.0;
    peakWarning_ = 0.0;
    peakAbortRisk_ = 0.0;
    launchElapsed_ = 0.0;
    returningHome_ = false;
    returnDriftHome_ = false;
    cutEnginesActive_ = false;
    pressureReliefUsed_ = false;
    pressureReliefOpen_ = false;
    pressureReliefFailed_ = false;
    cargoJettisoned_ = false;
    returnElapsed_ = 0.0;
    resultUsesTravelProgress_ = false;
    resultElapsed_ = 0.0;
    state_.screen = Screen::Launch;
    state_.statusLine = "Transfer attempt committed. Survive to the required burn, or abort before the ship decides for you.";
    panelDirty_ = true;
}

void RocketGameApp::buyOffer(int index)
{
    if (state_.screen != Screen::Upgrade) {
        return;
    }
    if (rocket::buyOffer(state_, catalog_, index)) {
        state_.screen = Screen::Hangar;
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::rerollOffers()
{
    if (state_.screen != Screen::Upgrade) {
        return;
    }

    if (rocket::rerollOffers(state_, catalog_, rng_)) {
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
    if (rocket::trainCrew(state_, catalog_)) {
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::restCrew()
{
    if (rocket::restCrew(state_, catalog_)) {
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::resetSave()
{
    clearBrowserSave();
    state_ = createNewGame(catalog_, 0x524F434B45544ULL);
    rng_ = Random(state_.seed);
    returningHome_ = false;
    returnDriftHome_ = false;
    cutEnginesActive_ = false;
    pressureReliefUsed_ = false;
    pressureReliefOpen_ = false;
    pressureReliefFailed_ = false;
    cargoJettisoned_ = false;
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
    outcome.peakWarning = std::max(outcome.peakWarning, peakWarning_);
    outcome.peakAbortRisk = std::max(outcome.peakAbortRisk, peakAbortRisk_);
    applyLaunchOutcome(state_, catalog_, outcome);
    state_.screen = Screen::Results;
    currentMultiplier_ = outcome.ejectMultiplier;
    peakWarning_ = 0.0;
    peakAbortRisk_ = 0.0;
    resultUsesTravelProgress_ = wasReturningHome;
    resultTravelProgress_ = frozenTravelProgress;
    returningHome_ = false;
    returnDriftHome_ = false;
    cutEnginesActive_ = false;
    pressureReliefUsed_ = false;
    pressureReliefOpen_ = false;
    pressureReliefFailed_ = false;
    cargoJettisoned_ = false;
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
    const PreparedLaunch flightModel = currentFlightModel();
    setBrowserPanelHtml(buildGamePanelHtml({
        state_,
        catalog_,
        activeLaunch_,
        flightModel,
        currentMultiplier_,
        returnBurnMultiplier_,
        returnElapsed_,
        returnDuration_,
        returningHome_,
        cutEnginesActive_,
        pressureReliefUsed_,
        pressureReliefOpen_,
        pressureReliefFailed_,
        cargoJettisoned_
    }));
    panelDirty_ = false;
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
        result.poweredFlight = returningHome_ ? !returnDriftHome_ : !cutEnginesActive_;
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
