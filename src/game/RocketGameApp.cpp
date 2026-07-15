#include "game/RocketGameApp.h"

#include "core/FlightProgress.h"
#include "core/ContentIds.h"
#include "core/GameUi.h"
#include "core/GameText.h"
#include "core/LaunchStatus.h"
#include "core/MiningSystem.h"
#include "core/ResearchSystem.h"
#include "core/Tuning.h"
#include "core/SaveData.h"
#include "game/GamePanel.h"
#include "platform/WebSaveStore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace rocket {
namespace {

int destinationIndexForId(const ContentCatalog& catalog, std::string_view destinationId);

struct DebugActOneCheckpoint {
    std::string_view label;
    std::string_view destinationId;
};

constexpr std::array<DebugActOneCheckpoint, 7> kDebugActOneCheckpoints {{
    {"Earth Orbit / Proving Mission", content::destination::earthOrbit},
    {"Moon", content::destination::moon},
    {"Mars", content::destination::mars},
    {"Jupiter", content::destination::outerPlanets},
    {"Saturn", content::destination::outerPlanets},
    {"Uranus", content::destination::outerPlanets},
    {"Neptune / Straylight", content::destination::outerPlanets}
}};

void addDebugUnlock(GameState& state, const char* unlockKey)
{
    if (!hasUnlock(state.meta, unlockKey)) {
        state.meta.unlockKeys.push_back(unlockKey);
    }
}

bool miningDroneTransferEnabled(const GameState& state)
{
    return hasUnlock(state.meta, content::unlock::surfaceDrills);
}

LaunchOutcome debugTransferOutcome(std::string destinationId)
{
    LaunchOutcome outcome;
    outcome.type = LaunchResultType::MissionComplete;
    outcome.recoveryMethod = RecoveryMethod::TransferArrival;
    outcome.destinationId = std::move(destinationId);
    outcome.assignedAstronautId = content::astronaut::ava;
    outcome.frontierTransfer = true;
    outcome.crashMultiplier = 2.2;
    outcome.ejectMultiplier = 2.4;
    outcome.payout = 260.0;
    outcome.blueprintGain = 3;
    outcome.peakWarning = 0.38;
    outcome.peakAbortRisk = 0.24;
    outcome.telemetry = {
        {1.0, 0.12, 0.10, 0.05, 0.92, 0.86, 0.04, 0.10, 0.12, "Debug launch nominal."},
        {1.7, 0.32, 0.24, 0.20, 0.72, 0.70, 0.12, 0.18, 0.28, "Transfer burn committed."},
        {2.4, 0.46, 0.36, 0.30, 0.44, 0.62, 0.24, 0.26, 0.38, "Arrival corridor reached."}
    };
    return outcome;
}

void seedDebugResearchAccess(GameState& state)
{
    addDebugUnlock(state, content::unlock::starter);
    addDebugUnlock(state, content::unlock::thermal);
    addDebugUnlock(state, content::unlock::recovery);
    addDebugUnlock(state, content::unlock::deepSpace);
    addDebugUnlock(state, content::unlock::surfaceProbes);
    addDebugUnlock(state, content::unlock::surfaceDrills);
    addDebugUnlock(state, content::unlock::cargoRigs);
    addDebugUnlock(state, content::unlock::analysisLab);
    addDebugUnlock(state, content::unlock::droneBay);
    addDebugUnlock(state, content::unlock::perimeterDrones);
    state.meta.materials.common = std::max(state.meta.materials.common, 18);
    state.meta.materials.rare = std::max(state.meta.materials.rare, 8);
    state.meta.materials.exotic = std::max(state.meta.materials.exotic, 2);
    state.meta.blueprintProgress = std::max(state.meta.blueprintProgress, 7);
}

void seedDebugDroneBay(GameState& state, const ContentCatalog& catalog)
{
    addDebugUnlock(state, content::unlock::droneBay);
    addDebugUnlock(state, content::unlock::perimeterDrones);
    state.meta.droneBaySlots = 6;
    state.meta.ownedDroneIds = {
        content::drone::miningDrone,
        content::drone::resourceDrone,
        content::drone::surveyDrone,
        content::drone::hazardDrone,
        content::drone::attackDrone,
        content::drone::defenseDrone
    };
    state.meta.equippedDroneIds.clear();
    ensureDroneBayState(state, catalog);
}

void seedDebugSurfaceExpedition(GameState& state, const ContentCatalog& catalog, Random& rng, std::string_view destinationId)
{
    state.run.destinationIndex = destinationIndexForId(catalog, destinationId);
    state.run.arrivalOps = {true, std::string(destinationId)};
    startSurfaceExpedition(state, catalog, &rng);
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    expedition.cargo = std::max(expedition.cargo, 2);
    expedition.temporaryMaterials.common = std::max(expedition.temporaryMaterials.common, 3);
    expedition.temporaryMaterials.rare = std::max(expedition.temporaryMaterials.rare, 1);
    expedition.prospectMaterials.common = std::max(expedition.prospectMaterials.common, 4);
    expedition.prospectMaterials.rare = std::max(expedition.prospectMaterials.rare, 2);
    expedition.prospectArtifacts = std::max(expedition.prospectArtifacts, 1);
    expedition.miningSitePrepared = true;
}

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

bool consumeIndexedAction(std::string_view action, std::string_view prefix, int& index)
{
    if (action.substr(0, prefix.size()) != prefix) {
        return false;
    }

    const std::string_view raw = action.substr(prefix.size());
    if (raw.empty()) {
        return false;
    }

    int value = 0;
    for (const char c : raw) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }

    index = value;
    return true;
}

