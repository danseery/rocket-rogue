#include "game/RocketGameApp.h"

#include "core/FlightProgress.h"
#include "core/GameText.h"
#include "core/LaunchStatus.h"
#include "core/Tuning.h"
#include "core/SaveData.h"
#include "game/GamePanel.h"
#include "platform/WebSaveStore.h"

#include <algorithm>

namespace rocket {

PreparedLaunch RocketGameApp::currentFlightModel() const
{
    return applyFlightActions(session_.preparedLaunch, session_.controls.actions);
}

void RocketGameApp::recordTelemetryPeak(const TelemetryEvent& event)
{
    session_.peakWarning = std::max(session_.peakWarning, event.warning);
    session_.peakAbortRisk = std::max(session_.peakAbortRisk, event.abortRisk);
}

void RocketGameApp::clearFlightControls()
{
    session_.controls = {};
    session_.returnTrip = {};
    session_.returnTrip.duration = tuning::session::returnDefaultDuration;
}

void RocketGameApp::clearResultView()
{
    session_.result = {};
}

void RocketGameApp::beginLaunchSession(PreparedLaunch preparedLaunch)
{
    session_.preparedLaunch = preparedLaunch;
    session_.elapsed = 0.0;
    session_.currentMultiplier = 1.0;
    session_.peakWarning = 0.0;
    session_.peakAbortRisk = 0.0;
    clearFlightControls();
    clearResultView();
}

double RocketGameApp::liveBurnMultiplier() const
{
    if (!session_.controls.actions.returningHome) {
        return session_.currentMultiplier;
    }
    return returnTelemetryMultiplier(
        session_.returnTrip.burnMultiplier,
        currentFlightModel().crashMultiplier,
        session_.returnTrip.elapsed,
        session_.returnTrip.duration);
}

bool RocketGameApp::initialize()
{
    catalog_ = createDefaultContent();
    state_ = createNewGame(catalog_, 0x524F434B45544ULL);
    rng_ = Random(state_.seed);

    if (const auto saveData = deserializeSaveData(loadBrowserSave())) {
        restoreSaveData(state_, catalog_, *saveData);
        rng_ = Random(saveData->seed + 0xA51CE5ULL + static_cast<std::uint64_t>(saveData->blueprintProgress));
        state_.statusLine = std::string(text::status::saveRestored);
    }

    syncLaunchConfig(state_, catalog_);

    if (!renderer_.initialize()) {
        state_.statusLine = std::string(text::status::webglFailed);
    }

    refreshPanel();
    return true;
}

void RocketGameApp::tick(double deltaSeconds)
{
    if (state_.screen == Screen::Launch) {
        const PreparedLaunch flightModel = currentFlightModel();
        const Destination* activeDestination = catalog_.findDestination(session_.preparedLaunch.config.destinationId);
        const Destination& destination = activeDestination == nullptr ? currentDestination(state_, catalog_) : *activeDestination;

        if (session_.controls.actions.returningHome) {
            session_.returnTrip.elapsed += std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds);
            const double returnProgress = flight_progress::returnCompletion(session_.returnTrip.elapsed, session_.returnTrip.duration);
            const double returnTelemetry = returnTelemetryMultiplier(
                session_.returnTrip.burnMultiplier,
                flightModel.crashMultiplier,
                session_.returnTrip.elapsed,
                session_.returnTrip.duration);
            if (returnProgress >= 1.0) {
                recordTelemetryPeak(telemetryAt(flightModel, returnTelemetry));
                completeLaunch(returnTelemetry, RecoveryMethod::ReturnHome);
            } else {
                const TelemetryEvent event = telemetryAt(flightModel, returnTelemetry);
                recordTelemetryPeak(event);
                state_.statusLine = launchStatusLine({
                    event,
                    session_.controls.actions,
                    session_.preparedLaunch.config.frontierTransfer,
                    session_.controls.returnDriftHome,
                    false,
                    returnProgress
                });
            }
        } else {
            const double clampedDelta = std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds);
            session_.currentMultiplier += burnMultiplierDelta(flightModel, destination, session_.elapsed, clampedDelta);
            session_.elapsed += clampedDelta;

            if (session_.currentMultiplier >= flightModel.crashMultiplier) {
                recordTelemetryPeak(telemetryAt(flightModel, flightModel.crashMultiplier));
                completeLaunch(flightModel.crashMultiplier, RecoveryMethod::None);
            } else if (session_.preparedLaunch.config.frontierTransfer && session_.currentMultiplier >= destination.targetMultiplier) {
                recordTelemetryPeak(telemetryAt(flightModel, destination.targetMultiplier));
                completeLaunch(destination.targetMultiplier, RecoveryMethod::TransferArrival);
            } else {
                const TelemetryEvent event = telemetryAt(flightModel, session_.currentMultiplier);
                recordTelemetryPeak(event);
                const bool pastDataGoal = !session_.preparedLaunch.config.frontierTransfer && session_.currentMultiplier >= destination.targetMultiplier;
                state_.statusLine = launchStatusLine({
                    event,
                    session_.controls.actions,
                    session_.preparedLaunch.config.frontierTransfer,
                    session_.controls.returnDriftHome,
                    pastDataGoal,
                    0.0
                });
            }
        }

