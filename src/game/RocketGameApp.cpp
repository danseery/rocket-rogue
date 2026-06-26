#include "game/RocketGameApp.h"

#include "core/FlightProgress.h"
#include "core/GameText.h"
#include "core/LaunchStatus.h"
#include "core/MiningSystem.h"
#include "core/ResearchSystem.h"
#include "core/Tuning.h"
#include "core/SaveData.h"
#include "game/GamePanel.h"
#include "platform/WebSaveStore.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rocket {
namespace {

std::vector<TelemetryEvent> chartTelemetryForOutcome(
    const PreparedLaunch& launch,
    const ContentCatalog& catalog,
    double burnMultiplier,
    bool returningHome,
    double travelProgress)
{
    constexpr int sampleCapacity = tuning::launch::telemetrySampleCount;
    const Destination* destination = catalog.findDestination(launch.config.destinationId);
    const double destinationTarget = destination == nullptr ? burnMultiplier : destination->targetMultiplier;
    const double sampleCeiling = returningHome
        ? burnMultiplier
        : std::max(burnMultiplier, destinationTarget);
    const double plotProgress = returningHome ? 1.0 : std::clamp(travelProgress, 0.0, 1.0);
    const int sampleCount = std::clamp(
        static_cast<int>(std::ceil(plotProgress * static_cast<double>(sampleCapacity - 1))) + 1,
        2,
        sampleCapacity);

    std::vector<TelemetryEvent> telemetry;
    telemetry.reserve(static_cast<std::size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sampleCapacity - 1);
        telemetry.push_back(telemetryAt(launch, 1.0 + (sampleCeiling - 1.0) * t));
    }
    return telemetry;
}

} // namespace

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

void RocketGameApp::beginArrivalFanfare()
{
    session_.arrivalFanfare = {true, 0.0};
    state_.screen = Screen::ArrivalFanfare;
    state_.statusLine = std::string(text::status::arrivalFanfare);
}

void RocketGameApp::finishArrivalFanfare()
{
    if (state_.screen != Screen::ArrivalFanfare) {
        return;
    }
    session_.arrivalFanfare = {};
    state_.screen = Screen::ArrivalOps;
    state_.statusLine = std::string(text::status::arrivalOpsOpened);
    panelDirty_ = true;
}

void RocketGameApp::beginSurfaceExpeditionOrRefit()
{
    startSurfaceExpedition(state_, catalog_, &rng_);
    state_.screen = state_.run.surfaceExpedition.active ? Screen::SurfaceExpedition : Screen::Upgrade;
    state_.statusLine = state_.run.surfaceExpedition.active
        ? std::string(text::status::surfaceExpeditionStarted)
        : std::string(text::status::refitWindowOpened);
    if (state_.screen == Screen::Upgrade) {
        generateModuleOffers(state_, catalog_, rng_);
    }
}

void RocketGameApp::beginLaunchSession(PreparedLaunch preparedLaunch)
{
    session_.preparedLaunch = preparedLaunch;
    session_.flightArmed = false;
    session_.elapsed = 0.0;
    session_.currentMultiplier = 1.0;
    session_.peakWarning = 0.0;
    session_.peakAbortRisk = 0.0;
    clearFlightControls();
    clearResultView();
    session_.arrivalFanfare = {};
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

    ensureDroneBayState(state_, catalog_);
    migrateLegacyDeepSpaceFrontier(state_, catalog_);
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
        if (!session_.flightArmed) {
            return;
        }
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
            } else if (!session_.preparedLaunch.config.frontierTransfer && destination.tier >= 1 && session_.currentMultiplier >= destination.targetMultiplier) {
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
    } else if (state_.screen == Screen::Mining) {
        const bool wasActive = state_.run.mining.active;
        updateMiningRun(state_, catalog_, deltaSeconds);
        if (wasActive && !state_.run.mining.active) {
            state_.statusLine = std::string(text::status::miningAborted);
            save();
        }
        panelDirty_ = true;
    } else if (state_.screen == Screen::Results) {
        session_.result.elapsed += std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds);
    } else if (state_.screen == Screen::ArrivalFanfare) {
        session_.arrivalFanfare.elapsed = std::min(
            session_.arrivalFanfare.elapsed + std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds),
            tuning::session::arrivalFanfareSeconds);
    }

    if (panelDirty_) {
        refreshPanel();
    }
}