int destinationIndexForId(const ContentCatalog& catalog, std::string_view destinationId)
{
    for (std::size_t i = 0; i < catalog.destinations.size(); ++i) {
        if (catalog.destinations[i].id == destinationId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void setDestinationHistory(
    std::vector<int>& values,
    const ContentCatalog& catalog,
    std::string_view destinationId,
    int count)
{
    const int index = destinationIndexForId(catalog, destinationId);
    if (index < 0) {
        return;
    }
    if (values.size() < catalog.destinations.size()) {
        values.resize(catalog.destinations.size(), 0);
    }
    values[static_cast<std::size_t>(index)] = count;
}

void appendProspectMarkers(
    std::vector<MiningCellMaterial>& markers,
    std::vector<int>& depthOffsets,
    const SurfaceDepthProspect& prospect)
{
    auto append = [&](MiningCellMaterial material, int count) {
        for (int i = 0; i < std::max(0, count); ++i) {
            markers.push_back(material);
            depthOffsets.push_back(std::max(0, prospect.depthOffset));
        }
    };
    append(MiningCellMaterial::CommonOre, prospect.possibleMaterials.common);
    append(MiningCellMaterial::RareOre, prospect.possibleMaterials.rare);
    append(MiningCellMaterial::ExoticVein, prospect.possibleMaterials.exotic);
    append(MiningCellMaterial::ArtifactCache, prospect.possibleArtifacts);
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
    session_.preflightElapsed = miningDroneTransferEnabled(state_)
        ? 0.0
        : tuning::session::preflightBoardingSeconds;
    session_.elapsed = 0.0;
    session_.currentMultiplier = 1.0;
    session_.peakWarning = 0.0;
    session_.peakAbortRisk = 0.0;
    clearFlightControls();
    clearResultView();
    session_.arrivalFanfare = {};
}

void RocketGameApp::consumeNextLaunchBoost()
{
    state_.run.nextLaunchFuelBoost = 0.0;
    state_.run.nextLaunchSpeedBoost = 0.0;
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

void RocketGameApp::loadSavedGameOrDefault()
{
    debugActOneCheckpoint_ = -1;
    state_ = createNewGame(catalog_, 0x524F434B45544ULL);
    rng_ = Random(state_.seed);
    session_ = {};
    panelDirty_ = true;

    if (const auto saveData = deserializeSaveData(loadBrowserSave())) {
        restoreSaveData(state_, catalog_, *saveData);
        rng_ = Random(saveData->seed + 0xA51CE5ULL + static_cast<std::uint64_t>(saveData->blueprintProgress));
        state_.statusLine = std::string(text::status::saveRestored);
    }

    ensureDroneBayState(state_, catalog_);
    migrateLegacyDeepSpaceFrontier(state_, catalog_);
    syncLaunchConfig(state_, catalog_);
}

void RocketGameApp::beginDebugSandbox(const std::string& statusLine)
{
    debugSessionActive_ = true;
    debugActOneCheckpoint_ = -1;
    state_ = createNewGame(catalog_, 0xD36B6D3BU);
    rng_ = Random(state_.seed ^ 0x51A7E5ULL);
    session_ = {};
    ensureDroneBayState(state_, catalog_);
    syncLaunchConfig(state_, catalog_);
    clearResearchAndExpeditionState(state_);
    state_.statusLine = statusLine;
}

void RocketGameApp::seedDebugDroneLoadout()
{
    seedDebugDroneBay(state_, catalog_);
}

void RocketGameApp::captureDebugDroneLoadout()
{
    if (!debugSessionActive_) {
        return;
    }
    debugDroneLoadout_.configured = true;
    debugDroneLoadout_.equippedDroneIds = state_.meta.equippedDroneIds;
    debugDroneLoadout_.droneUpgrades = state_.meta.droneUpgrades;
}

void RocketGameApp::applyDebugDroneLoadout()
{
    seedDebugDroneLoadout();
    if (!debugDroneLoadout_.configured) {
        return;
    }
    state_.meta.equippedDroneIds = debugDroneLoadout_.equippedDroneIds;
    state_.meta.droneUpgrades = debugDroneLoadout_.droneUpgrades;
    ensureDroneBayState(state_, catalog_);
}

bool RocketGameApp::initialize()
{
    catalog_ = createDefaultContent();
    loadSavedGameOrDefault();

    if (!renderer_.initialize()) {
        state_.statusLine = std::string(text::status::webglFailed);
    }
    rmlUi_.initialize([this](const std::string& action) {
        runUiAction(action);
    });

    refreshPanel();
    return true;
}

int RocketGameApp::currentScreen() const
{
    return static_cast<int>(state_.screen);
}

void RocketGameApp::tick(double deltaSeconds)
{
    visualTimeSeconds_ += std::clamp(deltaSeconds, 0.0, 0.25);

    if (state_.screen == Screen::Launch) {
        if (!session_.flightArmed) {
            const bool wasReady = session_.preflightElapsed >= tuning::session::preflightBoardingSeconds;
            session_.preflightElapsed = std::min(
                session_.preflightElapsed + std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds),
                tuning::session::preflightBoardingSeconds);
            if (!wasReady && session_.preflightElapsed >= tuning::session::preflightBoardingSeconds) {
                state_.statusLine = std::string(text::status::preflightReady);
                refreshPanel();
            }
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
                    arkDiscovered(state_),
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
                    arkDiscovered(state_),
                    session_.controls.returnDriftHome,
                    pastDataGoal,
                    0.0
                });
            }
        }

        panelDirty_ = true;
    } else if (state_.screen == Screen::Mining) {
        if (miningExtraction_.active) {
            miningExtraction_.elapsed = std::min(
                miningExtraction_.elapsed + std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds),
                tuning::mining::miningExtractionSequenceSeconds);
            if (miningExtraction_.elapsed >= tuning::mining::miningExtractionSequenceSeconds) {
                const bool showUpgradeDraft = miningExtraction_.showUpgradeDraft;
                miningExtraction_ = {};
                state_.run.mining = {};
                state_.screen = showUpgradeDraft ? Screen::SurfaceUpgrade : Screen::SurfaceExpedition;
            }
        } else {
            const bool wasActive = state_.run.mining.active;
            updateMiningRun(state_, catalog_, deltaSeconds);
            if (wasActive && !state_.run.mining.active) {
                state_.statusLine = std::string(text::status::miningAborted);
                save();
            }
        }
        panelDirty_ = true;
    } else if (state_.screen == Screen::Flyby) {
        const bool wasCompleted = state_.run.flyby.completed;
        updateFlybyRun(state_, deltaSeconds);
        if (!wasCompleted && state_.run.flyby.completed) {
            switch (state_.run.flyby.result) {
            case FlybyGrade::Perfect:
                state_.statusLine = "Perfect slingshot. Next launch gets fuel margin and speed.";
                break;
            case FlybyGrade::Good:
                state_.statusLine = "Clean flyby. Recon data secured.";
                break;
            case FlybyGrade::Miss:
            default:
                state_.statusLine = "Missed flyby window. Approach options remain open.";
                break;
            }
            save();
        }
        panelDirty_ = true;
    } else if (state_.screen == Screen::Orbit) {
        const bool wasCompleted = state_.run.orbit.completed;
        updateOrbitRun(state_, deltaSeconds);
        if (!wasCompleted && state_.run.orbit.completed) {
            switch (state_.run.orbit.result) {
            case OrbitGrade::Perfect:
                state_.statusLine = "Perfect orbit plotted. Research run ready to stamp.";
                break;
            case OrbitGrade::Good:
                state_.statusLine = "Stable orbit completed. Research data ready to stamp.";
                break;
            case OrbitGrade::Miss:
            default:
                state_.statusLine = "Orbit window missed. Fuel and time spent, no research banked.";
                break;
            }
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
    rmlUi_.render();
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
    consumeNextLaunchBoost();
    state_.screen = Screen::Launch;
    state_.statusLine = miningDroneTransferEnabled(state_)
        ? std::string(text::status::droneStowing)
        : std::string(text::status::preflightReadyWithoutDrone);
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

    if (session_.preflightElapsed < tuning::session::preflightBoardingSeconds) {
        state_.statusLine = std::string(text::status::droneStowing);
        refreshPanel();
        return;
    }

    session_.flightArmed = true;
    state_.statusLine = session_.preparedLaunch.config.frontierTransfer
        ? std::string(text::status::transferBurnStarted)
        : text::status::provingBurnStartedForHome(arkDiscovered(state_));
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
        ? text::status::fuelReserveGoneForHome(arkDiscovered(state_))
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
    } else if (state_.screen == Screen::SurfaceUpgrade) {
        state_.run.surfaceExpedition.surfaceUpgradeOfferIds = {};
        state_.run.surfaceExpedition.surfaceUpgradeOfferAvailable = false;
        state_.screen = Screen::SurfaceExpedition;
        state_.statusLine = "Field upgrade skipped. Keep digging or extract while the window holds.";
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

    startArrivalFlybyRun(state_, catalog_);
    state_.statusLine = "Manual flyby started. Stay in the approach corridor.";
    syncLaunchConfig(state_, catalog_);
    save();
    panelDirty_ = true;
}

void RocketGameApp::flybyMove(double xAxis, double yAxis)
{
    if (state_.screen != Screen::Flyby) {
        return;
    }
    setFlybyMove(state_, xAxis, yAxis);
}

void RocketGameApp::flybyAbort()
{
    if (state_.screen != Screen::Flyby || state_.run.flyby.completed) {
        return;
    }
    abortFlybyRun(state_);
    state_.statusLine = "Flyby aborted. No recon reward earned.";
    save();
    panelDirty_ = true;
}

void RocketGameApp::flybyContinue()
{
    if (state_.screen != Screen::Flyby || !state_.run.flyby.completed) {
        return;
    }

    const FlybyGrade grade = state_.run.flyby.result;
    completeFlybyRun(state_, catalog_);
    switch (grade) {
    case FlybyGrade::Perfect:
        state_.statusLine = "Perfect slingshot banked. Choose the next approach.";
        break;
    case FlybyGrade::Good:
        state_.statusLine = "Flyby data banked. Choose the next approach.";
        break;
    case FlybyGrade::Miss:
    default:
        state_.statusLine = "Missed window. Choose another approach or try again.";
        break;
    }
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

    startArrivalOrbitRun(state_, catalog_);
    state_.statusLine = "Orbital insertion started. Use prograde and radial corrections to stay in the research band.";
    syncLaunchConfig(state_, catalog_);
    save();
    panelDirty_ = true;
}

void RocketGameApp::orbitMove(double xAxis, double yAxis)
{
    if (state_.screen != Screen::Orbit) {
        return;
    }
    setOrbitMove(state_, xAxis, yAxis);
}

void RocketGameApp::orbitAbort()
{
    if (state_.screen != Screen::Orbit || state_.run.orbit.completed) {
        return;
    }
    abortOrbitRun(state_);
    state_.statusLine = "Orbit insertion aborted. No research reward earned.";
    save();
    panelDirty_ = true;
}

void RocketGameApp::orbitContinue()
{
    if (state_.screen != Screen::Orbit || !state_.run.orbit.completed) {
        return;
    }

    const OrbitGrade grade = state_.run.orbit.result;
    completeOrbitRun(state_, catalog_);
    switch (grade) {
    case OrbitGrade::Perfect:
        state_.statusLine = "Perfect orbit research banked. Choose the next approach.";
        break;
    case OrbitGrade::Good:
        state_.statusLine = "Orbit research banked. Choose the next approach.";
        break;
    case OrbitGrade::Miss:
    default:
        state_.statusLine = "Missed orbit. Choose another approach or try again.";
        break;
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
    bankArrivalLandingFlightData(state_, catalog_);
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

    const SurfaceActionOutcome outcome = startSurfaceScanRun(state_, rng_);
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

    const SurfaceActionOutcome outcome = startSurfacePushRun(state_, rng_);
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
    if (state_.screen != Screen::SurfaceUpgrade) {
        return;
    }

    if (chooseSurfaceUpgrade(state_, catalog_, index)) {
        state_.screen = Screen::SurfaceExpedition;
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
        captureDebugDroneLoadout();
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::unequipDroneSlot(int slotIndex)
{
    if (state_.screen != Screen::DroneOps) {
        return;
    }

    if (unequipMiniDroneSlot(state_, catalog_, slotIndex)) {
        captureDebugDroneLoadout();
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::upgradeDrone(int index)
{
    if (state_.screen != Screen::DroneOps) {
        return;
    }

    if (::rocket::upgradeMiniDrone(state_, catalog_, index)) {
        captureDebugDroneLoadout();
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
        captureDebugDroneLoadout();
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::miningMove(double xAxis, double yAxis)
{
    if (miningExtraction_.active) {
        return;
    }
    setMiningMove(state_, xAxis, yAxis);
}

void RocketGameApp::miningAim(double normalizedX, double normalizedY)
{
    if (miningExtraction_.active) {
        return;
    }
    setMiningAim(state_, normalizedX, normalizedY);
}

void RocketGameApp::miningDrill(bool active)
{
    if (miningExtraction_.active) {
        return;
    }
    setMiningDrilling(state_, active);
}

void RocketGameApp::miningScanner()
{
    if (state_.screen != Screen::Mining || miningExtraction_.active) {
        return;
    }

    pulseMiningScanner(state_, catalog_);
    state_.statusLine = "Scanner pulse widened the terrain readout.";
    panelDirty_ = true;
}

void RocketGameApp::miningTether()
{
    if (state_.screen != Screen::Mining || miningExtraction_.active) {
        return;
    }

    const MiningArtifactObject before = state_.run.mining.artifact;
    toggleMiningTether(state_);
    const MiningArtifactObject& artifact = state_.run.mining.artifact;
    if (!artifact.present || artifact.state == MiningArtifactState::Delivered || artifact.state == MiningArtifactState::Destroyed) {
        state_.statusLine = "No recoverable artifact tether target.";
    } else if (artifact.tethered) {
        state_.statusLine = "Artifact tether locked. Pull it free and bring it to the ship bay.";
    } else if (before.tethered) {
        state_.statusLine = "Artifact tether released.";
    } else if (!artifact.revealed && artifact.state == MiningArtifactState::Embedded) {
        state_.statusLine = "Expose the artifact before tethering it.";
    } else {
        state_.statusLine = "Move closer to the exposed artifact to tether it.";
    }
    panelDirty_ = true;
}

void RocketGameApp::miningRepairDrill()
{
    if (state_.screen != Screen::Mining || miningExtraction_.active) {
        return;
    }
    MiningRunState& mining = state_.run.mining;
    const int cost = miningDrillRepairCost(mining);
    if (!miningAtReturnZone(mining)) {
        state_.statusLine = "Return to the ship to repair the drill bit.";
    } else if (cost <= 0) {
        state_.statusLine = "Drill bit integrity is already full.";
    } else if (mining.stowedMaterials.common < cost) {
        state_.statusLine = "Need " + std::to_string(cost) + " banked common materials to repair the drill bit.";
    } else if (repairMiningDrill(state_)) {
        state_.statusLine = "Drill bit repaired for " + std::to_string(cost) + " common materials.";
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::miningRepairDrone()
{
    if (state_.screen != Screen::Mining || miningExtraction_.active) {
        return;
    }
    MiningRunState& mining = state_.run.mining;
    const int cost = miningDroneRepairCost(mining);
    if (!miningAtReturnZone(mining)) {
        state_.statusLine = "Return to the ship to repair the mining drone.";
    } else if (cost <= 0) {
        state_.statusLine = "Mining drone health is already full.";
    } else if (mining.stowedMaterials.common < cost) {
        state_.statusLine = "Need " + std::to_string(cost) + " banked common materials to repair the mining drone.";
    } else if (repairMiningDrone(state_)) {
        state_.statusLine = "Mining drone repaired for " + std::to_string(cost) + " common materials.";
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::miningStow()
{
    if (state_.screen != Screen::Mining || miningExtraction_.active) {
        return;
    }
    if (!miningAtReturnZone(state_.run.mining)) {
        state_.statusLine = std::string(text::status::miningReturnToShip);
        panelDirty_ = true;
        return;
    }

    const MiningRunState extractionVisual = state_.run.mining;
    const SurfaceActionOutcome outcome = finishMiningRun(state_, catalog_, false);
    if (outcome.applied) {
        generateSurfaceUpgradeOffers(state_, catalog_, rng_);
        state_.run.mining = extractionVisual;
        state_.run.mining.active = false;
        state_.run.mining.drilling = false;
        state_.run.mining.moveX = 0.0;
        state_.run.mining.moveY = 0.0;
        state_.run.mining.cargo = 0;
        state_.run.mining.temporaryMaterials = {};
        state_.run.mining.temporaryArtifacts.clear();
        state_.run.mining.stowedCargo = outcome.cargoDelta;
        state_.run.mining.stowedMaterials = outcome.materialDelta;
        state_.run.mining.combatProjectiles.clear();
        state_.run.mining.damageNumbers.clear();
        miningExtraction_.active = true;
        miningExtraction_.elapsed = 0.0;
        miningExtraction_.showUpgradeDraft = state_.run.surfaceExpedition.surfaceUpgradeOfferAvailable;
        state_.screen = Screen::Mining;
    }
    state_.statusLine = outcome.applied
        ? "Payload banked. Mini-drones returning to bay."
        : std::string(text::status::miningStowed);
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

bool recoveredSurfacePayloadThisAction(const SurfaceActionOutcome& outcome)
{
    return outcome.cargoDelta > 0
        || outcome.materialDelta.common > 0
        || outcome.materialDelta.rare > 0
        || outcome.materialDelta.exotic > 0
        || outcome.artifactFound;
}

} // namespace

void RocketGameApp::scanSurfacePulse()
{
    if (state_.screen != Screen::SurfaceScan) {
        return;
    }

    const SurfaceActionOutcome outcome = pulseSurfaceScan(state_, rng_);
    state_.statusLine = surfaceActionSummary(outcome);
    save();
    panelDirty_ = true;
}

void RocketGameApp::scanSurfaceBank()
{
    if (state_.screen != Screen::SurfaceScan) {
        return;
    }

    const SurfaceActionOutcome outcome = bankSurfaceScan(state_);
    if (outcome.applied && recoveredSurfacePayloadThisAction(outcome)) {
        generateSurfaceUpgradeOffers(state_, catalog_, rng_);
        if (state_.run.surfaceExpedition.surfaceUpgradeOfferAvailable) {
            state_.screen = Screen::SurfaceUpgrade;
        }
    }
    state_.statusLine = surfaceActionSummary(outcome);
    save();
    panelDirty_ = true;
}

void RocketGameApp::scanSurfaceAbort()
{
    if (state_.screen != Screen::SurfaceScan) {
        return;
    }

    const SurfaceActionOutcome outcome = abortSurfaceScan(state_);
    state_.statusLine = surfaceActionSummary(outcome);
    save();
    panelDirty_ = true;
}

void RocketGameApp::pushSurfaceStep()
{
    if (state_.screen != Screen::SurfacePush) {
        return;
    }

    const SurfaceActionOutcome outcome = pushSurfaceDepthStep(state_, rng_);
    state_.statusLine = surfaceActionSummary(outcome);
    save();
    panelDirty_ = true;
}

void RocketGameApp::pushSurfaceBank()
{
    if (state_.screen != Screen::SurfacePush) {
        return;
    }

    const SurfaceActionOutcome outcome = bankSurfacePush(state_);
    if (outcome.applied && recoveredSurfacePayloadThisAction(outcome)) {
        generateSurfaceUpgradeOffers(state_, catalog_, rng_);
        if (state_.run.surfaceExpedition.surfaceUpgradeOfferAvailable) {
            state_.screen = Screen::SurfaceUpgrade;
        }
    }
    state_.statusLine = surfaceActionSummary(outcome);
    save();
    panelDirty_ = true;
}

void RocketGameApp::pushSurfaceAbort()
{
    if (state_.screen != Screen::SurfacePush) {
        return;
    }

    const SurfaceActionOutcome outcome = abortSurfacePush(state_);
    state_.statusLine = surfaceActionSummary(outcome);
    save();
    panelDirty_ = true;
}

void RocketGameApp::miningAbort()
{
    if (state_.screen != Screen::Mining || miningExtraction_.active) {
        return;
    }
    if (miningAtReturnZone(state_.run.mining) && !state_.run.mining.failurePending) {
        state_.statusLine = "Leave is available inside the ship zone.";
        panelDirty_ = true;
        return;
    }

    const SurfaceActionOutcome outcome = finishMiningRun(state_, catalog_, true);
    if (outcome.applied && hasRecoveredSurfacePayload(state_.run.surfaceExpedition)) {
        generateSurfaceUpgradeOffers(state_, catalog_, rng_);
        if (state_.run.surfaceExpedition.surfaceUpgradeOfferAvailable) {
            state_.screen = Screen::SurfaceUpgrade;
        }
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
        if (state_.run.surfaceExpedition.surfaceUpgradeOfferAvailable) {
            state_.screen = Screen::SurfaceUpgrade;
        }
    }
    state_.statusLine = outcome.applied ? surfaceActionSummary(outcome) : std::string(text::status::miningAborted);
    save();
    panelDirty_ = true;
}

void RocketGameApp::debugStartFlyby()
{
    beginDebugSandbox("Debug flyby sandbox. No progress, rewards, or save data will be written.");
    state_.run.destinationIndex = destinationIndexForId(catalog_, content::destination::moon);
    state_.run.arrivalOps = {true, content::destination::moon};
    state_.lastOutcome.type = LaunchResultType::MissionComplete;
    state_.lastOutcome.recoveryMethod = RecoveryMethod::TransferArrival;
    state_.lastOutcome.destinationId = content::destination::moon;
    state_.lastOutcome.frontierTransfer = true;
    startArrivalFlybyRun(state_, catalog_);
    state_.statusLine = "Debug flyby sandbox. Fly this approach without touching your save.";
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugStartOrbit()
{
    beginDebugSandbox("Debug orbit sandbox. No progress, rewards, or save data will be written.");
    state_.run.destinationIndex = destinationIndexForId(catalog_, content::destination::moon);
    state_.run.arrivalOps = {true, content::destination::moon};
    state_.lastOutcome.type = LaunchResultType::MissionComplete;
    state_.lastOutcome.recoveryMethod = RecoveryMethod::TransferArrival;
    state_.lastOutcome.destinationId = content::destination::moon;
    state_.lastOutcome.frontierTransfer = true;
    setDestinationHistory(state_.meta.destinationFlybys, catalog_, content::destination::moon, 1);
    startArrivalOrbitRun(state_, catalog_);
    state_.statusLine = "Debug orbit sandbox. Hold the research band without touching your save.";
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugStartMining()
{
    debugStartMiningArena(1, 7, 0xA17E5701ULL, 0);
}

void RocketGameApp::debugStartCombatMining()
{
    debugStartMiningArena(2, 7, 0xC0BA7701ULL, 0);
}

void RocketGameApp::debugStartMiningArena(int act, int difficulty, std::uint64_t seed, int loadoutMode)
{
    const MiningAct miningAct = act <= 1
        ? MiningAct::ActOne
        : (act == 2 ? MiningAct::ActTwo : MiningAct::ActThree);
    const MiningArenaRequest request {
        miningAct,
        std::clamp(difficulty, 1, 10),
        std::max<std::uint64_t>(1, seed)
    };
    const MiningArenaRules rules = resolveMiningArenaRules(request);

    beginDebugSandbox("Mining Arena Lab sandbox. No payload, materials, or save data will be written.");
    state_.seed = request.seed;

    std::string_view destinationId = content::destination::moon;
    if (miningAct == MiningAct::ActOne) {
        if (request.difficulty >= 7) {
            destinationId = content::destination::outerPlanets;
            state_.meta.chapter = request.difficulty >= 9 ? GameChapter::Straylight : GameChapter::Breakthrough;
        } else if (request.difficulty >= 4) {
            destinationId = content::destination::mars;
            state_.meta.chapter = GameChapter::RedFrontier;
        } else {
            state_.meta.chapter = GameChapter::LunarProgram;
        }
    } else {
        destinationId = miningAct == MiningAct::ActTwo
            ? std::string_view(content::destination::nearbyStar)
            : std::string_view(content::destination::nearbyGalaxy);
        state_.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
        state_.meta.ark.condition = ArkCondition::DamagedStranded;
        state_.meta.ark.gravityWellDisaster = true;
        state_.meta.chapter = miningAct == MiningAct::ActTwo
            ? (request.difficulty <= 3 ? GameChapter::Arkfall : GameChapter::LastCampfire)
            : (request.difficulty <= 4
                ? GameChapter::VoidCompass
                : (request.difficulty <= 8 ? GameChapter::Ouroboros : GameChapter::Ascent));
        addDebugUnlock(state_, content::unlock::deepSpace);
        addDebugUnlock(state_, content::unlock::perimeterDrones);
    }

    state_.run.destinationIndex = destinationIndexForId(catalog_, destinationId);
    SurfaceExpeditionState& expedition = state_.run.surfaceExpedition;
    expedition = {};
    expedition.active = true;
    expedition.destinationId = std::string(destinationId);
    expedition.siteProfile = rules.band == MiningProgressionBand::Learn
        ? SurfaceSiteProfile::SurveyBasin
        : (rules.band == MiningProgressionBand::Combine ? SurfaceSiteProfile::OreShelf : SurfaceSiteProfile::FractureField);
    expedition.supply = tuning::research::baseSupply + act;
    expedition.sharedFuelCapacity = tuning::research::sharedFuelCapacity;
    expedition.sharedFuel = tuning::research::sharedFuelCapacity;
    expedition.hazard = tuning::research::baseHazard + static_cast<double>(request.difficulty - 1) * 0.02;
    expedition.enemyEncountersEnabled = miningAct != MiningAct::ActOne;
    expedition.miningSitePrepared = true;
    expedition.prospectArtifacts = rules.mechanics.artifactRecovery ? 1 : 0;

    const int normalizedLoadout = std::clamp(loadoutMode, 0, 2);
    if (normalizedLoadout == 1) {
        applyDebugDroneLoadout();
    } else if (normalizedLoadout == 0 && rules.referenceDrones.slots > 0) {
        seedDebugDroneLoadout();
        state_.meta.droneBaySlots = rules.referenceDrones.slots;
        state_.meta.equippedDroneIds.clear();
        if (rules.referenceDrones.maximumMark >= 2 && miningAct != MiningAct::ActOne) {
            addDebugUnlock(state_, content::unlock::perimeterCoordination);
        }
        for (std::size_t roleIndex = 0; roleIndex < rules.referenceDrones.roleCount; ++roleIndex) {
            const MiniDroneRole role = rules.referenceDrones.roles[roleIndex];
            const auto drone = std::find_if(catalog_.miniDrones.begin(), catalog_.miniDrones.end(), [role](const MiniDrone& candidate) {
                return candidate.role == role;
            });
            if (drone == catalog_.miniDrones.end()
                || state_.meta.equippedDroneIds.size() >= static_cast<std::size_t>(state_.meta.droneBaySlots)) {
                continue;
            }
            state_.meta.equippedDroneIds.push_back(drone->id);
            const auto upgrade = std::find_if(
                state_.meta.droneUpgrades.begin(),
                state_.meta.droneUpgrades.end(),
                [&](const DroneUpgradeRecord& record) { return record.droneId == drone->id; });
            if (upgrade != state_.meta.droneUpgrades.end()) {
                upgrade->level = std::max(1, rules.referenceDrones.maximumMark);
            }
        }
        ensureDroneBayState(state_, catalog_);
    } else {
        state_.meta.equippedDroneIds.clear();
    }

    const SurfaceActionOutcome outcome = startMiningRun(state_, catalog_, request, false);
    state_.statusLine = outcome.applied
        ? "Mining Arena Lab: Act " + std::to_string(static_cast<int>(request.act))
            + " level " + std::to_string(request.difficulty)
            + ", seed " + std::to_string(request.seed)
            + ". Sandbox rewards are not saved."
        : surfaceActionSummary(outcome);
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

std::string RocketGameApp::debugMiningArenaPreview(int act, int difficulty) const
{
    const MiningAct miningAct = act <= 1
        ? MiningAct::ActOne
        : (act == 2 ? MiningAct::ActTwo : MiningAct::ActThree);
    const MiningArenaRules rules = resolveMiningArenaRules({miningAct, std::clamp(difficulty, 1, 10), 1});
    const auto joinNames = [](const std::vector<std::string>& names) {
        if (names.empty()) {
            return std::string("None");
        }
        std::string joined = names.front();
        for (std::size_t index = 1; index < names.size(); ++index) {
            joined += ", " + names[index];
        }
        return joined;
    };
    std::vector<std::string> mechanics;
    const auto addMechanic = [&](bool enabled, std::string_view name) {
        if (enabled) {
            mechanics.emplace_back(name);
        }
    };
    addMechanic(rules.mechanics.movement, "movement");
    addMechanic(rules.mechanics.drilling, "drilling");
    addMechanic(rules.mechanics.returnZone, "return zone");
    addMechanic(rules.mechanics.fogAndScanner, "fog/scanner");
    addMechanic(rules.mechanics.oxygenAndFuel, "oxygen/fuel");
    addMechanic(rules.mechanics.drillHeat, "heat");
    addMechanic(rules.mechanics.drillIntegrity, "integrity");
    addMechanic(rules.mechanics.contactRebound, "rebound");
    addMechanic(rules.mechanics.fieldRepairs, "repairs");
    addMechanic(rules.mechanics.cargoDrag, "cargo drag");
    addMechanic(rules.mechanics.environmentalHazards, "hazards");
    addMechanic(rules.mechanics.artifactRecovery, "artifacts");
    addMechanic(rules.mechanics.artifactTethering, "tethering");
    addMechanic(rules.mechanics.passiveDroneCombat, "passive combat");

    std::vector<std::string> enemies;
    for (const MiningEnemyType enemy : {MiningEnemyType::Ant, MiningEnemyType::Flying, MiningEnemyType::Beetle, MiningEnemyType::Elemental, MiningEnemyType::Mammal, MiningEnemyType::Spawner}) {
        if (miningEnemyAllowed(rules, enemy)) {
            enemies.emplace_back(miningEnemyTypeName(enemy));
        }
    }
    std::vector<std::string> affinities;
    for (const MiningElementalAffinity affinity : {MiningElementalAffinity::Thermal, MiningElementalAffinity::Cryo, MiningElementalAffinity::Toxic, MiningElementalAffinity::Radiation}) {
        if (miningAffinityAllowed(rules, affinity)) {
            affinities.emplace_back(miningElementalAffinityName(affinity));
        }
    }
    std::vector<std::string> rooms;
    for (const MiningCellFeature feature : {MiningCellFeature::MainTunnel, MiningCellFeature::BranchTunnel, MiningCellFeature::EncounterZone, MiningCellFeature::TreasureVault, MiningCellFeature::HiveNest, MiningCellFeature::MinibossLair, MiningCellFeature::OrganicBurrow, MiningCellFeature::BossChamber}) {
        if (miningRoomFeatureAllowed(rules, feature)) {
            rooms.emplace_back(miningCellFeatureName(feature));
        }
    }

    std::ostringstream preview;
    preview << miningActName(rules.request.act)
            << " • Level " << rules.request.difficulty
            << " • " << miningProgressionBandName(rules.band)
            << " • Ruleset v" << miningArenaRulesVersion
            << "\nTutorial: " << rules.tutorialCallout
            << "\nNew: " << rules.complication
            << "\nMechanics: " << joinNames(mechanics)
            << "\nEnemies: " << joinNames(enemies)
            << "\nAffinities: " << joinNames(affinities)
            << "\nRooms: " << joinNames(rooms)
            << "\nRich cap: " << rules.rewardBudget.rareCap << " rare • "
            << rules.rewardBudget.exoticCap << " exotic"
            << "\nCounter: " << rules.recommendedCounters;
    return preview.str();
}

void RocketGameApp::debugStartSurfaceScan()
{
    beginDebugSandbox("Debug scanner sandbox. No materials, artifacts, or save data will be written.");
    state_.run.destinationIndex = destinationIndexForId(catalog_, content::destination::moon);
    state_.run.surfaceExpedition = {};
    state_.run.surfaceExpedition.active = true;
    state_.run.surfaceExpedition.destinationId = content::destination::moon;
    state_.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::FractureField;
    state_.run.surfaceExpedition.supply = tuning::research::baseSupply;
    state_.run.surfaceExpedition.sharedFuelCapacity = tuning::research::sharedFuelCapacity;
    state_.run.surfaceExpedition.sharedFuel = tuning::research::sharedFuelCapacity;
    state_.run.surfaceExpedition.hazard = tuning::research::baseHazard + 0.05;
    const SurfaceActionOutcome outcome = startSurfaceScanRun(state_, rng_);
    state_.statusLine = outcome.applied
        ? "Debug scanner sandbox. Pulse, bank, or bust without touching your save."
        : surfaceActionSummary(outcome);
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugStartSurfacePush()
{
    beginDebugSandbox("Debug Push Deeper sandbox. No materials, artifacts, or save data will be written.");
    state_.run.destinationIndex = destinationIndexForId(catalog_, content::destination::moon);
    state_.run.surfaceExpedition = {};
    state_.run.surfaceExpedition.active = true;
    state_.run.surfaceExpedition.destinationId = content::destination::moon;
    state_.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::OreShelf;
    state_.run.surfaceExpedition.supply = tuning::research::baseSupply;
    state_.run.surfaceExpedition.sharedFuelCapacity = tuning::research::sharedFuelCapacity;
    state_.run.surfaceExpedition.sharedFuel = tuning::research::sharedFuelCapacity;
    state_.run.surfaceExpedition.hazard = tuning::research::baseHazard + 0.08;
    const SurfaceActionOutcome outcome = startSurfacePushRun(state_, rng_);
    state_.statusLine = outcome.applied
        ? "Debug Push Deeper sandbox. Descend, bank, or collapse without touching your save."
        : surfaceActionSummary(outcome);
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugShowHangar()
{
    beginDebugSandbox("Debug Hangar board. No save data will be written.");
    state_.screen = Screen::Hangar;
    state_.run.credits = std::max(state_.run.credits, 180.0);
    state_.statusLine = "Debug Hangar board. Inspect compact ops cards without touching your save.";
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugShowResults()
{
    beginDebugSandbox("Debug Debrief board. No save data will be written.");
    state_.run.destinationIndex = destinationIndexForId(catalog_, content::destination::mars);
    state_.lastOutcome = debugTransferOutcome(content::destination::mars);
    state_.screen = Screen::Results;
    state_.statusLine = "Debug Debrief board. Inspect result cards without touching your save.";
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugShowArrivalOps()
{
    beginDebugSandbox("Debug Arrival Ops board. No save data will be written.");
    state_.run.destinationIndex = destinationIndexForId(catalog_, content::destination::mars);
    state_.lastOutcome = debugTransferOutcome(content::destination::mars);
    startArrivalOps(state_, state_.lastOutcome);
    state_.screen = Screen::ArrivalOps;
    state_.statusLine = "Debug Arrival Ops board. Inspect approach cards without touching your save.";
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugShowResearch()
{
    beginDebugSandbox("Debug Research board. No save data will be written.");
    state_.run.destinationIndex = destinationIndexForId(catalog_, content::destination::mars);
    state_.run.arrivalOps = {true, content::destination::mars};
    seedDebugResearchAccess(state_);
    generateResearchProjects(state_, catalog_, rng_);
    state_.screen = Screen::Research;
    state_.statusLine = "Debug Research board. Inspect project cards without touching your save.";
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugShowSurfaceUpgrade()
{
    beginDebugSandbox("Debug Surface Upgrade board. No save data will be written.");
    seedDebugResearchAccess(state_);
    seedDebugSurfaceExpedition(state_, catalog_, rng_, content::destination::mars);
    generateSurfaceUpgradeOffers(state_, catalog_, rng_);
    state_.screen = Screen::SurfaceUpgrade;
    state_.statusLine = "Debug Surface Upgrade board. Inspect draft cards without touching your save.";
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugShowDroneOps()
{
    beginDebugSandbox("Debug Drone Ops board. No save data will be written.");
    seedDebugResearchAccess(state_);
    seedDebugSurfaceExpedition(state_, catalog_, rng_, content::destination::nearbyStar);
    applyDebugDroneLoadout();
    state_.screen = Screen::DroneOps;
    state_.statusLine = "Debug Drone Ops. All 6 slots and drone types are available; this loadout carries into Mining and Combat Mining.";
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugShowNavigation()
{
    beginDebugSandbox("Debug Navigation board. No save data will be written.");
    addDebugUnlock(state_, content::unlock::deepSpace);
    addDebugUnlock(state_, content::unlock::perimeterDrones);
    state_.meta.ark.gravityWellDisaster = true;
    state_.meta.ark.condition = ArkCondition::DamagedStranded;
    state_.meta.ark.hullDamage = std::max(state_.meta.ark.hullDamage, 72);
    state_.meta.ark.fuelReserve = std::max(state_.meta.ark.fuelReserve, tuning::ark::hostileSystemFuelReserve);
    state_.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state_.meta.navigation.currentSystemId = "hostile_system";
    state_.meta.navigation.arkLocationId = "gravity_well";
    state_.meta.navigation.discoveredDestinationIds = {
        content::destination::nearbyStar,
        content::destination::nearbyGalaxy
    };
    state_.meta.navigation.selectedDestinationId = content::destination::nearbyStar;
    state_.run.destinationIndex = destinationIndexForId(catalog_, content::destination::nearbyStar);
    state_.screen = Screen::Navigation;
    state_.statusLine = "Debug Navigation board. Inspect sortie cards without touching your save.";
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugStartActOneFlow()
{
    beginDebugSandbox("Debug Act 1 flow. No save data will be written.");
    debugActOneCheckpoint_ = 0;
    applyDebugActOneCheckpoint();
}

void RocketGameApp::debugPreviousActOneCheckpoint()
{
    if (!debugSessionActive_ || debugActOneCheckpoint_ < 0) {
        debugStartActOneFlow();
        return;
    }
    debugActOneCheckpoint_ = std::max(0, debugActOneCheckpoint_ - 1);
    applyDebugActOneCheckpoint();
}

void RocketGameApp::debugNextActOneCheckpoint()
{
    if (!debugSessionActive_ || debugActOneCheckpoint_ < 0) {
        debugStartActOneFlow();
        return;
    }
    debugActOneCheckpoint_ = std::min(
        static_cast<int>(kDebugActOneCheckpoints.size()) - 1,
        debugActOneCheckpoint_ + 1);
    applyDebugActOneCheckpoint();
}

int RocketGameApp::debugActOneCheckpoint() const
{
    return debugActOneCheckpoint_;
}

void RocketGameApp::applyDebugActOneCheckpoint()
{
    debugActOneCheckpoint_ = std::clamp(
        debugActOneCheckpoint_,
        0,
        static_cast<int>(kDebugActOneCheckpoints.size()) - 1);
    const DebugActOneCheckpoint& checkpoint = kDebugActOneCheckpoints[static_cast<std::size_t>(debugActOneCheckpoint_)];

    session_ = {};
    clearResearchAndExpeditionState(state_);
    state_.lastOutcome = debugActOneCheckpoint_ == 0
        ? LaunchOutcome{}
        : debugTransferOutcome(std::string(checkpoint.destinationId));
    state_.run.destinationIndex = destinationIndexForId(catalog_, checkpoint.destinationId);
    state_.meta.furthestTier = currentDestination(state_, catalog_).tier;
    state_.meta.ark = {};
    state_.meta.campaignMilestone = CampaignMilestone::SolarTutorial;
    state_.meta.chapter = debugActOneCheckpoint_ == 0
        ? GameChapter::ProvingGround
        : (debugActOneCheckpoint_ == 1
            ? GameChapter::LunarProgram
            : (debugActOneCheckpoint_ == 2 ? GameChapter::RedFrontier : GameChapter::Breakthrough));

    if (debugActOneCheckpoint_ == static_cast<int>(kDebugActOneCheckpoints.size()) - 1) {
        discoverArk(state_, catalog_);
    }

    if (debugActOneCheckpoint_ == 0) {
        state_.launchConfig.frontierTransfer = false;
        state_.launchConfig.destinationId = std::string(checkpoint.destinationId);
        state_.launchConfig.burnGoalMultiplier = defaultProvingTarget(currentDestination(state_, catalog_));
        syncLaunchConfig(state_, catalog_);
        beginLaunchSession(rocket::prepareLaunch(state_, catalog_, rng_));
        session_.flightArmed = true;
        state_.screen = Screen::Launch;
    } else {
        setDestinationHistory(state_.meta.destinationFlybys, catalog_, checkpoint.destinationId, 1);
        setDestinationHistory(state_.meta.destinationOrbits, catalog_, checkpoint.destinationId, 1);
        startArrivalOps(state_, state_.lastOutcome);
        state_.screen = Screen::ArrivalOps;
    }

    state_.statusLine = "Debug Act 1 "
        + std::to_string(debugActOneCheckpoint_ + 1)
        + "/"
        + std::to_string(kDebugActOneCheckpoints.size())
        + ": "
        + std::string(checkpoint.label)
        + ". Use Previous or Next to inspect the route.";
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugExit()
{
    if (!debugSessionActive_) {
        return;
    }
    debugSessionActive_ = false;
    debugActOneCheckpoint_ = -1;
    debugDroneLoadout_ = {};
    loadSavedGameOrDefault();
    state_.statusLine = "Debug sandbox closed. Real save restored from local mission control.";
    refreshPanel();
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
    consumeNextLaunchBoost();
    state_.screen = Screen::Launch;
    state_.statusLine = miningDroneTransferEnabled(state_)
        ? std::string(text::status::droneStowing)
        : std::string(text::status::preflightReadyWithoutDrone);
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
    if (state_.screen == Screen::Upgrade) {
        if (rocket::rerollOffers(state_, catalog_, rng_)) {
            save();
        }
        panelDirty_ = true;
        return;
    }

    if (state_.screen == Screen::SurfaceUpgrade) {
        if (rocket::rerollSurfaceUpgradeOffers(state_, catalog_, rng_)) {
            save();
        }
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
    debugSessionActive_ = false;
    clearBrowserSave();
    state_ = createNewGame(catalog_, 0x524F434B45544ULL);
    rng_ = Random(state_.seed);
    session_ = {};
    session_.returnTrip.duration = tuning::session::returnDefaultDuration;
    refreshPanel();
}

bool RocketGameApp::uiMouseMove(int x, int y)
{
    return rmlUi_.mouseMove(x, y);
}

bool RocketGameApp::uiMouseDown(int x, int y, int button)
{
    return rmlUi_.mouseDown(x, y, button);
}

bool RocketGameApp::uiMouseUp(int x, int y, int button)
{
    return rmlUi_.mouseUp(x, y, button);
}

bool RocketGameApp::uiMouseWheel(int x, int y, double deltaY)
{
    return rmlUi_.mouseWheel(x, y, deltaY);
}

bool RocketGameApp::uiHitTest(int x, int y) const
{
    return rmlUi_.hitTest(x, y);
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
    if (debugSessionActive_) {
        return;
    }
    storeBrowserSave(serializeSaveData(captureSaveData(state_)));
}

void RocketGameApp::refreshPanel()
{
    const PreparedLaunch flightModel = currentFlightModel();
    const std::string panelHtml = buildGamePanelHtml({
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
        session_.preflightElapsed >= tuning::session::preflightBoardingSeconds,
        miningDroneTransferEnabled(state_),
        debugActOneCheckpoint_,
    });
    setBrowserPanelHtml(panelHtml);
    rmlUi_.setPanelHtml(panelHtml);
    panelDirty_ = false;
}

void RocketGameApp::runUiAction(const std::string& action)
{
    int index = 0;
    if (consumeIndexedAction(action, ui::actions::buyOfferPrefix, index)) {
        buyOffer(index);
    } else if (consumeIndexedAction(action, ui::actions::researchProjectPrefix, index)) {
        selectResearchProject(index);
    } else if (consumeIndexedAction(action, ui::actions::surfaceUpgradePrefix, index)) {
        selectSurfaceUpgrade(index);
    } else if (consumeIndexedAction(action, ui::actions::equipDronePrefix, index)) {
        equipDrone(index);
    } else if (consumeIndexedAction(action, ui::actions::unequipDroneSlotPrefix, index)) {
        unequipDroneSlot(index);
    } else if (consumeIndexedAction(action, ui::actions::upgradeDronePrefix, index)) {
        upgradeDrone(index);
    } else if (consumeIndexedAction(action, ui::actions::selectNavigationDestinationPrefix, index)) {
        selectNavigationDestination(index);
    } else if (consumeIndexedAction(action, ui::actions::recruitCandidatePrefix, index)) {
        recruitCrew(index);
    } else if (action == ui::actions::prepareLaunch) {
        prepareForLaunch();
    } else if (action == ui::actions::startLaunch) {
        startLaunch();
    } else if (action == ui::actions::ejectNow) {
        ejectNow();
    } else if (action == ui::actions::returnHome) {
        returnHome();
    } else if (action == ui::actions::arrivalOps) {
        arrivalOps();
    } else if (action == ui::actions::skipArrivalFanfare) {
        skipArrivalFanfare();
    } else if (action == ui::actions::cutEngines) {
        cutEngines();
    } else if (action == ui::actions::pressureRelief) {
        pressureReliefValve();
    } else if (action == ui::actions::closeReliefValve) {
        closePressureReliefValve();
    } else if (action == ui::actions::jettisonCargo) {
        jettisonCargo();
    } else if (action == ui::actions::next) {
        next();
    } else if (action == ui::actions::attemptFrontier) {
        attemptFrontierTransfer();
    } else if (action == ui::actions::openNavigation) {
        openNavigation();
    } else if (action == ui::actions::arkJump) {
        arkJump();
    } else if (action == ui::actions::rerollOffers) {
        rerollOffers();
    } else if (action == ui::actions::arrivalFlyby) {
        runArrivalFlyby();
    } else if (action == ui::actions::flybyAbort) {
        flybyAbort();
    } else if (action == ui::actions::flybyContinue) {
        flybyContinue();
    } else if (action == ui::actions::arrivalOrbit) {
        enterArrivalOrbit();
    } else if (action == ui::actions::orbitAbort) {
        orbitAbort();
    } else if (action == ui::actions::orbitContinue) {
        orbitContinue();
    } else if (action == ui::actions::arrivalLanding) {
        attemptArrivalLanding();
    } else if (action == ui::actions::skipResearch) {
        skipResearch();
    } else if (action == ui::actions::surveySurface) {
        surveySurface();
    } else if (action == ui::actions::mineSurface) {
        mineSurface();
    } else if (action == ui::actions::pushSurface) {
        pushSurface();
    } else if (action == ui::actions::extractSurface) {
        extractSurface();
    } else if (action == ui::actions::surfaceScanPulse) {
        scanSurfacePulse();
    } else if (action == ui::actions::surfaceScanBank) {
        scanSurfaceBank();
    } else if (action == ui::actions::surfaceScanAbort) {
        scanSurfaceAbort();
    } else if (action == ui::actions::surfacePushStep) {
        pushSurfaceStep();
    } else if (action == ui::actions::surfacePushBank) {
        pushSurfaceBank();
    } else if (action == ui::actions::surfacePushAbort) {
        pushSurfaceAbort();
    } else if (action == ui::actions::droneOps) {
        openDroneOps();
    } else if (action == ui::actions::backToSurfaceOps) {
        backToSurfaceOps();
    } else if (action == ui::actions::upgradeDroneSlot) {
        upgradeDroneSlot();
    } else if (action == ui::actions::miningScanner) {
        miningScanner();
    } else if (action == ui::actions::miningTether) {
        miningTether();
    } else if (action == ui::actions::miningRepairDrill) {
        miningRepairDrill();
    } else if (action == ui::actions::miningRepairDrone) {
        miningRepairDrone();
    } else if (action == ui::actions::miningStow) {
        miningStow();
    } else if (action == ui::actions::miningAbort) {
        miningAbort();
    } else if (action == ui::actions::miningFailureAck) {
        miningFailureAck();
    } else if (action == ui::actions::repairShip) {
        repairShip();
    } else if (action == ui::actions::recruitCrew) {
        recruitCrew();
    } else if (action == ui::actions::trainCrew) {
        trainCrew();
    } else if (action == ui::actions::restCrew) {
        restCrew();
    } else if (action == ui::actions::resetSave) {
        resetSave();
    }
}

RenderSnapshot RocketGameApp::snapshot() const
{
    RenderSnapshot result;
    const PreparedLaunch flightModel = currentFlightModel();
    result.screen = state_.screen;
    result.lastResult = state_.screen == Screen::Results ? state_.lastOutcome.type : LaunchResultType::None;
    result.currentMultiplier = session_.currentMultiplier;
    result.animationTime = session_.result.elapsed;
    if (state_.screen == Screen::Launch) {
        result.animationTime = session_.flightArmed ? session_.elapsed : session_.preflightElapsed;
    } else if (state_.screen == Screen::Mining) {
        result.animationTime = miningExtraction_.active ? miningExtraction_.elapsed : state_.run.mining.elapsedSeconds;
    } else if (state_.screen == Screen::Flyby) {
        result.animationTime = state_.run.flyby.elapsedSeconds;
    } else if (state_.screen == Screen::Orbit) {
        result.animationTime = state_.run.orbit.elapsedSeconds;
    } else if (state_.screen == Screen::SurfaceScan) {
        result.animationTime = visualTimeSeconds_;
    } else if (state_.screen == Screen::SurfacePush) {
        result.animationTime = visualTimeSeconds_;
    } else if (state_.screen == Screen::ArrivalFanfare) {
        result.animationTime = session_.arrivalFanfare.elapsed;
    }
    const Destination& currentFrontier = currentDestination(state_, catalog_);
    const Destination* visualDestination = &currentFrontier;
    if (state_.screen == Screen::Launch) {
        if (const Destination* activeDestination = catalog_.findDestination(session_.preparedLaunch.config.destinationId)) {
            visualDestination = activeDestination;
        }
        result.frontierTransfer = session_.preparedLaunch.config.frontierTransfer;
    } else if (state_.screen == Screen::Results || state_.screen == Screen::ArrivalFanfare || state_.screen == Screen::ArrivalOps || state_.screen == Screen::Flyby || state_.screen == Screen::Orbit || state_.screen == Screen::SurfaceScan || state_.screen == Screen::SurfacePush) {
        if (const Destination* resultDestination = catalog_.findDestination(state_.lastOutcome.destinationId)) {
            visualDestination = resultDestination;
        }
        if (state_.screen == Screen::Flyby && !state_.run.flyby.destinationId.empty()) {
            if (const Destination* flybyDestination = catalog_.findDestination(state_.run.flyby.destinationId)) {
                visualDestination = flybyDestination;
            }
        }
        if (state_.screen == Screen::Orbit && !state_.run.orbit.destinationId.empty()) {
            if (const Destination* orbitDestination = catalog_.findDestination(state_.run.orbit.destinationId)) {
                visualDestination = orbitDestination;
            }
        }
        if (state_.screen == Screen::SurfaceScan && !state_.run.surfaceScan.destinationId.empty()) {
            if (const Destination* scanDestination = catalog_.findDestination(state_.run.surfaceScan.destinationId)) {
                visualDestination = scanDestination;
            }
        }
        if (state_.screen == Screen::SurfacePush && !state_.run.surfacePush.destinationId.empty()) {
            if (const Destination* pushDestination = catalog_.findDestination(state_.run.surfacePush.destinationId)) {
                visualDestination = pushDestination;
            }
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
    } else if (state_.screen == Screen::ArrivalFanfare || state_.screen == Screen::Flyby || state_.screen == Screen::Orbit || state_.screen == Screen::SurfaceScan || state_.screen == Screen::SurfacePush) {
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
    result.debugActOneCheckpoint = debugActOneCheckpoint_;
    result.arkCondition = state_.meta.ark.condition;
    result.preflightActive = state_.screen == Screen::Launch && !session_.flightArmed && miningDroneTransferEnabled(state_);
    result.preflightProgress = result.preflightActive
        ? std::clamp(session_.preflightElapsed / tuning::session::preflightBoardingSeconds, 0.0, 1.0)
        : 1.0;

    if (state_.screen == Screen::Mining && (state_.run.mining.active || miningExtraction_.active)) {
        const MiningRunState& mining = state_.run.mining;
        result.miningExtractionActive = miningExtraction_.active;
        result.miningExtractionProgress = miningExtraction_.active
            ? std::clamp(miningExtraction_.elapsed / tuning::mining::miningExtractionSequenceSeconds, 0.0, 1.0)
            : 0.0;
        result.miningWidth = mining.terrain.width;
        result.miningHeight = mining.terrain.height;
        result.miningDroneX = mining.droneX;
        result.miningDroneY = mining.droneY;
        result.miningTargetX = mining.targetTipX;
        result.miningTargetY = mining.targetTipY;
        result.miningDrillDirX = mining.aimDirX;
        result.miningDrillDirY = mining.aimDirY;
        result.miningHeat = mining.drillHeat;
        result.miningOxygenSeconds = mining.oxygenSeconds;
        result.miningFuelCycleProgress = mining.fuelCycleProgress;
        result.miningDrillIntegrity = mining.drillIntegrity;
        result.miningDroneHealth = mining.droneHealth;
        result.miningReturnZoneX = mining.returnZoneX;
        result.miningReturnZoneY = mining.returnZoneY;
        result.miningAtReturnZone = miningAtReturnZone(mining);
        const MiningLoadStats loadStats = miningLoadStats(state_, catalog_);
        result.miningLoad = loadStats.currentLoad;
        result.miningLoadSpeedMultiplier = loadStats.speedMultiplier;
        result.miningLoadFuelConsumptionMultiplier = loadStats.fuelConsumptionMultiplier;
        result.miningHazardDelta = mining.hazardDelta;
        result.miningContactIntensity = mining.contactIntensity;
        result.miningScannerPulse = mining.scannerPulseSeconds;
        const MiningDrillStats miningStats = miningDrillStats(state_, catalog_);
        result.miningScannerRadius = miningStats.scannerRadius;
        result.miningBounceRelief = miningStats.hardRockBounceRelief;
        result.miningFailurePulse = mining.failurePending ? std::max(0.25, std::clamp(mining.failureSeconds / 1.5, 0.0, 1.0)) : 0.0;
        result.miningRecoilX = mining.recoilX;
        result.miningRecoilY = mining.recoilY;
        result.miningMoveX = mining.moveX;
        result.miningMoveY = mining.moveY;
        result.miningHullDirX = mining.hullDirX;
        result.miningHullDirY = mining.hullDirY;
        const MiningCell* target = miningCellAt(mining.terrain, mining.targetCellX, mining.targetCellY);
        const bool targetDrillable = target != nullptr && miningMaterialSolid(target->material) && target->material != MiningCellMaterial::Bedrock;
        result.miningBounce = mining.contactBounce;
        result.miningInputDrilling = mining.drilling;
        result.miningTargetDrillable = targetDrillable;
        result.miningDrilling = mining.drilling && targetDrillable;
        result.miningSharedFuel = state_.run.surfaceExpedition.sharedFuel;
        result.miningSharedFuelCapacity = state_.run.surfaceExpedition.sharedFuelCapacity;
        result.miningCargo = mining.cargo;
        result.miningStowedCargo = mining.stowedCargo;
        const MiniDroneLoadoutEffects droneEffects = miniDroneLoadoutEffects(state_, catalog_);
        result.miningSynergyCount = static_cast<int>(droneEffects.synergyNames.size());
        result.miningSignatureTier = droneEffects.signatureTier;
        result.miningSignatureStyle = static_cast<int>(droneEffects.signatureKind);
        for (const std::string& droneId : state_.meta.equippedDroneIds) {
            const MiniDrone* drone = catalog_.findMiniDrone(droneId);
            if (drone == nullptr) {
                continue;
            }
            result.miningSupportDroneCount += 1;
            if (miniDroneUpgradeLevel(state_, droneId) > 1) {
                result.miningTunedDroneCount += 1;
            }
            result.miningDroneRoles.push_back(static_cast<int>(drone->role));
            result.miningDroneUpgradeLevels.push_back(miniDroneUpgradeLevel(state_, droneId));
            if (drone->role == MiniDroneRole::Attack) {
                result.miningAttackDroneCount += 1;
            } else if (drone->role == MiniDroneRole::Defense) {
                result.miningDefenseDroneCount += 1;
            }
        }
        result.miningMiniDrones.reserve(mining.miniDrones.size());
        for (const MiningMiniDroneAgent& agent : mining.miniDrones) {
            result.miningMiniDrones.push_back({
                agent.x,
                agent.y,
                static_cast<int>(agent.role),
                agent.upgradeLevel,
                static_cast<int>(agent.behavior),
                agent.targetCellX,
                agent.targetCellY,
                agent.targetEnemyIndex,
                agent.surveyPulseSeconds,
                agent.defenseAngleRadians,
                agent.shieldCharge,
                agent.shieldRechargeSeconds,
                agent.shieldImpactSeconds,
                agent.haulMaterials
            });
        }
        result.miningMaterials = mining.temporaryMaterials;
        result.miningStowedMaterials = mining.stowedMaterials;
        if (mining.artifact.present) {
            result.miningArtifact = {
                true,
                mining.artifact.x,
                mining.artifact.y,
                mining.artifact.health,
                mining.artifact.maxHealth,
                static_cast<int>(mining.artifact.kind),
                static_cast<int>(mining.artifact.rewardType),
                static_cast<int>(mining.artifact.state),
                mining.artifact.tethered,
                mining.artifact.revealed
            };
        }
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
                    cell.hazard,
                    static_cast<int>(cell.hazardAffinity)
                });
            }
        }
        result.miningEnemies.reserve(mining.enemies.size());
        for (const MiningEnemy& enemy : mining.enemies) {
            if (!enemy.active) {
                continue;
            }
            result.miningEnemies.push_back({
                enemy.x,
                enemy.y,
                static_cast<int>(enemy.type),
                static_cast<int>(enemy.affinity),
                enemy.health,
                enemy.maxHealth,
                enemy.effectRadius,
                enemy.attackCooldownSeconds,
                enemy.active,
                enemy.spawn.spawned,
                enemy.spawn.maxSpawns,
                enemy.spawn.cooldownSeconds,
                enemy.spawn.intervalSeconds
            });
        }
        result.miningProjectiles.reserve(mining.combatProjectiles.size());
        for (const MiningProjectileVisual& projectile : mining.combatProjectiles) {
            result.miningProjectiles.push_back({
                projectile.startX,
                projectile.startY,
                projectile.endX,
                projectile.endY,
                projectile.age,
                projectile.lifetime,
                static_cast<int>(projectile.team),
                static_cast<int>(projectile.sourceType),
                static_cast<int>(projectile.affinity),
                projectile.critical
            });
        }
        result.miningDamageNumbers.reserve(mining.damageNumbers.size());
        for (const MiningDamageNumber& number : mining.damageNumbers) {
            result.miningDamageNumbers.push_back({
                number.x,
                number.y,
                number.amount,
                number.age,
                number.lifetime,
                static_cast<int>(number.team),
                static_cast<int>(number.kind),
                number.critical,
                number.rigDamage
            });
        }
    }

    if (state_.screen == Screen::Flyby && state_.run.flyby.active) {
        const FlybyRunState& flyby = state_.run.flyby;
        result.flybyActive = true;
        result.flybyCompleted = flyby.completed;
        result.flybyZone = flyby.currentZone;
        result.flybyResult = static_cast<int>(flyby.result);
        result.flybyElapsed = flyby.elapsedSeconds;
        result.flybyDuration = flyby.durationSeconds;
        result.flybyShipX = flyby.shipX;
        result.flybyShipY = flyby.shipY;
        result.flybyVelocityX = flyby.velocityX;
        result.flybyVelocityY = flyby.velocityY;
        result.flybyInputX = flyby.inputX;
        result.flybyInputY = flyby.inputY;
        result.flybyDestinationX = tuning::flyby::destinationX;
        result.flybyDestinationY = tuning::flyby::destinationY;
        result.flybyIdealRadius = tuning::flyby::idealRadius;
        result.flybyGoodBand = tuning::flyby::goodBand;
        result.flybyPerfectBand = tuning::flyby::perfectBand;
        result.flybyTrailPoints.reserve(flyby.trailPoints.size());
        for (const FlybyTrailPoint& point : flyby.trailPoints) {
            result.flybyTrailPoints.push_back({point.x, point.y});
        }
    }

    if (state_.screen == Screen::Orbit && state_.run.orbit.active) {
        const OrbitRunState& orbit = state_.run.orbit;
        result.orbitActive = true;
        result.orbitCompleted = orbit.completed;
        result.orbitZone = orbit.currentZone;
        result.orbitResult = static_cast<int>(orbit.result);
        result.orbitElapsed = orbit.elapsedSeconds;
        result.orbitDuration = orbit.durationSeconds;
        result.orbitProgress = orbit.orbitProgress;
        result.orbitShipX = orbit.shipX;
        result.orbitShipY = orbit.shipY;
        result.orbitVelocityX = orbit.velocityX;
        result.orbitVelocityY = orbit.velocityY;
        result.orbitInputX = orbit.inputX;
        result.orbitInputY = orbit.inputY;
        result.orbitPlanetRadius = orbit.planetRadius;
        result.orbitTargetRadius = orbit.targetRadius;
        result.orbitGoodBand = orbit.goodBand;
        result.orbitPerfectBand = orbit.perfectBand;
        result.orbitTrailPoints.reserve(orbit.trailPoints.size());
        for (const FlybyTrailPoint& point : orbit.trailPoints) {
            result.orbitTrailPoints.push_back({point.x, point.y});
        }
    }

    if (state_.screen == Screen::SurfaceScan && (state_.run.surfaceScan.active || state_.run.surfaceScan.completed)) {
        const SurfaceScanRunState& scan = state_.run.surfaceScan;
        result.surfaceScanActive = scan.active;
        result.surfaceScanCompleted = scan.completed;
        result.surfaceScanBusted = scan.busted;
        result.surfaceScanPulses = scan.pulses;
        result.surfaceScanMaxPulses = std::max(1, scan.maxPulses);
        result.surfaceScanSignal = scan.signal;
        result.surfaceScanInterference = scan.interference;
        result.surfaceScanBustRisk = scan.bustRisk;
        result.surfaceScanCargo = scan.cargo;
        result.surfaceScanMaterials = scan.temporaryMaterials;
        result.surfaceScanArtifacts = static_cast<int>(scan.temporaryArtifacts.size());
        for (const SurfaceDepthProspect& prospect : scan.depthProspects) {
            appendProspectMarkers(result.surfaceScanPreviewMarkers, result.surfaceScanPreviewDepthOffsets, prospect);
        }
    }

    if (state_.screen == Screen::SurfacePush && (state_.run.surfacePush.active || state_.run.surfacePush.completed)) {
        const SurfacePushRunState& push = state_.run.surfacePush;
        result.surfacePushActive = push.active;
        result.surfacePushCompleted = push.completed;
        result.surfacePushBusted = push.busted;
        result.surfacePushSteps = push.steps;
        result.surfacePushMaxSteps = std::max(1, push.maxSteps);
        result.surfacePushDepthGain = push.depthGain;
        result.surfacePushPressure = push.pressure;
        result.surfacePushCollapseRisk = push.collapseRisk;
        result.surfacePushCargo = push.cargo;
        result.surfacePushMaterials = push.temporaryMaterials;
        result.surfacePushArtifacts = static_cast<int>(push.temporaryArtifacts.size());
        result.surfacePushRewardMarkers = push.rewardMarkers;
        result.surfacePushRewardDepthOffsets = push.rewardMarkerDepthOffsets;
        for (const SurfaceDepthProspect& prospect : state_.run.surfaceExpedition.depthProspects) {
            appendProspectMarkers(result.surfacePushForecastMarkers, result.surfacePushForecastDepthOffsets, prospect);
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