        panelDirty_ = true;
    } else if (state_.screen == Screen::Results) {
        session_.result.elapsed += std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds);
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

    if (state_.run.shipDamage >= tuning::damage::destroyedShipDamage) {
        state_.statusLine = std::string(text::status::launchHullBlocked);
        panelDirty_ = true;
        return;
    }

    if (activeAstronaut(state_) == nullptr) {
        state_.statusLine = std::string(text::status::launchCrewBlocked);
        panelDirty_ = true;
        return;
    }

    syncLaunchConfig(state_, catalog_);
    state_.launchConfig.frontierTransfer = false;
    state_.launchConfig.destinationId = currentDestination(state_, catalog_).id;
    beginLaunchSession(prepareLaunch(state_, catalog_, rng_));
    state_.screen = Screen::Launch;
    state_.statusLine = std::string(text::status::provingBurnStarted);
    panelDirty_ = true;
}

void RocketGameApp::ejectNow()
{
    if (state_.screen != Screen::Launch) {
        return;
    }
    const double liveMultiplier = liveBurnMultiplier();
    recordTelemetryPeak(telemetryAt(currentFlightModel(), liveMultiplier));
    completeLaunch(liveMultiplier, RecoveryMethod::ManualEject);
}

void RocketGameApp::returnHome()
{
    if (state_.screen != Screen::Launch || session_.controls.actions.returningHome) {
        return;
    }

    const Destination* activeDestination = catalog_.findDestination(session_.preparedLaunch.config.destinationId);
    const Destination& destination = activeDestination == nullptr ? currentDestination(state_, catalog_) : *activeDestination;
    session_.returnTrip.burnMultiplier = session_.currentMultiplier;
    session_.returnTrip.startTravelProgress = flight_progress::travelProgressForBurn(session_.currentMultiplier, destination);
    session_.returnTrip.elapsed = 0.0;
    const PreparedLaunch flightModel = currentFlightModel();
    const TelemetryEvent event = telemetryAt(flightModel, session_.currentMultiplier);
    recordTelemetryPeak(event);
    const double fuelReserve = std::max(0.0, flightModel.stats.fuel);
    session_.controls.returnDriftHome = event.fuelMix > tuning::session::driftFuelMixThreshold ||
        (fuelReserve < tuning::session::driftFuelReserveThreshold &&
            session_.currentMultiplier > destination.targetMultiplier * tuning::session::driftTargetShare);
    session_.returnTrip.duration = flight_progress::returnDuration(
        session_.returnTrip.startTravelProgress,
        session_.controls.returnDriftHome);
    session_.controls.actions.returningHome = true;
    session_.controls.actions.cutEnginesActive = false;
    state_.statusLine = session_.controls.returnDriftHome
        ? std::string(text::status::fuelReserveGone)
        : std::string(text::status::returnBurnRotating);
    panelDirty_ = true;
}

void RocketGameApp::cutEngines()
{
    if (state_.screen != Screen::Launch || session_.controls.actions.returningHome) {
        return;
    }

    session_.controls.actions.cutEnginesActive = !session_.controls.actions.cutEnginesActive;
    state_.statusLine = session_.controls.actions.cutEnginesActive
        ? std::string(text::status::engineCutConfirmed)
        : std::string(text::status::thrustRestored);
    panelDirty_ = true;
}