void RocketGameApp::render()
{
    renderer_.render(snapshot());
}

void RocketGameApp::prepareForLaunch()
{
    if (state_.screen != Screen::Hangar) {
        return;
    }

    if (state_.run.shipDamage >= tuning::damage::destroyedShipDamage) {
        state_.statusLine = std::string(text::status::launchHullBlocked);
        refreshPanel();
        return;
    }

    if (activeAstronaut(state_) == nullptr) {
        state_.statusLine = std::string(text::status::launchCrewBlocked);
        refreshPanel();
        return;
    }

    state_.run.active = true;
    syncLaunchConfig(state_, catalog_);
    state_.launchConfig.frontierTransfer = hostileSystemActive(state_);
    state_.launchConfig.destinationId = currentDestination(state_, catalog_).id;
    state_.launchConfig.burnGoalMultiplier = state_.launchConfig.frontierTransfer
        ? currentDestination(state_, catalog_).targetMultiplier
        : defaultProvingTarget(currentDestination(state_, catalog_));
    beginLaunchSession(rocket::prepareLaunch(state_, catalog_, rng_));
    state_.screen = Screen::Launch;
    state_.statusLine = std::string(text::status::preflightReady);
    refreshPanel();
}

void RocketGameApp::startLaunch()
{
    if (state_.screen == Screen::Hangar) {
        prepareForLaunch();
        return;
    }

    if (state_.screen != Screen::Launch || session_.flightArmed) {
        return;
    }

    session_.flightArmed = true;
    state_.statusLine = session_.preparedLaunch.config.frontierTransfer
        ? std::string(text::status::transferBurnStarted)
        : std::string(text::status::provingBurnStarted);
    refreshPanel();
}

void RocketGameApp::ejectNow()
{
    if (state_.screen != Screen::Launch || !session_.flightArmed) {
        return;
    }
    const double liveMultiplier = liveBurnMultiplier();
    recordTelemetryPeak(telemetryAt(currentFlightModel(), liveMultiplier));
    completeLaunch(liveMultiplier, RecoveryMethod::ManualEject);
}

void RocketGameApp::returnHome()
{
    if (state_.screen != Screen::Launch || !session_.flightArmed || session_.controls.actions.returningHome) {
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

void RocketGameApp::arrivalOps()
{
    if (state_.screen != Screen::Launch || !session_.flightArmed || session_.controls.actions.returningHome || session_.preparedLaunch.config.frontierTransfer) {
        return;
    }

    const Destination& destination = currentDestination(state_, catalog_);
    if (destination.tier < 1 || session_.currentMultiplier < destination.targetMultiplier) {
        return;
    }

    recordTelemetryPeak(telemetryAt(currentFlightModel(), session_.currentMultiplier));
    completeLaunch(session_.currentMultiplier, RecoveryMethod::TransferArrival);
}

void RocketGameApp::skipArrivalFanfare()
{
    finishArrivalFanfare();
    if (panelDirty_) {
        refreshPanel();
    }
}

void RocketGameApp::cutEngines()
{
    if (state_.screen != Screen::Launch || !session_.flightArmed || session_.controls.actions.returningHome) {
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
    if (state_.screen != Screen::Launch || !session_.flightArmed || session_.controls.actions.returningHome || session_.controls.pressureReliefUsed) {
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
        !session_.flightArmed ||
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
    if (state_.screen != Screen::Launch || !session_.flightArmed || session_.controls.actions.returningHome || session_.controls.actions.cargoJettisoned) {
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
        if (shouldOpenArrivalOps(state_.lastOutcome, catalog_)) {
            startArrivalOps(state_, state_.lastOutcome);
            state_.screen = Screen::ArrivalOps;
            state_.statusLine = std::string(text::status::arrivalOpsOpened);
            syncLaunchConfig(state_, catalog_);
            save();
            panelDirty_ = true;
            return;
        }
        generateModuleOffers(state_, catalog_, rng_);
        state_.screen = Screen::Upgrade;
        state_.statusLine = std::string(text::status::refitWindowOpened);
        syncLaunchConfig(state_, catalog_);
        save();
    } else if (state_.screen == Screen::Upgrade) {
        state_.run.offerModuleIds = {};
        state_.run.offerCrewUpgradeIds = {};
        state_.screen = navigationAvailable(state_) ? Screen::Navigation : Screen::Hangar;
        state_.statusLine = std::string(text::status::refitWindowClosed);
        syncLaunchConfig(state_, catalog_);
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::runArrivalFlyby()
{
    if (state_.screen != Screen::ArrivalOps || !canRunArrivalFlyby(state_, catalog_)) {
        return;
    }

    completeArrivalFlyby(state_, catalog_);
    generateModuleOffers(state_, catalog_, rng_);
    state_.screen = Screen::Upgrade;
    state_.statusLine = std::string(text::status::flybyCompleted);
    syncLaunchConfig(state_, catalog_);
    save();
    panelDirty_ = true;
}

void RocketGameApp::enterArrivalOrbit()
{
    if (state_.screen != Screen::ArrivalOps || !canEnterArrivalOrbit(state_, catalog_)) {
        state_.statusLine = arrivalOperationBlockReason(state_, catalog_, "orbit");
        panelDirty_ = true;
        return;
    }

    completeArrivalOrbit(state_, catalog_);
    if (shouldOpenPostArrivalPhases(state_.lastOutcome, catalog_)) {
        generateResearchProjects(state_, catalog_, rng_);
        state_.screen = Screen::Research;
        state_.statusLine = std::string(text::status::researchWindowOpened);
    } else {
        generateModuleOffers(state_, catalog_, rng_);
        state_.screen = Screen::Upgrade;
        state_.statusLine = std::string(text::status::orbitCompleted);
    }
    syncLaunchConfig(state_, catalog_);
    save();
    panelDirty_ = true;
}

void RocketGameApp::attemptArrivalLanding()
{
    if (state_.screen != Screen::ArrivalOps || !canAttemptArrivalLanding(state_, catalog_)) {
        state_.statusLine = arrivalOperationBlockReason(state_, catalog_, "landing");
        panelDirty_ = true;
        return;
    }

    state_.statusLine = std::string(text::status::landingCommitted);
    beginSurfaceExpeditionOrRefit();
    syncLaunchConfig(state_, catalog_);
    save();
    panelDirty_ = true;
}

void RocketGameApp::selectResearchProject(int index)
{
    if (state_.screen != Screen::Research) {
        return;
    }

    const ResearchOutcome outcome = completeResearchProject(state_, catalog_, index);
    if (!outcome.completed) {
        panelDirty_ = true;
        return;
    }

    const std::string researchSummary = researchOutcomeSummary(outcome);
    beginSurfaceExpeditionOrRefit();
    state_.statusLine = researchSummary;
    save();
    panelDirty_ = true;
}

void RocketGameApp::skipResearch()
{
    if (state_.screen != Screen::Research) {
        return;
    }

    beginSurfaceExpeditionOrRefit();
    state_.statusLine = std::string(text::status::researchSkipped);
    save();
    panelDirty_ = true;
}

void RocketGameApp::surveySurface()
{
    if (state_.screen != Screen::SurfaceExpedition) {
        return;
    }

    const SurfaceActionOutcome outcome = surveySurfaceSite(state_, rng_);
    if (outcome.applied) {
        generateSurfaceUpgradeOffers(state_, catalog_, rng_);
    }
    state_.statusLine = surfaceActionSummary(outcome);
    save();
    panelDirty_ = true;
}

void RocketGameApp::mineSurface()
{
    if (state_.screen != Screen::SurfaceExpedition) {
        return;
    }

    const SurfaceActionOutcome outcome = startMiningRun(state_, catalog_);
    state_.statusLine = outcome.applied ? std::string(text::status::miningStarted) : surfaceActionSummary(outcome);
    save();
    panelDirty_ = true;
}

void RocketGameApp::pushSurface()
{
    if (state_.screen != Screen::SurfaceExpedition) {
        return;
    }

    const SurfaceActionOutcome outcome = pushSurfaceDeeper(state_, rng_);
    if (outcome.applied) {
        generateSurfaceUpgradeOffers(state_, catalog_, rng_);
    }
    state_.statusLine = surfaceActionSummary(outcome);
    save();
    panelDirty_ = true;
}

void RocketGameApp::extractSurface()
{
    if (state_.screen != Screen::SurfaceExpedition) {
        return;
    }

    const SurfaceActionOutcome outcome = extractSurfacePayload(state_, rng_);
    if (!outcome.applied) {
        panelDirty_ = true;
        return;
    }

    state_.statusLine = surfaceActionSummary(outcome);
    generateModuleOffers(state_, catalog_, rng_);
    state_.screen = Screen::Upgrade;
    save();
    panelDirty_ = true;
}

void RocketGameApp::selectSurfaceUpgrade(int index)
{
    if (state_.screen != Screen::SurfaceExpedition) {
        return;
    }

    if (chooseSurfaceUpgrade(state_, catalog_, index)) {
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::openDroneOps()
{
    if (state_.screen != Screen::SurfaceExpedition || !state_.run.surfaceExpedition.active || !droneBayUnlocked(state_)) {
        state_.statusLine = "Research Drone Bay before assigning helper drones.";
        panelDirty_ = true;
        return;
    }

    ensureDroneBayState(state_, catalog_);
    state_.screen = Screen::DroneOps;
    state_.statusLine = "Choose helper drones for the next mining run.";
    save();
    panelDirty_ = true;
}

void RocketGameApp::backToSurfaceOps()
{
    if (state_.screen != Screen::DroneOps) {
        return;
    }

    state_.screen = state_.run.surfaceExpedition.active ? Screen::SurfaceExpedition : Screen::Hangar;
    save();
    panelDirty_ = true;
}

void RocketGameApp::equipDrone(int index)
{
    if (state_.screen != Screen::DroneOps) {
        return;
    }

    if (equipMiniDrone(state_, catalog_, index)) {
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::upgradeDroneSlot()
{
    if (state_.screen != Screen::DroneOps) {
        return;
    }

    if (::rocket::upgradeDroneSlot(state_, catalog_)) {
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::miningMove(double xAxis, double yAxis)
{
    setMiningMove(state_, xAxis, yAxis);
}

void RocketGameApp::miningAim(double normalizedX, double normalizedY)
{
    setMiningAim(state_, normalizedX, normalizedY);
}

void RocketGameApp::miningDrill(bool active)
{
    setMiningDrilling(state_, active);
}

void RocketGameApp::miningScanner()
{
    if (state_.screen != Screen::Mining) {
        return;
    }

    pulseMiningScanner(state_, catalog_);
    state_.statusLine = "Scanner pulse widened the terrain readout.";
    panelDirty_ = true;
}

void RocketGameApp::miningStow()
{
    if (state_.screen != Screen::Mining) {
        return;
    }

    const SurfaceActionOutcome outcome = finishMiningRun(state_, catalog_, false);
    if (outcome.applied) {
        generateSurfaceUpgradeOffers(state_, catalog_, rng_);
    }
    state_.statusLine = outcome.applied ? surfaceActionSummary(outcome) : std::string(text::status::miningStowed);
    save();
    panelDirty_ = true;
}

namespace {

bool hasRecoveredSurfacePayload(const SurfaceExpeditionState& expedition)
{
    return expedition.cargo > 0
        || expedition.temporaryMaterials.common > 0
        || expedition.temporaryMaterials.rare > 0
        || expedition.temporaryMaterials.exotic > 0
        || !expedition.temporaryArtifacts.empty();
}

} // namespace

void RocketGameApp::miningAbort()
{
    if (state_.screen != Screen::Mining) {
        return;
    }

    const SurfaceActionOutcome outcome = finishMiningRun(state_, catalog_, true);
    if (outcome.applied && hasRecoveredSurfacePayload(state_.run.surfaceExpedition)) {
        generateSurfaceUpgradeOffers(state_, catalog_, rng_);
    }
    state_.statusLine = outcome.applied ? surfaceActionSummary(outcome) : std::string(text::status::miningAborted);
    save();
    panelDirty_ = true;
}

void RocketGameApp::miningFailureAck()
{
    if (state_.screen != Screen::Mining || !state_.run.mining.failurePending) {
        return;
    }

    const SurfaceActionOutcome outcome = finishMiningRun(state_, catalog_, true);
    if (outcome.applied && hasRecoveredSurfacePayload(state_.run.surfaceExpedition)) {
        generateSurfaceUpgradeOffers(state_, catalog_, rng_);
    }
    state_.statusLine = outcome.applied ? surfaceActionSummary(outcome) : std::string(text::status::miningAborted);
    save();
    panelDirty_ = true;
}

void RocketGameApp::attemptFrontierTransfer()
{
    if (state_.screen != Screen::Hangar) {
        return;
    }

    if (state_.run.shipDamage >= tuning::damage::destroyedShipDamage) {
        state_.statusLine = std::string(text::status::launchHullBlocked);
        refreshPanel();
        return;
    }

    if (activeAstronaut(state_) == nullptr) {
        state_.statusLine = std::string(text::status::launchCrewBlocked);
        refreshPanel();
        return;
    }

    if (!canCommitToNextFrontier(state_, catalog_)) {
        const Destination* next = nextDestination(state_, catalog_);
        state_.statusLine = next == nullptr ? std::string(text::status::noFartherFrontier) : std::string(text::status::moreProvingDataBeforeTransfer);
        refreshPanel();
        return;
    }

    const Destination* next = nextDestination(state_, catalog_);
    if (next == nullptr) {
        state_.statusLine = std::string(text::status::noFartherFrontier);
        refreshPanel();
        return;
    }

    state_.launchConfig.frontierTransfer = true;
    state_.launchConfig.destinationId = next->id;
    state_.launchConfig.burnGoalMultiplier = next->targetMultiplier;
    beginLaunchSession(rocket::prepareLaunch(state_, catalog_, rng_));
    state_.screen = Screen::Launch;
    state_.statusLine = std::string(text::status::preflightReady);
    refreshPanel();
}

void RocketGameApp::openNavigation()
{
    if (!navigationAvailable(state_)) {
        state_.statusLine = "Navigation opens once the Ark is stranded in a new system.";
        refreshPanel();
        return;
    }

    state_.screen = Screen::Navigation;
    state_.statusLine = "Choose the next shuttle sortie from the Ark.";
    save();
    refreshPanel();
}

void RocketGameApp::arkJump()
{
    if (!arkDiscovered(state_) || hostileSystemActive(state_)) {
        state_.statusLine = "The Ark cannot jump right now.";
        refreshPanel();
        return;
    }

    if (performArkJump(state_, catalog_)) {
        save();
    }
    refreshPanel();
}

void RocketGameApp::selectNavigationDestination(int index)
{
    if (state_.screen != Screen::Navigation) {
        return;
    }

    if (rocket::selectNavigationDestination(state_, catalog_, index)) {
        save();
    }
    refreshPanel();
}

void RocketGameApp::buyOffer(int index)
{
    if (state_.screen != Screen::Upgrade) {
        return;
    }
    if (rocket::buyOffer(state_, catalog_, index)) {
        state_.screen = navigationAvailable(state_) ? Screen::Navigation : Screen::Hangar;
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

void RocketGameApp::recruitCrew(int candidateIndex)
{
    if (rocket::recruitCrew(state_, catalog_, candidateIndex)) {
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
    outcome.telemetry = chartTelemetryForOutcome(flightModel, catalog_, burnMultiplier, wasReturningHome, frozenTravelProgress);
    applyLaunchOutcome(state_, catalog_, outcome);
    if (shouldOpenArrivalOps(outcome, catalog_)) {
        startArrivalOps(state_, outcome);
        beginArrivalFanfare();
    } else {
        state_.screen = Screen::Results;
    }
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
        session_.flightArmed,
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
    result.animationTime = state_.screen == Screen::Launch
        ? session_.elapsed
        : (state_.screen == Screen::Mining ? state_.run.mining.elapsedSeconds :
            (state_.screen == Screen::ArrivalFanfare ? session_.arrivalFanfare.elapsed : session_.result.elapsed));
    const Destination& currentFrontier = currentDestination(state_, catalog_);
    const Destination* visualDestination = &currentFrontier;
    if (state_.screen == Screen::Launch) {
        if (const Destination* activeDestination = catalog_.findDestination(session_.preparedLaunch.config.destinationId)) {
            visualDestination = activeDestination;
        }
        result.frontierTransfer = session_.preparedLaunch.config.frontierTransfer;
    } else if (state_.screen == Screen::Results || state_.screen == Screen::ArrivalFanfare || state_.screen == Screen::ArrivalOps) {
        if (const Destination* resultDestination = catalog_.findDestination(state_.lastOutcome.destinationId)) {
            visualDestination = resultDestination;
        }
        result.frontierTransfer = state_.lastOutcome.frontierTransfer;
    }
    result.targetMultiplier = visualDestination->targetMultiplier;
    if (state_.screen == Screen::Launch && !session_.flightArmed) {
        result.travelProgress = 0.0;
    } else if (session_.controls.actions.returningHome) {
        result.travelProgress = flight_progress::returnTravelProgress(
            session_.returnTrip.startTravelProgress,
            session_.returnTrip.elapsed,
            session_.returnTrip.duration);
        result.returningHome = true;
        result.returnTurnProgress = std::clamp(session_.returnTrip.elapsed / tuning::session::returnTurnSeconds, 0.0, 1.0);
    } else if (state_.screen == Screen::ArrivalFanfare) {
        result.travelProgress = 0.985;
    } else if (state_.screen == Screen::ArrivalOps) {
        result.travelProgress = 1.0;
    } else if (state_.screen == Screen::Results && session_.result.usesTravelProgress) {
        result.travelProgress = session_.result.travelProgress;
    } else {
        result.travelProgress = flight_progress::travelProgressForBurn(session_.currentMultiplier, *visualDestination);
    }
    result.shipDamage = static_cast<double>(state_.run.shipDamage);
    result.destinationTier = visualDestination->tier;
    result.currentFrontierTier = currentFrontier.tier;

    if (state_.screen == Screen::Mining && state_.run.mining.active) {
        const MiningRunState& mining = state_.run.mining;
        result.miningWidth = mining.terrain.width;
        result.miningHeight = mining.terrain.height;
        result.miningDroneX = mining.droneX;
        result.miningDroneY = mining.droneY;
        result.miningTargetX = mining.targetTipX;
        result.miningTargetY = mining.targetTipY;
        result.miningDrillDirX = mining.aimDirX;
        result.miningDrillDirY = mining.aimDirY;
        result.miningHeat = mining.drillHeat;
        result.miningContactIntensity = mining.contactIntensity;
        result.miningScannerPulse = mining.scannerPulseSeconds;
        result.miningFailurePulse = mining.failurePending ? std::max(0.25, std::clamp(mining.failureSeconds / 1.5, 0.0, 1.0)) : 0.0;
        result.miningRecoilX = mining.recoilX;
        result.miningRecoilY = mining.recoilY;
        const MiningCell* target = miningCellAt(mining.terrain, mining.targetCellX, mining.targetCellY);
        const bool targetDrillable = target != nullptr && miningMaterialSolid(target->material) && target->material != MiningCellMaterial::Bedrock;
        const double pressureX = -mining.recoilX;
        const double pressureY = -mining.recoilY;
        const double movePressure = mining.moveX * pressureX + mining.moveY * pressureY;
        const bool activelyPressingContact = (mining.drilling && targetDrillable) || movePressure > 0.20;
        result.miningBounce = activelyPressingContact ? mining.contactBounce : 0.0;
        result.miningInputDrilling = mining.drilling;
        result.miningTargetDrillable = targetDrillable;
        result.miningDrilling = mining.drilling && targetDrillable;
        result.miningCells.reserve(mining.terrain.cells.size());
        for (int y = 0; y < mining.terrain.height; ++y) {
            for (int x = 0; x < mining.terrain.width; ++x) {
                const std::size_t index = static_cast<std::size_t>(y * mining.terrain.width + x);
                if (index >= mining.terrain.cells.size()) {
                    continue;
                }
                const MiningCell& cell = mining.terrain.cells[index];
                const double integrity = cell.maxToughness <= 0.0 ? 1.0 : std::clamp(cell.remainingToughness / cell.maxToughness, 0.0, 1.0);
                result.miningCells.push_back({
                    x,
                    y,
                    static_cast<int>(cell.material),
                    integrity,
                    cell.revealed,
                    cell.hazard
                });
            }
        }
    }

    if (state_.screen == Screen::Launch) {
        if (!session_.flightArmed) {
            result.telemetryCount = 0;
            result.poweredFlight = false;
            result.launchShake = 0.0;
            return result;
        }

        const double displayedMultiplier = liveBurnMultiplier();
        const TelemetryEvent event = telemetryAt(flightModel, displayedMultiplier);
        result.heat = event.heat;
        result.warning = event.warning;
        const int sampleCapacity = static_cast<int>(result.telemetry.size());
        double plotProgress = std::clamp(result.travelProgress, 0.0, 1.0);
        double sampleCeiling = std::max(session_.currentMultiplier, result.targetMultiplier);
        if (session_.controls.actions.returningHome) {
            const double outboundProgress = std::clamp(session_.returnTrip.startTravelProgress, 0.0, 1.0);
            const double returnProgress = flight_progress::returnCompletion(
                session_.returnTrip.elapsed,
                session_.returnTrip.duration);
            plotProgress = std::clamp(
                outboundProgress + (1.0 - outboundProgress) * returnProgress,
                0.0,
                1.0);
            sampleCeiling = std::max(session_.returnTrip.burnMultiplier, result.targetMultiplier);
        }
        const int liveSampleCount = std::clamp(
            static_cast<int>(std::ceil(plotProgress * static_cast<double>(sampleCapacity - 1))) + 1,
            2,
            sampleCapacity);
        for (int i = 0; i < liveSampleCount; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(sampleCapacity - 1);
            const double sampleMultiplier = 1.0 + (sampleCeiling - 1.0) * t;
            const TelemetryEvent sample = telemetryAt(flightModel, sampleMultiplier);
            result.telemetry[static_cast<std::size_t>(i)] = sample.warning;
            result.heatTelemetry[static_cast<std::size_t>(i)] = std::clamp(sample.heat, 0.0, 1.0);
        }
        result.telemetryCount = liveSampleCount;
        result.poweredFlight = session_.flightArmed && (session_.controls.actions.returningHome
            ? !session_.controls.returnDriftHome
            : !session_.controls.actions.cutEnginesActive);
        result.launchShake = session_.flightArmed
            ? std::clamp(1.0 - session_.elapsed / tuning::session::launchShakeSeconds, 0.0, 1.0)
            : 0.0;
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