void RocketGameApp::pressureReliefValve()
{
    if (state_.screen != Screen::Launch || session_.controls.actions.returningHome || session_.controls.pressureReliefUsed) {
        return;
    }

    const PreparedLaunch flightModel = currentFlightModel();
    const TelemetryEvent event = telemetryAt(flightModel, session_.currentMultiplier);
    recordTelemetryPeak(event);
    const double failureChance = std::clamp(
        tuning::session::pressureReliefFailureBase +
            event.pressure * tuning::session::pressureReliefFailurePressureScale -
            flightModel.stats.pressure * tuning::session::pressureReliefFailureControlScale -
            flightModel.stats.hull * tuning::session::pressureReliefFailureHullScale,
        tuning::session::pressureReliefFailureMinimum,
        tuning::session::pressureReliefFailureMaximum);
    const double decompressionChance = std::clamp(
        tuning::session::decompressionBase +
            event.pressure * tuning::session::decompressionPressureScale +
            static_cast<double>(state_.run.shipDamage) * tuning::session::decompressionDamageScale -
            flightModel.stats.hull * tuning::session::decompressionHullScale,
        tuning::session::decompressionMinimum,
        tuning::session::decompressionMaximum);

    session_.controls.pressureReliefUsed = true;
    session_.controls.actions.pressureReliefOpen = true;
    if (rng_.chance(decompressionChance)) {
        completeLaunch(session_.currentMultiplier, RecoveryMethod::None);
        state_.statusLine = std::string(text::status::rapidDecompression);
        save();
        return;
    }

    session_.controls.actions.pressureReliefFailed = rng_.chance(failureChance);
    state_.statusLine = session_.controls.actions.pressureReliefFailed
        ? std::string(text::status::pressureReliefStuck)
        : std::string(text::status::pressureReliefOpened);
    panelDirty_ = true;
}

void RocketGameApp::closePressureReliefValve()
{
    if (state_.screen != Screen::Launch ||
        session_.controls.actions.returningHome ||
        !session_.controls.actions.pressureReliefOpen ||
        session_.controls.actions.pressureReliefFailed) {
        return;
    }

    session_.controls.actions.pressureReliefOpen = false;
    state_.statusLine = std::string(text::status::pressureReliefClosed);
    panelDirty_ = true;
}

void RocketGameApp::jettisonCargo()
{
    if (state_.screen != Screen::Launch || session_.controls.actions.returningHome || session_.controls.actions.cargoJettisoned) {
        return;
    }

    session_.controls.actions.cargoJettisoned = true;
    state_.statusLine = std::string(text::status::cargoJettisoned);
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
        state_.statusLine = std::string(text::status::refitWindowOpened);
        syncLaunchConfig(state_, catalog_);
        save();
    } else if (state_.screen == Screen::Upgrade) {
        state_.run.offerModuleIds = {};
        state_.run.offerCrewUpgradeIds = {};
        state_.screen = Screen::Hangar;
        state_.statusLine = std::string(text::status::refitWindowClosed);
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

    if (state_.run.shipDamage >= tuning::damage::destroyedShipDamage) {
        state_.statusLine = std::string(text::status::launchHullBlocked);
        panelDirty_ = true;
        return;
    }

    if (activeAstronaut(state_) == nullptr) {
        state_.statusLine = std::string(text::status::launchCrewBlocked);
        panelDirty_ = true;
        return;
    }

    if (!canCommitToNextFrontier(state_, catalog_)) {
        const Destination* next = nextDestination(state_, catalog_);
        state_.statusLine = next == nullptr ? std::string(text::status::noFartherFrontier) : std::string(text::status::moreProvingDataBeforeTransfer);
        panelDirty_ = true;
        return;
    }

    const Destination* next = nextDestination(state_, catalog_);
    if (next == nullptr) {
        state_.statusLine = std::string(text::status::noFartherFrontier);
        panelDirty_ = true;
        return;
    }

    state_.launchConfig.frontierTransfer = true;
    state_.launchConfig.destinationId = next->id;
    state_.launchConfig.burnGoalMultiplier = next->targetMultiplier;
    beginLaunchSession(prepareLaunch(state_, catalog_, rng_));
    state_.screen = Screen::Launch;
    state_.statusLine = std::string(text::status::transferBurnStarted);
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
    session_ = {};
    session_.returnTrip.duration = tuning::session::returnDefaultDuration;
    panelDirty_ = true;
}

void RocketGameApp::completeLaunch(double burnMultiplier, RecoveryMethod method)
{
    const PreparedLaunch flightModel = currentFlightModel();
    const bool wasReturningHome = session_.controls.actions.returningHome;
    double frozenTravelProgress = flight_progress::travelProgressForBurn(burnMultiplier, currentDestination(state_, catalog_));
    if (wasReturningHome) {
        frozenTravelProgress = flight_progress::returnTravelProgress(
            session_.returnTrip.startTravelProgress,
            session_.returnTrip.elapsed,
            session_.returnTrip.duration);
    } else if (const Destination* activeDestination = catalog_.findDestination(session_.preparedLaunch.config.destinationId)) {
        frozenTravelProgress = flight_progress::travelProgressForBurn(burnMultiplier, *activeDestination);
    }

    LaunchOutcome outcome = resolveLaunch(flightModel, catalog_, state_, burnMultiplier, method, rng_);
    outcome.peakWarning = std::max(outcome.peakWarning, session_.peakWarning);
    outcome.peakAbortRisk = std::max(outcome.peakAbortRisk, session_.peakAbortRisk);
    applyLaunchOutcome(state_, catalog_, outcome);
    state_.screen = Screen::Results;
    session_.currentMultiplier = outcome.ejectMultiplier;
    session_.peakWarning = 0.0;
    session_.peakAbortRisk = 0.0;
    session_.result.usesTravelProgress = wasReturningHome;
    session_.result.travelProgress = frozenTravelProgress;
    session_.result.elapsed = 0.0;
    clearFlightControls();
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
        session_.preparedLaunch,
        flightModel,
        session_.currentMultiplier,
        session_.returnTrip.burnMultiplier,
        session_.returnTrip.elapsed,
        session_.returnTrip.duration,
        session_.controls.actions,
        session_.controls.pressureReliefUsed,
    }));
    panelDirty_ = false;
}

RenderSnapshot RocketGameApp::snapshot() const
{
    RenderSnapshot result;
    const PreparedLaunch flightModel = currentFlightModel();
    result.screen = state_.screen;
    result.lastResult = state_.screen == Screen::Results ? state_.lastOutcome.type : LaunchResultType::None;
    result.currentMultiplier = session_.currentMultiplier;
    result.animationTime = state_.screen == Screen::Launch ? session_.elapsed : session_.result.elapsed;
    const Destination& currentFrontier = currentDestination(state_, catalog_);
    const Destination* visualDestination = &currentFrontier;
    if (state_.screen == Screen::Launch) {
        if (const Destination* activeDestination = catalog_.findDestination(session_.preparedLaunch.config.destinationId)) {
            visualDestination = activeDestination;
        }
        result.frontierTransfer = session_.preparedLaunch.config.frontierTransfer;
    } else if (state_.screen == Screen::Results) {
        if (const Destination* resultDestination = catalog_.findDestination(state_.lastOutcome.destinationId)) {
            visualDestination = resultDestination;
        }
        result.frontierTransfer = state_.lastOutcome.frontierTransfer;
    }
    result.targetMultiplier = visualDestination->targetMultiplier;
    if (session_.controls.actions.returningHome) {
        result.travelProgress = flight_progress::returnTravelProgress(
            session_.returnTrip.startTravelProgress,
            session_.returnTrip.elapsed,
            session_.returnTrip.duration);
        result.returningHome = true;
        result.returnTurnProgress = std::clamp(session_.returnTrip.elapsed / tuning::session::returnTurnSeconds, 0.0, 1.0);
    } else if (state_.screen == Screen::Results && session_.result.usesTravelProgress) {
        result.travelProgress = session_.result.travelProgress;
    } else {
        result.travelProgress = flight_progress::travelProgressForBurn(session_.currentMultiplier, *visualDestination);
    }
    result.shipDamage = static_cast<double>(state_.run.shipDamage);
    result.destinationTier = visualDestination->tier;
    result.currentFrontierTier = currentFrontier.tier;

    if (state_.screen == Screen::Launch) {
        const double displayedMultiplier = liveBurnMultiplier();
        const TelemetryEvent event = telemetryAt(flightModel, displayedMultiplier);
        result.heat = event.heat;
        result.warning = event.warning;
        for (int i = 0; i < static_cast<int>(result.telemetry.size()); ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(result.telemetry.size() - 1);
            const double sampleCeiling = session_.controls.actions.returningHome
                ? displayedMultiplier
                : std::max(session_.currentMultiplier, result.targetMultiplier);
            const double sampleMultiplier = 1.0 + (sampleCeiling - 1.0) * t;
            const TelemetryEvent sample = telemetryAt(flightModel, sampleMultiplier);
            result.telemetry[static_cast<std::size_t>(i)] = sample.warning;
            result.heatTelemetry[static_cast<std::size_t>(i)] = std::clamp(sample.heat, 0.0, 1.0);
        }
        result.telemetryCount = static_cast<int>(result.telemetry.size());
        result.poweredFlight = session_.controls.actions.returningHome
            ? !session_.controls.returnDriftHome
            : !session_.controls.actions.cutEnginesActive;
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
